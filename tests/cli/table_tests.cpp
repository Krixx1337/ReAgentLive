// reagentlive CLI verb table: each row's builder, args and result type, help
// built from it, refusals, error kinds, named types and tool packs.
#include "reagentlive/packs/ue/cli/results/call_text.h"
#include "reagentlive/packs/ue/cli/results/census.h"
#include "reagentlive/packs/ue/cli/results/layout.h"
#include "reagentlive/frontends/cli/results/labels.h"
#include "reagentlive/frontends/cli/results/result_json.h"
#include "reagentlive/packs/ue/cli/results/result_ue_objects.h"
#include "reagentlive/packs/ue/cli/results/result_ue_fields.h"
#include "reagentlive/packs/ue/cli/results/result_ue_calls.h"
#include "reagentlive/packs/ue/cli/results/result_ue_code.h"
#include "reagentlive/packs/ue/cli/values/pack_input.h"
#include "reagentlive/packs/ue/cli/values/watch_field.h"
#include "doctest.h"
#include "verbs.h"
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace reagentlive;
using namespace reagentlive::cli;

namespace {
struct Row {
    Addr address;
    std::optional<std::string> name;
    std::string kind;
    std::vector<std::optional<HexBytes>> values;
    shape::Location where;
    template<class V> static void Describe(V& v) {
        v("address", &Row::address);
        v("name", &Row::name);
        v("kind", &Row::kind, "code|data");
        v("values", &Row::values);
        v.flat("target_", &Row::where);
    }
};
}

TEST_CASE("a result struct writes exactly the keys it describes, and omits what does not apply") {
    const uint8_t bytes[] = {1, 0xff};
    Row row{0x1000, {}, "code", {HexBytes(bytes), std::nullopt}, {}};
    CHECK(ToJson(row) == Json{{"address", "0x1000"}, {"kind", "code"}, {"values", {"01 ff", nullptr}}});
    row.name = "Start";
    row.where = Locate(0x7ff600001234, std::vector<Module>{{"game.exe", 0x7ff600000000, 0x100000}});
    const auto json = ToJson(row);
    CHECK(json["name"] == "Start");
    CHECK(json["target_module"] == "game.exe");
    CHECK(json["target_rva"] == "0x1234");
    const auto type = schema::Of<Row>();
    CHECK(schema::Notation(type) ==
        "{address: addr, name?: str, kind: code|data, values: [bytes|null], target_module?: str, target_rva?: hex}");
    CHECK(schema::Problem(type, json) == "");
    // What a strict caller refuses: a key the type lacks, a missing one, a word outside the enum.
    auto extra = json;
    extra["nope"] = 1;
    CHECK(schema::Problem(type, extra) == "result: unknown key 'nope'");
    auto missing = json;
    missing.erase("kind");
    CHECK(schema::Problem(type, missing) == "result: missing key 'kind'");
    auto wrong = json;
    wrong["kind"] = "heap";
    CHECK(schema::Problem(type, wrong) == "result.kind: 'heap' is not one of code|data");
    wrong = json;
    wrong["address"] = 4096;
    CHECK(schema::Problem(type, wrong) == "result.address: not 0x text");
    CHECK(schema::TypeJson(type)["struct"]["name"] == Json{{"type", "str"}, {"optional", true}});
    CHECK(schema::TypeJson(type)["struct"]["kind"]["type"] == Json{{"enum", {"code", "data"}}});
    CHECK(schema::TypeJson(type)["struct"]["values"]["type"] == Json{{"list", "bytes"}, {"nullable", true}});
}

TEST_CASE("every verb has a row with a builder and a result type, and no name twice") {
    std::set<std::string> names;
    for (const auto& verb : AllVerbs()) {
        CAPTURE(verb.name);
        CHECK(names.insert(std::string(verb.name)).second);
        CHECK(verb.build != nullptr);
        REQUIRE(verb.result != nullptr);
        CHECK(verb.result().kind == schema::Type::Kind::Struct);
        CHECK_FALSE(verb.summary.empty());
        CHECK((verb.origin == "core" || verb.origin == "ue"));
        for (const auto& arg : verb.args) CHECK_FALSE(arg.help.empty());
        // The list of a result is `rows`; no result names it otherwise.
        for (const auto& field : verb.result().fields)
            for (const auto old : {"hits", "items", "objects", "entries", "results", "instructions", "slots", "pins", "trials"})
                CHECK(field.key != old);
    }
    CHECK(names.contains("sig-check"));
    for (const auto gone : {"census-diff", "params", "rtti", "sig-file", "base", "region", "read-batch", "?", "exit", "q"})
        CHECK_FALSE(names.contains(gone));
}

