// reagentlive CLI native objects and scans: vtable, pointer chains, strings,
// regions, touches and disp-scan.
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

TEST_CASE("vtable call and result") {
    auto parsed = Call(R"({"verb": "vtable", "args": {"addr": "0x1000", "view": {"limit": 12}, "offset": "0xf0", "resolve": true}})");
    REQUIRE(parsed);
    CHECK(parsed->kind == Kind::Vtable);
    CHECK(parsed->limit == 12);
    CHECK(parsed->offset == 0xf0);
    CHECK(parsed->bodies);
    CHECK(Call(R"({"verb": "vtable", "args": {"addr": "0x1000", "view": {"limit": 513}}})").error().starts_with("view.limit"));
    // slots: sorted and unique; past the table's read, refused; not with view.limit.
    auto picked = Call(R"({"verb": "vtable", "args": {"addr": "0x1000", "slots": [15, 6, 8, 6]}})");
    REQUIRE(picked);
    CHECK(picked->slots == std::vector<uint32_t>{6, 8, 15});
    CHECK(Call(R"({"verb": "vtable", "args": {"addr": "0x1000", "slots": [512]}})").error().starts_with("slots"));
    CHECK(Call(R"({"verb": "vtable", "args": {"addr": "0x1000", "slots": [1], "view": {"limit": 4}}})").error().starts_with("slots"));
    // parts: bytes rounded down to whole qwords; never with offset or rtti.
    auto parts = Call(R"({"verb": "vtable", "args": {"addr": "0x1000", "parts": "0x105", "slots": [6]}})");
    REQUIRE(parts);
    CHECK(parts->partBytes == 0x100);
    CHECK(Call(R"({"verb": "vtable", "args": {"addr": "0x1000", "parts": 4}})").error().starts_with("parts"));
    CHECK(Call(R"({"verb": "vtable", "args": {"addr": "0x1000", "parts": 64, "offset": 8}})").error().starts_with("parts"));
    CHECK(Call(R"({"verb": "vtable", "args": {"addr": "0x1000", "parts": 64, "rtti": true}})").error().starts_with("parts"));

    // Pick keeps the slots asked; one past the table is absent.
    native::Slots read{{{0, 1, {}}, {1, 2, {}}, {2, 3, {}}}, "not code", 9};
    native::Pick(read, std::vector<uint32_t>{1, 2, 7});
    REQUIRE(read.slots.size() == 2);
    CHECK(read.slots[0].slot == 1);
    CHECK(read.slots[1].slot == 2);

    // PartCandidates: qwords into a module but not into code (code at Base+0x1000..0x5000).
    // A table repeated by embedded structs is one candidate with every offset.
    std::vector<uint8_t> object(48);
    const uint64_t values[] = {Base + 0x5000, Base + 0x1100, 0x1234, Base + 0x6000, Base + 0x6000, Base + 0x5000};
    memcpy(object.data(), values, sizeof(values));
    const auto image = [](uint64_t v) { return v >= Base && v < Base + 0x100000; };
    const auto code = [](uint64_t v) { return v >= Base + 0x1000 && v < Base + 0x5000; };
    const auto candidates = native::PartCandidates(object, image, code);
    REQUIRE(candidates.size() == 2);
    CHECK(candidates[0].table == Base + 0x5000);
    CHECK(candidates[0].offsets == std::vector<uint64_t>{0, 0x28});
    CHECK(candidates[1].table == Base + 0x6000);
    CHECK(candidates[1].offsets == std::vector<uint64_t>{0x18, 0x20});

    const std::vector<Module> modules{{"Game.exe", Base, 0x100000}};
    native::Slots slots{{{0, Base + 0x1100, Base + 0x1800}}, "not code", 5};
    const auto json = Checked(MakeVtable(Base + 0x9000, 0, Base + 0x5000, std::nullopt, slots, modules));
    const auto none = Checked(MakeVtable(0x1000, 0, std::nullopt, 0x1234, {}, modules));
    CHECK_FALSE(none.contains("table"));
    auto withParts = MakeVtable(Base + 0x9000, 0, std::nullopt, std::nullopt, {}, modules);
    withParts.parts.emplace();
    const uint64_t partOffsets[]{0x28};
    withParts.parts->push_back(MakeVtablePart(partOffsets, Base + 0x5000, slots.slots, modules));
    const auto partsJson = Checked(withParts);
    REQUIRE(partsJson["parts"].size() == 1);
    CHECK(partsJson["parts"][0]["offsets"] == Json::array({"0x28"}));
    CHECK(partsJson["parts"][0]["rows"].size() == 1);
}

