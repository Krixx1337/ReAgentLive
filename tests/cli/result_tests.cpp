// reagentlive CLI results: the typed result structs (results/schema.h), the
// values their builders give and the verb table that names them. Builders take
// canned client results, so no agent or object table is needed. Every result
// built here is also checked against its own described type.
#include "reagentlive/packs/ue/cli/results/call_text.h"
#include "reagentlive/packs/ue/cli/results/census.h"
#include "reagentlive/packs/ue/cli/results/layout.h"
#include "reagentlive/frontends/cli/results/labels.h"
#include "reagentlive/frontends/cli/results/result_json.h"
#include "reagentlive/packs/ue/cli/results/result_json_ue.h"
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
const Ref Pawn{0x1000, 7, 3};

ue::Field Field(std::string name, uint32_t offset, uint32_t size, std::string type) {
    ue::Field field{};
    field.name = std::move(name);
    field.owner = "Pawn";
    field.offset = offset;
    field.size = size;
    field.type = std::move(type);
    return field;
}
}

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
    CHECK(read["args"][1] == Json{{"key", "len"}, {"type", "int"}, {"default", 64}, {"help", "how many bytes"}});
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
        {"help", "hits at most; the job stops when full"}});
    // An object arg lists its keys as fields, each as ArgJson gives it.
    CHECK(full["args"][2]["type"] == "obj");
    CHECK(full["args"][2]["fields"][1] == Json{{"key", "start"}, {"type", "addr"}, {"help", "start, or a job's next_cursor"}});
    // The example holds every required key, and the row accepts it.
    CHECK(full["example"] == Json{{"verb", "search"}, {"args", {{"type", "hex"}, {"value", "00 00 80 3f"}}}});
    CHECK(CheckCall(*search, full["example"]["args"]).has_value());
    // Only verbs that write the target's memory or call into it carry the mark.
    CHECK_FALSE(full.contains("writes"));
    std::set<std::string> writers;
    for (const auto& verb : AllVerbs())
        if (verb.writes) writers.insert(std::string(verb.name));
    CHECK(writers == std::set<std::string>{"write", "writes", "call", "release", "calls-start", "game-thread"});
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
    CHECK(Call(R"({"verb": "xrefs", "args": {"addr": "0x10", "all": "yes"}})").error().starts_with("all: true or false"));
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

TEST_CASE("find and describe results") {
    const ue::FindResult found{{{Pawn, "Pawn_0", "Pawn"}}, 1, 25};
    const auto first = Checked(MakeFind(found, 0));
    CHECK(first["rows"][0] == Json{{"ref", "0x1000#7:3"}, {"name", "Pawn_0"}, {"class", "Pawn"}});
    CHECK(first["total"] == 1);
    CHECK(first["next_cursor"] == 25);
    const auto later = Checked(MakeFind(found, 25));
    CHECK_FALSE(later.contains("total")); // Only the first page counts.
    CHECK(later["from"] == 25);
    const ue::FindResult last{{{Pawn, "Pawn_0", "Pawn"}}, 1, 0};
    CHECK_FALSE(Checked(MakeFind(last, 0)).contains("next_cursor")); // Done: no cursor.
    const ue::Object described{Pawn, "Pawn_0", "Pawn", 0x4f0}; // A single describe knows the class size.
    CHECK(Checked(MakeObject(described))["class_size"] == 0x4f0);
    CHECK_FALSE(Checked(MakeObject(found.objects[0])).contains("class_size"));
}

TEST_CASE("funcs lists functions with owner and name") {
    const ue::FunctionList list{{"Pawn:GetSpeed", "Actor:K2_GetActorLocation", "Bare", "Owner:GetData"},
        {false, false, false, true}, 0};
    const auto result = Checked(MakeFuncs(Pawn, list, 0));
    REQUIRE(result["rows"].size() == 4);
    CHECK(result["rows"][1] == Json{{"owner", "Actor"}, {"name", "K2_GetActorLocation"}});
    CHECK(result["rows"][2] == Json{{"name", "Bare"}});
    // A native interface's function is marked: only on the interface class.
    CHECK(result["rows"][3] == Json{{"owner", "Owner"}, {"name", "GetData"}, {"via", "interface"}});
    CHECK_FALSE(result.contains("next_cursor"));
}

