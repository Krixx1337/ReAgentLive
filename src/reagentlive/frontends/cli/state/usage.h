// reagentlive CLI: the usage log, one JSON line per console call in a file
// beside the console (<console>.usage.jsonl). A line holds facts about the
// call, never its values: no addresses, names, paths or results. `usage`
// (results/usage_report.h) reads it. Pure; the console writes and trims the file.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/values/json.h"

namespace reagentlive::cli {
struct UsageLine {
    std::string time;                   // UTC, 2026-01-02T03:04:05Z.
    std::string session;                // One console run; one-shot runs share theirs through the cache file.
    std::string verb, origin;           // The row's name and origin; an unknown verb as typed, origin empty.
    std::string caller;                 // tool, script or shell: only a tool's output lands in an agent's context.
    std::vector<std::string> keys;      // The args given, by key.
    std::string hash;                   // Of the whole input line: the same call made again.
    std::string outHash;                // Of what it printed (no timing, no context warning): the same result.
    uint64_t in = 0, out = 0;           // Characters. A batch's own line counts its wrapper alone.
    uint64_t digest = 0;                // The result's size when a digest stood in for it (out is the digest's).
    bool ok = true;
    std::string refused;                // The error's kind; empty when ok.
    double ms = 0;
    bool batch = false;                 // Ran inside a batch.
    // The call's max_chars when it set one (big: 0), else the cap a digest
    // stood for; nullopt otherwise.
    std::optional<uint64_t> cap;
    // Of the narrowing args' values (results/budget.h Narrowing, by path);
    // empty when the call gave none: a digest's follow-through compares it.
    std::string narrow;
};
constexpr uint64_t UsageCap = uint64_t{4} << 20; // Bytes; past it, the oldest sessions go at console start.
constexpr size_t MaxUsageVerb = 40;                 // An unknown verb is kept this long at most.

// FNV-1a, as 16 hex digits; UsageFold adds more text to a running hash.
constexpr uint64_t UsageHashStart = 0xcbf29ce484222325ull;
inline uint64_t UsageFold(uint64_t hash, std::string_view text) {
    for (const unsigned char c : text) hash = (hash ^ c) * 0x100000001b3ull;
    return hash;
}
inline std::string UsageHashText(uint64_t hash) {
    char out[20];
    snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(hash));
    return out;
}
inline std::string UsageHash(std::string_view text) { return UsageHashText(UsageFold(UsageHashStart, text)); }

inline std::string UsageText(const UsageLine& line) {
    Json json = {{"time", line.time}, {"session", line.session}, {"verb", line.verb}, {"origin", line.origin},
        {"caller", line.caller}, {"keys", line.keys}, {"hash", line.hash}, {"out_hash", line.outHash},
        {"in", line.in}, {"out", line.out},
        {"ok", line.ok}, {"ms", static_cast<double>(static_cast<int64_t>(line.ms * 10 + 0.5)) / 10}};
    if (!line.refused.empty()) json["refused"] = line.refused;
    if (line.batch) json["batch"] = true;
    if (line.digest) json["digest"] = line.digest;
    if (line.cap) json["cap"] = *line.cap;
    if (!line.narrow.empty()) json["narrow"] = line.narrow;
    return json.dump(-1, ' ', true, Json::error_handler_t::replace);
}

