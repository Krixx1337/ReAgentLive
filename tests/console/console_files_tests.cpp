// reagentlive console tests: the real console against the fake engine
// (fake_engine.h) for the verbs that write files or read code: disasm with
// save, labels, run, rows and assets-export, layout-export, owner and route,
// and the console's ceilings (--packs, --context-limit).
#include "fake_engine.h"
#include <chrono>
#include <format>
#include <limits>
#include <map>
#include <string_view>

#include "console_support.h"

using namespace fake;
using namespace console_support;

namespace {
// Game.exe (fake_engine.h): PE32+ headers, .text at 0x1000 and an exception
// directory of one-byte entries over real code, the decoys a packed image
// lists: a function's start (0x1000) and a chained chunk (0x1200); and a
// switch (0x1400) whose targets lie in its own entry, a chained chunk
// (0x1500) that also holds its table, code no entry lists (0x1600) and
// undecodable bytes (0x1610); a call chain (0x1700) and a function calling
// one more than the callee cap (0x1800), and a function calling the one
// behind the decoy (0x1b00).
uint64_t TheCode() {
    static const uint64_t base = [] {
        auto& image = TheImage();
        std::fill(image.bytes + 0x1000, image.bytes + 0x5000, uint8_t{0xcc});
        image.Put32(0x3c, 0x80);
        image.Put32(0x80, 0x00004550);
        image.Put(0x86, {2, 0});            // NumberOfSections.
        image.Put32(0x88, 0x5f5e100);       // TimeDateStamp.
        image.Put(0x94, {0xf0, 0});         // SizeOfOptionalHeader.
        image.Put(0x98, {0x0b, 0x02});
        image.Put32(0x98 + 108, 16);
        image.Put32(0x80 + 160, 0x5000);    // Exception directory.
        image.Put32(0x80 + 164, 11 * 12);
        const uint64_t sections = 0x98 + 0xf0;
        image.Put(sections, {'.', 't', 'e', 'x', 't', 0, 0, 0});
        image.Put32(sections + 8, 0x4000);
        image.Put32(sections + 12, 0x1000);
        image.Put32(sections + 36, 0x60000020);
        image.Put(sections + 40, {'.', 'r', 'd', 'a', 't', 'a', 0, 0});
        image.Put32(sections + 48, 0x3000);
        image.Put32(sections + 52, 0x5000);
        image.Put32(sections + 76, 0x40000040);
        const auto entry = [&](uint32_t index, uint32_t begin, uint32_t end, uint32_t unwind) {
            image.Put32(0x5000 + index * 12, begin);
            image.Put32(0x5000 + index * 12 + 4, end);
            image.Put32(0x5000 + index * 12 + 8, unwind);
        };
        entry(0, 0x1000, 0x1001, 0x6000);             // Decoy over a function's start.
        entry(1, 0x1100, 0x1140, 0x6010);
        entry(2, 0x1200, 0x1201, 0x5000 + 1 * 12 + 1); // A decoy chunk, chained to entry 1.
        entry(3, 0x1400, 0x1480, 0x6020);
        entry(4, 0x1500, 0x1520, 0x5000 + 3 * 12 + 1); // The switch's chained chunk, holding its table.
        uint32_t index = 5;                            // The call chain's functions.
        for (const auto& [begin, end] : {std::pair{0x1700, 0x1706}, {0x1720, 0x172a}, {0x1740, 0x1746}, {0x1760, 0x1766},
                                         {0x1780, 0x1786}, {0x17a0, 0x17a1}})
            entry(index++, begin, end, 0x6000);
        image.Put(0x6020, {0x01, 0x04, 0x00, 0x00});
        // cmp ecx, 4; ja 0x1478; lea rdx, [image base]; movsxd rax, ecx; mov ecx, [rdx + rax*4 + 0x1508];
        // add rcx, rdx; jmp rcx; then two cases (mov eax, N; ret) and the default at 0x1478; the table at
        // 0x1508, after the chained chunk's code.
        image.Put(0x1400, {0x83, 0xf9, 0x04, 0x0f, 0x87, 0x6f, 0, 0, 0, 0x48, 0x8d, 0x15, 0xf0, 0xeb, 0xff, 0xff,
                           0x48, 0x63, 0xc1, 0x8b, 0x8c, 0x82, 0x08, 0x15, 0, 0, 0x48, 0x03, 0xca, 0xff, 0xe1,
                           0xb8, 0, 0, 0, 0, 0xc3, 0xb8, 0x01, 0, 0, 0, 0xc3});
        uint32_t slot = 0x1508;
        for (const uint32_t target : {0x141f, 0x1425, 0x1500, 0x1600, 0x1610}) image.Put32(std::exchange(slot, slot + 4), target);
        image.Put(0x1478, {0x33, 0xc0, 0xc3});
        image.Put(0x1500, {0xb8, 0x02, 0, 0, 0, 0xc3});
        image.Put(0x1600, {0xb8, 0x03, 0, 0, 0, 0xc3});
        image.Put(0x1610, {0x06});
        image.Put(0x6000, {0x01, 0x04, 0x00, 0x00});
        image.Put(0x6010, {0x01, 0x04, 0x00, 0x00});
        // sub rsp, 0x28; mov eax, 1; add rsp, 0x28; ret
        image.Put(0x1000, {0x48, 0x83, 0xec, 0x28, 0xb8, 0x01, 0, 0, 0, 0x48, 0x83, 0xc4, 0x28, 0xc3});
        image.Put(0x1100, {0x48, 0x83, 0xec, 0x28, 0x48, 0x83, 0xc4, 0x28, 0xc3});
        // mov rax, rcx; add rsp, 0x28; ret
        image.Put(0x1200, {0x48, 0x8b, 0xc1, 0x48, 0x83, 0xc4, 0x28, 0xc3});
        image.Put(0x1300, {0x06});          // Undecodable in 64-bit mode.
        // A call chain, each function in its entry: 0x1700 calls 0x1720, which calls 0x1740 and
        // tail-jumps to 0x1760; 0x1740 calls 0x1780, which calls 0x17a0.
        image.Put(0x1700, {0xe8, 0x1b, 0, 0, 0, 0xc3});
        image.Put(0x1720, {0xe8, 0x1b, 0, 0, 0, 0xe9, 0x36, 0, 0, 0});
        image.Put(0x1740, {0xe8, 0x3b, 0, 0, 0, 0xc3});
        image.Put(0x1760, {0xb8, 0x05, 0, 0, 0, 0xc3});
        image.Put(0x1780, {0xe8, 0x1b, 0, 0, 0, 0xc3});
        image.Put(0x17a0, {0xc3});
        // 0x1800 calls 17 one-byte functions (0x1900, 0x1910, ...): one more than the cap.
        for (uint32_t i = 0; i < 17; ++i) {
            const uint32_t at = 0x1800 + i * 5, target = 0x1900 + i * 0x10;
            image.Put(at, {0xe8});
            image.Put32(at + 1, target - (at + 5));
            image.Put(target, {0xc3});
        }
        image.Put(0x1800 + 17 * 5, {0xc3});
        image.Put(0x1b00, {0xe8, 0xfb, 0xf4, 0xff, 0xff, 0xc3});   // call 0x1000 (behind its decoy); ret
        std::fill(image.bytes + 0x3000, image.bytes + 0x3100, uint8_t{0x90}); // Nops: no signature is unique.
        return image.Base();
    }();
    return base;
}
std::string At(uint32_t rva) { return std::format("0x{:x}", TheCode() + rva); }
std::string SaveFile(const char* name) {
    return (std::filesystem::temp_directory_path() / std::format("reagentlive-fake-{}-{}", GetCurrentProcessId(), name)).string();
}
}

