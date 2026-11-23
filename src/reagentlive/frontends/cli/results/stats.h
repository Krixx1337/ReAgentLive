// reagentlive CLI: `stats`, what a session cost, built from the usage log
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
#include <vector>
#include "reagentlive/frontends/cli/results/schema.h"
#include "reagentlive/frontends/cli/state/usage.h"

namespace reagentlive::cli {
namespace shape {
struct StatsTotals {
    uint64_t calls = 0, in = 0, out = 0, refused = 0;
    std::map<std::string, uint64_t> outByCaller;
    std::map<std::string, uint64_t> callsByPack;    // Every call (a batch's items too) by its verb's tool pack.
    uint64_t digested = 0, saved = 0;   // Results past the budget; what their digests kept out.
    double ms = 0;
    template<class V> static void Describe(V& v) {
        v("calls", &StatsTotals::calls);
        v("in", &StatsTotals::in);
        v("out", &StatsTotals::out);
        v("out_by_caller", &StatsTotals::outByCaller);
        v("calls_by_pack", &StatsTotals::callsByPack);
        v("refused", &StatsTotals::refused);
        v("digested", &StatsTotals::digested);
        v("saved", &StatsTotals::saved);
        v("ms", &StatsTotals::ms);
    }
};
struct StatsVerb {
    std::string verb, origin;
    uint64_t calls = 0, out = 0, largest = 0;
    double ms = 0;
    std::optional<uint64_t> batched, unfiltered, repeated, refused, digested, saved;
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
        v("digested", &StatsVerb::digested);
        v("saved", &StatsVerb::saved);
    }
};
struct StatsCall {
    std::string verb, caller;
    std::vector<std::string> keys;
    uint64_t out = 0;
    std::optional<bool> batch;
    std::optional<uint64_t> digest;     // The full size a digest stood in for.
    template<class V> static void Describe(V& v) {
        v("verb", &StatsCall::verb);
        v("keys", &StatsCall::keys);
        v("out", &StatsCall::out);
        v("caller", &StatsCall::caller);
        v("batch", &StatsCall::batch);
        v("digest", &StatsCall::digest);
    }
};
// stats with sessions: one session of the log.
struct StatsSession {
    std::string session, since, until;  // Its first and last call.
    std::vector<std::string> callers;
    uint64_t calls = 0, out = 0, toolOut = 0, digested = 0, saved = 0, refused = 0;
    template<class V> static void Describe(V& v) {
        v("session", &StatsSession::session);
        v("since", &StatsSession::since);
        v("until", &StatsSession::until);
        v("callers", &StatsSession::callers);
        v("calls", &StatsSession::calls);
        v("out", &StatsSession::out);
        v("tool_out", &StatsSession::toolOut);
        v("digested", &StatsSession::digested);
        v("saved", &StatsSession::saved);
        v("refused", &StatsSession::refused);
    }
};
// stats: this session (the latest in the log when no target is attached),
// or with `all` every session the log holds and the verbs none called.
struct Stats {
    std::optional<std::string> session;
    std::optional<uint64_t> sessions;
    std::optional<std::string> since;
    StatsTotals totals;
    std::vector<StatsVerb> rows;
    std::vector<StatsCall> largest;
    std::optional<std::vector<std::string>> neverCalled;
    std::optional<std::vector<StatsSession>> sessionRows;
    template<class V> static void Describe(V& v) {
        v("session", &Stats::session);
        v("sessions", &Stats::sessions);
        v("since", &Stats::since);
        v("totals", &Stats::totals);
        v("rows", &Stats::rows);
        v("largest", &Stats::largest);
        v("never_called", &Stats::neverCalled);
        v("session_rows", &Stats::sessionRows);
    }
};
}

namespace stats_detail {
// Args that narrow what a verb prints: a call to a verb that has one and
// gave none is marked `unfiltered`. A view's keys are logged by path (view.limit).
inline constexpr std::string_view FilterKeys[] = {"match", "nonzero", "own", "cls", "limit"};
inline bool IsFilter(std::string_view key) {
    if (const size_t dot = key.rfind('.'); dot != std::string_view::npos) key = key.substr(dot + 1);
    return std::ranges::find(FilterKeys, key) != std::end(FilterKeys);
}
}
constexpr size_t StatsLargestCalls = 5;

// `session` nullopt: every session (`all`). `verbs`: the console's table, for
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
        const uint64_t saved = line.digest > line.out ? line.digest - line.out : 0;
        if (line.digest) {
            bump(row.digested);
            row.saved = row.saved.value_or(0) + saved;
            ++stats.totals.digested;
            stats.totals.saved += saved;
        }
        if (const Verb* verb = FindVerb(verbs, line.verb)) ++stats.totals.callsByPack[std::string(ToolPackName(verb->pack))];
        // A digested call cost its digest only: not worth a mark.
        if (const Verb* verb = FindVerb(verbs, line.verb); verb && !line.digest) {
            const bool filters = std::ranges::any_of(verb->args, [](const Arg& a) {
                return IsFilter(a.key) || std::ranges::any_of(a.fields, [](const Arg& f) { return IsFilter(f.key); });
            });
            if (filters && std::ranges::none_of(line.keys, [](const std::string& k) { return IsFilter(k); }))
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
        shape::StatsCall call{calls[i]->verb, calls[i]->caller, calls[i]->keys, calls[i]->out};
        if (calls[i]->batch) call.batch = true;
        if (calls[i]->digest) call.digest = calls[i]->digest;
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

constexpr size_t StatsSessionRows = 30;

// stats with sessions: totals over the whole log, and its newest sessions one row each.
inline shape::Stats MakeSessionList(std::span<const UsageLine> lines, std::span<const Verb> verbs) {
    auto stats = MakeStats(lines, std::nullopt, verbs);
    stats.rows.clear();
    stats.largest.clear();
    stats.neverCalled.reset();
    std::map<std::string, shape::StatsSession> bySession;
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
    std::vector<shape::StatsSession> rows;
    for (auto& [id, row] : bySession) {
        std::ranges::sort(row.callers);
        rows.push_back(std::move(row));
    }
    std::ranges::stable_sort(rows, std::greater{}, &shape::StatsSession::until);
    if (rows.size() > StatsSessionRows) rows.resize(StatsSessionRows);
    stats.sessionRows = std::move(rows);
    return stats;
}

// What `stats` asked for: `all`, `sessions`, one `session`, or the current
// session (`current`; nullopt with no target: the log's latest).
struct StatsQuery {
    bool all = false, sessions = false;
    std::string session;
};
inline std::expected<shape::Stats, std::string> StatsFor(std::span<const UsageLine> lines, std::span<const Verb> verbs,
                                                         const StatsQuery& query, std::optional<std::string> current) {
    if (query.sessions) return MakeSessionList(lines, verbs);
    if (query.all) return MakeStats(lines, std::nullopt, verbs);
    if (!query.session.empty()) {
        if (std::ranges::none_of(lines, [&](const UsageLine& line) { return line.session == query.session; }))
            return std::unexpected("no session '" + query.session + "' in the usage log (stats with sessions lists them)");
        return MakeStats(lines, query.session, verbs);
    }
    return MakeStats(lines, current ? *current : LatestSession(lines), verbs);
}
}
