// reagentlive CLI, Unreal pack: its block of the cache file (state/cache.h):
// class names to refs, field tables by type, the census baselines and the
// last snap (so one-shot runs, and the MCP adapter's console per call, can
// use census --changed and dump --changed). Field tables are checked against
// page 0 of every lookup (ue::Fields with a cache). Field lines are shared
// with snapshot files.
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "reagentlive/frontends/cli/state/cache.h"
#include "reagentlive/frontends/cli/results/ue/census.h"
#include "reagentlive/frontends/cli/values/format.h"
#include "reagentlive/ue/client_ue.h"

namespace reagentlive::cli {
// snap's baseline for dump --changed: the object, its field table and its bytes.
struct Snapshot {
    Ref object;
    std::vector<ue::Field> fields;
    std::vector<uint8_t> bytes;
    bool operator==(const Snapshot&) const = default;
};
struct UnrealCache {
    std::vector<std::pair<std::string, Ref>> classes;
    std::vector<std::pair<int32_t, std::vector<ue::Field>>> tables; // By the type's object-table index.
    std::optional<CensusBaseline> census, censusBefore;
    std::optional<Snapshot> snap;
};
constexpr std::string_view UnrealCachePack = "ue";
constexpr size_t MaxCachedTables = 512, MaxCachedClasses = 512, MaxCachedCensusRows = 65536;
constexpr size_t MaxCachedSnapBytes = size_t{1} << 20; // A larger snap stays in its run (or its file).

// "field\t..." with every ue::Field member; shared with snapshot files.
inline std::string FieldLine(const ue::Field& f) {
    std::ostringstream out;
    out << "field\t" << f.offset << '\t' << f.size << '\t' << unsigned{f.boolMask} << '\t' << f.elementSize << '\t'
        << (f.structType.address ? RefText(f.structType) : "-") << '\t' << f.type << '\t' << f.name << '\t'
        << f.owner << '\t' << f.elementType << '\t' << f.structName << '\t' << f.stride << '\t' << f.valueOffset << '\t'
        << f.valueSize << '\t' << f.valueType << '\t' << (f.valueStructType.address ? RefText(f.valueStructType) : "-")
        << '\t' << f.valueStructName;
    return out.str();
}

inline std::optional<ue::Field> ParseFieldLine(std::string_view line) {
    const auto parts = cache_detail::SplitTabs(line);
    uint64_t offset, size, mask, elementSize, stride, valueOffset, valueSize;
    ue::Field f{};
    if (parts.size() != 17 || parts[0] != "field" || !text::Number(parts[1], offset) || !text::Number(parts[2], size) ||
        !text::Number(parts[3], mask) || !text::Number(parts[4], elementSize) || offset > UINT32_MAX ||
        size > UINT32_MAX || mask > 0xff || elementSize > UINT32_MAX ||
        (parts[5] != "-" && !text::LiteralRef(parts[5], f.structType)) || !text::Number(parts[11], stride) ||
        !text::Number(parts[12], valueOffset) || !text::Number(parts[13], valueSize) || stride > UINT32_MAX ||
        valueOffset > UINT32_MAX || valueSize > UINT32_MAX ||
        (parts[15] != "-" && !text::LiteralRef(parts[15], f.valueStructType))) return std::nullopt;
    f.stride = static_cast<uint32_t>(stride);
    f.valueOffset = static_cast<uint32_t>(valueOffset);
    f.valueSize = static_cast<uint32_t>(valueSize);
    f.valueType = parts[14];
    f.valueStructName = parts[16];
    f.offset = static_cast<uint32_t>(offset);
    f.size = static_cast<uint32_t>(size);
    f.boolMask = static_cast<uint8_t>(mask);
    f.elementSize = static_cast<uint32_t>(elementSize);
    f.type = parts[6]; f.name = parts[7]; f.owner = parts[8]; f.elementType = parts[9]; f.structName = parts[10];
    return f;
}

// The pack's lines for CacheData::packs.
inline std::vector<std::string> SaveUnrealCache(const UnrealCache& data) {
    std::ostringstream out;
    for (const auto& [name, ref] : data.classes) out << "class\t" << name << '\t' << RefText(ref) << '\n';
    for (const auto& [type, fields] : data.tables) {
        out << "table\t" << type << '\t' << fields.size() << '\n';
        for (const auto& field : fields) out << FieldLine(field) << '\n';
    }
    // census-row: ref, count, samples (comma-separated, "-" for none), name.
    const auto census = [&](const char* tag, const std::optional<CensusBaseline>& stored) {
        if (!stored || stored->rows.size() > MaxCachedCensusRows) return;
        out << tag << '\t' << stored->filter << '\t' << stored->rows.size() << '\n';
        for (const auto& row : stored->rows) {
            std::string samples;
            for (const auto& sample : row.samples) samples += (samples.empty() ? "" : ",") + RefText(sample);
            out << "census-row\t" << RefText(row.ref) << '\t' << row.count << '\t' << (samples.empty() ? "-" : samples)
                << '\t' << row.name << '\n';
        }
    };
    census("census", data.census);
    census("census-before", data.censusBefore);
    // snap: ref and field count, the field lines, then snap-bytes as hex.
    if (data.snap && data.snap->bytes.size() <= MaxCachedSnapBytes) {
        out << "snap\t" << RefText(data.snap->object) << '\t' << data.snap->fields.size() << '\n';
        for (const auto& field : data.snap->fields) out << FieldLine(field) << '\n';
        out << "snap-bytes\t" << format_detail::Hex(data.snap->bytes, data.snap->bytes.size()) << '\n';
    }
    std::vector<std::string> lines;
    std::string line;
    for (std::istringstream in(out.str()); std::getline(in, line);) lines.push_back(std::move(line));
    return lines;
}

// nullopt for anything malformed: the console then starts the pack fresh.
inline std::optional<UnrealCache> LoadUnrealCache(std::span<const std::string> block) {
    UnrealCache data;
    const std::vector<std::string_view> lines(block.begin(), block.end());
    for (size_t i = 0; i < lines.size(); ++i) {
        auto parts = cache_detail::SplitTabs(lines[i]);
        if (parts[0] == "class" && parts.size() == 3) {
            Ref ref{};
            if (!text::LiteralRef(parts[2], ref) || data.classes.size() >= MaxCachedClasses) return std::nullopt;
            data.classes.emplace_back(std::string(parts[1]), ref);
        } else if (parts[0] == "table" && parts.size() == 3) {
            uint64_t type = 0, count = 0;
            if (!text::Number(parts[1], type) || type > INT32_MAX || !text::Number(parts[2], count) ||
                count > ue::MaxFieldStart || count > lines.size() - i - 1 || data.tables.size() >= MaxCachedTables)
                return std::nullopt;
            auto& table = data.tables.emplace_back(static_cast<int32_t>(type), std::vector<ue::Field>{}).second;
            for (uint64_t n = 0; n < count; ++n) {
                auto field = ParseFieldLine(lines[++i]);
                if (!field) return std::nullopt;
                table.push_back(std::move(*field));
            }
        } else if ((parts[0] == "census" || parts[0] == "census-before") && parts.size() == 3) {
            auto& stored = parts[0] == "census" ? data.census : data.censusBefore;
            uint64_t count = 0;
            if (stored || !text::Number(parts[2], count) || count > MaxCachedCensusRows || count > lines.size() - i - 1)
                return std::nullopt;
            auto& census = stored.emplace(CensusBaseline{std::string(parts[1]), {}});
            for (uint64_t n = 0; n < count; ++n) {
                const auto row = cache_detail::SplitTabs(lines[++i]);
                CensusClass entry{};
                uint64_t instances = 0;
                if (row.size() != 5 || row[0] != "census-row" || !text::LiteralRef(row[1], entry.ref) ||
                    !text::Number(row[2], instances) || instances > UINT32_MAX) return std::nullopt;
                entry.count = static_cast<uint32_t>(instances);
                for (std::string_view samples = row[3]; samples != "-" && !samples.empty();) {
                    const size_t comma = samples.find(',');
                    Ref sample{};
                    if (!text::LiteralRef(samples.substr(0, comma), sample) || entry.samples.size() >= ue::MaxCensusSamples)
                        return std::nullopt;
                    entry.samples.push_back(sample);
                    samples = comma == std::string_view::npos ? std::string_view{} : samples.substr(comma + 1);
                }
                entry.name = row[4];
                census.rows.push_back(std::move(entry));
            }
        } else if (parts[0] == "snap" && parts.size() == 3) {
            uint64_t count = 0;
            Snapshot snap;
            if (data.snap || !text::LiteralRef(parts[1], snap.object) || !text::Number(parts[2], count) ||
                count > ue::MaxFieldStart || count + 1 > lines.size() - i - 1) return std::nullopt;
            for (uint64_t n = 0; n < count; ++n) {
                auto field = ParseFieldLine(lines[++i]);
                if (!field) return std::nullopt;
                snap.fields.push_back(std::move(*field));
            }
            const auto row = cache_detail::SplitTabs(lines[++i]);
            uint32_t length = 0;
            if (row.size() != 2 || row[0] != "snap-bytes") return std::nullopt;
            snap.bytes.resize(row[1].size() / 3 + 1);
            if (snap.bytes.size() > MaxCachedSnapBytes + 1 || !text::Bytes(row[1], snap.bytes.data(), snap.bytes.size(), length))
                return std::nullopt;
            snap.bytes.resize(length);
            data.snap = std::move(snap);
        } else {
            return std::nullopt;
        }
    }
    return data;
}
}
