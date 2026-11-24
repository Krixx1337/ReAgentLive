// reagentlive CLI fast lane: function bounds from a prepared exception directory,
// disasm over fixed bytes, decoder-exact sig fields, xref kinds, calls and
// result shapes, and type names and imports from this program's own image.
// No live process: reads are served from a byte vector (code_support.h).
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

TEST_CASE("the exception directory and sections come from the PE headers") {
    Image image;
    image.Headers(3);
    auto layout = image::ReadLayout(image.Read(), image.module);
    REQUIRE(layout);
    CHECK(layout->exception == unwind::Directory{PdataRva, 36});
    REQUIRE(layout->sections.size() == 2);
    CHECK(layout->sections[0] == image::Section{".text", 0x1000, 0x4000, 0x60000020});
    CHECK(layout->sections[0].Exec());
    CHECK_FALSE(layout->sections[1].Exec());
    CHECK(layout->At(0x1234)->name == ".text");
    CHECK(layout->At(0x5000)->name == ".rdata");
    CHECK(layout->At(0x9000) == nullptr);

    Image none;
    none.Headers(0);
    auto empty = image::ReadLayout(none.Read(), none.module);
    REQUIRE(empty);
    CHECK(empty->exception.size == 0);
    unwind::Table table(none.Read(), none.module, empty->exception);
    CHECK(table.Lookup(Base + 0x1000).value() == std::nullopt);

    Image bad;
    CHECK_FALSE(image::ReadLayout(bad.Read(), bad.module));
}

TEST_CASE("vtable slots stop at the first entry that is not code") {
    Image image;
    const auto code = [](uint64_t value) { return value >= Base + 0x1000 && value < Base + 0x5000; };
    std::vector<uint8_t> table;
    for (const uint64_t entry : {Base + 0x1100, Base + 0x1200, Base + 0x1300, Base + 0x5010, Base + 0x1400}) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&entry);
        table.insert(table.end(), bytes, bytes + 8);
    }
    auto all = native::ReadSlots(table, 0, code);
    REQUIRE(all.slots.size() == 3);
    CHECK(all.slots[2] == native::Slot{2, Base + 0x1300, std::nullopt});
    CHECK(all.stop == "not code");
    CHECK(all.stopValue == Base + 0x5010);
    auto two = native::ReadSlots(table, 2, code);
    CHECK(two.slots.size() == 2);
    CHECK(two.stop == "count");
    auto short_ = native::ReadSlots(std::span(table).first(16), 0, code);
    CHECK(short_.stop == "end of read");

    // A jump stub resolves to its body (through two hops); a plain function does not.
    const uint64_t stub = Base + 0x1100, middle = Base + 0x1500, body = Base + 0x1800, plain = Base + 0x1200;
    image.Put(stub, Join({{0xe9}, Rel32(static_cast<int32_t>(middle - (stub + 5)))}));
    image.Put(middle, {0xeb, 0x7e});                       // jmp short to middle + 0x80.
    image.Put(middle + 0x80, Join({{0xe9}, Rel32(static_cast<int32_t>(body - (middle + 0x85)))}));
    image.Put(plain, {0x48, 0x83, 0xec, 0x28});
    std::vector<native::Slot> slots{{0, stub, std::nullopt}, {1, plain, std::nullopt}};
    REQUIRE(native::ResolveBodies(image.Read(), slots));
    CHECK(slots[0].body == body);
    CHECK_FALSE(slots[1].body);
}

TEST_CASE("function bounds: adjacent entries, a gap, the ends and chained entries") {
    Image image;
    image.Headers(5);
    image.Entry(0, 0x1000, 0x1040, 0x8000);
    image.Entry(1, 0x1040, 0x1080, 0x8010);   // Adjacent.
    image.Entry(2, 0x1100, 0x1180, 0x8020);   // After a gap.
    image.Entry(3, 0x1200, 0x1210, 0x8030);   // Continues entry 2 through CHAININFO.
    image.Entry(4, 0x1300, 0x1320, 0x5000 + 2 * 12 + 1); // Continues entry 2 directly (bit 0).
    image.Put(Base + 0x8000, {0x01, 0x04, 0x00, 0x00});
    image.Put(Base + 0x8010, {0x01, 0x04, 0x00, 0x00});
    image.Put(Base + 0x8020, {0x01, 0x04, 0x00, 0x00});
    image.Put(Base + 0x8030, {0x01 | (unwind::ChainInfo << 3), 0x00, 0x01, 0x00, 0x11, 0x22, 0x00, 0x00});
    // The parent entry after one unwind code (padded to two): 4 + 2 * 2.
    image.Put32(Base + 0x8038, 0x1100);
    image.Put32(Base + 0x803c, 0x1180);
    image.Put32(Base + 0x8040, 0x8020);
    unwind::Table table(image.Read(), image.module, {PdataRva, 5 * unwind::EntrySize});
    const auto at = [&](uint64_t rva) { return table.Lookup(Base + rva).value(); };

    CHECK(at(0x1000)->range == unwind::Range{Base + 0x1000, Base + 0x1040});
    CHECK(at(0x103f)->range == unwind::Range{Base + 0x1000, Base + 0x1040});
    CHECK(at(0x1040)->range == unwind::Range{Base + 0x1040, Base + 0x1080});
    CHECK(at(0x1090) == std::nullopt);   // The gap.
    CHECK(at(0x0fff) == std::nullopt);   // Before the first.
    CHECK(at(0x1320) == std::nullopt);   // After the last.
    CHECK_FALSE(at(0x1100)->chunk);
    const auto chained = at(0x1204);
    REQUIRE(chained);
    CHECK(chained->range == unwind::Range{Base + 0x1100, Base + 0x1180});
    CHECK(chained->chunk == unwind::Range{Base + 0x1200, Base + 0x1210});
    const auto direct = at(0x1310);
    REQUIRE(direct);
    CHECK(direct->range == unwind::Range{Base + 0x1100, Base + 0x1180});
    CHECK(direct->chunk == unwind::Range{Base + 0x1300, Base + 0x1320});
    CHECK(table.Lookup(Base - 1).value() == std::nullopt);
}