TEST_CASE("help rows come from the table: args, an example call, the result in notation and its type") {
    const auto noGrammar = [](const Json& row) {
        for (const auto key : {"usage", "flags", "positionals", "aliases"}) CHECK_FALSE(row.contains(key));
    };
    const Verb* modules = FindVerb(AllVerbs(), "modules");
    REQUIRE(modules);
    const auto brief = Checked(MakeHelpRow(*modules, false, false));
    CHECK(brief == Json{{"verb", "modules"}, {"origin", "core"}, {"pack", "explore"},
        {"summary", "loaded modules, exe first (also refreshes the cached list)"}});
    const auto detail = Checked(MakeHelpRow(*modules, true, false));
    CHECK(detail["result"] == "{rows: [{name: str, base: addr, size: u64}]}");
    CHECK(detail["args"].empty());
    CHECK(detail["example"] == Json{{"verb", "modules"}, {"args", Json::object()}});
    CHECK_FALSE(detail.contains("type"));
    noGrammar(brief);
    noGrammar(detail);
    // read's region is a named type: help <verb> defines it under types.
    const auto read = Checked(MakeHelpRow(*FindVerb(AllVerbs(), "read"), true, false));
    CHECK(read["result"].get<std::string>().contains("region?: Region"));
    CHECK(read["types"]["Region"] == "{kind: code|rodata|data|mapped|private|reserved|free, base: addr, size: hex, protect: str}");
    CHECK(read["args"][0] == Json{{"key", "addr"}, {"type", "chain"}, {"required", true},
        {"help", "the address, or a pointer chain to follow first"}});
    CHECK(read["args"][1] == Json{{"key", "len"}, {"type", "int"}, {"default", 64}, {"help", "how many bytes (1..4096)"}});
    CHECK(read["example"] == Json{{"verb", "read"}, {"args", {{"addr", "0x140001000"}}}});
    noGrammar(read);
    const Verb* search = FindVerb(AllVerbs(), "search");
    REQUIRE(search);
    const auto full = Checked(MakeHelpRow(*search, true, true));
    noGrammar(full);
    // Each arg as ArgJson gives it: an enum as str with its words, a default as JSON.
    REQUIRE(full["args"].size() == search->args.size());
    for (size_t i = 0; i < search->args.size(); ++i) CHECK(full["args"][i] == ArgJson(search->args[i]));
    CHECK(full["args"][0] == Json{{"key", "type"}, {"type", "str"}, {"required", true},
        {"enum", {"hex", "u32", "i32", "u64", "ptr", "f32", "f64", "text", "wtext"}}, {"help", "the value's type (wtext: text as UTF-16)"}});
    CHECK(full["args"][1]["type"] == "scalar");
    CHECK(full["args"].back() == Json{{"key", "max"}, {"type", "int"}, {"default", 64},
        {"help", "hits at most (1..507); the job stops when full"}});
    // An object arg lists its keys as fields, each as ArgJson gives it.
    CHECK(full["args"][2]["type"] == "obj");
    CHECK(full["args"][2]["fields"][1] == Json{{"key", "from"}, {"type", "addr"}, {"help", "where to start, in place of module"}});
    // The example holds every required key, and the row accepts it.
    CHECK(full["example"] == Json{{"verb", "search"}, {"args", {{"type", "hex"}, {"value", "00 00 80 3f"}}}});
    CHECK(CheckCall(*search, full["example"]["args"]).has_value());
    // Only verbs that write the target's memory or call into it carry the mark.
    CHECK_FALSE(full.contains("writes"));
    std::set<std::string> writers;
    for (const auto& verb : AllVerbs())
        if (verb.writes) writers.insert(std::string(verb.name));
    CHECK(writers == std::set<std::string>{"write", "call", "release", "calls-start", "game-thread"});
    CHECK(Checked(MakeHelpRow(*FindVerb(AllVerbs(), "read"), false, false))["pack"] == "explore");
    CHECK(Checked(MakeHelpRow(*FindVerb(AllVerbs(), "write"), false, false))["writes"] == true);
    CHECK(full["type"]["struct"]["next_cursor"] == Json{{"type", "addr"}, {"optional", true}});
    CHECK_FALSE(FindVerb(AllVerbs(), "exit"));
    // The whole table, and a hash of it that moves with any row.
    const auto table = Checked(MakeHelpTable(AllVerbs(), AllOrigins()));
    CHECK(table["rows"].size() == AllVerbs().size());
    for (const auto& row : table["rows"]) {
        CAPTURE(row["verb"]);
        noGrammar(row);
        CHECK(row.contains("args"));
        CHECK(row.contains("example"));
    }
    const auto hash = TableHash(AllVerbs(), AllOrigins());
    CHECK(hash.size() == 16);
    auto changed = AllVerbs();
    changed[0].summary = "something else";
    CHECK(TableHash(changed, AllOrigins()) != hash);
}