TEST_CASE("pointer chains in read-only commands, refused for writes") {
    const std::vector<Module> modules{{"Game.exe", Base, 0x100000}};
    auto read = Call(R"({"verb": "read", "args": {"addr": {"base": {"base": "0x5000#1:1", "offset": 8}, "chain": [48, 16]},
        "len": 16}})", modules);
    REQUIRE(read);
    CHECK(read->request.address == 0x5008);
    CHECK(read->chain == std::vector<uint64_t>{0x30, 0x10});
    CHECK(read->request.length == 16);
    CHECK(Call(R"({"verb": "peek", "args": {"addr": {"base": {"module": "Game.exe", "rva": "0x100"}, "chain": [0, 8]}}})",
        modules)->chain == std::vector<uint64_t>{0, 0x8});
    CHECK(Call(R"({"verb": "disasm", "args": {"addr": {"base": "0x1000", "chain": [8, 0]}}})")->chain.size() == 2);
    CHECK(Call(R"({"verb": "vtable", "args": {"addr": {"base": "0x1000", "chain": [0, "0x20"]}, "offset": 8}})")->chain.size() == 2);
    // One offset reads no pointer: refused, never answered with the bytes at the base.
    for (const char* verb : {"read", "peek", "vtable", "disasm", "mem-snap"}) {
        CAPTURE(verb);
        Json call = {{"verb", verb}, {"args", {{"addr", {{"base", "0x5000#1:1"}, {"chain", Json::array({0x30})}}}}}};
        if (std::string_view(verb) == "mem-snap") call["args"]["len"] = 16;
        const auto refused = Call(call.dump());
        REQUIRE_FALSE(refused);
        CHECK(refused.error().starts_with("addr: "));
        CHECK(refused.error().find(value::ChainNoHop) != std::string::npos);
    }
    CHECK(Call(R"({"verb": "read", "args": {"addr": "0x1000"}})")->chain.empty());
    CHECK_FALSE(Call(R"({"verb": "read", "args": {"addr": {"base": "0x1000", "chain": []}}})"));
    CHECK_FALSE(Call(R"({"verb": "read", "args": {"addr": {"base": "0x1000", "chain": ["x"]}}})"));
    Json tooLong = {{"verb", "read"}, {"args", {{"addr", {{"base", "0x1000"}, {"chain", Json::array()}}}}}};
    for (size_t i = 0; i <= MaxChainHops; ++i) tooLong["args"]["addr"]["chain"].push_back(1);
    CHECK(Call(tooLong.dump()).error().starts_with("addr: a chain takes at most"));
    // A write goes to an address read first: a chain is not one of its address forms.
    for (const char* args : {R"({"addr": {"base": "0x1000", "chain": [8, 0]}, "data": "01"})",
                             R"({"addr": {"base": "0x1000", "chain": [8, 0]}, "data": "01", "hold": 5})"}) {
        const auto refused = Call(R"({"verb": "write", "args": )" + std::string(args) + "}");
        REQUIRE_FALSE(refused);
        CHECK(refused.error().starts_with("addr: "));
    }
}

TEST_CASE("read type cstr and wstr") {
    auto cstr = Call(R"({"verb": "read", "args": {"addr": "0x1000", "type": "cstr"}})");
    REQUIRE(cstr);
    CHECK(cstr->request.length == DefaultStringRead);
    CHECK(Call(R"({"verb": "read", "args": {"addr": "0x1000", "len": 33, "type": "wstr"}})")->request.length == 33);

    const std::vector<uint8_t> text{'W', 'e', '\n', '\\', 0x01, 'x', 0, 'z'};
    const auto c = native::CString(text);
    CHECK(c.text == "We\\n\\\\\\x01x");
    CHECK(c.length == 6);
    CHECK(c.terminated);
    const auto cut = native::CString(std::span(text).first(3));
    CHECK(cut.text == "We\\n");
    CHECK_FALSE(cut.terminated);

    const std::vector<uint8_t> wide{'A', 0, 0xe9, 0, 0x07, 0, 0x00, 0xd8, 0x3a, 0x04, 0, 0, 'q', 0};
    const auto w = native::WideString(wide);
    CHECK(w.text == "A\xc3\xa9\\u0007\\ud800\xd0\xba");
    CHECK(w.length == 5);
    CHECK(w.terminated);
    CHECK_FALSE(native::WideString(std::span(wide).first(4)).terminated);
}