TEST_CASE("a large directory is searched in a few requests, sharing its samples") {
    constexpr size_t Count = 6000;
    Image image(0x80000);
    image.Headers(Count);
    for (size_t i = 0; i < Count; ++i)
        image.Entry(i, static_cast<uint32_t>(0x20000 + i * 0x10), static_cast<uint32_t>(0x20000 + i * 0x10 + 8), 0x7f000);
    image.Put(Base + 0x7f000, {0x01, 0x00, 0x00, 0x00});
    unwind::Table table(image.Read(), image.module, {PdataRva, Count * unwind::EntrySize});
    auto found = table.Lookup(Base + 0x20000 + 4321 * 0x10 + 3).value();
    REQUIRE(found);
    CHECK(found->range.start == Base + 0x20000 + 4321 * 0x10);
    const size_t first = image.requests;
    CHECK(first <= 6);
    CHECK(table.Lookup(Base + 0x20000 + 4321 * 0x10 + 9).value() == std::nullopt); // Between entries.
    CHECK(table.Lookup(Base + 0x20000 + 17 * 0x10).value()->range.start == Base + 0x20000 + 17 * 0x10);
    // A sweep's lookups near earlier ones (its window read, its unwind data's page) cost no request.
    const size_t warm = image.requests;
    for (size_t i = 4312; i < 4406; ++i) // The window the first lookup read.
        CHECK(table.Lookup(Base + 0x20000 + i * 0x10 + 1).value()->range.start == Base + 0x20000 + i * 0x10);
    CHECK(image.requests == warm);
}

TEST_CASE("disasm decodes text, targets, fields and where code ends") {
    Image image;
    const uint64_t at = Base + 0x2000;
    image.Put(at, Join({{0x48, 0x8b, 0x05}, Rel32(0x100),        // mov rax, [rip+0x100]
        {0xe8}, Rel32(0x20),                                       // call +0x20
        {0xc7, 0x05}, Rel32(0x40), Rel32(7),                       // mov dword [rip+0x40], 7
        {0x48, 0xb8, 1, 2, 3, 4, 5, 6, 7, 8},                      // mov rax, imm64
        {0xc3}}));
    auto listing = dis::List(image.Read(), at, {});
    REQUIRE(listing);
    const auto& rows = listing->rows;
    REQUIRE(rows.size() >= 5);
    CHECK(rows[0].text.starts_with("mov rax"));
    CHECK(rows[0].target == at + 7 + 0x100);
    CHECK(rows[0].fields == std::vector<dis::Field>{{3, 4, "rip"}});
    CHECK(rows[1].text.starts_with("call"));
    CHECK(rows[1].target == at + 12 + 0x20);
    CHECK(rows[1].branch == "call");
    CHECK(rows[1].fields == std::vector<dis::Field>{{1, 4, "relative"}});
    CHECK(rows[2].target == at + 22 + 0x40);   // Measured from the instruction's end, past the immediate.
    CHECK(rows[2].fields == std::vector<dis::Field>{{2, 4, "rip"}, {6, 4, "immediate"}});
    CHECK(rows[3].fields == std::vector<dis::Field>{{2, 8, "immediate"}});
    CHECK(rows[3].absolute == 0x0807060504030201ull);
    CHECK(rows[4].text == "ret");
    CHECK(rows[4].ends);
    CHECK_FALSE(rows[3].ends);
    CHECK(rows.size() == 24);   // Straight on past the ret: no function bound.
    CHECK(listing->stop == "count");
}

TEST_CASE("disasm limits: count, bytes, a truncated instruction and undecodable bytes") {
    Image image;
    const uint64_t at = Base + 0x3000;
    std::vector<uint8_t> nops(600, 0x90);
    image.Put(at, nops);
    auto three = dis::List(image.Read(), at, {3});
    REQUIRE(three);
    CHECK(three->rows.size() == 3);
    CHECK(three->stop == "count");

    // 510 one-byte nops then a 7-byte instruction: it would end past byte 512.
    image.Put(at + 510, {0x48, 0x8b, 0x05, 0, 0, 0, 0});
    dis::Options options;
    options.count = 64;
    auto limited = dis::List(image.Read(), at, options);
    REQUIRE(limited);
    CHECK(limited->rows.size() == 64);
    image.Put(at, std::vector<uint8_t>(510, 0x90));
    // 64 rows cap first; use long instructions to reach the byte limit.
    std::vector<uint8_t> longs;
    for (int i = 0; i < 52; ++i) longs.insert(longs.end(), {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0}); // 520 bytes.
    image.Put(at, longs);
    auto bytes = dis::List(image.Read(), at, options);
    REQUIRE(bytes);
    CHECK(bytes->rows.size() == 51);
    REQUIRE(bytes->end);
    CHECK(bytes->end->reason == "instruction runs past the byte limit");
    CHECK(bytes->end->offset == 510);

    image.Put(at, {0x90, 0x06}); // push es: invalid in 64-bit mode.
    auto bad = dis::List(image.Read(), at, options);
    REQUIRE(bad);
    CHECK(bad->rows.size() == 1);
    REQUIRE(bad->end);
    CHECK(bad->end->reason == "undecodable");
    CHECK(bad->end->address == at + 1);
    CHECK(bad->end->offset == 1);

    image.holes.push_back({at + 0x1000 - (at & 0xfff), at + 0x2000 - (at & 0xfff)});
    const uint64_t edge = at + 0x1000 - (at & 0xfff) - 3;  // Three bytes before an unreadable page.
    image.Put(edge, {0x90, 0x48, 0x8b});
    auto cut = dis::List(image.Read(), edge, options);
    REQUIRE(cut);
    CHECK(cut->rows.size() == 1);
    REQUIRE(cut->end);
    CHECK(cut->end->reason == "instruction runs into unreadable memory");
}

