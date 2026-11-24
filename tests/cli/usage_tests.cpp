// reagentlive CLI usage: the usage log's lines and what `usage` reports from
// them (state/usage.h, results/usage_report.h). Lines are made here, so no
// console or log file is needed. Every report is checked against its type.
#include "reagentlive/frontends/cli/results/usage_report.h"
#include "doctest.h"
#include "verbs.h"
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

using namespace reagentlive;
using namespace reagentlive::cli;

TEST_CASE("usage ranks a session's verbs by output and marks unfiltered, repeated and refused calls") {
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
        line("s1", "funcs", 150, "gggggg"),                   // No filter, but small: never unfiltered.
    };
    lines[3].ok = false;
    lines[3].refused = "schema";
    lines[4].origin = lines[5].origin = "core";
    lines[5].batch = true;                                  // Inside the batch: counted under read.
    const auto stats = MakeUsage(lines, std::string("s1"), AllVerbs());
    const auto json = Checked(stats);
    CHECK(json["session"] == "s1");
    CHECK_FALSE(json.contains("never_called"));
    CHECK(stats.totals->calls == 6);                         // The batched read is inside the batch's call.
    CHECK(stats.totals->batched == 1);
    CHECK(stats.totals->out == 10790);
    CHECK(stats.totals->outByCaller.at("tool") == 10790);
    CHECK(stats.totals->in == 60);
    CHECK(stats.totals->refused == 1);
    REQUIRE(stats.rows->size() == 5);
    const auto funcs = std::ranges::find(*stats.rows, std::string("funcs"), &shape::UsageVerb::verb);
    REQUIRE(funcs != stats.rows->end());
    CHECK_FALSE(funcs->unfiltered.has_value());
    CHECK((*stats.rows)[0].verb == "get");
    CHECK((*stats.rows)[0].calls == 3);
    CHECK((*stats.rows)[0].out == 10300);
    CHECK((*stats.rows)[0].largest == 5000);
    CHECK((*stats.rows)[0].unfiltered == 2u);
    CHECK((*stats.rows)[0].repeated == 1u);
    CHECK((*stats.rows)[1].verb == "find");
    CHECK_FALSE((*stats.rows)[1].unfiltered.has_value());      // Refused: never a filter mark.
    CHECK((*stats.rows)[1].refused == 1u);
    CHECK((*stats.rows)[1].refusedBy->at("schema") == 1);
    const auto read = std::ranges::find(*stats.rows, std::string("read"), &shape::UsageVerb::verb);
    REQUIRE(read != stats.rows->end());
    CHECK(read->batched == 1u);
    CHECK_FALSE(read->unfiltered.has_value());              // read has no filter args.
    REQUIRE(stats.largest->size() == 5);
    CHECK((*stats.largest)[0].verb == "get");
    CHECK((*stats.largest)[0].out == 5000);

    const auto all = MakeUsage(lines, std::nullopt, AllVerbs());
    Checked(all);
    CHECK(all.sessions == 2u);
    CHECK(all.since == "2026-01-02T03:04:01Z");
    CHECK((*all.rows)[0].verb == "get");                      // 10300 over describe's 9000.
    CHECK(std::ranges::find(*all.neverCalled, "census") != all.neverCalled->end());
    CHECK(std::ranges::find(*all.neverCalled, "get") == all.neverCalled->end());

    const auto none = MakeUsage({}, std::string(), AllVerbs());
    CHECK_FALSE(Checked(none).contains("session"));
    CHECK(none.rows->empty());

    // Top-level calls per tool pack, as calls counts them: the batch, not its items.
    CHECK(stats.totals->callsByPack == std::map<std::string, uint64_t>{{"explore", 6}});
    std::vector<UsageLine> mixed = {line("p", "disasm", 10, "1"), line("p", "write", 10, "2"),
                                    line("p", "disasm", 10, "3"), line("p", "nonsense", 10, "4"),
                                    line("p", "call", 10, "5")};
    mixed[4].ok = false;
    mixed[4].refused = "pack";                              // Refused for its pack: a refusal, not a modify call.
    const auto packs = MakeUsage(mixed, std::string("p"), AllVerbs());
    CHECK(packs.totals->callsByPack == std::map<std::string, uint64_t>{{"native", 2}, {"modify", 1}});
    CHECK(packs.totals->refused == 1);

    // get's paths pick what is read: never unfiltered, whatever the size.
    const std::vector<UsageLine> picked = {line("q", "get", 50000, "1", {"object", "paths"}),
                                           line("q", "get", 50000, "22", {"object"})};
    const auto pickedStats = MakeUsage(picked, std::string("q"), AllVerbs());
    REQUIRE(pickedStats.rows->size() == 1);
    CHECK((*pickedStats.rows)[0].unfiltered == 1u);            // Only the call with no paths.
}