TEST_CASE("chain results name each hop and the broken one") {
    const std::vector<Module> modules{{"Game.exe", Base, 0x100000}};
    PointerChain chain;
    chain.address = 0x7010;
    chain.hops = {{Base + 0x100, 0x7000}};
    const auto ok = Checked(MakeChain(Base + 0xf8, chain, modules));
    chain.broken = 0;
    chain.reason = "null pointer";
    const auto broken = Checked(MakeChain(Base + 0xf8, chain, modules));
    CHECK(broken["broken"] == 0);
}

TEST_CASE("region kinds, protection text and the region read and peek report") {
    namespace r = native::region;
    RegionInfo info{0x1000, 0x2000, 0x1000, r::Commit, 0x20, 0x80, r::Image};
    CHECK(r::Kind(info, std::nullopt) == "code");
    CHECK(r::Kind(info, std::pair(false, false)) == "rodata");
    CHECK(r::Kind(info, std::pair(false, true)) == "data");
    info.protect = 0x04;
    CHECK(r::Kind(info, std::nullopt) == "data");
    info.type = r::Private;
    CHECK(r::Kind(info, std::nullopt) == "private");
    info.type = r::Mapped;
    CHECK(r::Kind(info, std::nullopt) == "mapped");
    info.state = r::Reserve;
    CHECK(r::Kind(info, std::nullopt) == "reserved");
    info.state = r::Free;
    CHECK(r::Kind(info, std::nullopt) == "free");
    CHECK(r::Protection(0x20) == "rx");
    CHECK(r::Protection(0x04 | r::Guard) == "rw+guard");
    CHECK(r::Protection(0x01) == "none");
    CHECK(r::Protection(0x04 | r::WriteCombine) == "rw+writecombine");

    const std::vector<Module> modules{{"Game.exe", Base, 0x100000}};
    const RegionInfo code{Base + 0x1000, 0x4000, Base, r::Commit, 0x20, 0x80, r::Image};
    const auto json = Checked(MakeRegion(code, "code"));
    CHECK(json == Json{{"kind", "code"}, {"base", HexNumber(Base + 0x1000)}, {"size", "0x4000"}, {"protect", "rx"}});

    PeekValue mapped{0x1000, 0x1aac568d980, true};
    mapped.readable = true;
    mapped.region = "mapped";
    CHECK(Checked(MakePeekRow(mapped, modules))["label"] == "mapped");
    mapped.region = "private";
    CHECK(Checked(MakePeekRow(mapped, modules))["label"] == "heap");
}

TEST_CASE("touches and disp-scan take every base but the stack frame's unless bases names them") {
    const auto code = Join({
        {0x8b, 0x81, 0x38, 0x05, 0x00, 0x00},             // mov eax, [rcx+0x538]
        {0x8b, 0x85, 0x38, 0x05, 0x00, 0x00},             // mov eax, [rbp+0x538]: a frame slot
        {0x89, 0x83, 0x38, 0x05, 0x00, 0x00},             // mov [rbx+0x538], eax
    });
    const std::vector<uint32_t> offsets{0x538};
    CHECK(touch::InCode(code, Base, offsets).size() == 2);           // rcx and rbx; rbp left out.
    const auto framed = touch::InCode(code, Base, offsets, touch::Bases{{"rbp"}});
    REQUIRE(framed.size() == 1);
    CHECK(framed[0].instruction == Base + 6);
    const auto onRcx = touch::InCode(code, Base, offsets, touch::Bases{{"rcx"}});
    REQUIRE(onRcx.size() == 1);
    CHECK(onRcx[0].instruction == Base);
}