TEST_CASE("disasm with save walks past a one-byte decoy entry and refuses an empty listing") {
    // A decoy at the function's start: walked to its return and saved.
    const auto file = SaveFile("decoy.jsonl");
    const auto saved = Result(CallText("disasm", {{"addr", At(0x1000)}, {"whole", true}, {"save", {{"file", file}}}}));
    CHECK(saved["saved_rows"] == 4);
    CHECK(saved["stop"] == "walk end");
    CHECK(std::filesystem::file_size(file) > 0);
    // view.cursor in a one-byte chunk: walked too.
    const auto resume = [&](uint32_t rva) {
        return Json{{"addr", At(rva)}, {"whole", true}, {"view", {{"cursor", At(rva)}}}, {"save", {{"file", file}}}};
    };
    const auto chunk = Result(CallText("disasm", resume(0x1200)));
    CHECK(chunk["saved_rows"] == 3);
    // Without save the listing is the same walk.
    CHECK(Result(CallText("disasm", {{"addr", At(0x1000)}, {"whole", true}}))["rows"].size() == 4);
    // Nothing decodes: a refusal, and the console keeps running.
    const auto records = RunLines(CallsText({CallOf("disasm", resume(0x1300)), CallOf("status")}));
    REQUIRE(records.size() == 2);
    CHECK_FALSE(records[0]["ok"].get<bool>());
    CHECK(records[0]["error"]["kind"] == "start");
    CHECK(records[1]["ok"].get<bool>());
    std::filesystem::remove(file);
}

TEST_CASE("disasm save callees follows calls and tail jumps breadth-first to the depth, inside the caps") {
    const auto file = SaveFile("callees.jsonl");
    const auto save = [&](uint32_t rva, int depth) {
        return Result(CallText("disasm", {{"addr", At(rva)}, {"whole", true}, {"save", {{"file", file}, {"callees", depth}}}}));
    };
    const auto starts = [](const Json& saved) {
        std::vector<std::pair<std::string, int>> out;
        for (const auto& one : saved["functions"]) out.push_back({one["start"].get<std::string>(), one["depth"].get<int>()});
        return out;
    };
    using Starts = std::vector<std::pair<std::string, int>>;
    // Depth 1: the call and nothing past it; the next level is listed as left out by depth.
    const auto one = save(0x1700, 1);
    CHECK(starts(one) == Starts{{At(0x1700), 0}, {At(0x1720), 1}});
    CHECK(one["skipped"] == Json::parse(R"([{"address": ")" + At(0x1740) + R"(", "why": "depth"}, {"address": ")" +
                                         At(0x1760) + R"(", "why": "depth"}])"));
    // Depth 3: the call, then its call and tail jump, then the call below those.
    const auto three = save(0x1700, 3);
    CHECK(starts(three) == Starts{{At(0x1700), 0}, {At(0x1720), 1}, {At(0x1740), 2}, {At(0x1760), 2}, {At(0x1780), 3}});
    CHECK(three["skipped"] == Json::parse(R"([{"address": ")" + At(0x17a0) + R"(", "why": "depth"}])"));
    size_t functionRows = 0;
    std::ifstream in(file);
    for (std::string line; std::getline(in, line);) functionRows += Json::parse(line)["row"] == "function";
    in.close();
    CHECK(functionRows == 5);
    // The count cap holds for the whole save: 16 callees, the 17th skipped.
    const auto capped = save(0x1800, 1);
    CHECK(capped["functions"].size() == 17);
    CHECK(capped["skipped"] == Json::parse(R"([{"address": ")" + At(0x1900 + 16 * 0x10) + R"(", "why": "count"}])"));
    // A callee behind a one-byte decoy entry is walked and saved, not skipped as unreadable.
    const auto decoy = save(0x1b00, 1);
    CHECK(starts(decoy) == Starts{{At(0x1b00), 0}, {At(0x1000), 1}});
    CHECK_FALSE(decoy.contains("skipped"));
    std::filesystem::remove(file);
}

TEST_CASE("find with limit pages until N rows and continues after the last") {
    TheFixture();
    const auto first = Result(CallText("find", {{"text", "Extra_"}, {"view", {{"limit", 30}}}}));
    REQUIRE(first["rows"].size() == 30);
    CHECK(first["total"] == 150);
    CHECK(first["rows"][29]["name"] == "Extra_29");
    const auto rest = Result(CallText("find", {{"text", "Extra_"}, {"view", {{"limit", 200}, {"cursor", first["next_cursor"]}}}}));
    REQUIRE(rest["rows"].size() == 120);
    CHECK(rest["rows"][0]["name"] == "Extra_30");
    CHECK_FALSE(rest.contains("next_cursor"));
    CHECK(Result(CallText("find", {{"text", "Extra_"}}))["rows"].size() == 24);   // One agent page without it.
}

