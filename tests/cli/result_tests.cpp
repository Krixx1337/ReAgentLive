// reagentlive CLI results: the typed result structs (results/schema.h), the
// values their builders give and the verb table that names them. Builders take
// canned client results, so no agent or object table is needed. Every result
// built here is also checked against its own described type.
#include "reagentlive/frontends/cli/results/ue/call_text.h"
#include "reagentlive/frontends/cli/results/ue/census.h"
#include "reagentlive/frontends/cli/results/labels.h"
#include "reagentlive/frontends/cli/results/result_json.h"
#include "reagentlive/frontends/cli/results/ue/result_json_ue.h"
#include "doctest.h"
#include "verbs.h"
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

TEST_CASE("every verb has a row with a parser and a result type, and no name twice") {
    std::set<std::string> names;
    for (const auto& verb : AllVerbs()) {
        CAPTURE(verb.name);
        CHECK(names.insert(std::string(verb.name)).second);
        for (const auto alias : verb.aliases) CHECK(names.insert(std::string(alias)).second);
        CHECK(verb.parse != nullptr);
        REQUIRE(verb.result != nullptr);
        CHECK(verb.result().kind == schema::Type::Kind::Struct);
        CHECK_FALSE(verb.summary.empty());
        CHECK((verb.origin == "core" || verb.origin == "ue"));
        for (const auto& positional : verb.positionals) CHECK_FALSE(positional.help.empty());
        // The list of a result is `rows`; no result names it otherwise.
        for (const auto& field : verb.result().fields)
            for (const auto old : {"hits", "items", "objects", "entries", "results", "instructions", "slots", "pins", "trials"})
                CHECK(field.key != old);
    }
    CHECK(names.contains("sig-check"));
    CHECK(names.contains("census-diff"));
    CHECK(names.contains("?"));
}

TEST_CASE("help rows come from the table: usage, flags, the result in notation and its type") {
    const Verb* region = FindVerb(AllVerbs(), "region");
    REQUIRE(region);
    const auto brief = Checked(MakeHelpRow(*region, false, false));
    CHECK(brief == Json{{"verb", "region"}, {"origin", "core"}, {"usage", "region <addr>"},
        {"summary", "what the memory at an address is: kind, base, size, protection"}});
    const auto detail = Checked(MakeHelpRow(*region, true, false));
    CHECK(detail["result"].get<std::string>().starts_with("{address: addr, module?: str, rva?: hex, section?: str, "
        "kind: code|rodata|data|mapped|private|reserved|free, base: addr, size: hex"));
    CHECK_FALSE(detail.contains("type"));
    const Verb* search = FindVerb(AllVerbs(), "search");
    REQUIRE(search);
    CHECK(Usage(*search).starts_with("search <hex|u32|i32|u64|ptr|f32|f64|text> <value>... [--wide] [--start <addr>]"));
    const auto full = Checked(MakeHelpRow(*search, true, true));
    CHECK(full["positionals"][1] == Json{{"key", "value"}, {"name", "value"}, {"arity", "some"}, {"type", "text"},
        {"help", "the value to find"}});
    CHECK(full["flags"].back() == Json{{"flag", "--max"}, {"key", "max"}, {"takes", "one"}, {"type", "int"}, {"value", "N"},
        {"default", "64"}, {"help", "rows per page"}});
    CHECK(detail["positionals"][0]["name"] == "addr");
    // Only verbs that write the target's memory or call into it carry the mark.
    CHECK_FALSE(full.contains("writes"));
    std::set<std::string> writers;
    for (const auto& verb : AllVerbs())
        if (verb.writes) writers.insert(std::string(verb.name));
    CHECK(writers == std::set<std::string>{"write", "trial", "trial-clear", "pin", "watch", "each", "call", "release"});
    CHECK(Checked(MakeHelpRow(*FindVerb(AllVerbs(), "write"), false, false))["writes"] == true);
    CHECK(full["flags"][1] == Json{{"flag", "--start"}, {"key", "start"}, {"takes", "one"}, {"type", "addr"}, {"value", "addr"},
        {"help", "start, or a page's next_cursor"}});
    CHECK(full["type"]["struct"]["next_cursor"] == Json{{"type", "addr"}, {"optional", true}});
    CHECK(FindVerb(AllVerbs(), "exit") == FindVerb(AllVerbs(), "quit"));
    // The whole table, and a hash of it that moves with any row.
    const auto table = Checked(MakeHelpTable(AllVerbs(), AllOrigins()));
    CHECK(table["rows"].size() == AllVerbs().size());
    const auto hash = TableHash(AllVerbs(), AllOrigins());
    CHECK(hash.size() == 16);
    auto changed = AllVerbs();
    changed[0].summary = "something else";
    CHECK(TableHash(changed, AllOrigins()) != hash);
}