TEST_CASE("every argument has a key, a type from the vocabulary, and keys are unique per verb") {
    for (const auto& verb : AllVerbs()) {
        CAPTURE(verb.name);
        std::set<std::string_view> keys;
        for (const auto& arg : verb.args) {
            CAPTURE(arg.key);
            CHECK(keys.insert(arg.key).second);
            CHECK(arg::Known(arg.type));
            // A default is JSON text of the arg's type.
            if (!arg.fallback.empty()) {
                const Json fallback = Json::parse(arg.fallback, nullptr, false);
                REQUIRE_FALSE(fallback.is_discarded());
                CHECK(check::Fits(arg, fallback));
            }
        }
        // The example a refusal shows is one the row accepts.
        CHECK(CheckCall(verb, ExampleCall(verb)["args"]).has_value());
        // One name in every form: a Python keyword would need another spelling in the generated functions.
        static const std::set<std::string_view> python{"False", "None", "True", "and", "as", "assert", "async", "await",
            "break", "class", "continue", "def", "del", "elif", "else", "except", "finally", "for", "from", "global", "if",
            "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with",
            "yield"};
        for (const auto key : keys) {
            CHECK_FALSE(python.contains(key));
            CHECK_FALSE(key.empty());
            CHECK(std::ranges::all_of(key, [](char c) { return (c >= 'a' && c <= 'z') || c == '_'; }));
        }
    }
}

TEST_CASE("an argument's type is checked against the row before the builder runs") {
    CHECK(Call(R"({"verb": "read", "args": {"addr": "0x10", "len": "zz"}})").error().starts_with("len: a whole number"));
    CHECK(Call(R"({"verb": "read", "args": {"addr": "0x10", "len": "0x"}})").error().starts_with("len: a whole number"));
    CHECK(Call(R"({"verb": "search", "args": {"type": "u16", "value": 5}})").error().starts_with(
        "type: one of hex|u32|i32|u64|ptr|f32|f64|text|wtext, not \"u16\""));
    CHECK(Call(R"({"verb": "touches", "args": {"addr": "0x10", "offsets": ["0x538", "x1"]}})").error().starts_with(
        "offsets: a list, each a whole number"));
    CHECK(Call(R"({"verb": "census", "args": {"view": {"limit": "many"}}})").error().starts_with("view.limit: a whole number"));
    CHECK(Call(R"({"verb": "xrefs", "args": {"addr": "0x10", "loose": "yes"}})").error().starts_with("loose: true or false"));
    // A decimal string is not an address: the builder's error names the key.
    CHECK(Call(R"({"verb": "read", "args": {"addr": "16"}})").error().starts_with("addr: '16' is not an address"));
    CHECK(Call(R"({"verb": "frobnicate"})").error() == "unknown verb 'frobnicate' (help lists them)");
    CHECK_FALSE(Call(R"({"args": {}})").has_value());
}