TEST_CASE("disasm follow steps through unconditional jumps only, at most three") {
    Image image;
    const uint64_t a = Base + 0x4000, b = Base + 0x4100, c = Base + 0x4200, d = Base + 0x4300, e = Base + 0x4400;
    image.Put(a, Join({{0xe9}, Rel32(static_cast<int32_t>(b - (a + 5)))}));
    image.Put(b, Join({{0xe9}, Rel32(static_cast<int32_t>(c - (b + 5)))}));
    image.Put(c, Join({{0xe9}, Rel32(static_cast<int32_t>(d - (c + 5)))}));
    image.Put(d, Join({{0xe9}, Rel32(static_cast<int32_t>(e - (d + 5)))}));
    dis::Options options;
    options.count = 6;
    options.follow = true;
    auto listing = dis::List(image.Read(), a, options);
    REQUIRE(listing);
    REQUIRE(listing->followed.size() == 3);
    CHECK(listing->followed[0].to == b);
    CHECK(listing->followed[2].to == d);
    CHECK(listing->rows[3].address == d);            // The fourth jump is listed, not followed.
    CHECK(listing->rows[4].address == d + 5);

    image.Put(a, {0x74, 0x10, 0xc3});               // je: left alone.
    auto branch = dis::List(image.Read(), a, options);
    REQUIRE(branch);
    CHECK(branch->followed.empty());
    CHECK(branch->rows[1].text == "ret");

    // With a bound: the stub's body is listed to its own end.
    image.Put(a, Join({{0xe9}, Rel32(static_cast<int32_t>(b - (a + 5)))}));
    image.Put(b, {0x90, 0x90, 0xc3});
    options.count = 64;
    options.bound = [&](uint64_t at) -> std::optional<uint64_t> { return at == b ? std::optional(b + 3) : std::optional(a + 5); };
    auto bounded = dis::List(image.Read(), a, options);
    REQUIRE(bounded);
    CHECK(bounded->rows.size() == 4);
    CHECK(bounded->stop == "function end");
}

TEST_CASE("disasm walk stops at the return no earlier forward branch passes") {
    Image image;
    const uint64_t a = Base + 0x4800;
    image.Put(a, Join({{0x74, 0x03},             // je +3: past the first ret
        {0x90, 0xc3},                              // nop; ret: an early return, passed by the je
        {0x90},                                    // the je's target
        {0xe9}, Rel32(0x100000),                   // jmp far away: a tail call, does not extend the walk
        {0x90, 0x90}}));
    dis::Options options;
    options.count = 64;
    options.walk = true;
    auto listing = dis::List(image.Read(), a, options);
    REQUIRE(listing);
    REQUIRE(listing->rows.size() == 5);
    CHECK(listing->rows[2].text == "ret");
    CHECK(listing->rows[4].text.starts_with("jmp"));
    CHECK(listing->stop == "walk end");

    // jmp +1 to the byte after a ret: the code goes on there, so that ret does not end it.
    image.Put(a, Join({{0xeb, 0x01}, {0xc3}, {0x90, 0xc3}}));
    auto over = dis::List(image.Read(), a, options);
    REQUIRE(over);
    CHECK(over->rows.size() == 4);
    CHECK(over->rows[3].address == a + 4);
    CHECK(over->stop == "walk end");
}

TEST_CASE("a whole-function listing passes the old limits and gives next where a cap cuts it") {
    Image image;
    const uint64_t a = Base + 0x6000;
    std::vector<uint8_t> body(5000, 0x90);   // Past 512 bytes and past one read chunk.
    body.push_back(0xc3);
    image.Put(a, body);
    dis::Options options;
    options.count = dis::MaxFunctionInstructions;
    options.bytes = dis::MaxFunctionBytes;
    options.walk = true;
    auto whole = dis::List(image.Read(), a, options);
    REQUIRE(whole);
    CHECK(whole->rows.size() == dis::MaxFunctionInstructions);   // The row cap comes before the ret.
    CHECK(whole->stop == "count");
    REQUIRE(whole->next);
    CHECK(*whole->next == a + 2048);
    options.count = 300;
    auto cut = dis::List(image.Read(), a, options);
    REQUIRE(cut);
    CHECK(cut->rows.size() == 300);
    CHECK(cut->next == a + 300);
    std::vector<uint8_t> shorter(1000, 0x90);
    shorter.push_back(0xc3);
    image.Put(a, shorter);
    options.count = dis::MaxFunctionInstructions;
    auto ends = dis::List(image.Read(), a, options);
    REQUIRE(ends);
    CHECK(ends->rows.size() == 1001);
    CHECK(ends->stop == "walk end");
    CHECK_FALSE(ends->next);
}