// One line, nullopt when it is not a usage line (a torn or foreign line is skipped).
inline std::optional<UsageLine> ParseUsageLine(std::string_view text) {
    const Json json = Json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object()) return std::nullopt;
    UsageLine line;
    const auto str = [&](const char* key, std::string& out) {
        if (!json.contains(key) || !json[key].is_string()) return false;
        out = json[key].get<std::string>();
        return true;
    };
    const auto number = [&](const char* key, uint64_t& out) {
        if (!json.contains(key) || !json[key].is_number_unsigned()) return false;
        out = json[key].get<uint64_t>();
        return true;
    };
    if (!str("time", line.time) || !str("session", line.session) || !str("verb", line.verb) ||
        !str("origin", line.origin) || !str("caller", line.caller) || !str("hash", line.hash) ||
        !number("in", line.in) || !number("out", line.out) || !json.contains("ok") || !json["ok"].is_boolean() ||
        !json.contains("ms") || !json["ms"].is_number() || !json.contains("keys") || !json["keys"].is_array())
        return std::nullopt;
    line.ok = json["ok"].get<bool>();
    line.ms = json["ms"].get<double>();
    for (const auto& key : json["keys"])
        if (key.is_string()) line.keys.push_back(key.get<std::string>());
    str("refused", line.refused);
    str("out_hash", line.outHash);
    number("digest", line.digest);
    if (uint64_t cap = 0; number("cap", cap)) line.cap = cap;
    str("narrow", line.narrow);
    line.batch = json.contains("batch") && json["batch"].is_boolean() && json["batch"].get<bool>();
    return line;
}

namespace usage_detail {
inline std::vector<std::string_view> Lines(std::string_view text) {
    std::vector<std::string_view> lines;
    for (size_t start = 0; start < text.size();) {
        const size_t end = text.find('\n', start);
        auto line = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (line.ends_with('\r')) line.remove_suffix(1);
        if (!line.empty()) lines.push_back(line);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return lines;
}
}

inline std::vector<UsageLine> ParseUsage(std::string_view text) {
    std::vector<UsageLine> lines;
    for (const auto line : usage_detail::Lines(text))
        if (auto parsed = ParseUsageLine(line)) lines.push_back(std::move(*parsed));
    return lines;
}

// The log cut to its newest sessions once it passes `cap`: whole sessions are
// kept, newest first, while they fit in three quarters of it, so a trim is
// not needed again at the next start. nullopt when the log is within the cap.
inline std::optional<std::string> TrimUsage(std::string_view text, uint64_t cap = UsageCap) {
    if (text.size() <= cap) return std::nullopt;
    const auto lines = usage_detail::Lines(text);
    std::map<std::string, uint64_t> bytes;          // Per session.
    std::vector<std::string> order;                 // Sessions by their last line, oldest first.
    std::vector<std::string> sessionOf(lines.size());
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto parsed = ParseUsageLine(lines[i]);
        if (!parsed) continue;
        sessionOf[i] = parsed->session;
        bytes[parsed->session] += lines[i].size() + 1;
    }
    for (size_t i = lines.size(); i-- > 0;)
        if (!sessionOf[i].empty() && std::ranges::find(order, sessionOf[i]) == order.end()) order.push_back(sessionOf[i]);
    std::vector<std::string> kept;                  // order is newest first here.
    uint64_t total = 0;
    for (const auto& session : order) {
        if (total + bytes[session] > cap / 4 * 3) break;
        total += bytes[session];
        kept.push_back(session);
    }
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i)
        if (!sessionOf[i].empty() && std::ranges::find(kept, sessionOf[i]) != kept.end()) {
            out += lines[i];
            out += '\n';
        }
    return out;
}
// Which usage session a console run is in, and which one its cache file hands
// on. One-shot (-c) runs chain with each other: one joins the handed-on session
// when it has its caller, and hands its own on. A named session (--session)
// and a long-lived console (calls on stdin: a script's session, the MCP
// adapter's) are their own: never handed on, so the cache keeps the one it held.
struct SessionPick { std::string session, handoff, handoffCaller; };
inline SessionPick PickSession(const std::string& fresh, const std::string& caller, bool oneShot,
                               const std::string& named, const std::string& handed, const std::string& handedCaller) {
    if (!named.empty()) return {named, handed, handedCaller};
    if (!oneShot) return {fresh, handed, handedCaller};
    const std::string& session = !handed.empty() && handedCaller == caller ? handed : fresh;
    return {session, session, caller};
}
}
