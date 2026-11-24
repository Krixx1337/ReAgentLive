// reagentlive CLI fast lane: saved functions (callees, chunks, switch tables,
// the data they read) and the cache's module layouts.
#include "reagentlive/frontends/cli/state/cache.h"
#include "reagentlive/frontends/cli/code/code_json.h"
#include "reagentlive/frontends/cli/call/build.h"
#include "reagentlive/frontends/cli/code/disasm.h"
#include "reagentlive/frontends/cli/code/function_save.h"
#include "reagentlive/frontends/cli/code/image.h"
#include "reagentlive/frontends/cli/code/jump_table.h"
#include "reagentlive/frontends/cli/code/native.h"
#include "reagentlive/frontends/cli/code/signature_json.h"
#include "reagentlive/frontends/cli/code/touches.h"
#include "reagentlive/frontends/cli/code/unwind.h"
#include "reagentlive/frontends/cli/code/xrefs.h"
#include "doctest.h"
#include "verbs.h"
#include <array>
#include <cstring>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace reagentlive;
using namespace reagentlive::cli;
#include "code_support.h"

using namespace code_support;

TEST_CASE("callees saves each in-module call target once, and labels name what the file holds") {
    const Module module{"Game.exe", Base, 0x100000};
    std::vector<dis::Instruction> rows(5);
    rows[0].address = Base + 0x1000; rows[0].bytes = {0xe8, 0, 0, 0, 0};
    rows[0].branch = "call"; rows[0].target = Base + 0x3000;
    rows[1].address = Base + 0x1005; rows[1].bytes = {0xe8, 0, 0, 0, 0};
    rows[1].branch = "call"; rows[1].target = Base + 0x3000;                        // Again: saved once.
    rows[2].address = Base + 0x100a; rows[2].bytes = {0xe8, 0, 0, 0, 0};
    rows[2].branch = "call"; rows[2].target = Base + 0x200000;                      // Outside the module.
    rows[3].address = Base + 0x100f; rows[3].bytes = {0x48, 0x8b, 0x05, 0, 0, 0, 0};
    rows[3].target = Base + 0x9000; rows[3].ripTarget = true; rows[3].loadSize = 8; // A writable global.
    rows[4].address = Base + 0x1016; rows[4].bytes = {0xe8, 0, 0, 0, 0};
    rows[4].branch = "call"; rows[4].target = Base + 0x4000;
    CHECK(save::Callees(rows, module, Base + 0x1000) == std::vector<uint64_t>{Base + 0x3000, Base + 0x4000});
    CHECK(save::Callees(rows, module, Base + 0x3000) == std::vector<uint64_t>{Base + 0x4000}); // Not itself.

    std::vector<dis::Instruction> callee(1);
    callee[0].address = Base + 0x3000; callee[0].bytes = {0xc3};
    const std::vector<save::Saved> saved{{Base + 0x1000, rows}, {Base + 0x3000, callee}};
    const std::map<uint64_t, std::string> labels{{Base + 0x3000, "AThing::Resolve"}, {Base + 0x9000, "GTagImpact"}};
    std::istringstream text(save::Lines(module, 7, saved, {}, labels));
    std::vector<Json> lines;
    for (std::string line; std::getline(text, line);) lines.push_back(Json::parse(line));
    REQUIRE(lines.size() == 8);
    CHECK(lines[0]["row"] == "function");
    CHECK(lines[0]["end"] == "0x14000101b");
    CHECK(lines[1]["row"] == "function");
    CHECK(lines[1]["start"] == "0x140003000");
    CHECK(lines[2]["row"] == "code");
    CHECK(lines[3]["address"] == "0x140003000");
    CHECK(lines[4]["name"] == "fn_1000");
    CHECK(lines[5]["name"] == "AThing::Resolve");
    CHECK(lines[5]["what"] == "function");
    CHECK(lines[6]["name"] == "fn_4000");     // A call target not saved: named so calls read as names.
    CHECK(lines[6]["what"] == "call");
    CHECK(lines[7]["name"] == "GTagImpact");  // Only a labelled global gets a name.
    CHECK(lines[7]["what"] == "global");
}

