// reagentlive CLI syntax: command lines to requests.
#include "reagentlive/frontends/cli/syntax/parse.h"
#include "reagentlive/packs/ue/protocol_ue.h"
#include "reagentlive/version.h"
#include "doctest.h"
#include "verbs.h"
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace reagentlive;
using namespace reagentlive::cli;
namespace {
const std::vector<Ref> NoRefs;
const std::vector<Ref> TwoRefs{{0x1000, 5, 9}, {0x2000, 7, 0}};
}

TEST_CASE("find takes the rest of the line as the search text") {
    const auto parsed = Parse("find  PlayerController ", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.op == ue::op::Find);
    CHECK(std::string_view(parsed->request.text) == "PlayerController");
    CHECK(ue::Check(parsed->request) == Status::Ok);
}

TEST_CASE("read defaults to 64 bytes and accepts hex and decimal") {
    auto parsed = Parse("read 0x7ff0 ", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.address == 0x7ff0);
    CHECK(parsed->request.length == 64);
    parsed = Parse("read 4096 8", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.address == 4096);
    CHECK(parsed->request.length == 8);
}

TEST_CASE("addresses resolve find results and offsets") {
    const auto parsed = Parse("read $2+0x20 4", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.address == 0x2020);
    CHECK_FALSE(Parse("read $3 4", TwoRefs).has_value());
    CHECK_FALSE(Parse("read $0 4", TwoRefs).has_value());
}

TEST_CASE("addresses accept base and module names with an offset inside the image") {
    const std::vector<Module> modules{{"Game.exe", 0x7ff600000000, 0x100000}, {"engine.dll", 0x7ffa00000000, 0x2000}};
    auto parsed = Parse("read base+0x10 4", NoRefs, modules);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.address == 0x7ff600000010);
    parsed = Parse("read ENGINE.DLL+0x1ff8 8", NoRefs, modules); // Names ignore case.
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.address == 0x7ffa00001ff8);
    parsed = Parse("read engine.dll 8", NoRefs, modules);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.address == 0x7ffa00000000);
    CHECK_FALSE(Parse("read engine.dll+0x2000 8", NoRefs, modules).has_value()); // Past the image.
    CHECK_FALSE(Parse("read other.dll+0x10 8", NoRefs, modules).has_value());    // Not loaded.
    CHECK_FALSE(Parse("read base+0x10 8", NoRefs).has_value());                  // List unknown.
    CHECK(Parse("search ptr game.exe+0x20", NoRefs, modules)->request.data[0] == 0x20);
}

TEST_CASE("export-labels takes an optional file and an optional module") {
    auto parsed = Parse("export-labels labels.jsonl", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::ExportLabels);
    CHECK(parsed->file == "labels.jsonl");
    CHECK(parsed->target.empty());
    parsed = Parse("export-labels labels.jsonl --module engine.dll", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->target == "engine.dll");
    parsed = Parse("export-labels", NoRefs);              // The console's labels file.
    REQUIRE(parsed.has_value());
    CHECK(parsed->file.empty());
    CHECK_FALSE(Parse("export-labels labels.jsonl --module", NoRefs).has_value());
    CHECK(parsed->limit == label_kind::All); // Every kind by default.
    parsed = Parse("export-labels labels.jsonl --kinds interface,global --module engine.dll", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->limit == (label_kind::Interface | label_kind::Global));
    CHECK(parsed->target == "engine.dll");
    CHECK_FALSE(Parse("export-labels labels.jsonl --kinds exec,types", NoRefs).has_value()); // Unknown kind.
    CHECK_FALSE(Parse("export-labels labels.jsonl --kinds", NoRefs).has_value());
    CHECK_FALSE(Parse("export-labels labels.jsonl --kinds exec,", NoRefs).has_value());
}

TEST_CASE("entries takes an object, a field and an optional cursor") {
    auto parsed = Parse("entries $1 Comp:Attributes", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Entries);
    CHECK(parsed->request.op == ue::op::Entries);
    CHECK(parsed->target == "Comp:Attributes");
    CHECK(parsed->limit == 0);
    parsed = Parse("entries $1 Attributes --start 12", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->limit == 12);
    CHECK_FALSE(Parse("entries $1", TwoRefs).has_value());
    CHECK_FALSE(Parse("entries $1 Attributes --start", TwoRefs).has_value());
    CHECK_FALSE(Parse("entries $1 Attributes --start 1 2", TwoRefs).has_value());
}

TEST_CASE("peek reads up to 64 qwords and labels exactly one") {
    const std::vector<Module> modules{{"Game.exe", 0x7ff600000000, 0x100000}};
    auto parsed = Parse("peek base+0x100", NoRefs, modules);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Peek);
    CHECK(parsed->request.op == op::Read);
    CHECK(parsed->request.address == 0x7ff600000100);
    CHECK(parsed->limit == 8);
    CHECK(parsed->request.length == 64);
    parsed = Parse("peek $1 64", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.length == 512);
    CHECK_FALSE(Parse("peek $1 65", TwoRefs).has_value());
    CHECK_FALSE(Parse("peek $1 0", TwoRefs).has_value());
    parsed = Parse("peek base+0x100 --label labels.jsonl", NoRefs, modules);
    REQUIRE(parsed.has_value());
    CHECK(parsed->limit == 1);
    CHECK(parsed->file == "labels.jsonl");
    CHECK(parsed->target.empty());
    parsed = Parse("peek base+0x100 1 --label labels.jsonl --name TagGlobal", NoRefs, modules);
    REQUIRE(parsed.has_value());
    CHECK(parsed->target == "TagGlobal");
    CHECK_FALSE(Parse("peek base+0x100 2 --label labels.jsonl", NoRefs, modules).has_value());
    parsed = Parse("peek base+0x100 --label", NoRefs, modules);   // The console's labels file.
    REQUIRE(parsed.has_value());
    CHECK(parsed->label);
    CHECK(parsed->file.empty());
    CHECK_FALSE(Parse("peek base+0x100 --label a b c", NoRefs, modules).has_value());
    CHECK_FALSE(Parse("peek base+0x100 --name TagGlobal", NoRefs, modules).has_value()); // A name labels nothing by itself.
}

TEST_CASE("read rejects zero, oversized and extra arguments") {
    CHECK_FALSE(Parse("read 0x10 0", NoRefs).has_value());
    CHECK_FALSE(Parse("read 0x10 4097", NoRefs).has_value());
    CHECK_FALSE(Parse("read 0x10 4 5", NoRefs).has_value());
    CHECK_FALSE(Parse("read 0 4", NoRefs).has_value());
    CHECK_FALSE(Parse("read zz 4", NoRefs).has_value());
}

TEST_CASE("write parses spaced and packed hex bytes") {
    auto parsed = Parse("write 0x10 00 00 80 3f", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.length == 4);
    CHECK(parsed->request.data[2] == 0x80);
    CHECK(parsed->request.data[3] == 0x3f);
    parsed = Parse("write 0x10 0080", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.length == 2);
    CHECK_FALSE(Parse("write 0x10 0", NoRefs).has_value());
    CHECK_FALSE(Parse("write 0x10 xy", NoRefs).has_value());
    CHECK_FALSE(Parse("write 0x10", NoRefs).has_value());
}

TEST_CASE("write is bounded by the agent's write limit") {
    std::string line = "write 0x10 ";
    for (size_t i = 0; i <= MaxWrite; ++i) line += "00";
    CHECK_FALSE(Parse(line, NoRefs).has_value());
}