TEST_CASE("read-only loads carry the value they read") {
    Image image;
    const uint64_t a = Base + 0x6000, data = Base + 0x8000, writable = Base + 0x9000;
    const auto rel = [&](uint64_t end, uint64_t target) { return Rel32(static_cast<int32_t>(target - end)); };
    image.Put(a, Join({{0xf3, 0x0f, 0x10, 0x05}, rel(a + 8, data),             // movss xmm0, [data]
        {0xf2, 0x0f, 0x10, 0x05}, rel(a + 16, data + 8),                       // movsd xmm0, [data+8]
        {0x8b, 0x05}, rel(a + 22, data + 16),                                  // mov eax, [data+16]
        {0x0f, 0x54, 0x05}, rel(a + 29, data + 32),                            // andps xmm0, [data+32]
        {0x8b, 0x05}, rel(a + 35, writable),                                   // mov eax, [writable]
        {0x48, 0x8d, 0x05}, rel(a + 42, data),                                 // lea rax, [data]: no load
        {0xc3}}));
    const float half = 0.5f;
    const double deg = 57.29577951308232;
    std::vector<uint8_t> values(48, 0);
    std::memcpy(values.data(), &half, 4);
    std::memcpy(values.data() + 8, &deg, 8);
    const int32_t seven = -7;
    std::memcpy(values.data() + 16, &seven, 4);
    for (int i = 0; i < 4; ++i) { values[32 + i * 4] = 0xff; values[33 + i * 4] = 0xff; values[34 + i * 4] = 0xff; values[35 + i * 4] = 0x7f; }
    image.Put(data, values);
    auto listing = dis::List(image.Read(), a, {7});
    REQUIRE(listing);
    REQUIRE(listing->rows.size() == 7);
    const dis::ConstantFn constant = [&](uint64_t at) { return at < writable; };
    REQUIRE(dis::AddValues(image.Read(), listing->rows, constant));
    const auto& rows = listing->rows;
    CHECK(rows[0].value == 0.5);
    CHECK(rows[0].valueType == "f32");
    CHECK(rows[1].value == deg);
    CHECK(rows[1].valueType == "f64");
    CHECK(rows[2].value == -7);
    CHECK(rows[2].valueType == "i32");
    CHECK_FALSE(rows[3].value);
    CHECK(rows[3].valueBytes.size() == 16);                 // A packed mask: its bytes.
    CHECK_FALSE(rows[4].value);                             // A writable global: peek it instead.
    CHECK(rows[4].valueBytes.empty());
    CHECK_FALSE(rows[5].value);                             // lea reads nothing.
    const std::vector<Module> modules{image.module};
    const auto json = Checked(MakeDisasm(a, *listing, std::nullopt, modules, false, true));
    CHECK(json["rows"][0]["value"] == 0.5);
    CHECK(json["rows"][0]["value_type"] == "f32");
    CHECK(json["rows"][3]["value_hex"].is_string());
}

TEST_CASE("string hints: ASCII, UTF-16 and a target that is not text") {
    Image image;
    const uint64_t at = Base + 0x5000;
    const uint64_t ascii = Base + 0x6000, wide = Base + 0x6100, junk = Base + 0x6200, shortText = Base + 0x6300;
    image.Put(ascii, {'W', 'e', 'a', 'p', 'o', 'n', '.', 'c', 'p', 'p', 0});
    image.Put(wide, {'A', 0, 'i', 0, 'm', 0, 'e', 0, 'd', 0, 0, 0});
    image.Put(junk, {0x48, 0x8b, 0x05, 0x01, 0x00});
    image.Put(shortText, {'a', 'b', 'c', 0});
    std::vector<uint8_t> code;
    for (const uint64_t target : {ascii, wide, junk, shortText}) {
        const uint64_t here = at + code.size();
        auto lea = Join({{0x48, 0x8d, 0x0d}, Rel32(static_cast<int32_t>(target - (here + 7)))});
        code.insert(code.end(), lea.begin(), lea.end());
    }
    image.Put(at, code);
    auto listing = dis::List(image.Read(), at, {4});
    REQUIRE(listing);
    std::vector<Module> modules{image.module};
    REQUIRE(dis::AddStrings(image.Read(), listing->rows, modules));
    CHECK(listing->rows[0].string == "Weapon.cpp");
    CHECK_FALSE(listing->rows[0].wide);
    CHECK(listing->rows[1].string == "Aimed");
    CHECK(listing->rows[1].wide);
    CHECK_FALSE(listing->rows[2].string);
    CHECK_FALSE(listing->rows[3].string);   // Under four characters.

    std::vector<uint8_t> longText(200, 'x');
    longText.push_back(0);
    CHECK_FALSE(dis::StringAt(std::span(longText).first(dis::StringWindow))); // No terminator inside the window.
    longText.resize(120);
    longText.back() = 0;
    CHECK(dis::StringAt(longText)->text.size() == dis::MaxString);
}

