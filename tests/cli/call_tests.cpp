// reagentlive CLI calls: the schema checks of a JSON call, the verb table's
// rows, the typed values and each verb's builder.
#include "reagentlive/frontends/cli/call/build.h"
#include "reagentlive/packs/ue/protocol_ue.h"
#include "reagentlive/version.h"
#include "doctest.h"
#include "verbs.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace reagentlive;
using namespace reagentlive::cli;
namespace {
const std::vector<Module> NoModules;
const std::vector<Module> Loaded{{"app.exe", 0x7ff600000000, 0x100000}, {"engine.dll", 0x7ffa00000000, 0x2000}};
// The literal refs the calls below use as "0x1000#5:9" and "0x2000#7:0".
const Ref One{0x1000, 5, 9}, Two{0x2000, 7, 0};

const Verb& Row(std::string_view name) {
    const Verb* verb = FindVerb(AllVerbs(), name);
    REQUIRE_MESSAGE(verb != nullptr, name);
    return *verb;
}

// A refused call's message; a call that builds fails the check.
std::string Why(const Json& call, std::span<const Module> modules = {}) {
    const auto parsed = BuildCall(AllVerbs(), call, modules);
    CHECK_MESSAGE(!parsed.has_value(), call.dump());
    return parsed ? std::string() : parsed.error();
}
std::string Why(const char* call, std::span<const Module> modules = {}) { return Why(Json::parse(call), modules); }

// Whether a call passes the schema checks and its builder.
bool Builds(const Json& call, std::span<const Module> modules = {}) {
    const auto built = BuildCall(AllVerbs(), call, modules);
    if (!built) MESSAGE(call.dump() << ": " << built.error());
    return built.has_value();
}

// The error object a console answers for a refused call: the schema's
// refusal, else the builder's, keyed by the key its message starts with.
Json Refused(std::string_view text) {
    const Json call = Json::parse(text);
    const Verb& verb = Row(call["verb"].get<std::string>());
    const Json given = call.value("args", Json());
    Refusal refusal;
    if (auto args = CheckCall(verb, given); !args) {
        refusal = args.error();
    } else {
        const auto built = Build(verb, *args);
        REQUIRE_FALSE(built.has_value());
        refusal = {built.error(), KeyOf(verb, built.error())};
    }
    return RefusalJson(verb, given, refusal);
}
}

// --- The schema checks (check.h) -------------------------------------------------

TEST_CASE("an unknown key is refused with the key it was likely meant as") {
    Json error = Refused(R"({"verb": "read", "args": {"addr": "0x1000", "lenn": 16}})");
    CHECK(error["kind"] == "schema");
    CHECK(error["key"] == "lenn");
    const auto message = error["message"].get<std::string>();
    CHECK(message.starts_with("unknown key 'lenn' (did you mean len?); read takes addr, len, type"));
    CHECK_FALSE(error.contains("arg"));                     // No row key: the whole row instead.
    REQUIRE(error["args"].is_array());
    CHECK(error["args"].size() == Row("read").args.size());
    CHECK(error["example"] == Json::parse(R"({"verb": "read", "args": {"addr": "0x1000", "len": 16}})"));
    CHECK(Builds(error["example"]));
    // Nothing near: no suggestion, and the example leaves it out.
    error = Refused(R"({"verb": "read", "args": {"addr": "0x1000", "zzzzzzzz": 1}})");
    CHECK_FALSE(error["message"].get<std::string>().contains("did you mean"));
    CHECK(error["example"] == Json::parse(R"({"verb": "read", "args": {"addr": "0x1000"}})"));
    // A misspelt key whose meant key is given too is dropped, not doubled.
    error = Refused(R"({"verb": "read", "args": {"addr": "0x1000", "len": 8, "lenn": 16}})");
    CHECK(error["example"]["args"]["len"] == 8);
    CHECK(Builds(error["example"]));
}

TEST_CASE("a missing required key is refused with its type; the example fills it") {
    const Json error = Refused(R"({"verb": "read", "args": {"len": 8}})");
    CHECK(error["kind"] == "schema");
    CHECK(error["key"] == "addr");
    CHECK(error["message"].get<std::string>().starts_with("missing addr: " + check::Takes(*FindArg(Row("read"), "addr"))));
    CHECK(error["arg"]["key"] == "addr");
    CHECK(error["arg"]["type"] == "chain");
    CHECK(error["arg"]["required"] == true);
    CHECK_FALSE(error.contains("args"));
    CHECK(error["example"]["args"]["addr"] == check::Example("chain"));
    CHECK(error["example"]["args"]["len"] == 8);
    CHECK(Builds(error["example"]));
    // No args at all is the same as {}.
    CHECK(Why(R"({"verb": "read"})").starts_with("missing addr: "));
    CHECK(Builds(Json::parse(R"({"verb": "modules"})")));
}