TEST_CASE("callees saves tail jumps out of the function, and names the unsaved ones") {
    const Module module{"Game.exe", Base, 0x100000};
    std::vector<dis::Instruction> rows(3);
    rows[0].address = Base + 0x1000; rows[0].bytes = {0xeb, 0x02};
    rows[0].branch = "jump"; rows[0].target = Base + 0x1004; rows[0].directJump = true; // Inside: not a tail
    rows[1].address = Base + 0x1002; rows[1].bytes = {0x90, 0x90};
    rows[2].address = Base + 0x1004; rows[2].bytes = {0xe9, 0, 0, 0, 0};
    rows[2].branch = "jump"; rows[2].target = Base + 0x8000; rows[2].directJump = true; // A tail call
    const std::vector<save::Range> held{{Base + 0x1000, Base + 0x1009}};
    CHECK(save::TailJumps(rows, module, held) == std::vector<uint64_t>{Base + 0x8000});
    const std::vector<save::Range> withTarget{{Base + 0x1000, Base + 0x1009}, {Base + 0x8000, Base + 0x8010}};
    CHECK(save::TailJumps(rows, module, withTarget).empty());                            // Saved already.

    const std::vector<save::Saved> one{{Base + 0x1000, rows}};
    std::istringstream text(save::Lines(module, 7, one, {}));
    bool named = false;
    for (std::string line; std::getline(text, line);) {
        const auto row = Json::parse(line);
        if (row.value("what", "") == "tail") named = row["name"] == "fn_8000" && row["address"] == "0x140008000";
    }
    CHECK(named);
}

TEST_CASE("a save ends with the console that wrote it, when it is known") {
    const Module module{"Game.exe", Base, 0x100000};
    std::vector<dis::Instruction> rows(1);
    rows[0].address = Base + 0x1000; rows[0].bytes = {0xc3};
    const std::vector<save::Saved> one{{Base + 0x1000, rows}};
    const save::Console console{R"(C:\tools\console.exe)", R"(C:\tools\console.labels.jsonl)", R"(C:\host\names.jsonl)"};
    std::vector<Json> lines;
    std::istringstream text(save::Lines(module, 7, one, {}, {}, console));
    for (std::string line; std::getline(text, line);) lines.push_back(Json::parse(line));
    REQUIRE(!lines.empty());
    CHECK(lines.back() == Json{{"row", "console"}, {"path", R"(C:\tools\console.exe)"},
                               {"labels", R"(C:\tools\console.labels.jsonl)"}, {"names", R"(C:\host\names.jsonl)"}});
    std::istringstream unknown(save::Lines(module, 7, one, {}));
    for (std::string line; std::getline(unknown, line);) CHECK(Json::parse(line)["row"] != "console");
}

TEST_CASE("a function's chained chunk is found by its jumps and saved as a chunk row") {
    const Module module{"Game.exe", Base, 0x100000};
    std::vector<dis::Instruction> rows(4);
    rows[0].address = Base + 0x1000; rows[0].bytes = {0x0f, 0x84, 0, 0, 0, 0};
    rows[0].branch = "branch"; rows[0].target = Base + 0x5000;                     // jz into the chunk
    rows[1].address = Base + 0x1006; rows[1].bytes = {0xe8, 0, 0, 0, 0};
    rows[1].branch = "call"; rows[1].target = Base + 0x3000;                       // A call: not a chunk
    rows[2].address = Base + 0x100b; rows[2].bytes = {0xeb, 0};
    rows[2].branch = "jump"; rows[2].target = Base + 0x5000;                       // Again: once
    rows[3].address = Base + 0x100d; rows[3].bytes = {0xc3};
    rows[3].ends = true;                                                            // The ret
    CHECK(save::Jumps(rows) == std::vector<uint64_t>{Base + 0x5000});
    // An entry cut mid-flow: its chunk follows on from the last row.
    std::vector<dis::Instruction> cut(1);
    cut[0].address = Base + 0x2000; cut[0].bytes = {0x8b, 0xc3};                    // mov eax, ebx
    CHECK(save::Jumps(cut) == std::vector<uint64_t>{Base + 0x2002});

    std::vector<dis::Instruction> tail(1);
    tail[0].address = Base + 0x5000; tail[0].bytes = {0x90, 0xc3};
    const std::vector<save::Saved> parts{{Base + 0x1000, rows}, {Base + 0x5000, tail, Base + 0x1000}};
    std::istringstream text(save::Lines(module, 7, parts, {}));
    std::vector<Json> lines;
    for (std::string line; std::getline(text, line);) lines.push_back(Json::parse(line));
    REQUIRE(lines.size() >= 5);
    CHECK(lines[0]["row"] == "function");
    CHECK(lines[1]["row"] == "chunk");                  // No function row of its own.
    CHECK(lines[1]["start"] == "0x140005000");
    CHECK(lines[1]["end"] == "0x140005002");
    CHECK(lines[1]["function"] == "0x140001000");
    CHECK(lines[3]["row"] == "code");
    CHECK(lines[3]["address"] == "0x140005000");
    CHECK(std::ranges::count_if(lines, [](const Json& l) { return l.value("what", "") == "function"; }) == 1);
}