TEST_CASE("a cls that names no class is refused by find, census and each; any case names one") {
    TheFixture();
    for (const auto& call : {CallText("find", {{"cls", "Thingy"}}), CallText("census", {{"cls", "Thingy"}}),
                             CallText("each", {{"cls", "Thingy"}, {"paths", PathsOf("Health")}})}) {
        const auto records = RunLines(call);
        REQUIRE(records.size() == 1);
        INFO(records[0].dump());
        CHECK_FALSE(records[0].value("ok", true));
        CHECK(records[0]["error"]["message"].get<std::string>().starts_with("no class named Thingy"));
    }
    // A part of a class name: the refusal names the classes containing it.
    const auto part = RunLines(CallText("find", {{"cls", "hin"}}));
    REQUIRE(part.size() == 1);
    CHECK(part[0]["error"]["message"].get<std::string>().find("classes containing it: Thing") != std::string::npos);
    // Another case finds the class, as the agent's filters compare.
    CHECK_FALSE(Result(CallText("find", {{"cls", "thing"}}))["rows"].empty());
    CHECK_FALSE(Result(CallText("census", {{"cls", "THING"}}))["rows"].empty());
    // A class with no live instances is still an empty result, not a refusal.
    CHECK(Result(CallText("find", {{"cls", "Thing"}, {"text", "NoSuchObjectName"}}))["rows"].empty());
}

TEST_CASE("disasm counts past 64 instructions and sig-bytes caps a long length") {
    const auto listed = Result(CallText("disasm", {{"addr", At(0x2000)}, {"count", 200}}));
    CHECK(listed["rows"].size() == 200);
    CHECK(listed["stop"] == "count");
    const auto bytes = Result(CallText("sig-bytes", {{"addr", At(0x2000)}, {"len", 200}}));
    CHECK(bytes["length"] == 96);
    CHECK(bytes["capped"] == true);
    CHECK_FALSE(Result(CallText("sig-bytes", {{"addr", At(0x2000)}, {"len", 16}})).contains("capped"));
}

namespace {
std::string LabelLine(uint32_t rva, const char* name, std::string extra = {}) {
    return std::format(R"({{"module":"Game.exe","module_size":{},"timestamp":100000000,"rva":"0x{:x}","name":"{}","kind":"user"{}}})",
        Image::Size, rva, name, extra);
}
std::vector<Json> FileRows(const std::string& path) {
    std::vector<Json> rows;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);) rows.push_back(Json::parse(line));
    return rows;
}
}

TEST_CASE("labels-sign marks a row it cannot sign and skips it on this build unless again") {
    TheCode();
    const auto file = SaveFile("labels.jsonl");
    std::ofstream(file, std::ios::binary) << LabelLine(0x1100, "Signable") << '\n' << LabelLine(0x3010, "Twin") << '\n';
    const auto first = Result(CallText("labels-sign", {{"file", file}}));
    CHECK(first["signed"] == 1);
    CHECK(first["unsigned"] == 1);
    CHECK(first["failed_before"] == 0);
    auto rows = FileRows(file);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].contains("sig"));
    CHECK_FALSE(rows[0].contains("sig_failed"));
    REQUIRE(rows[1].contains("sig_failed"));
    CHECK(rows[1]["sig_failed"]["timestamp"] == 100000000);
    CHECK(rows[1]["sig_failed"]["reason"].get<std::string>().starts_with("not unique"));
    // The same build again: nothing tried, nothing searched.
    const auto second = Result(CallText("labels-sign", {{"file", file}}));
    CHECK(second["failed_before"] == 1);
    CHECK(second["skipped"] == 1);
    CHECK(second["searches"] == 0);
    CHECK(second["rows"].empty());
    // again tries every row.
    const auto again = Result(CallText("labels-sign", {{"file", file}, {"retry", true}}));
    CHECK(again["rows"].size() == 2);
    CHECK(again["unsigned"] == 1);
    // A mark from another build is tried again and marked for this one.
    std::ofstream(file, std::ios::binary) << LabelLine(0x3010, "Twin", R"(,"sig_failed":{"timestamp":1,"reason":"old"})") << '\n';
    const auto other = Result(CallText("labels-sign", {{"file", file}}));
    CHECK(other["rows"].size() == 1);
    CHECK(other["failed_before"] == 0);
    CHECK(FileRows(file)[0]["sig_failed"]["timestamp"] == 100000000);
    // A row signed later loses its mark.
    std::ofstream(file, std::ios::binary) << LabelLine(0x1100, "Signable", R"(,"sig_failed":{"timestamp":1,"reason":"old"})") << '\n';
    CHECK(Result(CallText("labels-sign", {{"file", file}}))["signed"] == 1);
    rows = FileRows(file);
    CHECK(rows[0].contains("sig"));
    CHECK_FALSE(rows[0].contains("sig_failed"));
    std::filesystem::remove(file);
}

