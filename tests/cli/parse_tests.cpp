// reagentlive CLI syntax: command lines to requests.
#include "reagentlive/frontends/cli/syntax/parse.h"
#include "reagentlive/ue/protocol_ue.h"
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

TEST_CASE("export-labels takes a file and an optional module") {
    auto parsed = Parse("export-labels labels.jsonl", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::ExportLabels);
    CHECK(parsed->file == "labels.jsonl");
    CHECK(parsed->target.empty());
    parsed = Parse("export-labels labels.jsonl --module engine.dll", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->target == "engine.dll");
    CHECK_FALSE(Parse("export-labels", NoRefs).has_value());
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
    CHECK_FALSE(Parse("peek base+0x100 --label", NoRefs, modules).has_value());
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
    CHECK_FALSE(Parse("params $1 Owner:GetSpeed --script", TwoRefs).has_value());
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

TEST_CASE("write --trial needs an owner ref and expected bytes") {
    auto parsed = Parse("trial $1+0x10 2a 00 00 00 --owner $1 --expect 07 00 00 00 --seconds 5", TwoRefs);
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
    parsed = Parse("trial 0x5008 01 --owner $1 --object 0x5000 --expect 00", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->object == 0x5000);
    CHECK_FALSE(Parse("trial $1 01 --expect 00", TwoRefs).has_value());          // No owner.
    CHECK_FALSE(Parse("trial $1 01 --owner $1", TwoRefs).has_value());           // No --expect.
    CHECK_FALSE(Parse("trial $1 01 --owner 0x1000 --expect 00", TwoRefs).has_value()); // Owner must be a ref.
    CHECK_FALSE(Parse("trial $1 01 --owner $1 --expect 00 --seconds 61", TwoRefs).has_value());
    parsed = Parse("trial-clear 3", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.flags == static_cast<uint32_t>(TrialAction::Clear));
    CHECK(parsed->request.args[0] == 3);
    CHECK(Parse("trial-list", NoRefs).has_value());
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

TEST_CASE("base is local and modules asks the agent") {
    auto parsed = Parse("base", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Base);
    CHECK_FALSE(Parse("base 1", NoRefs).has_value());
    parsed = Parse("modules", NoRefs);
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

    parsed = Parse("search text \"ab c\" --wide", NoRefs);
    REQUIRE(parsed.has_value());
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

TEST_CASE("pin and watch parse targets, guards and durations") {
    const Ref refs[] = {{0x7000, 12, 3}};
    auto parsed = Parse("pin $1+0x3e4 00 00 80 3f --seconds 5 --expect 00 00 00 00", refs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Pin);
    CHECK(parsed->request.flags == static_cast<uint32_t>(PinAction::Add));
    CHECK(parsed->request.address == 0x7000 + 0x3e4);
    CHECK(parsed->request.length == 4);
    CHECK(parsed->milliseconds == 5000);
    CHECK(parsed->request.args[1] == 1);
    REQUIRE(parsed->guardObjects.size() == 1);
    CHECK(parsed->guardObjects[0] == 0x7000);
    parsed = Parse("pin 0x5000 01", refs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->guardObjects[0] == 0);
    CHECK(parsed->milliseconds == 10000);
    parsed = Parse("pin-clear 3", refs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.flags == static_cast<uint32_t>(PinAction::Clear));
    CHECK(parsed->request.args[0] == 3);
    CHECK(Parse("pin-list", refs).has_value());
    CHECK_FALSE(Parse("pin 0x5000 01 --seconds 61", refs).has_value());
    CHECK_FALSE(Parse("pin 0x5000 01 02 --expect 01", refs).has_value());
    CHECK_FALSE(Parse("pin-clear x", refs).has_value());

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

TEST_CASE("fields takes one object as a ref or raw address") {
    auto parsed = Parse("fields $2", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.op == ue::op::Fields);
    CHECK(ue::GetRef(parsed->request) == Ref{0x2000, 7, 0});
    CHECK(parsed->request.length == 0); // First page.
    CHECK(ue::Check(parsed->request) == Status::Ok);
    parsed = Parse("fields 0x4000", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(ue::GetRef(parsed->request).index == -1);
    CHECK_FALSE(Parse("fields", NoRefs).has_value());
    CHECK_FALSE(Parse("fields $1 extra", TwoRefs).has_value());
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
    CHECK_FALSE(Parse("stats extra", NoRefs).has_value());
    CHECK_FALSE(Parse("batch { stats }", NoRefs).has_value());
    CHECK_FALSE(Parse("frobnicate", NoRefs).has_value());
}

TEST_CASE("dump parses like fields and is marked for values") {
    const auto parsed = Parse("dump $1", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Dump);
    CHECK(parsed->request.op == ue::op::Fields);
    CHECK(ue::GetRef(parsed->request) == Ref{0x1000, 5, 9});
    CHECK_FALSE(Parse("dump", NoRefs).has_value());
}

TEST_CASE("capture commands preserve the ref and bound the recording") {
    const auto record = Parse("record $1 4 burst.tsv Ammo Weapon:State", TwoRefs);
    REQUIRE(record.has_value());
    CHECK(record->kind == Kind::Record);
    CHECK(ue::GetRef(record->request) == Ref{0x1000, 5, 9});
    CHECK(record->request.length == 4);
    CHECK(record->file == "burst.tsv");
    CHECK(record->fields == std::vector<std::string>{"Ammo", "Weapon:State"});
    CHECK_FALSE(Parse("snap 0x1000", TwoRefs).has_value());
    CHECK_FALSE(Parse("snap $1 a.snap extra", TwoRefs).has_value());
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

    auto signature = Parse("params $1 SceneComponent:GetSocketTransform", TwoRefs);
    REQUIRE(signature.has_value());
    CHECK(signature->request.op == ue::op::Parameters);
    CHECK(ue::GetRef(signature->request) == TwoRefs[0]);
    auto call = Parse("call $1 SceneComponent:GetSocketTransform SocketName=#42 TransformSpace=0", TwoRefs);
    REQUIRE(call.has_value());
    CHECK(call->kind == Kind::TypedCall);
    CHECK(call->request.op == ue::op::Call);
    CHECK(call->fields == std::vector<std::string>{"SocketName=#42", "TransformSpace=0"});
    CHECK(Parse("call $1 Controller:GetControlRotation --typed", TwoRefs)->kind == Kind::TypedCall);
}

TEST_CASE("literal refs round-trip and work wherever $N does") {
    const Ref ref{0x1b3eec26700, 185680, 9};
    CHECK(RefText(ref) == "0x1b3eec26700#185680:9");
    Ref parsed{};
    CHECK(text::LiteralRef(RefText(ref), parsed));
    CHECK(parsed == ref);
    const auto dump = Parse("dump 0x1b3eec26700#185680:9", NoRefs);
    REQUIRE(dump.has_value());
    CHECK(ue::GetRef(dump->request) == ref);
    const auto read = Parse("read 0x1b3eec26700#185680:9+0x330 24", NoRefs);
    REQUIRE(read.has_value());
    CHECK(read->request.address == 0x1b3eec26700 + 0x330);
    const auto snap = Parse("snap 0x1b3eec26700#185680:9", NoRefs);
    REQUIRE(snap.has_value());
    CHECK(ue::GetRef(snap->request) == ref);
    CHECK_FALSE(text::LiteralRef("0x1000#-1:0", parsed));
    CHECK_FALSE(text::LiteralRef("0#5:9", parsed));
    CHECK_FALSE(text::LiteralRef("0x1000:5#9", parsed));
    CHECK_FALSE(text::LiteralRef("0x1000#5", parsed));
}

TEST_CASE("snap takes an optional file; dump --changed reads it; diff is gone") {
    const auto snap = Parse("snap $1 ctrl.snap", TwoRefs);
    REQUIRE(snap.has_value());
    CHECK(snap->kind == Kind::Snap);
    CHECK(snap->file == "ctrl.snap");
    const auto bare = Parse("snap $1", TwoRefs);
    REQUIRE(bare.has_value());
    CHECK(bare->file.empty());
    const auto changed = Parse("dump $1 --changed ctrl.snap", TwoRefs);
    REQUIRE(changed.has_value());
    CHECK(changed->changed);
    CHECK(changed->file == "ctrl.snap");
    CHECK_FALSE(Parse("diff $1", TwoRefs).has_value());
}

TEST_CASE("one-shot commands split on semicolons and newlines") {
    CHECK(SplitCommands("find PC_; dump $1") == std::vector<std::string>{"find PC_", "dump $1"});
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

TEST_CASE("find takes a class filter and a cursor") {
    const auto both = Parse("find Vendor --cls Character --start 4096", NoRefs);
    REQUIRE(both.has_value());
    CHECK(std::string_view(both->request.text) == "Vendor");
    CHECK(std::string_view(reinterpret_cast<const char*>(both->request.data)) == "Character");
    CHECK(both->request.length == 10);
    CHECK(both->request.args[1] == 4096);
    CHECK(ue::Check(both->request) == Status::Ok);
    const auto classOnly = Parse("find --cls DataTable", NoRefs);
    REQUIRE(classOnly.has_value());
    CHECK(classOnly->request.text[0] == 0);
    CHECK(ue::Check(classOnly->request) == Status::Ok);
    const auto spaced = Parse("find two words --start 1", NoRefs);
    REQUIRE(spaced.has_value());
    CHECK(std::string_view(spaced->request.text) == "two words");
    CHECK_FALSE(Parse("find", NoRefs).has_value());
    CHECK_FALSE(Parse("find --cls", NoRefs).has_value());
    CHECK_FALSE(Parse("find x --start -1", NoRefs).has_value());
    CHECK_FALSE(Parse("find --cls A --cls B", NoRefs).has_value());
}

TEST_CASE("read-batch parses ranges and bounds the total response") {
    const std::vector<Module> exe{{"game.exe", 0x1000, 0x100}};
    const auto command = Parse("read-batch base+16 8 0x2000 16", {}, exe);
    REQUIRE(command.has_value());
    CHECK(command->request.op == reagentlive::op::ReadBatch);
    REQUIRE(command->reads.size() == 2);
    CHECK(command->reads[0].address == 0x1010);
    CHECK(command->reads[1].length == 16);
    CHECK(Parse("read-batch 1 4092", {}).has_value());
    CHECK_FALSE(Parse("read-batch 1 4093", {}).has_value());
    CHECK_FALSE(Parse("read-batch 1 2 3", {}).has_value());
    CHECK_FALSE(Parse("read-batch 0xffffffffffffffff 1", {}).has_value());
    CHECK_FALSE(Parse("read-batch 1 1 2 1 3 1 4 1 5 1 6 1 7 1 8 1 9 1", {}).has_value());
    // A refusal states the limit it hit.
    CHECK(Parse("read-batch 1 1 2 1 3 1 4 1 5 1 6 1 7 1 8 1 9 1", {}).error().starts_with("at most 8 ranges"));
    CHECK(Parse("read-batch 1 600 2 600 3 600 4 600 5 600 6 600 7 600 8 600", {}).error().starts_with(
        "the ranges total 4200 bytes; 7 ranges take at most 4068 (4096 less 4 per range)"));
}

TEST_CASE("object commands take a bare class name for the console to resolve") {
    for (const char* line : {"fields Character", "dump BP_Door_C", "funcs Character Jump",
                             "func Character CanJump"}) {
        const auto parsed = Parse(line, NoRefs);
        REQUIRE(parsed.has_value());
        CHECK_FALSE(parsed->className.empty());
        CHECK(parsed->request.address == 0); // Filled once the console resolves the class.
    }
    const auto params = Parse("params Character Character:CanJump", NoRefs);
    REQUIRE(params.has_value());
    CHECK(params->className == "Character");
    CHECK(params->request.op == ue::op::Parameters);
    CHECK(Parse("fields $1", TwoRefs)->className.empty());
    CHECK_FALSE(Parse("params Character CanJump extra", NoRefs).has_value());
    CHECK_FALSE(Parse("fields 12abc", NoRefs).has_value());
    CHECK_FALSE(Parse("fields Some-Thing", NoRefs).has_value());
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
    parsed = Parse("census-diff --cls Actor --match proj --limit 5", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->changed);
    CHECK(parsed->match == "proj");
    CHECK(parsed->limit == 5);
    CHECK_FALSE(Parse("census --match", NoRefs).has_value());
    CHECK_FALSE(Parse("census-diff --match", NoRefs).has_value());
    CHECK_FALSE(Parse("census --match a --match b", NoRefs).has_value());
    parsed = Parse("census --cls Actor --match zone --start 100 --limit 50", NoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->from == 100);
    CHECK(parsed->limit == 50);
    CHECK_FALSE(Parse("census --start", NoRefs).has_value());
    CHECK_FALSE(Parse("census --start 1 --start 2", NoRefs).has_value());
    CHECK_FALSE(Parse("census-diff --changed", NoRefs).has_value());
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

TEST_CASE("get takes an object and dotted field paths") {
    auto parsed = Parse("get $1 Mesh.AnimInstance.AimAlpha Owner:Health", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Get);
    CHECK(ue::GetRef(parsed->request) == TwoRefs[0]);
    CHECK(parsed->fields == std::vector<std::string>{"Mesh.AnimInstance.AimAlpha", "Owner:Health"});
    CHECK_FALSE(Parse("get $1", TwoRefs).has_value());
    CHECK_FALSE(Parse("get $1 A..B", TwoRefs).has_value());
    CHECK_FALSE(Parse("get $1 .A", TwoRefs).has_value());
    CHECK_FALSE(Parse("get $1 A.B.C.D.E.F.G.H.I", TwoRefs).has_value());
    CHECK_FALSE(Parse("get $3 A", TwoRefs).has_value());
}

TEST_CASE("dump filters parse; fields takes none") {
    auto parsed = Parse("dump $1 --match aim --nonzero --changed before.snap", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Dump);
    CHECK(parsed->match == "aim");
    CHECK(parsed->nonzero);
    CHECK(parsed->changed);
    CHECK(parsed->file == "before.snap");
    parsed = Parse("dump $1 --changed --nonzero", TwoRefs);
    REQUIRE(parsed.has_value());
    CHECK(parsed->changed);
    CHECK(parsed->file.empty());
    CHECK_FALSE(Parse("dump $1 --match", TwoRefs).has_value());
    CHECK_FALSE(Parse("dump $1 --bogus", TwoRefs).has_value());
    CHECK_FALSE(Parse("fields $1 --nonzero", TwoRefs).has_value());
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
