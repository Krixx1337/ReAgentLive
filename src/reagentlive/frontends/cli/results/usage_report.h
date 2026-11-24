// reagentlive CLI: `usage`, what a session cost, built from the usage log
// (state/usage.h). Ranked and short, stating facts with three marks
// (unfiltered, repeated, refused), never advice. Pure; tests cover it.
#pragma once
#include <algorithm>
#include <expected>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <vector>
#include "reagentlive/frontends/cli/results/budget.h"
#include "reagentlive/frontends/cli/results/schema.h"
#include "reagentlive/frontends/cli/state/usage.h"

namespace reagentlive::cli {
namespace shape {
struct UsageTotals {
    uint64_t calls = 0, batched = 0, in = 0, out = 0, refused = 0;  // calls: top-level (a batch is one); batched: inside one.
    std::map<std::string, uint64_t> outByCaller;
    std::map<std::string, uint64_t> callsByPack;    // The top-level calls by their verb's tool pack, but those refused for it.
    uint64_t digested = 0, saved = 0;   // Results past max_chars; what their digests kept out.
    double ms = 0;
    // Waited for the agent's slot while another client held it: in all, and
    // the longest one call waited.
    double slotMs = 0, slotMsMax = 0;
    template<class V> static void Describe(V& v) {
        v("calls", &UsageTotals::calls);
        v("batched", &UsageTotals::batched);
        v("in", &UsageTotals::in);
        v("out", &UsageTotals::out);
        v("out_by_caller", &UsageTotals::outByCaller);
        v("calls_by_pack", &UsageTotals::callsByPack);
        v("refused", &UsageTotals::refused);
        v("digested", &UsageTotals::digested);
        v("saved", &UsageTotals::saved);
        v("ms", &UsageTotals::ms);
        v("slot_ms", &UsageTotals::slotMs);
        v("slot_ms_max", &UsageTotals::slotMsMax);
    }
};
struct UsageVerb {
    std::string verb, origin;
    uint64_t calls = 0, out = 0, largest = 0;
    double ms = 0;
    std::optional<double> slotMs;       // Slot wait in all, when its calls waited at all.
    // repeated: the same input and the same result as an earlier call of the
    // session; sameArgs: the same input with another result (state changed).
    std::optional<uint64_t> batched, unfiltered, repeated, sameArgs, refused, digested, saved;
    // After a digest, the verb's next call: one that changed its narrowing
    // args (followed) or only raised max_chars (raised).
    std::optional<uint64_t> followed, raised;
    std::optional<std::map<std::string, uint64_t>> refusedBy;
    template<class V> static void Describe(V& v) {
        v("verb", &UsageVerb::verb);
        v("origin", &UsageVerb::origin);
        v("calls", &UsageVerb::calls);
        v("out", &UsageVerb::out);
        v("largest", &UsageVerb::largest);
        v("ms", &UsageVerb::ms);
        v("slot_ms", &UsageVerb::slotMs);
        v("batched", &UsageVerb::batched);
        v("unfiltered", &UsageVerb::unfiltered);
        v("repeated", &UsageVerb::repeated);
        v("same_args", &UsageVerb::sameArgs);
        v("refused", &UsageVerb::refused);
        v("refused_by", &UsageVerb::refusedBy);
        v("digested", &UsageVerb::digested);
        v("saved", &UsageVerb::saved);
        v("followed", &UsageVerb::followed);
        v("raised", &UsageVerb::raised);
    }
};
struct UsageCall {
    std::string verb, caller;
    std::vector<std::string> keys;
    uint64_t out = 0;
    std::optional<bool> batch;
    std::optional<uint64_t> digest;     // The full size a digest stood in for.
    template<class V> static void Describe(V& v) {
        v("verb", &UsageCall::verb);
        v("keys", &UsageCall::keys);
        v("out", &UsageCall::out);
        v("caller", &UsageCall::caller);
        v("batch", &UsageCall::batch);
        v("digest", &UsageCall::digest);
    }
};
// usage of one session: the characters that reached an agent (caller
// tool) and the console's soft limit on them (--context-limit).
struct UsageContext {
    uint64_t chars = 0, limit = 0;
    bool over = false;
    template<class V> static void Describe(V& v) {
        v("chars", &UsageContext::chars);
        v("limit", &UsageContext::limit);
        v("over", &UsageContext::over);
    }
};
// usage with advice: a verb often digested or unfiltered, and what to
// change in its row (a person or an agent applies it as a reviewed change).
struct UsageLimit {
    std::string key;                    // By path: view.limit.
    uint64_t now = 0, proposed = 0;     // now: the row's default (0: none or all).
    template<class V> static void Describe(V& v) {
        v("key", &UsageLimit::key);
        v("now", &UsageLimit::now);
        v("proposed", &UsageLimit::proposed);
    }
};
struct UsageAdvice {
    std::string verb;
    uint64_t calls = 0, digested = 0, unfiltered = 0, followed = 0, raised = 0;
    std::optional<UsageLimit> limit;    // A smaller default row limit, from its digests' sizes.
    std::string hint;                   // What else to change, one line.
    template<class V> static void Describe(V& v) {
        v("verb", &UsageAdvice::verb);
        v("calls", &UsageAdvice::calls);
        v("digested", &UsageAdvice::digested);
        v("unfiltered", &UsageAdvice::unfiltered);
        v("followed", &UsageAdvice::followed);
        v("raised", &UsageAdvice::raised);
        v("limit", &UsageAdvice::limit);
        v("hint", &UsageAdvice::hint);
    }
};
// usage with baseline: one figure that moved against the saved report.
struct UsageChange {
    std::string what;                   // calls, refused, digested, context, ms, refused_by.<kind>, ms_per_call.
    std::optional<std::string> verb;    // A verb's figure; absent for the session's totals.
    double before = 0, after = 0;
    template<class V> static void Describe(V& v) {
        v("what", &UsageChange::what);
        v("verb", &UsageChange::verb);
        v("before", &UsageChange::before);
        v("after", &UsageChange::after);
    }
};
struct UsageCompare {
    std::string baseline;               // The file compared against.
    uint64_t checked = 0;               // Figures compared.
    std::vector<UsageChange> worse, better;
    template<class V> static void Describe(V& v) {
        v("baseline", &UsageCompare::baseline);
        v("checked", &UsageCompare::checked);
        v("worse", &UsageCompare::worse);
        v("better", &UsageCompare::better);
    }
};
// usage: refused calls by verb, kind and the arg at fault, most first: the
// help examples and arg texts to fix first.
struct UsageRefusal {
    std::string verb, kind;
    std::optional<std::string> key;
    uint64_t count = 0;
    template<class V> static void Describe(V& v) {
        v("verb", &UsageRefusal::verb);
        v("kind", &UsageRefusal::kind);
        v("key", &UsageRefusal::key);
        v("count", &UsageRefusal::count);
    }
};
// usage with report sessions: one session of the log.
struct UsageSession {
    std::string session, since, until;  // Its first and last call.
    std::vector<std::string> callers;
    uint64_t calls = 0, out = 0, toolOut = 0, digested = 0, saved = 0, refused = 0;
    template<class V> static void Describe(V& v) {
        v("session", &UsageSession::session);
        v("since", &UsageSession::since);
        v("until", &UsageSession::until);
        v("callers", &UsageSession::callers);
        v("calls", &UsageSession::calls);
        v("out", &UsageSession::out);
        v("tool_out", &UsageSession::toolOut);
        v("digested", &UsageSession::digested);
        v("saved", &UsageSession::saved);
        v("refused", &UsageSession::refused);
    }
};
// usage: this session (the latest in the log when no target is attached),
// or with `all` every session the log holds and the verbs none called.
struct UsageReport {
    std::optional<std::string> session;
    std::optional<uint64_t> sessions;
    std::optional<std::string> since;
    // Absent only when `only` leaves them out (rows and largest also in a sessions list).
    std::optional<UsageTotals> totals;
    std::optional<std::vector<UsageVerb>> rows;
    std::optional<std::vector<UsageCall>> largest;
    std::optional<std::vector<std::string>> neverCalled;
    std::optional<std::vector<UsageSession>> sessionRows;
    std::optional<UsageContext> context;
    std::optional<std::vector<UsageAdvice>> advice;
    std::optional<std::vector<UsageRefusal>> refusals;
    std::optional<std::string> saved;           // save: the file this report went to.
    std::optional<UsageCompare> compare;        // baseline: what moved against it.
    template<class V> static void Describe(V& v) {
        v("session", &UsageReport::session);
        v("sessions", &UsageReport::sessions);
        v("since", &UsageReport::since);
        v("totals", &UsageReport::totals);
        v("rows", &UsageReport::rows);
        v("largest", &UsageReport::largest);
        v("never_called", &UsageReport::neverCalled);
        v("session_rows", &UsageReport::sessionRows);
        v("context", &UsageReport::context);
        v("refusals", &UsageReport::refusals);
        v("advice", &UsageReport::advice);
        v("saved", &UsageReport::saved);
        v("compare", &UsageReport::compare);
    }
};
}

namespace usage_detail_report {
// Args that narrow what a verb prints: a call to a verb that has one and
// gave none is marked `unfiltered`. A view's keys are logged by path (view.limit).
inline constexpr std::string_view FilterKeys[] = {"match", "nonzero", "own", "cls", "limit"};
inline bool IsFilter(std::string_view key) {
    if (const size_t dot = key.rfind('.'); dot != std::string_view::npos) key = key.substr(dot + 1);
    return std::ranges::find(FilterKeys, key) != std::end(FilterKeys);
}
}
constexpr size_t UsageLargestCalls = 5;
// --context-limit's default: characters a session may put into an agent's
// context before every result carries a warning. Soft: nothing stops.
constexpr uint64_t DefaultContextLimit = 400000;

// What a session's results put into an agent's context: the output of its
// tool-caller lines.
inline uint64_t ContextChars(std::span<const UsageLine> lines, std::string_view session) {
    uint64_t chars = 0;
    for (const auto& line : lines)
        if (line.session == session && line.caller == "tool") chars += line.out;
    return chars;
}
// The one line each envelope carries past the limit (`context_warning`).
inline std::string ContextWarning(uint64_t chars, uint64_t limit) {
    return "this session put " + std::to_string(chars) + " characters into the agent's context, past context_limit " +
        std::to_string(limit) + ": narrow calls (view.fields, view.limit, filters), follow a digest's next, or start a "
        "new session";
}
// A call printed at most this many characters is never `unfiltered`: a filter would not have saved much.
constexpr uint64_t UsageUnfilteredChars = 2000;

// `session` nullopt: every session (`all`). `verbs`: the console's table, for
// the unfiltered mark and the verbs never called.
inline shape::UsageReport MakeUsage(std::span<const UsageLine> lines, const std::optional<std::string>& session,
                             std::span<const Verb> verbs) {
    using namespace usage_detail_report;
    shape::UsageReport stats;
    stats.totals.emplace();
    stats.rows.emplace();
    stats.largest.emplace();
    const bool all = !session;
    if (all) {
        stats.sessions = 0;
        stats.neverCalled.emplace();
    } else {
        if (!session->empty()) stats.session = *session;
    }
    std::map<std::string, shape::UsageVerb> byVerb;
    std::set<std::pair<std::string, std::string>> seen;   // (session, input hash)
    std::set<std::tuple<std::string, std::string, std::string>> seenResult;   // (session, input hash, output hash)
    std::set<std::string> sessions;
    std::vector<const UsageLine*> calls;
    std::map<std::pair<std::string, std::string>, const UsageLine*> digestOf;  // (session, verb): its last digest.
    std::map<std::tuple<std::string, std::string, std::string>, uint64_t> refusals;  // (verb, kind, key).
    const auto bump = [](std::optional<uint64_t>& count) { count = count.value_or(0) + 1; };
    for (const auto& line : lines) {
        if (!all && line.session != *session) continue;
        sessions.insert(line.session);
        if (!stats.since || line.time < *stats.since) stats.since = line.time;
        auto& row = byVerb[line.verb];
        row.verb = line.verb;
        row.origin = line.origin;
        ++row.calls;
        row.out += line.out;
        row.largest = std::max(row.largest, line.out);
        row.ms += line.ms;
        if (line.slotMs > 0) row.slotMs = row.slotMs.value_or(0) + line.slotMs;
        if (line.batch) bump(row.batched);
        if (!line.ok) {
            bump(row.refused);
            if (!row.refusedBy) row.refusedBy.emplace();
            ++(*row.refusedBy)[line.refused.empty() ? "error" : line.refused];
            ++refusals[{line.verb, line.refused.empty() ? "error" : line.refused, line.refusedKey}];
        }
        const bool sameInput = !seen.insert({line.session, line.hash}).second;
        const bool sameResult = !seenResult.insert({line.session, line.hash, line.outHash}).second;
        if (sameInput) bump(sameResult ? row.repeated : row.sameArgs);
        // The call after a digest on the same verb: did it narrow, or only raise the cap?
        if (const auto pending = digestOf.find({line.session, line.verb}); pending != digestOf.end()) {
            const UsageLine& digested = *pending->second;
            if (!line.narrow.empty() && line.narrow != digested.narrow) bump(row.followed);
            else if (line.cap && (!*line.cap || *line.cap > digested.cap.value_or(0))) bump(row.raised);
            digestOf.erase(pending);
        }
        if (line.digest) digestOf[{line.session, line.verb}] = &line;
        const uint64_t saved = line.digest > line.out ? line.digest - line.out : 0;
        if (line.digest) {
            bump(row.digested);
            row.saved = row.saved.value_or(0) + saved;
            ++stats.totals->digested;
            stats.totals->saved += saved;
        }
        // Top-level calls, as `calls` counts them; one refused for its pack never ran in it: a refusal only.
        if (const Verb* verb = FindVerb(verbs, line.verb); verb && !line.batch && (line.ok || line.refused != "pack"))
            ++stats.totals->callsByPack[std::string(ToolPackName(verb->pack))];
        // A digested call cost its digest only, a refused one its error: neither is worth a mark.
        if (const Verb* verb = FindVerb(verbs, line.verb); verb && line.ok && !line.digest && line.out > UsageUnfilteredChars) {
            const bool filters = std::ranges::any_of(verb->args, [](const Arg& a) {
                return IsFilter(a.key) || std::ranges::any_of(a.fields, [](const Arg& f) { return IsFilter(f.key); });
            });
            // An optional `paths` picks what is read (get's): a filter too. A required one is the call's subject.
            const bool paths = std::ranges::find(line.keys, "paths") != line.keys.end() &&
                std::ranges::any_of(verb->args, [](const Arg& a) { return a.key == "paths" && !a.required; });
            if (filters && !paths && std::ranges::none_of(line.keys, [](const std::string& k) { return IsFilter(k); }))
                bump(row.unfiltered);
        }
        // Totals count what the agent sent: a batch's items are inside its own call.
        if (!line.batch) ++stats.totals->calls;
        else ++stats.totals->batched;
        if (!line.batch) {
            stats.totals->ms += line.ms;
            stats.totals->slotMs += line.slotMs;
            stats.totals->slotMsMax = std::max(stats.totals->slotMsMax, line.slotMs);
        }
        if (!line.ok) ++stats.totals->refused;
        stats.totals->in += line.batch ? 0 : line.in;
        stats.totals->out += line.out;
        stats.totals->outByCaller[line.caller] += line.out;
        calls.push_back(&line);
    }
    if (all) stats.sessions = sessions.size();
    else stats.since.reset();
    const auto tenths = [](double ms) { return static_cast<double>(static_cast<int64_t>(ms * 10 + 0.5)) / 10; };
    stats.totals->ms = tenths(stats.totals->ms);
    stats.totals->slotMs = tenths(stats.totals->slotMs);
    stats.totals->slotMsMax = tenths(stats.totals->slotMsMax);
    for (auto& [name, row] : byVerb) {
        row.ms = tenths(row.ms);
        if (row.slotMs) row.slotMs = tenths(*row.slotMs);
        stats.rows->push_back(std::move(row));
    }
    std::ranges::stable_sort(*stats.rows, std::greater{}, &shape::UsageVerb::out);
    if (!refusals.empty()) {
        stats.refusals.emplace();
        for (const auto& [at, count] : refusals) {
            const auto& [verb, kind, key] = at;
            stats.refusals->push_back({verb, kind, key.empty() ? std::nullopt : std::optional(key), count});
        }
        std::ranges::stable_sort(*stats.refusals, std::greater{}, &shape::UsageRefusal::count);
    }
    std::ranges::stable_sort(calls, std::greater{}, [](const UsageLine* line) { return line->out; });
    for (size_t i = 0; i < calls.size() && i < UsageLargestCalls; ++i) {
        shape::UsageCall call{calls[i]->verb, calls[i]->caller, calls[i]->keys, calls[i]->out};
        if (calls[i]->batch) call.batch = true;
        if (calls[i]->digest) call.digest = calls[i]->digest;
        stats.largest->push_back(std::move(call));
    }
    if (all) {
        // quit is never recorded (a program ends every session with it).
        for (const auto& verb : verbs)
            if (verb.name != "quit" && !byVerb.contains(std::string(verb.name)))
                stats.neverCalled->emplace_back(verb.name);
    }
    return stats;
}

// usage with advice: verbs whose calls were often digested or unfiltered
// (at least two, or half their calls), each with a smaller default
// view.limit when its digests show one (the default scaled by the share of
// the cap the largest digested result would fit in) and one line on what
// else to change. A report only: nothing changes by itself.
inline std::vector<shape::UsageAdvice> MakeAdvice(std::span<const UsageLine> lines,
                                                  const std::optional<std::string>& session,
                                                  const shape::UsageReport& report, std::span<const Verb> verbs) {
    std::vector<shape::UsageAdvice> out;
    if (!report.rows) return out;
    for (const auto& row : *report.rows) {
        const Verb* verb = FindVerb(verbs, row.verb);
        const uint64_t digested = row.digested.value_or(0), unfiltered = row.unfiltered.value_or(0);
        const uint64_t heavy = digested + unfiltered;
        if (!verb || !heavy || (heavy < 2 && heavy * 2 < row.calls)) continue;
        shape::UsageAdvice advice{row.verb, row.calls, digested, unfiltered, row.followed.value_or(0), row.raised.value_or(0)};
        // The largest result past its cap, as a share the cap would hold (a quarter kept for the envelope).
        double fit = 1;
        for (const auto& line : lines)
            if ((!session || line.session == *session) && line.verb == row.verb && line.digest && line.cap.value_or(0))
                fit = std::min(fit, static_cast<double>(*line.cap) * 0.75 / static_cast<double>(line.digest));
        const auto limitPath = KeyPath(*verb, "limit");
        if (!limitPath.empty() && fit < 1) {
            const Arg* limit = FindKey(*verb, limitPath);
            uint64_t now = 0;
            if (limit && !limit->fallback.empty()) now = std::strtoull(std::string(limit->fallback).c_str(), nullptr, 10);
            const uint64_t base = now ? now : 100;
            advice.limit = shape::UsageLimit{limitPath, now, std::max<uint64_t>(1, static_cast<uint64_t>(base * fit))};
        }
        const auto narrowing = budget::NarrowingArgs(*verb);
        std::string names;
        for (const auto& name : narrowing) names += (names.empty() ? "" : ", ") + name;
        if (narrowing.empty())
            advice.hint = "no arg narrows its result: give its row a view or a filter, or keep its result small";
        else if (advice.raised > advice.followed)
            advice.hint = "callers raised max_chars more often than they narrowed: make the digest's next and the "
                "help example use " + names;
        else if (unfiltered >= digested)
            advice.hint = "callers rarely filter: show " + names + " in its help example and notes";
        else
            advice.hint = advice.limit ? "lower its default row limit" : "its results are large: narrow by " + names;
        out.push_back(std::move(advice));
    }
    std::ranges::stable_sort(out, std::greater{}, [](const shape::UsageAdvice& a) { return a.digested + a.unfiltered; });
    return out;
}

// usage with baseline: a report (as save wrote it) against this one. Counts
// are worse when they grew; time when it grew by a quarter and more than
// noise (100 ms in all, 50 ms a call). Totals first, then refusals by kind,
// then each verb either report has; within each, the largest move first.
inline shape::UsageCompare CompareUsage(const Json& before, const Json& after, std::string baseline) {
    shape::UsageCompare out;
    out.baseline = std::move(baseline);
    const auto number = [](const Json& object, std::string_view key) {
        if (!object.is_object()) return 0.0;
        const auto found = object.find(key);
        return found != object.end() && found->is_number() ? found->get<double>() : 0.0;
    };
    std::vector<shape::UsageChange> worse, better;
    const auto count = [&](std::string what, std::optional<std::string> verb, double was, double now) {
        ++out.checked;
        if (now > was) worse.push_back({std::move(what), std::move(verb), was, now});
        else if (now < was) better.push_back({std::move(what), std::move(verb), was, now});
    };
    const auto time = [&](std::string what, std::optional<std::string> verb, double was, double now, double noise) {
        ++out.checked;
        if (now > was * 1.25 && now - was > noise) worse.push_back({std::move(what), std::move(verb), was, now});
        else if (was > now * 1.25 && was - now > noise) better.push_back({std::move(what), std::move(verb), was, now});
    };
    const auto tool = [&](const Json& report) {
        if (report.contains("context") && report["context"].is_object()) return number(report["context"], "chars");
        return report.contains("totals") ? number(report["totals"].value("out_by_caller", Json::object()), "tool") : 0.0;
    };
    const Json& was = before.value("totals", Json::object());
    const Json& now = after.value("totals", Json::object());
    for (const char* key : {"calls", "refused", "digested"}) count(key, std::nullopt, number(was, key), number(now, key));
    count("context", std::nullopt, tool(before), tool(after));
    time("ms", std::nullopt, number(was, "ms"), number(now, "ms"), 100);
    const auto rowsOf = [](const Json& report) {
        std::map<std::string, Json> rows;
        if (report.contains("rows") && report["rows"].is_array())
            for (const auto& row : report["rows"])
                if (row.is_object() && row.contains("verb") && row["verb"].is_string()) rows[row["verb"].get<std::string>()] = row;
        return rows;
    };
    const auto wasRows = rowsOf(before), nowRows = rowsOf(after);
    // Refusals by kind over every verb.
    std::map<std::string, std::pair<double, double>> kinds;
    for (const auto& [rows, side] : {std::pair{&wasRows, 0}, std::pair{&nowRows, 1}})
        for (const auto& [verb, row] : *rows)
            if (row.contains("refused_by") && row["refused_by"].is_object())
                for (const auto& [kind, n] : row["refused_by"].items())
                    (side ? kinds[kind].second : kinds[kind].first) += n.is_number() ? n.get<double>() : 0;
    for (const auto& [kind, pair] : kinds) count("refused_by." + kind, std::nullopt, pair.first, pair.second);
    std::set<std::string> verbs;
    for (const auto& [verb, row] : wasRows) verbs.insert(verb);
    for (const auto& [verb, row] : nowRows) verbs.insert(verb);
    const Json none = Json::object();
    for (const auto& verb : verbs) {
        const Json& a = wasRows.contains(verb) ? wasRows.at(verb) : none;
        const Json& b = nowRows.contains(verb) ? nowRows.at(verb) : none;
        for (const char* key : {"calls", "refused", "digested"}) count(key, verb, number(a, key), number(b, key));
        const double callsA = number(a, "calls"), callsB = number(b, "calls");
        if (callsA && callsB)
            time("ms_per_call", verb, number(a, "ms") / callsA, number(b, "ms") / callsB, 50);
    }
    const auto byMove = [](const shape::UsageChange& c) { return c.after > c.before ? c.after - c.before : c.before - c.after; };
    const auto order = [&](std::vector<shape::UsageChange>& list) {
        // Totals before verbs; within each, the largest move first.
        std::ranges::stable_sort(list, [&](const shape::UsageChange& x, const shape::UsageChange& y) {
            if (x.verb.has_value() != y.verb.has_value()) return !x.verb.has_value();
            return byMove(x) > byMove(y);
        });
    };
    order(worse);
    order(better);
    out.worse = std::move(worse);
    out.better = std::move(better);
    return out;
}

// The session `usage` reports when no target is attached: the log's latest.
inline std::string LatestSession(std::span<const UsageLine> lines) {
    return lines.empty() ? std::string() : lines.back().session;
}

constexpr size_t UsageSessionRows = 30;

// usage with report sessions: totals over the whole log, and its newest sessions one row each.
inline shape::UsageReport MakeSessionList(std::span<const UsageLine> lines, std::span<const Verb> verbs) {
    auto stats = MakeUsage(lines, std::nullopt, verbs);
    stats.rows.reset();
    stats.largest.reset();
    stats.neverCalled.reset();
    stats.refusals.reset();
    std::map<std::string, shape::UsageSession> bySession;
    for (const auto& line : lines) {
        auto& row = bySession[line.session];
        if (row.session.empty()) row.session = line.session, row.since = line.time;
        row.since = std::min(row.since, line.time);
        row.until = std::max(row.until, line.time);
        if (std::ranges::find(row.callers, line.caller) == row.callers.end()) row.callers.push_back(line.caller);
        if (!line.batch) ++row.calls;
        row.out += line.out;
        if (line.caller == "tool") row.toolOut += line.out;
        if (line.digest) {
            ++row.digested;
            row.saved += line.digest > line.out ? line.digest - line.out : 0;
        }
        if (!line.ok) ++row.refused;
    }
    std::vector<shape::UsageSession> rows;
    for (auto& [id, row] : bySession) {
        std::ranges::sort(row.callers);
        rows.push_back(std::move(row));
    }
    std::ranges::stable_sort(rows, std::greater{}, &shape::UsageSession::until);
    if (rows.size() > UsageSessionRows) rows.resize(UsageSessionRows);
    stats.sessionRows = std::move(rows);
    return stats;
}

// usage with `only`: the parts kept (empty: all). The report's identity
// (session, sessions, since, saved) always stays.
inline void KeepParts(shape::UsageReport& report, std::span<const std::string> parts) {
    if (parts.empty()) return;
    const auto keep = [&](std::string_view part) { return std::ranges::find(parts, part) != parts.end(); };
    if (!keep("totals")) report.totals.reset();
    if (!keep("rows")) report.rows.reset();
    if (!keep("largest")) report.largest.reset();
    if (!keep("never_called")) report.neverCalled.reset();
    if (!keep("session_rows")) report.sessionRows.reset();
    if (!keep("context")) report.context.reset();
    if (!keep("refusals")) report.refusals.reset();
    if (!keep("advice")) report.advice.reset();
    if (!keep("compare")) report.compare.reset();
}

// What `usage` asked for: report `all` or `sessions`, one `session`, or the current
// session (`current`; nullopt with no target: the log's latest).
struct UsageQuery {
    bool all = false, sessions = false, advice = false;
    std::string session;
    uint64_t contextLimit = 0;      // The console's --context-limit (0: none), shown with one session.
};
inline std::expected<shape::UsageReport, std::string> UsageFor(std::span<const UsageLine> lines, std::span<const Verb> verbs,
                                                         const UsageQuery& query, std::optional<std::string> current) {
    if (query.sessions) return MakeSessionList(lines, verbs);
    if (query.all) {
        auto report = MakeUsage(lines, std::nullopt, verbs);
        if (query.advice) report.advice = MakeAdvice(lines, std::nullopt, report, verbs);
        return report;
    }
    const auto one = [&](const std::string& session) {
        auto report = MakeUsage(lines, session, verbs);
        if (query.advice) report.advice = MakeAdvice(lines, session, report, verbs);
        const uint64_t chars = ContextChars(lines, session);
        report.context = shape::UsageContext{chars, query.contextLimit, query.contextLimit && chars > query.contextLimit};
        return report;
    };
    if (!query.session.empty()) {
        if (std::ranges::none_of(lines, [&](const UsageLine& line) { return line.session == query.session; }))
            return std::unexpected("no session '" + query.session + "' in the usage log (usage with report sessions lists them)");
        return one(query.session);
    }
    return one(current ? *current : LatestSession(lines));
}
}