TEST_CASE("a line's tokens sort into the row's positionals and flags, and errors end with the usage line") {
    const Verb verb{"probe", "core", "", {{"addr", "addr", Arity::One}, {"bytes", "bytes", Arity::Some}},
        {{"--expect", Takes::Some, "bytes", "bytes"}, {"--changed", Takes::Maybe, "path", "file"}, {"--all", Takes::Nothing},
         {"--then", Takes::Rest, "line", "line"}}};
    auto args = Tokenise(verb, "0x10 01 02 --expect 03 04 --all --changed --then call $1 Fn --all");
    REQUIRE(args.has_value());
    CHECK(args->positionals == std::vector<std::string>{"0x10", "01", "02"});
    CHECK(args->From(1) == "01 02");
    CHECK(Args::Joined(*args->Find("--expect")) == "03 04");
    CHECK(args->Has("--all"));
    CHECK(args->Has("--changed"));
    CHECK(args->Value("--changed").empty());       // The optional value was not given.
    CHECK(args->Value("--then") == "call $1 Fn --all"); // The rest of the line, verbatim.
    args = Tokenise(verb, "0x10 \"a b; --all\" --changed f.snap");
    REQUIRE(args.has_value());
    CHECK(args->At(1) == "a b; --all");            // Quoted: exact, and never a flag.
    CHECK_FALSE(args->Has("--all"));
    CHECK(args->Value("--changed") == "f.snap");
    const std::string usage = "; usage: probe <addr> <bytes>... [--expect <bytes>...] [--changed [file]] [--all] [--then line]";
    const Verb strict{"strict", "core", "", {}, {{"--owner", Takes::One, "ref", "ref", "", {}, true}}};
    CHECK(Tokenise(strict, "").error() == "missing --owner; usage: strict --owner <ref>");
    CHECK(Tokenise(verb, "0x10").error() == "too few arguments" + usage);
    CHECK(Tokenise(verb, "0x10 01 --bogus").error() == "unknown flag --bogus (it takes --expect, --changed, --all, --then)" + usage);
    CHECK(Tokenise(verb, "0x10 01 --all --all").error() == "--all given twice" + usage);
    CHECK(Tokenise(verb, "0x10 01 --expect").error() == "--expect needs a value" + usage);
    CHECK(Tokenise(verb, "0x10 01 --then").error() == "--then needs a value" + usage);
    CHECK(Tokenise(verb, "0x10 \"open").error() == "the quote is not closed" + usage);
    const Verb one{"single", "core", "", {{"addr", "addr", Arity::One}, {"len", "len", Arity::Optional}}, {}};
    CHECK(Tokenise(one, "1 2 3").error() == "too many arguments; usage: single <addr> [len]");
    // A parser's own complaint also ends with the usage line; a bare failure is the usage line.
    CHECK(Parse("read 0x10 0x", {}).error() == "len: '0x' is not a number; usage: read <addr> [len] [--type type]");
    CHECK(Parse("read zz", {}).error() == "usage: read <addr> [len] [--type type]");
    CHECK(Parse("frobnicate", {}).error() == "unknown command 'frobnicate' (try help)");
}

