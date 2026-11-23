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
    }
};
struct UsageVerb {
    std::string verb, origin;
    uint64_t calls = 0, out = 0, largest = 0;
    double ms = 0;
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
// usage with sessions: one session of the log.
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
    UsageTotals totals;
    std::vector<UsageVerb> rows;
    std::vector<UsageCall> largest;
    std::optional<std::vector<std::string>> neverCalled;
    std::optional<std::vector<UsageSession>> sessionRows;
    std::optional<UsageContext> context;
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
            ++stats.totals.digested;
            stats.totals.saved += saved;
        }
        // Top-level calls, as `calls` counts them; one refused for its pack never ran in it: a refusal only.
        if (const Verb* verb = FindVerb(verbs, line.verb); verb && !line.batch && (line.ok || line.refused != "pack"))
            ++stats.totals.callsByPack[std::string(ToolPackName(verb->pack))];
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
        if (!line.batch) ++stats.totals.calls;
        else ++stats.totals.batched;
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
    std::ranges::stable_sort(stats.rows, std::greater{}, &shape::UsageVerb::out);
    std::ranges::stable_sort(calls, std::greater{}, [](const UsageLine* line) { return line->out; });
    for (size_t i = 0; i < calls.size() && i < UsageLargestCalls; ++i) {
        shape::UsageCall call{calls[i]->verb, calls[i]->caller, calls[i]->keys, calls[i]->out};
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

// The session `usage` reports when no target is attached: the log's latest.
inline std::string LatestSession(std::span<const UsageLine> lines) {
    return lines.empty() ? std::string() : lines.back().session;
}

constexpr size_t UsageSessionRows = 30;

// usage with sessions: totals over the whole log, and its newest sessions one row each.
inline shape::UsageReport MakeSessionList(std::span<const UsageLine> lines, std::span<const Verb> verbs) {
    auto stats = MakeUsage(lines, std::nullopt, verbs);
    stats.rows.clear();
    stats.largest.clear();
    stats.neverCalled.reset();
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

// What `usage` asked for: `all`, `sessions`, one `session`, or the current
// session (`current`; nullopt with no target: the log's latest).
struct UsageQuery {
    bool all = false, sessions = false;
    std::string session;
    uint64_t contextLimit = 0;      // The console's --context-limit (0: none), shown with one session.
};
inline std::expected<shape::UsageReport, std::string> UsageFor(std::span<const UsageLine> lines, std::span<const Verb> verbs,
                                                         const UsageQuery& query, std::optional<std::string> current) {
    if (query.sessions) return MakeSessionList(lines, verbs);
    if (query.all) return MakeUsage(lines, std::nullopt, verbs);
    const auto one = [&](const std::string& session) {
        auto report = MakeUsage(lines, session, verbs);
        const uint64_t chars = ContextChars(lines, session);
        report.context = shape::UsageContext{chars, query.contextLimit, query.contextLimit && chars > query.contextLimit};
        return report;
    };
    if (!query.session.empty()) {
        if (std::ranges::none_of(lines, [&](const UsageLine& line) { return line.session == query.session; }))
            return std::unexpected("no session '" + query.session + "' in the usage log (usage with sessions lists them)");
        return one(query.session);
    }
    return one(current ? *current : LatestSession(lines));
}
}