TEST_CASE("usage counts digests and what they kept out, and lists and picks sessions") {
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
    const auto stats = MakeUsage(lines, std::string("new"), AllVerbs());
    Checked(stats);
    CHECK(stats.totals->digested == 2);
    CHECK(stats.totals->saved == (430000 - 500) + (22000 - 400));
    const auto census = std::ranges::find(*stats.rows, std::string("census"), &shape::UsageVerb::verb);
    REQUIRE(census != stats.rows->end());
    CHECK(census->digested == 1u);
    CHECK(census->saved == 429500u);
    CHECK_FALSE(census->unfiltered.has_value());              // The digested one is not marked; the rerun had limit.
    const auto dump = std::ranges::find(*stats.rows, std::string("get"), &shape::UsageVerb::verb);
    CHECK_FALSE(dump->unfiltered.has_value());
    const auto big = std::ranges::find(*stats.largest, uint64_t{500}, &shape::UsageCall::out);
    REQUIRE(big != stats.largest->end());
    CHECK(big->digest == 430000u);

    // sessions: newest first, totals over the whole log, no verb rows.
    const auto list = UsageFor(lines, AllVerbs(), {false, true, false, {}}, std::nullopt);
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
    CHECK(list->totals->calls == 4);
    CHECK_FALSE(list->rows.has_value());
    CHECK_FALSE(json.contains("never_called"));

    // session: that one, or a refusal naming stats with sessions.
    const auto old = UsageFor(lines, AllVerbs(), {false, false, false, "old"}, std::string("new"));
    REQUIRE(old.has_value());
    CHECK(old->session == "old");
    CHECK(old->totals->calls == 1);
    const auto missing = UsageFor(lines, AllVerbs(), {false, false, false, "gone"}, std::nullopt);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().find("usage with report sessions") != std::string::npos);
    // only: the parts named; the identity stays.
    auto some = *UsageFor(lines, AllVerbs(), {false, false, false, "new"}, std::nullopt);
    KeepParts(some, std::vector<std::string>{"totals"});
    const auto kept = Checked(some);
    CHECK(kept.contains("totals"));
    CHECK(kept.contains("session"));
    CHECK_FALSE(kept.contains("rows"));
    CHECK_FALSE(kept.contains("largest"));
    CHECK_FALSE(kept.contains("context"));
    // Neither: the current session, else the log's latest.
    CHECK(UsageFor(lines, AllVerbs(), {}, std::string("old"))->session == "old");
    CHECK(UsageFor(lines, AllVerbs(), {}, std::nullopt)->session == "new");

    // context: one session's tool output against the console's --context-limit.
    const auto within = UsageFor(lines, AllVerbs(), {false, false, false, {}, 400000}, std::string("new"));
    REQUIRE(within->context.has_value());
    CHECK(within->context->chars == 20900);
    CHECK(within->context->limit == 400000);
    CHECK_FALSE(within->context->over);
    const auto past = UsageFor(lines, AllVerbs(), {false, false, false, {}, 20000}, std::string("new"));
    CHECK(past->context->over);
    CHECK(ContextChars(lines, "old") == 0);                    // A shell's output is no context.
    CHECK_FALSE(list->context.has_value());                    // sessions and all: no one session's context.
    CHECK(ContextWarning(20900, 20000).find("past context_limit 20000") != std::string::npos);
}

