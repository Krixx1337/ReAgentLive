// reagentlive CLI output budgets (results/budget.h): the size profile, the
// narrower line a digest offers, and the digest that stands for a result
// past its budget. Synthetic results only.
#include "reagentlive/frontends/cli/results/budget.h"
#include "doctest.h"
#include "verbs.h"
#include <map>
#include <string>
#include <vector>

using namespace reagentlive;
using namespace reagentlive::cli;

namespace {
const Verb& Row(std::string_view name) {
    const Verb* verb = FindVerb(AllVerbs(), name);
    REQUIRE(verb);
    return *verb;
}

std::string Text(const Json& value) { return value.dump(-1, ' ', true, Json::error_handler_t::replace); }

// A typed call's result shaped as a vendor list: rows with a tag, a price, a
// transform and a nested array of variations, each with a long icon.
Json VendorCall(int rows) {
    Json list = Json::array();
    for (int i = 0; i < rows; ++i) {
        Json variations = Json::array();
        for (int v = 0; v < 2; ++v)
            variations.push_back({{"Price", 100 + v}, {"Icon", std::string(200, 'a')}});
        list.push_back({{"Tag", "Item.Weapon." + std::to_string(i)}, {"Price", 1000 + i},
            {"Transform", {{"Rotation", {{"X", 0.0}, {"Y", 0.0}, {"Z", 0.0}, {"W", 1.0}}},
                           {"Translation", {{"X", 12.5}, {"Y", -3.25}, {"Z", 100.0}}},
                           {"Scale3D", {{"X", 1.0}, {"Y", 1.0}, {"Z", 1.0}}}}},
            {"Variations", {{"count", 2}, {"capacity", 2}, {"address", "0x1000"}, {"rows", std::move(variations)}}}});
    }
    Json value = {{"count", rows}, {"capacity", rows}, {"address", "0x2000"}, {"rows", std::move(list)}};
    return {{"ref", "0x10#1:2"}, {"function", "Vendor:GetItems"}, {"mode", "typed"}, {"block_size", 16},
        {"hex", "00"}, {"kept", false},
        {"outputs", Json::array({{{"name", "ReturnValue"}, {"direction", "return"}, {"value", std::move(value)},
                                  {"truncated", false}}})}};
}

Json Envelope(Json result) { return {{"command", "call"}, {"ok", true}, {"result", std::move(result)}}; }

Args TypedArgs(const Verb& verb, std::string_view line) {
    std::string_view rest = line;
    text::Next(rest);
    auto args = Tokenise(verb, rest);
    REQUIRE(args);
    return *args;
}

budget::Line CallLine(std::string_view typed, bool reads) {
    budget::Line line;
    line.verb = &Row("call");
    line.args = TypedArgs(Row("call"), typed);
    line.refs = {"0x10#1:2"};
    line.limit = budget::ToolBudget;
    line.reads = reads;
    return line;
}
}

TEST_CASE("profile: sizes per folded path, the main list and its fields") {
    const Json result = VendorCall(50);
    const budget::Profile profile(result);
    CHECK(profile.Chars() == Text(result).size());
    const size_t rows = profile.Rows();
    REQUIRE(rows != SIZE_MAX);
    CHECK(profile.Nodes()[rows].path == "outputs[].value.rows");
    CHECK(profile.Nodes()[rows].count == 50);
    // Every row's tag folds into one path.
    const auto tag = std::ranges::find(profile.Nodes(), "outputs[].value.rows[].Tag", &budget::Node::path);
    REQUIRE(tag != profile.Nodes().end());
    size_t tags = 0;
    for (const auto& row : result["outputs"][0]["value"]["rows"]) tags += Text(row["Tag"]).size() + Text(Json("Tag")).size() + 1;
    CHECK(tag->chars == tags);

    const auto fields = profile.Fields();
    for (const char* path : {"Tag", "Price", "Transform.Rotation.X", "Transform.Scale3D.Z", "Variations.Price", "Variations.Icon"})
        CHECK_MESSAGE(fields.contains(path), path);
    // A nested array's header is not a member.
    CHECK_FALSE(fields.contains("Variations.count"));
    CHECK_FALSE(fields.contains("Variations.address"));

    // The largest part is found below its container, and parts do not overlap.
    const auto largest = profile.Largest(5);
    REQUIRE_FALSE(largest.empty());
    CHECK(largest.front().first == "outputs[].value.rows[].Variations.rows[].Icon");
    for (size_t i = 0; i < largest.size(); ++i)
        for (size_t j = 0; j < largest.size(); ++j)
            if (i != j) CHECK_FALSE(largest[i].first.starts_with(largest[j].first + "."));
}