TEST_CASE("a value of the wrong type is refused with what its type takes") {
    // Each: the call, the key at fault, the start of the message.
    const struct { std::string_view call, key, message; } cases[] = {
        {R"({"verb": "read", "args": {"addr": "0x1000", "len": "16"}})", "len", R"(len: a whole number, or "0x..." text, not "16")"},
        {R"({"verb": "read", "args": {"addr": "0x1000", "len": -1}})", "len", "len: a whole number"},
        {R"({"verb": "peek", "args": {"addr": "0x1000", "label": "yes"}})", "label", "label: true or false, not"},
        {R"({"verb": "write", "args": {"addr": 4096, "data": "01"}})", "addr", "addr: an address: "},
        {R"({"verb": "get", "args": {"object": "0x1000#5:9", "paths": ["Health"]}})", "paths", "paths: a list, each a field path"},
        {R"({"verb": "each", "args": {"cls": "Pawn", "paths": [["Health"]], "where": {"field": "Name", "contains": "x"}}})",
            "where", "where: {"},
        {R"({"verb": "search", "args": {"type": "f16", "value": "41 42"}})", "type", "type: one of hex|u32|i32|u64|ptr|f32|f64|text"},
        {R"({"verb": "name", "args": {"indices": 5}})", "indices", "indices: a list, each a whole number"},
        {R"({"verb": "watch-start", "args": {"duration": 0, "ranges": [{"addr": "0x1000", "len": 4}]}})", "duration",
            "duration: seconds, a number above 0"},
    };
    for (const auto& each : cases) {
        const Json error = Refused(each.call);
        CHECK_MESSAGE(error["key"].get<std::string>() == each.key, each.call);
        CHECK_MESSAGE(error["message"].get<std::string>().starts_with(each.message), error["message"]);
        CHECK(error["arg"]["key"].get<std::string>() == each.key);
        CHECK_MESSAGE(Builds(error["example"]), each.call);
    }
    // An optional key at fault is dropped; a required one gets an example of its type.
    CHECK(Refused(cases[0].call)["example"] == Json::parse(R"({"verb": "read", "args": {"addr": "0x1000"}})"));
    CHECK(Refused(cases[3].call)["example"]["args"] == Json::parse(R"({"addr": "0x140001000", "data": "01"})"));
    const Json enumError = Refused(cases[6].call);
    CHECK(enumError["arg"]["type"] == "str");
    CHECK(enumError["arg"]["enum"][0] == "hex");
    CHECK(enumError["example"]["args"] == Json::parse(R"({"type": "hex", "value": "41 42"})"));
    CHECK(Refused(cases[7].call)["example"]["args"]["indices"] == Json::array({16}));
}

TEST_CASE("a builder's refusal names the key its message starts with") {
    // Optional key: the example drops it.
    Json error = Refused(R"({"verb": "read", "args": {"addr": "0x1000", "len": 0}})");
    CHECK(error["kind"] == "schema");
    CHECK(error["message"].get<std::string>().starts_with("len must be 1.."));
    CHECK(error["key"] == "len");
    CHECK(error["arg"]["key"] == "len");
    CHECK(error["example"] == Json::parse(R"({"verb": "read", "args": {"addr": "0x1000"}})"));
    CHECK(Builds(error["example"]));
    // Required key: the example replaces it.
    error = Refused(R"({"verb": "mem-snap", "args": {"addr": "0x1000", "len": 0}})");
    CHECK(error["key"] == "len");
    CHECK(error["example"]["args"]["len"] == check::Example("int"));
    CHECK(Builds(error["example"]));
    // A message about no one key: the whole row.
    error = Refused(R"({"verb": "search", "args": {"type": "u32", "value": 1, "align": 3}})");
    CHECK(error["message"].get<std::string>().starts_with("invalid search"));
    CHECK_FALSE(error.contains("key"));
    CHECK(error["args"].is_array());
    // KeyOf wants the whole key, and the longest that fits.
    const Verb& read = Row("read");
    CHECK(KeyOf(read, "len must be 1..4096") == "len");
    CHECK(KeyOf(read, "type: layout: bad") == "type");
    CHECK(KeyOf(read, "lenient") == "");
    CHECK(KeyOf(read, "") == "");
}

TEST_CASE("an unknown verb, a call that is no object and args that are no object are refused") {
    CHECK(Why(R"({"verb": "raed", "args": {}})").starts_with("unknown verb 'raed'"));
    const auto nearest = check::NearestVerbs(AllVerbs(), "raed");
    CHECK(std::ranges::find(nearest, "read") != nearest.end());
    CHECK(check::NearestVerbs(AllVerbs(), "watch").size() <= 3);
    CHECK(check::NearestVerbs(AllVerbs(), "zzzzzzzzzzzz").empty());
    CHECK(Why(R"("read")").starts_with("a call is "));
    CHECK(Why(R"({"args": {}})").starts_with("a call is "));
    CHECK(Why(R"({"verb": "read", "args": [1]})") == "args is an object of named arguments");
    const Json error = Refused(R"({"verb": "read", "args": [1]})");
    CHECK_FALSE(error.contains("key"));
    CHECK(error["args"].is_array());
    CHECK(Builds(error["example"]));
}

// --- The verb table ---------------------------------------------------------------

TEST_CASE("every row names its pack, known arg types and unique keys") {
    std::set<std::string_view> names;
    for (const auto& verb : AllVerbs()) {
        CHECK_MESSAGE(names.insert(verb.name).second, verb.name);
        CHECK_MESSAGE(verb.build != nullptr, verb.name);
        // A verb that writes or calls is in modify; the other packs hold none.
        if (verb.writes) CHECK_MESSAGE(verb.pack == ToolPack::Modify, verb.name);
        std::set<std::string_view> keys;
        for (const auto& param : verb.args) {
            CHECK_MESSAGE(arg::Known(param.type), verb.name << "." << param.key << ": " << param.type);
            CHECK_MESSAGE(keys.insert(param.key).second, verb.name << "." << param.key);
            CHECK_MESSAGE(!(param.required && !param.fallback.empty()), verb.name << "." << param.key);
        }
    }
}

TEST_CASE("every row's example call passes the checks and builds") {
    // Rows whose example (required keys only, from type examples) a builder
    // refuses: the type example is not a value this verb takes, or the verb
    // wants one of several optional keys. Each gets a minimal valid call.
    const std::map<std::string_view, std::string_view> minimal = {
        {"batch", R"({"calls": [{"verb": "modules", "args": {}}]})"},       // status is not allowed in a batch.
        {"search", R"({"type": "hex", "value": "00 00 80 3f"})"},           // hex takes a string, not 1.
        {"mem-diff", R"({"test": "changed", "type": "f32"})"},              // type is an element type.
        {"sig-check", R"({"pattern": "48 8b 05 89 ?? 90"})"},               // pattern or file.
        {"find", R"({"text": "Pawn"})"},                                    // text, cls or outer.
        {"call", R"({"object": "0x1000#5:9", "function": "Name"})"},        // An object, not a class.
        {"call-pure", R"({"object": "0x1000#5:9", "function": "Name"})"},
        {"label", R"({"object": "Name", "offset": 16, "type": "f32", "name": "Name"})"}, // An element type.
        {"entries", R"({"object": "0x1000#5:9", "field": "Name"})"},
        {"rows", R"({"object": "0x1000#5:9"})"},
        {"get", R"({"object": "0x1000#5:9"})"},                             // A class by name needs types.
        {"describe", R"({"object": "0x1000#5:9"})"},                        // object, or index and serial.
        {"refs", R"({"name": "Name"})"},                                    // name or index.
    };
    for (const auto& verb : AllVerbs()) {
        const Json example = ExampleCall(verb);
        CHECK(example["verb"].get<std::string>() == verb.name);
        REQUIRE(example["args"].is_object());
        const auto args = CheckCall(verb, example["args"]);
        REQUIRE_MESSAGE(args.has_value(), example.dump());
        const auto built = Build(verb, *args);
        if (built) continue;
        // A refused example says which key to fix.
        CHECK_MESSAGE(!KeyOf(verb, built.error()).empty(), verb.name << ": " << built.error());
        const auto found = minimal.find(verb.name);
        REQUIRE_MESSAGE(found != minimal.end(), verb.name << "'s example does not build: " << built.error());
        CHECK(Builds(Json{{"verb", verb.name}, {"args", Json::parse(found->second)}}));
    }
}

TEST_CASE("ArgJson lists an arg's key, type, default, enum and help") {
    for (const auto& verb : AllVerbs())
        for (const auto& param : verb.args) {
            const Json json = ArgJson(param);
            CHECK(json["key"].get<std::string>() == param.key);
            CHECK(json["type"].get<std::string>() == (arg::Enum(param.type) ? std::string_view("str") : param.type));
            CHECK(json.contains("required") == param.required);
            CHECK(json.contains("list") == param.list);
            CHECK(json.contains("enum") == arg::Enum(param.type));
            CHECK(json.contains("help") == !param.help.empty());
            // A default is JSON text of the arg's own type.
            if (!param.fallback.empty()) {
                CHECK_MESSAGE(!Json::parse(param.fallback, nullptr, false).is_discarded(), verb.name << "." << param.key);
                CHECK_MESSAGE(check::Fits(param, json["default"]), verb.name << "." << param.key);
            }
        }
    CHECK(ArgJson(*FindArg(Row("read"), "len"))["default"] == 64);
    CHECK(ArgJson(*FindArg(Row("touches"), "range"))["default"] == "0x80000");
    const Json type = ArgJson(*FindArg(Row("search"), "type"));
    CHECK(type["enum"] == Json::parse(R"(["hex", "u32", "i32", "u64", "ptr", "f32", "f64", "text"])"));
    CHECK(type["required"] == true);
    CHECK(ArgJson(*FindArg(Row("name"), "indices"))["list"] == true);
}

namespace {
// What writes the target's memory or calls into it, from a built call. A
// trace calls a line trace, which changes no state (its row's notes); a
// batch's items are checked as their own verbs when they run.
bool WritesOrCalls(const Command& command) {
    const auto& request = command.request;
    if (command.kind == Kind::Pin || command.kind == Kind::Trial || command.kind == Kind::Writes ||
        command.kind == Kind::Release || (command.kind == Kind::TypedCall && !(request.flags & ue::CallPure))) return true;
    if (request.op == reagentlive::op::Write || request.op == reagentlive::op::Pin || request.op == reagentlive::op::Trial)
        return true;
    if (request.op == ue::op::Call && !(request.flags & ue::CallPure)) return true;
    if (request.op == ue::op::CallsStart || request.op == ue::op::CallsStop) return true;
    return request.op == ue::op::GameThread && request.flags != static_cast<uint32_t>(ue::GameThreadAction::Status);
}
}

TEST_CASE("no form of a verb outside the modify pack writes or calls") {
    // Every form of every verb in explore, native and observe: a new verb or key lands here.
    const std::map<std::string_view, std::vector<std::string_view>> forms = {
        {"help", {R"({})", R"({"verb": "read"})", R"({"json": true})", R"({"hash": true})"}},
        {"status", {R"({})"}},
        {"quit", {R"({})"}},
        {"stats", {R"({})", R"({"all": true})", R"({"sessions": true})", R"({"session": "abc"})"}},
        {"batch", {R"({"calls": [{"verb": "read", "args": {"addr": "0x1000", "len": 8}}, {"verb": "modules"}]})"}},
        {"read", {R"({"addr": "0x1000"})", R"({"addr": "0x1000", "len": 16, "type": "f32"})",
            R"({"addr": {"base": "0x1000", "chain": [16, 8]}})"}},
        {"peek", {R"({"addr": "0x1000"})", R"({"addr": "0x1000", "count": 1, "label": true, "name": "Foo"})"}},
        {"modules", {R"({})"}},
        {"search", {R"({"type": "f32", "value": 1.5})", R"({"type": "text", "value": "abc", "wide": true, "wait": 0, "max": 8})"}},
        {"search-job", {R"({"job": 3})", R"({"job": 3, "stop": true})"}},
        {"mem-snap", {R"({"addr": "0x1000", "len": 64})"}},
        {"mem-diff", {R"({"test": "changed", "type": "f32"})", R"({"test": "increased", "type": "i32", "keep": true, "limit": 4})"}},
        {"watch-start", {R"({"duration": 5, "ranges": [{"addr": "0x1000", "len": 8}]})",
            R"({"duration": 5, "ranges": [{"addr": "0x1000", "type": "f32"}], "every": 0.1, "file": "w.jsonl"})"}},
        {"watch-read", {R"({})", R"({"all": true})"}},
        {"watch-stop", {R"({})", R"({"all": true})"}},
        {"sig-check", {R"({"pattern": "48 8b 05 89 ?? 90"})", R"({"file": "sigs.txt"})"}},
        {"sig-bytes", {R"({"addr": "0x1000"})", R"({"addr": "0x1000", "len": 32})"}},
        {"labels-sign", {R"({})", R"({"again": true})"}},
        {"labels-port", {R"({})"}},
        {"disasm", {R"({"addr": "0x1000"})", R"({"addr": "0x1000", "count": 8, "function": true})",
            R"({"addr": "0x1000", "function": true, "save": "f.json", "callees": true})"}},
        {"xrefs", {R"({"addr": "0x1000"})", R"({"addr": "0x1000", "all": true})"}},
        {"touches", {R"({"addr": "0x1000", "offsets": [16]})", R"({"addr": "0x1000", "offsets": ["0x10"], "reads": true})"}},
        {"vtable", {R"({"addr": "0x1000#5:9"})", R"({"addr": "0x1000#5:9", "count": 4, "resolve": true, "rtti": true})"}},
        {"find", {R"({"text": "Pawn"})", R"({"cls": "Pawn", "live": true, "limit": 5})"}},
        {"census", {R"({})", R"({"cls": "Pawn", "diff": true})"}},
        {"name-find", {R"({"text": "Health"})"}},
        {"name", {R"({"indices": [1, 2]})"}},
        {"each", {R"({"cls": "Pawn", "paths": [["Health"]]})", R"({"cls": "Pawn", "paths": [["Health"]], "seconds": 5})",
            R"({"cls": "Pawn", "paths": [["Health"]], "where": {"field": ["Name"], "contains": "a"}, "limit": 0})"}},
        {"call-pure", {R"({"object": "0x1000#5:9", "function": "Actor:K2_GetActorLocation"})",
            R"({"object": "0x1000#5:9", "function": "GetSocket", "params": {"SocketName": "#42"}})",
            R"({"object": "0x1000#5:9", "function": "Actor:K2_GetActorLocation", "dry": true})",
            R"({"object": "Pawn", "function": "GetHealth", "each": true, "seconds": 5})"}},
        {"label", {R"({"object": "0x1000#5:9", "offset": "0x10", "type": "f32", "name": "Speed"})"}},
        {"export-labels", {R"({})", R"({"file": "out.jsonl"})"}},
        {"export-assets", {R"({"dir": "out", "cls": ["DataTable"]})",
            R"({"dir": "out", "cls": ["DataTable", "CurveTable"], "match": "Weapon"})"}},
        {"layout-export", {R"({"file": "out.json", "classes": ["Pawn"]})", R"({"file": "out.json", "classes": ["Pawn"], "closure": 1})",
            R"({"file": "out.json", "classes": ["Pawn"], "closure": 2})"}},
        {"layout-check", {R"({"file": "out.json"})"}},
        {"entries", {R"({"object": "0x1000#5:9", "field": "Items"})", R"({"object": "0x1000#5:9", "field": "Items", "start": 8})"}},
        {"rows", {R"({"object": "0x1000#5:9"})", R"({"object": "0x1000#5:9", "match": "a", "fields": [["b"]]})"}},
        {"get", {R"({"object": "0x1000#5:9"})", R"({"object": "0x1000#5:9", "paths": [["Health"]]})",
            R"({"object": "Pawn", "types": true})", R"({"object": "0x1000#5:9", "snap": true})",
            R"({"object": "0x1000#5:9", "changed": true})"}},
        {"trace", {R"({"start": [0, 0, 0], "end": [1, 1, 1], "ignore": "0x1000#5:9"})"}},
        {"describe", {R"({"object": "0x1000#5:9"})", R"({"index": 5, "serial": 9})"}},
        {"func", {R"({"object": "0x1000#5:9", "function": "GetHealth"})"}},
        {"script", {R"({"object": "0x1000#5:9", "function": "Tick"})", R"({"object": "0x1000#5:9", "function": "Tick", "decode": true})",
            R"({"object": "Pawn", "function": "ExecuteUbergraph_Pawn", "decode": true, "entry": "0x1c"})"}},
        {"enum", {R"({"enum": "EMode"})"}},
        {"refs", {R"({"name": "Health"})", R"({"index": 42})"}},
        {"funcs", {R"({"object": "0x1000#5:9"})", R"({"object": "Pawn", "text": "Get", "start": 4})"}},
    };
    for (const auto& verb : AllVerbs()) {
        if (verb.pack == ToolPack::Modify) continue;
        const auto found = forms.find(verb.name);
        REQUIRE_MESSAGE(found != forms.end(), "no forms listed for " << verb.name);
        for (const auto& args : found->second) {
            const Json call{{"verb", verb.name}, {"args", Json::parse(args)}};
            const auto parsed = BuildCall(AllVerbs(), call);
            CHECK_MESSAGE(parsed.has_value(), call.dump() << ": " << (parsed ? std::string() : parsed.error()));
            if (parsed) CHECK_MESSAGE(!WritesOrCalls(*parsed), call.dump());
        }
    }
}

TEST_CASE("removed verbs are not in the table") {
    for (const char* gone : {"trial", "trial-list", "trial-clear", "pin", "pin-list", "pin-clear", "base", "region",
                             "read-batch", "fields", "dump", "snap", "diff", "params", "watch", "record", "census-diff", "rtti"})
        CHECK_MESSAGE(!FindVerb(AllVerbs(), gone), gone);
}

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
    const auto search = Call(R"({"verb": "search", "args": {"type": "u32", "value": 1, "start": {"module": "app.exe", "rva": "0x20"}}})",
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
    auto each = Call(R"({"verb": "each", "args": {"cls": "ItemDefinition", "paths": [["ItemId"]], "where": {"field": ["DisplayName"], "contains": "rifle"}, "limit": 5}})");
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

    parsed = Call(R"({"verb": "search", "args": {"type": "ptr", "value": "0x7ff7bb2a7740", "writable": true, "max": 8, "start": "0x10000", "to": "0x20000"}})");
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
    parsed = Call(R"({"verb": "search", "args": {"type": "text", "value": "two  spaces; and -- dashes", "module": "lib.dll", "max": 1}})");
    REQUIRE(parsed.has_value());
    CHECK(text(*parsed) == "two  spaces; and -- dashes");
    CHECK(parsed->target == "lib.dll");
    CHECK(parsed->request.args[1] == 1);
    CHECK(parsed->textSearch == 1);       // Rows then carry the text around each hit.
    parsed = Call(R"({"verb": "search", "args": {"type": "text", "value": "say \"hi\" from C:\\dir\\x"}})");
    REQUIRE(parsed.has_value());
    CHECK(text(*parsed) == R"(say "hi" from C:\dir\x)");
    parsed = Call(R"({"verb": "search", "args": {"type": "text", "value": "ab c", "wide": true}})");
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
        {R"({"type": "hex", "value": "41", "wide": true})", "wide is for type text"},
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
    auto parsed = Call(R"({"verb": "search", "args": {"type": "ptr", "value": "0x1234", "heap": true, "paged": true, "wait": 30, "module": "app.exe"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->seconds == 30);
    CHECK((parsed->request.flags & SearchBackground) == 0);
    CHECK((parsed->request.flags & SearchPrivate) != 0);
    CHECK((parsed->request.flags & SearchPaged) != 0);
    CHECK(parsed->target == "app.exe");
    CHECK(Check(parsed->request) == Status::Ok);
    CHECK(Why(R"({"verb": "search", "args": {"type": "u32", "value": 1, "module": "app.exe", "start": "0x1000"}})").starts_with("module replaces"));
    CHECK(Why(R"({"verb": "search", "args": {"type": "u32", "value": 1, "wait": 61}})").starts_with("wait is"));
    CHECK(Why(R"({"verb": "search", "args": {"type": "u32", "value": 1, "align": 3}})").starts_with("invalid search"));
    CHECK(Why(R"({"verb": "search", "args": {"type": "u32", "value": 1, "max": 0}})").starts_with("max is"));
    CHECK(Why(R"({"verb": "search", "args": {"type": "u32", "value": 1, "start": "0x2000", "to": "0x1000"}})").starts_with("invalid search"));
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
    CHECK(Why(R"({"verb": "write", "args": {"addr": "0x10"}})").starts_with("missing data"));
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
    auto parsed = Call(R"({"verb": "write", "args": {"addr": {"base": "0x1000#5:9", "offset": "0x10"}, "data": "2a 00 00 00", "undo": 5, "owner": "0x1000#5:9", "expect": "07 00 00 00"}})");
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
    parsed = Call(R"({"verb": "write", "args": {"addr": "0x5008", "data": "01", "undo": 0.5, "owner": "0x1000#5:9", "object": "0x5000", "expect": "00"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->object == 0x5000);
    CHECK(parsed->milliseconds == 500);
    const struct { std::string_view args, message; } bad[] = {
        {R"({"addr": "0x1000", "data": "01", "undo": 5, "expect": "00"})", "owner: undo needs"},
        {R"({"addr": "0x1000", "data": "01", "undo": 5, "owner": "0x1000#5:9"})", "expect: undo needs"},
        {R"({"addr": "0x1000", "data": "01", "undo": 5, "owner": "0x1000", "expect": "00"})", "owner is a literal ref"},
        {R"({"addr": "0x1000", "data": "01", "undo": 61, "owner": "0x1000#5:9", "expect": "00"})", "undo: a timed write lasts at most 60 s"},
        {R"({"addr": "0x1000", "data": "01", "owner": "0x1000#5:9", "expect": "00"})", "owner: owner and object go with undo"},
        {R"({"addr": "0x1000", "data": "01", "undo": 5, "hold": 5, "owner": "0x1000#5:9", "expect": "00"})", "hold: undo and hold"},
        {R"({"addr": "0x1000", "data": "01", "hold": 5, "owner": "0x1000#5:9"})", "owner: owner and object go with undo"},
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

TEST_CASE("writes lists the timed writes; clear ends one id or all") {
    auto parsed = Call(R"({"verb": "writes"})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Writes);
    CHECK_FALSE(parsed->all);
    parsed = Call(R"({"verb": "writes", "args": {"clear": true}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->all);
    CHECK(parsed->request.args[0] == 0);
    parsed = Call(R"({"verb": "writes", "args": {"clear": true, "id": "undo:3"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->target == "undo");
    CHECK(parsed->request.args[0] == 3);
    CHECK(Call(R"({"verb": "writes", "args": {"clear": true, "id": "hold:2"}})")->target == "hold");
    CHECK(Why(R"({"verb": "writes", "args": {"clear": true, "id": "3"}})").starts_with("id is undo:N"));
    CHECK(Why(R"({"verb": "writes", "args": {"clear": true, "id": "pin:3"}})").starts_with("id is undo:N"));
    CHECK(Why(R"({"verb": "writes", "args": {"id": "undo:3"}})").starts_with("id goes with clear"));
}

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

TEST_CASE("each reads fields only; call-pure with each calls a function on every instance") {
    // Owner:Function is a function: each never calls.
    CHECK(Why(R"({"verb": "each", "args": {"cls": "Character", "paths": [["Character:CanJump"]]}})")
              .starts_with("paths: Character:CanJump is a function: call-pure with each"));
    auto parsed = Call(R"({"verb": "call-pure", "args": {"object": "Character", "function": "Character:CanJump", "each": true, "seconds": 10, "limit": 5}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Each);
    CHECK(parsed->eachCall);
    CHECK(parsed->className == "Character");
    CHECK(parsed->fields == std::vector<std::string>{"Character:CanJump"});
    CHECK(parsed->seconds == 10);
    CHECK(parsed->limit == 5);
    CHECK(Why(R"({"verb": "call-pure", "args": {"object": "Character", "function": "CanJump", "each": true, "block": "00"}})")
              .starts_with("params: "));                                        // No inputs.
    CHECK(Why(R"({"verb": "call-pure", "args": {"object": "Character", "function": "CanJump", "each": true, "params": {}}})")
              .starts_with("params: "));
    CHECK(Why(R"({"verb": "call-pure", "args": {"object": "Character", "function": "CanJump", "each": true, "dry": true}})")
              .starts_with("dry: "));
    CHECK(Why(R"({"verb": "call-pure", "args": {"object": "0x1000#5:9", "function": "CanJump", "each": true}})")
              .starts_with("object is a class name"));                          // A class, not an object.
    CHECK(Why(R"({"verb": "call-pure", "args": {"object": "0x1000#5:9", "function": "Actor:K2_GetActorLocation", "seconds": 2}})")
              .starts_with("seconds needs each"));
    // game-thread acts; status reads its state.
    CHECK(Call(R"({"verb": "game-thread", "args": {"action": "on"}})")->request.flags ==
          static_cast<uint32_t>(ue::GameThreadAction::On));
    CHECK(Call(R"({"verb": "game-thread", "args": {"action": "off"}})")->request.flags ==
          static_cast<uint32_t>(ue::GameThreadAction::Off));
    CHECK(Why(R"({"verb": "game-thread", "args": {"action": "status"}})").starts_with("action: one of on|off"));
    CHECK(Why(R"({"verb": "game-thread"})").starts_with("missing action"));
}

TEST_CASE("call-pure is a call with the pure flag; dry marks either form") {
    auto parsed = Call(R"({"verb": "call-pure", "args": {"object": "0x1000#5:9", "function": "Actor:K2_GetActorLocation"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.op == ue::op::Call);
    CHECK(parsed->request.flags == ue::CallPure);
    CHECK_FALSE(parsed->dry);
    CHECK(ue::Check(parsed->request) == Status::Ok);
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
    auto rows = Call(R"({"verb": "call", "args": {"object": "0x1000#5:9", "function": "Vendor:GetEntries", "fields": [["ItemTag"], ["Variations", "Price"]], "start": 100, "limit": 0}})");
    REQUIRE(rows.has_value());
    CHECK(rows->kind == Kind::TypedCall); // Implied: no params needed.
    CHECK(rows->rowPaths == std::vector<std::string>{"ItemTag", "Variations.Price"});
    CHECK(rows->from == 100);
    CHECK(rows->limit == 0);
    CHECK(Call(R"({"verb": "call", "args": {"object": "0x1000#5:9", "function": "Vendor:GetEntries", "params": {"Count": 2}, "limit": 5}})")
              ->fields == std::vector<std::string>{"Count=2"});
    CHECK(Why(R"({"verb": "call", "args": {"object": "0x1000#5:9", "function": "Vendor:GetEntries", "block": "00", "limit": 5}})")
              .starts_with("block: "));                                          // A raw block has no rows.
    CHECK(Why(R"({"verb": "call", "args": {"object": "0x1000#5:9", "function": "Vendor:GetEntries", "fields": [["a.b"]]}})")
              .starts_with("fields: "));
    CHECK(Why(R"({"verb": "call-pure", "args": {"object": "0x1000#5:9", "function": "Vendor:GetEntries", "fields": [["a"]]}})")
              .starts_with("unknown key 'fields'"));                              // Arrays own memory: call only.
}

TEST_CASE("nonzero: get, each, rows and a typed call; objects by Default__ name") {
    const auto call = Call(R"({"verb": "call", "args": {"object": "0x1000#5:9", "function": "Vendor:GetEntries", "nonzero": true}})");
    REQUIRE(call.has_value());
    CHECK(call->kind == Kind::TypedCall); // Implied, as fields is.
    CHECK(call->nonzero);
    CHECK(Why(R"({"verb": "call", "args": {"object": "0x1000#5:9", "function": "F", "block": "00", "nonzero": true}})").starts_with("block: "));
    CHECK(Call(R"({"verb": "get", "args": {"object": "0x1000#5:9", "paths": [["Health"], ["Stats"]], "nonzero": true}})")->nonzero);
    CHECK_FALSE(Call(R"({"verb": "get", "args": {"object": "0x1000#5:9", "paths": [["Health"]]}})")->nonzero);
    // A default object by name, as call takes it; the console finds it before reading.
    CHECK(Call(R"({"verb": "get", "args": {"object": "Default__PhysicsSettings", "paths": [["PhysicalSurfaces"]]}})")->className ==
          "Default__PhysicsSettings");
    CHECK(Call(R"({"verb": "rows", "args": {"object": "Default__Table", "limit": 5}})")->className == "Default__Table");
    CHECK(Call(R"({"verb": "entries", "args": {"object": "Default__Settings", "field": "Map"}})")->className == "Default__Settings");
    CHECK(Why(R"({"verb": "get", "args": {"object": "PhysicsSettings", "paths": [["PhysicalSurfaces"]]}})").contains("types")); // A class is not an object.
    CHECK(Call(R"({"verb": "each", "args": {"cls": "Pawn", "paths": [["Health"]], "nonzero": true}})")->nonzero);
    CHECK(Call(R"({"verb": "rows", "args": {"object": "0x1000#5:9", "nonzero": true, "limit": 0}})")->nonzero);
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
    const auto snap = Call(R"({"verb": "get", "args": {"object": "0x1b3eec26700#185680:9", "snap": true}})");
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

    parsed = Call(R"({"verb": "get", "args": {"object": "0x2000#7:0", "types": true}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Remote);
    CHECK(parsed->request.op == ue::op::Fields);
    CHECK(ue::GetRef(parsed->request) == Two);
    CHECK(parsed->request.length == 0); // First page.
    CHECK(ue::Check(parsed->request) == Status::Ok);
    CHECK(ue::GetRef(Call(R"({"verb": "get", "args": {"object": "0x4000", "types": true}})")->request).index == -1);
    CHECK(Call(R"({"verb": "get", "args": {"object": "Character", "types": true, "own": true, "match": "aim"}})")->className == "Character");
    CHECK(Why(R"({"verb": "get", "args": {"object": "0x1000#5:9", "paths": [["Health"]], "types": true}})").starts_with("paths: "));
    CHECK(Why(R"({"verb": "get", "args": {"object": "0x1000#5:9", "types": true, "nonzero": true}})").starts_with("types reads no values"));
}

TEST_CASE("get snap keeps a baseline in an optional file; changed reads it") {
    const auto snap = Call(R"({"verb": "get", "args": {"object": "0x1000#5:9", "snap": true, "file": "ctrl.snap"}})");
    REQUIRE(snap.has_value());
    CHECK(snap->kind == Kind::Snap);
    CHECK(snap->file == "ctrl.snap");
    CHECK(Call(R"({"verb": "get", "args": {"object": "0x1000#5:9", "snap": true}})")->file.empty());
    const auto changed = Call(R"({"verb": "get", "args": {"object": "0x1000#5:9", "changed": true, "file": "ctrl.snap"}})");
    REQUIRE(changed.has_value());
    CHECK(changed->kind == Kind::Dump);
    CHECK(changed->changed);
    CHECK(changed->file == "ctrl.snap");
    CHECK(Why(R"({"verb": "get", "args": {"object": "0x1000", "snap": true}})").starts_with("object: snap and changed need a literal ref"));
    CHECK(Why(R"({"verb": "get", "args": {"object": "0x1000#5:9", "snap": true, "changed": true}})").starts_with("snap and changed"));
    CHECK(Why(R"({"verb": "get", "args": {"object": "0x1000#5:9", "snap": true, "nonzero": true}})").starts_with("snap keeps"));
    CHECK(Why(R"({"verb": "get", "args": {"object": "0x1000#5:9", "paths": [["Health"]], "changed": true}})").starts_with("paths: "));
    CHECK(Why(R"({"verb": "get", "args": {"object": "0x1000#5:9", "file": "ctrl.snap"}})").starts_with("file goes with"));
}

TEST_CASE("get filters: match, nonzero and changed together") {
    auto parsed = Call(R"({"verb": "get", "args": {"object": "0x1000#5:9", "match": "aim", "nonzero": true, "changed": true, "file": "before.snap"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Dump);
    CHECK(parsed->match == "aim");
    CHECK(parsed->nonzero);
    CHECK(parsed->changed);
    CHECK(parsed->file == "before.snap");
    parsed = Call(R"({"verb": "get", "args": {"object": "0x1000#5:9", "changed": true, "nonzero": true}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->file.empty());
    CHECK(Why(R"({"verb": "get", "args": {"object": "0x1000#5:9", "bogus": true}})").starts_with("unknown key 'bogus'"));
    CHECK(Why(R"({"verb": "get", "args": {"object": "0x1000#5:9", "paths": [["Health"]], "match": "aim"}})").starts_with("paths: "));
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
    auto functions = Call(R"({"verb": "funcs", "args": {"object": "0x1000#5:9", "text": "Socket", "start": 32}})");
    REQUIRE(functions.has_value());
    CHECK(functions->request.op == ue::op::Functions);
    CHECK(ue::GetRef(functions->request) == One);
    CHECK(std::string_view(functions->request.text) == "Socket");
    CHECK(functions->request.length == 32);
    CHECK(ue::Check(functions->request) == Status::Ok);
    CHECK(Call(R"({"verb": "funcs", "args": {"object": "0x1000#5:9", "start": 32}})").has_value());
    CHECK(Why(R"({"verb": "funcs", "args": {"object": "0x1000#5:9", "text": "Socket", "start": 99999}})").starts_with("start is"));
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
}

TEST_CASE("refs takes a pooled name or its index") {
    auto parsed = Call(R"({"verb": "refs", "args": {"index": 145935}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Refs);
    CHECK(parsed->request.args[0] == 145935);
    parsed = Call(R"({"verb": "refs", "args": {"name": "Attribute.Gameplay.Character.ReloadSpeedMultiplier"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.args[0] == 0);
    CHECK(std::string_view(parsed->request.text) == "Attribute.Gameplay.Character.ReloadSpeedMultiplier");
    CHECK(Why(R"({"verb": "refs"})").starts_with("name: give"));
    CHECK(Why(R"({"verb": "refs", "args": {"name": "A", "index": 3}})").starts_with("name: give"));
    CHECK(Why(R"({"verb": "refs", "args": {"index": 0}})").starts_with("index is"));
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
    CHECK(Call(R"({"verb": "stats"})")->kind == Kind::Stats);
    CHECK_FALSE(Call(R"({"verb": "stats"})")->all);
    CHECK(Call(R"({"verb": "stats", "args": {"all": true}})")->all);
    CHECK(Call(R"({"verb": "stats", "args": {"sessions": true}})")->listSessions);
    CHECK(Call(R"({"verb": "stats", "args": {"session": "mcp-1"}})")->target == "mcp-1");
    CHECK(Why(R"({"verb": "stats", "args": {"all": true, "sessions": true}})").starts_with("all, sessions and session"));
    CHECK(Why(R"({"verb": "stats", "args": {"sessions": true, "session": "mcp-1"}})").starts_with("all, sessions and session"));
    CHECK(Why(R"({"verb": "stats", "args": {"session": 1}})").starts_with("session: "));
}

TEST_CASE("batch keeps its calls as JSON and refuses what cannot be held") {
    auto parsed = Call(R"({"verb": "batch", "args": {"calls": [{"verb": "find", "args": {"text": "Pawn"}}, {"verb": "read", "args": {"addr": "0x10", "len": 4}}, {"verb": "modules"}]}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Batch);
    REQUIRE(parsed->fields.size() == 3);
    CHECK(Json::parse(parsed->fields[1]) == Json::parse(R"({"verb": "read", "args": {"addr": "0x10", "len": 4}})"));
    CHECK(Json::parse(parsed->fields[2]) == Json::parse(R"({"verb": "modules"})"));
    for (const char* inner : {"batch", "quit", "status", "help", "stats"}) {
        const Json call{{"verb", "batch"}, {"args", {{"calls", Json::array({Json{{"verb", inner}}})}}}};
        CHECK_MESSAGE(Why(call).starts_with("calls: not allowed in a batch"), inner);
    }
    CHECK(Why(R"({"verb": "batch", "args": {"calls": [{"args": {}}]}})").starts_with("calls: "));
    CHECK(Why(R"({"verb": "batch", "args": {"calls": [{"verb": "read", "args": [1]}]}})").starts_with("calls: "));
    Json many{{"verb", "batch"}, {"args", {{"calls", Json::array()}}}};
    for (size_t i = 0; i <= MaxBatchCommands; ++i) many["args"]["calls"].push_back(Json{{"verb", "modules"}});
    CHECK(Why(many).starts_with("calls: at most"));
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
    CHECK(Call(R"({"verb": "get", "args": {"object": "0x1000#5:9", "paths": [["Map"]], "elements": 16}})")->elements == 16);
    CHECK(Call(R"({"verb": "each", "args": {"cls": "Pawn", "paths": [["Map"]], "elements": 256}})")->elements == 256);
    CHECK(Call(R"({"verb": "get", "args": {"object": "0x1000#5:9", "elements": 3}})")->elements == 3);
    CHECK(Why(R"({"verb": "get", "args": {"object": "0x1000#5:9", "paths": [["Map"]], "elements": 0}})").starts_with("elements is"));
    CHECK(Why(R"({"verb": "each", "args": {"cls": "Pawn", "paths": [["Map"]], "elements": 257}})").starts_with("elements is"));
}

TEST_CASE("find takes text, a class filter, a cursor and a row limit") {
    const auto both = Call(R"({"verb": "find", "args": {"text": "Vendor", "cls": "Character", "start": 4096}})");
    REQUIRE(both.has_value());
    CHECK(both->request.op == ue::op::Find);
    CHECK(std::string_view(both->request.text) == "Vendor");
    CHECK(std::string_view(reinterpret_cast<const char*>(both->request.data)) == "Character");
    CHECK(both->request.length == 10);
    CHECK(both->request.args[1] == 4096);
    CHECK(ue::Check(both->request) == Status::Ok);
    CHECK(both->limit == 0);
    CHECK(Call(R"({"verb": "find", "args": {"text": "Vendor", "limit": 50}})")->limit == 50);
    CHECK(Why(R"({"verb": "find", "args": {"text": "Vendor", "limit": 0}})").starts_with("limit must be"));
    CHECK(Why(R"({"verb": "find", "args": {"text": "Vendor", "limit": 1001}})").starts_with("limit must be"));
    const auto classOnly = Call(R"({"verb": "find", "args": {"cls": "DataTable"}})");
    REQUIRE(classOnly.has_value());
    CHECK(classOnly->request.text[0] == 0);
    CHECK(ue::Check(classOnly->request) == Status::Ok);
    CHECK(classOnly->request.flags == 0);
    const auto live = Call(R"({"verb": "find", "args": {"cls": "DataTable", "live": true}})");
    REQUIRE(live.has_value());
    CHECK(live->request.flags == ue::FindLive);
    CHECK(ue::Check(live->request) == Status::Ok);
    CHECK(std::string_view(Call(R"({"verb": "find", "args": {"text": "two words", "start": 1}})")->request.text) == "two words");
    CHECK(Why(R"({"verb": "find"})").starts_with("text: give text, cls or outer"));
    CHECK(Why(R"({"verb": "find", "args": {"cls": "A B"}})").starts_with("cls is"));
    CHECK(Why(R"({"verb": "find", "args": {"text": "x", "start": -1}})").starts_with("start: "));
}

TEST_CASE("object verbs take a bare class name for the console to resolve") {
    for (const char* call : {R"({"verb": "get", "args": {"object": "Character", "types": true}})",
                             R"({"verb": "get", "args": {"object": "BP_Door_C", "types": true}})",
                             R"({"verb": "funcs", "args": {"object": "Character", "text": "Jump"}})",
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
    CHECK(Call(R"({"verb": "get", "args": {"object": "0x1000#5:9", "types": true}})")->className.empty());
    CHECK(Why(R"({"verb": "get", "args": {"object": "12abc", "types": true}})").starts_with("object is"));
    CHECK(Why(R"({"verb": "get", "args": {"object": "Some-Thing", "types": true}})").starts_with("object is"));
    CHECK(Why(R"({"verb": "func", "args": {"object": "Character", "function": ":Bad"}})").starts_with("function: "));
}

TEST_CASE("census takes an optional class, a match, a page and diff") {
    auto parsed = Call(R"({"verb": "census"})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.op == ue::op::Census);
    CHECK(parsed->limit == DefaultCensusLimit);
    CHECK_FALSE(parsed->changed);
    CHECK(ue::Check(parsed->request) == Status::Ok);
    parsed = Call(R"({"verb": "census", "args": {"cls": "Actor", "limit": 0}})");
    REQUIRE(parsed.has_value());
    CHECK(std::string_view(reinterpret_cast<const char*>(parsed->request.data)) == "Actor");
    CHECK(parsed->limit == 0);
    CHECK(ue::Check(parsed->request) == Status::Ok);
    parsed = Call(R"({"verb": "census", "args": {"diff": true, "cls": "Actor", "match": "proj", "limit": 5}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->changed);
    CHECK(parsed->match == "proj");
    CHECK(parsed->limit == 5);
    parsed = Call(R"({"verb": "census", "args": {"cls": "Actor", "match": "zone", "start": 100, "limit": 50}})");
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
    const auto parsed = Call(R"({"verb": "name-find", "args": {"text": "Jump", "start": 131072}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->request.op == ue::op::NameFind);
    CHECK(std::string_view(parsed->request.text) == "Jump");
    CHECK(parsed->request.args[1] == 131072);
    CHECK(ue::Check(parsed->request) == Status::Ok);
    CHECK(Why(R"({"verb": "name-find", "args": {"start": 3}})").starts_with("missing text"));
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
    auto parsed = Call(R"({"verb": "trace", "args": {"start": {"object": "0x1000#5:9", "socket": "Head"}, "end": [100.5, -2, 3e2], "channel": 1, "simple": true, "ignore": "0x2000#7:0"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Trace);
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
    parsed = Call(R"({"verb": "trace", "args": {"start": [0, 0, 0], "end": "0x2000#7:0", "channel": 5}})");
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
        const Json call{{"verb", "trace"}, {"args", Json::parse(each.args)}};
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
    const auto read = Call(R"({"verb": "calls-read", "args": {"slot": 3, "start": 40}})");
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
    auto parsed = Call(R"({"verb": "label", "args": {"object": "0x1000#5:2", "offset": "0x538", "type": "f64[3]", "name": "Start"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Label);
    CHECK(parsed->offset == 0x538);
    CHECK(parsed->target == "f64[3]");
    CHECK(parsed->match == "Start");
    parsed = Call(R"({"verb": "label", "args": {"object": "TriggerVolume", "offset": "0x53c", "type": "f32", "name": "Duration?"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->className == "TriggerVolume");
    CHECK(Why(R"({"verb": "label", "args": {"object": "TriggerVolume", "offset": "0x53c", "type": "f32"}})").starts_with("missing name"));
    CHECK(Why(R"({"verb": "label", "args": {"object": "TriggerVolume", "offset": "0x53c", "type": "f16", "name": "X"}})").starts_with("type"));
    CHECK(Why(R"({"verb": "label", "args": {"object": "TriggerVolume", "offset": "0x53c", "type": "pad", "name": "4"}})").starts_with("type"));
    CHECK(Why(R"({"verb": "label", "args": {"object": "TriggerVolume", "offset": "0x10000", "type": "u8", "name": "X"}})").starts_with("offset is"));
    CHECK(Why(R"({"verb": "label", "args": {"object": "TriggerVolume", "offset": "0x53c", "type": "u8", "name": "X Y"}})").starts_with("type"));
}

TEST_CASE("export-labels takes an optional file, module and kinds") {
    auto parsed = Call(R"({"verb": "export-labels", "args": {"file": "labels.jsonl"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::ExportLabels);
    CHECK(parsed->file == "labels.jsonl");
    CHECK(parsed->target.empty());
    CHECK(parsed->limit == label_kind::All); // Every kind by default.
    parsed = Call(R"({"verb": "export-labels", "args": {"file": "labels.jsonl", "module": "engine.dll"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->target == "engine.dll");
    CHECK(Call(R"({"verb": "export-labels"})")->file.empty());       // The console's labels file.
    parsed = Call(R"({"verb": "export-labels", "args": {"kinds": ["interface", "global"], "module": "engine.dll"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->limit == (label_kind::Interface | label_kind::Global));
    CHECK(Why(R"({"verb": "export-labels", "args": {"kinds": ["exec", "types"]}})").starts_with("kinds: ")); // Unknown kind.
    CHECK(Why(R"({"verb": "export-labels", "args": {"kinds": []}})").starts_with("kinds: "));
    CHECK(Why(R"({"verb": "export-labels", "args": {"kinds": "exec"}})").starts_with("kinds: "));
}

TEST_CASE("entries takes an object, a field and an optional cursor") {
    auto parsed = Call(R"({"verb": "entries", "args": {"object": "0x1000#5:9", "field": "Comp:Attributes"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::Entries);
    CHECK(parsed->request.op == ue::op::Entries);
    CHECK(parsed->target == "Comp:Attributes");
    CHECK(parsed->limit == 0);
    CHECK(Call(R"({"verb": "entries", "args": {"object": "0x1000#5:9", "field": "Attributes", "start": 12}})")->limit == 12);
    CHECK(Why(R"({"verb": "entries", "args": {"object": "0x1000#5:9"}})").starts_with("missing field"));
    CHECK(Why(R"({"verb": "entries", "args": {"object": "0x1000#5:9", "field": ""}})").starts_with("field is"));
}

TEST_CASE("rows pages a data table with call's row options") {
    auto rows = Call(R"({"verb": "rows", "args": {"object": "0x1000#5:9", "match": "Rifle", "fields": [["Price"], ["Item", "TagName"]], "start": 10, "limit": 0}})");
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
    CHECK(Why(R"({"verb": "rows", "args": {"object": "0x1000#5:9", "fields": [[".a"]]}})").starts_with("fields: "));
    CHECK(Why(R"({"verb": "rows", "args": {"object": "0x1000#5:9", "at": 1, "fields": [["a"]]}})").starts_with("at reads curve tables"));
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

TEST_CASE("mem-snap takes an address and a length; mem-diff a test and an element type; vtable its options") {
    auto parsed = Call(R"({"verb": "mem-snap", "args": {"addr": "0x10000", "len": 4096}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::MemSnap);
    CHECK(parsed->range == 4096);
    CHECK(Why(R"({"verb": "mem-snap", "args": {"addr": "0x10000", "len": 0}})").starts_with("len must be"));
    CHECK(Why(R"({"verb": "mem-snap", "args": {"addr": "0x10000", "len": 2000000}})").starts_with("len must be"));
    parsed = Call(R"({"verb": "mem-diff", "args": {"test": "decreased", "type": "f32", "keep": true, "limit": 10}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->kind == Kind::MemDiff);
    CHECK(parsed->target == "decreased");
    CHECK(parsed->view == "f32");
    CHECK(parsed->all);
    CHECK(parsed->limit == 10);
    CHECK(Why(R"({"verb": "mem-diff", "args": {"test": "bigger", "type": "f32"}})").starts_with("test: one of"));
    CHECK(Why(R"({"verb": "mem-diff", "args": {"test": "changed", "type": "cstr"}})").starts_with("type is"));
    CHECK(Why(R"({"verb": "mem-diff", "args": {"test": "changed", "type": "f32", "limit": 257}})").starts_with("limit is"));
    const auto vtable = Call(R"({"verb": "vtable", "args": {"addr": "0x10000", "offset": "0x620", "rtti": true}})");
    REQUIRE(vtable.has_value());
    CHECK(vtable->kind == Kind::Vtable);
    CHECK(vtable->rtti);
    CHECK(vtable->offset == 0x620);
    CHECK(Why(R"({"verb": "vtable", "args": {"addr": "0x10000", "offset": 8, "table": true}})").starts_with("offset reads"));
}

TEST_CASE("the version text matches its parts") {
    const std::string expected = std::to_string(REAGENTLIVE_VERSION_MAJOR) + "." +
        std::to_string(REAGENTLIVE_VERSION_MINOR) + "." + std::to_string(REAGENTLIVE_VERSION_PATCH);
    CHECK(std::string(Version) == expected);
}