TEST_CASE("usage: after a digest, the verb's next call followed its narrow hint or only raised the cap") {
    std::vector<UsageLine> lines;
    const auto add = [&](std::string verb, uint64_t digest, std::optional<uint64_t> cap, std::string narrow) {
        UsageLine usage;
        usage.time = "2026-01-02T03:04:05Z";
        usage.session = "s";
        usage.verb = std::move(verb);
        usage.origin = "ue";
        usage.caller = "tool";
        usage.hash = std::to_string(lines.size());
        usage.out = 900;
        usage.digest = digest;
        usage.cap = cap;
        usage.narrow = std::move(narrow);
        lines.push_back(std::move(usage));
    };
    add("each", 90000, 25000, "");          // Digested,
    add("find", 0, std::nullopt, "");       // another verb between does not count,
    add("each", 0, std::nullopt, "a1");     // then narrowed: followed.
    add("each", 60000, 25000, "a1");        // Digested again,
    add("each", 0, 100000, "a1");           // then the same narrowing with a higher cap: raised.
    add("rows", 50000, 25000, "b1");        // Digested,
    add("rows", 0, 0, "b1");                // big: raised.
    add("get", 40000, 25000, "");           // Digested, never called again: neither.
    const auto usage = MakeUsage(lines, std::string("s"), AllVerbs());
    Checked(usage);
    const auto row = [&](std::string_view verb) { return *std::ranges::find(*usage.rows, std::string(verb), &shape::UsageVerb::verb); };
    CHECK(row("each").digested == 2u);
    CHECK(row("each").followed == 1u);
    CHECK(row("each").raised == 1u);
    CHECK(row("rows").raised == 1u);
    CHECK_FALSE(row("rows").followed.has_value());
    CHECK_FALSE(row("get").followed.has_value());
    CHECK_FALSE(row("get").raised.has_value());
    // The log line keeps cap and narrow.
    const auto parsed = ParseUsageLine(UsageText(lines[4]));
    REQUIRE(parsed.has_value());
    CHECK(parsed->cap == 100000u);
    CHECK(parsed->narrow == "a1");
}

TEST_CASE("usage: repeated is the same input and the same result; same_args the same input, another result") {
    std::vector<UsageLine> lines;
    const auto add = [&](std::string verb, std::string hash, std::string outHash) {
        UsageLine usage;
        usage.time = "2026-01-02T03:04:05Z";
        usage.session = "s";
        usage.verb = std::move(verb);
        usage.origin = "core";
        usage.caller = "tool";
        usage.hash = std::move(hash);
        usage.outHash = std::move(outHash);
        lines.push_back(std::move(usage));
    };
    add("labels-sign", "in1", "outA");
    add("labels-sign", "in1", "outB");      // Signed new names: needed, same_args.
    add("labels-sign", "in1", "outB");      // Nothing changed since: repeated.
    add("read", "in2", "outC");
    add("read", "in2", "outC");             // repeated.
    const auto usage = MakeUsage(lines, std::string("s"), AllVerbs());
    Checked(usage);
    const auto sign = *std::ranges::find(*usage.rows, std::string("labels-sign"), &shape::UsageVerb::verb);
    CHECK(sign.sameArgs == 1u);
    CHECK(sign.repeated == 1u);
    const auto read = *std::ranges::find(*usage.rows, std::string("read"), &shape::UsageVerb::verb);
    CHECK(read.repeated == 1u);
    CHECK_FALSE(read.sameArgs.has_value());
    CHECK(ParseUsageLine(UsageText(lines[1]))->outHash == "outB");
}

TEST_CASE("usage advice: verbs often digested or unfiltered, with a smaller default limit or a hint") {
    std::vector<UsageLine> lines;
    const auto add = [&](std::string verb, uint64_t out, uint64_t digest, std::optional<uint64_t> cap) {
        UsageLine usage;
        usage.time = "2026-01-02T03:04:05Z";
        usage.session = "s";
        usage.verb = std::move(verb);
        usage.origin = "ue";
        usage.caller = "tool";
        usage.hash = std::to_string(lines.size());
        usage.out = out;
        usage.digest = digest;
        usage.cap = cap;
        lines.push_back(std::move(usage));
    };
    add("census", 900, 75000, 25000);       // Twice past the cap: the largest needs a quarter of the rows.
    add("census", 900, 40000, 25000);
    add("census", 0, 0, 100000);            // Raised after: more raised than followed.
    add("modules", 9000, 0, std::nullopt);  // Big, but no filter to give: not unfiltered, not listed.
    add("find", 300, 0, std::nullopt);      // Small: no advice.
    const auto report = UsageFor(lines, AllVerbs(), {false, false, true, "s", 0}, std::nullopt);
    REQUIRE(report.has_value());
    Checked(*report);
    REQUIRE(report->advice.has_value());
    REQUIRE(report->advice->size() == 1);
    const auto& census = report->advice->front();
    CHECK(census.verb == "census");
    CHECK(census.digested == 2);
    CHECK(census.raised == 1);
    REQUIRE(census.limit.has_value());
    CHECK(census.limit->key == "view.limit");
    CHECK(census.limit->now == 100);
    CHECK(census.limit->proposed == 25);    // 100 * 25000 * 0.75 / 75000.
    CHECK(census.hint.find("raised max_chars") != std::string::npos);
    // Without advice: no list.
    CHECK_FALSE(UsageFor(lines, AllVerbs(), {}, std::string("s"))->advice.has_value());
}

