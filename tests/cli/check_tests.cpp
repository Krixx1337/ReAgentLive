// reagentlive CLI calls: the schema checks of a JSON call and the verb table's rows.
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

TEST_CASE("an object arg's keys are checked by path; one given at the top goes inside it") {
    // A view key at the top: refused with its path, and the example moves it into view.
    Json error = Refused(R"({"verb": "find", "args": {"text": "Pawn", "limit": 5}})");
    CHECK(error["key"] == "view.limit");
    CHECK(error["message"].get<std::string>().starts_with("'limit' goes inside view"));
    CHECK(error["arg"]["key"] == "limit");
    CHECK(error["example"] == Json::parse(R"({"verb": "find", "args": {"text": "Pawn", "view": {"limit": 5}}})"));
    CHECK(Builds(error["example"]));
    // A view key the verb does not take: the view's own list, the example without it.
    error = Refused(R"({"verb": "find", "args": {"text": "Pawn", "view": {"limit": 5, "nonzero": true}}})");
    CHECK(error["key"] == "view");
    CHECK(error["message"].get<std::string>().starts_with("unknown key 'view.nonzero'; view takes start, limit"));
    CHECK(error["example"]["args"]["view"] == Json{{"limit", 5}});
    // A wrong value inside: its path and its part of the schema; the example drops it.
    error = Refused(R"({"verb": "census", "args": {"view": {"limit": "many", "match": "Pawn"}}})");
    CHECK(error["key"] == "view.limit");
    CHECK(error["message"].get<std::string>().starts_with("view.limit: a whole number"));
    CHECK(error["arg"]["type"] == "int");
    CHECK(error["example"]["args"]["view"] == Json{{"match", "Pawn"}});
    CHECK(Builds(error["example"]));
    // A required key inside an object.
    error = Refused(R"({"verb": "disasm", "args": {"addr": "0x1000", "function": true, "save": {"callees": 1}}})");
    CHECK(error["key"] == "save.file");
    CHECK(error["message"].get<std::string>().starts_with("missing save.file: "));
    CHECK(Builds(error["example"]));
    // A builder's message about an object's key names its path.
    CHECK(KeyOf(Row("find"), "limit must be 1..1000") == "view.limit");
    CHECK(KeyOf(Row("search"), "start: bad") == "scope.start");
    CHECK(KeyOf(Row("write"), "after: a timed write lasts at most 60 s") == "undo.after");
    // Once checked, an object's keys read as the verb's own.
    const auto args = CheckCall(Row("rows"), Json::parse(R"({"object": "0x1000#5:9", "view": {"limit": 3, "nonzero": true}})"));
    REQUIRE(args);
    CHECK(args->Int("limit") == 3);
    CHECK(args->Has("nonzero"));
}

TEST_CASE("view is one schema; no verb takes more than six own args beside it") {
    for (const auto& verb : AllVerbs()) {
        size_t own = 0;
        std::set<std::string_view> names;
        for (const auto& param : verb.args) {
            if (param.key != "view") ++own;
            CHECK_MESSAGE(names.insert(param.key).second, verb.name << "." << param.key);
            // Only an object has keys, one level deep.
            CHECK_MESSAGE((param.type == "obj") == !param.fields.empty(), verb.name << "." << param.key);
            for (const auto& field : param.fields) {
                CHECK_MESSAGE(field.fields.empty(), verb.name << "." << param.key << "." << field.key);
                // An object's keys read as the verb's own once checked: never a name twice.
                CHECK_MESSAGE(names.insert(field.key).second, verb.name << "." << param.key << "." << field.key);
            }
            // Output shaping lives in view alone (a key of the same name and another type means another thing:
            // trace's start is a point).
            if (param.key != "view")
                for (const auto& shared : ViewKeys())
                    CHECK_MESSAGE(!(param.key == shared.key && param.type == shared.type), verb.name << "." << param.key << " belongs in view");
            if (param.key != "view") continue;
            CHECK(param.type == "obj");
            CHECK(param.help == View("limit").help);
            for (const auto& field : param.fields) {
                const auto shared = std::ranges::find(ViewKeys(), field.key, &Arg::key);
                REQUIRE_MESSAGE(shared != ViewKeys().end(), verb.name << ".view." << field.key);
                CHECK(field.type == shared->type);
                CHECK(field.list == shared->list);
                CHECK(field.help == shared->help);
                if (field.key != "limit") CHECK(field.fallback == shared->fallback);
            }
        }
        CHECK_MESSAGE(own <= MaxOwnArgs, verb.name << " takes " << own << " own args");
    }
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
        {"mem-diff", R"({"compare": "changed", "type": "f32"})"},              // type is an element type.
        {"sig-check", R"({"pattern": "48 8b 05 89 ?? 90"})"},               // pattern or file.
        {"find", R"({"text": "Pawn"})"},                                    // text, cls or outer.
        {"call", R"({"object": "0x1000#5:9", "function": "Name"})"},        // An object, not a class.
        {"call-pure", R"({"object": "0x1000#5:9", "function": "Name"})"},
        {"member-name", R"({"object": "Name", "offset": 16, "type": "f32", "name": "Name"})"}, // An element type.
        {"entries", R"({"object": "0x1000#5:9", "field": "Name"})"},
        {"rows", R"({"object": "0x1000#5:9"})"},
        {"get", R"({"object": "0x1000#5:9"})"},                             // A class by name needs types.
        {"describe", R"({"object": "0x1000#5:9"})"},                        // object, or slot and serial.
        {"name-refs", R"({"name": "Name"})"},                                    // name or name_index.
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

TEST_CASE("a builder reads only the keys its row has (REAGENTLIVE_ARGS_CHECK)") {
    // Args::At throws in this build when a builder reads a key its row lacks.
    // Each row's example, then the example with each optional arg (an
    // object's every key, an enum's every word), then with all of them: most
    // builder branches run at least once.
    const auto variants = [](const Verb& verb) {
        std::vector<Json> out;
        const Json base = ExampleCall(verb)["args"];
        out.push_back(base);
        Json all = base;
        for (const auto& arg : verb.args) {
            std::vector<Json> values;
            if (arg.type == "obj") {
                Json every = Json::object();
                for (const auto& field : arg.fields) every[std::string(field.key)] = check::Example(field);
                values.push_back(std::move(every));
                for (const auto& field : arg.fields) values.push_back(Json{{std::string(field.key), check::Example(field)}});
            } else if (arg::Enum(arg.type)) {
                for (std::string_view rest = arg.type; !rest.empty();) {
                    const size_t bar = rest.find('|');
                    values.push_back(std::string(rest.substr(0, bar)));
                    rest = bar == std::string_view::npos ? std::string_view{} : rest.substr(bar + 1);
                }
            } else {
                values.push_back(check::Example(arg));
            }
            for (auto& value : values) {
                Json one = base;
                one[std::string(arg.key)] = value;
                out.push_back(std::move(one));
            }
            all[std::string(arg.key)] = values.front();
        }
        out.push_back(std::move(all));
        return out;
    };
    for (const auto& verb : AllVerbs()) {
        if (!verb.build) continue;
        for (const auto& given : variants(verb)) {
            const auto args = CheckCall(verb, given);
            if (!args) continue;
            CHECK_NOTHROW_MESSAGE((void)Build(verb, *args, Loaded), verb.name << " " << given.dump());
        }
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
    CHECK(ArgJson(*FindArg(Row("touches"), "range"))["default"] == 524288);
    const Json type = ArgJson(*FindArg(Row("search"), "type"));
    CHECK(type["enum"] == Json::parse(R"(["hex", "u32", "i32", "u64", "ptr", "f32", "f64", "text", "wtext"])"));
    CHECK(type["required"] == true);
    CHECK(ArgJson(*FindArg(Row("name"), "indices"))["list"] == true);
}

namespace {
// What writes the target's memory or calls into it, from a built call. A
// trace calls a line trace, which changes no state (its row's notes); a
// batch's items are checked as their own verbs when they run.
bool WritesOrCalls(const Command& command) {
    const auto& request = command.request;
    if (command.kind == Kind::Pin || command.kind == Kind::Trial || (command.kind == Kind::TimedWrites && command.all) ||
        command.kind == Kind::Release || (command.kind == Kind::TypedCall && !(request.flags & ue::CallPure))) return true;
    if (request.op == reagentlive::op::Write || request.op == reagentlive::op::Pin || request.op == reagentlive::op::Trial)
        return true;
    if (request.op == ue::op::Call && !(request.flags & ue::CallPure)) return true;
    if (request.op == ue::op::CallsStart || request.op == ue::op::CallsStop) return true;
    if (command.kind == Kind::Calls && command.target == "stop") return true;
    return request.op == ue::op::GameThread && request.flags != static_cast<uint32_t>(ue::GameThreadAction::Status);
}
}

TEST_CASE("no form of a verb outside the modify pack writes or calls") {
    // Every form of every verb in explore, native and observe: a new verb or key lands here.
    const std::map<std::string_view, std::vector<std::string_view>> forms = {
        {"help", {R"({})", R"({"verb": "read"})", R"({"json": true})", R"({"hash": true})"}},
        {"status", {R"({})"}},
        {"quit", {R"({})"}},
        {"usage", {R"({})", R"({"report": "all"})", R"({"report": "sessions"})", R"({"session": "abc"})", R"({"only": ["totals", "advice"]})"}},
        {"batch", {R"({"calls": [{"verb": "read", "args": {"addr": "0x1000", "len": 8}}, {"verb": "modules"}]})"}},
        {"run", {R"({"plan": "plan.json"})", R"({"plan": "plan.json", "out": "lines.jsonl"})"}},
        {"read", {R"({"addr": "0x1000"})", R"({"addr": "0x1000", "len": 16, "type": "f32"})",
            R"({"addr": {"base": "0x1000", "chain": [16, 8]}})"}},
        {"peek", {R"({"addr": "0x1000"})", R"({"addr": "0x1000", "count": 1, "label": true, "name": "Foo"})"}},
        {"modules", {R"({})"}},
        {"search", {R"({"type": "f32", "value": 1.5})", R"({"type": "wtext", "value": "abc", "wait": 0, "max": 8})"}},
        {"search-job", {R"({"job": 3})", R"({"job": 3, "stop": true})"}},
        {"mem-snap", {R"({"addr": "0x1000", "len": 64})"}},
        {"mem-diff", {R"({"compare": "changed", "type": "f32"})", R"({"compare": "increased", "type": "i32", "keep": true, "view": {"limit": 4}})"}},
        {"watch-start", {R"({"duration": 5, "ranges": [{"addr": "0x1000", "len": 8}]})",
            R"({"duration": 5, "ranges": [{"addr": "0x1000", "type": "f32"}], "every": 0.1, "file": "w.jsonl"})"}},
        {"watch-read", {R"({})", R"({"all": true})", R"({"stop": true})", R"({"stop": true, "all": true})"}},
        {"sig-check", {R"({"pattern": "48 8b 05 89 ?? 90"})", R"({"file": "sigs.txt"})"}},
        {"sig-bytes", {R"({"addr": "0x1000"})", R"({"addr": "0x1000", "len": 32})"}},
        {"labels-sign", {R"({})", R"({"retry": true})"}},
        {"labels-port", {R"({})"}},
        {"labels-check", {R"({})", R"({"file": "names.jsonl", "module": "game.exe"})",
            R"({"status": ["moved", "missing"], "view": {"limit": 20}})"}},
        {"disasm", {R"({"addr": "0x1000"})", R"({"addr": "0x1000", "count": 8, "function": true})",
            R"({"addr": "0x1000", "function": true, "save": {"file": "f.json", "callees": 2}})"}},
        {"xrefs", {R"({"addr": "0x1000"})", R"({"addr": "0x1000", "all": true})"}},
        {"touches", {R"({"addr": "0x1000", "offsets": [16]})", R"({"addr": "0x1000", "offsets": ["0x10"], "access": "read"})",
            R"({"addr": "0x1000", "offsets": ["0x10"], "access": "write"})",
            R"({"addr": "0x1000", "offsets": [16], "bases": ["rcx", "rbp"]})"}},
        {"disp-scan", {R"({"offsets": ["0x538"]})", R"({"offsets": [16], "access": "any", "bases": ["rcx"], "view": {"start": "0x1000"}})",
            R"({"offsets": [16, 24], "access": "read", "width": 4, "imm": 1, "same_base": true})"}},
        {"vtable", {R"({"addr": "0x1000#5:9"})", R"({"addr": "0x1000#5:9", "view": {"limit": 4}, "resolve": true, "rtti": true})",
                    R"({"addr": "0x1000#5:9", "parts": 256, "slots": [6, 8]})"}},
        {"find", {R"({"text": "Pawn"})", R"({"cls": "Pawn", "live": true, "view": {"limit": 5}})"}},
        {"census", {R"({})", R"({"cls": "Pawn", "diff": true})"}},
        {"name-find", {R"({"text": "Health"})"}},
        {"name", {R"({"indices": [1, 2]})"}},
        {"each", {R"({"cls": "Pawn", "paths": [["Health"]]})", R"({"cls": "Pawn", "paths": [["Health"]], "duration": 5})",
            R"({"cls": "Pawn", "paths": [["Health"]], "where": {"field": ["Name"], "contains": "a"}, "view": {"limit": 0}})"}},
        {"call-pure", {R"({"object": "0x1000#5:9", "function": "Actor:K2_GetActorLocation"})",
            R"({"object": "0x1000#5:9", "function": "GetSocket", "params": {"SocketName": "#42"}})",
            R"({"object": "0x1000#5:9", "function": "Actor:K2_GetActorLocation", "dry": true})"}},
        {"call-each", {R"({"cls": "Pawn", "function": "GetHealth"})", R"({"cls": "Pawn", "function": "GetHealth", "duration": 5})",
            R"({"cls": "Pawn", "function": "GetHealth", "outer": "Level", "view": {"limit": 0, "nonzero": true}})"}},
        {"member-name", {R"({"object": "0x1000#5:9", "offset": "0x10", "type": "f32", "name": "Speed"})"}},
        {"labels-export", {R"({})", R"({"file": "out.jsonl"})"}},
        {"assets-export", {R"({"dir": "out", "classes": ["DataTable"]})",
            R"({"dir": "out", "classes": ["DataTable", "CurveTable"], "view": {"match": "Weapon"}})"}},
        {"layout-export", {R"({"file": "out.json", "classes": ["Pawn"]})", R"({"file": "out.json", "classes": ["Pawn"], "closure": 1})",
            R"({"file": "out.json", "classes": ["Pawn"], "closure": 2})"}},
        {"layout-check", {R"({"file": "out.json"})"}},
        {"entries", {R"({"object": "0x1000#5:9", "field": "Items"})", R"({"object": "0x1000#5:9", "field": "Items", "view": {"start": 8}})"}},
        {"rows", {R"({"object": "0x1000#5:9"})", R"({"object": "0x1000#5:9", "view": {"match": "a", "fields": [["b"]]}})"}},
        {"get", {R"({"object": "0x1000#5:9"})", R"({"object": "0x1000#5:9", "paths": [["Health"]]})",
            R"({"object": "Pawn", "mode": "types"})", R"({"object": "0x1000#5:9", "mode": "snap"})",
            R"({"object": "0x1000#5:9", "mode": "changed"})"}},
        {"line-trace", {R"({"start": [0, 0, 0], "end": [1, 1, 1], "ignore": "0x1000#5:9"})"}},
        {"describe", {R"({"object": "0x1000#5:9"})", R"({"slot": 5, "serial": 9})"}},
        {"func", {R"({"object": "0x1000#5:9", "function": "GetHealth"})"}},
        {"script", {R"({"object": "0x1000#5:9", "function": "Tick"})", R"({"object": "0x1000#5:9", "function": "Tick", "decode": true})",
            R"({"object": "Pawn", "function": "ExecuteUbergraph_Pawn", "decode": true, "entry": "0x1c"})",
            R"({"object": "0x1000#5:9", "decode": true})"}},
        {"enum", {R"({"enum": "EMode"})"}},
        {"name-refs", {R"({"name": "Health"})", R"({"name_index": 42})"}},
        {"owner", {R"({"addrs": ["0x1000"]})", R"({"addrs": ["0x1000", "0x2008"], "within": ["0x1000#5:9"]})"}},
        {"route", {R"({"object": "0x1000#5:9"})",
            R"({"object": "0x1000#5:9", "sources": ["pawn", "0x2000"], "depth": 4, "budget": 500, "up": true})"}},
        {"funcs", {R"({"object": "0x1000#5:9"})", R"({"object": "Pawn", "view": {"start": 4, "match": "Get"}})"}},
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

TEST_CASE("a plan is a list of steps: verb, args, expect and repeat, checked before any runs") {
    auto steps = plan::Parse(R"([{"verb": "status"}, {"verb": "read", "args": {"addr": "0x10"}, "expect": {"ok": true}},
        {"verb": "modules", "repeat": {"every": 0.5, "for": 2}}])");
    REQUIRE(steps);
    REQUIRE(steps->size() == 3);
    CHECK((*steps)[0].call == Json::parse(R"({"verb": "status", "args": {}})"));
    CHECK((*steps)[1].expect == Json{{"ok", true}});
    CHECK((*steps)[0].Runs() == 1);
    CHECK((*steps)[2].Runs() == 4);                         // At 0, 0.5, 1 and 1.5 s: for is the span, not runs + 1.
    plan::Step step;
    step.every = 1, step.seconds = 5;
    CHECK(step.Runs() == 5);
    step.every = 2;
    CHECK(step.Runs() == 3);                                // 0, 2, 4.
    step.every = 0.1, step.seconds = 0.3;
    CHECK(step.Runs() == 3);                                // Not 4 from rounding.
    step.every = 10, step.seconds = 1;
    CHECK(step.Runs() == 1);
    const struct { const char* text; const char* message; } bad[] = {
        {"{", "plan: not JSON"},
        {"[]", "plan: a JSON list of steps"},
        {R"({"verb": "status"})", "plan: a JSON list of steps"},
        {R"([{"verb": "status", "wait": 1}])", "plan step 1: unknown key 'wait'"},
        {R"([{"args": {}}])", "plan step 1: verb is"},
        {R"([{"verb": "status"}, {"verb": "quit"}])", "plan step 2: quit is not allowed"},
        {R"([{"verb": "status", "args": []}])", "plan step 1: args is an object"},
        {R"([{"verb": "status", "expect": true}])", "plan step 1: expect is an object"},
        {R"([{"verb": "status", "repeat": {"every": 1}}])", "plan step 1: repeat is"},
        {R"([{"verb": "status", "repeat": {"every": 0, "for": 1}}])", "plan step 1: repeat: every above 0"},
        {R"([{"verb": "status", "repeat": {"every": 1, "for": 601}}])", "plan step 1: repeat: every above 0"},
        {R"([{"verb": "status", "repeat": {"every": 0.001, "for": 10}}])", "plan step 1: repeat: at most 1000 runs"},
    };
    for (const auto& each : bad) {
        const auto parsed = plan::Parse(each.text);
        REQUIRE_FALSE_MESSAGE(parsed.has_value(), each.text);
        CHECK_MESSAGE(parsed.error().starts_with(each.message), parsed.error());
    }
}

TEST_CASE("an expect is a subset of the envelope; a mismatch names its path") {
    const Json envelope = Json::parse(R"({"command": "status", "ok": true, "result": {"pid": 4, "packs": ["ue"], "rate": 60.0}})");
    CHECK(plan::Mismatch(Json::parse(R"({"ok": true})"), envelope).empty());
    CHECK(plan::Mismatch(Json::parse(R"({"result": {"pid": 4, "packs": ["ue"], "rate": 60}})"), envelope).empty());
    CHECK(plan::Mismatch(Json::parse(R"({"ok": false})"), envelope) == "ok: wanted false, got true");
    CHECK(plan::Mismatch(Json::parse(R"({"result": {"tick": 1}})"), envelope) == "result.tick: wanted 1, absent");
    CHECK(plan::Mismatch(Json::parse(R"({"result": {"packs": []}})"), envelope) == "result.packs: wanted a list of 0, got [\"ue\"]");
    CHECK(plan::Mismatch(Json::parse(R"({"result": {"packs": ["x"]}})"), envelope) == "result.packs[0]: wanted \"x\", got \"ue\"");
    CHECK(plan::Mismatch(Json::parse(R"({"result": {"pid": {"a": 1}}})"), envelope) == "result.pid: wanted an object, got 4");
}

TEST_CASE("removed verbs are not in the table") {
    for (const char* gone : {"trial", "trial-list", "trial-clear", "pin", "pin-list", "pin-clear", "base", "region",
                             "read-batch", "fields", "dump", "snap", "diff", "params", "watch", "record", "census-diff", "rtti",
                             "stats", "refs", "label", "export-labels", "export-assets", "trace", "writes"})
        CHECK_MESSAGE(!FindVerb(AllVerbs(), gone), gone);
}