TEST_CASE("func --script and refs parse to their kinds") {
    auto parsed = Parse("script $1 Owner:GetSpeed", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Script);
    CHECK(std::string_view(parsed->request.text) == "Owner:GetSpeed");
    CHECK_FALSE(Parse("func $1 Owner:GetSpeed --script", TwoRefs).has_value());
    parsed = Parse("refs #145935", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Refs);
    CHECK(parsed->request.args[0] == 145935);
    parsed = Parse("refs Attribute.Gameplay.Character.ReloadSpeedMultiplier", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.args[0] == 0);
    CHECK(std::string_view(parsed->request.text) == "Attribute.Gameplay.Character.ReloadSpeedMultiplier");
    CHECK_FALSE(Parse("refs", NoRefs).has_value());
    CHECK_FALSE(Parse("refs #0", NoRefs).has_value());
}

TEST_CASE("write --undo needs an owner ref and expected bytes") {
    auto parsed = Parse("write $1+0x10 2a 00 00 00 --undo 5 --owner $1 --expect 07 00 00 00", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Trial);
    CHECK(parsed->request.op == op::Trial);
    CHECK(parsed->request.flags == static_cast<uint32_t>(TrialAction::Add));
    CHECK(parsed->request.address == 0x1010);
    CHECK(parsed->request.length == 4);
    CHECK(parsed->request.data[MaxTrialBytes] == 7);
    CHECK(parsed->owner == Ref{0x1000, 5, 9});
    CHECK(parsed->object == 0);
    CHECK(parsed->milliseconds == 5000);
    parsed = Parse("write 0x5008 01 --undo 500ms --owner $1 --object 0x5000 --expect 00", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->object == 0x5000);
    CHECK(parsed->milliseconds == 500);
    CHECK_FALSE(Parse("write $1 01 --undo 5 --expect 00", TwoRefs).has_value());          // No owner.
    CHECK_FALSE(Parse("write $1 01 --undo 5 --owner $1", TwoRefs).has_value());           // No --expect.
    CHECK_FALSE(Parse("write $1 01 --undo 5 --owner 0x1000 --expect 00", TwoRefs).has_value()); // Owner must be a ref.
    CHECK_FALSE(Parse("write $1 01 --undo 61 --owner $1 --expect 00", TwoRefs).has_value());
    CHECK_FALSE(Parse("write $1 01 --owner $1 --expect 00", TwoRefs).has_value());       // --owner goes with --undo.
    CHECK_FALSE(Parse("write $1 01 --undo 5 --hold --owner $1 --expect 00", TwoRefs).has_value());
    CHECK_FALSE(Parse("write $1 01 --hold --owner $1", TwoRefs).has_value());
    for (const char* gone : {"trial $1 01 --owner $1 --expect 00", "trial-list", "trial-clear", "pin 0x10 01", "pin-list", "pin-clear"})
        CHECK_FALSE(Parse(gone, TwoRefs).has_value());
}

TEST_CASE("writes lists the timed writes; clear takes one id or none") {
    auto parsed = Parse("writes", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Writes);
    CHECK_FALSE(parsed->all);
    parsed = Parse("writes clear", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->all);
    CHECK(parsed->request.args[0] == 0);
    parsed = Parse("writes clear undo:3", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->target == "undo");
    CHECK(parsed->request.args[0] == 3);
    CHECK(Parse("writes clear hold:2", NoRefs)->target == "hold");
    CHECK_FALSE(Parse("writes clear 3", NoRefs).has_value());
    CHECK_FALSE(Parse("writes clear pin:3", NoRefs).has_value());
    CHECK_FALSE(Parse("writes list", NoRefs).has_value());
    CHECK_FALSE(Parse("writes undo:3", NoRefs).has_value());
}

TEST_CASE("release is a call with the release flag and an optional block") {
    auto parsed = Parse("release $1 Comp:FindAttribute", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Release);
    CHECK(parsed->request.op == ue::op::Call);
    CHECK(parsed->request.flags == ue::CallRelease);
    CHECK(parsed->request.length == 0); // The console sizes a zeroed block.
    parsed = Parse("release $2 Comp:FindAttribute 00 00", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.length == 2);
    CHECK_FALSE(Parse("release $1", TwoRefs).has_value());
}

TEST_CASE("call-pure is a call with the pure flag; --dry marks either form") {
    auto parsed = Parse("call-pure $1 Actor:K2_GetActorLocation", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.op == ue::op::Call);
    CHECK(parsed->request.flags == ue::CallPure);
    CHECK_FALSE(parsed->dry);
    CHECK(ue::Check(parsed->request) == Status::Ok);
    parsed = Parse("call-pure $1 SceneComponent:GetSocketTransform SocketName=#42 TransformSpace=0", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::TypedCall);
    CHECK(parsed->request.flags == ue::CallPure);
    parsed = Parse("call $1 Actor:K2_GetActorLocation --dry", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->dry);
    CHECK(parsed->request.flags == 0);
    CHECK(Parse("call $1 Controller:GetControlRotation --typed --dry", TwoRefs)->dry);
}

TEST_CASE("a call's object can be a default object by name, never a class") {
    auto parsed = Parse("call-pure Default__KismetSystemLibrary IsValid Object=None", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::TypedCall);
    CHECK(parsed->className == "Default__KismetSystemLibrary");
    parsed = Parse("call Default__GameplayStatics GetPlayerPawn 00", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->className == "Default__GameplayStatics");
    CHECK_FALSE(Parse("call KismetSystemLibrary IsValid 00", TwoRefs).has_value());
}