TEST_CASE("switch tables: the MSVC form with a byte index table, and the table-relative form") {
    Image image;
    dis::Options options;
    options.count = 16;
    // cmp ecx, 9; ja; lea rdx, [image base]; movsxd rax, ecx; movzx eax, byte [rdx + rax + 0x2000];
    // mov ecx, [rdx + rax*4 + 0x2100]; add rcx, rdx; jmp rcx
    const uint64_t a = Base + 0x1000;
    image.Put(a, Join({{0x83, 0xf9, 0x09}, {0x77, 0x3b}, {0x48, 0x8d, 0x15}, Rel32(-0x100c), {0x48, 0x63, 0xc1},
        {0x0f, 0xb6, 0x84, 0x02, 0x00, 0x20, 0x00, 0x00}, {0x8b, 0x8c, 0x82, 0x00, 0x21, 0x00, 0x00},
        {0x48, 0x03, 0xca}, {0xff, 0xe1}}));
    auto listing = dis::List(image.Read(), a, options);
    REQUIRE(listing);
    const auto sites = jumptab::Find(listing->rows);
    REQUIRE(sites.size() == 1);
    CHECK(sites[0].jump == Base + 0x1021);
    CHECK(sites[0].table == Base + 0x2100);
    CHECK(sites[0].base == Base);
    CHECK(sites[0].indexTable == Base + 0x2000);
    CHECK(sites[0].indexCount == 10);                 // ja: the bound plus one.
    CHECK_FALSE(sites[0].signedEntries);
    const std::vector<uint8_t> index{0, 1, 1, 2, 0, 0, 2, 1, 0, 3};
    CHECK(jumptab::CountFromIndex(index) == 4);
    const std::vector<uint8_t> entries = Join({Rel32(0x1030), Rel32(0x1038), Rel32(0x1030), Rel32(0x1040)});
    CHECK(jumptab::Targets(sites[0], entries) == std::vector<uint64_t>{Base + 0x1030, Base + 0x1038, Base + 0x1040});

    // cmp eax, 5; jae; lea rcx, [table]; movsxd rax, [rcx + rax*4]; add rax, rcx; jmp rax
    const uint64_t b = Base + 0x1100;
    image.Put(b, Join({{0x83, 0xf8, 0x05}, {0x73, 0x10}, {0x48, 0x8d, 0x0d}, Rel32(0x2200 - 0x110c),
        {0x48, 0x63, 0x04, 0x81}, {0x48, 0x01, 0xc8}, {0xff, 0xe0}}));
    auto relative = dis::List(image.Read(), b, options);
    REQUIRE(relative);
    const auto found = jumptab::Find(relative->rows);
    REQUIRE(found.size() == 1);
    CHECK(found[0].table == Base + 0x2200);
    CHECK(found[0].base == Base + 0x2200);
    CHECK(found[0].count == 5);                       // jae: the bound itself.
    CHECK(found[0].signedEntries);
    CHECK(jumptab::Targets(found[0], Rel32(-0x1100)) == std::vector<uint64_t>{Base + 0x1100});

    // No bound before the load: no site (the table's size is unknown).
    image.Put(b, Join({{0x90, 0x90, 0x90, 0x90, 0x90}, {0x48, 0x8d, 0x0d}, Rel32(0x2200 - 0x110c),
        {0x48, 0x63, 0x04, 0x81}, {0x48, 0x01, 0xc8}, {0xff, 0xe0}}));
    auto unbounded = dis::List(image.Read(), b, options);
    REQUIRE(unbounded);
    CHECK(jumptab::Find(unbounded->rows).empty());
}

