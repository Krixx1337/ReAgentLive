// reagentlive CLI state files: readable names files and the console cache file.
#include "reagentlive/frontends/cli/state/cache.h"
#include "reagentlive/frontends/cli/state/ue/cache_ue.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/frontends/cli/state/usage.h"
#include "reagentlive/frontends/cli/state/ue/native_names.h"
#include "reagentlive/frontends/cli/values/layout_json.h"
#include "reagentlive/frontends/cli/results/ue/result_json_ue.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include "doctest.h"
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace reagentlive;
using namespace reagentlive::cli;

namespace {
struct NamedField {
    uint32_t offset, size;
    uint8_t boolMask;
    std::string name, owner;
};
}

TEST_CASE("names map obfuscated fields by owner, offset, size and bit") {
    std::istringstream file(
        "# comment\n"
        "Controller 0x330 24 0 ControlRotation\n"
        "Actor 0x60 1 0x4 bHidden   # bitfield\n"
        "\n"
        "InventorySlot 0xc 0 0 ItemId?\n");
    Names names;
    CHECK(names.Load(file) == 0);
    CHECK(names.Size() == 3);
    const NamedField rotation{0x330, 24, 0, "_qkzvxwmpltrnhdbgf", "Controller"};
    CHECK(names.Display(rotation) == "ControlRotation (_qkzvxwmpltrnhdbgf)");
    CHECK(names.Matches(rotation, "ControlRotation"));
    CHECK(names.Matches(rotation, "_qkzvxwmpltrnhdbgf"));
    CHECK(names.Display(NamedField{0x330, 12, 0, "_x", "Controller"}) == "_x");          // Size differs.
    CHECK(names.Display(NamedField{0x330, 24, 0, "_x", "PlayerController"}) == "_x");    // Other owner.
    CHECK(names.Display(NamedField{0x60, 1, 0x4, "_b", "Actor"}) == "bHidden (_b)");
    CHECK(names.Display(NamedField{0x60, 1, 0x2, "_b", "Actor"}) == "_b");                // Other bit.
    const NamedField slot{0xc, 8, 0, "_s", "InventorySlot"};
    CHECK(names.Display(slot) == "ItemId? (_s)");
    CHECK(names.Matches(slot, "ItemId"));
    CHECK(names.Display(NamedField{0x2f8, 8, 0, "Pawn", "Controller"}) == "Pawn");
}

TEST_CASE("names files report their first malformed line") {
    std::istringstream file("Controller 0x330 24 0 ControlRotation\nController 0x330 24 ControlRotation\n");
    Names names;
    CHECK(names.Load(file) == 2);
    std::istringstream extra("Controller 0x330 24 0 Control Rotation\n");
    CHECK(names.Load(extra) == 1);
}

TEST_CASE("the cache file round-trips and refuses partial or malformed text") {
    cli::CacheData data;
    data.owner = 0xabc;
    data.session = "20260101T000000-42";
    data.modules = {{"Game.exe", 0x7ff600000000, 0x1000}, {"engine dll.dll", 0x7ffa00000000, 0x2000}};
    // The last one is address-only: a pointer get followed, kept as its address.
    data.refs = {{0x7000, 1, 2}, {0x8000, 3, 4}, {0x9000, -1, 0}};
    data.packs = {{"ue", {"class\tA\t0x10#1:1", "table\t1\t0", "field\tends\tin\ta\ttab\t"}}, {"other", {}}};
    const auto text = SaveCache(data);
    const auto loaded = LoadCache(text);
    REQUIRE(loaded.has_value());
    CHECK(loaded->owner == data.owner);
    CHECK(loaded->session == data.session);
    REQUIRE(loaded->modules.size() == 2);
    CHECK(loaded->modules[1].name == "engine dll.dll");
    CHECK(loaded->modules[1].base == 0x7ffa00000000);
    CHECK(loaded->modules[1].size == 0x2000);
    CHECK(loaded->refs == data.refs);
    CHECK(loaded->packs == data.packs); // Kept verbatim, trailing tabs included.
    CHECK_FALSE(LoadCache(text.substr(0, text.size() - 4)).has_value()); // No end line.
    CHECK_FALSE(LoadCache("reagentlive cache 9\nowner\t0\nend\n").has_value());
    CHECK_FALSE(LoadCache("reagentlive cache 9\nowner\t5\nbogus\nend\n").has_value());
    CHECK_FALSE(LoadCache("reagentlive cache 9\nowner\t5\npack\tue\t3\nclass\tA\t0x10#1:1\nend\n").has_value());
    CHECK_FALSE(LoadCache("reagentlive cache 9\nowner\t5\npack\t\t0\nend\n").has_value()); // A pack has a name.
    CHECK(LoadCache("reagentlive cache 9\nowner\t5\nend\n").has_value());
    CHECK_FALSE(LoadCache("reagentlive cache 7\nowner\t5\nend\n").has_value()); // Older layout: dropped, not migrated.
    CHECK_FALSE(LoadCache("reagentlive cache 3\nowner\t5\nend\n").has_value()); // Older layout: dropped, not migrated.
}

