// reagentlive CLI output budgets (results/budget.h): the size profile, the
// narrower call a digest offers, and the digest that stands for a result
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

// A call's args as check.h accepts them.
Args ArgsOf(const Verb& verb, const Json& given) {
    auto args = CheckCall(verb, given);
    REQUIRE(args);
    return *args;
}

const Json VendorArgs = {{"object", "0x10#1:2"}, {"function", "Vendor:GetItems"}, {"view", {{"limit", 0}}}};

budget::Line LineOf(bool reads, const Json& given = VendorArgs) {
    budget::Line line;
    line.verb = &Row("call");
    line.args = ArgsOf(Row("call"), given);
    line.call = Json{{"verb", "call"}, {"args", given}};
    line.limit = budget::ToolMaxChars;
    line.reads = reads;
    return line;
}

std::string Joined(const Json& path) {
    std::string out;
    for (const auto& step : path) out += (out.empty() ? "" : ".") + step.get<std::string>();
    return out;
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

    // A value printed as bytes is one member, not its keys (--fields would refuse them).
    Json opaque = Json::array();
    for (int i = 0; i < 3; ++i)
        opaque.push_back({{"name", "R"}, {"address", "0x10"}, {"value", {{"Tags", {{"byte_count", 80}, {"hex", "00"},
            {"truncated", true}}}, {"Speed", {{"non_finite", "nan"}}}, {"Cost", i}}}});
    const auto bytes = budget::Profile(Json{{"rows", opaque}}).Fields();
    CHECK(bytes.contains("Tags"));
    CHECK(bytes.contains("Speed"));
    CHECK_FALSE(bytes.contains("Tags.byte_count"));
    CHECK_FALSE(bytes.contains("Tags.hex"));
}

TEST_CASE("narrow: fields keeps the members that fit, whole where they can, limit when they do not fit") {
    const Json result = VendorCall(200);
    const budget::Profile profile(result);
    const auto changes = budget::Narrow(profile, Row("call"), ArgsOf(Row("call"), VendorArgs), budget::ToolMaxChars);
    REQUIRE_FALSE(changes.empty());
    REQUIRE(changes.front().key == "view.fields");
    const Json& paths = changes.front().value;
    REQUIRE(paths.is_array());
    bool tag = false;
    // The fields kept must fit at the rows shown, or a smaller page follows.
    const auto all = profile.Fields();
    size_t kept = 0;
    for (const auto& steps : paths) {
        const std::string path = Joined(steps);
        CHECK(path.find("Icon") == std::string::npos);
        tag |= path == "Tag";
        for (const auto& [field, chars] : all)
            if (field == path || field.starts_with(path + ".")) kept += chars;
    }
    CHECK(tag);
    if (kept > budget::ToolMaxChars / 4 * 3) {
        REQUIRE(changes.size() == 2);
        CHECK(changes[1].key == "view.limit");
        CHECK(changes[1].value.is_number());
    }

    // A member that fits is kept whole; one that does not is split.
    size_t sum = 0;
    const std::map<std::string, size_t> small{{"Tag", 10}, {"Box.A", 5}, {"Box.B", 5}, {"Big.A", 50}, {"Big.B", 8}};
    CHECK(budget::detail::Pick(small, 30, sum) == std::vector<std::string>{"Big.B", "Box", "Tag"});
    CHECK(sum == 28);
    // Nothing fits: the smallest alone.
    CHECK(budget::detail::Pick(small, 1, sum) == std::vector<std::string>{"Box.A"});
}

