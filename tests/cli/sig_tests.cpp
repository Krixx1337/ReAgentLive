// reagentlive CLI signatures: pattern forms, the anchor, and matching against a
// stubbed image (search and reads served from a byte vector).
#include "reagentlive/frontends/cli/call/build.h"
#include "reagentlive/frontends/cli/results/result_json.h"
#include "reagentlive/frontends/cli/code/signature.h"
#include "reagentlive/frontends/cli/code/signature_json.h"
#include "reagentlive/frontends/cli/code/label_sign.h"
#include "reagentlive/frontends/cli/results/labels.h"
#include "doctest.h"
#include "verbs.h"
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace reagentlive;
using namespace reagentlive::cli;

namespace {
// A module image the stubs serve: search pages of anchor hits and reads.
struct Image {
    Module module;
    std::vector<uint8_t> bytes;
    size_t searches = 0, reads = 0;

    Image(uint64_t base, size_t size) : module{"Game.exe", base, size}, bytes(size, 0xcc) {}
    void Put(uint64_t address, std::vector<uint8_t> data) {
        std::memcpy(bytes.data() + (address - module.base), data.data(), data.size());
    }
    sig::SearchFn Search() {
        return [this](uint64_t from, uint64_t to, std::span<const uint8_t> anchor, size_t maxHits)
                   -> std::expected<sig::SearchPage, std::string> {
            ++searches;
            sig::SearchPage page;
            const uint64_t end = std::min(to, module.base + module.size);
            for (uint64_t at = from; at + anchor.size() <= end; ++at) {
                if (std::memcmp(bytes.data() + (at - module.base), anchor.data(), anchor.size())) continue;
                if (page.hits.size() == maxHits) { page.resume = at; break; }
                page.hits.push_back(at);
            }
            return page;
        };
    }
    sig::ReadFn Read() {
        return [this](std::span<const ReadSpan> spans)
                   -> std::expected<std::vector<std::optional<std::vector<uint8_t>>>, std::string> {
            ++reads;
            REQUIRE(spans.size() <= MaxBatchReads);
            REQUIRE(ReadBatchSize(spans));
            std::vector<std::optional<std::vector<uint8_t>>> items;
            for (const auto& span : spans) {
                if (span.address < module.base || span.address + span.length > module.base + module.size) {
                    items.emplace_back();
                    continue;
                }
                const auto* at = bytes.data() + (span.address - module.base);
                items.emplace_back(std::vector<uint8_t>(at, at + span.length));
            }
            return items;
        };
    }
    std::expected<sig::CheckResult, std::string> Check(const sig::Check& check) {
        return sig::Run(check, module, Search(), Read(), std::span(&module, 1));
    }
};

std::vector<uint64_t> Addresses(const sig::CheckResult& result) {
    std::vector<uint64_t> addresses;
    for (const auto& match : result.matches) addresses.push_back(match.address);
    return addresses;
}
std::vector<uint8_t> Le32(int32_t value) {
    std::vector<uint8_t> bytes(4);
    std::memcpy(bytes.data(), &value, 4);
    return bytes;
}
std::vector<uint8_t> Cat(std::initializer_list<std::vector<uint8_t>> parts) {
    std::vector<uint8_t> out;
    for (const auto& part : parts) out.insert(out.end(), part.begin(), part.end());
    return out;
}
std::set<std::string> Keys(const Json& object) {
    std::set<std::string> keys;
    for (const auto& item : object.items()) keys.insert(item.key());
    return keys;
}

sig::Pattern P(std::string_view text) {
    auto pattern = sig::ParsePattern(text);
    REQUIRE(pattern.has_value());
    return *pattern;
}
}

TEST_CASE("both pattern forms parse to the same bytes and mask") {
    const auto text = P("48 8b ?? ? 89 05");
    const auto list = P("0x48, 0x8B, -1, -1, 0x89, 5");
    CHECK(text == list);
    CHECK(text.Size() == 6);
    CHECK(text.Fixed() == 4);
    CHECK(text.fixed == std::vector<bool>{true, true, false, false, true, true});
    CHECK(P("{ 0x48, 0x8b, -1 }") == P("48 8b ??"));      // Braces as in source.
    CHECK(P("\"488b 89\"") == P("48 8b 89"));                // Glued hex pairs split; quotes dropped.
    CHECK(sig::Text(list) == "48 8b ?? ?? 89 05");
    CHECK(sig::List(text) == "0x48, 0x8B, -1, -1, 0x89, 0x05");
}