TEST_CASE("the Unreal pack's cache block round-trips and refuses malformed lines") {
    UnrealCache data;
    data.classes = {{"PlayerController", Ref{0x5000, 12, 3}}};
    ue::Field field{0x10, 4, 0, "FloatProperty", "Health", "Pawn", 0, "", Ref{}, ""};
    ue::Field structField{0x20, 24, 0, "StructProperty", "Location", "Actor", 0, "", Ref{0x6000, 40, 1}, "Vector"};
    ue::Field mapField{0x30, 80, 0, "MapProperty", "Attributes", "Comp", 8, "StructProperty", Ref{0x7000, 41, 1},
        "GameplayTag", 0, 20, 8, 4, "FloatProperty", Ref{}, ""};
    data.tables = {{12, {field, structField, mapField}}};
    data.census = CensusBaseline{"Actor", {{"Projectile Base", Ref{0x9000, 50, 2}, 7, {Ref{0x9100, 60, 1}, Ref{0x9200, 61, 1}}},
        {"Pawn", Ref{0xa000, 51, 0}, 1}}};
    data.censusBefore = CensusBaseline{"Actor", {{"Pawn", Ref{0xa000, 51, 0}, 2, {Ref{0xa100, 62, 3}}}}};
    data.snap = Snapshot{Ref{0xb000, 52, 4}, {field, structField}, {0x00, 0x00, 0x80, 0x3f, 0xff}};
    // Through the whole file, as the console stores it.
    cli::CacheData file;
    file.owner = 5;
    file.packs = {{std::string(UnrealCachePack), SaveUnrealCache(data)}};
    const auto stored = LoadCache(SaveCache(file));
    REQUIRE(stored.has_value());
    REQUIRE(stored->packs.size() == 1);
    const auto loaded = LoadUnrealCache(stored->packs[0].lines);
    REQUIRE(loaded.has_value());
    CHECK(loaded->classes == data.classes);
    CHECK(loaded->tables == data.tables);
    CHECK(loaded->census == data.census);
    CHECK(loaded->censusBefore == data.censusBefore);
    CHECK(loaded->snap == data.snap);
    data.snap->bytes.clear(); // A snap of no bytes still round-trips.
    CHECK(LoadUnrealCache(SaveUnrealCache(data))->snap == data.snap);
    data.snap->bytes.assign(MaxCachedSnapBytes + 1, 0); // Too large: stays in its run.
    CHECK_FALSE(LoadUnrealCache(SaveUnrealCache(data))->snap.has_value());
    data.snap.reset();
    data.census = CensusBaseline{"", {}}; // The whole table, no rows.
    CHECK(LoadUnrealCache(SaveUnrealCache(data))->census == data.census);
    data.census.reset();
    CHECK_FALSE(LoadUnrealCache(SaveUnrealCache(data))->census.has_value());
    const auto block = [](std::initializer_list<std::string> lines) { return std::vector<std::string>(lines); };
    // Fewer rows than its census line says.
    CHECK_FALSE(LoadUnrealCache(block({"census\tActor\t2", "census-row\t0x10#1:1\t3\t-\tA"})).has_value());
    CHECK_FALSE(LoadUnrealCache(block({"census\tActor\t1", "census-row\t0x10#1:1\t3\tbad\tA"})).has_value());
    CHECK(LoadUnrealCache(block({"census\tActor\t1", "census-row\t0x10#1:1\t3\t-\tA"})).has_value());
    CHECK_FALSE(LoadUnrealCache(block({"table\t1\t3"})).has_value());
    CHECK_FALSE(LoadUnrealCache(block({"snap\t0x10#1:1\t0"})).has_value());              // No bytes line.
    CHECK_FALSE(LoadUnrealCache(block({"snap\t0x10#1:1\t0", "snap-bytes\tzz"})).has_value());
    CHECK(LoadUnrealCache(block({"snap\t0x10#1:1\t0", "snap-bytes\t01 02"}))->snap->bytes.size() == 2);
    CHECK_FALSE(LoadUnrealCache(block({"bogus"})).has_value());
    CHECK(LoadUnrealCache(block({})).has_value());
}