namespace {
ue::Field Member(std::string name, std::string type, uint32_t offset, uint32_t size) {
    ue::Field f{};
    f.name = std::move(name); f.type = std::move(type); f.offset = offset; f.size = size;
    return f;
}
}
TEST_CASE("typed call inputs pack scalars and plain structs") {
    namespace pi = pack_input;
    std::vector<uint8_t> b(8);
    auto span = [&](size_t n) { std::fill(b.begin(), b.end(), 0xcc); return std::span(b).first(n); };
    CHECK(pi::PackScalar(Member("F", "FloatProperty", 0, 4), "1.5", span(4)));
    float f; memcpy(&f, b.data(), 4); CHECK(f == 1.5f);
    CHECK(pi::PackScalar(Member("D", "DoubleProperty", 0, 8), "-2.25e3", span(8)));
    double d; memcpy(&d, b.data(), 8); CHECK(d == -2250.0);
    CHECK_FALSE(pi::PackScalar(Member("F", "FloatProperty", 0, 4), "1e39", span(4)));
    CHECK_FALSE(pi::PackScalar(Member("F", "FloatProperty", 0, 4), "nan", span(4)));
    CHECK(pi::PackScalar(Member("I", "IntProperty", 0, 4), "-7", span(4)));
    int32_t i; memcpy(&i, b.data(), 4); CHECK(i == -7);
    CHECK_FALSE(pi::PackScalar(Member("I", "Int8Property", 0, 1), "128", span(1)));
    CHECK(pi::PackScalar(Member("I", "Int8Property", 0, 1), "-128", span(1)));
    CHECK(pi::PackScalar(Member("U", "UInt16Property", 0, 2), "0xffff", span(2)));
    CHECK_FALSE(pi::PackScalar(Member("U", "UInt16Property", 0, 2), "65536", span(2)));
    CHECK_FALSE(pi::PackScalar(Member("U", "UInt32Property", 0, 4), "-1", span(4)));
    CHECK(pi::PackScalar(Member("E", "EnumProperty", 0, 1), "3", span(1)));
    CHECK(b[0] == 3);
    // A bitfield bool sets only its mask; a native bool (mask 0xff) the byte.
    auto flag = Member("B", "BoolProperty", 0, 1);
    flag.boolMask = 0x04;
    std::fill(b.begin(), b.end(), 0);
    CHECK(pi::PackScalar(flag, "true", std::span(b).first(1)));
    CHECK(b[0] == 0x04);
    flag.boolMask = 0xff;
    CHECK(pi::PackScalar(flag, "false", std::span(b).first(1)));
    CHECK(b[0] == 0);
    CHECK_FALSE(pi::PackScalar(flag, "yes", std::span(b).first(1)));
    CHECK_FALSE(pi::PackScalar(Member("S", "StrProperty", 0, 16), "x", span(8)));

    // FVector of doubles; a struct holding one nests.
    const Ref vectorType{0x1000, 1, 0}, outerType{0x2000, 2, 0};
    const std::vector<ue::Field> vector{Member("X", "DoubleProperty", 0, 8), Member("Y", "DoubleProperty", 8, 8),
        Member("Z", "DoubleProperty", 16, 8)};
    auto inner = Member("Where", "StructProperty", 8, 24);
    inner.structType = vectorType;
    const std::vector<ue::Field> outer{Member("Scale", "FloatProperty", 0, 4), inner};
    const auto fieldsOf = [&](const Ref& type) -> const std::vector<ue::Field>* {
        return type == vectorType ? &vector : type == outerType ? &outer : nullptr;
    };
    const auto noNames = [](std::string_view, int32_t&, uint32_t&) { return false; };
    auto location = Member("Location", "StructProperty", 0, 24);
    location.structType = vectorType;
    std::vector<uint8_t> block(32, 0);
    double xyz[3];
    CHECK(pi::PackValue(location, "(X=1, Y=-2.5, Z=3)", std::span(block).first(24), fieldsOf, noNames));
    memcpy(xyz, block.data(), 24);
    CHECK((xyz[0] == 1 && xyz[1] == -2.5 && xyz[2] == 3));
    CHECK(pi::PackValue(location, "(4,5,6)", std::span(block).first(24), fieldsOf, noNames));
    memcpy(xyz, block.data(), 24);
    CHECK((xyz[0] == 4 && xyz[1] == 5 && xyz[2] == 6));
    CHECK(pi::PackValue(location, "(Z=9)", std::span(block).first(24), fieldsOf, noNames)); // Others keep their bytes.
    memcpy(xyz, block.data(), 24);
    CHECK((xyz[0] == 4 && xyz[2] == 9));
    CHECK_FALSE(pi::PackValue(location, "(1,2,3,4)", std::span(block).first(24), fieldsOf, noNames));
    CHECK_FALSE(pi::PackValue(location, "(W=1)", std::span(block).first(24), fieldsOf, noNames));
    CHECK_FALSE(pi::PackValue(location, "(X=1, X=2)", std::span(block).first(24), fieldsOf, noNames));
    CHECK_FALSE(pi::PackValue(location, "(1,2", std::span(block).first(24), fieldsOf, noNames));
    CHECK_FALSE(pi::PackValue(location, "1,2,3", std::span(block).first(24), fieldsOf, noNames));
    auto nested = Member("Box", "StructProperty", 0, 32);
    nested.structType = outerType;
    std::fill(block.begin(), block.end(), 0);
    CHECK(pi::PackValue(nested, "(Scale=2, Where=(7, 8, 9))", std::span(block), fieldsOf, noNames));
    float scale; memcpy(&scale, block.data(), 4); memcpy(xyz, block.data() + 8, 24);
    CHECK((scale == 2 && xyz[0] == 7 && xyz[2] == 9));
    CHECK(pi::PackValue(nested, "(3, (1, 1, 1))", std::span(block), fieldsOf, noNames));
    auto unknown = Member("Other", "StructProperty", 0, 8);
    unknown.structType = {0x3000, 3, 0};
    CHECK_FALSE(pi::PackValue(unknown, "(1)", std::span(block).first(8), fieldsOf, noNames));
}

TEST_CASE("typed call inputs take names, and a tag as its name alone") {
    namespace pi = pack_input;
    std::string asked;
    const auto pool = [&](std::string_view text, int32_t& index, uint32_t& number) {
        asked = text;
        if (text != "Some.Tag.Name") return false;
        index = 0x1234;
        number = 0;
        return true;
    };
    const Ref tagType{0x4000, 4, 0}, holderType{0x5000, 5, 0};
    const std::vector<ue::Field> tag{Member("TagName", "NameProperty", 0, 8)};
    auto tagMember = Member("Tag", "StructProperty", 8, 8);
    tagMember.structType = tagType;
    const std::vector<ue::Field> holder{Member("Count", "IntProperty", 0, 4), tagMember};
    const auto fieldsOf = [&](const Ref& type) -> const std::vector<ue::Field>* {
        return type == tagType ? &tag : type == holderType ? &holder : nullptr;
    };
    std::vector<uint8_t> block(16, 0xcc);
    const auto nameAt = [&](size_t at) { int32_t i; uint32_t n; memcpy(&i, &block[at], 4); memcpy(&n, &block[at + 4], 4);
        return std::pair{i, n}; };
    // A name parameter, a tag parameter by its name, and a tag inside a struct.
    CHECK(pi::PackValue(Member("Name", "NameProperty", 0, 8), "Some.Tag.Name", std::span(block).first(8), fieldsOf, pool));
    CHECK(nameAt(0) == std::pair{0x1234, 0u});
    auto tagParam = Member("ResourceTag", "StructProperty", 0, 8);
    tagParam.structType = tagType;
    std::fill(block.begin(), block.end(), 0xcc);
    CHECK(pi::PackValue(tagParam, " Some.Tag.Name ", std::span(block).first(8), fieldsOf, pool));
    CHECK(nameAt(0) == std::pair{0x1234, 0u});
    CHECK(pi::PackValue(tagParam, "(TagName=Some.Tag.Name)", std::span(block).first(8), fieldsOf, pool));
    auto holderParam = Member("Entry", "StructProperty", 0, 16);
    holderParam.structType = holderType;
    std::fill(block.begin(), block.end(), 0);
    CHECK(pi::PackValue(holderParam, "(Count=2, Tag=Some.Tag.Name)", std::span(block), fieldsOf, pool));
    CHECK(nameAt(8) == std::pair{0x1234, 0u});
    // A name the pool lacks is refused, and the resolver saw its exact text.
    CHECK_FALSE(pi::PackValue(tagParam, "Missing.Tag", std::span(block).first(8), fieldsOf, pool));
    CHECK(asked == "Missing.Tag");
    CHECK_FALSE(pi::PackValue(tagParam, "", std::span(block).first(8), fieldsOf, pool));
    // Only a struct of one name takes a bare value.
    auto vectorish = Member("Holder", "StructProperty", 0, 16);
    vectorish.structType = holderType;
    CHECK_FALSE(pi::PackValue(vectorish, "Some.Tag.Name", std::span(block), fieldsOf, pool));
}

TEST_CASE("a watched field takes its element type from reflection") {
    using watch_field::FieldType;
    CHECK(WatchTypeName(FieldType(Member("Health", "DoubleProperty", 0, 8), nullptr)) == "f64"); // A Blueprint float.
    CHECK(WatchTypeName(FieldType(Member("Speed", "FloatProperty", 0, 4), nullptr)) == "f32");
    CHECK(WatchTypeName(FieldType(Member("Count", "IntProperty", 0, 4), nullptr)) == "i32");
    CHECK(WatchTypeName(FieldType(Member("Mode", "EnumProperty", 0, 1), nullptr)) == "u8");
    CHECK(WatchTypeName(FieldType(Member("bOn", "BoolProperty", 0, 1), nullptr)) == "u8");
    CHECK(WatchTypeName(FieldType(Member("Owner", "ObjectProperty", 0, 8), nullptr)) == "ptr");
    CHECK(FieldType(Member("Name", "NameProperty", 0, 8), nullptr) == 0);     // Raw bytes.
    CHECK(FieldType(Member("Odd", "FloatProperty", 0, 8), nullptr) == 0);     // Size disagrees with the type.
    const std::vector<ue::Field> vector{Member("X", "DoubleProperty", 0, 8), Member("Y", "DoubleProperty", 8, 8),
        Member("Z", "DoubleProperty", 16, 8)};
    CHECK(WatchTypeName(FieldType(Member("Location", "StructProperty", 0, 24), &vector)) == "f64");
    const std::vector<ue::Field> mixed{Member("A", "FloatProperty", 0, 4), Member("B", "IntProperty", 4, 4)};
    CHECK(FieldType(Member("Pair", "StructProperty", 0, 8), &mixed) == 0);
    const std::vector<ue::Field> gapped{Member("A", "FloatProperty", 0, 4), Member("B", "FloatProperty", 8, 4)};
    CHECK(FieldType(Member("Gap", "StructProperty", 0, 12), &gapped) == 0);
    CHECK(FieldType(Member("Unknown", "StructProperty", 0, 24), nullptr) == 0);
}