TEST_CASE("malformed and oversized patterns are refused with a clear message") {
    CHECK_FALSE(sig::ParsePattern("").has_value());
    CHECK_FALSE(sig::ParsePattern("48 8").has_value());
    CHECK_FALSE(sig::ParsePattern("48 zz").has_value());
    CHECK_FALSE(sig::ParsePattern("488b??89").has_value()); // A wildcard is its own token.
    CHECK_FALSE(sig::ParsePattern("0x48, 0x100").has_value());
    CHECK_FALSE(sig::ParsePattern("0x48, ??").has_value()); // A list wildcard is -1.
    std::string long_pattern;
    for (size_t i = 0; i <= sig::MaxPattern; ++i) long_pattern += "90 ";
    const auto refused = sig::ParsePattern(long_pattern);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().find("96 bytes") != std::string::npos);
    long_pattern.resize(sig::MaxPattern * 3);
    CHECK(sig::ParsePattern(long_pattern).has_value());
}

TEST_CASE("the anchor is the longest run of fixed bytes, at least 4") {
    auto anchor = sig::ChooseAnchor(P("48 8b ?? 11 22 33 44 55 ?? 66 77 88 99"));
    REQUIRE(anchor.has_value());
    CHECK(anchor->offset == 3);
    CHECK(anchor->length == 5);
    anchor = sig::ChooseAnchor(P("11 22 33 44 ?? 55 66 77 88")); // Equal runs: the first.
    REQUIRE(anchor.has_value());
    CHECK(anchor->offset == 0);
    const auto refused = sig::ChooseAnchor(P("48 8b 05 ?? 89 05 ?? ff 15"));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().find("4 fixed bytes in a row") != std::string::npos);
    std::string run;
    for (int i = 0; i < 80; ++i) run += "90 ";
    CHECK(sig::ChooseAnchor(P(run))->length == MaxSearchPattern); // The search takes 64 bytes.
}

TEST_CASE("sig-check builds the pattern and its options") {
    const std::vector<Module> modules{{"Game.exe", 0x140000000, 0x100000}, {"engine.dll", 0x7ffa00000000, 0x2000}};
    auto parsed = Call(R"({"verb": "sig-check", "args": {"pattern": "48 8b 05 11 ?? ?? 89 05", "module": "engine.dll",
        "expect": {"module": "game.exe", "rva": "0x1230"}, "offset": 4}})", modules);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Sig);
    CHECK(parsed->pattern == P("48 8b 05 11 ?? ?? 89 05"));
    CHECK(parsed->target == "engine.dll");
    CHECK(parsed->expect == 0x140001230);
    CHECK(parsed->offset == 4);
    parsed = Call(R"({"verb": "sig-check", "args": {"pattern": "0x48, 0x8B, 0x05, 0x11, -1, 0x89"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->target.empty()); // The exe.
    CHECK_FALSE(parsed->expect);
    CHECK_FALSE(Call(R"({"verb": "sig", "args": {"pattern": "48 8b 89 05"}})").has_value());
    CHECK_FALSE(Call(R"({"verb": "sig-check"})").has_value());
    CHECK_FALSE(Call(R"({"verb": "sig-check", "args": {"module": "x"}})").has_value());
    CHECK_FALSE(Call(R"({"verb": "sig-check", "args": {"pattern": "48 ?? 89 05"}})").has_value());               // No anchor.
    const auto offset = Call(R"({"verb": "sig-check", "args": {"pattern": "48 8b 89 05", "offset": 4}})"); // Offset without expect.
    REQUIRE_FALSE(offset.has_value());
    CHECK(offset.error().starts_with("offset"));
    CHECK_FALSE(Call(R"({"verb": "sig-check", "args": {"pattern": "48 8b 89 05", "expect": true}})").has_value());
    CHECK_FALSE(Call(R"({"verb": "sig-check", "args": {"pattern": "48 8b 89 05", "bogus": 1}})").has_value());
    // An expect in a module the list does not hold.
    CHECK_FALSE(Call(R"({"verb": "sig-check", "args": {"pattern": "48 8b 89 05", "expect": {"module": "x.dll", "rva": 16}}})",
        modules).has_value());
}

TEST_CASE("matching: one hit, two hits, none") {
    Image image(0x140000000, 0x4000);
    image.Put(0x140001000, {0x48, 0x8b, 0x05, 0x77, 0x10, 0x20, 0x30, 0x40, 0x89, 0x05});
    image.Put(0x140002000, {0x48, 0x8b, 0x05, 0x77, 0x99, 0x99, 0x99, 0x99, 0x89, 0x05});
    image.Put(0x140003000, {0x48, 0x8b, 0x05, 0x77, 0x10, 0x20, 0x30, 0x40, 0x89, 0x06}); // Anchor hit, no match.

    auto result = image.Check({P("48 8b 05 77 ?? ?? ?? ?? 89 05"), std::nullopt, 0});
    REQUIRE(result.has_value());
    CHECK(Addresses(*result) == std::vector<uint64_t>{0x140001000, 0x140002000});
    CHECK(result->anchorHits == 3);
    CHECK_FALSE(result->resolves);

    result = image.Check({P("48 8b 05 77 10 20 30 40 89 05"), std::nullopt, 0});
    REQUIRE(result.has_value());
    CHECK(Addresses(*result) == std::vector<uint64_t>{0x140001000});

    result = image.Check({P("48 8b 05 77 10 20 30 40 89 07"), std::nullopt, 0});
    REQUIRE(result.has_value()); // No hit is a result, not an error.
    CHECK(result->matches.empty());
}