TEST_CASE("a refused call names its key, that key's schema and a corrected example") {
    const Verb* read = FindVerb(AllVerbs(), "read");
    REQUIRE(read);
    // A misspelt key: the nearest one named, the whole row's args, the value moved to the key meant.
    const Json misspelt = {{"addr", "0x10"}, {"length", 4}};
    const auto unknown = CheckCall(*read, misspelt);
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().message == "unknown key 'length' (did you mean len?); read takes addr, len, type");
    CHECK(unknown.error().key == "length");
    const Json error = RefusalJson(*read, misspelt, unknown.error());
    CHECK(error["kind"] == "schema");
    CHECK(error["key"] == "length");
    CHECK_FALSE(error.contains("arg"));
    CHECK(error["args"].size() == read->args.size());
    CHECK(error["example"] == Json{{"verb", "read"}, {"args", {{"addr", "0x10"}, {"len", 4}}}});
    // A wrong type: that key's ArgJson; an optional key at fault leaves the example.
    const Json wrong = {{"addr", "0x10"}, {"len", "zz"}};
    const auto typed = CheckCall(*read, wrong);
    REQUIRE_FALSE(typed.has_value());
    CHECK(typed.error().key == "len");
    const Json typedError = RefusalJson(*read, wrong, typed.error());
    CHECK(typedError["arg"] == ArgJson(read->args[1]));
    CHECK_FALSE(typedError.contains("args"));
    CHECK(typedError["example"] == Json{{"verb", "read"}, {"args", {{"addr", "0x10"}}}});
    // A missing required key gets an example value; a list where the row takes one value is refused.
    const Json noAddr = {{"len", 4}};
    const auto missing = CheckCall(*read, noAddr);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().message.starts_with("missing addr: "));
    CHECK(missing.error().key == "addr");
    CHECK(RefusalJson(*read, noAddr, missing.error())["example"] ==
        Json{{"verb", "read"}, {"args", {{"addr", "0x140001000"}, {"len", 4}}}});
    CHECK(CheckCall(*read, Json{{"addr", {"0x10", "0x20"}}}).error().key == "addr");
    CHECK(CheckCall(*read, Json::array()).error().message == "args is an object of named arguments");
    // A misspelt key is named as unknown, never read as another key's value.
    CHECK(CheckCall(*FindVerb(AllVerbs(), "each"), Json{{"class_", "Pawn"}, {"paths", Json::array({Json::array({"Health"})})}})
        .error().message.starts_with("unknown key 'class_' (did you mean cls?); each takes cls, paths, "));
    // A builder's error about one key starts with it.
    CHECK(KeyOf(*read, "len must be 1..4096") == "len");
    CHECK(KeyOf(*read, "addr: no loaded module 'x.dll'") == "addr");
    CHECK(KeyOf(*read, "nothing here").empty());
    // A chain that follows no pointer is refused; two offsets build.
    const auto chain = Call(R"({"verb": "read", "args": {"addr": {"base": "0x1000", "chain": [0]}}})");
    REQUIRE_FALSE(chain.has_value());
    CHECK(chain.error().find(value::ChainNoHop) != std::string::npos);
    const auto hops = Call(R"({"verb": "read", "args": {"addr": {"base": "0x1000", "chain": [0, 8]}}})");
    REQUIRE(hops.has_value());
    CHECK(hops->chain == std::vector<uint64_t>{0, 8});
    // A verb name near a real one is suggested.
    const auto nearest = check::NearestVerbs(AllVerbs(), "raed");
    CHECK(std::ranges::find(nearest, "read") != nearest.end());

    // batch: a list of calls, each kept as its JSON text; some verbs are refused inside one.
    const auto parsed = Call(R"({"verb": "batch", "args": {"calls": [{"verb": "read", "args": {"addr": "0x10"}}, {"verb": "modules"}]}})");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->fields.size() == 2);
    CHECK(Json::parse(parsed->fields[0])["args"]["addr"] == "0x10");
    CHECK(Json::parse(parsed->fields[1])["verb"] == "modules");
    CHECK(Call(R"({"verb": "batch", "args": {"calls": [{"verb": "status"}]}})").error().starts_with(
        "calls: not allowed in a batch: status"));
    CHECK(Call(R"({"verb": "batch", "args": {"calls": "read 0x10"}})").error().starts_with("calls: a list of"));
}

TEST_CASE("error kinds: one catalogue in ErrorKind's order, each with a meaning and a message shape") {
    std::set<std::string_view> names;
    for (size_t i = 0; i < std::size(ErrorKinds); ++i) {
        CHECK(ErrorKinds[i].kind == static_cast<ErrorKind>(i));
        CHECK_MESSAGE(names.insert(ErrorKinds[i].name).second, ErrorKinds[i].name);
        CHECK_FALSE(ErrorKinds[i].meaning.empty());
        CHECK_FALSE(ErrorKinds[i].message.empty());
    }
    CHECK(std::size(ErrorKinds) == static_cast<size_t>(ErrorKind::Internal) + 1);
    CHECK(KindName(ErrorKind::Schema) == "schema");
    CHECK(ErrorEnvelope("read", ErrorKind::File, "x")["error"]["kind"] == "file");
    // The shared templates, as the catalogue shows them.
    CHECK(refusal::OutsidePacks("write", "modify", "explore") ==
          "write is in the modify pack, outside this console's --packs explore: start it with modify in --packs");
    CHECK(refusal::Misplaced("limit", "view") == R"('limit' goes inside view: {"view": {"limit": ...}})");
}