TEST_CASE("run: a plan's steps as NDJSON lines, under the packs ceiling, with expects, repeats and a report") {
    const Json health = CallOf("get", GetArgs(PathsOf("Health")));
    const auto planFile = SaveFile("plan.json");
    const Json steps = Json::array({
        {{"verb", "get"}, {"args", health["args"]}, {"expect", {{"ok", true}}}},
        {{"verb", "write"}, {"args", {{"addr", "0x1000"}, {"data", "00"}}}},          // modify: outside the ceiling.
        {{"verb", "status"}, {"expect", {{"result", {{"tool_packs", Json{"explore"}}}}}}},
        {{"verb", "status"}, {"expect", {{"result", {{"tool_packs", Json{"modify"}}}}}}},
        {{"verb", "modules"}, {"repeat", {{"every", 0.05}, {"for", 0.1}}}},
        // A batch step: its envelope, every item in it, is the step's line.
        {{"verb", "batch"}, {"args", {{"calls", Json::array({health, CallOf("modules")})}}}, {"expect", {{"ok", true}}}},
    });
    std::ofstream(planFile, std::ios::binary) << steps.dump();
    // No out: the lines come back in the result.
    const auto records = RunLines(CallText("run", {{"plan", planFile}}), "explore");
    REQUIRE(records.size() == 1);
    REQUIRE(records[0].value("ok", false));
    const Json& result = records[0]["result"];
    const Json& rows = result["rows"];
    REQUIRE(rows.size() == 7);
    CHECK(result["lines"] == 7);
    CHECK(rows[0]["step"] == 1);
    CHECK(rows[0]["command"] == "get");
    CHECK(rows[0]["expect"] == "met");
    CHECK(rows[0].contains("at_ms"));
    CHECK(rows[1]["error"]["kind"] == "pack");                                    // Refused, and the plan goes on.
    CHECK(rows[2]["expect"] == "met");
    CHECK(rows[3]["expect"] == "failed");
    CHECK(rows[3]["expect_failed"].get<std::string>().starts_with("result.tool_packs[0]: wanted \"modify\""));
    CHECK(rows[4]["run"] == 1);
    CHECK(rows[5]["run"] == 2);                                                   // every 0.05 for 0.1: two runs.
    CHECK(rows[6]["command"] == "batch");
    CHECK(rows[6]["ok"] == true);
    CHECK(rows[6]["expect"] == "met");
    REQUIRE(rows[6]["result"]["rows"].size() == 2);
    CHECK(rows[6]["result"]["rows"][0]["command"] == "get");
    CHECK(rows[6]["result"]["rows"][1]["command"] == "modules");
    const Json& report = result["report"];
    CHECK(report["steps"] == 6);
    CHECK(report["calls"] == 7);
    CHECK(report["ok"] == 6);
    CHECK(report["failed"] == 1);
    CHECK(report["refused"] == Json{{"pack", 1}});
    CHECK(report["expect_met"] == 3);
    CHECK(report["expect_failed"] == 1);
    REQUIRE(report["timings"].size() == 6);
    CHECK(report["timings"][4]["calls"] == 2);
    CHECK(report["timings"][4]["ms"].get<double>() >= 0);
    CHECK_FALSE(report["console"].get<std::string>().empty());
    CHECK_FALSE(report["table_hash"].get<std::string>().empty());
    // out: the lines and a last report line go to the file; the result keeps the report.
    const auto outFile = SaveFile("plan-out.jsonl");
    const auto written = Result(CallText("run", {{"plan", planFile}, {"out", outFile}}));
    CHECK(written["file"] == outFile);
    CHECK_FALSE(written.contains("rows"));
    std::vector<Json> lines;
    {
        std::ifstream in(outFile, std::ios::binary);
        for (std::string line; std::getline(in, line);) lines.push_back(Json::parse(line));
    }
    REQUIRE(lines.size() == 8);
    CHECK(lines[1].value("error", Json::object()).value("kind", "") != "pack");    // Every pack: the write is tried.
    CHECK(lines[7]["report"]["calls"] == 7);
    CHECK(lines[7]["report"]["steps"] == 6);
    CHECK(lines[6]["command"] == "batch");
    CHECK(lines[6]["ok"] == true);
    // A bad plan is refused before any step runs; run is not allowed in a plan.
    std::ofstream(planFile, std::ios::binary) << R"([{"verb": "status"}, {"verb": "run", "args": {"plan": "x"}}])";
    const auto refused = Envelope(CallText("run", {{"plan", planFile}}));
    CHECK(refused["error"]["kind"] == "schema");
    CHECK(refused["error"]["message"] == "plan step 2: run is not allowed in a plan");
    CHECK(Envelope(CallText("run", {{"plan", SaveFile("missing.json")}}))["error"]["kind"] == "file");
    std::filesystem::remove(planFile);
    std::filesystem::remove(outFile);
}

TEST_CASE("--packs is a ceiling: a verb of another pack is refused, inside a batch too") {
    const Json health = CallOf("get", GetArgs(PathsOf("Health")));
    // explore and native: a write and a capture are refused by name, reads run.
    const auto records = RunLines(CallsText({CallOf("write", {{"addr", "0x1000"}, {"data", "00"}}), CallOf("watch-read"),
        health, CallOf("status")}), "native");
    REQUIRE(records.size() == 4);
    CHECK(records[0]["error"]["kind"] == "pack");
    CHECK(records[0]["error"]["message"] ==
          "write is in the modify pack, outside this console's --packs explore,native: start it with modify in --packs");
    CHECK(records[1]["error"]["kind"] == "pack");
    CHECK(records[2].value("ok", false));
    CHECK(records[3]["result"]["tool_packs"] == Json{"explore", "native"});
    // A batch is explore; its items obey the ceiling one by one.
    const auto batch = RunLines(CallText("batch", {{"calls", Json::array({health,
        CallOf("call-each", {{"cls", "Thing"}, {"function", "GetScore"}})})}}), "explore");
    REQUIRE(batch.size() == 1);
    const auto& rows = batch[0]["result"]["rows"];
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].value("ok", false));
    CHECK(rows[1]["error"]["kind"] == "pack");
    // Default: every pack; one call alone is checked the same way.
    CHECK(Result(CallText("status"))["tool_packs"] == Json{"explore", "native", "observe", "modify"});
    const auto json = RunLines(CallText("watch-read"), "explore");
    REQUIRE(json.size() == 1);
    CHECK(json[0]["error"]["kind"] == "pack");
    // An unknown pack stops the console at startup.
    const auto bad = RunLines(CallText("status"), "explore,writes");
    REQUIRE(bad.size() == 1);
    CHECK(bad[0]["error"]["kind"] == "startup");
}

namespace {
// Curve tables: one simple (linear), one rich (constant, linear and cubic keys).
struct RichKey { uint8_t interp, tangentMode, weightMode, pad; float time, value, arrive, arriveWeight, leave, leaveWeight; };
static_assert(sizeof(RichKey) == ue::layout::RichCurveKeySize);
struct CurveRow { int32_t name, number; uint64_t curve; int32_t hashNext, hashIndex; };
static_assert(sizeof(CurveRow) == 24);
struct CurveTables { std::string simple, rich, derived, thing; };
const CurveTables& TheCurveTables() {
    static const CurveTables tables = [] {
        const auto& layout = ue::layout::StockUE5;
        auto& world = TheWorld();
        Type& curveTable = world.AddType("CurveTable", 0xA0, false, "Object");
        Type& derivedType = world.AddType("GameCurves", 0xA0, false, "CurveTable");
        const auto table = [&](const Type& type, const char* name, uint8_t mode, std::vector<CurveRow> rows) {
            Object& object = world.Instance(name, type);
            uint8_t* bytes = world.Bytes(object);
            SetSparse(bytes + layout.curveRowMap, {reinterpret_cast<const uint8_t*>(rows.data()), rows.size() * sizeof(CurveRow)},
                      static_cast<uint32_t>(rows.size()), 24);
            bytes[layout.curveTableMode] = mode;
            return Text(object.ref);
        };
        const auto curve = [&](std::span<const uint8_t> keys, uint32_t count, int32_t keysAt, int interp) {
            uint8_t* bytes = world.Alloc(0x90);
            if (interp >= 0) bytes[layout.simpleCurveInterp] = static_cast<uint8_t>(interp);
            SetArray(bytes + keysAt, keys, count);
            return reinterpret_cast<uint64_t>(bytes);
        };
        const float sway[] = {1, 1, 3, 1, 4, 1.3f};
        const float flat[] = {0, 5};
        const uint64_t swayCurve = curve(AsBytes(sway), 3, layout.simpleCurveKeys, 0);
        const uint64_t flatCurve = curve(AsBytes(flat), 1, layout.simpleCurveKeys, 1);
        const RichKey rich[] = {{1, 0, 0, 0, 0, 10, 0, 0, 0, 0}, {0, 0, 0, 0, 1, 20, 0, 0, 0, 0},
                                {2, 0, 0, 0, 2, 40, 0, 0, 4, 0}, {0, 0, 0, 0, 4, 0, -8, 0, 0, 0}};
        const uint64_t richCurve = curve(AsBytes(rich), 4, layout.richCurveKeys, -1);
        CurveTables out;
        out.simple = table(curveTable, "CT_Sway", 1, {{world.Name("Pitch"), 0, swayCurve, -1, 0},
                                                       {world.Name("Flat"), 0, flatCurve, -1, 0}});
        out.rich = table(curveTable, "CT_Skills", 2, {{world.Name("Level"), 0, richCurve, -1, 0}});
        out.derived = table(derivedType, "CT_Derived", 1, {{world.Name("Pitch"), 0, swayCurve, -1, 0}});
        out.thing = TheFixture().thing;
        return out;
    }();
    return tables;
}
}