TEST_CASE("decoder-exact sig fields, falling back to the byte heuristic") {
    Image image;
    std::vector<Module> modules{image.module};
    const uint64_t at = Base + 0x7000;
    // cmp dword [rip+x], 5 (83 3D disp32 imm8): the heuristic knows 83; mov ecx, 0x00e8xxxx is
    // where the byte heuristic sees a call (E8) that is not one.
    const auto bytes = Join({{0x83, 0x3d}, Rel32(0x100), {0x05},
        {0xb9, 0x00, 0xe8, 0x10, 0x00}, {0x00, 0x00, 0x00}});
    const auto pattern = sig::FromBytes(bytes);
    size_t decoded = 0;
    const auto exact = dis::ExactFields(pattern, bytes, at, modules, &decoded);
    REQUIRE(exact.size() == 1);
    CHECK(exact[0].kind == std::string_view("rip"));
    CHECK(exact[0].offset == 2);
    CHECK(exact[0].target == at + 7 + 0x100);
    CHECK(decoded == bytes.size() - 1); // add [rax], al, then one byte that is not an instruction.
    const auto heuristic = sig::AddressFields(pattern, at, modules);
    CHECK(heuristic.size() > exact.size());   // It also flags the E8 inside the immediate.

    auto masked = pattern;
    masked.fixed[3] = false;                  // A wildcard inside the field: not reported.
    CHECK(dis::ExactFields(masked, bytes, at, modules).empty());

    const auto broken = Join({{0x06}, {0xe8}, Rel32(0x10)});
    const auto fallback = dis::ExactFields(sig::FromBytes(broken), broken, at, modules, &decoded);
    CHECK(decoded == 0);
    REQUIRE(fallback.size() == 1);
    CHECK(fallback[0].kind == std::string_view("call"));
    CHECK(fallback[0].offset == 2);

    const Json json = Checked(MakeSigBytes(at, bytes, modules, ExactFields{exact, decoded}));
    CHECK(json["suggested"]["fields"].size() == 1);
    CHECK(json["suggested"].contains("decoded"));
}

TEST_CASE("xref kinds from the bytes in front of the field") {
    const uint64_t target = 0x140010000;
    const auto window = [&](std::vector<uint8_t> before, uint64_t field, int64_t slack = 0) {
        std::array<uint8_t, xref::Window> out{};
        std::copy(before.begin(), before.end(), out.begin() + (xref::Before - before.size()));
        const auto value = static_cast<int32_t>(static_cast<int64_t>(target) - static_cast<int64_t>(field + 4) - slack);
        std::memcpy(out.data() + xref::Before, &value, 4);
        return xref::Classify(field, out, target);
    };
    const uint64_t f = 0x140001000;
    CHECK(window({0x90, 0xe8}, f) == xref::Hit{f, "call", f - 1});
    CHECK(window({0xe9}, f) == xref::Hit{f, "jmp", f - 1});
    CHECK(window({0x0f, 0x84}, f) == xref::Hit{f, "jcc", f - 2});
    CHECK(window({0x48, 0x8d, 0x0d}, f) == xref::Hit{f, "lea", f - 3});
    CHECK(window({0x00, 0x8b, 0x05}, f) == xref::Hit{f, "load", f - 2});
    CHECK(window({0x48, 0x8b, 0x05}, f) == xref::Hit{f, "load", f - 3});
    CHECK(window({0x0f, 0xb6, 0x05}, f) == xref::Hit{f, "load", f - 3});
    CHECK(window({0x48, 0x89, 0x0d}, f) == xref::Hit{f, "store", f - 3});
    CHECK(window({0x66, 0x89, 0x05}, f) == xref::Hit{f, "store", f - 3});
    CHECK(window({0xc6, 0x05}, f, 1) == xref::Hit{f, "store", f - 2});
    CHECK(window({0xc7, 0x05}, f, 4) == xref::Hit{f, "store", f - 2});
    CHECK(window({0x66, 0xc7, 0x05}, f, 2) == xref::Hit{f, "store", f - 3});
    CHECK(window({0xc7, 0x05}, f, 1).kind == "unknown");          // Wrong immediate for the opcode.
    CHECK(window({0x83, 0x3d}, f, 1) == xref::Hit{f, "rip", std::nullopt}); // cmp [rip], imm8.
    CHECK(window({0xff, 0x15}, f) == xref::Hit{f, "rip", std::nullopt});    // call [rip].
    CHECK(window({0x0f, 0x10, 0x05}, f).kind == "rip");             // movups: another two-byte opcode.
    CHECK(window({0x12, 0x34}, f).kind == "unknown");
    CHECK(window({0xe8}, f, 2).kind == "unknown");
}

