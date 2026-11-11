// reagentlive CLI: state kept between console runs against one agent instance.
#pragma once
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "reagentlive/frontends/cli/results/census.h"
#include "reagentlive/frontends/cli/syntax/parse.h"
#include "reagentlive/frontends/cli/code/image.h"
#include "reagentlive/ue/client_ue.h"

// One file per target process, tagged with the agent's owner id (new on
// every injection), so a restarted game that reuses the pid or a re-injected
// agent never matches. It holds what costs round trips to rebuild: class
// names to refs, field tables by type, the loaded modules, the last $N list
// and the census baseline (so one-shot runs can use census --changed).
// Refs carry index and serial, so the agent refuses stale ones; field tables
// are checked against page 0 of every lookup (ue::Fields with a cache).
namespace reagentlive::cli {
struct CacheData {
    uint32_t owner = 0;
    std::vector<std::pair<std::string, Ref>> classes;
    std::vector<std::pair<int32_t, std::vector<ue::Field>>> tables; // By the type's object-table index.
    std::vector<Ref> refs;                                          // $1..$N.
    std::vector<Module> modules;                                    // Exe first.
    std::vector<std::pair<uint64_t, image::Layout>> layouts;        // Module headers by module base.
    std::optional<CensusBaseline> census, censusBefore;
};
constexpr std::string_view CacheHeader = "reagentlive cache 7";
constexpr size_t MaxCachedTables = 512, MaxCachedClasses = 512, MaxCachedModules = 1024, MaxCachedCensusRows = 65536;

namespace cache_detail {
inline std::vector<std::string_view> SplitTabs(std::string_view line) {
    std::vector<std::string_view> parts;
    for (size_t start = 0;;) {
        const size_t tab = line.find('\t', start);
        parts.push_back(line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
        if (tab == std::string_view::npos) return parts;
        start = tab + 1;
    }
}
}

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

inline std::string SaveCache(const CacheData& data) {
    std::ostringstream out;
    out << CacheHeader << "\nowner\t" << data.owner << '\n';
    for (const auto& module : data.modules)
        out << "module\t0x" << std::hex << module.base << std::dec << '\t' << module.size << '\t' << module.name << '\n';
    for (const auto& [base, layout] : data.layouts) {
        out << "image\t0x" << std::hex << base << "\t0x" << layout.exception.rva << std::dec << '\t'
            << layout.exception.size << '\t' << layout.sections.size() << '\n';
        for (const auto& section : layout.sections)
            out << "section\t" << section.name << "\t0x" << std::hex << section.rva << "\t0x" << section.size
                << "\t0x" << section.flags << std::dec << '\n';
    }
    for (const auto& [name, ref] : data.classes) out << "class\t" << name << '\t' << RefText(ref) << '\n';
    for (const auto& [type, fields] : data.tables) {
        out << "table\t" << type << '\t' << fields.size() << '\n';
        for (const auto& field : fields) out << FieldLine(field) << '\n';
    }
    for (const auto& ref : data.refs) out << "ref\t" << RefText(ref) << '\n';
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
    out << "end\n";
    return out.str();
}

// nullopt for anything malformed or incomplete: the console then starts fresh.
inline std::optional<CacheData> LoadCache(std::string_view text) {
    CacheData data;
    std::vector<std::string_view> lines;
    for (size_t start = 0; start < text.size();) {
        const size_t end = text.find('\n', start);
        // Empty trailing columns end in tabs: strip only the line ending.
        auto line = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (line.ends_with('\r')) line.remove_suffix(1);
        lines.push_back(line);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    if (lines.size() < 3 || lines[0] != CacheHeader) return std::nullopt;
    auto parts = cache_detail::SplitTabs(lines[1]);
    uint64_t number = 0;
    if (parts.size() != 2 || parts[0] != "owner" || !text::Number(parts[1], number) || !number || number > UINT32_MAX)
        return std::nullopt;
    data.owner = static_cast<uint32_t>(number);
    for (size_t i = 2; i < lines.size(); ++i) {
        parts = cache_detail::SplitTabs(lines[i]);
        if (parts[0] == "end") return data;
        if (parts[0] == "module" && parts.size() == 4) {
            Module module{std::string(parts[3]), 0, 0};
            if (!text::Number(parts[1], module.base) || !module.base || !text::Number(parts[2], module.size) ||
                !module.size || data.modules.size() >= MaxCachedModules) return std::nullopt;
            data.modules.push_back(std::move(module));
        } else if (parts[0] == "image" && parts.size() == 5) {
            uint64_t base = 0, count = 0;
            image::Layout layout;
            if (!text::Number(parts[1], base) || !base || !text::Number(parts[2], layout.exception.rva) ||
                !text::Number(parts[3], layout.exception.size) || !text::Number(parts[4], count) ||
                count > image::MaxSections || count > lines.size() - i - 1 || data.layouts.size() >= MaxCachedModules)
                return std::nullopt;
            for (uint64_t n = 0; n < count; ++n) {
                const auto row = cache_detail::SplitTabs(lines[++i]);
                uint64_t rva = 0, size = 0, flags = 0;
                if (row.size() != 5 || row[0] != "section" || !text::Number(row[2], rva) || !text::Number(row[3], size) ||
                    !text::Number(row[4], flags) || rva > UINT32_MAX || size > UINT32_MAX || flags > UINT32_MAX)
                    return std::nullopt;
                layout.sections.push_back({std::string(row[1]), static_cast<uint32_t>(rva), static_cast<uint32_t>(size),
                    static_cast<uint32_t>(flags)});
            }
            data.layouts.emplace_back(base, std::move(layout));
        } else if (parts[0] == "class" && parts.size() == 3) {
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
        } else if (parts[0] == "ref" && parts.size() == 2) {
            Ref ref{};
            if (!text::LiteralRef(parts[1], ref)) return std::nullopt;
            data.refs.push_back(ref);
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
        } else {
            return std::nullopt;
        }
    }
    return std::nullopt; // No end line: written partially.
}
}