TEST_CASE("a const reference parameter is an input") {
    CHECK(ue::InputParameter(ue::Param));
    CHECK_FALSE(ue::InputParameter(ue::Param | ue::Output));
    CHECK_FALSE(ue::InputParameter(ue::Param | ue::Output | ue::Reference)); // in/out ref: filled by the call.
    CHECK(ue::InputParameter(ue::Param | ue::Output | ue::Reference | ue::Const)); // const FVector&
    CHECK_FALSE(ue::InputParameter(ue::Param | ue::Output | ue::Return | ue::Reference | ue::Const));
}

TEST_CASE("a typed watch range is summarised per value") {
    std::vector<shape::WatchRange> ranges(3);
    ranges[0].type = "f64";
    ranges[1].type = "i32[2]";      // ranges[2] is raw: no summary.
    std::vector<shape::WatchSample> rows(3);
    rows[0].us = 0;
    rows[0].values = std::vector<Json>{41.9, Json::array({30452, 30549}), "01 02"};
    rows[1].us = 500000;
    rows[1].values = std::vector<Json>{nullptr, Json::array({30452, 30549}), "01 02"};  // Unreadable once.
    rows[2].us = 2000000;
    rows[2].values = std::vector<Json>{41.8, Json::array({30549, 30650}), "01 03"};
    const auto summary = shape::SummariseWatch(ranges, rows);
    REQUIRE(summary.size() == 3);
    CHECK(summary[0].range == 0);
    CHECK_FALSE(summary[0].element);
    CHECK(summary[0].samples == 2);
    CHECK(summary[0].first == doctest::Approx(41.9));
    CHECK(summary[0].least == doctest::Approx(41.8));
    CHECK(*summary[0].perSecond == doctest::Approx(-0.05));   // -0.1 over 2 s.
    CHECK(summary[1].element == 0u);
    CHECK(summary[2].element == 1u);
    CHECK(summary[2].greatest == 30650);
    CHECK(*summary[2].perSecond == doctest::Approx(50.5));
    CHECK(summary[0].changes == 1);  // 41.9 -> 41.8 (the unreadable sample does not count).
    CHECK(summary[1].changes == 1);
    CHECK(Checked(shape::WatchCapture{false, std::nullopt, 9, 2000, 1000, std::nullopt, ranges, rows, std::nullopt,
        std::nullopt, std::nullopt, summary})["summary"][0]["per_second"].is_number());
}

TEST_CASE("watch rows keep the first sample, each change with its time and the last") {
    const auto row = [](uint64_t us, Json value) {
        shape::WatchSample sample;
        sample.us = us;
        sample.values = std::vector<Json>{std::move(value)};
        return sample;
    };
    // A bullet list: 0, then 1 for 63 ms, then 0 again, sampled every 10 ms.
    std::vector<shape::WatchSample> rows;
    for (uint64_t t = 0; t < 1000; t += 10) rows.push_back(row(t * 1000, t >= 300 && t < 363 ? 1 : 0));
    uint64_t dropped = 9;
    const auto kept = shape::WatchChanges(rows, shape::MaxWatchChangeRows, dropped);
    CHECK(dropped == 0);
    REQUIRE(kept.size() == 4);
    CHECK(kept[0].us == 0);
    CHECK((kept[1].us == 300000 && (*kept[1].values)[0] == 1));
    CHECK((kept[2].us == 370000 && (*kept[2].values)[0] == 0));
    CHECK(kept[3].us == 990000);  // The last sample, unchanged, ends the capture.
    // A gap's lost and resumed rows always stay, and so does the row after a gap.
    std::vector<shape::WatchSample> gap{row(0, 5), row(10, 5), shape::WatchSample{}, row(30, 5), row(40, 5)};
    gap[2].us = 20;
    gap[2].lost = true;
    gap[3].resumed = true;
    CHECK(shape::WatchChanges(gap, 200, dropped).size() == 4);
    // A value that changes every sample is cut to the limit, half from each end.
    std::vector<shape::WatchSample> moving;
    for (uint64_t i = 0; i < 1000; ++i) moving.push_back(row(i, static_cast<double>(i)));
    const auto cut = shape::WatchChanges(moving, 200, dropped);
    CHECK(cut.size() == 200);
    CHECK(dropped == 800);
    CHECK(cut[99].us == 99);
    CHECK(cut[100].us == 900);
    CHECK(cut.back().us == 999);
    CHECK(shape::WatchChanges({}, 200, dropped).empty());
}

TEST_CASE("each groups its columns' spans into passes of at most 64 bytes") {
    using Spans = std::vector<std::pair<uint32_t, uint32_t>>;
    // Near fields share a pass; a far one starts its own; overlaps merge.
    CHECK(shape::EachPasses({{0x130, 0x134}, {0x100, 0x108}, {0x134, 0x138}, {0x400, 0x404}}, 64) ==
        Spans{{0x100, 0x138}, {0x400, 0x404}});
    CHECK(shape::EachPasses({{0, 8}, {4, 12}}, 64) == Spans{{0, 12}});
    // Exactly 64 bytes fits; one more starts a pass.
    CHECK(shape::EachPasses({{0, 4}, {60, 64}}, 64) == Spans{{0, 64}});
    CHECK(shape::EachPasses({{0, 4}, {61, 65}}, 64) == Spans{{0, 4}, {61, 65}});

    shape::Each each;
    each.className = "Pawn";
    each.classRef = RefId{"0x1#1:0"};
    each.columns = {"Health", "Controller.PlayerState"};
    each.mode = "field";
    each.rows.push_back({RefId{"0x10#2:1"}, std::string("BP_Pawn_C_0"), {100.0, nullptr}, {}});
    each.errors = std::vector<shape::EachError>{{RefId{"0x10#2:1"}, 1, "null pointer at Controller"}};
    const auto json = Checked(each);
    CHECK(json["rows"][0] == Json{{"ref", "0x10#2:1"}, {"name", "BP_Pawn_C_0"}, {"values", Json::array({100.0, nullptr})}});
    CHECK(json["errors"][0]["column"] == 1);
    CHECK(json["same_tick"] == false);
}