TEST_CASE("profile: a table row's members sit under its value") {
    Json rows = Json::array();
    for (int i = 0; i < 4; ++i)
        rows.push_back({{"name", "Row_" + std::to_string(i)}, {"address", "0x10"}, {"value", {{"Cost", i}, {"Label", "x"}}}});
    const budget::Profile profile(Json{{"ref", "0x1#1:1"}, {"rows", rows}});
    const auto fields = profile.Fields();
    CHECK(fields.contains("Cost"));
    CHECK(fields.contains("Label"));
    CHECK_FALSE(fields.contains("name"));
}

TEST_CASE("narrow: --fields keeps the members that fit, whole where they can, --limit when they do not fit") {
    const Json result = VendorCall(200);
    const budget::Profile profile(result);
    const auto& call = Row("call");
    const auto changes = budget::Narrow(profile, call, TypedArgs(call, "call 0x10#1:2 Vendor:GetItems --limit 0"),
        budget::ToolBudget);
    REQUIRE_FALSE(changes.empty());
    REQUIRE(changes.front().flag == "--fields");
    const std::string fields = *changes.front().value;
    CHECK(fields.find("Icon") == std::string::npos);
    CHECK(fields.find("Tag") != std::string::npos);
    // The fields kept must fit at the rows shown, or a smaller page follows.
    const auto all = profile.Fields();
    size_t kept = 0;
    std::string_view list = fields;
    while (!list.empty()) {
        const size_t comma = list.find(',');
        const std::string path(list.substr(0, comma));
        for (const auto& [field, chars] : all)
            if (field == path || field.starts_with(path + ".")) kept += chars;
        list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
    }
    if (kept > budget::ToolBudget / 4 * 3) {
        REQUIRE(changes.size() == 2);
        CHECK(changes[1].flag == "--limit");
    }

    // A member that fits is kept whole; one that does not is split.
    size_t sum = 0;
    const std::map<std::string, size_t> small{{"Tag", 10}, {"Box.A", 5}, {"Box.B", 5}, {"Big.A", 50}, {"Big.B", 8}};
    CHECK(budget::detail::Pick(small, 30, sum) == std::vector<std::string>{"Big.B", "Box", "Tag"});
    CHECK(sum == 28);
    // Nothing fits: the smallest alone.
    CHECK(budget::detail::Pick(small, 1, sum) == std::vector<std::string>{"Box.A"});
}

TEST_CASE("narrow: --limit for a list without --fields; --nonzero, then --own, for dump") {
    Json rows = Json::array();
    for (int i = 0; i < 500; ++i) rows.push_back({{"ref", "0x10#1:2"}, {"values", Json::array({std::string(80, 'v')})}});
    const budget::Profile each(Json{{"class", "Pawn"}, {"rows", rows}});
    const auto& eachRow = Row("each");
    const auto changes = budget::Narrow(each, eachRow, TypedArgs(eachRow, "each Pawn Health --limit 0"), budget::ToolBudget);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].flag == "--limit");
    CHECK(std::stoul(*changes[0].value) * 100 < budget::ToolBudget);

    const budget::Profile dump(Json{{"rows", Json::array({{{"name", std::string(30000, 'x')}}})}});
    const auto& dumpRow = Row("dump");
    CHECK(budget::Narrow(dump, dumpRow, TypedArgs(dumpRow, "dump $1"), budget::ToolBudget) ==
        std::vector<budget::Change>{{"--nonzero", {}}});
    CHECK(budget::Narrow(dump, dumpRow, TypedArgs(dumpRow, "dump $1 --nonzero"), budget::ToolBudget) ==
        std::vector<budget::Change>{{"--own", {}}});

    // A verb with no narrowing flag offers no line.
    const budget::Profile status(Json{{"blob", std::string(30000, 'x')}});
    CHECK(budget::Narrow(status, Row("status"), Args{}, budget::ToolBudget).empty());
}