TEST_CASE("fast-lane calls") {
    const std::vector<Module> modules{{"Game.exe", Base, 0x100000}};
    auto disasm = Call(R"({"verb": "disasm", "args": {"addr": {"module": "Game.exe", "rva": "0x1000"}, "count": 12,
        "function": true, "follow": true}})", modules);
    REQUIRE(disasm);
    CHECK(disasm->kind == Kind::Disasm);
    CHECK(disasm->request.address == Base + 0x1000);
    CHECK(disasm->limit == 12);
    CHECK(disasm->function);
    CHECK(disasm->follow);
    CHECK(Call(R"({"verb": "disasm", "args": {"addr": "0x1000"}})")->limit == DefaultDisasm);
    CHECK(Call(R"({"verb": "disasm", "args": {"addr": "0x1000", "count": 1024}})")->limit == 1024);
    CHECK(Call(R"({"verb": "disasm", "args": {"addr": "0x1000", "count": 1025}})").error().starts_with("count"));
    // function: the whole function by default, a count up to its cap; resume and save need it.
    CHECK(Call(R"({"verb": "disasm", "args": {"addr": "0x1000", "function": true}})")->limit == dis::MaxFunctionInstructions);
    CHECK(Call(R"({"verb": "disasm", "args": {"addr": "0x1000", "count": 300, "function": true}})")->limit == 300);
    CHECK_FALSE(Call(R"({"verb": "disasm", "args": {"addr": "0x1000", "count": 2049, "function": true}})"));
    CHECK(Call(R"({"verb": "disasm", "args": {"addr": "0x1000", "function": true, "resume": true}})")->resume);
    CHECK(Call(R"({"verb": "disasm", "args": {"addr": "0x1000", "function": true, "save": {"file": "fn.jsonl"}}})")->file == "fn.jsonl");
    CHECK(Call(R"({"verb": "disasm", "args": {"addr": "0x1000", "resume": true}})").error().starts_with("resume"));
    CHECK(Call(R"({"verb": "disasm", "args": {"addr": "0x1000", "save": {"file": "fn.jsonl"}}})").error().starts_with("save"));
    const auto callees = Call(R"({"verb": "disasm", "args": {"addr": "0x1000", "function": true, "save": {"file": "fn.jsonl", "callees": 2, "labels": "l.jsonl"}}})");
    REQUIRE(callees);
    CHECK(callees->calleeDepth == 2);
    CHECK(Call(R"({"verb": "disasm", "args": {"addr": "0x1000", "function": true, "save": {"file": "f", "callees": 0}}})").error().starts_with("callees must be 1..3"));
    CHECK(Call(R"({"verb": "disasm", "args": {"addr": "0x1000", "function": true, "save": {"file": "f", "callees": 4}}})").error().starts_with("callees must be 1..3"));
    CHECK_FALSE(Call(R"({"verb": "disasm", "args": {"addr": "0x1000", "function": true, "save": {"file": "f", "callees": true}}})")); // A depth, not a bool.
    CHECK(callees->labels == "l.jsonl");
    CHECK_FALSE(Call(R"({"verb": "disasm", "args": {"addr": "0x1000", "function": true, "callees": true}})")); // Needs save.
    CHECK_FALSE(Call(R"({"verb": "disasm", "args": {"addr": "0x1000", "function": true, "labels": "l.jsonl"}})"));
    CHECK_FALSE(Call(R"({"verb": "disasm", "args": {"addr": "0x1000", "bogus": true}})"));

    auto xrefs = Call(R"({"verb": "xrefs", "args": {"addr": "0x1000", "module": "engine.dll", "all": true}})");
    REQUIRE(xrefs);
    CHECK(xrefs->kind == Kind::Xrefs);
    CHECK(xrefs->request.op == op::Xrefs);
    CHECK(xrefs->target == "engine.dll");
    CHECK(xrefs->all);
    CHECK_FALSE(Call(R"({"verb": "xrefs"})"));

    auto text = Call(R"({"verb": "search", "args": {"type": "text", "value": "Weapon.cpp", "scope": {"module": "Game.exe"}}})");
    REQUIRE(text);
    CHECK(text->request.length == 10);
    CHECK(std::memcmp(text->request.data, "Weapon.cpp", 10) == 0);
    auto wide = Call(R"({"verb": "search", "args": {"type": "wtext", "value": "Aim"}})");
    REQUIRE(wide);
    CHECK(wide->request.length == 6);
    CHECK(wide->request.data[1] == 0);
    CHECK(wide->request.data[2] == 'i');
    const Json wideKey = {{"verb", "search"}, {"args", {{"type", "hex"}, {"value", "01"}, {"wide", true}}}};
    CHECK(Call(wideKey.dump()).error().starts_with("unknown key 'wide'"));
    const Json tooLong = {{"verb", "search"}, {"args", {{"type", "text"}, {"value", std::string(65, 'x')}}}};
    CHECK(Call(tooLong.dump()).error().starts_with("value"));
}

TEST_CASE("the xrefs request is checked like a search") {
    Request request{};
    request.op = op::Xrefs;
    request.length = 8;
    request.address = Base;
    request.args[0] = Base + 0x1000;
    request.args[1] = 64;
    CHECK(Check(request) == Status::Ok);
    request.flags = SearchBackground;
    CHECK(Check(request) == Status::Ok);
    request.flags = SearchPaged;
    CHECK(Check(request) == Status::BadRequest);
    request.flags = 0;
    request.args[0] = 0;
    CHECK(Check(request) == Status::BadRequest);   // The end is required.
    request.args[0] = Base + 0x1000;
    request.length = 4;
    CHECK(Check(request) == Status::BadRequest);
}