TEST_CASE("get rows: every field is name, offset and value; a path is path, value and address") {
    shape::Get dump;
    shape::GetRow row;
    row.name = "MaxHealth";
    row.raw = "_aaabcd";
    row.offset = 308;
    row.value = 100;
    dump.rows.push_back(row);
    row.raw.reset();
    row.name = "Flags";
    row.truncated = true;
    row.meta = MakeFieldInfo(Field("Flags", 0x60, 1, "ByteProperty"));
    dump.rows.push_back(row);
    const auto json = Checked(dump);
    CHECK(json["rows"][0] == Json{{"name", "MaxHealth"}, {"raw", "_aaabcd"}, {"offset", "0x134"}, {"value", 100}});
    CHECK(json["rows"][1]["truncated"] == true);    // Only when true, and the layout only with meta.
    CHECK(json["rows"][1]["meta"]["type"] == "ByteProperty");

    shape::Get get;
    get.ref = RefId{"0x10#1:1"};
    shape::GetRow value;
    value.path = "A.B";
    value.value = 1.5;
    value.address = Addr(0x18);
    get.rows.push_back(value);
    shape::GetRow failed;
    failed.path = "C";
    failed.error = "no field C";
    get.rows.push_back(failed);
    const auto got = Checked(get);
    CHECK(got["rows"][0] == Json{{"path", "A.B"}, {"value", 1.5}, {"address", "0x18"}});
    CHECK(got["rows"][1] == Json{{"path", "C"}, {"error", "no field C"}});    // A failed path keeps its place.
}

TEST_CASE("get types, func and its signature") {
    const std::vector<ue::Field> fields{Field("_aaabcd", 0x100, 4, "FloatProperty")};
    const auto table = Checked(MakeTypes(Pawn, fields, [](const ue::Field&) { return std::string_view("Health"); }));
    CHECK(table["rows"][0] == Json{{"name", "Health"}, {"raw", "_aaabcd"}, {"owner", "Pawn"}, {"type", "FloatProperty"},
        {"offset", "0x100"}, {"size", 4}});
    CHECK(table["capped"] == false);

    const ue::FunctionInfo function{0x2000, 0x7ff600001000, 0x400, true, "Pawn", "GetSpeed"};
    const std::vector<Module> modules{{"game.exe", 0x7ff600000000, 0x100000}};
    const auto located = Checked(MakeFunc(Pawn, function, modules));
    CHECK(located["exec"] == "0x7ff600001000");
    CHECK(located["rva"] == "0x1000");
    CHECK(Checked(MakeFunc(Pawn, function, {}, "modules unavailable"))["module_error"] == "modules unavailable");
    auto declared = function;
    declared.interface = true;
    declared.interfaceOffset = 0xF0;
    declared.slot = 0xA8;
    declared.implementation = 0x7ff600004000;
    const auto viaInterface = Checked(MakeFunc(Pawn, declared, modules));
    CHECK(viaInterface["interface"] == Json{{"offset", "0xf0"}, {"slot", 21}, {"implementation", "0x7ff600004000"},
        {"implementation_module", "game.exe"}, {"implementation_rva", "0x4000"}});
    declared.implementation = 0;
    CHECK(Checked(MakeFunc(Pawn, declared, modules))["interface"] == Json{{"offset", "0xf0"}, {"slot", 21}});
    auto native = function;
    native.exec = 0;
    const auto unlocated = Checked(MakeFunc(Pawn, native, modules));
    CHECK_FALSE(unlocated.contains("exec")); // No exec entry: absent, with nothing to locate.
    CHECK_FALSE(unlocated.contains("module"));

    ue::Signature signature{8, {Field("ReturnValue", 0, 4, "FloatProperty")}};
    signature.parameters[0].parameterFlags = ue::Return;
    auto withSignature = MakeFunc(Pawn, function, modules);
    AddSignature(withSignature, signature);
    const auto params = Checked(std::move(withSignature));
    CHECK(params["block_size"] == 8);
    CHECK(params["params"][0]["direction"] == "return");
    CHECK(params["params"][0]["name"] == "ReturnValue");
    CHECK_FALSE(params.contains("params_error"));
}

TEST_CASE("addresses inside a module carry module and rva, others nothing") {
    const std::vector<Module> modules{{"game.exe", 0x7ff600000000, 0x100000}, {"engine.dll", 0x7ffa00000000, 0x2000}};
    CHECK(ToJson(Locate(0x7ffa00001ff8, modules)) == Json{{"module", "engine.dll"}, {"rva", "0x1ff8"}});
    CHECK(ToJson(Locate(0x7ff600000000, modules)) == Json{{"module", "game.exe"}, {"rva", "0x0"}});
    CHECK(ToJson(Locate(0x7ffa00002000, modules)).empty()); // One past the image.
    CHECK(ToJson(Locate(0x1aac568d980, modules)).empty());  // Heap.
    CHECK(ToJson(Locate(0, modules)).empty());
    CHECK(ToJson(Locate(0x7ff600000010, {})).empty());      // List unknown.
}

TEST_CASE("peek labels each qword by the first that holds: object, module, heap, value") {
    const std::vector<Module> modules{{"game.exe", 0x7ff600000000, 0x100000}};
    PeekValue object{0x7ff600000010, 0x1aac568d980, true};
    object.object = MakeObject(ue::Object{Pawn, "Pawn_0", "Pawn"});
    object.readable = true;
    object.name = "Ignored";
    auto row = Checked(MakePeekRow(object, modules));
    CHECK(row["label"] == "object");
    CHECK(row["object"]["name"] == "Pawn_0");
    CHECK(row["rva"] == "0x10");
    CHECK_FALSE(row.contains("name_hint"));

    PeekValue vtable{0x1aac568d980, 0x7ff600004000, true};
    row = Checked(MakePeekRow(vtable, modules));
    CHECK(row == Json{{"address", "0x1aac568d980"}, {"value", "0x7ff600004000"}, {"label", "module"},
        {"value_module", "game.exe"}, {"value_rva", "0x4000"}});

    // A value that decodes as a name and is also a readable pointer is a pointer, with no hint.
    PeekValue heap{0x1000, 0x1aac568d980, true};
    heap.readable = true;
    heap.name = "SomeName";
    row = Checked(MakePeekRow(heap, modules));
    CHECK(row == Json{{"address", "0x1000"}, {"value", "0x1aac568d980"}, {"label", "heap"}});

    PeekValue tag{0x1008, 0x23a0f, true};
    tag.name = "Attribute.Gameplay.Weapon.SpreadMultiplier";
    row = Checked(MakePeekRow(tag, modules));
    CHECK(row["label"] == "value");
    CHECK(row["f32"].size() == 2);
    CHECK(row.contains("f64"));
    CHECK(row["name_hint"] == "Attribute.Gameplay.Weapon.SpreadMultiplier");

    PeekValue unreadablePointer{0x1010, 0x1aac568d980, true}; // Pointer-shaped but not readable: a value.
    CHECK(Checked(MakePeekRow(unreadablePointer, modules))["label"] == "value");
    CHECK(Checked(MakePeekRow(PeekValue{0x1018}, modules)) == Json{{"address", "0x1018"}, {"label", "unreadable"}});
    PeekValue zero{0x1020, 0, true};
    CHECK(Checked(MakePeekRow(zero, modules))["value"] == "0x0");

    const auto label = Checked(MakeLabel(modules[0], 0x6543210f, 0x7ff600001234, "FName_Tag", "global"));
    CHECK(label == Json{{"module", "game.exe"}, {"module_size", 0x100000}, {"timestamp", 0x6543210f}, {"rva", "0x1234"},
        {"name", "FName_Tag"}, {"kind", "global"}});
}

