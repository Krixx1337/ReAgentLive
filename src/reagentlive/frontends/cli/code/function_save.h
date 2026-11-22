// reagentlive CLI: one function saved for a static tool (`disasm --function
// --save <file>`). JSON lines: a `function` row (module, size, timestamp,
// bounds), `code` and `data` rows (live bytes at their addresses), and
// `name` rows (imports and the function itself). A loader builds a small
// database from it, so the tool decompiles that function without analysing
// the whole image. Pure: rows and bytes come in, text goes out.
#pragma once
#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/code/disasm.h"
#include "reagentlive/frontends/cli/values/json.h"

namespace reagentlive::cli::save {
constexpr size_t MaxData = 64; // Bytes kept per referenced constant or string.

struct Range {
    uint64_t start = 0, end = 0;
    bool operator==(const Range&) const = default;
};

// The read-only data the rows reference: each load's operand (at least 8
// bytes) and each string with its terminator, at most MaxData each, merged
// where they touch. `constant` says whether an address is read-only.
inline std::vector<Range> DataRanges(std::span<const dis::Instruction> rows, const dis::ConstantFn& constant) {
    std::vector<Range> ranges;
    for (const auto& row : rows) {
        if (!row.target || !row.ripTarget || row.import || !constant(*row.target)) continue;
        size_t length = 0;
        if (row.string) length = (row.string->size() + 1) * (row.wide ? 2 : 1);
        else if (row.loadSize) length = std::max<size_t>(row.loadSize, 8);
        if (!length) continue;
        ranges.push_back({*row.target, *row.target + std::min(length, MaxData)});
    }
    std::ranges::sort(ranges, {}, &Range::start);
    std::vector<Range> merged;
    for (const auto& range : ranges) {
        if (!merged.empty() && range.start <= merged.back().end) merged.back().end = std::max(merged.back().end, range.end);
        else merged.push_back(range);
    }
    return merged;
}

// The listed code as contiguous runs (a --follow hop starts a new run).
inline std::vector<std::pair<uint64_t, std::vector<uint8_t>>> CodeRuns(std::span<const dis::Instruction> rows) {
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> runs;
    for (const auto& row : rows) {
        if (runs.empty() || runs.back().first + runs.back().second.size() != row.address) runs.push_back({row.address, {}});
        runs.back().second.insert(runs.back().second.end(), row.bytes.begin(), row.bytes.end());
    }
    return runs;
}

inline std::string Hex(uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(value));
    return text;
}
inline std::string HexText(std::span<const uint8_t> bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const uint8_t b : bytes) {
        out += digits[b >> 4];
        out += digits[b & 15];
    }
    return out;
}

// The file's text. `data` pairs each DataRanges range with the bytes read
// there (an unreadable one is left out).
inline std::string Lines(const Module& module, uint32_t timestamp, uint64_t functionStart,
                         std::span<const dis::Instruction> rows,
                         std::span<const std::pair<uint64_t, std::vector<uint8_t>>> data) {
    std::string out;
    const auto line = [&](const Json& row) { out += row.dump() + "\n"; };
    const uint64_t end = rows.empty() ? functionStart : rows.back().address + rows.back().bytes.size();
    line({{"row", "function"}, {"module", module.name}, {"module_size", module.size}, {"timestamp", timestamp},
        {"base", Hex(module.base)}, {"start", Hex(functionStart)}, {"end", Hex(end)}});
    for (const auto& [at, bytes] : CodeRuns(rows))
        line({{"row", "code"}, {"address", Hex(at)}, {"rva", Hex(at - module.base)}, {"bytes", HexText(bytes)}});
    for (const auto& [at, bytes] : data)
        line({{"row", "data"}, {"address", Hex(at)}, {"rva", Hex(at - module.base)}, {"bytes", HexText(bytes)}});
    line({{"row", "name"}, {"address", Hex(functionStart)}, {"rva", Hex(functionStart - module.base)},
        {"name", "fn_" + Hex(functionStart - module.base).substr(2)}, {"what", "function"}});
    std::vector<uint64_t> named;
    for (const auto& row : rows) {
        if (!row.import || !row.target || std::ranges::find(named, *row.target) != named.end()) continue;
        named.push_back(*row.target);
        line({{"row", "name"}, {"address", Hex(*row.target)}, {"rva", Hex(*row.target - module.base)},
            {"name", *row.import}, {"what", "import"}});
    }
    return out;
}
}