TEST_CASE("switch tables: a bound further back, and the inverted jbe guard") {
    Image image;
    dis::Options options;
    options.count = 64;
    const std::vector<uint8_t> dispatch = Join({{0x48, 0x8d, 0x0d}, Rel32(0x2200 - 0x1124),
        {0x48, 0x63, 0x04, 0x81}, {0x48, 0x01, 0xc8}, {0xff, 0xe0}});
    // cmp eax, 5; jae; 24 nops; lea rcx, [table]; movsxd rax, [rcx + rax*4]; add rax, rcx; jmp rax
    const uint64_t a = Base + 0x1100;
    image.Put(a, Join({{0x83, 0xf8, 0x05}, {0x73, 0x40}, std::vector<uint8_t>(24, 0x90), dispatch}));
    auto far = dis::List(image.Read(), a, options);
    REQUIRE(far);
    auto found = jumptab::Find(far->rows);
    REQUIRE(found.size() == 1);
    CHECK(found[0].table == Base + 0x2200);
    CHECK(found[0].count == 5);

    // cmp eax, 5; jbe dispatch; the default (xor edx, edx; ret; 21 nops); the dispatch.
    image.Put(a, Join({{0x83, 0xf8, 0x05}, {0x76, 0x18}, {0x31, 0xd2}, {0xc3}, std::vector<uint8_t>(21, 0x90), dispatch}));
    auto inverted = dis::List(image.Read(), a, options);
    REQUIRE(inverted);
    found = jumptab::Find(inverted->rows);
    REQUIRE(found.size() == 1);
    CHECK(found[0].count == 6);                       // jbe: the bound plus one.
}

TEST_CASE("a saved function holds its code, the read-only data it reads and its import names") {
    const Module module{"Game.exe", Base, 0x100000};
    std::vector<dis::Instruction> rows(4);
    rows[0].address = Base + 0x1000; rows[0].bytes = {0xf3, 0x0f, 0x10, 0x05, 0, 0, 0, 0};
    rows[0].target = Base + 0x8000; rows[0].ripTarget = true; rows[0].loadSize = 4;
    rows[1].address = Base + 0x1008; rows[1].bytes = {0x48, 0x8d, 0x0d, 0, 0, 0, 0};
    rows[1].target = Base + 0x8004; rows[1].ripTarget = true; rows[1].string = "Hit";  // Touches the load's 8 bytes: merged.
    rows[2].address = Base + 0x100f; rows[2].bytes = {0xff, 0x15, 0, 0, 0, 0};
    rows[2].target = Base + 0x9000; rows[2].ripTarget = true; rows[2].loadSize = 8; rows[2].import = "KERNEL32.dll!Sleep";
    rows[3].address = Base + 0x2000; rows[3].bytes = {0xc3};                            // After a hop: its own run.
    const dis::ConstantFn constant = [](uint64_t) { return true; };
    const auto ranges = save::DataRanges(rows, constant);
    REQUIRE(ranges.size() == 1);
    CHECK(ranges[0] == save::Range{Base + 0x8000, Base + 0x8008});
    const std::vector<std::pair<uint64_t, std::vector<uint8_t>>> data{{Base + 0x8000, {0, 0, 0, 0x3f, 'H', 'i', 't', 0}}};
    const std::vector<save::Saved> one{{Base + 0x1000, rows}};
    std::istringstream text(save::Lines(module, 7, one, data));
    std::vector<Json> lines;
    for (std::string line; std::getline(text, line);) lines.push_back(Json::parse(line));
    REQUIRE(lines.size() == 6);
    CHECK(lines[0]["row"] == "function");
    CHECK(lines[0]["start"] == "0x140001000");
    CHECK(lines[0]["end"] == "0x140002001");
    CHECK(lines[0]["timestamp"] == 7);
    CHECK(lines[1]["row"] == "code");
    CHECK(lines[1]["bytes"].get<std::string>().size() == 21 * 2);
    CHECK(lines[2]["row"] == "code");
    CHECK(lines[2]["address"] == "0x140002000");
    CHECK(lines[3]["row"] == "data");
    CHECK(lines[3]["bytes"] == "0000003f48697400");
    CHECK(lines[4]["name"] == "fn_1000");
    CHECK(lines[5]["name"] == "KERNEL32.dll!Sleep");
    CHECK(lines[5]["rva"] == "0x9000");
}