TEST_CASE("labels keep one row per RVA, confirmed names first, and one name per vtable") {
    const auto merged = MergeLabels({{0x30, "vtbl_A", "vtable"}, {0x10, "A::execB", "exec"}, {0x10, "FName_Tag", "global"},
        {0x20, "A::I_F", "interface"}, {0x20, "A::execF", "exec"}});
    CHECK(merged == std::vector<LabelRow>{{0x10, "FName_Tag", "global"}, {0x20, "A::execF", "exec"}, {0x30, "vtbl_A", "vtable"}});
    const std::vector<std::pair<uint64_t, std::string>> owners{{0x100, "BP_Pawn_C"}, {0x100, "Pawn"}, {0x100, "Character"},
        {0x200, "BP_Only_C"}};
    CHECK(VtableLabels(owners) == std::vector<LabelRow>{{0x100, "vtbl_Pawn", "vtable"}, {0x200, "vtbl_BP_Only_C", "vtable"}});
}

TEST_CASE("a labels line is read back only for the same build") {
    const Module module{"Game.exe", 0x7ff600000000, 0x100000};
    const auto line = Checked(MakeLabel(module, 7, 0x7ff600001234, "FName_Tag", "global")).dump();
    const auto row = ParseLabel(line, module, 7);
    REQUIRE(row.has_value());
    CHECK(*row == LabelRow{0x1234, "FName_Tag", "global"});
    CHECK_FALSE(ParseLabel(line, module, 8).has_value());                                   // Another timestamp.
    CHECK_FALSE(ParseLabel(line, Module{"Game.exe", 0x7ff600000000, 0x200000}, 7).has_value()); // Another size.
    CHECK(ParseLabel(line, Module{"GAME.EXE", 0x7ff700000000, 0x100000}, 7).has_value());   // Base moves; RVA stays.
    CHECK_FALSE(ParseLabel("not json", module, 7).has_value());
}

TEST_CASE("a name given in the static tool outranks every derived one, and an export keeps what it does not derive") {
    CHECK(MergeLabels({{0x10, "A::execB", "exec"}, {0x10, "Ricochet_Resolve", "user"}, {0x10, "GTag", "global"}}) ==
          std::vector<LabelRow>{{0x10, "Ricochet_Resolve", "user"}});
    const Module module{"Game.exe", 0x7ff600000000, 0x100000};
    const std::vector<std::string> lines{
        Checked(MakeLabel(module, 7, module.base + 0x10, "A::execB", "exec")).dump(),
        Checked(MakeLabel(module, 7, module.base + 0x20, "Ricochet_Resolve", "user")).dump(),
        Checked(MakeLabel(module, 7, module.base + 0x30, "GTag", "global")).dump(),
        Checked(MakeLabel(module, 7, module.base + 0x40, "vtbl_A", "vtable")).dump(),
        Checked(MakeLabel(module, 8, module.base + 0x50, "Old", "user")).dump(),   // Another build.
        "not json"};
    const auto kept = KeepLabels(lines, module, 7, [](std::string_view kind) { return kind == "exec"; });
    CHECK(kept.rows == std::vector<LabelRow>{{0x20, "Ricochet_Resolve", "user"}, {0x30, "GTag", "global"},
                                             {0x40, "vtbl_A", "vtable"}});   // exec is derived again; vtable was not asked for.
    REQUIRE(kept.otherBuilds.size() == 1);
    CHECK(kept.otherBuilds[0] == lines[4]);
}

TEST_CASE("name and name-find results") {
    const std::vector<int32_t> indices{5, 6};
    const std::vector<std::string> names{"Health", ""};
    const auto decoded = Checked(MakeNames(indices, names));
    CHECK(decoded["rows"][0] == Json{{"index", 5}, {"name", "Health"}});
    CHECK(decoded["rows"][1] == Json{{"index", 6}}); // No pooled name at that index.

    const ue::NameFindResult found{{{5, "Health"}}, 64};
    const auto page = Checked(MakeNameFind(found, 0));
    CHECK(page["rows"][0]["name"] == "Health");
    CHECK(page["next_cursor"] == 64);
}

namespace {
ue::CensusRow CensusRow(std::string name, Ref ref, uint32_t count) {
    return {{ref, std::move(name), ""}, count, {Ref{0x50000 + ref.address, 900, 1}}};
}
}

TEST_CASE("census rows filter by class name and keep the whole census's totals") {
    const ue::CensusResult census{{CensusRow("BP_Projectile_C", {0x100, 1, 1}, 9),
        CensusRow("Pawn", {0x200, 2, 1}, 3), CensusRow("ProjectileMovementComponent", {0x300, 3, 1}, 2)}, 14};
    auto rows = census.rows;
    KeepMatching(rows, "PROJECTILE", [](const ue::CensusRow& r) -> const std::string& { return r.type.name; });
    REQUIRE(rows.size() == 2);
    const auto result = Checked(MakeCensus("Actor", census, rows, {0, 1}, 5));
    CHECK(result["classes"] == 3);
    CHECK(result["instances"] == 14);
    CHECK(result["rows"].size() == 1); // limit after the filter.
    CHECK(result["rows"][0] == Json{{"class", "BP_Projectile_C"}, {"ref", "0x100#1:1"}, {"count", 9},
        {"samples", {"0x50100#900:1"}}});
    CHECK(result["next_cursor"] == 1);
    CHECK(result["total"] == 2);
    CHECK(result["filter"] == "Actor");
    CHECK(result["hint"] == "start 1 for more, limit 0 for all");
    const auto plain = Checked(MakeCensus("", census, census.rows, {}, 5));
    CHECK_FALSE(plain.contains("hint"));
    CHECK_FALSE(plain.contains("next_cursor"));
    CHECK_FALSE(plain.contains("filter"));
    CHECK(plain["rows"].size() == 3);
}

TEST_CASE("census pages join to every row of a filter, and the last page has no cursor") {
    std::vector<ue::CensusRow> all;
    for (uint32_t i = 0; i < 7; ++i)
        all.push_back(CensusRow((i % 2 ? "Zone" : "Other") + std::to_string(i), {0x100 * (i + 1), static_cast<int32_t>(i), 1}, 10 - i));
    const ue::CensusResult census{all, 49};
    // Stored and read back as a later page would be: the same rows, samples included.
    const auto stored = StoredCensus(MakeCensusBaseline("Actor", census));
    REQUIRE(stored.rows.size() == census.rows.size());
    CHECK(stored.rows[3].samples == census.rows[3].samples);
    CHECK(stored.instances == 49);
    auto rows = stored.rows;
    KeepMatching(rows, "zone", [](const ue::CensusRow& r) -> const std::string& { return r.type.name; });
    REQUIRE(rows.size() == 3);
    std::vector<std::string> seen;
    std::optional<uint32_t> cursor = 0;
    for (int page = 0; page < 5 && cursor; ++page) {
        const auto result = Checked(MakeCensus("Actor", stored, rows, {*cursor, 2}, 0));
        for (const auto& row : result["rows"]) seen.push_back(row["class"]);
        CHECK(result["total"] == 3);
        cursor = result.contains("next_cursor") ? std::optional(result["next_cursor"].get<uint32_t>()) : std::nullopt;
        CHECK(result.contains("hint") == cursor.has_value());
    }
    CHECK(seen == std::vector<std::string>{"Zone1", "Zone3", "Zone5"});
    // Past the end: an empty page, no cursor.
    const auto past = Checked(MakeCensus("Actor", stored, rows, {9, 2}, 0));
    CHECK(past["rows"].empty());
    CHECK_FALSE(past.contains("next_cursor"));
    CHECK(CensusPageProblem(MakeCensusBaseline("Actor", census), std::nullopt, "Actor", false).empty());
    CHECK(CensusPageProblem(MakeCensusBaseline("Actor", census), std::nullopt, "Actor", true) ==
        "no stored census to page: run census with diff for Actor without start first");
    CHECK_FALSE(CensusPageProblem(MakeCensusBaseline("Pawn", census), std::nullopt, "Actor", false).empty());
    CHECK_FALSE(CensusPageProblem(std::nullopt, std::nullopt, "", false).empty());
}