TEST_CASE("narrow: limit for a list without fields; nonzero, then own, for get") {
    Json rows = Json::array();
    for (int i = 0; i < 500; ++i) rows.push_back({{"ref", "0x10#1:2"}, {"values", Json::array({std::string(80, 'v')})}});
    const budget::Profile each(Json{{"class", "Pawn"}, {"rows", rows}});
    const auto& eachRow = Row("each");
    const auto changes = budget::Narrow(each, eachRow,
        ArgsOf(eachRow, {{"cls", "Pawn"}, {"paths", Json::array({Json::array({"Health"})})}, {"view", {{"limit", 0}}}}), budget::ToolMaxChars);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].key == "view.limit");
    REQUIRE(changes[0].value.is_number_unsigned());
    CHECK(changes[0].value.get<size_t>() * 100 < budget::ToolMaxChars);

    const budget::Profile all(Json{{"rows", Json::array({{{"name", std::string(30000, 'x')}}})}});
    const auto& getRow = Row("get");
    CHECK(budget::Narrow(all, getRow, ArgsOf(getRow, {{"object", "0x10#1:2"}}), budget::ToolMaxChars) ==
        std::vector<budget::Change>{{"view.nonzero"}});
    CHECK(budget::Narrow(all, getRow, ArgsOf(getRow, {{"object", "0x10#1:2"}, {"view", {{"nonzero", true}}}}), budget::ToolMaxChars) ==
        std::vector<budget::Change>{{"own"}});

    // A verb with no narrowing arg offers no call.
    const budget::Profile status(Json{{"blob", std::string(30000, 'x')}});
    CHECK(budget::Narrow(status, Row("status"), Args{}, budget::ToolMaxChars).empty());
}

TEST_CASE("next call: the changes set in the call's args, its other keys kept") {
    const std::vector<budget::Change> changes{{"view.fields", Json::array({Json::array({"Tag"}), Json::array({"Price"})})},
        {"view.limit", 40}};
    const Json json = budget::CallLine(Json{{"verb", "call"},
        {"args", {{"object", "0x10#1:2"}, {"function", "F"}, {"view", {{"limit", 0}, {"nonzero", true}}}}}, {"meta", true}}, changes);
    CHECK(json["args"]["view"]["fields"] == Json::array({Json::array({"Tag"}), Json::array({"Price"})}));
    CHECK(json["args"]["view"]["limit"] == 40);
    CHECK(json["args"]["view"]["nonzero"] == true);
    CHECK(json["args"]["object"] == "0x10#1:2");
    CHECK(json["meta"] == true);
    // A call without args gets them; an own key stays at the top.
    CHECK(budget::CallLine(Json{{"verb", "get"}}, {{"view.nonzero"}})["args"] == Json{{"view", {{"nonzero", true}}}});
    CHECK(budget::CallLine(Json{{"verb", "get"}}, {{"own"}})["args"] == Json{{"own", true}});
}

TEST_CASE("digest: a const call gets a narrower line, a call that may change state a file") {
    const Json result = VendorCall(200);
    const size_t chars = Text(Envelope(result)).size();
    REQUIRE(chars > budget::ToolMaxChars);

    // Runs again: a digest with next, no file, no result.
    Json record = Envelope(result);
    bool kept = false;
    auto reads = budget::Replace(record, chars, LineOf(true),
        [&](const std::string&) { kept = true; return std::string("unused"); });
    REQUIRE(reads);
    CHECK_FALSE(kept);
    CHECK((*reads)["ok"] == true);
    CHECK_FALSE(reads->contains("result"));
    const Json& digest = (*reads)["digest"];
    CHECK(digest["chars"] == chars);
    CHECK(digest["max_chars"] == budget::ToolMaxChars);
    CHECK(digest["rows"]["path"] == "outputs[].value.rows");
    CHECK(digest["rows"]["count"] == 200);
    CHECK_FALSE(digest["largest"].empty());
    CHECK_FALSE(digest.contains("file"));
    REQUIRE(digest.contains("next"));
    // next is a JSON call: the same call, narrowed, that checks and builds again.
    const std::string next = digest["next"];
    const Json call = Json::parse(next);
    CHECK(call["verb"] == "call");
    CHECK(call["args"]["object"] == "0x10#1:2");
    CHECK(call["args"]["function"] == "Vendor:GetItems");
    CHECK(call["args"]["view"]["fields"].is_array());
    CHECK(Call(next));
    // narrow names the verb's narrowing args by their paths.
    CHECK(digest["narrow"] == Json::array({"view.limit", "view.fields", "view.nonzero"}));
    CHECK(Text(*reads).size() < budget::ToolMaxChars);

    // Must not run again: the whole envelope goes to the file, no next.
    record = Envelope(result);
    std::string written;
    auto writes = budget::Replace(record, chars, LineOf(false),
        [&](const std::string& text) { written = text; return std::string("C:/temp/r.json"); });
    REQUIRE(writes);
    CHECK((*writes)["digest"]["file"] == "C:/temp/r.json");
    CHECK_FALSE((*writes)["digest"].contains("next"));
    CHECK(Json::parse(written) == Envelope(result));

    // No file written: the record prints whole.
    record = Envelope(result);
    CHECK_FALSE(budget::Replace(record, chars, LineOf(false, {{"object", "0x10#1:2"}, {"function", "Vendor:GetItems"}}),
        [](const std::string&) { return std::string(); }));
}