TEST_CASE("rows reads a curve table's keys and evaluates them with at") {
    const auto& tables = TheCurveTables();
    const auto rows = [](const std::string& table, const Json& more = {}) { return CallOf("rows", With({{"object", table}}, more)); };
    const auto simple = Result(rows(tables.simple).dump());
    CHECK(simple["curve"] == "simple");
    CHECK_FALSE(simple.contains("row_struct"));
    REQUIRE(simple["rows"].size() == 2);
    CHECK(simple["rows"][0]["name"] == "Pitch");
    CHECK(simple["rows"][0]["interp"] == "linear");
    CHECK(simple["rows"][0]["keys"] == Json::parse("[[1, 1], [3, 1], [4, 1.3]]"));
    CHECK_FALSE(simple["rows"][0].contains("value"));
    // An address alone names the table too.
    CHECK(Result(rows(tables.simple.substr(0, tables.simple.find('#'))).dump())["rows"] == simple["rows"]);
    // Linear between keys, the end key's value outside them, constant keeps its value.
    const auto at = Result(rows(tables.simple, {{"at", 3.5}}).dump());
    CHECK(at["at"] == 3.5);
    CHECK(at["rows"][0]["value"] == 1.15);
    CHECK(Result(rows(tables.simple, {{"at", 9}}).dump())["rows"][0]["value"] == 1.3);
    const auto flat = Result(rows(tables.simple, {{"at", -1}, {"view", {{"match", "flat"}}}}).dump());
    REQUIRE(flat["rows"].size() == 1);
    CHECK(flat["total"] == 1);
    CHECK(flat["rows"][0]["interp"] == "constant");
    CHECK(flat["rows"][0]["keys"] == Json::parse("[[0, 5]]"));
    CHECK(flat["rows"][0]["value"] == 5);

    const auto rich = Result(rows(tables.rich).dump());
    CHECK(rich["curve"] == "rich");
    CHECK(rich["rows"][0]["keys"][2] == Json::parse(R"([2, 40, "cubic", 0, 4])"));
    const auto value = [&](double t) { return Result(rows(tables.rich, {{"at", t}}).dump())["rows"][0]["value"].get<double>(); };
    CHECK(value(0.5) == 10);                // constant
    CHECK(value(1.5) == 30);                // linear
    // Cubic from (2, 40) leaving at 4 to (4, 0) arriving at -8: P1 = 40 + 4*2/3, P2 = 0 + 8*2/3, at alpha 0.5.
    CHECK(value(3) == doctest::Approx((40 + 3 * (40 + 8.0 / 3) + 3 * (16.0 / 3) + 0) / 8).epsilon(1e-5));

    // A subclass of CurveTable is read too; refusals name both table kinds and the wrong args.
    CHECK(Result(rows(tables.derived).dump())["rows"][0]["name"] == "Pitch");
    const auto refused = RunLines(CallsText({rows(tables.thing), rows(tables.simple, {{"view", {{"fields", PathsOf("A")}}}}),
        rows(tables.simple, {{"at", "x"}})}));
    REQUIRE(refused.size() == 3);
    CHECK(refused[0]["error"]["message"] == "neither a data table (no RowStruct field) nor a curve table (no CurveTable class)");
    CHECK(refused[1]["error"]["message"].get<std::string>().find("read data tables; a curve table's rows are keys") !=
          std::string::npos);
    CHECK(refused[2]["error"]["kind"] == "schema");
}

TEST_CASE("assets-export writes each object's fields and a table's rows, one file each, and an index") {
    const auto& fixture = TheFixture();
    TheCurveTables();
    const auto dir = std::filesystem::path(SaveFile("export"));
    std::filesystem::remove_all(dir);
    const auto result = Result(CallText("assets-export", {{"dir", dir.string()}, {"classes", Json::array({"Thing", "CurveTable"})}}));
    CHECK(result["failed"] == 0);
    CHECK(result["capped"] == false);
    CHECK(result["exported"] == result["rows"].size());
    const auto index = Json::parse(std::ifstream(result["index"].get<std::string>()));
    CHECK(index.size() == result["exported"]);
    bool thing = false;
    uint64_t tables = 0;
    for (const auto& row : index) {
        const auto file = Json::parse(std::ifstream(row["file"].get<std::string>()));
        CHECK(file["ref"] == row["ref"]);
        CHECK(file["get"].contains("rows"));
        if (row["name"] == "Thing_0") {
            thing = true;
            CHECK(file["ref"] == fixture.thing);
            CHECK_FALSE(file.contains("rows"));
        }
        if (file.contains("rows")) {
            ++tables;
            CHECK(row["table_rows"] == file["rows"]["rows"].size());
            CHECK(file["rows"].contains("curve"));
        }
    }
    CHECK(thing);
    CHECK(tables == result["tables"]);
    CHECK(tables == 3); // CT_Derived too: a subclass of the class asked for.
    const auto sway = Result(CallText("assets-export", {{"dir", dir.string()}, {"classes", Json::array({"CurveTable"})}, {"view", {{"match", "sway"}}}}));
    CHECK(sway["exported"] == 1);
    CHECK(sway["rows"][0]["table_rows"] == 2);
    // A missing cls is refused before running.
    CHECK(RunLines(CallText("assets-export", {{"dir", "x"}}))[0]["error"]["kind"] == "schema");
}