TEST_CASE("fast-lane results have their documented keys") {
    const std::vector<Module> modules{{"Game.exe", Base, 0x100000}};
    const unwind::Function function{{Base + 0x1000, Base + 0x1040}, unwind::Range{Base + 0x1200, Base + 0x1210}};
    const auto fn = Checked(MakeFunction(function, modules));
    CHECK(!MakeFunction(std::nullopt, modules).has_value());

    dis::Listing listing;
    dis::Instruction row;
    row.address = Base + 0x1000;
    row.bytes = {0xe8, 0, 0, 0, 0};
    row.text = "call 0x140001005";
    row.target = Base + 0x1005;
    row.fields = {{1, 4, "relative"}};
    row.string = "Weapon.cpp";
    row.ends = true;
    listing.rows.push_back(row);
    listing.followed.push_back({Base + 0x1000, Base + 0x1005});
    listing.end = dis::End{Base + 0x1005, 5, "undecodable"};
    listing.stop = "undecodable";
    const auto json = Checked(MakeDisasm(Base + 0x1000, listing, function, modules));
    // Full rows (meta): each row's module and its address fields.
    CHECK(json["rows"][0]["module"] == "Game.exe");
    CHECK(json["rows"][0]["target_module"] == "Game.exe");
    CHECK(json["rows"][0].contains("fields"));
    // Compact rows (the default): the listing names the module once; rva, target and string stay.
    const auto compact = Checked(MakeDisasm(Base + 0x1000, listing, function, modules, false));
    CHECK(compact["module"] == "Game.exe");
    CHECK_FALSE(compact["rows"][0].contains("module"));
    CHECK_FALSE(compact["rows"][0].contains("target_module"));
    CHECK_FALSE(compact["rows"][0].contains("fields"));
    CHECK(compact["rows"][0]["rva"] == "0x1000");
    CHECK(compact["rows"][0]["target_rva"] == "0x1005");
    CHECK(compact["rows"][0]["string"] == "Weapon.cpp");
    CHECK(compact.dump().size() < json.dump().size());
    // A function listing without meta: no bytes, rva on the first row only, the strings summary, next.
    dis::Listing more = listing;
    dis::Instruction second = row;
    second.address = Base + 0x1005;
    more.rows.push_back(second);
    more.next = Base + 0x100a;
    const auto whole = Checked(MakeDisasm(Base + 0x1000, more, function, modules, false, true));
    CHECK_FALSE(whole["rows"][0].contains("bytes"));
    CHECK(whole["rows"][0]["rva"] == "0x1000");
    CHECK_FALSE(whole["rows"][1].contains("rva"));
    CHECK(whole["rows"][1]["target_rva"] == "0x1005");
    REQUIRE(whole["strings"].size() == 1);
    CHECK(whole["strings"][0]["text"] == "Weapon.cpp");
    CHECK(whole["strings"][0]["at"].size() == 2);
    CHECK(whole["next"] == "0x14000100a");
    const auto wholeFull = Checked(MakeDisasm(Base + 0x1000, more, function, modules, true, true));
    CHECK(wholeFull["rows"][1].contains("bytes"));
    CHECK(wholeFull["rows"][1]["rva"] == "0x1005");
    CHECK_FALSE(compact.contains("strings"));

    const std::vector<xref::Hit> hits{{Base + 0x2001, "call", Base + 0x2000}, {Base + 0x3000, "unknown", std::nullopt},
        {Base + 0x4000, "pointer", std::nullopt}};
    const std::vector<std::optional<unwind::Function>> functions{function, std::nullopt, std::nullopt};
    const auto xr = Checked(MakeXrefs(Base + 0x1000, modules[0], hits, functions, false, true, 12, modules));
    CHECK(xr["rows"].size() == 2);
    CHECK(xr["capped"] == true);
    CHECK(xr["kinds"]["unknown"] == 1);
    CHECK(xr["kinds"]["call"] == 1);
    CHECK(xr["rows"][0]["rva"] == "0x2000");
    CHECK(Checked(MakeXrefs(Base, modules[0], hits, functions, true, false, 0, modules))["rows"].size() == 3);
    CHECK_FALSE(xr["rows"][0].contains("unlisted"));

    // A code row no entry holds gets the piece holding it; the pointer row gets none.
    const std::vector<xref::Hit> loose{{Base + 0x5001, "call", Base + 0x5000}, {Base + 0x6000, "pointer", std::nullopt}};
    const std::vector<std::optional<unwind::Function>> none(2);
    const std::vector<std::optional<touch::Piece>> pieces{touch::Piece{Base + 0x4ff0, Base + 0x5020}, std::nullopt};
    const auto unlisted = Checked(MakeXrefs(Base + 0x1000, modules[0], loose, none, false, false, 0, modules, pieces));
    CHECK(unlisted["rows"][0]["unlisted"] == true);
    CHECK(unlisted["rows"][0]["function"]["start"] == "0x140004ff0");
    CHECK(unlisted["rows"][0]["function"]["end"] == "0x140005020");
    CHECK_FALSE(unlisted["rows"][1].contains("function"));
    CHECK_FALSE(unlisted["rows"][1].contains("unlisted"));

    // save: the file holds the listing, the result its summary.
    auto saved = MakeDisasm(Base + 0x1000, more, function, modules, false, true);
    const auto listed = saved.rows->size();
    KeepSaveSummary(saved, "fn.jsonl");
    const auto summary = Checked(std::move(saved));
    CHECK_FALSE(summary.contains("rows"));
    CHECK_FALSE(summary.contains("strings"));
    CHECK(summary["saved"] == "fn.jsonl");
    CHECK(summary["saved_rows"] == listed);
    CHECK(summary["next"] == "0x14000100a");
    CHECK(summary.contains("function"));
}

// Run-time type information and imports, read from this test program's own
// image: MSVC builds it with type information and it imports from kernel32.
namespace rtti_probe {
struct Shape { virtual ~Shape() = default; virtual int Sides() const { return 0; } };
struct Square final : Shape { int Sides() const override { return 4; } };
}
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include "reagentlive/frontends/cli/code/pe_names.h"
namespace {
// Reads this process's memory; an unreadable range is a missing item.
sig::ReadFn SelfRead() {
    return [](std::span<const ReadSpan> spans) -> std::expected<std::vector<std::optional<std::vector<uint8_t>>>, std::string> {
        std::vector<std::optional<std::vector<uint8_t>>> out;
        for (const auto& span : spans) {
            std::vector<uint8_t> bytes(span.length);
            SIZE_T read = 0;
            if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(span.address), bytes.data(), span.length, &read) &&
                read == span.length) out.emplace_back(std::move(bytes));
            else out.emplace_back(std::nullopt);
        }
        return out;
    };
}
Module SelfModule() {
    MODULEINFO info{};
    K32GetModuleInformation(GetCurrentProcess(), GetModuleHandleW(nullptr), &info, sizeof(info));
    return {"self.exe", reinterpret_cast<uint64_t>(info.lpBaseOfDll), info.SizeOfImage};
}
}