TEST_CASE("digest: status keys survive, a list as its length") {
    Json result = VendorCall(200);
    result["capped"] = true;
    result["unnamed"] = 7;
    result["errors"] = Json::array({{{"column", 0}}, {{"column", 1}}});
    result["tick_accurate"] = false;
    Json record = Envelope(result);
    const auto digest = budget::Replace(record, Text(record).size(), LineOf(true, {{"object", "0x10#1:2"}, {"function", "Vendor:GetItems"}}),
        [](const std::string&) { return std::string(); });
    REQUIRE(digest);
    const Json& status = (*digest)["digest"]["status"];
    CHECK(status["capped"] == true);
    CHECK(status["unnamed"] == 7);
    CHECK(status["errors"] == 2);
    CHECK(status["tick_accurate"] == false);
    CHECK_FALSE(status.contains("omitted"));
    // A file the call wrote and a cut listing's resume address survive too.
    Json listing = VendorCall(200);
    listing["saved"] = "fn.jsonl";
    listing["next_cursor"] = "0x140001000";
    Json written = Envelope(listing);
    const auto kept = budget::Replace(written, Text(written).size(), LineOf(true, {{"object", "0x10#1:2"}, {"function", "Vendor:GetItems"}}),
        [](const std::string&) { return std::string(); });
    REQUIRE(kept);
    CHECK((*kept)["digest"]["status"]["saved"] == "fn.jsonl");
    CHECK((*kept)["digest"]["status"]["next_cursor"] == "0x140001000");
    // A search job's id, state and progress survive, so it can still be polled.
    Json job = VendorCall(200);
    job["job"] = 3;
    job["state"] = "running";
    job["scanned"] = 0.25;
    job["next_cursor"] = "0x7ff000";
    Json polled = Envelope(job);
    const auto still = budget::Replace(polled, Text(polled).size(), LineOf(true, {{"object", "0x10#1:2"}, {"function", "Vendor:GetItems"}}),
        [](const std::string&) { return std::string(); });
    REQUIRE(still);
    const Json& jobStatus = (*still)["digest"]["status"];
    CHECK(jobStatus["job"] == 3);
    CHECK(jobStatus["state"] == "running");
    CHECK(jobStatus["scanned"] == 0.25);
    CHECK(jobStatus["next_cursor"] == "0x7ff000");
    // Every other top-level scalar and count object survives too; lists and structs do not.
    Json plain = VendorCall(200);
    plain["by_kind"] = {{"code", 3}, {"data", 4}};
    plain["path"] = std::string(400, 'p');
    Json printed = Envelope(plain);
    const auto scalars = budget::Replace(printed, Text(printed).size(), LineOf(true, {{"object", "0x10#1:2"}, {"function", "Vendor:GetItems"}}),
        [](const std::string&) { return std::string(); });
    const Json& others = (*scalars)["digest"]["status"];
    CHECK(others["function"] == "Vendor:GetItems");
    CHECK(others["block_size"] == 16);
    CHECK(others["kept"] == false);
    CHECK(others["by_kind"] == Json{{"code", 3}, {"data", 4}});
    CHECK_FALSE(others.contains("outputs"));
    CHECK_FALSE(others.contains("path")); // Past MaxStatusText.
}