TEST_CASE("disasm with save follows a switch: its table is data, each target a chunk, the rest jumpouts_left") {
    const auto file = SaveFile("switch.jsonl");
    const auto saved = Result(CallText("disasm", {{"addr", At(0x1400)}, {"whole", true}, {"save", {{"file", file}}}}));
    REQUIRE(saved.contains("tables"));
    CHECK(saved["tables"] == Json::parse(R"([{"jump": ")" + At(0x141d) + R"(", "table": ")" + At(0x1508) +
                                         R"(", "targets": 5}])"));
    std::vector<std::string> chunks;
    for (const auto& chunk : saved["chunks"]) chunks.push_back(chunk["start"].get<std::string>());
    CHECK(chunks == std::vector<std::string>{At(0x1500), At(0x1600)});
    CHECK(saved["jumpouts_left"] == Json::array({At(0x1610)}));
    CHECK_FALSE(saved.contains("capped"));
    // The file: the table as one data row, never as code (the chunk that runs through it stops there).
    std::ifstream in(file);
    bool table = false;
    for (std::string line; std::getline(in, line);) {
        const auto row = Json::parse(line);
        if (row["row"] == "data" && row["address"] == At(0x1508)) table = row["bytes"].get<std::string>().size() == 40;
        if (row["row"] == "code") {
            const uint64_t at = std::stoull(row["address"].get<std::string>(), nullptr, 16);
            const uint64_t end = at + row["bytes"].get<std::string>().size() / 2;
            CHECK_FALSE((at < TheCode() + 0x151c && end > TheCode() + 0x1508));
        }
    }
    CHECK(table);
}

namespace {
// Layout types: a class with a parent, a struct, two referenced classes (one
// a step further) and an enum, every field kind the export describes.
struct LayoutTypes { std::string file; };
const LayoutTypes& TheLayoutTypes() {
    static const LayoutTypes types = [] {
        auto& world = TheWorld();
        const auto own = [](Field field, const char* owner, std::string subtype = {}) {
            field.owner = owner;
            field.subtype = std::move(subtype);
            return field;
        };
        Type& base = world.AddType("LayoutBase", 0x40, false);
        base.fields = {own(MakeField("Health", "FloatProperty", 0x28, 4), "LayoutBase")};
        Type& spot = world.AddType("LayoutSpot", 0x10, true);
        spot.fields = {own(MakeField("X", "FloatProperty", 0, 4), "LayoutSpot"), own(MakeField("Y", "FloatProperty", 4, 4), "LayoutSpot")};
        Type& deep = world.AddType("LayoutDeep", 0x30, false);
        Type& other = world.AddType("LayoutOther", 0x30, false);
        other.fields = {own(MakeField("Deep", "ObjectProperty", 0x28, 8), "LayoutOther", "LayoutDeep")};
        Type& thing = world.AddType("LayoutThing", 0x100, false, "LayoutBase");
        Field flag = own(MakeField("bFlag", "BoolProperty", 0xc0, 1), "LayoutThing");
        flag.numbers.boolMask = 4;
        Field others = own(ArrayField("Others", 0x60, "ObjectProperty", 8), "LayoutThing", "LayoutOther");
        Field value = MakeField("", "ByteProperty", 0, 1);
        value.subtype = "ELayoutRole";
        thing.fields = {own(MakeField("Owner", "ObjectProperty", 0x40, 8), "LayoutThing", "LayoutBase"),
            own(MakeField("Role", "EnumProperty", 0x48, 1), "LayoutThing", "ELayoutRole"),
            own(StructField("Spot", 0x50, spot), "LayoutThing"), others,
            own(MapField("Roles", 0x70, MakeField("", "NameProperty", 0, 8), value, 16, 8), "LayoutThing"), flag,
            // Inherited rows come with the class's table; the export writes them on their own type's lines.
            own(MakeField("Health", "FloatProperty", 0x28, 4), "LayoutBase")};
        (void)deep;
        Object& role = world.Add("ELayoutRole", "Enum", 64);
        world.enums.push_back({role.ref.index, {{"ELayoutRole::A", 0}, {"ELayoutRole::B", 1}}});
        return LayoutTypes{SaveFile("layout.jsonl")};
    }();
    return types;
}
std::vector<Json> FileLines(const std::string& path) {
    std::vector<Json> lines;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);) lines.push_back(Json::parse(line));
    return lines;
}
}

TEST_CASE("layout-export writes type lines and full field lines; closure adds what they reference") {
    const auto& types = TheLayoutTypes();
    const auto layout = [&](const Json& classes, const Json& more = {}) {
        return Result(CallText("layout-export", With({{"file", types.file}, {"classes", classes}}, more)));
    };
    const auto alone = layout(Json::array({"LayoutThing"}));
    CHECK(alone["classes"] == 1);
    CHECK(alone["fields"] == 6);
    auto lines = FileLines(types.file);
    REQUIRE(lines.size() == 7);
    CHECK(lines[0] == Json::parse(R"({"kind": "class", "name": "LayoutThing", "super": "LayoutBase", "size": "0x100"})"));
    CHECK(lines[1] == Json::parse(R"({"kind": "field", "owner": "LayoutThing", "field": "Owner", "offset": "0x40",
        "size": 8, "type": "ObjectProperty", "class": "LayoutBase"})"));
    CHECK(lines[2]["enum"] == "ELayoutRole");
    CHECK(lines[3]["struct"] == "LayoutSpot");
    CHECK(lines[4]["element"] == Json::parse(R"({"type": "ObjectProperty", "size": 8, "class": "LayoutOther"})"));
    CHECK(lines[5]["key"] == Json::parse(R"({"type": "NameProperty", "size": 8})"));
    CHECK(lines[5]["value"] == Json::parse(R"({"type": "ByteProperty", "size": 1, "enum": "ELayoutRole"})"));
    CHECK(lines[5]["stride"] == 16);
    CHECK(lines[5]["value_offset"] == "0x8");
    CHECK(lines[6]["bool_mask"] == 4);
    // layout-check reads the same file: the type lines are skipped, every field holds.
    const auto check = Result(CallText("layout-check", {{"file", types.file}}));
    CHECK(check["checked"] == 6);
    CHECK(check["ok"] == true);

    // Depth 1: the super chain and the types one step away, and the enum with its size.
    const auto closure = layout(Json::array({"LayoutThing"}), {{"closure", 1}});
    CHECK(closure["classes"] == 3);
    CHECK(closure["structs"] == 1);
    CHECK(closure["enums"] == 1);
    lines = FileLines(types.file);
    std::vector<std::string> named;
    for (const auto& line : lines)
        if (line["kind"] != "field") named.push_back(line["name"].get<std::string>());
    CHECK(named == std::vector<std::string>{"LayoutThing", "LayoutBase", "LayoutSpot", "LayoutOther", "ELayoutRole"});
    CHECK(lines.back() == Json::parse(R"({"kind": "enum", "name": "ELayoutRole", "size": 1,
        "values": [["ELayoutRole::A", 0], ["ELayoutRole::B", 1]]})"));
    CHECK(std::ranges::count_if(lines, [](const Json& l) { return l.value("field", "") == "Health"; }) == 1);
    // Depth 2 reaches the class LayoutOther points to; an enum and an unknown name by themselves.
    CHECK(layout(Json::array({"LayoutThing"}), {{"closure", 2}})["classes"] == 4);
    const auto enumOnly = layout(Json::array({"ELayoutRole", "NoSuchType"}));
    CHECK(enumOnly["enums"] == 1);
    CHECK(enumOnly["missing"] == Json::array({"NoSuchType"}));
    CHECK_FALSE(FileLines(types.file)[0].contains("size"));
}