namespace {
ue::Field Reflected(std::string owner, std::string name, uint32_t offset, uint32_t size) {
    ue::Field field{};
    field.owner = std::move(owner);
    field.name = std::move(name);
    field.offset = offset;
    field.size = size;
    field.type = "IntProperty";
    return field;
}
// A zone: reflected fields around a padding gap at 0x538..0x5a0, and its base class's.
const std::vector<ue::Field> Zone{Reflected("Actor", "Tags", 0x10, 8), Reflected("TriggerVolume", "Radius", 0x530, 4),
    Reflected("TriggerVolume", "Owner", 0x5a0, 8)};
}

TEST_CASE("native members parse, replace by owner and offset, and show only for their class") {
    std::istringstream file(
        "# hand-owned\n"
        "TriggerVolume 0x538 u8 Phase 0x6730a1b2\n"
        "TriggerVolume 0x53c f32 Duration? 0x6730a1b2\n"
        "TriggerVolume 0x538 u8 MovePhase 0x6730a1b2\n"
        "OtherClass 0x40 f64[3] Start 0x6730a1b2\n");
    NativeNames native;
    REQUIRE(native.Load(file) == 0);
    const auto members = native.For(Zone);
    REQUIRE(members.size() == 2);
    CHECK(members[0]->field.name == "MovePhase"); // The later line for 0x538 replaced the first.
    CHECK(members[0]->line == 4);
    CHECK(members[1]->field.name == "Duration?");
    CHECK(NativeMatches(*members[1], "Duration"));
    CHECK(NativeMatches(*members[1], "Duration?"));
    CHECK_FALSE(NativeMatches(*members[1], "Dur"));
    std::string why;
    std::istringstream bad("TriggerVolume 0x538 f16 Phase 0x1\n");
    CHECK(NativeNames().Load(bad, &why) == 1);
    CHECK(why.starts_with("unknown type"));
    std::istringstream noStamp("TriggerVolume 0x538 u8 Phase\n");
    CHECK(NativeNames().Load(noStamp) == 1);
    std::istringstream pad("TriggerVolume 0x538 pad 4 Phase 0x1\n");
    CHECK(NativeNames().Load(pad) == 1);
}

TEST_CASE("native members are refused over reflected fields or past the class size, and stale after a patch") {
    std::istringstream file(
        "TriggerVolume 0x538 f64[3] Start 0x10\n"
        "TriggerVolume 0x52c u64 Overlap 0x10\n"
        "TriggerVolume 0x5a8 f32 Tail 0x10\n");
    NativeNames native;
    REQUIRE(native.Load(file) == 0);
    const auto members = native.For(Zone);
    REQUIRE(members.size() == 3);
    CHECK(CheckNative(*members[0], Zone, 0, 0).problem == "overlaps reflected TriggerVolume:Radius at 0x530");
    const auto start = CheckNative(*members[1], Zone, 0x5b0, 0x10);
    CHECK(start.problem.empty());
    CHECK_FALSE(start.stale);
    CHECK(CheckNative(*members[1], Zone, 0x5b0, 0x20).stale); // Another build of the main image.
    CHECK(CheckNative(*members[2], Zone, 0x5a8, 0x10).problem == "ends at 0x5ac, past the class size 0x5a8");
    CHECK(CheckNative(*members[2], Zone, 0, 0x10).problem.empty()); // Class size unknown.
}