TEST_CASE("a hit whose window crosses the module end is not a match") {
    Image image(0x140000000, 0x1000);
    image.Put(0x140000ffc, {0x11, 0x22, 0x33, 0x44});
    auto result = image.Check({P("11 22 33 44 ?? ??"), std::nullopt, 0});
    REQUIRE(result.has_value());
    CHECK(result->anchorHits == 1);
    CHECK(result->matches.empty());
    image.Put(0x140000000, {0x55, 0x11, 0x22, 0x33, 0x44}); // And one before the start.
    result = image.Check({P("?? ?? 11 22 33 44"), std::nullopt, 0});
    REQUIRE(result.has_value());
    CHECK(Addresses(*result) == std::vector<uint64_t>{0x140000ffa}); // Not the one starting before the image.
}

TEST_CASE("an anchor with too many hits is refused") {
    Image image(0x140000000, 0x8000); // All 0xcc.
    const auto refused = image.Check({P("cc cc cc cc ?? 90"), std::nullopt, 0});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().find("lengthen the pattern") != std::string::npos);
    CHECK(image.reads == 0);
}

TEST_CASE("many anchor hits page through search and read 8 at a time") {
    Image image(0x140000000, 0x10000);
    for (uint64_t i = 0; i < 600; ++i) image.Put(0x140000000 + i * 0x40, {0x11, 0x22, 0x33, 0x44, 0x55});
    const auto result = image.Check({P("11 22 33 44 55"), std::nullopt, 0});
    REQUIRE(result.has_value());
    CHECK(result->matches.size() == 600);
    CHECK(image.searches == 2);           // 512 per page.
    CHECK(image.reads == (600 + 7) / 8);
}

TEST_CASE("resolves: the only match at expect, with and without an offset") {
    Image image(0x140000000, 0x4000);
    image.Put(0x140001010, {0x48, 0x8b, 0x05, 0x10, 0x20, 0x30, 0x40});
    const auto pattern = P("48 8b 05 10 20 30 40");
    CHECK(image.Check({pattern, 0x140001010, 0})->resolves == true);
    CHECK(image.Check({pattern, 0x140001000, 0})->resolves == false);
    CHECK(image.Check({pattern, 0x140001000, 0x10})->resolves == true); // Starts 0x10 into the function.
    image.Put(0x140002000, {0x48, 0x8b, 0x05, 0x10, 0x20, 0x30, 0x40});
    CHECK(image.Check({pattern, 0x140001010, 0})->resolves == false); // Not unique.
}

TEST_CASE("sig-check results have their documented keys") {
    Image image(0x140000000, 0x4000);
    image.Put(0x140001010, {0x48, 0x8b, 0x05, 0x10, 0x20, 0x30, 0x40});
    const sig::Check check{P("48 8b 05 10 20 30 ??"), 0x140001000, 0x10, std::nullopt};
    const auto result = image.Check(check);
    REQUIRE(result.has_value());
    const auto json = Checked(MakeSigCheck(check, *result, image.module, std::span(&image.module, 1)));
    CHECK(json["unique"] == true);
    CHECK(json["resolves"] == true);
    CHECK(json["rows"][0]["rva"] == "0x1010");
    CHECK(json["fixed_bytes"] == 6);
}

TEST_CASE("no match at expect lists the fixed bytes that differ there") {
    Image image(0x140000000, 0x4000);
    image.Put(0x140001000, {0x48, 0x8b, 0x05, 0x77, 0x10, 0x20, 0x30, 0x40});
    const sig::Check check{P("48 8b 0d 77 10 ?? 30 41"), 0x140001000, 0, std::nullopt}; // Two typos.
    auto result = image.Check(check);
    REQUIRE(result.has_value());
    CHECK(result->matches.empty());
    REQUIRE(result->mismatches.has_value());
    CHECK(*result->mismatches == std::vector<sig::Mismatch>{{2, 0x0d, 0x05}, {7, 0x41, 0x40}});
    const auto json = Checked(MakeSigCheck(check, *result, image.module, std::span(&image.module, 1)));
    CHECK(json["mismatches"][1]["found"] == "0x40");
    CHECK(json["resolves"] == false);

    result = image.Check({P("48 8b 05 77 10 20 30 40"), 0x140001000, 0, std::nullopt});
    CHECK_FALSE(result->compared); // A match there: nothing to compare.
    const sig::Check outside{P("48 8b 05 77 10 20 30 40"), 0x150000000, 0, std::nullopt};
    result = image.Check(outside);
    CHECK(result->compared);
    CHECK_FALSE(result->mismatches); // Unreadable there.
    const auto unreadable = Checked(MakeSigCheck(outside, *result, image.module, std::span(&image.module, 1)));
    CHECK_FALSE(unreadable.contains("mismatches"));
    CHECK(unreadable["mismatch_error"] == "the bytes at the expected address are unreadable");
}