TEST_CASE("disp-scan sweeps code for one instruction form: access, width, immediate, resume point") {
    const auto code = Join({
        {0x8b, 0x81, 0x38, 0x05, 0x00, 0x00},             // mov eax, [rcx+0x538]: a read
        {0xc7, 0x81, 0x38, 0x05, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}, // mov dword [rcx+0x538], 1
        {0xcc, 0xcc},                                     // padding
        {0x88, 0x83, 0x38, 0x05, 0x00, 0x00},             // mov [rbx+0x538], al: a byte store
        {0x48, 0x8d, 0x91, 0x38, 0x05, 0x00, 0x00},       // lea rdx, [rcx+0x538]: the address
        {0x89, 0x84, 0x24, 0x38, 0x05, 0x00, 0x00},       // mov [rsp+0x538], eax: a stack slot
    });
    const auto run = [&](touch::Form form, bool last = true, size_t most = 16) {
        std::vector<touch::Hit> hits;
        const size_t stopped = touch::Sweep(code, Base, form, last, hits, most);
        return std::pair(hits, stopped);
    };
    auto [writes, end] = run({{0x538}});
    CHECK(end == code.size());
    REQUIRE(writes.size() == 2);
    CHECK(writes[0].address == Base + 6);
    CHECK(writes[0].access == "write");
    CHECK(writes[0].width == 4);
    CHECK(writes[1].width == 1);
    CHECK(run({{0x538}, "read"}).first.size() == 1);
    const auto any = run({{0x538}, "any"}).first;
    REQUIRE(any.size() == 4);
    CHECK(any[3].access == "address");
    CHECK(run({{0x538}, "write", 4}).first.size() == 1);
    const auto one = run({{0x538}, "write", 0, 1}).first;
    REQUIRE(one.size() == 1);
    CHECK(one[0].text.find("0x01") != std::string::npos);
    CHECK(run({{0x538}, "write", 0, 2}).first.empty());
    CHECK(run({{0x538}, "write", 0, std::nullopt, touch::Bases{{"rsp"}}}).first.size() == 1);
    // A full page stops right after the hit that filled it; a chunk that is not the last stops
    // before an instruction that might run past it.
    CHECK(run({{0x538}}, true, 1).second == 16);
    CHECK(run({{0x538}, "any"}, false).second == 24);              // The lea at 24 could run past: the next chunk starts there.
}

TEST_CASE("disp-scan takes several disps, tags each hit, keeps rbp and groups functions by base") {
    const auto code = Join({
        {0x88, 0x87, 0xd0, 0x00, 0x00, 0x00},             // mov [rdi+0xD0], al
        {0x48, 0x89, 0x87, 0xb8, 0x00, 0x00, 0x00},       // mov [rdi+0xB8], rax
        {0x88, 0x85, 0xd0, 0x00, 0x00, 0x00},             // mov [rbp+0xD0], al: rbp as a plain register
        {0x48, 0x89, 0x83, 0xb8, 0x00, 0x00, 0x00},       // mov [rbx+0xB8], rax
    });
    std::vector<touch::Hit> hits;
    touch::Sweep(code, Base, {{0xd0, 0xb8}}, true, hits, 16);
    REQUIRE(hits.size() == 4);
    CHECK(hits[0].disp == 0xd0);
    CHECK(hits[0].base == "rdi");
    CHECK(hits[1].disp == 0xb8);
    CHECK(hits[2].base == "rbp");
    // touches' default leaves rbp out; disp-scan's (Form) keeps it.
    hits.clear();
    touch::Sweep(code, Base, {{0xd0, 0xb8}, "write", 0, std::nullopt, touch::Bases{}}, true, hits, 16);
    CHECK(hits.size() == 3);
    // Every hit in one function: only rdi carries both disps.
    hits.clear();
    touch::Sweep(code, Base, {{0xd0, 0xb8}}, true, hits, 16);
    const std::vector<uint32_t> disps{0xd0, 0xb8};
    const std::vector<std::optional<uint64_t>> one(hits.size(), Base), none(hits.size());
    const auto groups = touch::SameBase(hits, one, disps);
    REQUIRE(groups.size() == 1);
    CHECK(groups[0].base == "rdi");
    CHECK(groups[0].hits.size() == 2);
    CHECK(touch::SameBase(hits, none, disps).empty());
}