TEST_CASE("call keeps the full ref from find and marks raw addresses for identification") {
    auto parsed = Parse("call $1 Actor:K2_GetActorLocation 00", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.op == ue::op::Call);
    CHECK(ue::GetRef(parsed->request) == Ref{0x1000, 5, 9});
    CHECK(std::string_view(parsed->request.text) == "Actor:K2_GetActorLocation");
    CHECK(parsed->request.length == 1);
    parsed = Parse("call 0x3000 GetName", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(ue::GetRef(parsed->request).address == 0x3000);
    CHECK(ue::GetRef(parsed->request).index == -1);
    CHECK(parsed->request.length == 0);
    CHECK(ue::Check(parsed->request) == Status::Ok);
}

TEST_CASE("modules asks the agent; base and region are no verbs") {
    CHECK_FALSE(Parse("base", NoRefs).has_value());
    CHECK_FALSE(Parse("region 0x1000", NoRefs).has_value());
    auto parsed = Parse("modules", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.op == op::Modules);
    CHECK(Check(parsed->request) == Status::Ok);
}

TEST_CASE("search parses typed values, options and limits") {
    auto parsed = Parse("search f32 100", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.op == op::Search);
    CHECK(parsed->request.length == 4);
    float value = 0;
    memcpy(&value, parsed->request.data, 4);
    CHECK(value == 100.0f);
    CHECK(SearchAlignment(parsed->request.flags) == 4);
    CHECK(parsed->request.args[1] == 64);
    CHECK(Check(parsed->request) == Status::Ok);

    parsed = Parse("search ptr 0x7ff7bb2a7740 --writable --max 8 --start 0x10000 --to 0x20000", NoRefs);
    REQUIRE(parsed.has_value());
    uint64_t pointer = 0;
    memcpy(&pointer, parsed->request.data, 8);
    CHECK(pointer == 0x7ff7bb2a7740);
    CHECK(SearchAlignment(parsed->request.flags) == 8);
    CHECK((parsed->request.flags & SearchWritable) != 0);
    CHECK(parsed->request.args[1] == 8);
    CHECK(parsed->request.address == 0x10000);
    CHECK(parsed->request.args[0] == 0x20000);

    parsed = Parse("search hex de ad be ef --align 2", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.length == 4);
    CHECK(SearchAlignment(parsed->request.flags) == 2);
    parsed = Parse("search i32 -5", NoRefs);
    REQUIRE(parsed.has_value());
    int32_t negative = 0;
    memcpy(&negative, parsed->request.data, 4);
    CHECK(negative == -5);

    CHECK_FALSE(Parse("search", NoRefs).has_value());
    CHECK_FALSE(Parse("search f32 abc", NoRefs).has_value());
    CHECK_FALSE(Parse("search u32 1 --align 3", NoRefs).has_value());
    CHECK_FALSE(Parse("search u32 1 --max 0", NoRefs).has_value());
    CHECK_FALSE(Parse("search u32 1 --start 0x2000 --to 0x1000", NoRefs).has_value());
    CHECK_FALSE(Parse("search str abc", NoRefs).has_value());
}

TEST_CASE("search text takes a quoted string exactly") {
    const auto text = [](const Command& command) {
        return std::string(reinterpret_cast<const char*>(command.request.data), command.request.length);
    };
    auto parsed = Parse("search text \"Fatal Python error\" --module python313.dll --max 1", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(text(*parsed) == "Fatal Python error");
    CHECK(parsed->target == "python313.dll");
    CHECK(parsed->request.args[1] == 1);

    parsed = Parse("search text \"two  spaces; and -- dashes\"", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(text(*parsed) == "two  spaces; and -- dashes");
    parsed = Parse(R"(search text "say \"hi\" from C:\Game\x \\ end")", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(text(*parsed) == R"(say "hi" from C:\Game\x \ end)");

    CHECK(parsed->textSearch == 1);   // Rows then carry the text around each hit.
    CHECK(Parse("search hex 41 42", NoRefs)->textSearch == 0);
    parsed = Parse("search text \"ab c\" --wide", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->textSearch == 2);
    CHECK(parsed->request.length == 8);
    CHECK(parsed->request.data[4] == ' ');
    CHECK(parsed->request.data[5] == 0);

    parsed = Parse("search text Fatal Python error", NoRefs); // Unquoted words still join.
    REQUIRE(parsed.has_value());
    CHECK(text(*parsed) == "Fatal Python error");

    CHECK_FALSE(Parse("search text \"never closed", NoRefs).has_value());
    CHECK_FALSE(Parse("search text \"\"", NoRefs).has_value());
}

TEST_CASE("search filters and safe write parse") {
    auto parsed = Parse("search ptr 0x1234 --heap --paged --background --module Game.exe", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK((parsed->request.flags & SearchBackground) != 0);
    CHECK((parsed->request.flags & SearchPrivate) != 0);
    CHECK((parsed->request.flags & SearchPaged) != 0);
    CHECK(parsed->target == "Game.exe");
    CHECK(Check(parsed->request) == Status::Ok);
    CHECK_FALSE(Parse("search ptr 0x1234 --module Game.exe --start 0x1000", NoRefs).has_value());
    CHECK_FALSE(Parse("search ptr 0x1234 --module", NoRefs).has_value());

    parsed = Parse("write 0x1000 01 02 --expect 03 04", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.flags == WriteExpect);
    CHECK(parsed->request.length == 2);
    CHECK(parsed->request.data[0] == 1);
    CHECK(parsed->request.data[2] == 3);
    CHECK(parsed->request.data[3] == 4);
    CHECK(Check(parsed->request) == Status::Ok);
    CHECK_FALSE(Parse("write 0x1000 01 02 --expect 03", NoRefs).has_value());
    parsed = Parse("write 0x1000 01 02", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.flags == 0);
}

TEST_CASE("write --hold and watch parse targets, guards and durations") {
    const Ref refs[] = {{0x7000, 12, 3}};
    auto parsed = Parse("write $1+0x3e4 00 00 80 3f --hold 5s --expect 00 00 00 00", refs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Pin);
    CHECK(parsed->request.flags == static_cast<uint32_t>(PinAction::Add));
    CHECK(parsed->request.address == 0x7000 + 0x3e4);
    CHECK(parsed->request.length == 4);
    CHECK(parsed->milliseconds == 5000);
    CHECK(parsed->request.args[1] == 1);
    REQUIRE(parsed->guardObjects.size() == 1);
    CHECK(parsed->guardObjects[0] == 0x7000);
    parsed = Parse("write 0x5000 01 --hold", refs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->guardObjects[0] == 0);
    CHECK(parsed->milliseconds == 10000);
    CHECK_FALSE(Parse("write 0x5000 01 --hold 61", refs).has_value());
    CHECK_FALSE(Parse("write 0x5000 01 02 --hold --expect 01", refs).has_value());

    parsed = Parse("watch 2 $1+0x3e4:4 0x7000#12:3+0x258:8 0x9000:16 --then call $1 Owner:SetAiming 01", refs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Watch);
    CHECK(parsed->milliseconds == 2000);
    REQUIRE(parsed->reads.size() == 3);
    CHECK(parsed->reads[1].address == 0x7000 + 0x258);
    CHECK(parsed->reads[1].length == 8);
    CHECK(parsed->guardObjects == std::vector<uint64_t>{0x7000, 0x7000, 0});
    CHECK(parsed->then == "call $1 Owner:SetAiming 01");
    parsed = Parse("watch 250ms 0x9000:4", refs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->milliseconds == 250);
    CHECK_FALSE(Parse("watch 2", refs).has_value());
    CHECK_FALSE(Parse("watch 31 0x9000:4", refs).has_value());
    CHECK_FALSE(Parse("watch 2 0x9000:513", refs).has_value());
    CHECK_FALSE(Parse("watch 2 0x9000:4 --then read 0x1", refs).has_value());
}

TEST_CASE("get --types takes one object as a ref or raw address, or a class") {
    auto parsed = Parse("get $2 --types", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Remote);
    CHECK(parsed->request.op == ue::op::Fields);
    CHECK(ue::GetRef(parsed->request) == Ref{0x2000, 7, 0});
    CHECK(parsed->request.length == 0); // First page.
    CHECK(ue::Check(parsed->request) == Status::Ok);
    parsed = Parse("get 0x4000 --types", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(ue::GetRef(parsed->request).index == -1);
    CHECK(Parse("get Character --types --own --match aim", NoRefs)->className == "Character");
    CHECK_FALSE(Parse("get --types", NoRefs).has_value());
    CHECK_FALSE(Parse("get $1 Health --types", TwoRefs).has_value());   // --types takes no paths.
    for (const char* gone : {"fields $1", "dump $1", "snap $1"}) CHECK_FALSE(Parse(gone, TwoRefs).has_value());
}

TEST_CASE("local commands do not build a request") {
    CHECK(Parse("help", NoRefs)->kind == Kind::Help);
    CHECK(Parse("help --hash", NoRefs)->hashOnly);
    CHECK_FALSE(Parse("help --hash --json", NoRefs).has_value());
    CHECK_FALSE(Parse("help read --hash", NoRefs).has_value());
    CHECK(Parse("status", NoRefs)->kind == Kind::Status);
    CHECK(Parse("quit", NoRefs)->kind == Kind::Quit);
    CHECK(Parse("stats", NoRefs)->kind == Kind::Stats);
    CHECK_FALSE(Parse("stats", NoRefs)->all);
    CHECK(Parse("stats --all", NoRefs)->all);
    CHECK(Parse("stats --sessions", NoRefs)->listSessions);
    CHECK(Parse("stats --session mcp-1", NoRefs)->target == "mcp-1");
    CHECK_FALSE(Parse("stats --all --sessions", NoRefs).has_value());
    CHECK_FALSE(Parse("stats --sessions --session mcp-1", NoRefs).has_value());
    CHECK_FALSE(Parse("stats --session", NoRefs).has_value());
    CHECK_FALSE(Parse("stats extra", NoRefs).has_value());
    CHECK_FALSE(Parse("batch { stats }", NoRefs).has_value());
    CHECK_FALSE(Parse("frobnicate", NoRefs).has_value());
}

TEST_CASE("get with no paths reads every field") {
    const auto parsed = Parse("get $1", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Dump);
    CHECK(parsed->request.op == ue::op::Fields);
    CHECK(ue::GetRef(parsed->request) == Ref{0x1000, 5, 9});
    CHECK_FALSE(Parse("get", NoRefs).has_value());
    // A class by name has no values: --types, or its Default__ object.
    CHECK(Parse("get Character", NoRefs).error().contains("Default__Character"));
    CHECK(Parse("get Default__Character", NoRefs)->kind == Kind::Dump);
}

TEST_CASE("capture commands preserve the ref and bound the recording") {
    const auto record = Parse("record $1 4 burst.tsv Ammo Weapon:State", TwoRefs);
    REQUIRE(record.has_value());
    CHECK(record->kind == Kind::Record);
    CHECK(ue::GetRef(record->request) == Ref{0x1000, 5, 9});
    CHECK(record->request.length == 4);
    CHECK(record->file == "burst.tsv");
    CHECK(record->fields == std::vector<std::string>{"Ammo", "Weapon:State"});
    CHECK_FALSE(Parse("get 0x1000 --snap", TwoRefs).has_value());     // A baseline needs a ref.
    CHECK_FALSE(Parse("get $1 a.snap extra --snap", TwoRefs).has_value());
    CHECK_FALSE(Parse("record $1 11 burst.tsv Ammo", TwoRefs).has_value());
    CHECK_FALSE(Parse("record $1 4 burst.tsv", TwoRefs).has_value());
}

TEST_CASE("describe takes an address or preserves a find ref") {
    const auto raw = Parse("describe 0x1234", TwoRefs);
    const auto known = Parse("describe $1", TwoRefs);
    REQUIRE(raw.has_value());
    REQUIRE(known.has_value());
    CHECK(raw->request.op == ue::op::Describe);
    CHECK(ue::GetRef(raw->request) == Ref{0x1234, -1, 0});
    CHECK(ue::GetRef(known->request) == TwoRefs[0]);
    CHECK_FALSE(Parse("describe $1 extra", TwoRefs).has_value());
    const auto slot = Parse("describe #331227:138023", NoRefs);
    REQUIRE(slot.has_value());
    CHECK(ue::GetRef(slot->request) == Ref{0, 331227, 138023});
    CHECK(ue::Check(slot->request) == Status::Ok);
    CHECK_FALSE(Parse("describe #5:0", NoRefs).has_value());
    Request weak{};
    weak.op = ue::op::Describe;
    ue::SetRef(weak, Ref{0, 5, 9});
    CHECK(ue::Check(weak) == Status::Ok);
    ue::SetRef(weak, Ref{0, 5, 0});
    CHECK(ue::Check(weak) == Status::BadRequest);
}

TEST_CASE("func resolves a named entry without a call payload") {
    const auto parsed = Parse("func $1 PrimitiveComponent:GetMass", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.op == ue::op::Function);
    CHECK(ue::GetRef(parsed->request) == TwoRefs[0]);
    CHECK(std::string_view(parsed->request.text) == "PrimitiveComponent:GetMass");
    CHECK(ue::Check(parsed->request) == Status::Ok);
    CHECK(Parse("func 0x1234 CalculateWeaponTrace", NoRefs).has_value());
    CHECK_FALSE(Parse("func $1 :CalculateWeaponTrace", TwoRefs).has_value());
    CHECK_FALSE(Parse("func $1 PrimitiveComponent:", TwoRefs).has_value());
    CHECK_FALSE(Parse("func $1 CalculateWeaponTrace 00", TwoRefs).has_value());
    auto request = parsed->request;
    request.length = 1;
    CHECK(ue::Check(request) == Status::BadRequest);
}

TEST_CASE("function discovery, signatures and named calls preserve object refs") {
    auto functions = Parse("funcs $1 Socket --start 32", TwoRefs);
    REQUIRE(functions.has_value());
    CHECK(functions->request.op == ue::op::Functions);
    CHECK(ue::GetRef(functions->request) == TwoRefs[0]);
    CHECK(std::string_view(functions->request.text) == "Socket");
    CHECK(functions->request.length == 32);
    CHECK(ue::Check(functions->request) == Status::Ok);
    CHECK(Parse("funcs $1 --start 32", TwoRefs).has_value());
    CHECK_FALSE(Parse("funcs $1 --start", TwoRefs).has_value());
    CHECK_FALSE(Parse("funcs $1 Socket --start 99999", TwoRefs).has_value());

    // func reads the signature too: params is no verb.
    auto signature = Parse("func $1 SceneComponent:GetSocketTransform", TwoRefs);
    REQUIRE(signature.has_value());
    CHECK(signature->request.op == ue::op::Function);
    CHECK(ue::GetRef(signature->request) == TwoRefs[0]);
    CHECK_FALSE(Parse("params $1 SceneComponent:GetSocketTransform", TwoRefs).has_value());
    auto call = Parse("call $1 SceneComponent:GetSocketTransform SocketName=#42 TransformSpace=0", TwoRefs);
    REQUIRE(call.has_value());
    CHECK(call->kind == Kind::TypedCall);
    CHECK(call->request.op == ue::op::Call);
    CHECK(call->fields == std::vector<std::string>{"SocketName=#42", "TransformSpace=0"});
    CHECK(Parse("call $1 Controller:GetControlRotation --typed", TwoRefs)->kind == Kind::TypedCall);
    CHECK(Parse("call $1 Controller:GetControlRotation --typed", TwoRefs)->limit == DefaultCallRows);
}

TEST_CASE("call --fields, --start and --limit page a typed call's returned arrays") {
    auto rows = Parse("call $1 Vendor:GetEntries --fields ItemTag,Variations.Price --start 100 --limit 0", TwoRefs);
    REQUIRE(rows.has_value());
    CHECK(rows->kind == Kind::TypedCall); // Implied: no --typed needed.
    CHECK(rows->rowPaths == std::vector<std::string>{"ItemTag", "Variations.Price"});
    CHECK(rows->from == 100);
    CHECK(rows->limit == 0);
    CHECK(Parse("call $1 Vendor:GetEntries Count=2 --limit 5", TwoRefs)->fields == std::vector<std::string>{"Count=2"});
    CHECK_FALSE(Parse("call $1 Vendor:GetEntries 00 --limit 5", TwoRefs).has_value()); // A raw block has no rows.
    CHECK_FALSE(Parse("call $1 Vendor:GetEntries --fields a..b", TwoRefs).has_value());
    CHECK_FALSE(Parse("call $1 Vendor:GetEntries --fields a,", TwoRefs).has_value());
    CHECK_FALSE(Parse("call-pure $1 Vendor:GetEntries --fields a", TwoRefs).has_value()); // Arrays own memory: call only.
}

TEST_CASE("--nonzero: get, each, rows and a typed call") {
    const auto call = Parse("call $1 Vendor:GetEntries --nonzero", TwoRefs);
    REQUIRE(call.has_value());
    CHECK(call->kind == Kind::TypedCall); // Implied, as --fields is.
    CHECK(call->nonzero);
    CHECK_FALSE(Parse("call $1 Vendor:GetEntries 00 --nonzero", TwoRefs).has_value());
    CHECK(Parse("get $1 Health Stats --nonzero", TwoRefs)->nonzero);
    // A default object by name, as call takes it; the console finds it before reading.
    CHECK(Parse("get Default__PhysicsSettings PhysicalSurfaces", TwoRefs)->className == "Default__PhysicsSettings");
    CHECK(Parse("rows Default__Table --limit 5", TwoRefs)->className == "Default__Table");
    CHECK(Parse("entries Default__Settings Map", TwoRefs)->className == "Default__Settings");
    CHECK_FALSE(Parse("get PhysicsSettings PhysicalSurfaces", TwoRefs));   // A class is not an object.
    CHECK(Parse("get PhysicsSettings PhysicalSurfaces", TwoRefs).error().contains("--types"));
    CHECK_FALSE(Parse("get $1 Health", TwoRefs)->nonzero);
    CHECK(Parse("each Pawn Health --nonzero", TwoRefs)->nonzero);
    CHECK(Parse("rows $1 --nonzero --limit 0", TwoRefs)->nonzero);
}

TEST_CASE("literal refs round-trip and work wherever $N does") {
    const Ref ref{0x1b3eec26700, 185680, 9};
    CHECK(RefText(ref) == "0x1b3eec26700#185680:9");
    Ref parsed{};
    CHECK(text::LiteralRef(RefText(ref), parsed));
    CHECK(parsed == ref);
    const auto all = Parse("get 0x1b3eec26700#185680:9", NoRefs);
    REQUIRE(all.has_value());
    CHECK(ue::GetRef(all->request) == ref);
    const auto read = Parse("read 0x1b3eec26700#185680:9+0x330 24", NoRefs);
    REQUIRE(read.has_value());
    CHECK(read->request.address == 0x1b3eec26700 + 0x330);
    const auto snap = Parse("get 0x1b3eec26700#185680:9 --snap", NoRefs);
    REQUIRE(snap.has_value());
    CHECK(ue::GetRef(snap->request) == ref);
    CHECK_FALSE(text::LiteralRef("0x1000#-1:0", parsed));
    CHECK_FALSE(text::LiteralRef("0#5:9", parsed));
    CHECK_FALSE(text::LiteralRef("0x1000:5#9", parsed));
    CHECK_FALSE(text::LiteralRef("0x1000#5", parsed));
}

TEST_CASE("get --snap takes an optional file; get --changed reads it") {
    const auto snap = Parse("get $1 --snap ctrl.snap", TwoRefs);
    REQUIRE(snap.has_value());
    CHECK(snap->kind == Kind::Snap);
    CHECK(snap->file == "ctrl.snap");
    const auto bare = Parse("get $1 --snap", TwoRefs);
    REQUIRE(bare.has_value());
    CHECK(bare->file.empty());
    const auto changed = Parse("get $1 --changed ctrl.snap", TwoRefs);
    REQUIRE(changed.has_value());
    CHECK(changed->kind == Kind::Dump);
    CHECK(changed->changed);
    CHECK(changed->file == "ctrl.snap");
    CHECK_FALSE(Parse("get $1 --snap --changed", TwoRefs).has_value());
    CHECK_FALSE(Parse("get $1 --snap --nonzero", TwoRefs).has_value());
    CHECK_FALSE(Parse("get $1 Health --changed", TwoRefs).has_value());
    CHECK_FALSE(Parse("diff $1", TwoRefs).has_value());
}

TEST_CASE("one-shot commands split on semicolons and newlines") {
    CHECK(SplitCommands("find PC_; get $1") == std::vector<std::string>{"find PC_", "get $1"});
    CHECK(SplitCommands(" status \n\n;; quit ") == std::vector<std::string>{"status", "quit"});
    CHECK(SplitCommands("").empty());
    CHECK(SplitCommands("find X; batch { read $1 4; get $1 Health }; status") ==
          std::vector<std::string>{"find X", "batch { read $1 4; get $1 Health }", "status"});
    CHECK(SplitCommands("batch {\nread 0x10 4\nread 0x20 4\n}") ==
          std::vector<std::string>{"batch {;read 0x10 4;read 0x20 4;}"});
    CHECK(SplitCommands(R"(search text "a; {b} \"c;\""; status)") ==
          std::vector<std::string>{R"(search text "a; {b} \"c;\"")", "status"});
}

TEST_CASE("read decodes element types and name takes long index lists") {
    auto parsed = Parse("read 0x1000 16 --type f32", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->view == "f32");
    CHECK(parsed->request.length == 16);
    parsed = Parse("read 0x1000 --type ptr", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.length == 64);
    CHECK_FALSE(Parse("read 0x1000 6 --type f32", NoRefs).has_value());
    CHECK_FALSE(Parse("read 0x1000 8 --type f16", NoRefs).has_value());
    CHECK_FALSE(Parse("read 0x1000 8 --bytes", NoRefs).has_value());
    std::string many = "name";
    for (int i = 0; i < 100; ++i) many += " " + std::to_string(i + 1);
    parsed = Parse(many, NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.length == 400);
}

TEST_CASE("name takes FName indices and the agent bounds them") {
    const auto parsed = Parse("name 2379 0x10 0", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.op == ue::op::Names);
    CHECK(parsed->request.length == 12);
    int32_t second = 0;
    memcpy(&second, parsed->request.data + 4, 4);
    CHECK(second == 16);
    CHECK(ue::Check(parsed->request) == Status::Ok);
    CHECK_FALSE(Parse("name", NoRefs).has_value());
    CHECK_FALSE(Parse("name x", NoRefs).has_value());
    CHECK_FALSE(Parse("name 0x80000000", NoRefs).has_value());
    Request request{};
    request.op = ue::op::Names;
    request.length = 6;
    CHECK(ue::Check(request) == Status::BadRequest);
    request.length = (ue::MaxNames + 1) * 4;
    CHECK(ue::Check(request) == Status::BadRequest);
}

TEST_CASE("--outer takes an object or an outer's name, on find, each and census") {
    const auto byRef = Parse("find --cls BodySetup --outer 0x10#4:2", NoRefs);
    REQUIRE(byRef.has_value());
    CHECK(byRef->outerRef == Ref{0x10, 4, 2});                            // The whole ref, serial included.
    CHECK(byRef->outerText.empty());
    const auto byN = Parse("each BodySetup BoneName --outer $1", TwoRefs);
    REQUIRE(byN.has_value());
    CHECK(byN->outerRef == TwoRefs[0]);
    const auto byName = Parse("census --cls BodySetup --outer PA_Pawn", NoRefs);
    REQUIRE(byName.has_value());
    CHECK(byName->outerText == "PA_Pawn");
    CHECK(byName->outerRef.address == 0);
    CHECK(Parse("find --outer PA_Pawn", NoRefs).has_value());             // An outer alone is a filter.
    CHECK_FALSE(Parse("find --outer " + std::string(64, 'x'), NoRefs).has_value());
    // --elements: 1..256 on get (paths or every field) and each.
    CHECK(Parse("get $1 Map --elements 16", TwoRefs)->elements == 16);
    CHECK(Parse("each Pawn Map --elements 256", NoRefs)->elements == 256);
    CHECK(Parse("get $1 --elements 3", TwoRefs)->elements == 3);
    CHECK_FALSE(Parse("get $1 Map --elements 0", TwoRefs).has_value());
    CHECK_FALSE(Parse("each Pawn Map --elements 257", NoRefs).has_value());
}

TEST_CASE("find takes a class filter and a cursor") {
    const auto both = Parse("find Vendor --cls Character --start 4096", NoRefs);
    REQUIRE(both.has_value());
    CHECK(std::string_view(both->request.text) == "Vendor");
    CHECK(std::string_view(reinterpret_cast<const char*>(both->request.data)) == "Character");
    CHECK(both->request.length == 10);
    CHECK(both->request.args[1] == 4096);
    CHECK(ue::Check(both->request) == Status::Ok);
    CHECK(both->limit == 0);
    CHECK(Parse("find Vendor --limit 50", NoRefs)->limit == 50);
    CHECK_FALSE(Parse("find Vendor --limit 0", NoRefs).has_value());
    CHECK_FALSE(Parse("find Vendor --limit 1001", NoRefs).has_value());
    const auto classOnly = Parse("find --cls DataTable", NoRefs);
    REQUIRE(classOnly.has_value());
    CHECK(classOnly->request.text[0] == 0);
    CHECK(ue::Check(classOnly->request) == Status::Ok);
    CHECK(classOnly->request.flags == 0);
    const auto live = Parse("find --cls DataTable --live", NoRefs);
    REQUIRE(live.has_value());
    CHECK(live->request.flags == ue::FindLive);
    CHECK(ue::Check(live->request) == Status::Ok);
    const auto spaced = Parse("find two words --start 1", NoRefs);
    REQUIRE(spaced.has_value());
    CHECK(std::string_view(spaced->request.text) == "two words");
    CHECK_FALSE(Parse("find", NoRefs).has_value());
    CHECK_FALSE(Parse("find --cls", NoRefs).has_value());
    CHECK_FALSE(Parse("find x --start -1", NoRefs).has_value());
    CHECK_FALSE(Parse("find --cls A --cls B", NoRefs).has_value());
}

TEST_CASE("read-batch is no verb: batch and the session cover it") {
    CHECK_FALSE(Parse("read-batch 0x1000 8", NoRefs).has_value());
}

TEST_CASE("object commands take a bare class name for the console to resolve") {
    for (const char* line : {"get Character --types", "get BP_Door_C --types", "funcs Character Jump",
                             "func Character CanJump"}) {
        const auto parsed = Parse(line, NoRefs);
        REQUIRE(parsed.has_value());
        CHECK_FALSE(parsed->className.empty());
        CHECK(parsed->request.address == 0); // Filled once the console resolves the class.
    }
    const auto func = Parse("func Character Character:CanJump", NoRefs);
    REQUIRE(func.has_value());
    CHECK(func->className == "Character");
    CHECK(func->request.op == ue::op::Function);
    CHECK(Parse("get $1 --types", TwoRefs)->className.empty());
    CHECK_FALSE(Parse("func Character CanJump extra", NoRefs).has_value());
    CHECK_FALSE(Parse("get 12abc --types", NoRefs).has_value());
    CHECK_FALSE(Parse("get Some-Thing --types", NoRefs).has_value());
    CHECK_FALSE(Parse("func Character :Bad", NoRefs).has_value());
}

TEST_CASE("census takes an optional class and row limit") {
    auto parsed = Parse("census", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.op == ue::op::Census);
    CHECK(parsed->limit == DefaultCensusLimit);
    CHECK(ue::Check(parsed->request) == Status::Ok);
    parsed = Parse("census --cls Actor --limit 0", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(std::string_view(reinterpret_cast<const char*>(parsed->request.data)) == "Actor");
    CHECK(parsed->limit == 0);
    CHECK(ue::Check(parsed->request) == Status::Ok);
    CHECK_FALSE(Parse("census Actor", NoRefs).has_value());
    CHECK_FALSE(Parse("census --cls", NoRefs).has_value());
    CHECK_FALSE(Parse("census --cls A --cls B", NoRefs).has_value());
    CHECK_FALSE(Parse("census", NoRefs)->changed);
    parsed = Parse("census --diff --cls Actor --match proj --limit 5", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->changed);
    CHECK(parsed->match == "proj");
    CHECK(parsed->limit == 5);
    CHECK_FALSE(Parse("census --match", NoRefs).has_value());
    CHECK_FALSE(Parse("census --diff --match", NoRefs).has_value());
    CHECK_FALSE(Parse("census-diff", NoRefs).has_value());
    CHECK_FALSE(Parse("census --match a --match b", NoRefs).has_value());
    parsed = Parse("census --cls Actor --match zone --start 100 --limit 50", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->from == 100);
    CHECK(parsed->limit == 50);
    CHECK_FALSE(Parse("census --start", NoRefs).has_value());
    CHECK_FALSE(Parse("census --start 1 --start 2", NoRefs).has_value());
    CHECK_FALSE(Parse("census --diff --changed", NoRefs).has_value());
    Request request = parsed->request;
    CopyText(request.text, "x");
    CHECK(ue::Check(request) == Status::BadRequest); // Census takes no name text.
}

TEST_CASE("name-find takes text and a cursor") {
    const auto parsed = Parse("name-find Jump --start 131072", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.op == ue::op::NameFind);
    CHECK(std::string_view(parsed->request.text) == "Jump");
    CHECK(parsed->request.args[1] == 131072);
    CHECK(ue::Check(parsed->request) == Status::Ok);
    CHECK_FALSE(Parse("name-find", NoRefs).has_value());
    CHECK_FALSE(Parse("name-find --start 3", NoRefs).has_value());
    CHECK_FALSE(Parse("name-find x --start", NoRefs).has_value());
    Request empty{};
    empty.op = ue::op::NameFind;
    CHECK(ue::Check(empty) == Status::BadRequest);
}

TEST_CASE("each takes a class, its columns and an optional repeat") {
    auto parsed = Parse("each Character Character:CanJump --seconds 10", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Each);
    CHECK(parsed->request.op == ue::op::Each);
    CHECK(parsed->className == "Character");
    CHECK(parsed->fields == std::vector<std::string>{"Character:CanJump"});
    CHECK(parsed->seconds == 10);
    parsed = Parse("each Pawn Health Stats.MaxSpeed Controller.PlayerState.Score", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->fields == std::vector<std::string>{"Health", "Stats.MaxSpeed", "Controller.PlayerState.Score"});
    CHECK_FALSE(Parse("each Pawn Health..Max", NoRefs).has_value());
    std::string many = "each Pawn";
    for (int i = 0; i <= 16; ++i) many += " F" + std::to_string(i);
    CHECK(Parse(many, NoRefs).error().starts_with("at most 16 columns"));
    parsed = Parse("each Pawn Controller", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->seconds == 0);
    CHECK_FALSE(Parse("each Pawn", NoRefs).has_value());
    CHECK_FALSE(Parse("each $1 Health", TwoRefs).has_value());
    CHECK_FALSE(Parse("each Pawn Health --seconds 0", NoRefs).has_value());
    CHECK_FALSE(Parse("each Pawn Health --seconds 61", NoRefs).has_value());
}

TEST_CASE("batch keeps its command lines and refuses what cannot be held") {
    auto parsed = Parse("batch { find Pawn ; read $1 8;; get $1 Health }", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Batch);
    CHECK(parsed->fields == std::vector<std::string>{"find Pawn", "read $1 8", "get $1 Health"});
    parsed = Parse(R"(batch { search text "x; {y}"; read 0x10 4 })", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->fields == std::vector<std::string>{R"(search text "x; {y}")", "read 0x10 4"});
    CHECK_FALSE(Parse("batch read 0x10 4", NoRefs).has_value());
    CHECK_FALSE(Parse("batch { }", NoRefs).has_value());
    CHECK_FALSE(Parse("batch { batch { read 0x10 4 } }", NoRefs).has_value());
    CHECK_FALSE(Parse("batch { watch 1 0x10:4 }", NoRefs).has_value());
    CHECK_FALSE(Parse("batch { status }", NoRefs).has_value());
    std::string many = "batch {";
    for (size_t i = 0; i <= MaxBatchCommands; ++i) many += " read 0x10 4;";
    CHECK_FALSE(Parse(many + " }", NoRefs).has_value());
}

TEST_CASE("trace takes two ends: points, objects or sockets, and its options") {
    auto parsed = Parse("trace $1@Head 100.5,-2,3e2 --channel camera --simple --ignore $2", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Trace);
    CHECK(parsed->traceEnds[0].object == TwoRefs[0]);
    CHECK(parsed->traceEnds[0].socket == "Head");
    CHECK_FALSE(parsed->traceEnds[0].literal);
    CHECK(parsed->traceEnds[1].literal);
    CHECK(parsed->traceEnds[1].point[0] == 100.5);
    CHECK(parsed->traceEnds[1].point[1] == -2);
    CHECK(parsed->traceEnds[1].point[2] == 300);
    CHECK(parsed->traceChannel == 1);
    CHECK(parsed->traceSimple);
    CHECK(parsed->traceIgnore == TwoRefs[1]);
    auto plain = Parse("trace (0,0,0) $2 --channel 5", TwoRefs);
    REQUIRE(plain.has_value());
    CHECK(plain->traceEnds[0].literal);
    CHECK(plain->traceEnds[1].object == TwoRefs[1]);
    CHECK(plain->traceChannel == 5);
    CHECK_FALSE(plain->traceSimple);
    CHECK_FALSE(Parse("trace $1", TwoRefs).has_value());
    CHECK_FALSE(Parse("trace 1,2 $1", TwoRefs).has_value());
    CHECK_FALSE(Parse("trace 1,2,3,4 $1", TwoRefs).has_value());
    CHECK_FALSE(Parse("trace 1,2,nan $1", TwoRefs).has_value());
    CHECK_FALSE(Parse("trace $1@ $2", TwoRefs).has_value());
    CHECK_FALSE(Parse("trace $1 $2 --channel 32", TwoRefs).has_value());
    CHECK_FALSE(Parse("trace $1 $2 --ignore 7", TwoRefs).has_value());
}

TEST_CASE("calls-start takes a target, a function and a duration; read and stop take a slot") {
    auto parsed = Parse("calls-start $1 Owner:Damage 10s --object $2 --dry", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Calls);
    CHECK(parsed->target == "start");
    CHECK(parsed->request.op == ue::op::CallsStart);
    CHECK(ue::GetRef(parsed->request) == TwoRefs[0]); // The duration must not overwrite the ref.
    CHECK(std::string(parsed->request.text) == "Owner:Damage");
    CHECK(parsed->request.args[1] == 10000);
    uint64_t filter = 0;
    REQUIRE(parsed->request.length == sizeof(filter));
    memcpy(&filter, parsed->request.data, sizeof(filter));
    CHECK(filter == TwoRefs[1].address);
    CHECK(parsed->request.flags == ue::CallsDry);
    auto byClass = Parse("calls-start SomeClass Fire 500ms", TwoRefs);
    REQUIRE(byClass.has_value());
    CHECK(byClass->className == "SomeClass");
    CHECK(byClass->request.args[1] == 500);
    CHECK_FALSE(Parse("calls-start $1 Fire 61s", TwoRefs).has_value());
    CHECK_FALSE(Parse("calls-start $1 Fire", TwoRefs).has_value());
    CHECK_FALSE(Parse("calls-start $1 Fire 5s --object 7", TwoRefs).has_value());
    auto read = Parse("calls-read --slot 3 --start 40", TwoRefs);
    REQUIRE(read.has_value());
    CHECK(read->target == "read");
    CHECK(read->callsSlot == 3);
    CHECK(read->callsAfter == 40);
    auto latest = Parse("calls-stop", TwoRefs);
    REQUIRE(latest.has_value());
    CHECK(latest->target == "stop");
    CHECK(latest->callsSlot == ue::CallsLatest);
    CHECK_FALSE(Parse("calls-read --slot 8", TwoRefs).has_value());
}

TEST_CASE("get takes an object and dotted field paths") {
    auto parsed = Parse("get $1 Mesh.AnimInstance.AimAlpha Owner:Health", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Get);
    CHECK(ue::GetRef(parsed->request) == TwoRefs[0]);
    CHECK(parsed->fields == std::vector<std::string>{"Mesh.AnimInstance.AimAlpha", "Owner:Health"});
    CHECK(Parse("get $1", TwoRefs)->kind == Kind::Dump);            // No paths: every field.
    CHECK_FALSE(Parse("get $1 A..B", TwoRefs).has_value());
    CHECK_FALSE(Parse("get $1 .A", TwoRefs).has_value());
    CHECK_FALSE(Parse("get $1 A.B.C.D.E.F.G.H.I", TwoRefs).has_value());
    CHECK_FALSE(Parse("get $3 A", TwoRefs).has_value());
}

TEST_CASE("get filters parse; --types reads no values") {
    auto parsed = Parse("get $1 --match aim --nonzero --changed before.snap", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Dump);
    CHECK(parsed->match == "aim");
    CHECK(parsed->nonzero);
    CHECK(parsed->changed);
    CHECK(parsed->file == "before.snap");
    parsed = Parse("get $1 --changed --nonzero", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->changed);
    CHECK(parsed->file.empty());
    CHECK_FALSE(Parse("get $1 --match", TwoRefs).has_value());
    CHECK_FALSE(Parse("get $1 --bogus", TwoRefs).has_value());
    CHECK_FALSE(Parse("get $1 --types --nonzero", TwoRefs).has_value());
    CHECK_FALSE(Parse("get $1 Health --match aim", TwoRefs).has_value());
}

TEST_CASE("read --type takes a layout whose size is the read length") {
    auto parsed = Parse("read 0x1000 --type \"u8 phase, pad 3, f32 duration, f32 elapsed, f64[3] start\"", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.length == 36);
    CHECK(parsed->view == "u8 phase, pad 3, f32 duration, f32 elapsed, f64[3] start");
    parsed = Parse("read 0x1000 8 --type u32 a, u32 b", NoRefs); // Quotes optional; the length agrees.
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.length == 8);
    CHECK_FALSE(Parse("read 0x1000 16 --type \"u32 a, u32 b\"", NoRefs).has_value()); // Length mismatch.
    CHECK_FALSE(Parse("read 0x1000 --type \"u32 a, f16 b\"", NoRefs).has_value());     // Bad type.
    CHECK_FALSE(Parse("read 0x1000 --type \"u32 a,\"", NoRefs).has_value());
}

TEST_CASE("label names an unreflected member by object or class, offset, type and name") {
    auto parsed = Parse("label 0x1000#5:2 0x538 f64[3] Start", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Label);
    CHECK(parsed->offset == 0x538);
    CHECK(parsed->target == "f64[3]");
    CHECK(parsed->match == "Start");
    parsed = Parse("label TriggerVolume 0x53c f32 Duration?", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->className == "TriggerVolume");
    CHECK_FALSE(Parse("label TriggerVolume 0x53c f32", NoRefs).has_value());
    CHECK_FALSE(Parse("label TriggerVolume 0x53c f16 X", NoRefs).has_value());
    CHECK_FALSE(Parse("label TriggerVolume 0x53c pad 4", NoRefs).has_value());
    CHECK_FALSE(Parse("label TriggerVolume 0x10000 u8 X", NoRefs).has_value());
    CHECK_FALSE(Parse("label TriggerVolume 0x53c u8 X extra", NoRefs).has_value());
}

TEST_CASE("watch-start takes an interval and a root; --read and --stop take nothing") {
    const std::vector<Ref> refs{{0x1000, 5, 2}};
    auto parsed = Parse("watch-start 10m --every 10s $1+0x538:72 0x9000:8", refs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Watch);
    CHECK(parsed->target == "start");
    CHECK(parsed->milliseconds == 600000);
    CHECK(parsed->interval == 10000);
    REQUIRE(parsed->reads.size() == 2);
    CHECK(parsed->guardObjects[0] == 0x1000);
    parsed = Parse("watch-start 120s --every 500ms --follow pawn +0x538:16 0x3e4:4", refs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->root == "pawn");
    CHECK(parsed->reads[0].address == 0x538);
    CHECK(parsed->reads[1].address == 0x3e4);
    CHECK(parsed->guardObjects[1] == 0);
    parsed = Parse("watch-read", refs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->target == "read");
    CHECK(Parse("watch-stop", refs)->target == "stop");
    CHECK_FALSE(Parse("watch-read now", refs).has_value());
    CHECK_FALSE(Parse("watch-start 61m --every 1s 0x9000:4", refs).has_value());   // Past the capture limit.
    CHECK_FALSE(Parse("watch-start 31s 0x9000:4", refs).has_value());              // Every tick: 30 s at most.
    CHECK_FALSE(Parse("watch-start 10s --every 20s 0x9000:4", refs).has_value());  // Interval past the duration.
    CHECK_FALSE(Parse("watch-start 10s --follow pawn 0x10000:4", refs).has_value()); // Offset past an object.
    CHECK_FALSE(Parse("watch-start 10s 0x9000:4 --then call $1 F", refs).has_value());
    CHECK_FALSE(Parse("watch 10s --every 1s 0x9000:4", refs).has_value());           // Options need --start.
}

TEST_CASE("a watch range may name a reflected field; --all on watch, watch-read and watch-stop") {
    const std::vector<Ref> refs{{0x1000, 5, 2}};
    auto parsed = Parse("watch-start 30s $1.Health 0x1000#5:2.Root.RelativeLocation 0x9000:f64", refs);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->reads.size() == 3);
    REQUIRE(parsed->watchFields.size() == 2);
    CHECK(parsed->watchFields[0].range == 0);
    CHECK(parsed->watchFields[0].object == refs[0]);
    CHECK(parsed->watchFields[0].path == "Health");
    CHECK(parsed->watchFields[1].range == 1);
    CHECK(parsed->watchFields[1].path == "Root.RelativeLocation");
    CHECK(parsed->reads[2].address == 0x9000);
    // Following a root, ranges stay offsets; a field path is refused there.
    CHECK_FALSE(Parse("watch-start 10s --follow pawn $1.Health", refs).has_value());
    CHECK_FALSE(parsed->all);
    CHECK(Parse("watch-read --all", refs)->all);
    CHECK(Parse("watch-stop --all", refs)->all);
    CHECK(Parse("watch 2s 0x9000:4 --all", refs)->all);
}

TEST_CASE("a watch range may carry a read --type element, with a count") {
    const std::vector<Ref> refs{{0x1000, 5, 2}};
    auto parsed = Parse("watch-start 20s --every 1s 0x9000:f64 0x1000#5:2+0x40:i32[2] 0x9100:16", refs);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->reads.size() == 3);
    CHECK(parsed->reads[0].length == 8);
    CHECK(parsed->reads[1].address == 0x1040);   // A literal ref keeps its own colon.
    CHECK(parsed->reads[1].length == 8);
    CHECK(parsed->watchTypes == std::vector<uint32_t>{WatchTypeCode("f64"), WatchTypeCode("i32"), 0});
    CHECK(WatchTypeName(parsed->watchTypes[1]) == "i32");
    CHECK(Parse("watch-start 10s --follow pawn +0x538:f32", refs)->watchTypes.front() == WatchTypeCode("f32"));
    for (const char* bad : {"watch 1s 0x9000:f16", "watch 1s 0x9000:i32[0]", "watch 1s 0x9000:i32[", "watch 1s 0x9000:cstr"})
        CHECK(Parse(bad, refs).error().starts_with("a range is <addr>:<len>"));
}

TEST_CASE("the version text matches its parts") {
    const std::string expected = std::to_string(REAGENTLIVE_VERSION_MAJOR) + "." +
        std::to_string(REAGENTLIVE_VERSION_MINOR) + "." + std::to_string(REAGENTLIVE_VERSION_PATCH);
    CHECK(std::string(Version) == expected);
}

TEST_CASE("layout-export takes class names; layout-check a file") {
    auto parsed = Parse("layout-export out.jsonl Pawn PlayerController", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Layout);
    CHECK(parsed->target == "export");
    CHECK(parsed->fields == std::vector<std::string>{"Pawn", "PlayerController"});
    CHECK_FALSE(Parse("layout-export out.jsonl", NoRefs).has_value());
    CHECK_FALSE(Parse("layout-export out.jsonl Bad.Name", NoRefs).has_value());
    parsed = Parse("layout-check expected.jsonl", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->target == "check");
    CHECK(parsed->file == "expected.jsonl");
}

TEST_CASE("mem-snap takes an address and a length; mem-diff a test and an element type") {
    auto parsed = Parse("mem-snap 0x10000 4096", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::MemSnap);
    CHECK(parsed->range == 4096);
    CHECK_FALSE(Parse("mem-snap 0x10000 0", NoRefs).has_value());
    CHECK_FALSE(Parse("mem-snap 0x10000 2000000", NoRefs).has_value());
    parsed = Parse("mem-diff decreased f32 --keep --limit 10", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::MemDiff);
    CHECK(parsed->target == "decreased");
    CHECK(parsed->view == "f32");
    CHECK(parsed->all);
    CHECK(parsed->limit == 10);
    CHECK_FALSE(Parse("mem-diff bigger f32", NoRefs).has_value());
    CHECK_FALSE(Parse("mem-diff changed cstr", NoRefs).has_value());
    auto vtable = Parse("vtable 0x10000 --offset 0x620 --rtti", NoRefs);
    REQUIRE(vtable.has_value());
    CHECK(vtable->kind == Kind::Vtable);
    CHECK(vtable->rtti);
    CHECK(vtable->offset == 0x620);
    CHECK_FALSE(Parse("vtable 0x10000 --offset 8 --table --rtti", NoRefs).has_value());
    CHECK_FALSE(Parse("rtti 0x10000", NoRefs).has_value());
}

TEST_CASE("each --where filters on a field's value and --limit caps the rows") {
    auto each = Parse("each ItemDefinition ItemId --where DisplayName~rifle --limit 5", NoRefs);
    REQUIRE(each.has_value());
    CHECK(each->whereField == "DisplayName");
    CHECK(each->match == "rifle");
    CHECK(each->limit == 5);
    CHECK(Parse("each ItemDefinition ItemId", NoRefs)->limit == DefaultEachRows);
    CHECK(Parse("each ItemDefinition ItemId --where Tag.TagName~A~B", NoRefs)->match == "A~B");
    CHECK_FALSE(Parse("each ItemDefinition ItemId --where DisplayName", NoRefs).has_value());
    CHECK_FALSE(Parse("each ItemDefinition ItemId --where ~rifle", NoRefs).has_value());
    CHECK_FALSE(Parse("each ItemDefinition ItemId --where DisplayName~", NoRefs).has_value());
    CHECK_FALSE(Parse("each ItemDefinition ItemId --where A~b --seconds 2", NoRefs).has_value());
}

TEST_CASE("rows pages a data table with call's row options") {
    auto rows = Parse("rows $1 --match Rifle --fields Price,Item.TagName --start 10 --limit 0", TwoRefs);
    REQUIRE(rows.has_value());
    CHECK(rows->kind == Kind::Rows);
    CHECK(ue::GetRef(rows->request) == TwoRefs[0]);
    CHECK(rows->match == "Rifle");
    CHECK(rows->rowPaths == std::vector<std::string>{"Price", "Item.TagName"});
    CHECK((rows->from == 10 && rows->limit == 0));
    CHECK(Parse("rows $1", TwoRefs)->limit == DefaultCallRows);
    CHECK_FALSE(Parse("rows DataTable", TwoRefs).has_value());
    CHECK_FALSE(Parse("rows $1 --fields .a", TwoRefs).has_value());
}
