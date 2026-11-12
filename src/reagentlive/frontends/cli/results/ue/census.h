// reagentlive CLI: census results, their name filter and the comparison with the
// last census (`census-diff`). Built from client results only (no I/O),
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
#include "reagentlive/frontends/cli/results/schema.h"
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
// census-diff and the rows `--from` pages through; the one before it is what
// a later page of census-diff compares with. Tagged with the --class filter it
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

namespace shape {
struct CensusRow {
    std::string className;
    RefId ref;
    uint32_t count = 0;
    std::vector<RefId> samples;
    template<class V> static void Describe(V& v) {
        v("class", &CensusRow::className);
        v("ref", &CensusRow::ref);
        v("count", &CensusRow::count);
        v("samples", &CensusRow::samples);
    }
};
// census: `classes` and `instances` cover the whole census; `total` counts
// the rows left after --match, of which a page shows some.
struct Census {
    std::optional<std::string> filter;
    uint64_t classes = 0, instances = 0;
    int64_t ms = 0;
    std::vector<CensusRow> rows;
    uint64_t total = 0, from = 0;
    std::optional<uint64_t> nextCursor;
    std::optional<std::string> hint;
    template<class V> static void Describe(V& v) {
        v("filter", &Census::filter);
        v("classes", &Census::classes);
        v("instances", &Census::instances);
        v("ms", &Census::ms);
        v("rows", &Census::rows);
        v("total", &Census::total);
        v("from", &Census::from);
        v("next_cursor", &Census::nextCursor);
        v("hint", &Census::hint);
    }
};
struct BeforeAfter {
    uint64_t before = 0, after = 0;
    template<class V> static void Describe(V& v) {
        v("before", &BeforeAfter::before);
        v("after", &BeforeAfter::after);
    }
};
struct CensusDiffRow {
    std::string className;
    RefId ref;
    uint32_t before = 0, after = 0;
    std::vector<RefId> samples;
    template<class V> static void Describe(V& v) {
        v("class", &CensusDiffRow::className);
        v("ref", &CensusDiffRow::ref);
        v("before", &CensusDiffRow::before);
        v("after", &CensusDiffRow::after);
        v("samples", &CensusDiffRow::samples);
    }
};
// census-diff: the classes whose count differs from the census before;
// `classes` and `instances` give both whole censuses.
struct CensusDiff {
    std::optional<std::string> filter;
    BeforeAfter classes, instances;
    int64_t ms = 0;
    std::vector<CensusDiffRow> rows;
    uint64_t total = 0, from = 0;
    std::optional<uint64_t> nextCursor;
    std::optional<std::string> hint;
    template<class V> static void Describe(V& v) {
        v("filter", &CensusDiff::filter);
        v("classes", &CensusDiff::classes);
        v("instances", &CensusDiff::instances);
        v("ms", &CensusDiff::ms);
        v("rows", &CensusDiff::rows);
        v("total", &CensusDiff::total);
        v("from", &CensusDiff::from);
        v("next_cursor", &CensusDiff::nextCursor);
        v("hint", &CensusDiff::hint);
    }
};
}

namespace census_detail {
inline std::vector<RefId> Samples(std::span<const Ref> samples) {
    std::vector<RefId> list;
    for (const auto& sample : samples) list.push_back(FullRef(sample));
    return list;
}
// The paging keys beside the counts: the rows of all pages, where this page
// starts, where the next one starts and how to get it.
template<class Result> void Paging(Result& result, size_t total, size_t first, size_t shown) {
    const size_t next = first + shown;
    result.total = total;
    result.from = first;
    if (next < total) {
        result.nextCursor = next;
        result.hint = "--from " + std::to_string(next) + " for more, --limit 0 for all";
    }
}
}

// census: `matched` are the rows left after --match, of which `page` shows some.
inline shape::Census MakeCensus(std::string_view filter, const ue::CensusResult& census,
                                std::span<const ue::CensusRow> matched, CensusPage page, int64_t ms) {
    shape::Census out;
    if (!filter.empty()) out.filter = std::string(filter);
    out.classes = census.rows.size();
    out.instances = census.instances;
    out.ms = ms;
    const auto [first, shown] = CensusSlice(matched.size(), page);
    for (const auto& row : matched.subspan(first, shown))
        out.rows.push_back({row.type.name, FullRef(row.type.ref), row.count, census_detail::Samples(row.samples)});
    census_detail::Paging(out, matched.size(), first, shown);
    return out;
}

// census-diff: `changes` after --match, of which `page` shows some.
inline shape::CensusDiff MakeCensusDiff(const CensusBaseline& before, const ue::CensusResult& after,
                                        std::span<const CensusChange> changes, CensusPage page, int64_t ms) {
    shape::CensusDiff out;
    if (!before.filter.empty()) out.filter = before.filter;
    uint64_t instances = 0;
    for (const auto& row : before.rows) instances += row.count;
    out.classes = {before.rows.size(), after.rows.size()};
    out.instances = {instances, after.instances};
    out.ms = ms;
    const auto [first, shown] = CensusSlice(changes.size(), page);
    for (const auto& change : changes.subspan(first, shown))
        out.rows.push_back({change.name, FullRef(change.ref), change.before, change.after,
            census_detail::Samples(change.samples)});
    census_detail::Paging(out, changes.size(), first, shown);
    return out;
}

// Why a later page (`--from`) of a census under `filter` has nothing stored
// to page through; empty when it has. census-diff also needs the census before it.
inline std::string CensusPageProblem(const std::optional<CensusBaseline>& last, const std::optional<CensusBaseline>& before,
                                     std::string_view filter, bool changed) {
    if (last && last->filter == filter && (!changed || (before && before->filter == filter))) return {};
    return std::string("no stored census to page: run ") + (changed ? "census-diff" : "census") +
        (filter.empty() ? std::string() : " --class " + std::string(filter)) + " without --from first";
}

// Why `census-diff` under `filter` has nothing to compare with; empty when it has.
inline std::string CensusBaselineProblem(const std::optional<CensusBaseline>& baseline, std::string_view filter) {
    const auto command = [](std::string_view f) { return "census" + (f.empty() ? std::string() : " --class " + std::string(f)); };
    if (!baseline) return "no census baseline for this agent: run " + command(filter) + " first";
    if (baseline->filter != filter)
        return "the baseline is from " + command(baseline->filter) + ": run " + command(filter) + " first";
    return {};
}
}