TEST_CASE("census diff keeps grown, shrunk, new and vanished classes, largest change first") {
    const CensusBaseline before{"Actor", {{"Grown", {0x100, 1, 1}, 2}, {"Shrunk", {0x200, 2, 1}, 5},
        {"Same", {0x300, 3, 1}, 4}, {"Vanished", {0x400, 4, 1}, 3}, {"Reused", {0x500, 5, 1}, 1},
        {"Serial", {0x600, 6, 0}, 2}}};
    const ue::CensusResult after{{CensusRow("Grown", {0x100, 1, 1}, 10), CensusRow("Shrunk", {0x200, 2, 1}, 4),
        CensusRow("Same", {0x300, 3, 1}, 4), CensusRow("New", {0x700, 7, 1}, 2),
        CensusRow("Other", {0x800, 5, 2}, 1), CensusRow("Serial", {0x600, 6, 4}, 2)}, 23};
    const auto changes = CensusChanges(before, after);
    std::vector<std::string> names;
    for (const auto& change : changes) names.push_back(change.name);
    // Same and Serial (serial assigned since) are unchanged; slot 5 now holds another class.
    CHECK(names == std::vector<std::string>{"Grown", "Vanished", "New", "Other", "Reused", "Shrunk"});
    CHECK(changes[0].before == 2);
    CHECK(changes[0].after == 10);
    CHECK(changes[0].samples.size() == 1);
    CHECK(changes[1].after == 0);
    CHECK(changes[1].samples.empty()); // Gone: no instances to sample.
    CHECK(changes[2].before == 0);
    const auto result = Checked(MakeCensusDiff(before, after, changes, {}, 5));
    const auto paged = Checked(MakeCensusDiff(before, after, changes, {2, 2}, 5));
    CHECK(paged["rows"][0]["class"] == "New");
    CHECK(paged["rows"][0]["before"] == 0);
    CHECK(paged["rows"][0]["count"] == 2);
    CHECK(paged["next_cursor"] == 4);
    CHECK(paged["total"] == 6);
    CHECK(result["before"]["classes"] == 6);
    CHECK(result["before"]["instances"] == 17);
    CHECK(result["instances"] == 23);
    CHECK(result["filter"] == "Actor");
    CHECK(CensusChanges(before, {{}, 0}).size() == 6);
    CHECK(CensusChanges(MakeCensusBaseline("Actor", after), after).empty());
}

TEST_CASE("census diff needs a baseline under the same filter") {
    CHECK(CensusBaselineProblem(std::nullopt, "Actor").starts_with("no census baseline"));
    const CensusBaseline all{"", {}};
    CHECK(CensusBaselineProblem(all, "Actor") == "the baseline is from census: run census for Actor first");
    CHECK(CensusBaselineProblem(all, "").empty());
    CHECK(CensusBaselineProblem(CensusBaseline{"Actor", {}}, "Actor").empty());
}

TEST_CASE("script and refs results group entries by kind") {
    ue::ScriptResult script{513, {}};
    ue::ScriptEntry name{};
    name.offset = 350;
    name.count = 1;
    name.kind = ue::ScriptKind::Name;
    name.name = 145935;
    strcpy_s(name.text, "Attribute.Gameplay.Character.ReloadSpeedMultiplier");
    ue::ScriptEntry call{};
    call.kind = ue::ScriptKind::Function;
    call.object = Ref{0x7ff4d87a0378, 9, 0};
    strcpy_s(call.text, "BHGameplayEffectLibrary:GetValues");
    ue::ScriptEntry local{};
    local.kind = ue::ScriptKind::Property;
    local.pointer = 0x1aac568d980;
    strcpy_s(local.text, "DoubleProperty PlayRate");
    script.entries = {name, call, local};
    const auto result = Checked(MakeScript(Pawn, "BP_Action_Reload_C:GetReloadSpeedMultiplier", script));
    CHECK(result["names"][0] == Json{{"offset", "0x15e"}, {"count", 1}, {"index", 145935},
        {"name", "Attribute.Gameplay.Character.ReloadSpeedMultiplier"}});
    CHECK(result["functions"][0]["ref"] == "0x7ff4d87a0378#9:0");
    CHECK(result["properties"][0]["address"] == "0x1aac568d980");

    ue::RefsResult refs{{}, false};
    ue::RefsEntry row{};
    row.function = Ref{0x2000, 3, 1};
    strcpy_s(row.owner, "BP_Action_Reload_C");
    strcpy_s(row.name, "GetReloadSpeedMultiplier");
    refs.rows = {row};
    const auto found = Checked(MakeRefs(145935, "Attribute.Gameplay.Character.ReloadSpeedMultiplier", refs));
    CHECK(found["rows"][0] == Json{{"ref", "0x2000#3:1"}, {"owner", "BP_Action_Reload_C"},
        {"name", "GetReloadSpeedMultiplier"}, {"offset", "0x0"}, {"count", 0}});
    CHECK(found["capped"] == false);
    CHECK_FALSE(Checked(MakeRefs(7, "", refs)).contains("name"));
}

TEST_CASE("every line is a result or an error envelope") {
    CHECK(ResultEnvelope("find", Json::object()) == Json{{"command", "find"}, {"ok", true}, {"result", Json::object()}});
    const auto validation = ErrorEnvelope("find", "validation", "bad");
    CHECK(validation == Json{{"command", "find"}, {"ok", false}, {"error", {{"kind", "validation"}, {"message", "bad"}}}});
    const Error refused{Error::Kind::Agent, Status::NotFound, 0, "gone", "not found: gone"};
    const auto agent = ErrorEnvelope("describe", refused);
    CHECK(agent["error"]["kind"] == "agent");
    CHECK(agent["error"]["reason"] == "gone");
    CHECK(agent["error"].contains("status"));
    CHECK(agent["error"].contains("detail"));
}

