// reagentlive CLI calls: the Unreal pack's verbs' builders.
#include "reagentlive/frontends/cli/call/build.h"
#include "reagentlive/frontends/cli/call/plan.h"
#include "reagentlive/packs/ue/protocol_ue.h"
#include "reagentlive/packs/ue/request_ue.h"
#include "reagentlive/version.h"
#include "doctest.h"
#include "verbs.h"
#include "call_support.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace reagentlive;
using namespace reagentlive::cli;
using namespace call_support;

// --- Builders: Unreal --------------------------------------------------------------

TEST_CASE("release is a call with the release flag and an optional block") {
    auto parsed = Call(R"({"verb": "release", "args": {"object": "0x1000#5:9", "function": "Comp:FindAttribute"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Release);
    CHECK(parsed->request.op == ue::op::Call);
    CHECK(parsed->request.flags == ue::CallRelease);
    CHECK(parsed->request.length == 0); // The console sizes a zeroed block.
    CHECK(Call(R"({"verb": "release", "args": {"object": "0x2000#7:0", "function": "Comp:FindAttribute", "block": "00 00"}})")
              ->request.length == 2);
    CHECK(Why(R"({"verb": "release", "args": {"object": "0x1000#5:9"}})").starts_with("missing function"));
}

TEST_CASE("each reads fields only; call-each calls a function on every instance") {
    // Owner:Function is a function: each never calls.
    CHECK(Why(R"({"verb": "each", "args": {"cls": "Character", "paths": [["Character:CanJump"]]}})")
              .starts_with("paths: Character:CanJump is a function: call-each"));
    auto parsed = Call(R"({"verb": "call-each", "args": {"cls": "Character", "function": "Character:CanJump", "seconds": 10, "view": {"limit": 5}}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Each);
    CHECK(parsed->eachCall);
    CHECK(parsed->className == "Character");
    CHECK(parsed->fields == std::vector<std::string>{"Character:CanJump"});
    CHECK(parsed->seconds == 10);
    CHECK(parsed->limit == 5);
    // No inputs and no dry run: the row has no such keys.
    for (const char* key : {"block", "params", "dry"})
        CHECK(Why(Json{{"verb", "call-each"}, {"args", {{"cls", "Character"}, {"function", "CanJump"}, {key, true}}}})
                  .starts_with(std::string("unknown key '") + key + "'"));
    CHECK(Why(R"({"verb": "call-each", "args": {"cls": "0x1000#5:9", "function": "CanJump"}})")
              .starts_with("cls is a class name"));                             // A class, not an object.
    CHECK(Why(R"({"verb": "call-pure", "args": {"object": "0x1000#5:9", "function": "Actor:K2_GetActorLocation", "seconds": 2}})")
              .starts_with("unknown key 'seconds'"));
    // game-thread acts; status reads its state.
    CHECK(Call(R"({"verb": "game-thread", "args": {"action": "on"}})")->request.flags ==
          static_cast<uint32_t>(ue::GameThreadAction::On));
    CHECK(Call(R"({"verb": "game-thread", "args": {"action": "off"}})")->request.flags ==
          static_cast<uint32_t>(ue::GameThreadAction::Off));
    CHECK(Why(R"({"verb": "game-thread", "args": {"action": "status"}})").starts_with("action: one of on|off"));
    CHECK(Why(R"({"verb": "game-thread"})").starts_with("missing action"));
}

TEST_CASE("call-pure is a call with the pure flag, typed unless given a block; dry marks either form") {
    auto parsed = Call(R"({"verb": "call-pure", "args": {"object": "0x1000#5:9", "function": "Actor:K2_GetActorLocation"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.op == ue::op::Call);
    CHECK(parsed->request.flags == ue::CallPure);
    CHECK(parsed->kind == Kind::TypedCall);                 // No params: typed all the same, outputs decoded.
    CHECK(parsed->fields.empty());
    CHECK_FALSE(parsed->dry);
    CHECK(ue::Check(parsed->request) == Status::Ok);
    parsed = Call(R"({"verb": "call-pure", "args": {"object": "0x1000#5:9", "function": "Actor:K2_GetActorLocation", "block": "00"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind != Kind::TypedCall);                 // A block: raw.
    parsed = Call(R"({"verb": "call", "args": {"object": "0x1000#5:9", "function": "Actor:K2_GetActorLocation"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind != Kind::TypedCall);                 // call without params: a zeroed raw block.
    parsed = Call(R"({"verb": "call-pure", "args": {"object": "0x1000#5:9", "function": "SceneComponent:GetSocketTransform", "params": {"SocketName": "#42", "TransformSpace": 0}}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::TypedCall);
    CHECK(parsed->request.flags == ue::CallPure);
    parsed = Call(R"({"verb": "call", "args": {"object": "0x1000#5:9", "function": "Actor:K2_GetActorLocation", "dry": true}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->dry);
    CHECK(parsed->request.flags == 0);
    CHECK(Call(R"({"verb": "call", "args": {"object": "0x1000#5:9", "function": "Controller:GetControlRotation", "params": {}, "dry": true}})")->dry);
}

TEST_CASE("a call's object can be a default object by name, never a class") {
    auto parsed = Call(R"({"verb": "call-pure", "args": {"object": "Default__KismetSystemLibrary", "function": "IsValid", "params": {"Object": null}}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::TypedCall);
    CHECK(parsed->className == "Default__KismetSystemLibrary");
    CHECK(parsed->fields == std::vector<std::string>{"Object=None"});
    parsed = Call(R"({"verb": "call", "args": {"object": "Default__GameplayStatics", "function": "GetPlayerPawn", "block": "00"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->className == "Default__GameplayStatics");
    CHECK(Why(R"({"verb": "call", "args": {"object": "KismetSystemLibrary", "function": "IsValid", "block": "00"}})").starts_with("object is"));
}

TEST_CASE("a typed call's fields, start and limit page its returned arrays") {
    auto rows = Call(R"({"verb": "call", "args": {"object": "0x1000#5:9", "function": "Vendor:GetEntries", "view": {"fields": [["ItemTag"], ["Variations", "Price"]], "start": 100, "limit": 0}}})");
    REQUIRE(rows.has_value());
    CHECK(rows->kind == Kind::TypedCall); // Implied: no params needed.
    CHECK(rows->rowPaths == std::vector<std::string>{"ItemTag", "Variations.Price"});
    CHECK(rows->from == 100);
    CHECK(rows->limit == 0);
    CHECK(Call(R"({"verb": "call", "args": {"object": "0x1000#5:9", "function": "Vendor:GetEntries", "params": {"Count": 2}, "view": {"limit": 5}}})")
              ->fields == std::vector<std::string>{"Count=2"});
    CHECK(Why(R"({"verb": "call", "args": {"object": "0x1000#5:9", "function": "Vendor:GetEntries", "block": "00", "view": {"limit": 5}}})")
              .starts_with("block: "));                                          // A raw block has no rows.
    CHECK(Why(R"({"verb": "call", "args": {"object": "0x1000#5:9", "function": "Vendor:GetEntries", "view": {"fields": [["a.b"]]}}})")
              .starts_with("fields: "));
    CHECK(Why(R"({"verb": "call-pure", "args": {"object": "0x1000#5:9", "function": "Vendor:GetEntries", "view": {"fields": [["a"]]}}})")
              .starts_with("unknown key 'view'"));                              // Arrays own memory: call only.
}

TEST_CASE("nonzero: get, each, rows and a typed call; objects by Default__ name") {
    const auto call = Call(R"({"verb": "call", "args": {"object": "0x1000#5:9", "function": "Vendor:GetEntries", "view": {"nonzero": true}}})");
    REQUIRE(call.has_value());
    CHECK(call->kind == Kind::TypedCall); // Implied, as fields is.
    CHECK(call->nonzero);
    CHECK(Why(R"({"verb": "call", "args": {"object": "0x1000#5:9", "function": "F", "block": "00", "view": {"nonzero": true}}})").starts_with("block: "));
    CHECK(Call(R"({"verb": "get", "args": {"object": "0x1000#5:9", "paths": [["Health"], ["Stats"]], "view": {"nonzero": true}}})")->nonzero);
    CHECK_FALSE(Call(R"({"verb": "get", "args": {"object": "0x1000#5:9", "paths": [["Health"]]}})")->nonzero);
    // A default object by name, as call takes it; the console finds it before reading.
    CHECK(Call(R"({"verb": "get", "args": {"object": "Default__PhysicsSettings", "paths": [["PhysicalSurfaces"]]}})")->className ==
          "Default__PhysicsSettings");
    CHECK(Call(R"({"verb": "rows", "args": {"object": "Default__Table", "view": {"limit": 5}}})")->className == "Default__Table");
    CHECK(Call(R"({"verb": "entries", "args": {"object": "Default__Settings", "field": "Map"}})")->className == "Default__Settings");
    CHECK(Why(R"({"verb": "get", "args": {"object": "PhysicsSettings", "paths": [["PhysicalSurfaces"]]}})").contains("types")); // A class is not an object.
    CHECK(Call(R"({"verb": "each", "args": {"cls": "Pawn", "paths": [["Health"]], "view": {"nonzero": true}}})")->nonzero);
    CHECK(Call(R"({"verb": "rows", "args": {"object": "0x1000#5:9", "view": {"nonzero": true, "limit": 0}}})")->nonzero);
}

TEST_CASE("literal refs round-trip and work wherever an object or address goes") {
    const Ref ref{0x1b3eec26700, 185680, 9};
    CHECK(RefText(ref) == "0x1b3eec26700#185680:9");
    Ref parsed{};
    CHECK(text::LiteralRef(RefText(ref), parsed));
    CHECK(parsed == ref);
    const auto all = Call(R"({"verb": "get", "args": {"object": "0x1b3eec26700#185680:9"}})");
    REQUIRE(all.has_value());
    CHECK(ue::GetRef(all->request) == ref);
    const auto read = Call(R"({"verb": "read", "args": {"addr": {"base": "0x1b3eec26700#185680:9", "offset": "0x330"}, "len": 24}})");
    REQUIRE(read.has_value());
    CHECK(read->request.address == 0x1b3eec26700 + 0x330);
    const auto snap = Call(R"({"verb": "get", "args": {"object": "0x1b3eec26700#185680:9", "mode": "snap"}})");
    REQUIRE(snap.has_value());
    CHECK(ue::GetRef(snap->request) == ref);
    CHECK_FALSE(text::LiteralRef("0x1000#-1:0", parsed));
    CHECK_FALSE(text::LiteralRef("0#5:9", parsed));
    CHECK_FALSE(text::LiteralRef("0x1000:5#9", parsed));
    CHECK_FALSE(text::LiteralRef("0x1000#5", parsed));
}

TEST_CASE("get with no paths reads every field; types reads the layout") {
    auto parsed = Call(R"({"verb": "get", "args": {"object": "0x1000#5:9"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Dump);
    CHECK(parsed->request.op == ue::op::Fields);
    CHECK(ue::GetRef(parsed->request) == One);
    CHECK(Why(R"({"verb": "get"})").starts_with("missing object"));
    // A class by name has no values: types, or its Default__ object.
    CHECK(Why(R"({"verb": "get", "args": {"object": "Character"}})").contains("Default__Character"));
    CHECK(Call(R"({"verb": "get", "args": {"object": "Default__Character"}})")->kind == Kind::Dump);

    parsed = Call(R"({"verb": "get", "args": {"object": "0x2000#7:0", "mode": "types"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Remote);
    CHECK(parsed->request.op == ue::op::Fields);
    CHECK(ue::GetRef(parsed->request) == Two);
    CHECK(parsed->request.length == 0); // First page.
    CHECK(ue::Check(parsed->request) == Status::Ok);
    CHECK(ue::GetRef(Call(R"({"verb": "get", "args": {"object": "0x4000", "mode": "types"}})")->request).index == -1);
    CHECK(Call(R"({"verb": "get", "args": {"object": "Character", "own": true, "mode": "types", "view": {"match": "aim"}}})")->className == "Character");
    CHECK(Why(R"({"verb": "get", "args": {"object": "0x1000#5:9", "paths": [["Health"]], "mode": "types"}})").starts_with("paths: "));
    CHECK(Why(R"({"verb": "get", "args": {"object": "0x1000#5:9", "mode": "types", "view": {"nonzero": true}}})").starts_with("mode: types reads no values"));
}

TEST_CASE("get snap keeps a baseline in an optional file; changed reads it") {
    const auto snap = Call(R"({"verb": "get", "args": {"object": "0x1000#5:9", "file": "ctrl.snap", "mode": "snap"}})");
    REQUIRE(snap.has_value());
    CHECK(snap->kind == Kind::Snap);
    CHECK(snap->file == "ctrl.snap");
    CHECK(Call(R"({"verb": "get", "args": {"object": "0x1000#5:9", "mode": "snap"}})")->file.empty());
    const auto changed = Call(R"({"verb": "get", "args": {"object": "0x1000#5:9", "file": "ctrl.snap", "mode": "changed"}})");
    REQUIRE(changed.has_value());
    CHECK(changed->kind == Kind::Dump);
    CHECK(changed->changed);
    CHECK(changed->file == "ctrl.snap");
    CHECK(Why(R"({"verb": "get", "args": {"object": "0x1000", "mode": "snap"}})").starts_with("object: snap and changed need a literal ref"));
    CHECK(Why(R"({"verb": "get", "args": {"object": "0x1000#5:9", "mode": "snap", "view": {"nonzero": true}}})").starts_with("mode: snap keeps"));
    CHECK(Why(R"({"verb": "get", "args": {"object": "0x1000#5:9", "paths": [["Health"]], "mode": "changed"}})").starts_with("paths: "));
    CHECK(Why(R"({"verb": "get", "args": {"object": "0x1000#5:9", "file": "ctrl.snap"}})").starts_with("file goes with"));
}

TEST_CASE("get filters: match, nonzero and changed together") {
    auto parsed = Call(R"({"verb": "get", "args": {"object": "0x1000#5:9", "file": "before.snap", "mode": "changed", "view": {"match": "aim", "nonzero": true}}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Dump);
    CHECK(parsed->match == "aim");
    CHECK(parsed->nonzero);
    CHECK(parsed->changed);
    CHECK(parsed->file == "before.snap");
    parsed = Call(R"({"verb": "get", "args": {"object": "0x1000#5:9", "mode": "changed", "view": {"nonzero": true}}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->file.empty());
    CHECK(Why(R"({"verb": "get", "args": {"object": "0x1000#5:9", "bogus": true}})").starts_with("unknown key 'bogus'"));
    CHECK(Why(R"({"verb": "get", "args": {"object": "0x1000#5:9", "paths": [["Health"]], "view": {"match": "aim"}}})").starts_with("paths: "));
}

TEST_CASE("describe takes an object, or an object-table slot and serial") {
    const auto raw = Call(R"({"verb": "describe", "args": {"object": "0x1234"}})");
    REQUIRE(raw.has_value());
    CHECK(raw->request.op == ue::op::Describe);
    CHECK(ue::GetRef(raw->request) == Ref{0x1234, -1, 0});
    CHECK(ue::GetRef(Call(R"({"verb": "describe", "args": {"object": "0x1000#5:9"}})")->request) == One);
    const auto slot = Call(R"({"verb": "describe", "args": {"index": 331227, "serial": 138023}})");
    REQUIRE(slot.has_value());
    CHECK(ue::GetRef(slot->request) == Ref{0, 331227, 138023});
    CHECK(ue::Check(slot->request) == Status::Ok);
    CHECK(Why(R"({"verb": "describe", "args": {"index": 5, "serial": 0}})").starts_with("serial is"));
    CHECK(Why(R"({"verb": "describe", "args": {"object": "0x1000#5:9", "index": 5, "serial": 9}})").starts_with("object: give"));
    CHECK(Why(R"({"verb": "describe"})").starts_with("object: give"));
    Request weak{};
    weak.op = ue::op::Describe;
    ue::SetRef(weak, Ref{0, 5, 9});
    CHECK(ue::Check(weak) == Status::Ok);
    ue::SetRef(weak, Ref{0, 5, 0});
    CHECK(ue::Check(weak) == Status::BadRequest);
}

TEST_CASE("func resolves a named entry without a call payload") {
    const auto parsed = Call(R"({"verb": "func", "args": {"object": "0x1000#5:9", "function": "PrimitiveComponent:GetMass"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.op == ue::op::Function);
    CHECK(ue::GetRef(parsed->request) == One);
    CHECK(std::string_view(parsed->request.text) == "PrimitiveComponent:GetMass");
    CHECK(ue::Check(parsed->request) == Status::Ok);
    CHECK(Call(R"({"verb": "func", "args": {"object": "0x1234", "function": "CalculateWeaponTrace"}})").has_value());
    CHECK(Why(R"({"verb": "func", "args": {"object": "0x1000#5:9", "function": ":CalculateWeaponTrace"}})").starts_with("function: invalid"));
    CHECK(Why(R"({"verb": "func", "args": {"object": "0x1000#5:9", "function": "PrimitiveComponent:"}})").starts_with("function: invalid"));
    CHECK(Why(R"({"verb": "func", "args": {"object": "0x1000#5:9", "function": "F", "block": "00"}})").starts_with("unknown key 'block'"));
    auto request = parsed->request;
    request.length = 1;
    CHECK(ue::Check(request) == Status::BadRequest);
}

TEST_CASE("funcs pages the functions; func reads the signature too") {
    auto functions = Call(R"({"verb": "funcs", "args": {"object": "0x1000#5:9", "view": {"start": 32, "match": "Socket"}}})");
    REQUIRE(functions.has_value());
    CHECK(functions->request.op == ue::op::Functions);
    CHECK(ue::GetRef(functions->request) == One);
    CHECK(std::string_view(functions->request.text) == "Socket");
    CHECK(functions->request.length == 32);
    CHECK(ue::Check(functions->request) == Status::Ok);
    CHECK(Call(R"({"verb": "funcs", "args": {"object": "0x1000#5:9", "view": {"start": 32}}})").has_value());
    CHECK(Why(R"({"verb": "funcs", "args": {"object": "0x1000#5:9", "view": {"start": 99999, "match": "Socket"}}})").starts_with("start is"));
    const auto signature = Call(R"({"verb": "func", "args": {"object": "0x1000#5:9", "function": "SceneComponent:GetSocketTransform"}})");
    REQUIRE(signature.has_value());
    CHECK(signature->request.op == ue::op::Function);
}

TEST_CASE("script reads bytecode; decode with entry reads one event") {
    auto parsed = Call(R"({"verb": "script", "args": {"object": "0x1000#5:9", "function": "Owner:GetSpeed"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Script);
    CHECK(std::string_view(parsed->request.text) == "Owner:GetSpeed");
    CHECK_FALSE(parsed->decode);
    parsed = Call(R"({"verb": "script", "args": {"object": "0x1000#5:9", "function": "Owner:GetSpeed", "decode": true, "entry": "0x58"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->decode);
    CHECK(parsed->entry == 0x58u);
    CHECK(Why(R"({"verb": "script", "args": {"object": "0x1000#5:9", "function": "Owner:GetSpeed", "entry": "0x58"}})").starts_with("entry needs decode"));
    CHECK(Why(R"({"verb": "script", "args": {"object": "0x1000#5:9", "function": "F", "decode": true, "entry": "x"}})").starts_with("entry: "));
    // A big decode narrows: an offset range (entry and to) and view.match.
    parsed = Call(R"({"verb": "script", "args": {"object": "0x1000#5:9", "function": "F", "decode": true, "entry": "0x58", "to": "0x200", "view": {"match": "Damage"}}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->entry == 0x58u);
    CHECK(parsed->to == 0x200u);
    CHECK(parsed->match == "Damage");
    CHECK(Why(R"({"verb": "script", "args": {"object": "0x1000#5:9", "function": "F", "to": 16}})").starts_with("to needs decode"));
    CHECK(Why(R"({"verb": "script", "args": {"object": "0x1000#5:9", "function": "F", "decode": true, "entry": 32, "to": 16}})").starts_with("to is a bytecode offset past entry"));
    CHECK(Why(R"({"verb": "script", "args": {"object": "0x1000#5:9", "function": "F", "view": {"match": "x"}}})").starts_with("view.match needs decode"));
}

TEST_CASE("refs takes a pooled name or its index") {
    auto parsed = Call(R"({"verb": "name-refs", "args": {"index": 145935}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::NameRefs);
    CHECK(parsed->request.args[0] == 145935);
    parsed = Call(R"({"verb": "name-refs", "args": {"name": "Attribute.Gameplay.Character.ReloadSpeedMultiplier"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.args[0] == 0);
    CHECK(std::string_view(parsed->request.text) == "Attribute.Gameplay.Character.ReloadSpeedMultiplier");
    CHECK(Why(R"({"verb": "name-refs"})").starts_with("name: give"));
    CHECK(Why(R"({"verb": "name-refs", "args": {"name": "A", "index": 3}})").starts_with("name: give"));
    CHECK(Why(R"({"verb": "name-refs", "args": {"index": 0}})").starts_with("index is"));
}

TEST_CASE("name takes FName indices and the agent bounds them") {
    const auto parsed = Call(R"({"verb": "name", "args": {"indices": [2379, "0x10", 0]}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.op == ue::op::Names);
    CHECK(parsed->request.length == 12);
    int32_t second = 0;
    memcpy(&second, parsed->request.data + 4, 4);
    CHECK(second == 16);
    CHECK(ue::Check(parsed->request) == Status::Ok);
    CHECK(Why(R"({"verb": "name"})").starts_with("missing indices"));
    CHECK(Why(R"({"verb": "name", "args": {"indices": ["x"]}})").starts_with("indices: "));
    CHECK(Why(R"({"verb": "name", "args": {"indices": []}})").starts_with("indices: "));
    CHECK(Why(R"({"verb": "name", "args": {"indices": [2147483648]}})").starts_with("indices: up to"));
    Json many{{"verb", "name"}, {"args", {{"indices", Json::array()}}}};
    for (int i = 0; i < 100; ++i) many["args"]["indices"].push_back(i + 1);
    CHECK(BuildCall(AllVerbs(), many)->request.length == 400);
    Request request{};
    request.op = ue::op::Names;
    request.length = 6;
    CHECK(ue::Check(request) == Status::BadRequest);
    request.length = (ue::MaxNames + 1) * 4;
    CHECK(ue::Check(request) == Status::BadRequest);
}

TEST_CASE("outer takes an object or an outer's name, on find, each and census; elements on get and each") {
    const auto byRef = Call(R"({"verb": "find", "args": {"cls": "BodySetup", "outer": "0x10#4:2"}})");
    REQUIRE(byRef.has_value());
    CHECK(byRef->outerRef == Ref{0x10, 4, 2});                            // The whole ref, serial included.
    CHECK(byRef->outerText.empty());
    const auto inEach = Call(R"({"verb": "each", "args": {"cls": "BodySetup", "paths": [["BoneName"]], "outer": "0x1000#5:9"}})");
    REQUIRE(inEach.has_value());
    CHECK(inEach->outerRef == One);
    const auto byName = Call(R"({"verb": "census", "args": {"cls": "BodySetup", "outer": "PA_Pawn"}})");
    REQUIRE(byName.has_value());
    CHECK(byName->outerText == "PA_Pawn");
    CHECK(byName->outerRef.address == 0);
    CHECK(Call(R"({"verb": "find", "args": {"outer": "PA_Pawn"}})").has_value()); // An outer alone is a filter.
    const Json longOuter{{"verb", "find"}, {"args", {{"outer", std::string(64, 'x')}}}};
    CHECK(Why(longOuter).starts_with("outer is"));
    // elements: 1..256 on get (paths or every field) and each.
    CHECK(Call(R"({"verb": "get", "args": {"object": "0x1000#5:9", "paths": [["Map"]], "view": {"elements": 16}}})")->elements == 16);
    CHECK(Call(R"({"verb": "each", "args": {"cls": "Pawn", "paths": [["Map"]], "view": {"elements": 256}}})")->elements == 256);
    CHECK(Call(R"({"verb": "get", "args": {"object": "0x1000#5:9", "view": {"elements": 3}}})")->elements == 3);
    CHECK(Why(R"({"verb": "get", "args": {"object": "0x1000#5:9", "paths": [["Map"]], "view": {"elements": 0}}})").starts_with("elements is"));
    CHECK(Why(R"({"verb": "each", "args": {"cls": "Pawn", "paths": [["Map"]], "view": {"elements": 257}}})").starts_with("elements is"));
}

TEST_CASE("find takes text, a class filter, a cursor and a row limit") {
    const auto both = Call(R"({"verb": "find", "args": {"text": "Vendor", "cls": "Character", "view": {"start": 4096}}})");
    REQUIRE(both.has_value());
    CHECK(both->request.op == ue::op::Find);
    CHECK(std::string_view(both->request.text) == "Vendor");
    CHECK(std::string_view(reinterpret_cast<const char*>(both->request.data)) == "Character");
    CHECK(both->request.length == 10);
    CHECK(both->request.args[1] == 4096);
    CHECK(ue::Check(both->request) == Status::Ok);
    CHECK(both->limit == 0);
    CHECK(Call(R"({"verb": "find", "args": {"text": "Vendor", "view": {"limit": 50}}})")->limit == 50);
    CHECK(Why(R"({"verb": "find", "args": {"text": "Vendor", "view": {"limit": 0}}})").starts_with("limit must be"));
    CHECK(Why(R"({"verb": "find", "args": {"text": "Vendor", "view": {"limit": 1001}}})").starts_with("limit must be"));
    const auto classOnly = Call(R"({"verb": "find", "args": {"cls": "DataTable"}})");
    REQUIRE(classOnly.has_value());
    CHECK(classOnly->request.text[0] == 0);
    CHECK(ue::Check(classOnly->request) == Status::Ok);
    CHECK(classOnly->request.flags == 0);
    const auto live = Call(R"({"verb": "find", "args": {"cls": "DataTable", "live": true}})");
    REQUIRE(live.has_value());
    CHECK(live->request.flags == ue::FindLive);
    CHECK(ue::Check(live->request) == Status::Ok);
    CHECK(std::string_view(Call(R"({"verb": "find", "args": {"text": "two words", "view": {"start": 1}}})")->request.text) == "two words");
    CHECK(Why(R"({"verb": "find"})").starts_with("text: give text, cls or outer"));
    CHECK(Why(R"({"verb": "find", "args": {"cls": "A B"}})").starts_with("cls is"));
    CHECK(Why(R"({"verb": "find", "args": {"text": "x", "view": {"start": -1}}})").starts_with("view.start: "));
}

TEST_CASE("object verbs take a bare class name for the console to resolve") {
    for (const char* call : {R"({"verb": "get", "args": {"object": "Character", "mode": "types"}})",
                             R"({"verb": "get", "args": {"object": "BP_Door_C", "mode": "types"}})",
                             R"({"verb": "funcs", "args": {"object": "Character", "view": {"match": "Jump"}}})",
                             R"({"verb": "func", "args": {"object": "Character", "function": "CanJump"}})"}) {
        const auto parsed = Call(call);
        REQUIRE_MESSAGE(parsed.has_value(), call);
        CHECK_FALSE(parsed->className.empty());
        CHECK(parsed->request.address == 0); // Filled once the console resolves the class.
    }
    const auto func = Call(R"({"verb": "func", "args": {"object": "Character", "function": "Character:CanJump"}})");
    REQUIRE(func.has_value());
    CHECK(func->className == "Character");
    CHECK(func->request.op == ue::op::Function);
    CHECK(Call(R"({"verb": "get", "args": {"object": "0x1000#5:9", "mode": "types"}})")->className.empty());
    CHECK(Why(R"({"verb": "get", "args": {"object": "12abc", "mode": "types"}})").starts_with("object is"));
    CHECK(Why(R"({"verb": "get", "args": {"object": "Some-Thing", "mode": "types"}})").starts_with("object is"));
    CHECK(Why(R"({"verb": "func", "args": {"object": "Character", "function": ":Bad"}})").starts_with("function: "));
}

TEST_CASE("census takes an optional class, a match, a page and diff") {
    auto parsed = Call(R"({"verb": "census"})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.op == ue::op::Census);
    CHECK(parsed->limit == DefaultCensusLimit);
    CHECK_FALSE(parsed->changed);
    CHECK(ue::Check(parsed->request) == Status::Ok);
    parsed = Call(R"({"verb": "census", "args": {"cls": "Actor", "view": {"limit": 0}}})");
    REQUIRE(parsed.has_value());
    CHECK(std::string_view(reinterpret_cast<const char*>(parsed->request.data)) == "Actor");
    CHECK(parsed->limit == 0);
    CHECK(ue::Check(parsed->request) == Status::Ok);
    parsed = Call(R"({"verb": "census", "args": {"diff": true, "cls": "Actor", "view": {"match": "proj", "limit": 5}}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->changed);
    CHECK(parsed->match == "proj");
    CHECK(parsed->limit == 5);
    parsed = Call(R"({"verb": "census", "args": {"cls": "Actor", "view": {"match": "zone", "start": 100, "limit": 50}}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->from == 100);
    CHECK(parsed->limit == 50);
    CHECK(Why(R"({"verb": "census", "args": {"cls": "A B"}})").starts_with("cls is"));
    CHECK(Why(R"({"verb": "census", "args": {"cls": ["A", "B"]}})").starts_with("cls: "));
    Request request = parsed->request;
    CopyText(request.text, "x");
    CHECK(ue::Check(request) == Status::BadRequest); // Census takes no name text.
}

TEST_CASE("name-find takes text and a cursor") {
    const auto parsed = Call(R"({"verb": "name-find", "args": {"text": "Jump", "view": {"start": 131072}}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.op == ue::op::NameFind);
    CHECK(std::string_view(parsed->request.text) == "Jump");
    CHECK(parsed->request.args[1] == 131072);
    CHECK(ue::Check(parsed->request) == Status::Ok);
    CHECK(Why(R"({"verb": "name-find", "args": {"view": {"start": 3}}})").starts_with("missing text"));
    Request empty{};
    empty.op = ue::op::NameFind;
    CHECK(ue::Check(empty) == Status::BadRequest);
}

TEST_CASE("each takes a class, its columns and an optional repeat") {
    auto parsed = Call(R"({"verb": "each", "args": {"cls": "Character", "paths": [["Health"]], "seconds": 10}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Each);
    CHECK(parsed->request.op == ue::op::Each);
    CHECK(parsed->className == "Character");
    CHECK(parsed->fields == std::vector<std::string>{"Health"});
    CHECK(parsed->seconds == 10);
    CHECK_FALSE(parsed->eachCall);
    parsed = Call(R"({"verb": "each", "args": {"cls": "Pawn", "paths": [["Health"], ["Stats", "MaxSpeed"], ["Controller", "PlayerState", "Score"]]}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->fields == std::vector<std::string>{"Health", "Stats.MaxSpeed", "Controller.PlayerState.Score"});
    CHECK(parsed->seconds == 0);
    CHECK(Why(R"({"verb": "each", "args": {"cls": "Pawn", "paths": [["Health", "", "Max"]]}})").starts_with("paths: "));
    Json many{{"verb", "each"}, {"args", {{"cls", "Pawn"}, {"paths", Json::array()}}}};
    for (int i = 0; i <= 16; ++i) many["args"]["paths"].push_back(Json::array({"F" + std::to_string(i)}));
    CHECK(Why(many).starts_with("paths: at most 16 columns"));
    CHECK(Why(R"({"verb": "each", "args": {"cls": "Pawn"}})").starts_with("missing paths"));
    CHECK(Why(R"({"verb": "each", "args": {"cls": "0x1000#5:9", "paths": [["Health"]]}})").starts_with("cls is a class name"));
    CHECK(Why(R"({"verb": "each", "args": {"cls": "Pawn", "paths": [["Health"]], "seconds": 0}})").starts_with("seconds is"));
    CHECK(Why(R"({"verb": "each", "args": {"cls": "Pawn", "paths": [["Health"]], "seconds": 61}})").starts_with("seconds is"));
}

TEST_CASE("trace takes two ends: points, objects or sockets, and its options") {
    auto parsed = Call(R"({"verb": "line-trace", "args": {"start": {"object": "0x1000#5:9", "socket": "Head"}, "end": [100.5, -2, 3e2], "channel": 1, "simple": true, "ignore": "0x2000#7:0"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::LineTrace);
    CHECK(parsed->traceEnds[0].object == One);
    CHECK(parsed->traceEnds[0].socket == "Head");
    CHECK_FALSE(parsed->traceEnds[0].literal);
    CHECK(parsed->traceEnds[1].literal);
    CHECK(parsed->traceEnds[1].point[0] == 100.5);
    CHECK(parsed->traceEnds[1].point[1] == -2);
    CHECK(parsed->traceEnds[1].point[2] == 300);
    CHECK(parsed->traceChannel == 1);
    CHECK(parsed->traceSimple);
    CHECK(parsed->traceIgnore == Two);
    parsed = Call(R"({"verb": "line-trace", "args": {"start": [0, 0, 0], "end": "0x2000#7:0", "channel": 5}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->traceEnds[0].literal);
    CHECK(parsed->traceEnds[1].object == Two);
    CHECK(parsed->traceEnds[1].socket.empty());
    CHECK(parsed->traceChannel == 5);
    CHECK_FALSE(parsed->traceSimple);
    const struct { std::string_view args, message; } bad[] = {
        {R"({"start": "0x1000#5:9"})", "missing end"},
        {R"({"start": [1, 2], "end": "0x1000#5:9"})", "start: "},
        {R"({"start": [1, 2, 3, 4], "end": "0x1000#5:9"})", "start: "},
        {R"({"start": [1, 2, "x"], "end": "0x1000#5:9"})", "start: "},
        {R"({"start": {"object": "0x1000#5:9", "socket": ""}, "end": "0x2000#7:0"})", "start is"},
        {R"({"start": {"object": "0x1000#5:9"}, "end": "0x2000#7:0"})", "start: "},
        {R"({"start": "0x1000#5:9", "end": "0x2000"})", "end is"},
        {R"({"start": "0x1000#5:9", "end": "0x2000#7:0", "channel": 32})", "channel is"},
        {R"({"start": "0x1000#5:9", "end": "0x2000#7:0", "ignore": "0x7"})", "ignore is"},
    };
    for (const auto& each : bad) {
        const Json call{{"verb", "line-trace"}, {"args", Json::parse(each.args)}};
        CHECK_MESSAGE(Why(call).starts_with(each.message), each.args);
    }
}

TEST_CASE("calls-start takes an owner, a function and a duration; read and stop take a slot") {
    auto parsed = Call(R"({"verb": "calls-start", "args": {"owner": "0x1000#5:9", "function": "Owner:Damage", "duration": 10, "object": "0x2000#7:0", "dry": true}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Calls);
    CHECK(parsed->target == "start");
    CHECK(parsed->request.op == ue::op::CallsStart);
    CHECK(ue::GetRef(parsed->request) == One); // The duration must not overwrite the ref.
    CHECK(std::string(parsed->request.text) == "Owner:Damage");
    CHECK(parsed->request.args[1] == 10000);
    uint64_t filter = 0;
    REQUIRE(parsed->request.length == sizeof(filter));
    memcpy(&filter, parsed->request.data, sizeof(filter));
    CHECK(filter == Two.address);
    CHECK(parsed->request.flags == ue::CallsDry);
    const auto byClass = Call(R"({"verb": "calls-start", "args": {"owner": "SomeClass", "function": "Fire", "duration": 0.5}})");
    REQUIRE(byClass.has_value());
    CHECK(byClass->className == "SomeClass");
    CHECK(byClass->request.args[1] == 500);
    CHECK(Why(R"({"verb": "calls-start", "args": {"owner": "0x1000#5:9", "function": "Fire", "duration": 61}})").starts_with("duration is"));
    CHECK(Why(R"({"verb": "calls-start", "args": {"owner": "0x1000#5:9", "function": "Fire"}})").starts_with("missing duration"));
    CHECK(Why(R"({"verb": "calls-start", "args": {"owner": "0x1000#5:9", "function": "Fire", "duration": 5, "object": "7"}})")
              .starts_with("object is a literal ref"));
    const auto read = Call(R"({"verb": "calls-read", "args": {"slot": 3, "view": {"start": 40}}})");
    REQUIRE(read.has_value());
    CHECK(read->target == "read");
    CHECK(read->callsSlot == 3);
    CHECK(read->callsAfter == 40);
    const auto latest = Call(R"({"verb": "calls-stop"})");
    REQUIRE(latest.has_value());
    CHECK(latest->target == "stop");
    CHECK(latest->callsSlot == ue::CallsLatest);
    CHECK(Why(R"({"verb": "calls-read", "args": {"slot": 8}})").starts_with("slot is"));
}

TEST_CASE("label names an unreflected member by object or class, offset, type and name") {
    auto parsed = Call(R"({"verb": "member-name", "args": {"object": "0x1000#5:2", "offset": "0x538", "type": "f64[3]", "name": "Start"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::MemberName);
    CHECK(parsed->offset == 0x538);
    CHECK(parsed->target == "f64[3]");
    CHECK(parsed->match == "Start");
    parsed = Call(R"({"verb": "member-name", "args": {"object": "TriggerVolume", "offset": "0x53c", "type": "f32", "name": "Duration?"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->className == "TriggerVolume");
    CHECK(Why(R"({"verb": "member-name", "args": {"object": "TriggerVolume", "offset": "0x53c", "type": "f32"}})").starts_with("missing name"));
    CHECK(Why(R"({"verb": "member-name", "args": {"object": "TriggerVolume", "offset": "0x53c", "type": "f16", "name": "X"}})").starts_with("type"));
    CHECK(Why(R"({"verb": "member-name", "args": {"object": "TriggerVolume", "offset": "0x53c", "type": "pad", "name": "4"}})").starts_with("type"));
    CHECK(Why(R"({"verb": "member-name", "args": {"object": "TriggerVolume", "offset": "0x10000", "type": "u8", "name": "X"}})").starts_with("offset is"));
    CHECK(Why(R"({"verb": "member-name", "args": {"object": "TriggerVolume", "offset": "0x53c", "type": "u8", "name": "X Y"}})").starts_with("type"));
}

TEST_CASE("labels-export takes an optional file, module and kinds") {
    auto parsed = Call(R"({"verb": "labels-export", "args": {"file": "labels.jsonl"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::LabelsExport);
    CHECK(parsed->file == "labels.jsonl");
    CHECK(parsed->target.empty());
    CHECK(parsed->limit == label_kind::All); // Every kind by default.
    parsed = Call(R"({"verb": "labels-export", "args": {"file": "labels.jsonl", "module": "engine.dll"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->target == "engine.dll");
    CHECK(Call(R"({"verb": "labels-export"})")->file.empty());       // The console's labels file.
    parsed = Call(R"({"verb": "labels-export", "args": {"kinds": ["interface", "global"], "module": "engine.dll"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->limit == (label_kind::Interface | label_kind::Global));
    CHECK(Why(R"({"verb": "labels-export", "args": {"kinds": ["exec", "types"]}})").starts_with("kinds: ")); // Unknown kind.
    CHECK(Why(R"({"verb": "labels-export", "args": {"kinds": []}})").starts_with("kinds: "));
    CHECK(Why(R"({"verb": "labels-export", "args": {"kinds": "exec"}})").starts_with("kinds: "));
}

TEST_CASE("entries takes an object, a field and an optional cursor") {
    auto parsed = Call(R"({"verb": "entries", "args": {"object": "0x1000#5:9", "field": "Comp:Attributes"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Entries);
    CHECK(parsed->request.op == ue::op::Entries);
    CHECK(parsed->target == "Comp:Attributes");
    CHECK(parsed->limit == 0);
    CHECK(Call(R"({"verb": "entries", "args": {"object": "0x1000#5:9", "field": "Attributes", "view": {"start": 12}}})")->limit == 12);
    CHECK(Why(R"({"verb": "entries", "args": {"object": "0x1000#5:9"}})").starts_with("missing field"));
    CHECK(Why(R"({"verb": "entries", "args": {"object": "0x1000#5:9", "field": ""}})").starts_with("field is"));
}

TEST_CASE("rows pages a data table with call's row options") {
    auto rows = Call(R"({"verb": "rows", "args": {"object": "0x1000#5:9", "view": {"match": "Rifle", "fields": [["Price"], ["Item", "TagName"]], "start": 10, "limit": 0}}})");
    REQUIRE(rows.has_value());
    CHECK(rows->kind == Kind::Rows);
    CHECK(ue::GetRef(rows->request) == One);
    CHECK(rows->match == "Rifle");
    CHECK(rows->rowPaths == std::vector<std::string>{"Price", "Item.TagName"});
    CHECK(rows->from == 10);
    CHECK(rows->limit == 0);
    CHECK(Call(R"({"verb": "rows", "args": {"object": "0x1000#5:9"}})")->limit == DefaultCallRows);
    CHECK(Call(R"({"verb": "rows", "args": {"object": "0x1000#5:9", "at": 1.5}})")->curveAt == 1.5f);
    CHECK(Why(R"({"verb": "rows", "args": {"object": "DataTable"}})").starts_with("object is"));
    CHECK(Why(R"({"verb": "rows", "args": {"object": "0x1000#5:9", "view": {"fields": [[".a"]]}}})").starts_with("fields: "));
    CHECK(Why(R"({"verb": "rows", "args": {"object": "0x1000#5:9", "at": 1, "view": {"fields": [["a"]]}}})").starts_with("at reads curve tables"));
}

TEST_CASE("layout-export takes class names and a closure depth; layout-check a file") {
    auto parsed = Call(R"({"verb": "layout-export", "args": {"file": "out.jsonl", "classes": ["Pawn", "PlayerController"]}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Layout);
    CHECK(parsed->target == "export");
    CHECK(parsed->fields == std::vector<std::string>{"Pawn", "PlayerController"});
    CHECK_FALSE(parsed->closure);
    CHECK(Why(R"({"verb": "layout-export", "args": {"file": "out.jsonl"}})").starts_with("missing classes"));
    CHECK(Why(R"({"verb": "layout-export", "args": {"file": "out.jsonl", "classes": ["Bad.Name"]}})").starts_with("classes: "));
    CHECK(Call(R"({"verb": "layout-export", "args": {"file": "out.jsonl", "classes": ["Pawn"], "closure": 1}})")->closure == 1u);
    CHECK(Call(R"({"verb": "layout-export", "args": {"file": "out.jsonl", "classes": ["Pawn", "ENetRole"], "closure": 3}})")->closure == 3u);
    CHECK(Why(R"({"verb": "layout-export", "args": {"file": "out.jsonl", "classes": ["Pawn"], "closure": 0}})").starts_with("closure is"));
    CHECK(Why(R"({"verb": "layout-export", "args": {"file": "out.jsonl", "classes": ["Pawn"], "closure": 9}})").starts_with("closure is"));
    CHECK(Why(R"({"verb": "layout-export", "args": {"file": "out.jsonl", "classes": ["Pawn"], "closure": true}})").starts_with("closure: "));
    parsed = Call(R"({"verb": "layout-check", "args": {"file": "expected.jsonl"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->target == "check");
    CHECK(parsed->file == "expected.jsonl");
}
