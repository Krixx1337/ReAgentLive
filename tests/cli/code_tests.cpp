// reagentlive CLI fast lane: function bounds from a prepared exception directory,
// disasm over fixed bytes, decoder-exact sig fields, xref kinds, calls and
// result shapes. No live process: reads are served from a byte vector.
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

namespace {
constexpr uint64_t Base = 0x140000000;
constexpr uint64_t PdataRva = 0x5000;

struct Image {
    Module module;
    std::vector<uint8_t> bytes;
    std::vector<std::pair<uint64_t, uint64_t>> holes; // Unreadable ranges.
    size_t requests = 0;

    explicit Image(size_t size = 0x20000) : module{"Game.exe", Base, size}, bytes(size, 0xcc) {}
    void Put(uint64_t address, std::vector<uint8_t> data) {
        std::memcpy(bytes.data() + (address - module.base), data.data(), data.size());
    }
    void Put32(uint64_t address, uint32_t value) {
        std::memcpy(bytes.data() + (address - module.base), &value, 4);
    }
    // PE32+ headers with an exception directory of `count` entries at
    // PdataRva and two sections: .text (code, 0x1000..0x5000) and .rdata.
    void Headers(size_t count) {
        constexpr uint64_t Pe = Base + 0x80;
        Put32(Base + 0x3c, 0x80);
        Put32(Pe, 0x00004550);
        Put(Pe + 6, {2, 0});            // NumberOfSections.
        Put(Pe + 20, {0xf0, 0});        // SizeOfOptionalHeader.
        Put(Pe + 24, {0x0b, 0x02});
        Put32(Pe + 24 + 108, 16);
        Put32(Pe + 160, PdataRva);
        Put32(Pe + 164, static_cast<uint32_t>(count * unwind::EntrySize));
        const uint64_t sections = Pe + 24 + 0xf0;
        Put(sections, {'.', 't', 'e', 'x', 't', 0, 0, 0});
        Put32(sections + 8, 0x4000);
        Put32(sections + 12, 0x1000);
        Put32(sections + 36, 0x60000020);
        Put(sections + 40, {'.', 'r', 'd', 'a', 't', 'a', 0, 0});
        Put32(sections + 48, 0x3000);
        Put32(sections + 52, 0x5000);
        Put32(sections + 76, 0x40000040);
    }
    void Entry(size_t index, uint32_t begin, uint32_t end, uint32_t unwind) {
        const uint64_t at = Base + PdataRva + index * unwind::EntrySize;
        Put32(at, begin);
        Put32(at + 4, end);
        Put32(at + 8, unwind);
    }
    sig::ReadFn Read() {
        return [this](std::span<const ReadSpan> spans)
                   -> std::expected<std::vector<std::optional<std::vector<uint8_t>>>, std::string> {
            ++requests;
            REQUIRE(spans.size() <= MaxBatchReads);
            REQUIRE(ReadBatchSize(spans));
            std::vector<std::optional<std::vector<uint8_t>>> items;
            for (const auto& span : spans) {
                bool hole = span.address < module.base || span.address + span.length > module.base + module.size;
                for (const auto& [from, to] : holes) hole |= span.address < to && span.address + span.length > from;
                if (hole) { items.emplace_back(); continue; }
                const auto* at = bytes.data() + (span.address - module.base);
                items.emplace_back(std::vector<uint8_t>(at, at + span.length));
            }
            return items;
        };
    }
};

std::set<std::string> Keys(const Json& object) {
    std::set<std::string> keys;
    for (const auto& item : object.items()) keys.insert(item.key());
    return keys;
}
using KeySet = std::set<std::string>;
std::vector<uint8_t> Rel32(int32_t value) {
    std::vector<uint8_t> out(4);
    std::memcpy(out.data(), &value, 4);
    return out;
}
std::vector<uint8_t> Join(std::initializer_list<std::vector<uint8_t>> parts) {
    std::vector<uint8_t> out;
    for (const auto& part : parts) out.insert(out.end(), part.begin(), part.end());
    return out;
}
}

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

TEST_CASE("vtable call and result") {
    auto parsed = Call(R"({"verb": "vtable", "args": {"addr": "0x1000", "count": 12, "offset": "0xf0", "resolve": true}})");
    REQUIRE(parsed);
    CHECK(parsed->kind == Kind::Vtable);
    CHECK(parsed->limit == 12);
    CHECK(parsed->offset == 0xf0);
    CHECK(parsed->bodies);
    CHECK(Call(R"({"verb": "vtable", "args": {"addr": "0x1000", "table": true}})")->table);
    CHECK(Call(R"({"verb": "vtable", "args": {"addr": "0x1000", "table": true, "offset": 8}})").error().starts_with("offset"));
    CHECK(Call(R"({"verb": "vtable", "args": {"addr": "0x1000", "count": 513}})").error().starts_with("count"));

    const std::vector<Module> modules{{"Game.exe", Base, 0x100000}};
    native::Slots slots{{{0, Base + 0x1100, Base + 0x1800}}, "not code", 5};
    const auto json = Checked(MakeVtable(Base + 0x9000, 0, Base + 0x5000, std::nullopt, slots, modules));
    const auto none = Checked(MakeVtable(0x1000, 0, std::nullopt, 0x1234, {}, modules));
    CHECK_FALSE(none.contains("table"));
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
    const auto groups = touch::SameBase(hits, disps, [](uint64_t) { return std::optional<uint64_t>(Base); });
    REQUIRE(groups.size() == 1);
    CHECK(groups[0].base == "rdi");
    CHECK(groups[0].hits.size() == 2);
    CHECK(touch::SameBase(hits, disps, [](uint64_t) { return std::optional<uint64_t>(); }).empty());
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

TEST_CASE("touches cuts unlisted code at returns and int3 padding, and reads drops copies") {
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

    const touch::Use read{Base, 0x538, false, {}}, write{Base, 0x538, true, {}}, other{Base, 0x540, false, {}};
    std::vector<touch::Row> rows(4);
    rows[0].start = 1; rows[0].uses = {read, write, other};   // 0x540 is read and not written: kept
    rows[1].start = 2; rows[1].uses = {read, write};          // a copy: dropped
    rows[2].start = 3; rows[2].uses = {write};                // writes only: dropped
    rows[3].start = 4; rows[3].uses = {read, other};          // two reads, no write
    touch::Rank(rows, true);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].start == 1);   // Two reads each; the row that writes ranks first, as without reads.
    CHECK(rows[0].distinct == 2);
    CHECK(rows[1].start == 4);
    CHECK(rows[1].distinct == 2);
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
    CHECK_FALSE(Call(R"({"verb": "touches", "args": {"addr": "0x1000", "offsets": [16]}})")->readsOnly);
    CHECK(Call(R"({"verb": "touches", "args": {"addr": "0x1000", "offsets": [16], "reads": true}})")->readsOnly);
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