TEST_CASE("disp-scan same_base pages end on a function boundary and together equal one page") {
    // Two functions, each writing both disps on rdi; the second straddles the page end.
    const auto hit = [](uint64_t address, uint32_t disp) { return touch::Hit{address, disp, "rdi", "write", 8, {}}; };
    const std::vector<touch::Hit> all{hit(Base + 0x10, 0xd0), hit(Base + 0x20, 0xb8), hit(Base + 0x110, 0xd0),
        hit(Base + 0x190, 0xb8)};
    const auto functionOf = [](uint64_t address) { return std::optional<uint64_t>(address < Base + 0x100 ? Base : Base + 0x100); };
    std::vector<std::optional<uint64_t>> allStarts;
    for (const auto& h : all) allStarts.push_back(functionOf(h.address));
    const std::vector<uint32_t> disps{0xd0, 0xb8};
    const auto whole = touch::SameBase(all, allStarts, disps);
    REQUIRE(whole.size() == 2);

    // The first page stopped at 0x150, inside the second function: it ends at that function's start.
    std::vector<touch::Hit> page(all.begin(), all.begin() + 3);
    std::vector<std::optional<uint64_t>> starts(allStarts.begin(), allStarts.begin() + 3);
    const uint64_t next = touch::KeepWhole(page, starts, Base, Base + 0x150, functionOf(Base + 0x150));
    CHECK(next == Base + 0x100);
    CHECK(page.size() == 2);
    CHECK(starts.size() == 2);
    auto groups = touch::SameBase(page, starts, disps);
    // The second page sees the second function whole.
    const std::vector<touch::Hit> rest(all.begin() + 2, all.end());
    const std::vector<std::optional<uint64_t>> restStarts(allStarts.begin() + 2, allStarts.end());
    for (auto& group : touch::SameBase(rest, restStarts, disps)) groups.push_back(std::move(group));
    REQUIRE(groups.size() == whole.size());
    for (size_t i = 0; i < groups.size(); ++i) {
        CHECK(groups[i].function == whole[i].function);
        CHECK(groups[i].hits.size() == whole[i].hits.size());
    }
    // A cut at a function start, or in the function the page began with, keeps everything.
    CHECK(touch::KeepWhole(page, starts, Base, Base + 0x100, Base + 0x100) == Base + 0x100);
    CHECK(touch::KeepWhole(page, starts, Base, Base + 0x30, Base) == Base + 0x30);
    CHECK(page.size() == 2);
}

TEST_CASE("one anchor scan finds every anchor across blocks, as separate searches would") {
    std::vector<uint8_t> image(0x3000, 0x90);
    const std::vector<uint8_t> a{0x48, 0x89, 0x5c, 0x24, 0x08}, b{0x48, 0x89, 0x5c, 0x24, 0x10}, c{0xde, 0xad, 0xbe, 0xef};
    const auto put = [&](size_t at, const std::vector<uint8_t>& bytes) { std::ranges::copy(bytes, image.begin() + at); };
    put(0x10, a);
    put(0xffe, b);   // Across the first block's end.
    put(0x2000, a);
    put(0x2ffc, c);  // At the very end.
    sig::AnchorScan scan({a, b, c, {0x11, 0x22, 0x33, 0x44}});
    for (size_t at = 0; at < image.size(); at += 0x1000)
        scan.Feed(Base + at, std::span(image).subspan(at, 0x1000), at + 0x1000 >= image.size());
    CHECK(scan.Hits()[0] == std::vector<uint64_t>{Base + 0x10, Base + 0x2000});
    CHECK(scan.Hits()[1] == std::vector<uint64_t>{Base + 0xffe});
    CHECK(scan.Hits()[2] == std::vector<uint64_t>{Base + 0x2ffc});
    CHECK(scan.Hits()[3].empty());
}