TEST_CASE("digest: labels-check keeps its counts and narrows by status before raising max_chars") {
    Json rows = Json::array();
    for (int i = 0; i < 400; ++i)
        rows.push_back({{"name", "Name_" + std::to_string(i) + std::string(60, 'n')}, {"kind", "user"}, {"rva", "0x1000"},
            {"status", i % 50 ? "ok" : i % 100 ? "moved" : "missing"}});
    const Json result = {{"path", "names.jsonl"}, {"module", "x.exe"}, {"timestamp", 1}, {"ok", 392}, {"moved", 4},
        {"missing", 4}, {"ambiguous", 0}, {"invalid", 0}, {"unsigned", 0}, {"searches", 1}, {"rows", rows},
        {"capped", false}, {"scan_ms", 900}};
    Json record = {{"command", "labels-check"}, {"ok", true}, {"result", result}};
    budget::Line line;
    line.verb = &Row("labels-check");
    line.args = ArgsOf(Row("labels-check"), Json::object());
    line.call = Json{{"verb", "labels-check"}, {"args", Json::object()}};
    line.limit = budget::ToolMaxChars;
    const auto digest = budget::Replace(record, Text(record).size(), line, [](const std::string&) { return std::string(); });
    REQUIRE(digest);
    const Json& status = (*digest)["digest"]["status"];
    CHECK(status["ok"] == 392);
    CHECK(status["moved"] == 4);
    CHECK(status["missing"] == 4);
    CHECK(status["searches"] == 1);
    const Json next = Json::parse((*digest)["digest"]["next"].get<std::string>());
    CHECK(next["args"]["status"] == Json::array({"moved", "missing"}));
    CHECK_FALSE(next.contains("max_chars"));
    CHECK((*digest)["digest"]["narrow"] == Json::array({"status", "view.limit"}));
}

TEST_CASE("digest: no change that fits offers the same call with a budget, and names what narrows it") {
    Json properties = Json::array();
    for (int i = 0; i < 300; ++i) properties.push_back({{"property", "DoubleProperty Value_" + std::to_string(i)}, {"count", i}});
    Json record = {{"command", "script"}, {"ok", true}, {"result", {{"function", "F"}, {"properties", std::move(properties)}}}};
    const size_t chars = Text(record).size();
    budget::Line line;
    line.verb = &Row("script");
    const Json given = {{"object", "0x10#1:2"}, {"function", "F"}};
    line.args = ArgsOf(Row("script"), given);
    line.call = Json{{"verb", "script"}, {"args", given}};
    line.limit = 2000;
    const auto digest = budget::Replace(record, chars, line, [](const std::string&) { return std::string(); });
    REQUIRE(digest);
    const Json next = Json::parse((*digest)["digest"]["next"].get<std::string>());
    CHECK(next["verb"] == "script");
    CHECK(next["args"] == Json{{"object", "0x10#1:2"}, {"function", "F"}});
    REQUIRE(next["max_chars"].is_number_unsigned());
    CHECK(next["max_chars"].get<size_t>() >= chars);
    // A decode narrows by text (view.match) or by offset range (to).
    CHECK((*digest)["digest"]["narrow"] == Json::array({"to", "view.match"}));
}

TEST_CASE("digest: kept results are per target pid and agent owner") {
    CHECK(budget::ResultsName(123, 456) == "reagentlive-123-456.results");
    CHECK(budget::ResultsName(123, 456).starts_with(budget::ResultsPrefix(123)));
    CHECK_FALSE(budget::ResultsName(123, 456).starts_with(budget::ResultsPrefix(12)));
    CHECK(budget::ResultsName(123, 456) != budget::ResultsName(123, 457));
}
