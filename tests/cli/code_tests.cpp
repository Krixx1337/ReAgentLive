// reagentlive CLI fast lane: function bounds from a prepared exception directory,
// disasm over fixed bytes, decoder-exact sig fields, xref kinds, syntax and
// result shapes. No live process: reads are served from a byte vector.
#include "reagentlive/frontends/cli/state/cache.h"
#include "reagentlive/frontends/cli/code/code_json.h"
#include "reagentlive/frontends/cli/syntax/parse.h"
#include "reagentlive/frontends/cli/code/disasm.h"
#include "reagentlive/frontends/cli/code/function_save.h"
#include "reagentlive/frontends/cli/code/image.h"
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
    CHECK(rows.size() == 24);   // Straight on past the ret: no --function bound.
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

TEST_CASE("disasm --follow steps through unconditional jumps only, at most three") {
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

TEST_CASE("fast-lane syntax") {
    const std::vector<Ref> refs;
    const std::vector<Module> modules{{"Game.exe", Base, 0x100000}};
    auto disasm = Parse("disasm Game.exe+0x1000 12 --function --follow", refs, modules);
    REQUIRE(disasm);
    CHECK(disasm->kind == Kind::Disasm);
    CHECK(disasm->request.address == Base + 0x1000);
    CHECK(disasm->limit == 12);
    CHECK(disasm->function);
    CHECK(disasm->follow);
    CHECK(Parse("disasm 0x1000", refs)->limit == DefaultDisasm);
    CHECK_FALSE(Parse("disasm 0x1000 65", refs));
    // --function: the whole function by default, a count up to its cap; --resume and --save need it.
    CHECK(Parse("disasm 0x1000 --function", refs)->limit == dis::MaxFunctionInstructions);
    CHECK(Parse("disasm 0x1000 300 --function", refs)->limit == 300);
    CHECK_FALSE(Parse("disasm 0x1000 2049 --function", refs));
    CHECK(Parse("disasm 0x1000 --function --resume", refs)->resume);
    CHECK(Parse("disasm 0x1000 --function --save fn.jsonl", refs)->file == "fn.jsonl");
    CHECK_FALSE(Parse("disasm 0x1000 --resume", refs));
    CHECK_FALSE(Parse("disasm 0x1000 --save fn.jsonl", refs));
    CHECK_FALSE(Parse("disasm 0x1000 --bogus", refs));

    auto xrefs = Parse("xrefs 0x1000 --module engine.dll --all", refs);
    REQUIRE(xrefs);
    CHECK(xrefs->kind == Kind::Xrefs);
    CHECK(xrefs->request.op == op::Xrefs);
    CHECK(xrefs->target == "engine.dll");
    CHECK(xrefs->all);
    CHECK_FALSE(Parse("xrefs", refs));

    auto text = Parse("search text Weapon.cpp --module Game.exe", refs);
    REQUIRE(text);
    CHECK(text->request.length == 10);
    CHECK(std::memcmp(text->request.data, "Weapon.cpp", 10) == 0);
    auto wide = Parse("search text Aim --wide", refs);
    REQUIRE(wide);
    CHECK(wide->request.length == 6);
    CHECK(wide->request.data[1] == 0);
    CHECK(wide->request.data[2] == 'i');
    CHECK_FALSE(Parse("search hex 01 --wide", refs));
    CHECK_FALSE(Parse("search text " + std::string(65, 'x'), refs));
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
    // Full rows (--meta): each row's module and its address fields.
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
    // A --function listing without --meta: no bytes, rva on the first row only, the strings summary, next.
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
    std::istringstream text(save::Lines(module, 7, Base + 0x1000, rows, data));
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

TEST_CASE("vtable syntax and result") {
    const std::vector<Ref> refs;
    auto parsed = Parse("vtable 0x1000 12 --offset 0xf0 --resolve", refs);
    REQUIRE(parsed);
    CHECK(parsed->kind == Kind::Vtable);
    CHECK(parsed->limit == 12);
    CHECK(parsed->offset == 0xf0);
    CHECK(parsed->bodies);
    CHECK(Parse("vtable 0x1000 --table", refs)->table);
    CHECK_FALSE(Parse("vtable 0x1000 --table --offset 8", refs));
    CHECK_FALSE(Parse("vtable 0x1000 513", refs));

    const std::vector<Module> modules{{"Game.exe", Base, 0x100000}};
    native::Slots slots{{{0, Base + 0x1100, Base + 0x1800}}, "not code", 5};
    const auto json = Checked(MakeVtable(Base + 0x9000, 0, Base + 0x5000, std::nullopt, slots, modules));
    const auto none = Checked(MakeVtable(0x1000, 0, std::nullopt, 0x1234, {}, modules));
    CHECK_FALSE(none.contains("table"));
}

TEST_CASE("pointer chains in read-only commands, refused for writes") {
    const std::vector<Ref> refs{{0x5000, 1, 1}};
    const std::vector<Module> modules{{"Game.exe", Base, 0x100000}};
    auto read = Parse("read $1+0x8->0x30->0x10 16", refs, modules);
    REQUIRE(read);
    CHECK(read->request.address == 0x5008);
    CHECK(read->chain == std::vector<uint64_t>{0x30, 0x10});
    CHECK(read->request.length == 16);
    CHECK(Parse("peek Game.exe+0x100->0->0x8", refs, modules)->chain == std::vector<uint64_t>{0, 0x8});
    CHECK(Parse("disasm 0x1000->0x8->0", refs)->chain.size() == 2);
    CHECK(Parse("vtable 0x1000->0->0x20 --offset 8", refs)->chain.size() == 2);
    // One ->off reads no pointer: refused, never answered with the bytes at the base.
    for (const char* line : {"read 0x1000->0x0", "peek $1->0x30", "region 0x1000->0x8", "disasm 0x1000->0"})
        CHECK(Parse(line, refs).error().starts_with(text::ChainNoHop));
    // The error spells the fix with the line's own address and offset.
    CHECK(Parse("peek $1->0x30", refs).error().contains("$1->0->0x30 reads the pointer at $1 and adds 0x30; $1+0x30"));
    CHECK(Parse("read 0x1000", refs)->chain.empty());
    CHECK_FALSE(Parse("read 0x1000->", refs));
    CHECK_FALSE(Parse("read 0x1000->x", refs));
    std::string tooLong = "read 0x1000";
    for (size_t i = 0; i <= MaxChainHops; ++i) tooLong += "->0x1";
    CHECK_FALSE(Parse(tooLong, refs));
    for (const char* line : {"write 0x1000->0x8 01", "pin 0x1000->0x8 01", "trial 0x1000->0x8 01 --owner $1 --expect 00"}) {
        auto refused = Parse(line, refs);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().starts_with(text::ChainRefused));
    }
}

TEST_CASE("read --type cstr and wstr") {
    const std::vector<Ref> refs;
    auto cstr = Parse("read 0x1000 --type cstr", refs);
    REQUIRE(cstr);
    CHECK(cstr->request.length == DefaultStringRead);
    CHECK(Parse("read 0x1000 33 --type wstr", refs)->request.length == 33);

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

TEST_CASE("region kinds, protection text, syntax and result") {
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

    const std::vector<Ref> refs;
    auto parsed = Parse("region 0x1000->0->0x8", refs);
    REQUIRE(parsed);
    CHECK(parsed->kind == Kind::Region);
    CHECK(parsed->request.op == op::Region);
    CHECK_FALSE(Parse("region 0x1000 extra", refs));

    const std::vector<Module> modules{{"Game.exe", Base, 0x100000}};
    const RegionInfo code{Base + 0x1000, 0x4000, Base, r::Commit, 0x20, 0x80, r::Image};
    const auto json = Checked(MakeRegion(Base + 0x1234, code, "code", ".text", modules));
    CHECK(json["protect"] == "rx");
    CHECK(json["type"] == "image");

    PeekValue mapped{0x1000, 0x1aac568d980, true};
    mapped.readable = true;
    mapped.region = "mapped";
    CHECK(Checked(MakePeekRow(mapped, modules))["label"] == "mapped");
    mapped.region = "private";
    CHECK(Checked(MakePeekRow(mapped, modules))["label"] == "heap");
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

TEST_CASE("touches cuts unlisted code at returns and int3 padding, and --reads drops copies") {
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

    const touch::Use read{Base, 0x538, false, {}}, write{Base, 0x538, true, {}}, other{Base, 0x540, false, {}};
    std::vector<touch::Row> rows(4);
    rows[0].start = 1; rows[0].uses = {read, write, other};   // 0x540 is read and not written: kept
    rows[1].start = 2; rows[1].uses = {read, write};          // a copy: dropped
    rows[2].start = 3; rows[2].uses = {write};                // writes only: dropped
    rows[3].start = 4; rows[3].uses = {read, other};          // two reads, no write
    touch::Rank(rows, true);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].start == 1);   // Two reads each; the row that writes ranks first, as without --reads.
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

TEST_CASE("touches syntax and result") {
    const std::vector<Ref> refs;
    const std::vector<Module> modules{{"Game.exe", Base, 0x100000}};
    auto touches = Parse("touches Game.exe+0x2000 0x538 0x540 0x538 --range 0x1000", refs, modules);
    REQUIRE(touches);
    CHECK(touches->kind == Kind::Touches);
    CHECK(touches->request.address == Base + 0x2000);
    CHECK(touches->offsets == std::vector<uint32_t>{0x538, 0x540});
    CHECK(touches->range == 0x1000);
    CHECK(Parse("touches 0x1000 0x10", refs)->range == touch::DefaultRange);
    CHECK_FALSE(Parse("touches 0x1000 0x10", refs)->readsOnly);
    CHECK(Parse("touches 0x1000 0x10 --reads", refs)->readsOnly);
    CHECK_FALSE(Parse("touches 0x1000", refs));
    CHECK_FALSE(Parse("touches 0x1000 0", refs));
    CHECK_FALSE(Parse("touches 0x1000 0x10 --range 0", refs));
    CHECK_FALSE(Parse("touches 0x1000 0x10 --range 0x400001", refs));
    CHECK_FALSE(Parse("touches 0x1000 1 2 3 4 5 6 7 8 9", refs));

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

TEST_CASE("rtti names a live object's class and its base from this program's own image") {
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