TEST_CASE("touches keeps [reg + disp] uses of the asked offsets, marks writes and ranks functions") {
    const auto code = Join({
        {0x8b, 0x81, 0x38, 0x05, 0x00, 0x00},             // mov eax, [rcx+0x538]
        {0xf3, 0x0f, 0x11, 0x83, 0x40, 0x05, 0x00, 0x00}, // movss [rbx+0x540], xmm0
        {0x48, 0x8b, 0x84, 0x24, 0x38, 0x05, 0x00, 0x00}, // mov rax, [rsp+0x538]: a stack slot
        {0x48, 0x8b, 0x05, 0x38, 0x05, 0x00, 0x00},       // mov rax, [rip+0x538]: a global
        {0x8b, 0x41, 0x10},                               // mov eax, [rcx+0x10]: not asked
        {0xff},                                           // undecodable alone at the end
    });
    const std::vector<uint32_t> offsets{0x538, 0x540};
    const auto uses = touch::InCode(code, Base + 0x1000, offsets);
    REQUIRE(uses.size() == 2);
    CHECK(uses[0].instruction == Base + 0x1000);
    CHECK(uses[0].offset == 0x538);
    CHECK_FALSE(uses[0].write);
    CHECK(uses[0].load);
    CHECK_FALSE(uses[1].load);
    CHECK(uses[1].instruction == Base + 0x1006);
    CHECK(uses[1].offset == 0x540);
    CHECK(uses[1].write);
    CHECK(uses[1].text.find("0x540") != std::string::npos);

    // EVEX compresses an 8-bit displacement (disp8*N): raw 0x01 is [rcx+0x40]
    // for a 64-byte operand, so the raw value alone must not filter it out.
    const auto evex = Join({
        {0x62, 0xf1, 0x7c, 0x48, 0x10, 0x41, 0x01},       // vmovups zmm0, [rcx+0x40]
        {0x62, 0xf1, 0x7c, 0x48, 0x10, 0x41, 0x40},       // vmovups zmm0, [rcx+0x1000]: not asked
    });
    const std::vector<uint32_t> forty{0x40};
    const auto scaled = touch::InCode(evex, Base, forty);
    REQUIRE(scaled.size() == 1);
    CHECK(scaled[0].instruction == Base);
    CHECK(scaled[0].offset == 0x40);
    CHECK(scaled[0].text.find("zmm0") != std::string::npos);
    const std::vector<uint32_t> one{0x1};
    CHECK(touch::InCode(evex, Base, one).empty());                   // The raw byte is not the offset.

    std::vector<touch::Row> rows(3);
    rows[0].start = 1; rows[0].uses = {uses[0], uses[0], uses[0]};
    rows[1].start = 2; rows[1].uses = {uses[0], uses[1]};
    rows[2].start = 3; rows[2].uses = {uses[1]};
    touch::Rank(rows);
    CHECK(rows[0].start == 2); // Two distinct offsets.
    CHECK(rows[0].distinct == 2);
    CHECK(rows[1].start == 3); // One offset, but written.
    CHECK(rows[2].start == 1);
}

