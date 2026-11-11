// reagentlive CLI: labels for a static-analysis tool. One JSON line per address
// (module, module_size, timestamp, rva, name, kind) of one build; pure, so
// tests pin the merge rules.
#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "reagentlive/frontends/cli/results/result_json.h"

namespace reagentlive::cli {
struct LabelRow {
    uint64_t rva;
    std::string name, kind;
    bool operator==(const LabelRow&) const = default;
};

// Which row keeps an RVA that two rows share: what the reader confirmed
// (peek --label), then reflected names, then names derived from layouts.
inline int LabelRank(std::string_view kind) {
    return kind == "global" ? 0 : kind == "exec" ? 1 : kind == "interface" ? 2 : 3;
}

// Sorted by RVA, one row per RVA.
inline std::vector<LabelRow> MergeLabels(std::vector<LabelRow> rows) {
    std::ranges::stable_sort(rows, [](const LabelRow& a, const LabelRow& b) {
        return a.rva != b.rva ? a.rva < b.rva : LabelRank(a.kind) < LabelRank(b.kind);
    });
    const auto [first, last] = std::ranges::unique(rows, [](const LabelRow& a, const LabelRow& b) { return a.rva == b.rva; });
    rows.erase(first, last);
    return rows;
}

// Blueprint classes (generated names end in _C) share their native parent's vtable.
inline bool BlueprintClass(std::string_view name) { return name.ends_with("_C"); }

// One vtbl_<Class> row per vtable from (rva, class) pairs: a native class's
// name over a Blueprint one, else the first seen.
inline std::vector<LabelRow> VtableLabels(std::span<const std::pair<uint64_t, std::string>> owners) {
    std::vector<LabelRow> rows;
    for (const auto& [rva, name] : owners) {
        auto found = std::ranges::find(rows, rva, &LabelRow::rva);
        if (found == rows.end()) rows.push_back({rva, "vtbl_" + name, "vtable"});
        else if (BlueprintClass(found->name) && !BlueprintClass(name)) found->name = "vtbl_" + name;
    }
    return rows;
}

// One labels line of this build (same module name, image size and PE
// timestamp); nullopt for another build or a malformed line.
inline std::optional<LabelRow> ParseLabel(std::string_view line, const Module& module, uint32_t timestamp) {
    const auto row = Json::parse(line, nullptr, false);
    if (!row.is_object() || !row.contains("rva") || !row["rva"].is_string() || !row.contains("name") ||
        !row["name"].is_string() || !row.contains("kind") || !row["kind"].is_string() ||
        row.value("module", std::string{}).empty() || !text::SameText(row["module"].get<std::string>(), module.name) ||
        row.value("module_size", uint64_t{0}) != module.size || row.value("timestamp", uint64_t{0}) != timestamp)
        return std::nullopt;
    uint64_t rva = 0;
    if (!text::Number(row["rva"].get<std::string>(), rva) || rva >= module.size) return std::nullopt;
    auto name = row["name"].get<std::string>();
    if (name.empty()) return std::nullopt;
    return LabelRow{rva, std::move(name), row["kind"].get<std::string>()};
}
}