TEST_CASE("next line: literal refs, flags replaced, JSON calls keep their form") {
    const auto& call = Row("call");
    const std::vector<budget::Change> changes{{"--fields", "Tag,Price"}, {"--limit", "40"}};
    const auto line = budget::TypedLine(call, TypedArgs(call, "call $1 Vendor:GetItems --limit 0"), changes, {"0x10#1:2"});
    REQUIRE(line);
    CHECK(*line == "call 0x10#1:2 Vendor:GetItems --fields Tag,Price --limit 40");
    // $N past the list, or a ref with an offset.
    CHECK_FALSE(budget::TypedLine(call, TypedArgs(call, "call $3 Vendor:GetItems"), changes, {"0x10#1:2"}));
    const auto& get = Row("get");
    CHECK(budget::TypedLine(get, TypedArgs(get, "get $1+0x10 Health"), {}, {"0x10#1:2"}) == "get 0x10#1:2+0x10 Health");

    const Json json = budget::CallLine(Json{{"verb", "call"}, {"args", {{"object", "0x10#1:2"}, {"function", "F"}}}, {"meta", true}},
        call, changes);
    CHECK(json["args"]["fields"] == "Tag,Price");
    CHECK(json["args"]["limit"] == 40);
    CHECK(json["meta"] == true);
}

TEST_CASE("digest: a const call gets a narrower line, a call that may change state a file") {
    const Json result = VendorCall(200);
    const size_t chars = Text(Envelope(result)).size();
    REQUIRE(chars > budget::ToolBudget);

    // Runs again: a digest with next, no file, no result.
    Json record = Envelope(result);
    bool kept = false;
    auto reads = budget::Replace(record, chars, CallLine("call $1 Vendor:GetItems --limit 0", true),
        [&](const std::string&) { kept = true; return std::string("unused"); });
    REQUIRE(reads);
    CHECK_FALSE(kept);
    CHECK((*reads)["ok"] == true);
    CHECK_FALSE(reads->contains("result"));
    const Json& digest = (*reads)["digest"];
    CHECK(digest["chars"] == chars);
    CHECK(digest["budget"] == budget::ToolBudget);
    CHECK(digest["rows"]["path"] == "outputs[].value.rows");
    CHECK(digest["rows"]["count"] == 200);
    CHECK_FALSE(digest["largest"].empty());
    CHECK_FALSE(digest.contains("file"));
    REQUIRE(digest.contains("next"));
    const std::string next = digest["next"];
    CHECK(next.starts_with("call 0x10#1:2 Vendor:GetItems "));
    CHECK(next.find("--fields ") != std::string::npos);
    // The line it offers parses again.
    CHECK(Parse(next, {}));
    CHECK(Text(*reads).size() < budget::ToolBudget);

    // Must not run again: the whole envelope goes to the file, no next.
    record = Envelope(result);
    std::string written;
    auto writes = budget::Replace(record, chars, CallLine("call $1 Vendor:GetItems --limit 0", false),
        [&](const std::string& text) { written = text; return std::string("C:/temp/r.json"); });
    REQUIRE(writes);
    CHECK((*writes)["digest"]["file"] == "C:/temp/r.json");
    CHECK_FALSE((*writes)["digest"].contains("next"));
    CHECK(Json::parse(written) == Envelope(result));

    // No file written: the record prints whole.
    record = Envelope(result);
    CHECK_FALSE(budget::Replace(record, chars, CallLine("call $1 Vendor:GetItems", false),
        [](const std::string&) { return std::string(); }));
}

TEST_CASE("digest: kept results are per target pid and agent owner") {
    CHECK(budget::ResultsName(123, 456) == "reagentlive-123-456.results");
    CHECK(budget::ResultsName(123, 456).starts_with(budget::ResultsPrefix(123)));
    CHECK_FALSE(budget::ResultsName(123, 456).starts_with(budget::ResultsPrefix(12)));
    CHECK(budget::ResultsName(123, 456) != budget::ResultsName(123, 457));
}