TEST_CASE("touches cuts unlisted code at returns and int3 padding, and access read drops copies") {
    const auto code = Join({
        {0x8b, 0x81, 0x38, 0x05, 0x00, 0x00},             // mov eax, [rcx+0x538]
        {0xc3},                                           // ret: the first piece ends here
        {0xcc, 0xcc, 0xcc},                               // padding: in no piece
        {0x89, 0x83, 0x38, 0x05, 0x00, 0x00},             // mov [rbx+0x538], eax
        {0xc3},
    });
    const auto pieces = touch::Pieces(code, Base + 0x1000);
    REQUIRE(pieces.size() == 2);
    CHECK(pieces[0].start == Base + 0x1000);
    CHECK(pieces[0].end == Base + 0x1007);
    CHECK(pieces[1].start == Base + 0x100a);
    CHECK(pieces[1].end == Base + 0x1011);
    const auto second = touch::PieceAt(code, Base + 0x1000, Base + 0x100a);
    REQUIRE(second);
    CHECK(second->start == Base + 0x100a);
    CHECK(second->end == Base + 0x1011);
    CHECK_FALSE(touch::PieceAt(code, Base + 0x1000, Base + 0x1008));   // Padding.

    // From a call inside a function: junk after the previous return (it decodes
    // to a return of its own) is skipped, and a c3 byte inside an instruction
    // does not cut the piece.
    const auto junk = Join({
        {0xc3},                                           // the previous function's ret
        {0xad, 0xc3},                                     // junk: lodsd; ret
        {0x48, 0x8b, 0xc4},                               // mov rax, rsp: the function starts here
        {0x8b, 0xc3},                                     // mov eax, ebx
        {0xe8, 0x00, 0x00, 0x00, 0x00},                   // call: the hit
        {0xc3},
    });
    // A start lies close after an end of straight-line code; a call site far past one.
    CHECK(touch::NearEnd(junk, Base + 0x2000, Base + 0x2003, Base + 0x2003, 0));
    CHECK_FALSE(touch::NearEnd(junk, Base + 0x2000, Base + 0x2003, Base + 0x2008, 2));  // 5 bytes past the start.
    // A function after a tail jump and junk bytes: ...; jmp; junk (sar r9, 1); push rbx.
    const auto tail = Join({{0x48, 0x8b, 0xc4}, {0xe9, 0, 0, 0, 0}, {0x49, 0xd1, 0xf9}, {0x53}});
    CHECK(touch::NearEnd(tail, Base, Base, Base + 11, 3));
    CHECK_FALSE(touch::NearEnd(tail, Base, Base, Base + 11, 2));
    CHECK_FALSE(touch::NearEnd(tail, Base, Base, Base + 3, 2));                         // At the jmp, 3 past the start.
    const auto caller = touch::PieceAt(junk, Base + 0x2000, Base + 0x2008);
    REQUIRE(caller);
    CHECK(caller->start == Base + 0x2003);
    CHECK(caller->end == Base + 0x200e);

    // The window around one address: its section, clipped at the entries on either side.
    const std::vector<touch::Piece> entries{{Base + 0x1000, Base + 0x1100}, {Base + 0x1800, Base + 0x1900}};
    const auto window = touch::Window(Base + 0x1400, Base, Base + 0x10000, entries);
    REQUIRE(window);
    CHECK(window->start == Base + 0x1100);
    CHECK(window->end == Base + 0x1800);
    CHECK_FALSE(touch::Window(Base + 0x1050, Base, Base + 0x10000, entries));   // An entry holds it.
    // One-byte decoy entries (a packed image's) clip nothing.
    const std::vector<touch::Piece> decoys{{Base + 0x1300, Base + 0x1301}, {Base + 0x1500, Base + 0x1501}};
    const auto past = touch::Window(Base + 0x1400, Base + 0x1100, Base + 0x1800, decoys);
    REQUIRE(past);
    CHECK(past->start == Base + 0x1100);
    CHECK(past->end == Base + 0x1800);
    const auto reach = touch::Window(Base + 0x8000, Base + 0x7000, Base + 0x20000, {}, 0x100);
    REQUIRE(reach);
    CHECK(reach->start == Base + 0x7f00);
    CHECK(reach->end == Base + 0x8100);
    CHECK(touch::Window(Base + 0x7010, Base + 0x7000, Base + 0x20000, {}, 0x100)->start == Base + 0x7000);

    const touch::Use read{Base, 0x538, false, {}, true}, write{Base, 0x538, true, {}}, other{Base, 0x540, false, {}, true};
    const touch::Use rmw{Base, 0x548, true, {}, true}, lea{Base, 0x550, false, {}};
    const auto make = [&] {
        std::vector<touch::Row> rows(4);
        rows[0].start = 1; rows[0].uses = {read, write, other};   // 0x540 is loaded and not stored: kept
        rows[1].start = 2; rows[1].uses = {read, write};          // a copy: dropped
        rows[2].start = 3; rows[2].uses = {write};                // writes only: dropped
        rows[3].start = 4; rows[3].uses = {read, other};          // two loads, no store
        return rows;
    };
    auto rows = make();
    touch::Rank(rows, "read");
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].start == 1);   // Two loads each, in the order given.
    CHECK(rows[0].distinct == 2);
    CHECK(rows[0].uses.size() == 2); // Only the loads stay.
    CHECK(rows[1].start == 4);
    CHECK(rows[1].distinct == 2);
    // read counts a read-modify-write as a load and leaves a lea out; any keeps every use.
    std::vector<touch::Row> mixed(1);
    mixed[0].start = 1; mixed[0].uses = {rmw, lea};
    touch::Rank(mixed, "read");
    REQUIRE(mixed.size() == 1);
    CHECK(mixed[0].uses.size() == 1);
    mixed[0].uses = {rmw, lea};
    touch::Rank(mixed, "any");
    CHECK(mixed[0].uses.size() == 2);
    // write keeps the uses that write, and drops rows with none.
    rows = make();
    rows.push_back({});
    rows.back().start = 5; rows.back().uses = {other};
    touch::Rank(rows, "write");
    REQUIRE(rows.size() == 3);
    for (const auto& row : rows) CHECK(row.writes);
    CHECK(rows[0].uses.size() == 1);
}