TEST_CASE("disasm save adds layout rows for the classes its labelled functions name") {
    TheLayoutTypes();
    const auto labels = SaveFile("save-labels.jsonl");
    {
        std::ofstream out(labels, std::ios::binary | std::ios::trunc);
        out << LabelLine(0x1700, "NoClass_Run") << "\n" << LabelLine(0x1720, "LayoutThing_Do_Tick") << "\n"
            << LabelLine(0x1740, "LayoutSpot::Get") << "\n";
    }
    const auto file = SaveFile("save-layout.jsonl");
    const auto saved = Result(CallText("disasm", {{"addr", At(0x1700)}, {"whole", true},
        {"save", {{"file", file}, {"callees", 2}, {"labels", labels}}}}));
    // The longest prefix that is a class (LayoutThing, not LayoutThing_Do); `::` names it whole.
    REQUIRE(saved.contains("layouts"));
    CHECK(saved["layouts"]["classes"] == Json::array({"LayoutThing", "LayoutSpot"}));
    CHECK(saved["layouts"]["types"] == 5);   // The closure of one step: its super, struct, class and enum.
    std::vector<std::string> types;
    for (const auto& row : FileLines(file))
        if (row["row"] == "layout" && row["kind"] != "field") types.push_back(row["name"].get<std::string>());
    CHECK(types == std::vector<std::string>{"LayoutThing", "LayoutSpot", "LayoutBase", "LayoutOther", "ELayoutRole"});
    // No label names a class: no layout rows.
    const auto plain = Result(CallText("disasm", {{"addr", At(0x1760)}, {"whole", true}, {"save", {{"file", file}, {"labels", labels}}}}));
    CHECK_FALSE(plain.contains("layouts"));
    std::filesystem::remove(file);
    std::filesystem::remove(labels);
}

TEST_CASE("a map of structs past 512 bytes decodes in get; one with no stride names its element types") {
    TheFixture();
    static const std::string seater = [] {
        World& world = TheWorld();
        Type& camera = world.AddType("SeatCamera", 0x300, true);
        camera.fields = {MakeField("Fov", "FloatProperty", 0, 4)};
        const auto key = MakeField("Key", "IntProperty", 0, 4);
        const auto value = StructField("Value", 0, camera);
        // A curve at its engine defaults: DefaultValue FLT_MAX (unset), an empty key-handle map.
        Type& handles = world.AddType("KeyHandleMap", 0x60, true);
        Type& curve = world.AddType("SeatCurve", 0x70, true);
        curve.fields = {MakeField("DefaultValue", "FloatProperty", 0, 4), StructField("KeyHandlesToIndices", 8, handles),
            MakeField("Gain", "FloatProperty", 0x68, 4)};
        Type& type = world.AddType("Seater", 0x200, false, "Object");
        type.fields = {MapField("Seats", 0x30, key, value, 0x310, 8), MapField("Refused", 0x80, key, value, 0, 0),
            StructField("Curve", 0xd0, curve)};
        const Object& object = world.Instance("Seater_0", type);
        const Ref ref = object.ref;
        uint8_t* curveAt = TheWorld().Bytes(world.objects[static_cast<size_t>(ref.index)]) + 0xd0;
        const float unset = std::numeric_limits<float>::max(), gain = 1.5f;
        memcpy(curveAt, &unset, 4);
        SetSparse(curveAt + 8, {}, 0, 8);
        memcpy(curveAt + 0x68, &gain, 4);
        // Two seats: key 0 and 2, Fov 90 and 70, hash links after the value.
        std::vector<uint8_t> slots(2 * 0x310);
        for (int i = 0; i < 2; ++i) {
            const int32_t seat = i * 2;
            const float fov = i ? 70.0f : 90.0f;
            const int32_t links[2] = {-1, 0};
            memcpy(slots.data() + i * 0x310, &seat, 4);
            memcpy(slots.data() + i * 0x310 + 8, &fov, 4);
            memcpy(slots.data() + i * 0x310 + 0x308, links, 8);
        }
        SetSparse(TheWorld().Bytes(world.objects[static_cast<size_t>(ref.index)]) + 0x30, slots, 2, 0x310);
        return Text(ref);
    }();
    const auto get = [](const char* path) {
        const auto result = Result(CallText("get", {{"object", seater}, {"paths", PathsOf(path)}}));
        REQUIRE(result["rows"].size() == 1);
        return result["rows"][0]["value"];
    };
    const Json seats = get("Seats");
    CHECK(seats["count"] == 2);
    REQUIRE(seats["rows"].size() == 2);
    CHECK(seats["rows"][1]["key"] == 2);
    CHECK(seats["rows"][1]["value"]["Fov"] == 70.0);
    const std::string why = get("Refused")["container_error"].get<std::string>();
    CHECK(why.starts_with("the element layout of Refused (map IntProperty (4 bytes) -> SeatCamera (768 bytes)) is unknown"));
    CHECK(why.find("re-inject") != std::string::npos);                     // A slot past 512 bytes: an older agent's refusal.
    // nonzero: a curve at its defaults shows only what was set.
    const Json curve = get("Curve");
    CHECK(curve["KeyHandlesToIndices"] == Json{{"count", 0}});
    CHECK(curve["DefaultValue"].get<double>() > 3e38);
    const auto set = Result(CallText("get", {{"object", seater}, {"paths", PathsOf("Curve")}, {"view", {{"nonzero", true}}}}));
    CHECK(set["rows"][0]["value"] == Json{{"Gain", 1.5}});
    // entries carries 512-byte slots at most: refused with what to use instead.
    const auto entries = Envelope(CallText("entries", {{"object", seater}, {"field", "Seats"}}));
    CHECK(entries["error"]["kind"] == "validation");
    CHECK(entries["error"]["message"].get<std::string>().find("get the field") != std::string::npos);
}