TEST_CASE("usage baseline: what got worse and better against a saved report") {
    const Json before = Json::parse(R"({"totals": {"calls": 10, "refused": 1, "digested": 2, "ms": 1000,
        "out_by_caller": {"tool": 50000}}, "rows": [
        {"verb": "find", "calls": 4, "ms": 400, "refused": 1, "refused_by": {"schema": 1}},
        {"verb": "get", "calls": 6, "ms": 600, "digested": 2}]})");
    const Json after = Json::parse(R"({"totals": {"calls": 14, "refused": 3, "digested": 1, "ms": 1050},
        "context": {"chars": 80000, "limit": 400000, "over": false}, "rows": [
        {"verb": "find", "calls": 4, "ms": 1200, "refused": 3, "refused_by": {"schema": 2, "pack": 1}},
        {"verb": "get", "calls": 6, "ms": 600, "digested": 1},
        {"verb": "census", "calls": 4, "ms": 40}]})");
    const auto compare = CompareUsage(before, after, "base.json");
    Checked(compare);
    CHECK(compare.baseline == "base.json");
    const auto has = [](const std::vector<shape::UsageChange>& list, std::string what, std::optional<std::string> verb) {
        return std::ranges::any_of(list, [&](const shape::UsageChange& c) { return c.what == what && c.verb == verb; });
    };
    CHECK(has(compare.worse, "calls", std::nullopt));
    CHECK(has(compare.worse, "refused", std::nullopt));
    CHECK(has(compare.worse, "context", std::nullopt));             // Total tool output: 50000 then 80000.
    CHECK(has(compare.worse, "refused_by.schema", std::nullopt));
    CHECK(has(compare.worse, "refused_by.pack", std::nullopt));
    CHECK(has(compare.worse, "ms_per_call", std::string("find")));   // 100 ms a call, then 300.
    CHECK(has(compare.worse, "calls", std::string("census")));       // New: from none.
    CHECK_FALSE(has(compare.worse, "ms", std::nullopt));             // 5% slower: noise.
    CHECK(has(compare.better, "digested", std::nullopt));
    CHECK(has(compare.better, "digested", std::string("get")));
    CHECK_FALSE(compare.worse.front().verb.has_value());             // Totals first.
    // baseline and save take one session's report.
    CHECK_FALSE(Call(R"({"verb": "usage", "args": {"report": "all", "baseline": "b.json"}})").has_value());
    CHECK_FALSE(Call(R"({"verb": "usage", "args": {"report": "sessions", "save": "b.json"}})").has_value());
    CHECK(Call(R"({"verb": "usage", "args": {"session": "s", "baseline": "b.json", "save": "c.json"}})").has_value());
}

TEST_CASE("usage refusals: by verb, kind and the arg at fault, most first") {
    std::vector<UsageLine> lines;
    const auto add = [&](std::string verb, std::string kind, std::string key) {
        UsageLine usage;
        usage.time = "2026-01-02T03:04:05Z";
        usage.session = "s";
        usage.verb = std::move(verb);
        usage.origin = "ue";
        usage.caller = "tool";
        usage.hash = std::to_string(lines.size());
        usage.ok = kind.empty();
        usage.refused = std::move(kind);
        usage.refusedKey = std::move(key);
        lines.push_back(std::move(usage));
    };
    add("get", "schema", "view.limit");
    add("find", "validation", "");
    add("get", "schema", "view.limit");
    add("get", "schema", "paths");
    add("get", "", "");
    const auto usage = MakeUsage(lines, std::string("s"), AllVerbs());
    Checked(usage);
    REQUIRE(usage.refusals.has_value());
    REQUIRE(usage.refusals->size() == 3);
    const auto& top = usage.refusals->front();
    CHECK(top.verb == "get");
    CHECK(top.kind == "schema");
    CHECK(top.key == "view.limit");
    CHECK(top.count == 2);
    CHECK(std::ranges::any_of(*usage.refusals, [](const shape::UsageRefusal& r) { return r.verb == "find" && !r.key; }));
    CHECK(ParseUsageLine(UsageText(lines[0]))->refusedKey == "view.limit");
    CHECK_FALSE(MakeUsage({}, std::string("s"), AllVerbs()).refusals.has_value());   // None: no list.
}