TEST_CASE("the exception directory lists the entries starting in a range") {
    Image image;
    image.Headers(4);
    image.Entry(0, 0x1000, 0x1040, 0x8000);
    image.Entry(1, 0x1040, 0x1080, 0x8000);
    image.Entry(2, 0x1100, 0x1180, 0x8000);
    image.Entry(3, 0x1200, 0x1210, 0x8000);
    unwind::Table table(image.Read(), image.module, {PdataRva, 4 * unwind::EntrySize});
    const auto inside = table.Between(Base + 0x1020, Base + 0x1200);
    REQUIRE(inside);
    REQUIRE(inside->size() == 2); // Starts at 0x1040 and 0x1100; 0x1000 began before, 0x1200 at the end.
    CHECK((*inside)[0] == unwind::Range{Base + 0x1040, Base + 0x1080});
    CHECK((*inside)[1] == unwind::Range{Base + 0x1100, Base + 0x1180});
    CHECK(table.Between(Base, Base + 0x100000)->size() == 4);
    CHECK(table.Between(Base + 0x1300, Base + 0x1400)->empty());
    CHECK(table.Between(Base - 0x100, Base + 0x1001)->size() == 1);
}

TEST_CASE("touches call and result") {
    const std::vector<Module> modules{{"Game.exe", Base, 0x100000}};
    auto touches = Call(R"({"verb": "touches", "args": {"addr": {"module": "Game.exe", "rva": "0x2000"},
        "offsets": ["0x538", "0x540", "0x538"], "range": "0x1000"}})", modules);
    REQUIRE(touches);
    CHECK(touches->kind == Kind::Touches);
    CHECK(touches->request.address == Base + 0x2000);
    CHECK(touches->offsets == std::vector<uint32_t>{0x538, 0x540});
    CHECK(touches->range == 0x1000);
    CHECK(Call(R"({"verb": "touches", "args": {"addr": "0x1000", "offsets": [16]}})")->range == touch::DefaultRange);
    CHECK(Call(R"({"verb": "touches", "args": {"addr": "0x1000", "offsets": [16]}})")->access == "any");
    CHECK(Call(R"({"verb": "touches", "args": {"addr": "0x1000", "offsets": [16], "access": "read"}})")->access == "read");
    CHECK(Call(R"({"verb": "touches", "args": {"addr": "0x1000", "offsets": [16], "access": "write"}})")->access == "write");
    CHECK_FALSE(Call(R"({"verb": "touches", "args": {"addr": "0x1000", "offsets": [16], "access": "both"}})"));
    CHECK_FALSE(Call(R"({"verb": "touches", "args": {"addr": "0x1000", "offsets": [16], "reads": true}})"));
    CHECK_FALSE(Call(R"({"verb": "touches", "args": {"addr": "0x1000"}})"));
    CHECK_FALSE(Call(R"({"verb": "touches", "args": {"addr": "0x1000", "offsets": []}})"));
    CHECK(Call(R"({"verb": "touches", "args": {"addr": "0x1000", "offsets": [0]}})").error().starts_with("offsets"));
    CHECK(Call(R"({"verb": "touches", "args": {"addr": "0x1000", "offsets": [16], "range": 0}})").error().starts_with("range"));
    CHECK(Call(R"({"verb": "touches", "args": {"addr": "0x1000", "offsets": [16], "range": "0x400001"}})").error()
        .starts_with("range"));
    CHECK(Call(R"({"verb": "touches", "args": {"addr": "0x1000", "offsets": [1, 2, 3, 4, 5, 6, 7, 8, 9]}})").error()
        .starts_with("offsets"));

    touch::Row row;
    row.start = Base + 0x1000; row.end = Base + 0x1100; row.distinct = 1;
    row.uses.push_back({Base + 0x1004, 0x538, true, "mov [rcx+0x538], eax"});
    const std::vector<uint32_t> offsets{0x538};
    touch::Row unlisted = row;
    unlisted.listed = false;
    const auto json = Checked(MakeTouches(Base + 0x2000, offsets, 0x1000, Base + 0x1000, Base + 0x3000, 3, 5, 0x2000,
        {row, unlisted}, 7, modules));
    CHECK(json["rows"][0]["uses"][0]["rva"] == "0x1004");
    CHECK_FALSE(json["rows"][0].contains("unlisted"));
    CHECK(json["rows"][1]["unlisted"] == true);
    CHECK(json["scanned"]["unlisted"] == 5);
    CHECK(json["offsets"][0] == "0x538");
}