TEST_CASE("several matches: where each diverges from the reference past the pattern") {
    Image image(0x140000000, 0x4000);
    const std::vector<uint8_t> body{0x48, 0x89, 0x5c, 0x24, 0x08, 0x57};
    image.Put(0x140001000, Cat({body, {0x11, 0x22, 0x33}}));
    image.Put(0x140002000, Cat({body, {0x11, 0x99}}));
    image.Put(0x140003000, Cat({body, {0x11, 0x22, 0x33}})); // Same for the whole window read.
    auto result = image.Check({P("48 89 5c 24 08 57"), std::nullopt, 0, std::nullopt});
    REQUIRE(result.has_value());
    REQUIRE(result->matches.size() == 3);
    CHECK(result->reference == 0);
    CHECK(result->matches[1].divergesAt == 7);
    CHECK_FALSE(result->matches[2].divergesAt);
    CHECK_FALSE(result->uniqueAtLength); // One never diverges.

    image.Put(0x140003006, {0x12});
    const sig::Check check{P("48 89 5c 24 08 57"), 0x140002000, 0, std::nullopt};
    result = image.Check(check);
    CHECK(result->reference == 1); // The expected match is the reference.
    CHECK(result->matches[0].divergesAt == 7);
    CHECK(result->matches[2].divergesAt == 6);
    CHECK(result->uniqueAtLength == 8);
    const auto json = Checked(MakeSigCheck(check, *result, image.module, std::span(&image.module, 1)));
    CHECK(json["rows"][1]["reference"] == true);
    CHECK(json["rows"][2]["diverges_at"] == 6);
    CHECK(json["unique_at_length"] == 8);
}

TEST_CASE("fixed address fields are flagged when they point inside a module") {
    const Module game{"Game.exe", 0x140000000, 0x100000}, engine{"engine.dll", 0x7ffa00000000, 0x10000};
    const std::vector<Module> modules{game, engine};
    const uint64_t at = 0x140001000;
    const auto fields = [&](std::vector<uint8_t> bytes) {
        return sig::AddressFields(sig::FromBytes(bytes), at, modules);
    };
    using F = sig::AddressField;
    CHECK(fields(Cat({{0x90, 0xe8}, Le32(0x100)})) == std::vector<F>{{2, 4, "call", at + 6 + 0x100}});
    CHECK(fields(Cat({{0xe9}, Le32(-0x800)})) == std::vector<F>{{1, 4, "jump", at + 5 - 0x800}});
    CHECK(fields(Cat({{0x0f, 0x84}, Le32(0x40)})) == std::vector<F>{{2, 4, "branch", at + 6 + 0x40}});
    CHECK(fields(Cat({{0x48, 0x8b, 0x05}, Le32(0x2000)})) == std::vector<F>{{3, 4, "rip", at + 7 + 0x2000}});
    // mov dword [rip+d], imm32: the immediate follows the displacement.
    CHECK(fields(Cat({{0xc7, 0x05}, Le32(0x2000), {1, 0, 0, 0}})) == std::vector<F>{{2, 4, "rip", at + 10 + 0x2000}});
    // cmp byte [rip+d], imm8.
    CHECK(fields(Cat({{0x80, 0x3d}, Le32(0x2000), {1}})) == std::vector<F>{{2, 4, "rip", at + 7 + 0x2000}});
    // movss xmm0, [rip+d] (F3 0F 10 05).
    CHECK(fields(Cat({{0xf3, 0x0f, 0x10, 0x05}, Le32(0x30)})) == std::vector<F>{{4, 4, "rip", at + 8 + 0x30}});
    // mov rax, imm64 into another module.
    CHECK(fields({0x48, 0xb8, 0x10, 0x00, 0x00, 0x00, 0xfa, 0x7f, 0x00, 0x00}) ==
          std::vector<F>{{2, 8, "absolute", 0x7ffa00000010}});
    // Masked fields and constants are not flagged.
    CHECK(sig::AddressFields(P("e8 ?? ?? ?? ?? 48 8b 05 ?? ?? ?? ??"), at, modules).empty());
    CHECK(fields(Cat({{0xe8}, Le32(0x7fffff00)})).empty());        // Lands outside every module.
    CHECK(fields({0xb8, 0x01, 0x00, 0x00, 0x00, 0xc3}).empty());    // mov eax, 1; ret.
    CHECK(fields(Cat({{0x31, 0x05}, Le32(0x2000)})).size() == 1);   // xor [rip+d], eax.
    CHECK(fields(Cat({{0x50, 0x05}, Le32(0x2000)})).empty());       // push; add eax, imm32: no ModRM.
}