TEST_CASE("batch results give each command its tick and name the ones that moved it") {
    const auto envelope = [](const char* command) { return Json{{"command", command}, {"ok", true}, {"result", Json::object()}}; };
    // Three single-tick reads.
    std::vector<BatchItemTicks> same{{0, 40, 40}, {40, 40, 40}, {40, 40, 40}};
    auto result = Checked(MakeBatch({envelope("read"), envelope("read"), envelope("get")}, same, 40, 40));
    CHECK(result["same_tick"] == true);
    CHECK(result["all_ok"] == true);
    CHECK(result["spans_ticks"].empty());
    CHECK(result["rows"][1]["tick"] == 40);
    CHECK_FALSE(result["rows"][1].contains("last_tick"));
    // A read, a scan over many ticks, then a read in the tick the scan ended.
    std::vector<BatchItemTicks> skewed{{0, 40, 40}, {40, 41, 355}, {355, 355, 355}};
    result = Checked(MakeBatch({envelope("read"), envelope("each"), envelope("read")}, skewed, 40, 355));
    CHECK(result["same_tick"] == false);
    CHECK(result["first_tick"] == 40);
    CHECK(result["last_tick"] == 355);
    REQUIRE(result["spans_ticks"].size() == 1);
    CHECK(result["spans_ticks"][0] == Json{{"index", 1}, {"command", "each"}});
    CHECK(result["rows"][1]["tick"] == 41);
    CHECK(result["rows"][1]["last_tick"] == 355);
    CHECK_FALSE(result["rows"][2].contains("last_tick"));
    // A command that sent nothing (a schema refusal) has no tick.
    std::vector<BatchItemTicks> none{{40, 0, 0}};
    result = Checked(MakeBatch({envelope("bad")}, none, 40, 40));
    CHECK_FALSE(result["rows"][0].contains("tick"));
}

namespace {
ue::Field Parameter(std::string name, uint8_t flags, std::string type, std::string structName = {}, Ref structType = {}) {
    auto field = Field(std::move(name), 0, 8, std::move(type));
    field.parameterFlags = flags;
    field.structName = std::move(structName);
    field.structType = structType;
    return field;
}
const Ref TagType{0x7000, 20, 1}, ContainerType{0x7100, 21, 1};
const std::vector<ue::Field> TagMembers{Field("TagName", 0, 8, "NameProperty")};
const std::vector<ue::Field> ContainerMembers{Field("GameplayTags", 0, 16, "ArrayProperty"), Field("ParentTags", 16, 16, "ArrayProperty")};
const std::vector<ue::Field>* Structs(const Ref& type) {
    if (type == TagType) return &TagMembers;
    if (type == ContainerType) return &ContainerMembers;
    return nullptr;
}
}

TEST_CASE("an outputs-only call takes a zeroed block; one with inputs names the input") {
    const ue::Signature getter{8, {Parameter("ReturnValue", ue::Param | ue::Output | ue::Return | ue::Scalar, "StructProperty", "Vector")}};
    CHECK(calls::ZeroBlockRefusal(getter).empty());
    const ue::Signature out{24, {Parameter("Location", ue::Param | ue::Output | ue::Plain, "StructProperty", "Vector")}};
    CHECK(calls::ZeroBlockRefusal(out).empty());
    const ue::Signature input{16, {Parameter("Socket", ue::Param | ue::Plain, "NameProperty"),
        Parameter("ReturnValue", ue::Param | ue::Output | ue::Return | ue::Plain, "FloatProperty")}};
    CHECK(calls::ZeroBlockRefusal(input) == "Socket is an input: pass the block (params shows the layout) or Name=value");
    const ue::Signature constRef{8, {Parameter("Tag", ue::Param | ue::Output | ue::Reference, "StructProperty", "GameplayTag", TagType)}};
    CHECK(calls::ZeroBlockRefusal(constRef).starts_with("Tag is an input"));
}

TEST_CASE("each refusals and kept reasons speak of the parameter they name") {
    // A gameplay tag the agent's plain bit misses (not plain old data, yet it owns nothing).
    const ue::Signature tag{8, {Parameter("ReturnValue", ue::Param | ue::Output | ue::Return, "StructProperty", "GameplayTag", TagType)}};
    CHECK(calls::EachRefusal(tag, Structs) ==
        "ReturnValue is a GameplayTag, which the engine does not mark plain data (it may need destroying); "
        "each reads only plain values: use call");
    const ue::Signature container{24, {Parameter("ReturnValue", ue::Param | ue::Output | ue::Return, "StructProperty",
        "GameplayTagContainer", ContainerType)}};
    CHECK(calls::EachRefusal(container, Structs) ==
        "ReturnValue is a GameplayTagContainer, which owns GameplayTags (an array); each reads only plain values: use call");
    CHECK(calls::KeptReason(container, Structs).starts_with("ReturnValue is a GameplayTagContainer, which owns GameplayTags"));
    const ue::Signature text{16, {Parameter("ReturnValue", ue::Param | ue::Output | ue::Return, "StrProperty")}};
    CHECK(calls::KeptReason(text, Structs).starts_with("ReturnValue is a String, which owns memory; the agent keeps"));
    const ue::Signature input{8, {Parameter("Index", ue::Param | ue::Plain, "IntProperty")}};
    CHECK(calls::EachRefusal(input, Structs) == "Index is an input; each calls only functions with no inputs: use call");
    const ue::Signature plain{24, {Parameter("ReturnValue", ue::Param | ue::Output | ue::Return | ue::Plain, "StructProperty", "Vector")}};
    CHECK(calls::EachRefusal(plain, Structs).empty());
    CHECK(calls::KeptReason(plain, Structs).empty());
}

