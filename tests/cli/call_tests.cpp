// reagentlive CLI calls: typed values and the core verbs' builders.
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

// --- Typed values (values.h) ------------------------------------------------------

TEST_CASE("an address is hex text, a literal ref, a module and rva, or a base and offset") {
    auto place = value::Address(Json("0x7ff0"), NoModules);
    REQUIRE(place.has_value());
    CHECK(place->address == 0x7ff0);
    CHECK(place->object == 0);
    CHECK(place->chain.empty());
    place = value::Address(Json("0x1000#5:9"), NoModules);          // A literal ref: the object it names.
    REQUIRE(place.has_value());
    CHECK(place->address == 0x1000);
    CHECK(place->object == 0x1000);
    place = value::Address(Json::parse(R"({"base": "0x1000#5:9", "offset": "0x330"})"), NoModules);
    REQUIRE(place.has_value());
    CHECK(place->address == 0x1330);
    CHECK(place->object == 0x1000);                                 // A ref base keeps its object.
    place = value::Address(Json::parse(R"({"base": "0x1000", "offset": 16})"), NoModules);
    REQUIRE(place.has_value());
    CHECK(place->address == 0x1010);
    CHECK(place->object == 0);
    for (const char* bad : {R"("4096")", R"("0x0")", R"("zz")", R"("0x1000#-1:0")"})
        CHECK_MESSAGE(!value::Address(Json::parse(bad), NoModules), bad);
    CHECK_FALSE(value::Address(Json::parse(R"({"base": "0xffffffffffffffff", "offset": 1})"), NoModules));
}

TEST_CASE("a module address needs the module list and stays inside the image") {
    auto place = value::Address(Json::parse(R"({"module": "base", "rva": "0x10"})"), Loaded);
    REQUIRE(place.has_value());
    CHECK(place->address == 0x7ff600000010);                        // base names the exe.
    place = value::Address(Json::parse(R"({"module": "ENGINE.DLL", "rva": "0x1ff8"})"), Loaded); // Names ignore case.
    REQUIRE(place.has_value());
    CHECK(place->address == 0x7ffa00001ff8);
    place = value::Address(Json::parse(R"({"module": "engine.dll", "rva": 0})"), Loaded);
    REQUIRE(place.has_value());
    CHECK(place->address == 0x7ffa00000000);
    const auto past = value::Address(Json::parse(R"({"module": "engine.dll", "rva": "0x2000"})"), Loaded);
    REQUIRE_FALSE(past);
    CHECK(past.error().starts_with("the rva is past"));
    CHECK(value::Address(Json::parse(R"({"module": "other.dll", "rva": "0x10"})"), Loaded).error().starts_with("no loaded module"));
    CHECK_FALSE(value::Address(Json::parse(R"({"module": "base", "rva": "0x10"})"), NoModules)); // List unknown.
    // Through a call: the builder gets the console's list.
    const auto read = Call(R"({"verb": "read", "args": {"addr": {"module": "base", "rva": "0x10"}, "len": 4}})", Loaded);
    REQUIRE(read.has_value());
    CHECK(read->request.address == 0x7ff600000010);
    CHECK(Why(R"({"verb": "read", "args": {"addr": {"module": "base", "rva": "0x10"}}})").starts_with("addr: no loaded module"));
    const auto search = Call(R"({"verb": "search", "args": {"type": "u32", "value": 1, "scope": {"start": {"module": "app.exe", "rva": "0x20"}}}})",
        Loaded);
    REQUIRE(search.has_value());
    CHECK(search->request.address == 0x7ff600000020);
}

TEST_CASE("a pointer chain takes two to sixteen offsets, and never in a write") {
    auto place = value::Address(Json::parse(R"({"base": "0x1000#5:9", "chain": ["0x10", 8]})"), NoModules, true);
    REQUIRE(place.has_value());
    CHECK(place->address == 0x1000);
    CHECK(place->chain == std::vector<uint64_t>{0x10, 8});
    const auto one = value::Address(Json::parse(R"({"base": "0x1000", "chain": [16]})"), NoModules, true);
    REQUIRE_FALSE(one);
    CHECK(one.error() == value::ChainNoHop);
    Json longChain = Json::parse(R"({"base": "0x1000", "chain": []})");
    for (int i = 0; i <= 16; ++i) longChain["chain"].push_back(8);
    CHECK(value::Address(longChain, NoModules, true).error().starts_with("a chain takes at most 16"));
    // Where chains are not taken, a chain is refused for what it is.
    const auto refused = value::Address(Json::parse(R"({"base": "0x1000", "chain": [16, 8]})"), NoModules);
    REQUIRE_FALSE(refused);
    CHECK(refused.error() == value::ChainRefused);

    const auto read = Call(R"({"verb": "read", "args": {"addr": {"base": "0x1000#5:9", "chain": [16, 8]}, "len": 8}})");
    REQUIRE(read.has_value());
    CHECK(read->request.address == 0x1000);
    CHECK(read->chain == std::vector<uint64_t>{16, 8});
    CHECK(Why(R"({"verb": "read", "args": {"addr": {"base": "0x1000", "chain": [16]}}})") ==
          std::string("addr: ") + value::ChainNoHop);
    for (const char* verb : {"peek", "mem-snap", "vtable", "disasm"}) {
        Json call{{"verb", verb}, {"args", Json::parse(R"({"addr": {"base": "0x1000", "chain": [16, 8]}})")}};
        if (std::string_view(verb) == "mem-snap") call["args"]["len"] = 64;
        const auto parsed = BuildCall(AllVerbs(), call);
        REQUIRE_MESSAGE(parsed.has_value(), verb);
        CHECK(parsed->chain.size() == 2);
    }
    // write's addr is no chain: the schema refuses it before the builder.
    CHECK(Why(R"({"verb": "write", "args": {"addr": {"base": "0x1000", "chain": [16, 8]}, "data": "01"}})").starts_with("addr: an address"));
    CHECK(Why(R"({"verb": "xrefs", "args": {"addr": {"base": "0x1000", "chain": [16, 8]}}})").starts_with("addr: an address"));
}

TEST_CASE("a path is a list of names joined by '.'; a step holds no '.' and the depth is bounded") {
    std::string joined;
    CHECK(value::Path(Json::parse(R"(["Mesh", "AnimInstance", "AimAlpha"])"), joined, MaxGetDepth).empty());
    CHECK(joined == "Mesh.AnimInstance.AimAlpha");
    CHECK(value::Path(Json::parse(R"(["Owner:Health"])"), joined, MaxGetDepth).empty());
    CHECK(joined == "Owner:Health");
    CHECK(value::Path(Json::parse(R"(["A.B"])"), joined, MaxGetDepth).starts_with("a path step is one field name"));
    CHECK_FALSE(value::Path(Json::parse(R"(["A", ""])"), joined, MaxGetDepth).empty());
    CHECK(value::Path(Json::parse(R"(["A", "B", "C", "D", "E", "F", "G", "H", "I"])"), joined, MaxGetDepth)
              .starts_with("a path is up to 8 steps"));
    // Through get.
    const auto get = Call(R"({"verb": "get", "args": {"object": "0x1000#5:9", "paths": [["Mesh", "AnimInstance", "AimAlpha"], ["Owner:Health"]]}})");
    REQUIRE(get.has_value());
    CHECK(get->kind == Kind::Get);
    CHECK(ue::GetRef(get->request) == One);
    CHECK(get->fields == std::vector<std::string>{"Mesh.AnimInstance.AimAlpha", "Owner:Health"});
    CHECK(Why(R"({"verb": "get", "args": {"object": "0x1000#5:9", "paths": [["A.B"]]}})").starts_with("paths: a path step"));
    CHECK(Why(R"({"verb": "get", "args": {"object": "0x1000#5:9", "paths": [["A", "B", "C", "D", "E", "F", "G", "H", "I"]]}})")
              .starts_with("paths: a path is up to 8"));
    CHECK(Why(R"({"verb": "get", "args": {"object": "0x1000#5:9", "paths": [[]]}})").starts_with("paths: "));    // Schema: no empty path.
}

TEST_CASE("each's where filters on a field's value; a filter is exactly field and contains") {
    auto each = Call(R"({"verb": "each", "args": {"cls": "ItemDefinition", "paths": [["ItemId"]], "where": {"field": ["DisplayName"], "contains": "rifle"}, "view": {"limit": 5}}})");
    REQUIRE(each.has_value());
    CHECK(each->whereField == "DisplayName");
    CHECK(each->match == "rifle");
    CHECK(each->limit == 5);
    each = Call(R"({"verb": "each", "args": {"cls": "ItemDefinition", "paths": [["ItemId"]], "where": {"field": ["Tag", "TagName"], "contains": "A~B"}}})");
    REQUIRE(each.has_value());
    CHECK(each->whereField == "Tag.TagName");
    CHECK(each->match == "A~B");
    CHECK(Call(R"({"verb": "each", "args": {"cls": "ItemDefinition", "paths": [["ItemId"]]}})")->limit == DefaultEachRows);
    CHECK(Why(R"({"verb": "each", "args": {"cls": "I", "paths": [["ItemId"]], "where": {"field": ["DisplayName"], "contains": ""}}})")
              .starts_with("where: contains"));
    CHECK(Why(R"({"verb": "each", "args": {"cls": "I", "paths": [["ItemId"]], "where": {"field": ["A"], "contains": "b"}, "seconds": 2}})")
              .starts_with("where and seconds"));
    CHECK(Why(R"({"verb": "each", "args": {"cls": "I", "paths": [["ItemId"]], "where": {"field": ["A.B"], "contains": "b"}}})")
              .starts_with("where: "));
    for (const char* where : {R"({"field": ["A"]})", R"({"contains": "b"})", R"({"field": [], "contains": "b"})",
                              R"({"field": ["A"], "contains": "b", "more": 1})", R"("A~b")"}) {
        Json call = Json::parse(R"({"verb": "each", "args": {"cls": "I", "paths": [["ItemId"]]}})");
        call["args"]["where"] = Json::parse(where);
        CHECK_MESSAGE(Why(call).starts_with("where: {"), where);
    }
}

TEST_CASE("params make a typed call: each input as Name=value") {
    auto call = Call(R"({"verb": "call", "args": {"object": "0x1000#5:9", "function": "SceneComponent:GetSocketTransform", "params": {"SocketName": "#42", "TransformSpace": 0}}})");
    REQUIRE(call.has_value());
    CHECK(call->kind == Kind::TypedCall);
    CHECK(call->request.op == ue::op::Call);
    CHECK(call->fields == std::vector<std::string>{"SocketName=#42", "TransformSpace=0"});
    // A struct as a list in member order or an object by member name, null as None.
    call = Call(R"({"verb": "call", "args": {"object": "0x1000#5:9", "function": "F", "params": {"Hit": null, "Loc": [1, 2.5, -3], "Name": "#42", "Ok": true, "Rot": {"Pitch": 1.5, "Yaw": 0}, "Xf": {"Loc": [1, 2, 3]}}}})");
    REQUIRE(call.has_value());
    CHECK(call->fields == std::vector<std::string>{"Hit=None", "Loc=(1, 2.5, -3)", "Name=#42", "Ok=true",
                                                  "Rot=(Pitch=1.5, Yaw=0)", "Xf=(Loc=(1, 2, 3))"});
    // {} is a typed call with no inputs.
    call = Call(R"({"verb": "call", "args": {"object": "0x1000#5:9", "function": "Controller:GetControlRotation", "params": {}}})");
    REQUIRE(call.has_value());
    CHECK(call->kind == Kind::TypedCall);
    CHECK(call->fields.empty());
    CHECK(call->limit == DefaultCallRows);
    CHECK(Why(R"({"verb": "call", "args": {"object": "0x1000#5:9", "function": "F", "params": {"A": 1}, "block": "00"}})")
              .starts_with("params: "));
    CHECK(Why(R"({"verb": "call", "args": {"object": "0x1000#5:9", "function": "F", "params": [1]}})").starts_with("params: "));
}

TEST_CASE("block makes a raw call: the full ref, the function and the bytes") {
    auto parsed = Call(R"({"verb": "call", "args": {"object": "0x1000#5:9", "function": "Actor:K2_GetActorLocation", "block": "00"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind != Kind::TypedCall);
    CHECK(parsed->request.op == ue::op::Call);
    CHECK(ue::GetRef(parsed->request) == One);
    CHECK(std::string_view(parsed->request.text) == "Actor:K2_GetActorLocation");
    CHECK(parsed->request.length == 1);
    // No block and no params: a zeroed block the console sizes; a raw address is marked for identification.
    parsed = Call(R"({"verb": "call", "args": {"object": "0x3000", "function": "GetName"}})");
    REQUIRE(parsed.has_value());
    CHECK(ue::GetRef(parsed->request).address == 0x3000);
    CHECK(ue::GetRef(parsed->request).index == -1);
    CHECK(parsed->request.length == 0);
    CHECK(ue::Check(parsed->request) == Status::Ok);
    CHECK(Why(R"({"verb": "call", "args": {"object": "0x1000#5:9", "function": "F", "block": "zz"}})").starts_with("block must be hex"));
}

TEST_CASE("search values: text and hex as strings, numbers by type, u64 also as hex text") {
    const auto text = [](const Command& command) {
        return std::string(reinterpret_cast<const char*>(command.request.data), command.request.length);
    };
    auto parsed = Call(R"({"verb": "search", "args": {"type": "f32", "value": 100}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.op == op::Search);
    CHECK(parsed->request.length == 4);
    float f = 0;
    memcpy(&f, parsed->request.data, 4);
    CHECK(f == 100.0f);
    CHECK(SearchAlignment(parsed->request.flags) == 4);
    CHECK(parsed->request.args[1] == 64);
    CHECK((parsed->request.flags & SearchJobStart) != 0);
    CHECK(parsed->seconds == 10);
    CHECK(Check(parsed->request) == Status::Ok);

    parsed = Call(R"({"verb": "search", "args": {"type": "ptr", "value": "0x7ff7bb2a7740", "max": 8, "scope": {"start": "0x10000", "to": "0x20000", "writable": true}}})");
    REQUIRE(parsed.has_value());
    uint64_t pointer = 0;
    memcpy(&pointer, parsed->request.data, 8);
    CHECK(pointer == 0x7ff7bb2a7740);
    CHECK(SearchAlignment(parsed->request.flags) == 8);
    CHECK((parsed->request.flags & SearchWritable) != 0);
    CHECK(parsed->request.args[1] == 8);
    CHECK(parsed->request.address == 0x10000);
    CHECK(parsed->request.args[0] == 0x20000);
    parsed = Call(R"({"verb": "search", "args": {"type": "u64", "value": 4660}})");
    REQUIRE(parsed.has_value());
    memcpy(&pointer, parsed->request.data, 8);
    CHECK(pointer == 0x1234);

    parsed = Call(R"({"verb": "search", "args": {"type": "hex", "value": "de ad be ef", "align": 2}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.length == 4);
    CHECK(SearchAlignment(parsed->request.flags) == 2);
    CHECK(parsed->textSearch == 0);
    parsed = Call(R"({"verb": "search", "args": {"type": "i32", "value": -5}})");
    REQUIRE(parsed.has_value());
    int32_t negative = 0;
    memcpy(&negative, parsed->request.data, 4);
    CHECK(negative == -5);

    // Text exactly as given: spaces, quotes and backslashes kept.
    parsed = Call(R"({"verb": "search", "args": {"type": "text", "value": "two  spaces; and -- dashes", "max": 1, "scope": {"module": "lib.dll"}}})");
    REQUIRE(parsed.has_value());
    CHECK(text(*parsed) == "two  spaces; and -- dashes");
    CHECK(parsed->target == "lib.dll");
    CHECK(parsed->request.args[1] == 1);
    CHECK(parsed->textSearch == 1);       // Rows then carry the text around each hit.
    parsed = Call(R"({"verb": "search", "args": {"type": "text", "value": "say \"hi\" from C:\\dir\\x"}})");
    REQUIRE(parsed.has_value());
    CHECK(text(*parsed) == R"(say "hi" from C:\dir\x)");
    parsed = Call(R"({"verb": "search", "args": {"type": "wtext", "value": "ab c"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->textSearch == 2);
    CHECK(parsed->request.length == 8);
    CHECK(parsed->request.data[4] == ' ');
    CHECK(parsed->request.data[5] == 0);

    // The value's kind must match the type.
    const struct { std::string_view args, message; } bad[] = {
        {R"({"type": "f32", "value": "abc"})", "value: type f32 takes a number"},
        {R"({"type": "text", "value": 5})", "value: type text takes a string"},
        {R"({"type": "hex", "value": 5})", "value: type hex takes a string"},
        {R"({"type": "hex", "value": "zz"})", "value: hex pairs"},
        {R"({"type": "i32", "value": 1.5})", "value must be a 32-bit integer"},
        {R"({"type": "i32", "value": 3000000000})", "value must be a 32-bit integer"},
        {R"({"type": "u32", "value": -1})", "value must be a 32-bit unsigned"},
        {R"({"type": "u64", "value": "abc"})", "value must be a 64-bit"},
        {R"({"type": "u64", "value": -1})", "value must be a 64-bit"},
        {R"({"type": "text", "value": ""})", "value is empty"},
        {R"({"type": "hex", "value": "41", "wide": true})", "unknown key 'wide'"},
        {R"({"type": "str", "value": "abc"})", "type: "},
        {R"({"type": "u32", "value": [1]})", "value: "},
    };
    for (const auto& each : bad) {
        const Json call{{"verb", "search"}, {"args", Json::parse(each.args)}};
        CHECK_MESSAGE(Why(call).starts_with(each.message), each.args);
    }
    const Json tooLong{{"verb", "search"}, {"args", {{"type", "text"}, {"value", std::string(MaxSearchPattern + 1, 'a')}}}};
    CHECK(Why(tooLong).starts_with("value: text is at most"));
}

TEST_CASE("search options: ranges, filters, limits and its job") {
    auto parsed = Call(R"({"verb": "search", "args": {"type": "ptr", "value": "0x1234", "wait": 30, "scope": {"module": "app.exe", "heap": true, "paged": true}}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->seconds == 30);
    CHECK((parsed->request.flags & SearchBackground) == 0);
    CHECK((parsed->request.flags & SearchPrivate) != 0);
    CHECK((parsed->request.flags & SearchPaged) != 0);
    CHECK(parsed->target == "app.exe");
    CHECK(Check(parsed->request) == Status::Ok);
    CHECK(Why(R"({"verb": "search", "args": {"type": "u32", "value": 1, "scope": {"module": "app.exe", "start": "0x1000"}}})").starts_with("scope: module replaces"));
    CHECK(Why(R"({"verb": "search", "args": {"type": "u32", "value": 1, "wait": 61}})").starts_with("wait is"));
    CHECK(Why(R"({"verb": "search", "args": {"type": "u32", "value": 1, "align": 3}})").starts_with("invalid search"));
    CHECK(Why(R"({"verb": "search", "args": {"type": "u32", "value": 1, "max": 0}})").starts_with("max is"));
    CHECK(Why(R"({"verb": "search", "args": {"type": "u32", "value": 1, "scope": {"start": "0x2000", "to": "0x1000"}}})").starts_with("invalid search"));
    CHECK(Why(R"({"verb": "search", "args": {"type": "u32"}})").starts_with("missing value"));
    parsed = Call(R"({"verb": "search-job", "args": {"job": 7, "stop": true}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.op == op::Search);
    CHECK(parsed->request.flags == SearchJobStop);
    CHECK(parsed->request.args[0] == 7);
    CHECK(Check(parsed->request) == Status::Ok);
    CHECK(Call(R"({"verb": "search-job", "args": {"job": 7}})")->request.flags == SearchJobStatus);
    CHECK(Why(R"({"verb": "search-job", "args": {"job": 0}})").starts_with("job is"));
}

// --- Builders ---------------------------------------------------------------------

TEST_CASE("read defaults to 64 bytes; len is 1..4096") {
    auto parsed = Call(R"({"verb": "read", "args": {"addr": "0x7ff0"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.address == 0x7ff0);
    CHECK(parsed->request.length == 64);
    parsed = Call(R"({"verb": "read", "args": {"addr": "0x1000", "len": "0x8"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.length == 8);
    CHECK(Why(R"({"verb": "read", "args": {"addr": "0x10", "len": 0}})").starts_with("len must be"));
    CHECK(Why(R"({"verb": "read", "args": {"addr": "0x10", "len": 4097}})").starts_with("len must be"));
    CHECK(Why(R"({"verb": "read", "args": {"addr": "0x0", "len": 4}})").starts_with("addr: "));
    CHECK(Why(R"({"verb": "read", "args": {"addr": "4096"}})").starts_with("addr: '4096' is not an address"));
}

TEST_CASE("read's type decodes elements, text or a layout whose size is the length") {
    auto parsed = Call(R"({"verb": "read", "args": {"addr": "0x1000", "len": 16, "type": "f32"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->view == "f32");
    CHECK(parsed->request.length == 16);
    parsed = Call(R"({"verb": "read", "args": {"addr": "0x1000", "type": "ptr"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.length == 64);
    CHECK(Call(R"({"verb": "read", "args": {"addr": "0x1000", "type": "cstr"}})")->request.length == DefaultStringRead);
    CHECK(Why(R"({"verb": "read", "args": {"addr": "0x1000", "len": 6, "type": "f32"}})").starts_with("len must be a multiple"));
    CHECK(Why(R"({"verb": "read", "args": {"addr": "0x1000", "len": 8, "type": "f16"}})").starts_with("type: layout: "));
    parsed = Call(R"({"verb": "read", "args": {"addr": "0x1000", "type": "u8 phase, pad 3, f32 duration, f32 elapsed, f64[3] start"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.length == 36);
    CHECK(parsed->view == "u8 phase, pad 3, f32 duration, f32 elapsed, f64[3] start");
    CHECK(Call(R"({"verb": "read", "args": {"addr": "0x1000", "len": 8, "type": "u32 a, u32 b"}})")->request.length == 8);
    CHECK(Why(R"({"verb": "read", "args": {"addr": "0x1000", "len": 16, "type": "u32 a, u32 b"}})").starts_with("len 16 differs"));
    CHECK(Why(R"({"verb": "read", "args": {"addr": "0x1000", "type": "u32 a, f16 b"}})").starts_with("type: layout: "));
    CHECK(Why(R"({"verb": "read", "args": {"addr": "0x1000", "type": "u32 a,"}})").starts_with("type: layout: "));
    CHECK(Why(R"({"verb": "read", "args": {"addr": "0x1000", "type": " "}})").starts_with("type is"));
}

TEST_CASE("peek reads up to 64 qwords and labels exactly one") {
    auto parsed = Call(R"({"verb": "peek", "args": {"addr": {"module": "base", "rva": "0x100"}}})", Loaded);
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Peek);
    CHECK(parsed->request.op == op::Read);
    CHECK(parsed->request.address == 0x7ff600000100);
    CHECK(parsed->limit == 8);
    CHECK(parsed->request.length == 64);
    CHECK(Call(R"({"verb": "peek", "args": {"addr": "0x1000#5:9", "count": 64}})")->request.length == 512);
    CHECK(Why(R"({"verb": "peek", "args": {"addr": "0x1000", "count": 65}})").starts_with("count must be"));
    CHECK(Why(R"({"verb": "peek", "args": {"addr": "0x1000", "count": 0}})").starts_with("count must be"));
    parsed = Call(R"({"verb": "peek", "args": {"addr": "0x1000", "label": true, "file": "labels.jsonl"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->label);
    CHECK(parsed->limit == 1);
    CHECK(parsed->file == "labels.jsonl");
    CHECK(parsed->target.empty());
    parsed = Call(R"({"verb": "peek", "args": {"addr": "0x1000", "count": 1, "label": true, "name": "TagGlobal"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->target == "TagGlobal");
    CHECK(parsed->file.empty());                                    // The console's labels file.
    CHECK(Why(R"({"verb": "peek", "args": {"addr": "0x1000", "count": 2, "label": true}})").starts_with("count: label takes one"));
    CHECK(Why(R"({"verb": "peek", "args": {"addr": "0x1000", "name": "TagGlobal"}})").starts_with("label: ")); // A name labels nothing by itself.
    CHECK(Why(R"({"verb": "peek", "args": {"addr": "0xfffffffffffffff8", "count": 2}})").starts_with("addr: "));
}

TEST_CASE("write takes hex bytes, bounded by the agent's write limit; expect guards it") {
    auto parsed = Call(R"({"verb": "write", "args": {"addr": "0x10", "data": "00 00 80 3f"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.op == op::Write);
    CHECK(parsed->request.length == 4);
    CHECK(parsed->request.data[2] == 0x80);
    CHECK(parsed->request.data[3] == 0x3f);
    CHECK(parsed->request.flags == 0);
    CHECK(Call(R"({"verb": "write", "args": {"addr": "0x10", "data": "0080"}})")->request.length == 2);
    for (const char* data : {"0", "xy", ""}) {
        const Json call{{"verb", "write"}, {"args", {{"addr", "0x10"}, {"data", data}}}};
        CHECK_MESSAGE(Why(call).starts_with("data must be hex pairs"), data);
    }
    CHECK(Why(R"({"verb": "write", "args": {"addr": "0x10"}})").starts_with("data: write needs addr and data"));
    const Json tooLong{{"verb", "write"}, {"args", {{"addr", "0x10"}, {"data", std::string((MaxWrite + 1) * 2, '0')}}}};
    CHECK(Why(tooLong).starts_with("data must be hex pairs"));

    parsed = Call(R"({"verb": "write", "args": {"addr": "0x1000", "data": "01 02", "expect": "03 04"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.flags == WriteExpect);
    CHECK(parsed->request.length == 2);
    CHECK(parsed->request.data[0] == 1);
    CHECK(parsed->request.data[2] == 3);
    CHECK(parsed->request.data[3] == 4);
    CHECK(Check(parsed->request) == Status::Ok);
    CHECK(Why(R"({"verb": "write", "args": {"addr": "0x1000", "data": "01 02", "expect": "03"}})").starts_with("expect needs"));
}

TEST_CASE("write undo needs an owner ref and expected bytes") {
    auto parsed = Call(R"({"verb": "write", "args": {"addr": {"base": "0x1000#5:9", "offset": "0x10"}, "data": "2a 00 00 00", "expect": "07 00 00 00", "undo": {"after": 5, "owner": "0x1000#5:9"}}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Trial);
    CHECK(parsed->request.op == op::Trial);
    CHECK(parsed->request.flags == static_cast<uint32_t>(TrialAction::Add));
    CHECK(parsed->request.address == 0x1010);
    CHECK(parsed->request.length == 4);
    CHECK(parsed->request.data[MaxTrialBytes] == 7);
    CHECK(parsed->owner == One);
    CHECK(parsed->object == 0);
    CHECK(parsed->milliseconds == 5000);
    parsed = Call(R"({"verb": "write", "args": {"addr": "0x5008", "data": "01", "expect": "00", "undo": {"after": 0.5, "owner": "0x1000#5:9", "object": "0x5000"}}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->object == 0x5000);
    CHECK(parsed->milliseconds == 500);
    const struct { std::string_view args, message; } bad[] = {
        {R"({"addr": "0x1000", "data": "01", "undo": {"after": 5}, "expect": "00"})", "missing undo.owner"},
        {R"({"addr": "0x1000", "data": "01", "undo": {"after": 5, "owner": "0x1000#5:9"}})", "expect: undo needs"},
        {R"({"addr": "0x1000", "data": "01", "undo": {"after": 5, "owner": "0x1000"}, "expect": "00"})", "owner is a literal ref"},
        {R"({"addr": "0x1000", "data": "01", "undo": {"after": 61, "owner": "0x1000#5:9"}, "expect": "00"})", "after: a timed write lasts at most 60 s"},
        {R"({"addr": "0x1000", "data": "01", "undo": 5, "expect": "00"})", "undo: an object {after, owner, object?}"},
        {R"({"addr": "0x1000", "data": "01", "owner": "0x1000#5:9", "expect": "00"})", "'owner' goes inside undo"},
        {R"({"addr": "0x1000", "data": "01", "undo": {"after": 5, "owner": "0x1000#5:9"}, "hold": 5, "expect": "00"})", "hold: undo and hold"},
    };
    for (const auto& each : bad) {
        const Json call{{"verb", "write"}, {"args", Json::parse(each.args)}};
        CHECK_MESSAGE(Why(call).starts_with(each.message), each.args);
    }
}

TEST_CASE("write hold rewrites for seconds and guards a ref-based addr on its object") {
    auto parsed = Call(R"({"verb": "write", "args": {"addr": {"base": "0x7000#12:3", "offset": "0x3e4"}, "data": "00 00 80 3f", "hold": 5, "expect": "00 00 00 00"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Pin);
    CHECK(parsed->request.flags == static_cast<uint32_t>(PinAction::Add));
    CHECK(parsed->request.address == 0x7000 + 0x3e4);
    CHECK(parsed->request.length == 4);
    CHECK(parsed->milliseconds == 5000);
    CHECK(parsed->request.args[1] == 1);
    CHECK(parsed->guardObjects == std::vector<uint64_t>{0x7000});
    parsed = Call(R"({"verb": "write", "args": {"addr": "0x5000", "data": "01", "hold": 10}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->guardObjects == std::vector<uint64_t>{0});
    CHECK(parsed->milliseconds == 10000);
    CHECK(Why(R"({"verb": "write", "args": {"addr": "0x5000", "data": "01", "hold": 61}})").starts_with("hold: a timed write"));
    CHECK(Why(R"({"verb": "write", "args": {"addr": "0x5000", "data": "01 02", "hold": 1, "expect": "01"}})").starts_with("expect needs"));
    CHECK(Why(R"({"verb": "write", "args": {"addr": "0x5000", "data": "01", "hold": true}})").starts_with("hold: seconds"));
}

TEST_CASE("timed-writes lists the timed writes; write with clear ends one id or all") {
    auto parsed = Call(R"({"verb": "timed-writes"})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::TimedWrites);
    CHECK_FALSE(parsed->all);
    CHECK_FALSE(FindVerb(AllVerbs(), "timed-writes")->writes);  // Listing is read-only; clearing is write's.
    parsed = Call(R"({"verb": "write", "args": {"clear": "all"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::TimedWrites);
    CHECK(parsed->all);
    CHECK(parsed->request.args[0] == 0);
    parsed = Call(R"({"verb": "write", "args": {"clear": "undo:3"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->target == "undo");
    CHECK(parsed->request.args[0] == 3);
    CHECK(Call(R"({"verb": "write", "args": {"clear": "hold:2"}})")->target == "hold");
    CHECK(Why(R"({"verb": "write", "args": {"clear": "3"}})").starts_with("clear is \"all\""));
    CHECK(Why(R"({"verb": "write", "args": {"clear": "pin:3"}})").starts_with("clear is \"all\""));
    CHECK(Why(R"({"verb": "write", "args": {"clear": "all", "addr": "0x10"}})").starts_with("addr: clear ends"));
    CHECK(Why(R"({"verb": "write", "args": {"data": "01"}})").starts_with("addr: write needs"));
    CHECK(Why(R"({"verb": "write", "args": {"addr": "0x10"}})").starts_with("data: write needs"));
}

TEST_CASE("modules asks the agent") {
    const auto parsed = Call(R"({"verb": "modules"})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.op == op::Modules);
    CHECK(Check(parsed->request) == Status::Ok);
}

TEST_CASE("local verbs build no request") {
    CHECK(Call(R"({"verb": "help"})")->kind == Kind::Help);
    CHECK(Call(R"({"verb": "help", "args": {"verb": "read"}})")->target == "read");
    CHECK(Call(R"({"verb": "help", "args": {"hash": true}})")->hashOnly);
    CHECK(Why(R"({"verb": "help", "args": {"hash": true, "json": true}})").starts_with("hash "));
    CHECK(Why(R"({"verb": "help", "args": {"verb": "read", "hash": true}})").starts_with("hash "));
    CHECK(Why(R"({"verb": "help", "args": {"verb": "read", "json": true}})").starts_with("json "));
    CHECK(Call(R"({"verb": "status"})")->kind == Kind::Status);
    CHECK(Call(R"({"verb": "quit"})")->kind == Kind::Quit);
    CHECK(Call(R"({"verb": "usage"})")->kind == Kind::Usage);
    CHECK_FALSE(Call(R"({"verb": "usage"})")->all);
    CHECK(Call(R"({"verb": "usage", "args": {"report": "all"}})")->all);
    CHECK(Call(R"({"verb": "usage", "args": {"report": "sessions"}})")->listSessions);
    CHECK(Call(R"({"verb": "usage", "args": {"session": "mcp-1"}})")->target == "mcp-1");
    CHECK(Why(R"({"verb": "usage", "args": {"report": "every"}})").starts_with("report: "));
    CHECK(Why(R"({"verb": "usage", "args": {"report": "sessions", "session": "mcp-1"}})").starts_with("report: one session"));
    const auto parts = Call(R"({"verb": "usage", "args": {"only": ["totals", "advice"]}})");
    REQUIRE(parts.has_value());
    CHECK(parts->parts == std::vector<std::string>{"totals", "advice"});
    CHECK(parts->advice);                                   // Asking for the part asks for advice.
    CHECK(Why(R"({"verb": "usage", "args": {"only": ["compare"]}})").starts_with("only: compare needs baseline"));
    CHECK(Why(R"({"verb": "usage", "args": {"only": ["everything"]}})").starts_with("only: "));
    CHECK(Why(R"({"verb": "usage", "args": {"session": 1}})").starts_with("session: "));
}

TEST_CASE("batch keeps its calls as JSON and refuses what cannot be held") {
    auto parsed = Call(R"({"verb": "batch", "args": {"calls": [{"verb": "find", "args": {"text": "Pawn"}}, {"verb": "read", "args": {"addr": "0x10", "len": 4}}, {"verb": "modules"}]}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Batch);
    REQUIRE(parsed->fields.size() == 3);
    CHECK(Json::parse(parsed->fields[1]) == Json::parse(R"({"verb": "read", "args": {"addr": "0x10", "len": 4}})"));
    CHECK(Json::parse(parsed->fields[2]) == Json::parse(R"({"verb": "modules"})"));
    for (const char* inner : {"batch", "quit", "status", "help", "usage"}) {
        const Json call{{"verb", "batch"}, {"args", {{"calls", Json::array({Json{{"verb", inner}}})}}}};
        CHECK_MESSAGE(Why(call).starts_with("calls: not allowed in a batch"), inner);
    }
    CHECK(Why(R"({"verb": "batch", "args": {"calls": [{"args": {}}]}})").starts_with("calls: "));
    CHECK(Why(R"({"verb": "batch", "args": {"calls": [{"verb": "read", "args": [1]}]}})").starts_with("calls: "));
    Json many{{"verb", "batch"}, {"args", {{"calls", Json::array()}}}};
    for (size_t i = 0; i <= MaxBatchCommands; ++i) many["args"]["calls"].push_back(Json{{"verb", "modules"}});
    CHECK(Why(many).starts_with("calls: at most"));
}

TEST_CASE("watch-start ranges: an address with a length or a type, a field, or offsets from a root") {
    auto parsed = Call(R"({"verb": "watch-start", "args": {"duration": 2, "file": "aim.jsonl", "ranges": [
        {"addr": {"base": "0x7000#12:3", "offset": "0x3e4"}, "len": 4},
        {"addr": "0x7000#12:3", "len": 8},
        {"addr": "0x9000", "len": 16}]}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Watch);
    CHECK(parsed->target == "start");
    CHECK(parsed->milliseconds == 2000);
    REQUIRE(parsed->reads.size() == 3);
    CHECK(parsed->reads[0].address == 0x7000 + 0x3e4);
    CHECK(parsed->reads[1].address == 0x7000);
    CHECK(parsed->reads[1].length == 8);
    CHECK(parsed->guardObjects == std::vector<uint64_t>{0x7000, 0x7000, 0}); // A ref-based addr is guarded on its object.
    CHECK(parsed->file == "aim.jsonl");

    // A typed range: its length from the type and count.
    parsed = Call(R"({"verb": "watch-start", "args": {"duration": 20, "every": 1, "ranges": [
        {"addr": "0x9000", "type": "f64"},
        {"addr": {"base": "0x1000#5:2", "offset": "0x40"}, "type": "i32", "count": 2},
        {"addr": "0x9100", "len": 16}]}})");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->reads.size() == 3);
    CHECK(parsed->reads[0].length == 8);
    CHECK(parsed->reads[1].address == 0x1040);
    CHECK(parsed->reads[1].length == 8);
    CHECK(parsed->watchTypes == std::vector<uint32_t>{WatchTypeCode("f64"), WatchTypeCode("i32"), 0});
    CHECK(WatchTypeName(parsed->watchTypes[1]) == "i32");

    // A reflected field: its place and type come from the pack before the start.
    parsed = Call(R"({"verb": "watch-start", "args": {"duration": 30, "ranges": [
        {"object": "0x1000#5:2", "field": ["Health"]},
        {"object": "0x1000#5:2", "field": ["Root", "RelativeLocation"]},
        {"addr": "0x9000", "type": "f64"}]}})");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->reads.size() == 3);
    REQUIRE(parsed->watchFields.size() == 2);
    CHECK(parsed->watchFields[0].range == 0);
    CHECK(parsed->watchFields[0].object == Ref{0x1000, 5, 2});
    CHECK(parsed->watchFields[0].path == "Health");
    CHECK(parsed->watchFields[1].range == 1);
    CHECK(parsed->watchFields[1].path == "Root.RelativeLocation");
    CHECK(parsed->reads[2].address == 0x9000);

    // Following a root, ranges are offsets from it.
    parsed = Call(R"({"verb": "watch-start", "args": {"duration": 120, "every": 0.5, "follow": "pawn", "ranges": [
        {"offset": "0x538", "len": 16}, {"offset": "0x3e4", "type": "f32"}]}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->root == "pawn");
    CHECK(parsed->interval == 500);
    CHECK(parsed->reads[0].address == 0x538);
    CHECK(parsed->reads[1].address == 0x3e4);
    CHECK(parsed->watchTypes[1] == WatchTypeCode("f32"));
    CHECK(parsed->guardObjects[1] == 0);

    const struct { std::string_view range, follow, message; } bad[] = {
        {R"({"addr": "0x9000"})", "", "ranges: each is"},
        {R"({"addr": "0x9000", "len": 513})", "", "ranges: each is"},
        {R"({"addr": "0x9000", "len": 0})", "", "ranges: each is"},
        {R"({"addr": "0x9000", "type": "f16"})", "", "ranges: each is"},
        {R"({"addr": "0x9000", "type": "cstr"})", "", "ranges: each is"},
        {R"({"addr": "0x9000", "type": "i32", "count": 0})", "", "ranges: each is"},
        {R"({"addr": "0x9000", "len": 4, "count": 2})", "", "ranges: each is"},
        {R"({"addr": "0x9000", "len": 4, "extra": 1})", "", "ranges: each is"},
        {R"({"addr": "0x0", "len": 4})", "", "ranges: "},
        {R"({"object": "0x1000", "field": ["Health"]})", "", "ranges: each is"},
        {R"({"object": "0x1000#5:2", "field": ["A.B"]})", "", "ranges: a path step"},
        {R"({"addr": "0x9000", "len": 4})", "pawn", "ranges: each is"},
        {R"({"offset": "0x10000", "len": 4})", "pawn", "ranges: an offset from the root"},
        {R"({"object": "0x1000#5:2", "field": ["Health"]})", "pawn", "ranges: a field range does not follow"},
    };
    for (const auto& each : bad) {
        Json call{{"verb", "watch-start"}, {"args", {{"duration", 10}, {"ranges", Json::array({Json::parse(each.range)})}}}};
        if (!each.follow.empty()) call["args"]["follow"] = std::string(each.follow);
        CHECK_MESSAGE(Why(call).starts_with(each.message), each.range);
    }
    Json many{{"verb", "watch-start"}, {"args", {{"duration", 10}, {"ranges", Json::array()}}}};
    for (size_t i = 0; i <= MaxWatchRanges; ++i) many["args"]["ranges"].push_back(Json{{"addr", "0x9000"}, {"len", 4}});
    CHECK(Why(many).starts_with("ranges: at most"));
}

TEST_CASE("watch-start durations are seconds; watch-read and watch-stop take all") {
    auto parsed = Call(R"({"verb": "watch-start", "args": {"duration": 600, "every": 10, "ranges": [{"addr": "0x9000", "len": 8}]}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->milliseconds == 600000);
    CHECK(parsed->interval == 10000);
    parsed = Call(R"({"verb": "watch-start", "args": {"duration": 0.25, "ranges": [{"addr": "0x9000", "len": 4}]}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->milliseconds == 250);
    CHECK(parsed->interval == 0);
    CHECK(parsed->file.empty());
    const struct { std::string_view args, message; } bad[] = {
        {R"({"duration": 31})", "duration: a watch sampling every tick"},       // Every tick: 30 s at most.
        {R"({"duration": 3601, "every": 1})", "duration: a watch with every"},  // Past the capture limit.
        {R"({"duration": 10, "every": 20})", "every takes"},                    // Interval past the duration.
        {R"({"duration": "5s"})", "duration: "},
        {R"({"duration": 5, "follow": ""})", "follow: "},
    };
    for (const auto& each : bad) {
        Json call{{"verb", "watch-start"}, {"args", Json::parse(each.args)}};
        call["args"]["ranges"] = Json::array({Json{{"addr", "0x9000"}, {"len", 4}}});
        CHECK_MESSAGE(Why(call).starts_with(each.message), each.args);
    }
    CHECK(Why(R"({"verb": "watch-start", "args": {"duration": 2}})").starts_with("missing ranges"));
    CHECK(Why(R"({"verb": "watch-start", "args": {"duration": 2, "ranges": []}})").starts_with("ranges: "));
    parsed = Call(R"({"verb": "watch-read"})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->target == "read");
    CHECK_FALSE(parsed->all);
    CHECK(Call(R"({"verb": "watch-read", "args": {"all": true}})")->all);
    CHECK(Call(R"({"verb": "watch-stop"})")->target == "stop");
    CHECK(Call(R"({"verb": "watch-stop", "args": {"all": true}})")->all);
}

TEST_CASE("mem-snap takes an address and a length; mem-diff a test and an element type; vtable its options") {
    auto parsed = Call(R"({"verb": "mem-snap", "args": {"addr": "0x10000", "len": 4096}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::MemSnap);
    CHECK(parsed->range == 4096);
    CHECK(Why(R"({"verb": "mem-snap", "args": {"addr": "0x10000", "len": 0}})").starts_with("len must be"));
    CHECK(Why(R"({"verb": "mem-snap", "args": {"addr": "0x10000", "len": 2000000}})").starts_with("len must be"));
    parsed = Call(R"({"verb": "mem-diff", "args": {"compare": "decreased", "type": "f32", "keep": true, "view": {"limit": 10}}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::MemDiff);
    CHECK(parsed->target == "decreased");
    CHECK(parsed->view == "f32");
    CHECK(parsed->all);
    CHECK(parsed->limit == 10);
    CHECK(Why(R"({"verb": "mem-diff", "args": {"compare": "bigger", "type": "f32"}})").starts_with("compare: one of"));
    CHECK(Why(R"({"verb": "mem-diff", "args": {"compare": "changed", "type": "cstr"}})").starts_with("type is"));
    CHECK(Why(R"({"verb": "mem-diff", "args": {"compare": "changed", "type": "f32", "view": {"limit": 257}}})").starts_with("limit is"));
    const auto vtable = Call(R"({"verb": "vtable", "args": {"addr": "0x10000", "offset": "0x620", "rtti": true}})");
    REQUIRE(vtable.has_value());
    CHECK(vtable->kind == Kind::Vtable);
    CHECK(vtable->rtti);
    CHECK(vtable->offset == 0x620);
    CHECK(Why(R"({"verb": "vtable", "args": {"addr": "0x10000", "offset": 8, "parts": 64}})").starts_with("parts scans"));
}

TEST_CASE("the version text matches its parts") {
    const std::string expected = std::to_string(REAGENTLIVE_VERSION_MAJOR) + "." +
        std::to_string(REAGENTLIVE_VERSION_MINOR) + "." + std::to_string(REAGENTLIVE_VERSION_PATCH);
    CHECK(std::string(Version) == expected);
}