TEST_CASE("type names undecorate plain and nested classes and keep anything else as it is") {
    CHECK(pe::Undecorate(".?AVWeapon@@") == "Weapon");
    CHECK(pe::Undecorate(".?AUState@Game@Studio@@") == "Studio::Game::State");
    CHECK(pe::Undecorate(".?AV?$TArray@H@@") == ".?AV?$TArray@H@@");      // Templates stay decorated.
    CHECK(pe::Undecorate("not a type") == "not a type");
}

TEST_CASE("vtable rtti names a live object's class and its base from this program's own image") {
    const rtti_probe::Square square;
    const rtti_probe::Shape& shape = square;
    uint64_t table = 0;
    memcpy(&table, &shape, 8);
    const Module self = SelfModule();
    const std::vector<Module> modules{self};
    auto rtti = pe::ReadRtti(SelfRead(), modules, table);
    REQUIRE_MESSAGE(rtti.has_value(), rtti.error());
    CHECK(rtti->type.name == "rtti_probe::Square");
    CHECK(rtti->type.decorated == ".?AUSquare@rtti_probe@@");
    CHECK(rtti->offset == 0);
    REQUIRE(rtti->bases.size() == 1);
    CHECK(rtti->bases[0].name == "rtti_probe::Shape");
    // A table with no locator before it (the image's first bytes) is refused, not guessed.
    CHECK_FALSE(pe::ReadRtti(SelfRead(), modules, self.base + 0x10).has_value());
    // vtable rtti: the complete object is this part less its offset inside it.
    const auto json = Checked(MakeVtableRtti(0x5000, 0x10, *rtti));
    CHECK(json["name"] == "rtti_probe::Square");
    CHECK(json["complete"] == "0x5010");
    CHECK(json["bases"][0]["name"] == "rtti_probe::Shape");
}

TEST_CASE("unwind info of an unknown version is no chain: the entry stands on its own") {
    Image image;
    image.Headers(2);
    image.Entry(0, 0x1100, 0x1180, 0x8000);
    image.Entry(1, 0x1200, 0x1201, 0x8010);   // A decoy whose unwind data is garbage.
    image.Put(Base + 0x8000, {0x01, 0x04, 0x00, 0x00});
    image.Put(Base + 0x8010, {0x05 | (unwind::ChainInfo << 3), 0x00, 0xff, 0x00}); // Version 5, chain bit set.
    unwind::Table table(image.Read(), image.module, {PdataRva, 2 * unwind::EntrySize});
    const auto decoy = table.Lookup(Base + 0x1200);
    REQUIRE_MESSAGE(decoy.has_value(), decoy.error());   // Not "chained unwind data malformed".
    REQUIRE(*decoy);
    CHECK((*decoy)->range == unwind::Range{Base + 0x1200, Base + 0x1201});
    CHECK_FALSE((*decoy)->chunk);
}

TEST_CASE("an import with no name table is not named from the resolved import table") {
    Image image;
    constexpr uint32_t Descriptors = 0x9000, Table = 0x9100, Dll = 0x9200, Name = 0x9300;
    image.Put32(Base + Descriptors, 0);                  // OriginalFirstThunk: none.
    image.Put32(Base + Descriptors + 12, Dll);
    image.Put32(Base + Descriptors + 16, Table);
    image.Put(Base + Descriptors + 20, std::vector<uint8_t>(20, 0)); // The terminator.
    image.Put(Base + Dll, {'x', '.', 'd', 'l', 'l', 0});
    image.Put(Base + Name, {0, 0, 'J', 'u', 'n', 'k', 0});
    // A resolved address whose low half happens to land on name-like bytes.
    const uint64_t resolved = 0x7ff800000000 + Name;
    image.Put(Base + Table, std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&resolved),
                                                 reinterpret_cast<const uint8_t*>(&resolved) + 8));
    const pe::ImportDirectories directories{Descriptors, 40, Table, 16};
    CHECK_FALSE(pe::ImportAt(image.Read(), image.module, directories, Base + Table).has_value());
}

TEST_CASE("an import-table slot is named by its DLL and function") {
    const Module self = SelfModule();
    const auto read = SelfRead();
    const auto directories = pe::ReadImportDirectories(read, self);
    REQUIRE(directories.has_value());
    REQUIRE(directories->tableSize);
    // The slot that holds GetTickCount64 (this file calls it through the table).
    const auto wanted = reinterpret_cast<uint64_t>(&GetTickCount64);
    CHECK(GetTickCount64() > 0);
    uint64_t slot = 0;
    for (uint32_t at = 0; at + 8 <= directories->tableSize && !slot; at += 8) {
        uint64_t value = 0;
        memcpy(&value, reinterpret_cast<const void*>(self.base + directories->tableRva + at), 8);
        if (value == wanted) slot = self.base + directories->tableRva + at;
    }
    REQUIRE(slot);
    const auto name = pe::ImportAt(read, self, *directories, slot);
    REQUIRE(name.has_value());
    CHECK(name->ends_with("!GetTickCount64"));
    CHECK(name->find(".dll") != std::string::npos);
    CHECK_FALSE(pe::ImportAt(read, self, *directories, self.base + 0x10).has_value()); // Not a table slot.
}