TEST_CASE("named types: listed once in the table, by name in the rows that use them") {
    const auto table = Checked(MakeHelpTable(AllVerbs(), AllOrigins()));
    REQUIRE(table.contains("types"));
    for (const auto name : {"Chain", "Object", "TimedWrite", "WatchRange", "WatchSample", "ue.Name", "ue.FieldInfo"}) {
        CAPTURE(name);
        CHECK(table["types"].contains(name));
    }
    CHECK(table["types"]["Object"]["notation"] == "{ref: ref, name: str, class: str, class_size?: u32, outer?: {name: str, ref: ref}}");
    for (const auto& [name, named] : table["types"].items()) {
        CAPTURE(name);
        CHECK(named["type"].contains("struct")); // The definition itself, not a reference to it.
    }
    // peek is the core's, and its object rows are the core's Object type, which the pack fills.
    const Verb* peek = FindVerb(AllVerbs(), "peek");
    const auto machine = Checked(MakeHelpRow(*peek, true, true));
    CHECK(machine["type"]["struct"]["chain"]["type"] == Json{{"named", "Chain"}});
    CHECK(machine["type"]["struct"]["rows"]["type"]["list"]["struct"]["object"]["type"] == Json{{"named", "Object"}});
    const auto detail = Checked(MakeHelpRow(*peek, true, false));
    CHECK(detail["result"].get<std::string>().find("chain?: Chain") != std::string::npos);
    CHECK(detail["types"].contains("Chain"));
    CHECK(detail["types"].contains("Object"));
    // A verb whose whole result is a named type shows its definition.
    CHECK(Checked(MakeHelpRow(*FindVerb(AllVerbs(), "describe"), true, false))["result"] ==
        "{ref: ref, name: str, class: str, class_size?: u32, outer?: {name: str, ref: ref}}");
    // Origins give their own titles; only origins with rows are listed.
    CHECK(table["origins"] == Json{{{"origin", "core"}, {"title", "Core"}}, {{"origin", "ue"}, {"title", "Unreal pack"}}});
    // Blocking verbs are marked so a tool client can leave them out: a host's may block (a
    // countdown capture), and run (a plan's repeats); the other core and pack verbs return at once.
    for (const auto& verb : AllVerbs()) CHECK(verb.blocks == (verb.name == "run"));
}

TEST_CASE("every verb is in one tool pack; every writer in modify, none in the others") {
    std::map<ToolPack, std::set<std::string>> byPack;
    for (const auto& verb : AllVerbs()) {
        REQUIRE(static_cast<size_t>(verb.pack) < std::size(ToolPacks));
        byPack[verb.pack].insert(std::string(verb.name));
        if (verb.writes) CHECK_MESSAGE(verb.pack == ToolPack::Modify, verb.name << " writes outside modify");
    }
    // The packs a research session loads by default hold nothing that writes or calls.
    for (const auto pack : {ToolPack::Explore, ToolPack::Native, ToolPack::Observe})
        for (const auto& name : byPack[pack]) CHECK_FALSE_MESSAGE(FindVerb(AllVerbs(), name)->writes, name);
    CHECK(byPack[ToolPack::Modify] == std::set<std::string>{"write", "timed-writes", "call", "release", "calls-start",
                                                             "calls-read", "game-thread"});
    CHECK(byPack[ToolPack::Native].contains("disasm"));
    CHECK(byPack[ToolPack::Observe].contains("call-pure"));
    // help --json lists the packs, and plain help groups its rows by them.
    const auto table = Checked(MakeHelpTable(AllVerbs(), AllOrigins()));
    REQUIRE(table["packs"].size() == std::size(ToolPacks));
    CHECK(table["packs"][0] == Json{{"pack", "explore"}, {"job", std::string(ToolPacks[0].job)}});
    const auto rows = HelpByPack(AllVerbs());
    REQUIRE(rows.size() == AllVerbs().size());
    CHECK(rows.front().pack == "explore");
    CHECK(rows.back().pack == "modify");
    CHECK(std::ranges::is_sorted(rows, {}, [](const shape::HelpRow& row) {
        return static_cast<int>(FindToolPack(row.pack)->pack); }));
}

TEST_CASE("--packs lists parse to a set that always holds explore") {
    CHECK(*ParseToolPacks("native") == (ToolPackBit(ToolPack::Explore) | ToolPackBit(ToolPack::Native)));
    CHECK(*ParseToolPacks(" observe , modify,") ==
          (ToolPackBit(ToolPack::Explore) | ToolPackBit(ToolPack::Observe) | ToolPackBit(ToolPack::Modify)));
    CHECK(ToolPackList(AllToolPacks) == "explore,native,observe,modify");
    CHECK(ParseToolPacks("explore,writes").error() == "unknown pack 'writes' (packs: explore, native, observe, modify)");
}