TEST_CASE("past --context-limit a tool caller's every envelope carries context_warning; nothing stops") {
    const auto calls = CallsText({CallOf("status"), CallOf("modules")});
    const auto over = RunWith(calls, {L"--caller", L"tool", L"--context-limit", L"10"});
    REQUIRE(over.size() == 2);
    for (const auto& record : over) {
        CHECK(record.value("ok", false));                                       // Never a stop.
        REQUIRE(record.contains("context_warning"));
        CHECK(record["context_warning"].get<std::string>().find("context_limit 10") != std::string::npos);
    }
    for (const auto& record : RunWith(calls, {L"--caller", L"tool"}))           // The default: far above two results.
        CHECK_FALSE(record.contains("context_warning"));
    for (const auto& record : RunWith(calls, {L"--caller", L"tool", L"--context-limit", L"0"}))  // 0: no limit.
        CHECK_FALSE(record.contains("context_warning"));
    for (const auto& record : RunWith(calls, {L"--context-limit", L"10"}))       // A shell's output is no context.
        CHECK_FALSE(record.contains("context_warning"));
}

TEST_CASE("get with no paths on an address alone resolves the object as describe does") {
    const std::string thing = TheFixture().thing;
    const std::string address = thing.substr(0, thing.find('#'));
    const auto bare = Result(CallText("get", {{"object", address}}));
    CHECK(bare["ref"] == thing);                                   // The full ref, as describe gives it.
    CHECK(bare["rows"] == Result(CallText("get", GetArgs()))["rows"]);
    const auto nothing = RunLines(CallText("get", {{"object", "0x10"}}));    // No object there: a clear refusal.
    REQUIRE(nothing.size() == 1);
    CHECK_FALSE(nothing[0].value("ok", true));
    CHECK_FALSE(nothing[0]["error"].value("message", std::string()).empty());
}

TEST_CASE("owner names fields, structs and container elements, and route prints the field path") {
    using namespace fake;
    using reagentlive::cli::RefText;
    auto& world = TheWorld();
    Type& link = world.AddType("RouteLink", 16, true);
    link.fields = {MakeField("Target", "ObjectProperty", 8, 8)};
    Type& node = world.AddType("RouteNode", 0x100, false, "Object");
    node.fields = {MakeField("Next", "ObjectProperty", 0x30, 8), StructField("Link", 0x40, link),
        ArrayField("Members", 0x60, "ObjectProperty", 8)};
    const Ref endRef = world.Instance("RouteEnd", node).ref;
    const Ref middleRef = world.Instance("RouteMiddle", node).ref;
    const Ref startRef = world.Instance("RouteStart", node).ref;
    auto* middle = reinterpret_cast<uint8_t*>(middleRef.address);
    auto* start = reinterpret_cast<uint8_t*>(startRef.address);
    const uint64_t members[2] = {startRef.address, endRef.address};
    SetArray(middle + 0x60, std::span(reinterpret_cast<const uint8_t*>(members), sizeof(members)), 2);
    memcpy(start + 0x30, &middleRef.address, 8);
    memcpy(start + 0x48, &endRef.address, 8);
    uint64_t buffer = 0;
    memcpy(&buffer, middle + 0x60, 8);
    const auto hex = [](uint64_t value) {
        char text[32];
        snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(value));
        return std::string(text);
    };

    const auto owner = Result(CallText("owner", {{"addrs", {hex(startRef.address + 0x30), hex(startRef.address + 0x48),
        hex(buffer + 8)}}, {"within", {RefText(middleRef)}}}));
    REQUIRE(owner["rows"].size() == 3);
    CHECK(owner["rows"][0]["path"] == "Next");
    CHECK(owner["rows"][0]["object"]["name"] == "RouteStart");
    CHECK(owner["rows"][1]["path"] == "Link.Target");
    CHECK(owner["rows"][2]["path"] == "Members[1]");
    CHECK(owner["rows"][2]["container"] == true);
    CHECK(owner["rows"][2]["object"]["name"] == "RouteMiddle");
    CHECK(owner["owned"] == 3);

    const auto route = Result(CallText("route", {{"object", RefText(middleRef)}, {"sources", {RefText(startRef)}}}));
    CHECK(route["end"] == "found");
    REQUIRE(route["rows"].size() == 1);
    CHECK(route["rows"][0]["steps"][0]["field"] == "Next");
    CHECK(route["rows"][0]["steps"][0]["object"]["name"] == "RouteMiddle");
    CHECK(route["rows"][0]["path"] == RefText(startRef) + ".Next");
    const auto back = Result(CallText("route", {{"object", RefText(startRef)}, {"sources", {RefText(middleRef)}}}));
    REQUIRE(back["rows"].size() == 1);
    CHECK(back["rows"][0]["steps"][0]["field"] == "Members[0]");
}

TEST_CASE("owner names a map element by its key") {
    using namespace fake;
    using reagentlive::cli::RefText;
    auto& world = TheWorld();
    Type& holder = world.AddType("KeyedHolder", 0x100, false, "Object");
    holder.fields = {MapField("Modes", 0x30, MakeField("Key", "NameProperty", 0, 8), MakeField("Value", "ObjectProperty", 0, 8), 24, 8)};
    const Ref holderRef = world.Instance("KeyedHolder_0", holder).ref;
    struct Slot { int32_t name, number; uint64_t value; int32_t next, hash; };
    const Slot slots[2] = {{world.Name("Walking"), 0, holderRef.address, -1, 0}, {world.Name("Falling"), 0, holderRef.address, -1, 0}};
    auto* bytes = reinterpret_cast<uint8_t*>(holderRef.address);
    SetSparse(bytes + 0x30, std::span(reinterpret_cast<const uint8_t*>(slots), sizeof(slots)), 2, 24);
    uint64_t data = 0;
    memcpy(&data, bytes + 0x30, 8);
    char address[32];
    snprintf(address, sizeof(address), "0x%llx", static_cast<unsigned long long>(data + 24 + 8));
    const auto owner = Result(CallText("owner", {{"addrs", {address}}, {"within", {RefText(holderRef)}}}));
    REQUIRE(owner["rows"].size() == 1);
    CHECK(owner["rows"][0]["path"] == "Modes[Falling]");
    CHECK(owner["rows"][0]["container"] == true);
}