TEST_CASE("warnings are reported at the match") {
    Image image(0x140000000, 0x4000);
    image.Put(0x140001000, Cat({{0x48, 0x8b, 0x0d}, Le32(0x1000), {0xe8}, Le32(0x20), {0x48, 0x85, 0xc0}}));
    const sig::Check check{P("48 8b 0d ?? ?? ?? ?? e8 20 00 00 00 48 85 c0"), std::nullopt, 0, std::nullopt};
    const auto result = image.Check(check);
    REQUIRE(result.has_value());
    REQUIRE(result->matches.size() == 1);
    REQUIRE(result->warnings.size() == 1);
    CHECK(result->warnings[0] == sig::AddressField{8, 4, "call", 0x140001000 + 12 + 0x20});
    const auto json = Checked(MakeSigCheck(check, *result, image.module, std::span(&image.module, 1)));
    CHECK(json["warnings"][0]["target_rva"] == "0x102c");
}

TEST_CASE("resolve parses next, field and size") {
    CHECK(sig::ParseResolve("7,3") == sig::Resolve{7, 3, 4});
    CHECK(sig::ParseResolve("0x7,3,2") == sig::Resolve{7, 3, 2});
    CHECK(sig::ParseResolve("2,1,1") == sig::Resolve{2, 1, 1});
    CHECK_FALSE(sig::ParseResolve("7").has_value());
    CHECK_FALSE(sig::ParseResolve("7,3,8").has_value());
    CHECK_FALSE(sig::ParseResolve("7,3,4,1").has_value());
    CHECK_FALSE(sig::ParseResolve("7,3,").has_value());
    CHECK_FALSE(sig::ParseResolve("7,x").has_value());
    CHECK_FALSE(sig::ParseResolve("200,200").has_value()); // Past the bytes read.
}

TEST_CASE("reference signatures resolve their target, each size") {
    Image image(0x140000000, 0x4000);
    // lea rcx, [rip+0x1234]; call ...
    image.Put(0x140001000, Cat({{0x48, 0x8d, 0x0d}, Le32(0x1234), {0xe8, 0x00, 0x00, 0x00, 0x00, 0xcc}}));
    // jmp short -0x10, then a 16-bit value.
    image.Put(0x140002000, {0x5a, 0x5b, 0x5c, 0x5d, 0xeb, 0xf0, 0x34, 0x12});
    const auto lea = P("48 8d 0d ?? ?? ?? ?? e8 00 00 00 00");
    auto result = image.Check({lea, 0x14000223b, 0, sig::Resolve{7, 3, 4}});
    REQUIRE(result.has_value());
    CHECK(result->matches[0].resolved == 0x14000223b);
    CHECK(result->resolves == true);
    result = image.Check({lea, 0x14000223a, 0, sig::Resolve{7, 3, 4}});
    CHECK(result->resolves == false); // A wrong target.
    result = image.Check({P("5a 5b 5c 5d eb ??"), 0x140002000 + 6 - 0x10, 0, sig::Resolve{6, 5, 1}});
    CHECK(result->resolves == true);
    result = image.Check({P("5a 5b 5c 5d eb"), 0x140002000 + 8 + 0x1234, 0, sig::Resolve{8, 6, 2}});
    CHECK(result->resolves == true); // The field lies past the pattern, in the bytes read after it.
    CHECK_FALSE(result->compared);   // A resolve's expect is the target, not the match.
    const sig::Check check{lea, std::nullopt, 0, sig::Resolve{7, 3, 4}};
    const auto json = Checked(MakeSigCheck(check, *image.Check(check), image.module, std::span(&image.module, 1)));
    CHECK(json["rows"][0]["resolved_rva"] == "0x223b");
}

