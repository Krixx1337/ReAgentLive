// reagentlive CLI: census results, their name filter and the comparison with the
// last census (`census --changed`). Built from client results only (no I/O),
// so tests pin the comparison and the result keys.
#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include "reagentlive/frontends/cli/syntax/parse.h"
#include "reagentlive/frontends/cli/values/ue/json_output.h"
#include "reagentlive/ue/client_ue.h"

namespace reagentlive::cli {
// One class's count in the baseline, with its sample instances.
struct CensusClass {
    std::string name;
    Ref ref;
    uint32_t count;
    std::vector<Ref> samples = {};
    bool operator==(const CensusClass&) const = default;
};
// A census's rows (all of them, before --match and --limit), kept for the
// console run and in the cache file: the last one is the baseline for
// --changed and the rows `--from` pages through; the one before it is what
// a later page of --changed compares with. Tagged with the --class filter it
// was taken with (empty: the whole table).
struct CensusBaseline {
    std::string filter;
    std::vector<CensusClass> rows;
    bool operator==(const CensusBaseline&) const = default;
};

inline CensusBaseline MakeCensusBaseline(std::string_view filter, const ue::CensusResult& census) {
    CensusBaseline baseline{std::string(filter), {}};
    baseline.rows.reserve(census.rows.size());
    for (const auto& row : census.rows) baseline.rows.push_back({row.type.name, row.type.ref, row.count, row.samples});
    return baseline;
}

// A stored census as the agent would return it, for a later page.
inline ue::CensusResult StoredCensus(const CensusBaseline& stored) {
    ue::CensusResult result{{}, 0};
    for (const auto& row : stored.rows) {
        result.rows.push_back({{row.ref, row.name, ""}, row.count, row.samples});
        result.instances += row.count;
    }
    return result;
}

// A class whose count differs between the baseline and the new census. A
// class absent before has `before` 0; one gone now has `after` 0 and no samples.
struct CensusChange {
    std::string name;
    Ref ref;
    uint32_t before, after;
    std::vector<Ref> samples;
};

// Slot and address equal, and serials equal or one not assigned yet (0).
inline bool SameObject(const Ref& a, const Ref& b) {
    return a.address == b.address && a.index == b.index && (a.serial == b.serial || !a.serial || !b.serial);
}

// Rows whose count differs, the largest change first, then by name. Classes
// match by their full ref, so two classes sharing a name stay apart.
inline std::vector<CensusChange> CensusChanges(const CensusBaseline& before, const ue::CensusResult& after) {
    std::unordered_map<int32_t, const CensusClass*> old; // By object-table index.
    for (const auto& row : before.rows) old.emplace(row.ref.index, &row);
    std::vector<CensusChange> changes;
    for (const auto& row : after.rows) {
        auto found = old.find(row.type.ref.index);
        if (found != old.end() && !SameObject(found->second->ref, row.type.ref)) found = old.end(); // Slot reused.
        const uint32_t count = found == old.end() ? 0 : found->second->count;
        if (found != old.end()) old.erase(found);
        if (count != row.count) changes.push_back({row.type.name, row.type.ref, count, row.count, row.samples});
    }
    for (const auto& row : before.rows) {
        const auto found = old.find(row.ref.index);
        if (found != old.end() && found->second == &row && row.count)
            changes.push_back({row.name, row.ref, row.count, 0, {}});
    }
    const auto size = [](const CensusChange& c) { return c.after > c.before ? c.after - c.before : c.before - c.after; };
    std::ranges::sort(changes, [&](const CensusChange& a, const CensusChange& b) {
        return size(a) != size(b) ? size(a) > size(b) : a.name < b.name;
    });
    return changes;
}

// Drops rows whose class name does not contain `match` (case-insensitive);
// an empty match keeps all.
template<class Row, class Name>
void KeepMatching(std::vector<Row>& rows, std::string_view match, Name name) {
    if (!match.empty()) std::erase_if(rows, [&](const Row& row) { return !text::ContainsNoCase(name(row), match); });
}

// The rows one page shows: from `from`, up to `limit` (0 = all).
struct CensusPage { uint32_t from = 0, limit = 0; };
inline std::pair<size_t, size_t> CensusSlice(size_t rows, CensusPage page) {
    const size_t first = std::min<size_t>(page.from, rows);
    return {first, page.limit ? std::min<size_t>(page.limit, rows - first) : rows - first};
}

namespace census_detail {
inline Json Samples(std::span<const Ref> samples) {
    Json list = Json::array();
    for (const auto& sample : samples) list.push_back(RefText(sample));
    return list;
}
inline Json Filter(std::string_view filter) { return filter.empty() ? Json(nullptr) : Json(filter); }
// The paging keys beside the counts: where this page starts, its rows, the
// rows of all pages, where the next page starts and how to get it.
inline void Paging(Json& result, size_t total, size_t first, size_t shown) {
    const size_t next = first + shown;
    result["from"] = first;
    result["shown"] = shown;
    result["total"] = total;
    result["next_cursor"] = next < total ? Json(next) : Json(nullptr);
    result["truncated"] = next < total;
    if (next < total) result["hint"] = "--from " + std::to_string(next) + " for more, --limit 0 for all";
}
}

// census: `matched` are the rows left after --match, of which `page` shows
// some. `classes` and `instances` cover the whole census.
inline Json CensusJson(std::string_view filter, const ue::CensusResult& census, std::span<const ue::CensusRow> matched,
                       CensusPage page, int64_t ms) {
    Json rows = Json::array();
    const auto [first, shown] = CensusSlice(matched.size(), page);
    for (const auto& row : matched.subspan(first, shown))
        rows.push_back({{"class", row.type.name}, {"ref", RefText(row.type.ref)}, {"count", row.count},
            {"samples", census_detail::Samples(row.samples)}});
    Json result = {{"filter", census_detail::Filter(filter)}, {"classes", census.rows.size()},
        {"instances", census.instances}, {"ms", ms}, {"rows", std::move(rows)}};
    census_detail::Paging(result, matched.size(), first, shown);
    return result;
}

// census --changed: `changes` after --match, of which `page` shows some.
// `classes` and `instances` give both whole censuses.
inline Json CensusChangedJson(const CensusBaseline& before, const ue::CensusResult& after,
                              std::span<const CensusChange> changes, CensusPage page, int64_t ms) {
    uint64_t instances = 0;
    for (const auto& row : before.rows) instances += row.count;
    Json rows = Json::array();
    const auto [first, shown] = CensusSlice(changes.size(), page);
    for (const auto& change : changes.subspan(first, shown))
        rows.push_back({{"class", change.name}, {"ref", RefText(change.ref)}, {"before", change.before},
            {"after", change.after}, {"samples", census_detail::Samples(change.samples)}});
    Json result = {{"filter", census_detail::Filter(before.filter)},
        {"classes", {{"before", before.rows.size()}, {"after", after.rows.size()}}},
        {"instances", {{"before", instances}, {"after", after.instances}}}, {"ms", ms}, {"rows", std::move(rows)}};
    census_detail::Paging(result, changes.size(), first, shown);
    return result;
}

// Why a later page (`--from`) of a census under `filter` has nothing stored
// to page through; empty when it has. --changed also needs the census before it.
inline std::string CensusPageProblem(const std::optional<CensusBaseline>& last, const std::optional<CensusBaseline>& before,
                                     std::string_view filter, bool changed) {
    if (last && last->filter == filter && (!changed || (before && before->filter == filter))) return {};
    return "no stored census to page: run census" + (filter.empty() ? std::string() : " --class " + std::string(filter)) +
        (changed ? " --changed" : "") + " without --from first";
}

// Why `census --changed` under `filter` has nothing to compare with; empty when it has.
inline std::string CensusBaselineProblem(const std::optional<CensusBaseline>& baseline, std::string_view filter) {
    const auto command = [](std::string_view f) { return "census" + (f.empty() ? std::string() : " --class " + std::string(f)); };
    if (!baseline) return "no census baseline for this agent: run " + command(filter) + " first";
    if (baseline->filter != filter)
        return "the baseline is from " + command(baseline->filter) + ": run " + command(filter) + " first";
    return {};
}
}