TEST_CASE("label files a member under its class and appends a line the names file never sees") {
    CHECK(NativeOwner(Zone, 0x538) == "TriggerVolume");
    CHECK(NativeOwner(Zone, 0x20) == "Actor");
    CHECK(NativeOwner(Zone, 0x4) == "Actor");
    CHECK(NativeOwner(Zone, 0x20, "TriggerVolume") == "TriggerVolume");
    CHECK(NativeOwner(Zone, 0x20, "Unrelated") == "Actor");
    CHECK(NativeOwner({}, 0x20).empty());
    auto field = layout::ParseField("f64[3] Start", true);
    REQUIRE(field.has_value());
    field->offset = 0x540;
    CHECK(NativeLine("TriggerVolume", *field, 0x6730a1b2) == "TriggerVolume 0x540 f64[3] Start 0x6730a1b2");
    // Appended twice to the native file (as label does), beside a names file it never touches.
    const auto folder = std::filesystem::temp_directory_path() /
        ("reagentlive-native-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(folder);
    const auto nativePath = folder / "console.native.txt", namesPath = folder / "console.names.txt";
    std::ofstream(namesPath) << "# Generated by the host\nTriggerVolume 0x530 4 0 Radius\n";
    std::ofstream(nativePath, std::ios::trunc) << "";
    for (int i = 0; i < 2; ++i) std::ofstream(nativePath, std::ios::app) << NativeLine("TriggerVolume", *field, 0x6730a1b2) << '\n';
    NativeNames native;
    Names names;
    {
        std::ifstream in(nativePath), namesIn(namesPath);
        REQUIRE(native.Load(in) == 0);
        REQUIRE(names.Load(namesIn) == 0);
    }
    REQUIRE(native.For(Zone).size() == 1);
    CHECK(native.For(Zone)[0]->line == 2);
    CHECK(names.Size() == 1);
    // As dump shows it: a field row marked native, decoded by its type.
    const auto& member = *native.For(Zone)[0];
    auto row = ToJson(MakeNativeFieldRow(member, true));
    CHECK(row["type"] == "f64[3]");
    CHECK(row["size"] == 24);
    CHECK(row["native"] == true);
    CHECK(row["stale"] == true);
    std::vector<uint8_t> bytes(24);
    const double start[3] = {1, 2, 3};
    memcpy(bytes.data(), start, sizeof(start));
    CHECK(layout::Value(member.field, bytes) == Json{1.0, 2.0, 3.0});
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
}

namespace {
UsageLine Usage(std::string session, std::string verb, uint64_t out, std::string hash = "h") {
    UsageLine line;
    line.time = "2026-01-02T03:04:05Z";
    line.session = std::move(session);
    line.verb = std::move(verb);
    line.origin = "core";
    line.caller = "tool";
    line.hash = std::move(hash);
    line.out = out;
    return line;
}
}

TEST_CASE("a usage line round-trips, and a torn or foreign line is skipped") {
    auto line = Usage("s1", "read", 120);
    line.flags = {"--type"};
    line.in = 30;
    line.ok = false;
    line.refused = "syntax";
    line.ms = 1.26;
    line.batch = true;
    const auto text = UsageText(line);
    CHECK(text.find('\n') == std::string::npos);
    const auto parsed = ParseUsageLine(text);
    REQUIRE(parsed.has_value());
    CHECK(parsed->session == "s1");
    CHECK(parsed->flags == std::vector<std::string>{"--type"});
    CHECK(parsed->in == 30);
    CHECK(parsed->out == 120);
    CHECK_FALSE(parsed->ok);
    CHECK(parsed->refused == "syntax");
    CHECK(parsed->ms == doctest::Approx(1.3));
    CHECK(parsed->batch);
    CHECK_FALSE(ParseUsageLine(text.substr(0, text.size() / 2)).has_value());
    CHECK_FALSE(ParseUsageLine("{\"verb\": \"read\"}").has_value());
    CHECK(ParseUsage(text + "\n{torn\n" + UsageText(Usage("s2", "find", 1)) + "\n").size() == 2);
    CHECK(UsageHash("read 0x1000") == UsageHash("read 0x1000"));
    CHECK(UsageHash("read 0x1000") != UsageHash("read 0x1001"));
}

TEST_CASE("the usage log past its cap keeps its newest whole sessions") {
    std::string text;
    for (const char* session : {"old", "mid", "new"})
        for (int i = 0; i < 4; ++i) text += UsageText(Usage(session, "read", 1)) + "\n";
    CHECK_FALSE(TrimUsage(text, text.size()).has_value());
    const uint64_t perSession = text.size() / 3;
    // Room for two sessions in three quarters of the cap: old goes, mid and new stay whole.
    const auto trimmed = TrimUsage(text, perSession * 2 * 4 / 3 + 8);
    REQUIRE(trimmed.has_value());
    const auto lines = ParseUsage(*trimmed);
    REQUIRE(lines.size() == 8);
    CHECK(lines.front().session == "mid");
    CHECK(lines.back().session == "new");
}
