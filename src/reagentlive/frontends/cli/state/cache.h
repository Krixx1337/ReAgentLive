// reagentlive CLI: state kept between console runs against one agent instance.
#pragma once
#include <cstdint>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "reagentlive/frontends/cli/call/build.h"
#include "reagentlive/frontends/cli/code/image.h"

// One file per target process, tagged with the agent's owner id (new on
// every injection), so a restarted game that reuses the pid or a re-injected
// agent never matches. It holds what costs round trips to rebuild: the loaded
// modules and their headers, and a block per engine pack
// (packs/ue/cli/state/cache_ue.h) that the core keeps without reading it. It also names the usage
// log's session (state/usage.h) and its caller: one-shot runs of the same
// caller share it, so a shell's checks never join a tool's session.
namespace reagentlive::cli {
// A pack's block: its name and its lines, verbatim.
struct CachePack {
    std::string name;
    std::vector<std::string> lines;
    bool operator==(const CachePack&) const = default;
};
// search text: the last job's pattern length and character step (1 or 2), so
// search-job in a later run shows the text around its hits too.
struct SearchText {
    uint32_t job = 0, length = 0;
    uint8_t step = 0;
    bool operator==(const SearchText&) const = default;
};
struct CacheData {
    uint32_t owner = 0;
    std::string session;                                            // The usage log's; empty: none.
    std::string sessionCaller;                                      // tool, script or shell.
    std::vector<Module> modules;                                    // Exe first.
    std::vector<std::pair<uint64_t, image::Layout>> layouts;        // Module headers by module base.
    std::vector<CachePack> packs;
    std::string watchFile;                                          // watch-start with `file`.
    SearchText searchText;                                          // The last search's, when it was text.
};
constexpr std::string_view CacheHeader = "reagentlive cache 11";
constexpr size_t MaxCachedModules = 1024, MaxCachedPackLines = size_t{1} << 22;

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

inline std::string SaveCache(const CacheData& data) {
    std::ostringstream out;
    out << CacheHeader << "\nowner\t" << data.owner << '\n';
    if (!data.session.empty()) out << "session\t" << data.session << '\t' << data.sessionCaller << '\n';
    if (!data.watchFile.empty()) out << "watch_file\t" << data.watchFile << '\n';
    if (data.searchText.step)
        out << "search_text\t" << data.searchText.job << '\t' << data.searchText.length << '\t'
            << static_cast<int>(data.searchText.step) << '\n';
    for (const auto& module : data.modules)
        out << "module\t0x" << std::hex << module.base << std::dec << '\t' << module.size << '\t' << module.name << '\n';
    for (const auto& [base, layout] : data.layouts) {
        out << "image\t0x" << std::hex << base << "\t0x" << layout.exception.rva << std::dec << '\t'
            << layout.exception.size << '\t' << layout.sections.size() << '\n';
        for (const auto& section : layout.sections)
            out << "section\t" << section.name << "\t0x" << std::hex << section.rva << "\t0x" << section.size
                << "\t0x" << section.flags << std::dec << '\n';
    }
    for (const auto& pack : data.packs) {
        out << "pack\t" << pack.name << '\t' << pack.lines.size() << '\n';
        for (const auto& line : pack.lines) out << line << '\n';
    }
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
        if (parts[0] == "session" && parts.size() == 3 && !parts[1].empty()) {
            data.session = parts[1];
            data.sessionCaller = parts[2];
            continue;
        }
        if (parts[0] == "watch_file" && parts.size() == 2 && !parts[1].empty()) {
            data.watchFile = parts[1];
            continue;
        }
        if (parts[0] == "search_text" && parts.size() == 4) {
            uint64_t job = 0, length = 0, step = 0;
            if (!text::Number(parts[1], job) || job > UINT32_MAX || !text::Number(parts[2], length) || length > UINT32_MAX ||
                !text::Number(parts[3], step) || (step != 1 && step != 2)) return std::nullopt;
            data.searchText = {static_cast<uint32_t>(job), static_cast<uint32_t>(length), static_cast<uint8_t>(step)};
            continue;
        }
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
        } else if (parts[0] == "pack" && parts.size() == 3) {
            uint64_t count = 0;
            if (parts[1].empty() || !text::Number(parts[2], count) || count > MaxCachedPackLines ||
                count > lines.size() - i - 1) return std::nullopt;
            auto& pack = data.packs.emplace_back(CachePack{std::string(parts[1]), {}});
            for (uint64_t n = 0; n < count; ++n) pack.lines.emplace_back(lines[++i]);
        } else {
            return std::nullopt;
        }
    }
    return std::nullopt; // No end line: written partially.
}
}