TEST_CASE("every argument has a key, a type from the vocabulary, and keys are unique per verb") {
    const std::set<std::string_view> vocabulary{"addr", "ref", "int", "dur", "bytes", "pattern", "text", "str", "path",
        "layout", "range", "span", "line", "calls"};
    for (const auto& verb : AllVerbs()) {
        CAPTURE(verb.name);
        std::set<std::string_view> keys;
        for (const auto& positional : verb.positionals) {
            CHECK(keys.insert(positional.key).second);
            CHECK((vocabulary.contains(positional.type) || arg::Enum(positional.type)));
        }
        for (const auto& flag : verb.flags) {
            CHECK(keys.insert(FlagKey(flag)).second);
            CHECK(((flag.takes == Takes::Nothing) == (flag.type == "bool")));
            CHECK((flag.type == "bool" || vocabulary.contains(flag.type) || arg::Enum(flag.type)));
        }
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

TEST_CASE("an argument of type int or an enum is checked against the row, typed or as a JSON call") {
    CHECK(Parse("read 0x10 zz", {}).error() == "len: 'zz' is not a number; usage: read <addr> [len] [--type type]");
    CHECK(Parse("search u16 5", {}).error().starts_with("hex|u32|i32|u64|ptr|f32|f64|text: 'u16' is not one of"));
    CHECK(Parse("touches 0x10 0x538 x1", {}).error().starts_with("offset: 'x1' is not a number"));
    CHECK(Parse("census --limit many", {}).error().starts_with("--limit: 'many' is not a number"));
    const Verb* read = FindVerb(AllVerbs(), "read");
    CHECK(CallArgs(*read, Json{{"addr", "0x10"}, {"len", "zz"}}).error().starts_with("len: 'zz' is not a number"));
}

TEST_CASE("a JSON call's named arguments land where a typed line's tokens do") {
    const auto same = [](std::string_view line, const Json& call) {
        CAPTURE(line);
        std::string_view rest = line;
        const Verb* verb = FindVerb(AllVerbs(), text::Next(rest));
        REQUIRE(verb);
        const auto typed = Tokenise(*verb, rest);
        const auto named = CallArgs(*verb, call);
        REQUIRE(typed.has_value());
        REQUIRE(named.has_value());
        CHECK(typed->positionals == named->positionals);
        CHECK(typed->rest == named->rest);
        REQUIRE(typed->flags.size() == named->flags.size());
        for (const auto& given : typed->flags) {
            const auto* other = named->Find(given.flag);
            REQUIRE(other);
            CHECK(other->values == given.values);
            CHECK(other->rest == given.rest);
        }
    };
    same("read 0x10 16 --type u8 phase, pad 3", {{"addr", "0x10"}, {"len", 16}, {"type", "u8 phase, pad 3"}});
    same("read 0x10", {{"addr", 16}});
    same("write 0x10 00 00 80 3f --expect 01 02", {{"addr", "0x10"}, {"data", {"00", "00", "80", "3f"}}, {"expect", {"01", "02"}}});
    same("write 0x10 \"00 00 80 3f\"", {{"addr", "0x10"}, {"data", "00 00 80 3f"}});
    same("read-batch 0x10 8 0x20 4", {{"spans", Json::array({Json::array({"0x10", 8}), Json::array({"0x20", 4})})}});
    same("search text \"a b\" --wide --max 2", {{"type", "text"}, {"value", "a b"}, {"wide", true}, {"max", 2}});
    same("search hex 01 02", {{"type", "hex"}, {"value", {"01", "02"}}, {"heap", false}});
    same("watch 1500ms 0x10:4 0x20:1 --then call 0x10#1:1 Fn 01",
        {{"duration", 1.5}, {"ranges", {"0x10:4", "0x20:1"}}, {"then", "call 0x10#1:1 Fn 01"}});
    same("watch-start 10s 0x10:4", {{"duration", 10}, {"ranges", "0x10:4"}});
    same("dump 0x10#1:1 --changed", {{"target", "0x10#1:1"}, {"changed", true}});
    same("dump 0x10#1:1 --changed f.snap --nonzero", {{"target", "0x10#1:1"}, {"changed", "f.snap"}, {"nonzero", true}});
    same("get 0x10#1:1 A.B C", {{"object", "0x10#1:1"}, {"paths", {"A.B", "C"}}});
    same("name 5 6", {{"indices", {5, 6}}});
    same("census --cls Actor --start 100", {{"cls", "Actor"}, {"start", 100}});
    // A quoted file path with spaces needs no quoting as a value.
    same("sig-file \"my sigs.jsonl\"", {{"file", "my sigs.jsonl"}});

    const Verb* read = FindVerb(AllVerbs(), "read");
    const std::string usage = "; usage: read <addr> [len] [--type type]";
    CHECK(CallArgs(*read, Json{{"addr", "$1"}}).error() ==
        "addr: '$1': a JSON call takes the literal ref a result carries, not $N" + usage);
    CHECK(CallArgs(*read, Json{{"addr", "0x10"}, {"length", 4}}).error() == "unknown argument 'length' (it takes addr, len, type)" + usage);
    CHECK(CallArgs(*read, Json{{"len", 4}}).error() == "len given after a left-out argument" + usage);
    // A chain that follows no pointer is refused from a JSON call as from a typed line.
    const auto chain = CallArgs(*read, Json{{"addr", "0x1000->0x0"}});
    REQUIRE(chain.has_value());
    CHECK(Build(*read, *chain, {}).error().starts_with(text::ChainNoHop));
    // A misspelt key is named as unknown, never read as a left-out argument.
    CHECK(CallArgs(*FindVerb(AllVerbs(), "each"), Json{{"class_", "Pawn"}, {"paths", "Health"}}).error().starts_with(
        "unknown argument 'class_' (it takes cls, paths, seconds)"));
    CHECK(CallArgs(*read, Json::object()).error() == "too few arguments" + usage);
    CHECK(CallArgs(*read, Json{{"addr", {"0x10", "0x20"}}}).error() == "addr: one value, not a list" + usage);
    CHECK_FALSE(CallArgs(*FindVerb(AllVerbs(), "xrefs"), Json{{"addr", "0x10"}, {"all", "yes"}}).has_value());

    // batch: a list of calls, each kept as its JSON text; refused verbs as in a typed batch.
    const Verb* batch = FindVerb(AllVerbs(), "batch");
    Json calls = Json::object();
    calls["calls"] = Json::array({Json{{"verb", "read"}, {"args", Json{{"addr", "0x10"}}}}, Json{{"verb", "base"}}});
    const auto args = CallArgs(*batch, calls);
    REQUIRE(args.has_value());
    REQUIRE(args->calls.size() == 2);
    CHECK(args->calls[0].verb == "read");
    const auto parsed = Build(*batch, *args, {});
    REQUIRE(parsed.has_value());
    CHECK(parsed->fields.size() == 2);
    CHECK(Json::parse(parsed->fields[0])["args"]["addr"] == "0x10");
    Json watchCall = Json::object();
    watchCall["calls"] = Json::array({Json{{"verb", "watch"}}});
    const auto watch = CallArgs(*batch, watchCall);
    REQUIRE(watch.has_value());
    CHECK(Build(*batch, *watch, {}).error().starts_with("not allowed in a batch: watch"));
    CHECK(CallArgs(*batch, Json{{"calls", "read 0x10"}}).error().starts_with("calls: a list of {verb, args}"));
}

TEST_CASE("named types: listed once in the table, by name in the rows that use them") {
    const auto table = Checked(MakeHelpTable(AllVerbs(), AllOrigins()));
    REQUIRE(table.contains("types"));
    for (const auto name : {"Chain", "Object", "Trial", "Pin", "WatchRange", "WatchSample", "ue.Name", "ue.FieldInfo"}) {
        CAPTURE(name);
        CHECK(table["types"].contains(name));
    }
    CHECK(table["types"]["Object"]["notation"] == "{ref: ref, name: str, class: str, class_size?: u32}");
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
        "{ref: ref, name: str, class: str, class_size?: u32}");
    // Origins give their own titles; only origins with rows are listed.
    CHECK(table["origins"] == Json{{{"origin", "core"}, {"title", "Core"}}, {{"origin", "ue"}, {"title", "Unreal pack"}}});
    // Blocking verbs are marked so a tool client can leave them out.
    std::set<std::string> blocking;
    for (const auto& verb : AllVerbs())
        if (verb.blocks) blocking.insert(std::string(verb.name));
    CHECK(blocking == std::set<std::string>{"watch", "record"});
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
    const ue::FunctionList list{{"Pawn:GetSpeed", "Actor:K2_GetActorLocation", "Bare"}, 0};
    const auto result = Checked(MakeFuncs(Pawn, list, 0));
    REQUIRE(result["rows"].size() == 3);
    CHECK(result["rows"][1] == Json{{"owner", "Actor"}, {"name", "K2_GetActorLocation"}});
    CHECK(result["rows"][2] == Json{{"name", "Bare"}});
    CHECK_FALSE(result.contains("next_cursor"));
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
    CHECK(Checked(shape::WatchCapture{false, std::nullopt, 9, 2000, 1000, std::nullopt, ranges, rows, std::nullopt,
        summary})["summary"][0]["per_second"].is_number());
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
    each.rows.push_back({RefId{"0x10#2:1"}, {100.0, nullptr}, {}});
    each.errors = std::vector<shape::EachError>{{RefId{"0x10#2:1"}, 1, "null pointer at Controller"}};
    const auto json = Checked(each);
    CHECK(json["rows"][0] == Json{{"ref", "0x10#2:1"}, {"values", Json::array({100.0, nullptr})}});
    CHECK(json["errors"][0]["column"] == 1);
    CHECK(json["same_tick"] == false);
}

TEST_CASE("compact rows: dump is name, offset and value; get is value and address") {
    shape::Dump dump;
    shape::DumpRow row;
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
    CHECK(json["rows"][0] == Json{{"name", "MaxHealth"}, {"raw", "_aaabcd"}, {"offset", 308}, {"value", 100}});
    CHECK(json["rows"][1]["truncated"] == true);    // Only when true, and the layout only with --meta.
    CHECK(json["rows"][1]["meta"]["type"] == "ByteProperty");

    shape::Get get;
    get.ref = RefId{"0x10#1:1"};
    shape::GetRow value;
    value.value = 1.5;
    value.address = Addr(0x18);
    get.rows.push_back(value);
    shape::GetRow failed;
    failed.error = "no field C";
    get.rows.push_back(failed);
    const auto got = Checked(get);
    CHECK(got["rows"][0] == Json{{"value", 1.5}, {"address", "0x18"}});
    CHECK(got["rows"][1] == Json{{"error", "no field C"}});    // A failed path keeps its place.
}

TEST_CASE("fields, func and params results") {
    const std::vector<ue::Field> fields{Field("Health", 0x100, 4, "FloatProperty")};
    const auto table = Checked(MakeFields(Pawn, fields, [](const ue::Field&) { return std::string("Health"); }));
    CHECK(table["rows"][0] == Json{{"name", "Health"}, {"owner", "Pawn"}, {"type", "FloatProperty"}, {"offset", 0x100},
        {"size", 4}, {"label", "Health"}});
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
    const auto params = Checked(MakeParams(Pawn, function, signature));
    CHECK(params["block_size"] == 8);
    CHECK(params["rows"][0]["direction"] == "return");
    CHECK(params["rows"][0]["name"] == "ReturnValue");
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
    CHECK(result["rows"].size() == 1); // --limit after the filter.
    CHECK(result["rows"][0] == Json{{"class", "BP_Projectile_C"}, {"ref", "0x100#1:1"}, {"count", 9},
        {"samples", {"0x50100#900:1"}}});
    CHECK(result["next_cursor"] == 1);
    CHECK(result["total"] == 2);
    CHECK(result["filter"] == "Actor");
    CHECK(result["hint"] == "--start 1 for more, --limit 0 for all");
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
        "no stored census to page: run census-diff --cls Actor without --start first");
    CHECK_FALSE(CensusPageProblem(MakeCensusBaseline("Pawn", census), std::nullopt, "Actor", false).empty());
    CHECK_FALSE(CensusPageProblem(std::nullopt, std::nullopt, "", false).empty());
}

