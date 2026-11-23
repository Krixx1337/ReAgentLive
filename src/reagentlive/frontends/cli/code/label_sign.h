// reagentlive CLI: signatures on labels rows, so a name survives a patch.
// labels-sign grows each row's pattern until one match lands on the row;
// labels-port finds signed rows of another build in the running image. Pure:
// tests pin the growth and the port choices.
//
// labels-sign is the mass signer, frozen as one fixed loop: no later starts,
// caller signatures, ranking or looser masks. A row it cannot sign is listed
// unsigned and gets a hand-made signature (the skill's recipe) or none; a
// failure is never a reason to extend this loop.
#pragma once
#include <algorithm>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/code/signature.h"
#include "reagentlive/frontends/cli/results/labels.h"
#include "reagentlive/frontends/cli/results/result_json.h"

namespace reagentlive::cli {
namespace shape {
struct LabelSignRow {
    std::string name, kind;
    HexNum rva;
    std::string status;                 // signed or unsigned.
    std::optional<uint64_t> length;     // Signed: the pattern's bytes.
    std::optional<HexNum> site;         // A global's: the code that loads it, where the pattern starts.
    std::optional<std::string> reason;  // Unsigned: why.
    template<class V> static void Describe(V& v) {
        v("name", &LabelSignRow::name);
        v("kind", &LabelSignRow::kind);
        v("rva", &LabelSignRow::rva);
        v("status", &LabelSignRow::status, "signed|unsigned");
        v("length", &LabelSignRow::length);
        v("site", &LabelSignRow::site);
        v("reason", &LabelSignRow::reason);
    }
};
struct LabelsSign {
    std::string path, module;
    uint32_t timestamp = 0;
    uint64_t signedRows = 0, unsignedRows = 0, skipped = 0, searches = 0;
    std::vector<LabelSignRow> rows;
    template<class V> static void Describe(V& v) {
        v("path", &LabelsSign::path);
        v("module", &LabelsSign::module);
        v("timestamp", &LabelsSign::timestamp);
        v("signed", &LabelsSign::signedRows);
        v("unsigned", &LabelsSign::unsignedRows);
        v("skipped", &LabelsSign::skipped);
        v("searches", &LabelsSign::searches);
        v("rows", &LabelsSign::rows);
    }
};
struct LabelPortRow {
    std::string name, kind;
    uint32_t from = 0;                  // The build (timestamp) the signature came from.
    std::string status;                 // ported, missing or ambiguous.
    std::optional<HexNum> rva;          // Ported: where it lies in this build.
    std::optional<uint64_t> matches;    // Ambiguous: how many.
    std::optional<std::string> reason;
    template<class V> static void Describe(V& v) {
        v("name", &LabelPortRow::name);
        v("kind", &LabelPortRow::kind);
        v("from", &LabelPortRow::from);
        v("status", &LabelPortRow::status, "ported|missing|ambiguous");
        v("rva", &LabelPortRow::rva);
        v("matches", &LabelPortRow::matches);
        v("reason", &LabelPortRow::reason);
    }
};
struct LabelsPort {
    std::string path, module;
    uint32_t timestamp = 0;
    uint64_t ported = 0, missing = 0, ambiguous = 0, present = 0, unsignedNames = 0, searches = 0;
    std::vector<LabelPortRow> rows;
    template<class V> static void Describe(V& v) {
        v("path", &LabelsPort::path);
        v("module", &LabelsPort::module);
        v("timestamp", &LabelsPort::timestamp);
        v("ported", &LabelsPort::ported);
        v("missing", &LabelsPort::missing);
        v("ambiguous", &LabelsPort::ambiguous);
        v("present", &LabelsPort::present);
        v("unsigned", &LabelsPort::unsignedNames);
        v("searches", &LabelsPort::searches);
        v("rows", &LabelsPort::rows);
    }
};
}

// A row's `sig`: a pattern sig-check takes, and where the row lies from its
// one match: a function at the match minus offset (usually 0), a global at
// the reference signature's target (resolve, at code that loads it). With
// the row's rva as --expect it is a sig-file line.
struct LabelSig {
    sig::Pattern pattern;
    uint64_t offset = 0;
    std::optional<sig::Resolve> resolve;
};

inline std::optional<LabelSig> ParseLabelSig(const Json& json) {
    if (!json.is_object() || !json.contains("pattern") || !json["pattern"].is_string()) return std::nullopt;
    auto pattern = sig::ParsePattern(json["pattern"].get<std::string>());
    if (!pattern || !sig::ChooseAnchor(*pattern)) return std::nullopt;
    LabelSig out{std::move(*pattern)};
    if (json.contains("offset")) {
        if (!json["offset"].is_number_unsigned()) return std::nullopt;
        out.offset = json["offset"].get<uint64_t>();
    }
    if (json.contains("resolve")) {
        if (!json["resolve"].is_string()) return std::nullopt;
        auto resolve = sig::ParseResolve(json["resolve"].get<std::string>());
        if (!resolve) return std::nullopt;
        out.resolve = *resolve;
    }
    if (out.offset && out.resolve) return std::nullopt;
    return out;
}

inline Json LabelSigJson(const LabelSig& sig) {
    Json out{{"pattern", sig::Text(sig.pattern)}};
    if (sig.offset) out["offset"] = sig.offset;
    if (sig.resolve)
        out["resolve"] = std::to_string(sig.resolve->next) + "," + std::to_string(sig.resolve->field) + "," +
            std::to_string(sig.resolve->size);
    return out;
}

// The first `length` bytes of a pattern, trailing wildcards dropped.
inline sig::Pattern CutPattern(const sig::Pattern& pattern, size_t length) {
    length = std::min(length, pattern.Size());
    while (length && !pattern.fixed[length - 1]) --length;
    return {std::vector<uint8_t>(pattern.bytes.begin(), pattern.bytes.begin() + static_cast<ptrdiff_t>(length)),
            std::vector<bool>(pattern.fixed.begin(), pattern.fixed.begin() + static_cast<ptrdiff_t>(length))};
}

constexpr size_t SignStart = 16, SignStep = 16;
struct Grown {
    std::optional<sig::Pattern> pattern;
    std::string reason;
};
// Lengthens a signature cut from `full` (the site's live bytes, address
// fields masked), from SignStart (at least `least`) bytes, until exactly one
// match lands where wanted (`resolves`). `ends`: where each decoded whole
// instruction ends, ascending; every cut ends on one (the shortest covering
// the length wanted). With none decoded it cuts raw bytes, trailing
// wildcards dropped. `check` runs one cut with the row's expect. Several
// matches jump to unique_at_length when it is known. No match, or one
// elsewhere, stops: more bytes cannot fix that.
template<class CheckFn>
Grown GrowSignature(const sig::Pattern& full, size_t least, std::span<const size_t> ends, CheckFn check) {
    const auto snap = [&](size_t wanted) {
        if (ends.empty()) return std::min(wanted, full.Size());
        for (const size_t end : ends)
            if (end >= wanted) return end;
        return ends.back();
    };
    const size_t cap = ends.empty() ? full.Size() : ends.back();
    size_t length = snap(std::max(SignStart, least));
    std::string reason = "no bytes to sign";
    while (length) {
        const auto cut = ends.empty() ? CutPattern(full, length) : sig::Pattern{
            std::vector<uint8_t>(full.bytes.begin(), full.bytes.begin() + static_cast<ptrdiff_t>(length)),
            std::vector<bool>(full.fixed.begin(), full.fixed.begin() + static_cast<ptrdiff_t>(length))};
        const std::expected<sig::CheckResult, std::string> result = check(cut);
        size_t next = length + SignStep;
        if (result) {
            if (result->resolves.value_or(false)) return {cut, {}};
            if (result->matches.size() < 2)
                return {std::nullopt, result->matches.empty() ? "no match where the row lies" : "its one match lies elsewhere"};
            reason = "not unique at " + std::to_string(cut.Size()) + " bytes (" + std::to_string(result->matches.size()) +
                " matches)";
            if (result->uniqueAtLength && *result->uniqueAtLength > length) next = *result->uniqueAtLength;
        } else {
            reason = result.error();
        }
        if (length >= cap) break;
        length = snap(next);
    }
    return {std::nullopt, reason};
}

// What labels-port works on: per name, the signed row of another build of
// this module (same module name, another image size or timestamp) to look
// for; the last such line wins (builds are appended in order). Names the
// running build already has are present; names with no signed row in any
// build are unsigned.
struct PortCandidate {
    std::string name, kind;
    uint32_t from = 0;
    LabelSig sig;
    Json sigJson;
};
struct PortPlan {
    std::vector<PortCandidate> candidates;   // In first-seen order.
    std::vector<std::string> present;
    size_t unsignedNames = 0;
};
inline PortPlan PlanPort(std::span<const std::string> lines, const Module& module, uint32_t timestamp) {
    std::set<std::string> here;
    for (const auto& line : lines)
        if (auto row = ParseLabel(line, module, timestamp)) here.insert(row->name);
    PortPlan plan;
    std::map<std::string, size_t> index;
    std::set<std::string> unsignedNames, presentNames;
    for (const auto& line : lines) {
        const auto row = Json::parse(line, nullptr, false);
        if (!row.is_object() || !row.contains("name") || !row["name"].is_string() || !row.contains("module") ||
            !row["module"].is_string() || !text::SameText(row["module"].get<std::string>(), module.name))
            continue;
        const auto size = row.value("module_size", uint64_t{0});
        const auto stamp = row.value("timestamp", uint64_t{0});
        if (size == module.size && stamp == timestamp) continue; // This build's own row.
        const auto name = row["name"].get<std::string>();
        if (name.empty()) continue;
        if (here.contains(name)) { presentNames.insert(name); continue; }
        const auto sig = row.contains("sig") ? ParseLabelSig(row["sig"]) : std::nullopt;
        if (!sig) { unsignedNames.insert(name); continue; }
        PortCandidate candidate{name, row.value("kind", std::string{}), static_cast<uint32_t>(stamp), *sig, row["sig"]};
        if (const auto found = index.find(name); found != index.end()) plan.candidates[found->second] = std::move(candidate);
        else {
            index.emplace(name, plan.candidates.size());
            plan.candidates.push_back(std::move(candidate));
        }
    }
    plan.present.assign(presentNames.begin(), presentNames.end());
    plan.unsignedNames = static_cast<size_t>(std::ranges::count_if(unsignedNames, [&](const std::string& name) {
        return !index.contains(name);
    }));
    return plan;
}

// One candidate's search in the running image: ported (one match, its row's
// rva inside the module), missing (none, or the search failed) or ambiguous.
struct PortOutcome {
    std::string status;
    std::optional<uint64_t> rva;
    size_t matches = 0;
    std::string reason;
};
inline PortOutcome PortResult(const LabelSig& sig, const std::expected<sig::CheckResult, std::string>& result,
                              const Module& module) {
    if (!result) return {"missing", std::nullopt, 0, result.error()};
    const auto& matches = result->matches;
    if (matches.empty()) return {"missing", std::nullopt, 0, "no match: the code changed"};
    if (matches.size() > 1) return {"ambiguous", std::nullopt, matches.size(), {}};
    const auto& match = matches.front();
    std::optional<uint64_t> address;
    if (sig.resolve) address = match.resolved;
    else if (match.address >= sig.offset) address = match.address - sig.offset;
    if (!address || *address < module.base || *address - module.base >= module.size)
        return {"missing", std::nullopt, 1, "the match points outside the module"};
    return {"ported", *address - module.base, 1, {}};
}
}