TEST_CASE("stats ranks a session's verbs by output and marks unfiltered, repeated and refused calls") {
    const auto line = [](std::string session, std::string verb, uint64_t out, std::string hash,
                         std::vector<std::string> keys = {}) {
        UsageLine usage;
        usage.time = "2026-01-02T03:04:0" + std::to_string(hash.size()) + "Z";
        usage.session = std::move(session);
        usage.verb = std::move(verb);
        usage.origin = "ue";
        usage.caller = "tool";
        usage.hash = std::move(hash);
        usage.keys = std::move(keys);
        usage.in = 10;
        usage.out = out;
        usage.ms = 2;
        return usage;
    };
    std::vector<UsageLine> lines = {
        line("s1", "get", 5000, "a"),                        // No filter: unfiltered.
        line("s1", "get", 300, "bb", {"match"}),
        line("s1", "get", 5000, "a"),                        // The same input again: repeated, unfiltered.
        line("s1", "find", 200, "ccc"),
        line("s1", "batch", 40, "dddd"),
        line("s1", "read", 100, "eeeee"),
        line("s2", "describe", 9000, "f"),                    // Another session: not in s1's figures.
    };
    lines[3].ok = false;
    lines[3].refused = "schema";
    lines[4].origin = lines[5].origin = "core";
    lines[5].batch = true;                                  // Inside the batch: counted under read.
    const auto stats = MakeStats(lines, std::string("s1"), AllVerbs());
    const auto json = Checked(stats);
    CHECK(json["session"] == "s1");
    CHECK_FALSE(json.contains("never_called"));
    CHECK(stats.totals.calls == 5);                         // The batched read is inside the batch's call.
    CHECK(stats.totals.out == 10640);
    CHECK(stats.totals.outByCaller.at("tool") == 10640);
    CHECK(stats.totals.in == 50);
    CHECK(stats.totals.refused == 1);
    REQUIRE(stats.rows.size() == 4);
    CHECK(stats.rows[0].verb == "get");
    CHECK(stats.rows[0].calls == 3);
    CHECK(stats.rows[0].out == 10300);
    CHECK(stats.rows[0].largest == 5000);
    CHECK(stats.rows[0].unfiltered == 2u);
    CHECK(stats.rows[0].repeated == 1u);
    CHECK(stats.rows[1].verb == "find");
    CHECK(stats.rows[1].unfiltered == 1u);                  // find has cls.
    CHECK(stats.rows[1].refused == 1u);
    CHECK(stats.rows[1].refusedBy->at("schema") == 1);
    const auto read = std::ranges::find(stats.rows, std::string("read"), &shape::StatsVerb::verb);
    REQUIRE(read != stats.rows.end());
    CHECK(read->batched == 1u);
    CHECK_FALSE(read->unfiltered.has_value());              // read has no filter args.
    REQUIRE(stats.largest.size() == 5);
    CHECK(stats.largest[0].verb == "get");
    CHECK(stats.largest[0].out == 5000);

    const auto all = MakeStats(lines, std::nullopt, AllVerbs());
    Checked(all);
    CHECK(all.sessions == 2u);
    CHECK(all.since == "2026-01-02T03:04:01Z");
    CHECK(all.rows[0].verb == "get");                      // 10300 over describe's 9000.
    CHECK(std::ranges::find(*all.neverCalled, "census") != all.neverCalled->end());
    CHECK(std::ranges::find(*all.neverCalled, "get") == all.neverCalled->end());

    const auto none = MakeStats({}, std::string(), AllVerbs());
    CHECK_FALSE(Checked(none).contains("session"));
    CHECK(none.rows.empty());

    // Calls per tool pack, a batch's items included.
    CHECK(stats.totals.callsByPack == std::map<std::string, uint64_t>{{"explore", 6}});
    const std::vector<UsageLine> mixed = {line("p", "disasm", 10, "1"), line("p", "write", 10, "2"),
                                          line("p", "disasm", 10, "3"), line("p", "nonsense", 10, "4")};
    CHECK(MakeStats(mixed, std::string("p"), AllVerbs()).totals.callsByPack ==
          std::map<std::string, uint64_t>{{"native", 2}, {"modify", 1}});
}

TEST_CASE("stats counts digests and what they kept out, and lists and picks sessions") {
    const auto line = [](std::string session, std::string verb, uint64_t out, std::string time, std::string caller = "tool") {
        UsageLine usage;
        usage.time = "2026-01-02T03:04:0" + time + "Z";
        usage.session = std::move(session);
        usage.verb = std::move(verb);
        usage.origin = "ue";
        usage.caller = std::move(caller);
        usage.hash = verb + time;
        usage.out = out;
        return usage;
    };
    std::vector<UsageLine> lines = {
        line("old", "find", 300, "1", "shell"),
        line("new", "census", 500, "2"),                     // Digested: 430000 stood behind it.
        line("new", "census", 20000, "3"),                   // Its next, printed whole.
        line("new", "get", 400, "4"),                       // Digested and unfiltered: no mark.
    };
    lines[1].digest = 430000;
    lines[3].digest = 22000;
    lines[2].keys = {"limit"};
    const auto stats = MakeStats(lines, std::string("new"), AllVerbs());
    Checked(stats);
    CHECK(stats.totals.digested == 2);
    CHECK(stats.totals.saved == (430000 - 500) + (22000 - 400));
    const auto census = std::ranges::find(stats.rows, std::string("census"), &shape::StatsVerb::verb);
    REQUIRE(census != stats.rows.end());
    CHECK(census->digested == 1u);
    CHECK(census->saved == 429500u);
    CHECK_FALSE(census->unfiltered.has_value());              // The digested one is not marked; the rerun had limit.
    const auto dump = std::ranges::find(stats.rows, std::string("get"), &shape::StatsVerb::verb);
    CHECK_FALSE(dump->unfiltered.has_value());
    const auto big = std::ranges::find(stats.largest, uint64_t{500}, &shape::StatsCall::out);
    REQUIRE(big != stats.largest.end());
    CHECK(big->digest == 430000u);

    // sessions: newest first, totals over the whole log, no verb rows.
    const auto list = StatsFor(lines, AllVerbs(), {false, true, {}}, std::nullopt);
    REQUIRE(list.has_value());
    const auto json = Checked(*list);
    REQUIRE(list->sessionRows.has_value());
    REQUIRE(list->sessionRows->size() == 2);
    const auto& newest = list->sessionRows->front();
    CHECK(newest.session == "new");
    CHECK(newest.since == "2026-01-02T03:04:02Z");
    CHECK(newest.until == "2026-01-02T03:04:04Z");
    CHECK(newest.callers == std::vector<std::string>{"tool"});
    CHECK(newest.calls == 3);
    CHECK(newest.toolOut == 20900);
    CHECK(newest.digested == 2);
    CHECK(list->sessionRows->back().session == "old");
    CHECK(list->sessionRows->back().toolOut == 0);
    CHECK(list->totals.calls == 4);
    CHECK(list->rows.empty());
    CHECK_FALSE(json.contains("never_called"));

    // session: that one, or a refusal naming stats --sessions.
    const auto old = StatsFor(lines, AllVerbs(), {false, false, "old"}, std::string("new"));
    REQUIRE(old.has_value());
    CHECK(old->session == "old");
    CHECK(old->totals.calls == 1);
    const auto missing = StatsFor(lines, AllVerbs(), {false, false, "gone"}, std::nullopt);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().find("stats --sessions") != std::string::npos);
    // Neither: the current session, else the log's latest.
    CHECK(StatsFor(lines, AllVerbs(), {}, std::string("old"))->session == "old");
    CHECK(StatsFor(lines, AllVerbs(), {}, std::nullopt)->session == "new");
}

TEST_CASE("a layout line round-trips and names the first thing that differs") {
    const auto written = LayoutJson("Pawn", "Controller", 0x2e8, 8, "ObjectProperty").dump();
    const auto line = ParseLayoutLine(written);
    REQUIRE(line.has_value());
    CHECK(line->owner == "Pawn");
    CHECK(line->offset == 0x2e8);
    CHECK(line->size == 8u);
    CHECK(LayoutProblem(*line, 0x2e8, 8, "ObjectProperty").empty());
    CHECK(LayoutProblem(*line, 0x2f0, 4, "IntProperty") == "offset");
    CHECK(LayoutProblem(*line, 0x2e8, 4, "IntProperty") == "size");
    CHECK(LayoutProblem(*line, 0x2e8, 8, "IntProperty") == "type");
    const auto bare = ParseLayoutLine(R"({"owner": "Pawn", "field": "Controller", "offset": 744})");
    REQUIRE(bare.has_value());                                  // Size and type are optional, offset a plain number.
    CHECK(LayoutProblem(*bare, 744, 4, "IntProperty").empty());
    CHECK_FALSE(ParseLayoutLine(R"({"owner": "Pawn", "field": "X"})").has_value());
    CHECK_FALSE(ParseLayoutLine(R"({"owner": "Pawn", "field": "X", "offset": "nope"})").has_value());
    CHECK_FALSE(ParseLayoutLine("not json").has_value());
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
    CHECK(byPack[ToolPack::Modify] == std::set<std::string>{"write", "writes", "call", "release", "calls-start", "calls-read",
                                                             "calls-stop", "game-thread"});
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