TEST_CASE("census-diff keeps grown, shrunk, new and vanished classes, largest change first") {
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
    CHECK(paged["rows"][0]["after"] == 2);
    CHECK(paged["next_cursor"] == 4);
    CHECK(paged["total"] == 6);
    CHECK(result["classes"]["before"] == 6);
    CHECK(result["instances"]["before"] == 17);
    CHECK(result["instances"]["after"] == 23);
    CHECK(result["filter"] == "Actor");
    CHECK(CensusChanges(before, {{}, 0}).size() == 6);
    CHECK(CensusChanges(MakeCensusBaseline("Actor", after), after).empty());
}

TEST_CASE("census-diff needs a baseline under the same filter") {
    CHECK(CensusBaselineProblem(std::nullopt, "Actor").starts_with("no census baseline"));
    const CensusBaseline all{"", {}};
    CHECK(CensusBaselineProblem(all, "Actor") == "the baseline is from census: run census --cls Actor first");
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
    CHECK(result["names"][0] == Json{{"offset", 350}, {"count", 1}, {"index", 145935},
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
        {"name", "GetReloadSpeedMultiplier"}, {"offset", 0}, {"count", 0}});
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
    // A command that sent nothing (a syntax error) has no tick.
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
                         std::vector<std::string> flags = {}) {
        UsageLine usage;
        usage.time = "2026-01-02T03:04:0" + std::to_string(hash.size()) + "Z";
        usage.session = std::move(session);
        usage.verb = std::move(verb);
        usage.origin = "ue";
        usage.caller = "tool";
        usage.hash = std::move(hash);
        usage.flags = std::move(flags);
        usage.in = 10;
        usage.out = out;
        usage.ms = 2;
        return usage;
    };
    std::vector<UsageLine> lines = {
        line("s1", "dump", 5000, "a"),                        // No filter: unfiltered.
        line("s1", "dump", 300, "bb", {"--match"}),
        line("s1", "dump", 5000, "a"),                        // The same input again: repeated, unfiltered.
        line("s1", "find", 200, "ccc"),
        line("s1", "batch", 40, "dddd"),
        line("s1", "read", 100, "eeeee"),
        line("s2", "describe", 9000, "f"),                    // Another session: not in s1's figures.
    };
    lines[3].ok = false;
    lines[3].refused = "syntax";
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
    CHECK(stats.rows[0].verb == "dump");
    CHECK(stats.rows[0].calls == 3);
    CHECK(stats.rows[0].out == 10300);
    CHECK(stats.rows[0].largest == 5000);
    CHECK(stats.rows[0].unfiltered == 2u);
    CHECK(stats.rows[0].repeated == 1u);
    CHECK(stats.rows[1].verb == "find");
    CHECK(stats.rows[1].unfiltered == 1u);                  // find has --cls.
    CHECK(stats.rows[1].refused == 1u);
    CHECK(stats.rows[1].refusedBy->at("syntax") == 1);
    const auto read = std::ranges::find(stats.rows, std::string("read"), &shape::StatsVerb::verb);
    REQUIRE(read != stats.rows.end());
    CHECK(read->batched == 1u);
    CHECK_FALSE(read->unfiltered.has_value());              // read has no filter flags.
    REQUIRE(stats.largest.size() == 5);
    CHECK(stats.largest[0].verb == "dump");
    CHECK(stats.largest[0].out == 5000);

    const auto all = MakeStats(lines, std::nullopt, AllVerbs());
    Checked(all);
    CHECK(all.sessions == 2u);
    CHECK(all.since == "2026-01-02T03:04:01Z");
    CHECK(all.rows[0].verb == "dump");                      // 10300 over describe's 9000.
    CHECK(std::ranges::find(*all.neverCalled, "census") != all.neverCalled->end());
    CHECK(std::ranges::find(*all.neverCalled, "dump") == all.neverCalled->end());

    const auto none = MakeStats({}, std::string(), AllVerbs());
    CHECK_FALSE(Checked(none).contains("session"));
    CHECK(none.rows.empty());
}