TEST_CASE("sig-check builds resolve and file; sig-bytes its length") {
    auto parsed = Call(R"({"verb": "sig-check", "args": {"pattern": "48 8d 0d 11 ?? ?? ?? ?? e8", "expect": "0x14000223b",
        "resolve": "7,3"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->resolve == sig::Resolve{7, 3, 4});
    CHECK_FALSE(Call(R"({"verb": "sig-check", "args": {"pattern": "48 8d 0d 11", "expect": "0x10", "offset": 2, "resolve": "7,3"}})")
        .has_value());
    CHECK_FALSE(Call(R"({"verb": "sig-check", "args": {"pattern": "48 8d 0d 11", "resolve": "7"}})").has_value());
    parsed = Call(R"({"verb": "sig-check", "args": {"file": "sigs.jsonl"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->sigMode == Command::SigMode::File);
    CHECK(parsed->file == "sigs.jsonl");
    CHECK_FALSE(Call(R"({"verb": "sig-check", "args": {"file": true}})").has_value());
    CHECK_FALSE(Call(R"({"verb": "sig-check", "args": {"pattern": "48 8b", "file": "sigs.jsonl"}})").has_value());
    CHECK_FALSE(Call(R"({"verb": "sig-check", "args": {"file": "sigs.jsonl", "module": "x.dll"}})").has_value());
    CHECK_FALSE(Call(R"({"verb": "sig-file", "args": {"file": "sigs.jsonl"}})").has_value());
    parsed = Call(R"({"verb": "sig-bytes", "args": {"addr": "0x140001000"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->sigMode == Command::SigMode::Bytes);
    CHECK(parsed->request.length == 64);
    CHECK(Call(R"({"verb": "sig-bytes", "args": {"addr": "0x140001000", "len": 96}})")->request.length == 96);
    CHECK_FALSE(Call(R"({"verb": "sig-bytes", "args": {"addr": "0x140001000", "len": 96}})")->capped);
    const auto capped = Call(R"({"verb": "sig-bytes", "args": {"addr": "0x140001000", "len": 200}})"); // Cut to the longest pattern, not refused.
    REQUIRE(capped.has_value());
    CHECK(capped->request.length == sig::MaxPattern);
    CHECK(capped->capped);
    CHECK_FALSE(Call(R"({"verb": "sig-bytes", "args": {"addr": "0x140001000", "len": 0}})").has_value());
    CHECK_FALSE(Call(R"({"verb": "sig-bytes"})").has_value());
}

TEST_CASE("patterns file lines parse and the winner is the first unique one that resolves") {
    auto entry = ParseSigLine(R"({"pattern": "48 8b 05 77 ?? ??", "module": "engine.dll", "rva": "0x1000", "offset": 4})");
    REQUIRE(entry.has_value());
    CHECK(entry->pattern == P("48 8b 05 77 ?? ??"));
    CHECK(entry->module == "engine.dll");
    CHECK(entry->rva == 0x1000);
    CHECK(entry->offset == 4);
    entry = ParseSigLine(R"({"pattern": [72, 139, 13, 119, -1], "rva": 4096, "resolve": [7, 3]})");
    REQUIRE(entry.has_value());
    CHECK(entry->pattern == P("48 8b 0d 77 ??"));
    CHECK(entry->resolve == sig::Resolve{7, 3, 4});
    CHECK(ParseSigLine(R"({"pattern": "48 8b 0d 77", "resolve": "7,3,2"})")->resolve == sig::Resolve{7, 3, 2});
    CHECK_FALSE(ParseSigLine("not json").has_value());
    CHECK_FALSE(ParseSigLine(R"({"rva": 1})").has_value());
    CHECK_FALSE(ParseSigLine(R"({"pattern": "48 ?? 0d 77"})").has_value());              // No anchor.
    CHECK_FALSE(ParseSigLine(R"({"pattern": "48 8b 0d 77", "extra": 1})").has_value());
    CHECK_FALSE(ParseSigLine(R"({"pattern": "48 8b 0d 77", "offset": 2})").has_value()); // Offset needs an rva.

    Image image(0x140000000, 0x4000);
    image.Put(0x140001000, {0x48, 0x8b, 0x05, 0x77, 0x01});
    image.Put(0x140002000, {0x48, 0x8b, 0x05, 0x77, 0x02});
    sig::Checker checker(image.Search(), image.Read(), std::span(&image.module, 1));
    const auto run = [&](sig::Check check) { return std::optional(*checker.Run(check, image.module)); };
    std::vector<std::optional<sig::CheckResult>> results;
    results.push_back(run({P("48 8b 05 77"), 0x140001000, 0, std::nullopt}));        // Two hits.
    results.push_back(run({P("48 8b 05 77 ?? cc"), 0x140001000, 0, std::nullopt}));  // Two hits too.
    results.emplace_back();                                                           // A bad line.
    results.push_back(run({P("48 8b 05 77 02"), 0x140001000, 0, std::nullopt}));     // Unique, elsewhere.
    results.push_back(run({P("48 8b 05 77 01"), 0x140001000, 0, std::nullopt}));
    results.push_back(run({P("48 8b 05 77 01 ??"), std::nullopt, 0, std::nullopt}));
    CHECK(sig::Winner(results) == 4);
    CHECK(checker.Searches() == 3); // One per distinct anchor, shared by the patterns that have it.
    CHECK(image.searches == 3);
    CHECK(sig::Winner(std::span(results).subspan(5)) == 0); // Without an expect, unique is enough.
}

TEST_CASE("sig-bytes suggests the same bytes with the warned fields masked") {
    const Module game{"Game.exe", 0x140000000, 0x100000};
    const uint64_t at = 0x140001000;
    const auto bytes = Cat({{0x48, 0x8b, 0x05}, Le32(0x2000), {0xe8}, Le32(0x10), {0x48, 0x85, 0xc0, 0x74, 0x05}});
    const auto json = Checked(MakeSigBytes(at, bytes, std::span(&game, 1)));
    CHECK(json["pattern"] == "48 8b 05 00 20 00 00 e8 10 00 00 00 48 85 c0 74 05");
    CHECK(json["suggested"]["pattern"] == "48 8b 05 ?? ?? ?? ?? e8 ?? ?? ?? ?? 48 85 c0 74 05");
    CHECK(json["suggested"]["list"].get<std::string>().starts_with("0x48, 0x8B, 0x05, -1"));
    CHECK(json["suggested"]["fixed_bytes"] == 9);
    // The fields agree with the warnings for the same bytes left fixed.
    const auto warned = sig::AddressFields(sig::FromBytes(bytes), at, std::span(&game, 1));
    REQUIRE(json["suggested"]["fields"].size() == warned.size());
    for (size_t i = 0; i < warned.size(); ++i) {
        CHECK(json["suggested"]["fields"][i]["offset"] == warned[i].offset);
        CHECK(json["suggested"]["fields"][i]["kind"] == warned[i].kind);
    }
    CHECK(sig::AddressFields(*sig::ParsePattern(json["suggested"]["pattern"].get<std::string>()), at,
                             std::span(&game, 1)).empty());
}

TEST_CASE("a labels row keeps fields past the six, so an export never drops a signature") {
    const Module module{"Game.exe", 0x7ff600000000, 0x100000};
    auto json = Checked(MakeLabel(module, 7, module.base + 0x20, "Ricochet_Resolve", "user"));
    json["sig"] = Json{{"pattern", "48 8b c4"}};
    json["ported_from"] = 6;
    const std::vector<std::string> lines{json.dump()};
    const auto kept = KeepLabels(lines, module, 7, [](std::string_view kind) { return kind == "exec"; });
    REQUIRE(kept.rows.size() == 1);
    CHECK(kept.rows[0].extra == Json{{"sig", {{"pattern", "48 8b c4"}}}, {"ported_from", 6}});
    CHECK(MergeLabels({kept.rows[0], {0x20, "A::execB", "exec"}})[0].extra.contains("sig")); // The user row wins with it.
}

TEST_CASE("a label signature reads back as written, and a bad one is refused") {
    const auto sig = ParseLabelSig(Json{{"pattern", "48 8d 0d ?? ?? ?? ?? e8 48 85 c9"}, {"resolve", "7,3,4"}});
    REQUIRE(sig.has_value());
    CHECK(sig->resolve == sig::Resolve{7, 3, 4});
    CHECK(ParseLabelSig(LabelSigJson(*sig)).has_value());
    CHECK(LabelSigJson(*sig) == Json{{"pattern", "48 8d 0d ?? ?? ?? ?? e8 48 85 c9"}, {"resolve", "7,3,4"}});
    CHECK(LabelSigJson(LabelSig{*sig::ParsePattern("48 8b c4 55"), 0, std::nullopt}) == Json{{"pattern", "48 8b c4 55"}});
    CHECK_FALSE(ParseLabelSig(Json{{"pattern", "?? ?? ??"}}).has_value());          // No anchor.
    CHECK_FALSE(ParseLabelSig(Json{{"pattern", "48 8b c4 55"}, {"offset", 2}, {"resolve", "7,3"}}).has_value());
    CHECK_FALSE(ParseLabelSig(Json("48 8b")).has_value());
    CHECK(CutPattern(*sig::ParsePattern("48 8d 0d ?? ?? ?? ?? e8 48 85 c9"), 6).Size() == 3);  // Trailing wildcards dropped.
}

namespace {
sig::CheckResult Matches(size_t count, bool resolves, std::optional<size_t> uniqueAt = std::nullopt) {
    sig::CheckResult result;
    for (size_t i = 0; i < count; ++i) result.matches.push_back({0x1000 + i * 0x100, {}, std::nullopt, std::nullopt});
    result.resolves = resolves;
    result.uniqueAtLength = uniqueAt;
    return result;
}
}

TEST_CASE("a signature grows until one match lands on the row, jumping to unique_at_length") {
    const auto full = sig::FromBytes(std::vector<uint8_t>(96, 0x90));
    std::vector<size_t> tried;
    auto grown = GrowSignature(full, 0, {}, [&](const sig::Pattern& cut) -> std::expected<sig::CheckResult, std::string> {
        tried.push_back(cut.Size());
        if (cut.Size() < 32) return Matches(3, false, 41);   // Known divergence: jump straight there.
        return cut.Size() >= 41 ? Matches(1, true) : Matches(2, false);
    });
    REQUIRE(grown.pattern.has_value());
    CHECK(tried == std::vector<size_t>{16, 41});
    CHECK(grown.pattern->Size() == 41);

    tried.clear();
    grown = GrowSignature(full, 0, {}, [&](const sig::Pattern& cut) -> std::expected<sig::CheckResult, std::string> {
        tried.push_back(cut.Size());
        if (cut.Size() < 48) return std::unexpected(std::string("the anchor has more than 4096 hits"));
        return Matches(2, false);                            // Never separates.
    });
    CHECK_FALSE(grown.pattern.has_value());
    CHECK(tried == std::vector<size_t>{16, 32, 48, 64, 80, 96});
    CHECK(grown.reason == "not unique at 96 bytes (2 matches)");

    grown = GrowSignature(full, 20, {}, [&](const sig::Pattern& cut) -> std::expected<sig::CheckResult, std::string> {
        CHECK(cut.Size() == 20);                             // A global's site: at least its instruction.
        return Matches(0, false);
    });
    CHECK(grown.reason == "no match where the row lies");   // More bytes cannot help.

    const std::vector<size_t> ends{5, 12, 19, 26, 40, 50, 93};  // Whole instructions; the last one runs past 96.
    tried.clear();
    grown = GrowSignature(full, 0, ends, [&](const sig::Pattern& cut) -> std::expected<sig::CheckResult, std::string> {
        tried.push_back(cut.Size());
        if (cut.Size() < 41) return Matches(3, false, 41);
        return Matches(2, false);
    });
    CHECK(tried == std::vector<size_t>{19, 50, 93});          // Each cut ends on an instruction, never past the last.
    CHECK(grown.reason == "not unique at 93 bytes (2 matches)");
}

TEST_CASE("labels-port looks for each name this build lacks, from the newest signed row") {
    const Module module{"Game.exe", 0x7ff600000000, 0x100000};
    const Module old{"Game.exe", 0x7ff600000000, 0xf0000};
    const auto row = [](const Module& m, uint32_t stamp, uint64_t rva, std::string_view name, std::string_view pattern) {
        auto json = Checked(MakeLabel(m, stamp, m.base + rva, name, "user"));
        if (!pattern.empty()) json["sig"] = Json{{"pattern", pattern}};
        return json.dump();
    };
    const std::vector<std::string> lines{
        row(old, 5, 0x10, "Moved", "48 8b c4 55"),
        row(old, 6, 0x10, "Moved", "48 89 5c 24"),   // Newer build: its signature wins.
        row(old, 6, 0x20, "Unsigned", ""),
        row(old, 6, 0x30, "Here", "40 53 48 83"),
        row(module, 7, 0x40, "Here", ""),            // Already named in this build.
        row(Module{"Other.dll", 0, 0x1000}, 6, 0x10, "Elsewhere", "40 53 48 83")};
    const auto plan = PlanPort(lines, module, 7);
    REQUIRE(plan.candidates.size() == 1);
    CHECK(plan.candidates[0].name == "Moved");
    CHECK(plan.candidates[0].from == 6);
    CHECK(sig::Text(plan.candidates[0].sig.pattern) == "48 89 5c 24");
    CHECK(plan.present == std::vector<std::string>{"Here"});
    CHECK(plan.unsignedNames == 1);

    const LabelSig function{*sig::ParsePattern("48 89 5c 24"), 2, std::nullopt};
    auto one = Matches(1, false);
    one.matches[0].address = module.base + 0x502;
    CHECK(PortResult(function, one, module).rva == 0x500);   // The function lies offset before its match.
    CHECK(PortResult(function, Matches(2, false), module).status == "ambiguous");
    CHECK(PortResult(function, Matches(0, false), module).status == "missing");
    CHECK(PortResult(function, std::unexpected(std::string("search: timeout")), module).reason == "search: timeout");
    LabelSig global = function;
    global.offset = 0;
    global.resolve = sig::Resolve{7, 3, 4};
    one.matches[0].resolved = module.base + 0x9000;
    CHECK(PortResult(global, one, module).rva == 0x9000);
    one.matches[0].resolved = 0x10;
    CHECK(PortResult(global, one, module).status == "missing"); // Resolves outside the module.

    // labels-check: the same search, judged against the row's own rva.
    one.matches[0].resolved = module.base + 0x9000;
    CHECK(CheckLabel(global, one, module, 0x9000).status == "ok");
    const auto moved = CheckLabel(global, one, module, 0x8000);
    CHECK(moved.status == "moved");
    CHECK(moved.found == 0x9000u);
    CHECK(CheckLabel(function, Matches(2, false), module, 0x500).status == "ambiguous");
    CHECK(CheckLabel(function, Matches(0, false), module, 0x500).status == "missing");
}