TEST_CASE("a save never holds overlapping code, nor data under code") {
    const Module module{"Game.exe", Base, 0x100000};
    // A callee listing that runs on through another saved callee.
    std::vector<dis::Instruction> outer(3), inner(2), apart(1);
    outer[0].address = Base + 0x3000; outer[0].bytes = {0x90, 0x90};
    outer[1].address = Base + 0x3002; outer[1].bytes = {0x90, 0x90};
    outer[2].address = Base + 0x3004; outer[2].bytes = {0xc3};
    inner[0].address = Base + 0x3002; inner[0].bytes = {0x90, 0x90};   // Inside the outer listing.
    inner[1].address = Base + 0x3004; inner[1].bytes = {0xc3};
    apart[0].address = Base + 0x3005; apart[0].bytes = {0xcc, 0xc3};   // Touches the outer end: joined.
    const std::vector<save::Saved> parts{{Base + 0x3002, inner}, {Base + 0x3000, outer}, {Base + 0x3005, apart}};
    // Data partly under code: only the bytes past the code are kept.
    const std::vector<std::pair<uint64_t, std::vector<uint8_t>>> data{{Base + 0x3006, {0xc3, 1, 2}}, {Base + 0x8000, {7}}};
    std::istringstream text(save::Lines(module, 7, parts, data));
    std::vector<Json> lines;
    for (std::string line; std::getline(text, line);) lines.push_back(Json::parse(line));
    std::vector<Json> code, kept, functions;
    for (const auto& line : lines) {
        if (line["row"] == "code") code.push_back(line);
        if (line["row"] == "data") kept.push_back(line);
        if (line["row"] == "function") functions.push_back(line);
    }
    CHECK(functions.size() == 3);                       // Function rows stay as they are: they may overlap.
    CHECK(functions[0]["start"] == "0x140003002");
    REQUIRE(code.size() == 1);
    CHECK(code[0]["address"] == "0x140003000");
    CHECK(code[0]["bytes"] == "90909090c3ccc3");
    REQUIRE(kept.size() == 2);
    CHECK(kept[0]["address"] == "0x140003007");
    CHECK(kept[0]["bytes"] == "0102");
    CHECK(kept[1]["address"] == "0x140008000");

    // Data inside one code run is dropped; data spanning two runs keeps the gap.
    const std::vector<std::pair<uint64_t, std::vector<uint8_t>>> runs{{Base + 0x10, {1, 2}}, {Base + 0x14, {3, 4}}};
    const std::vector<uint8_t> under{1}, spanning{2, 9, 9, 3, 4, 5};
    CHECK(save::Uncovered(Base + 0x10, under, runs).empty());
    const auto gaps = save::Uncovered(Base + 0x11, spanning, runs);
    REQUIRE(gaps.size() == 2);
    CHECK(gaps[0] == std::pair<uint64_t, std::vector<uint8_t>>{Base + 0x12, {9, 9}});
    CHECK(gaps[1] == std::pair<uint64_t, std::vector<uint8_t>>{Base + 0x16, {5}});
}

TEST_CASE("the cache keeps module layouts") {
    CacheData data;
    data.owner = 9;
    data.modules = {{"Game.exe", Base, 0x100000}};
    data.layouts = {{Base, {{0x5000, 0x24}, {{".text", 0x1000, 0x4000, 0x60000020}, {".rdata", 0x5000, 0x3000, 0x40000040}}}}};
    auto loaded = LoadCache(SaveCache(data));
    REQUIRE(loaded);
    REQUIRE(loaded->layouts.size() == 1);
    CHECK(loaded->layouts[0].first == Base);
    CHECK(loaded->layouts[0].second == data.layouts[0].second);
    CHECK_FALSE(LoadCache("reagentlive cache 9\nowner\t5\nimage\t0x10\t0x0\t0\t2\nsection\t.text\t0x1\t0x2\t0x3\nend\n"));
}
