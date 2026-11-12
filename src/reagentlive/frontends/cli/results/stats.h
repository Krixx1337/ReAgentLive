// reagentlive CLI: `stats`, what a session cost, built from the usage log
// (state/usage.h). Ranked and short, stating facts with three marks
// (unfiltered, repeated, refused), never advice. Pure; tests cover it.
#pragma once
#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/results/schema.h"
#include "reagentlive/frontends/cli/state/usage.h"

namespace reagentlive::cli {
namespace shape {
struct StatsTotals {
    uint64_t calls = 0, in = 0, out = 0, refused = 0;
    std::map<std::string, uint64_t> outByCaller;
    double ms = 0;
    template<class V> static void Describe(V& v) {
        v("calls", &StatsTotals::calls);
        v("in", &StatsTotals::in);
        v("out", &StatsTotals::out);
        v("out_by_caller", &StatsTotals::outByCaller);
        v("refused", &StatsTotals::refused);
        v("ms", &StatsTotals::ms);
    }
};
struct StatsVerb {
    std::string verb, origin;
    uint64_t calls = 0, out = 0, largest = 0;
    double ms = 0;
    std::optional<uint64_t> batched, unfiltered, repeated, refused;
    std::optional<std::map<std::string, uint64_t>> refusedBy;
    template<class V> static void Describe(V& v) {
        v("verb", &StatsVerb::verb);
        v("origin", &StatsVerb::origin);
        v("calls", &StatsVerb::calls);
        v("out", &StatsVerb::out);
        v("largest", &StatsVerb::largest);
        v("ms", &StatsVerb::ms);
        v("batched", &StatsVerb::batched);
        v("unfiltered", &StatsVerb::unfiltered);
        v("repeated", &StatsVerb::repeated);
        v("refused", &StatsVerb::refused);
        v("refused_by", &StatsVerb::refusedBy);
    }
};
struct StatsCall {
    std::string verb, caller;
    std::vector<std::string> flags;
    uint64_t out = 0;
    std::optional<bool> batch;
    template<class V> static void Describe(V& v) {
        v("verb", &StatsCall::verb);
        v("flags", &StatsCall::flags);
        v("out", &StatsCall::out);
        v("caller", &StatsCall::caller);
        v("batch", &StatsCall::batch);
    }
};
// stats: this session (the latest in the log when no target is attached),
// or with --all every session the log holds and the verbs none called.
struct Stats {
    std::optional<std::string> session;
    std::optional<uint64_t> sessions;
    std::optional<std::string> since;
    StatsTotals totals;
    std::vector<StatsVerb> rows;
    std::vector<StatsCall> largest;
    std::optional<std::vector<std::string>> neverCalled;
    template<class V> static void Describe(V& v) {
        v("session", &Stats::session);
        v("sessions", &Stats::sessions);
        v("since", &Stats::since);
        v("totals", &Stats::totals);
        v("rows", &Stats::rows);
        v("largest", &Stats::largest);
        v("never_called", &Stats::neverCalled);
    }
};
}

namespace stats_detail {
// Flags that narrow what a verb prints: a call to a verb that has one and
// gave none is marked `unfiltered`.
inline constexpr std::string_view FilterFlags[] = {"--match", "--nonzero", "--own", "--changed", "--cls", "--limit"};
inline bool IsFilter(std::string_view flag) { return std::ranges::find(FilterFlags, flag) != std::end(FilterFlags); }
}
constexpr size_t StatsLargestCalls = 5;

// `session` nullopt: every session (--all). `verbs`: the console's table, for
// the unfiltered mark and the verbs never called.
inline shape::Stats MakeStats(std::span<const UsageLine> lines, const std::optional<std::string>& session,
                             std::span<const Verb> verbs) {
    using namespace stats_detail;
    shape::Stats stats;
    const bool all = !session;
    if (all) {
        stats.sessions = 0;
        stats.neverCalled.emplace();
    } else {
        if (!session->empty()) stats.session = *session;
    }
    std::map<std::string, shape::StatsVerb> byVerb;
    std::set<std::pair<std::string, std::string>> seen;   // (session, hash)
    std::set<std::string> sessions;
    std::vector<const UsageLine*> calls;
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
        if (line.batch) bump(row.batched);
        if (!line.ok) {
            bump(row.refused);
            if (!row.refusedBy) row.refusedBy.emplace();
            ++(*row.refusedBy)[line.refused.empty() ? "error" : line.refused];
        }
        if (!seen.insert({line.session, line.hash}).second) bump(row.repeated);
        if (const Verb* verb = FindVerb(verbs, line.verb)) {
            const bool filters = std::ranges::any_of(verb->flags, [](const Flag& f) { return IsFilter(f.name); });
            if (filters && std::ranges::none_of(line.flags, [](const std::string& f) { return IsFilter(f); }))
                bump(row.unfiltered);
        }
        // Totals count what the agent sent: a batch's items are inside its own call.
        if (!line.batch) ++stats.totals.calls;
        if (!line.batch) stats.totals.ms += line.ms;
        if (!line.ok) ++stats.totals.refused;
        stats.totals.in += line.batch ? 0 : line.in;
        stats.totals.out += line.out;
        stats.totals.outByCaller[line.caller] += line.out;
        calls.push_back(&line);
    }
    if (all) stats.sessions = sessions.size();
    else stats.since.reset();
    const auto tenths = [](double ms) { return static_cast<double>(static_cast<int64_t>(ms * 10 + 0.5)) / 10; };
    stats.totals.ms = tenths(stats.totals.ms);
    for (auto& [name, row] : byVerb) {
        row.ms = tenths(row.ms);
        stats.rows.push_back(std::move(row));
    }
    std::ranges::stable_sort(stats.rows, std::greater{}, &shape::StatsVerb::out);
    std::ranges::stable_sort(calls, std::greater{}, [](const UsageLine* line) { return line->out; });
    for (size_t i = 0; i < calls.size() && i < StatsLargestCalls; ++i) {
        shape::StatsCall call{calls[i]->verb, calls[i]->caller, calls[i]->flags, calls[i]->out};
        if (calls[i]->batch) call.batch = true;
        stats.largest.push_back(std::move(call));
    }
    if (all) {
        // quit is never recorded (a program ends every session with it).
        for (const auto& verb : verbs)
            if (verb.name != "quit" && !byVerb.contains(std::string(verb.name)))
                stats.neverCalled->emplace_back(verb.name);
    }
    return stats;
}

// The session `stats` reports when no target is attached: the log's latest.
inline std::string LatestSession(std::span<const UsageLine> lines) {
    return lines.empty() ? std::string() : lines.back().session;
}
}
