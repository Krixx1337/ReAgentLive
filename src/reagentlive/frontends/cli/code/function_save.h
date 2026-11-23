// reagentlive CLI: functions saved for a static tool (`disasm` with
// `function` and `save.file`, optionally `save.callees`). JSON lines: one `function` row per saved
// function (module, size, timestamp, bounds), `code` and `data` rows (live
// bytes at their addresses, never overlapping), `name` rows (the functions,
// imports, call targets and labelled globals), and `chunk` rows (a
// function's chained continuations, listed apart). A loader builds a small
// database from it, so the tool decompiles those functions without
// analysing the whole image. Pure: rows and bytes come in, text goes out.
#pragma once
#include <algorithm>
#include <map>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/code/disasm.h"
#include "reagentlive/frontends/cli/values/json.h"

namespace reagentlive::cli::save {
constexpr size_t MaxData = 64; // Bytes kept per referenced constant or string.
// callees: the functions saved besides the first, and the code of the
// whole save in bytes (every function and chunk).
constexpr size_t MaxCallees = 16, MaxSaveBytes = 256 * 1024;
// Chunks saved per file: a function's continuations elsewhere (chained
// entries, switch targets); past it the result says capped.
constexpr size_t MaxChunks = 256;
// Bytes of junk a packed image may leave between a return and the next
// function: a walk that far into its unlisted piece is still a start.
constexpr uint64_t JunkBytes = 32;

struct Range {
    uint64_t start = 0, end = 0;
    bool operator==(const Range&) const = default;
};

// Sorted, with ranges that touch or overlap made one.
inline std::vector<Range> Merge(std::vector<Range> ranges) {
    std::ranges::sort(ranges, {}, &Range::start);
    std::vector<Range> merged;
    for (const auto& range : ranges) {
        if (!merged.empty() && range.start <= merged.back().end) merged.back().end = std::max(merged.back().end, range.end);
        else merged.push_back(range);
    }
    return merged;
}

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
    return Merge(std::move(ranges));
}

// The listed code as contiguous runs (a `follow` hop starts a new run).
inline std::vector<std::pair<uint64_t, std::vector<uint8_t>>> CodeRuns(std::span<const dis::Instruction> rows) {
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> runs;
    for (const auto& row : rows) {
        if (runs.empty() || runs.back().first + runs.back().second.size() != row.address) runs.push_back({row.address, {}});
        runs.back().second.insert(runs.back().second.end(), row.bytes.begin(), row.bytes.end());
    }
    return runs;
}

// The code runs of every saved part made one sorted set: runs that overlap
// or touch are joined, so no byte is written twice (one build, one set of
// bytes; where two listings overlap, the first one's bytes are kept).
inline std::vector<std::pair<uint64_t, std::vector<uint8_t>>> JoinedRuns(
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> runs) {
    std::ranges::sort(runs, {}, [](const auto& run) { return run.first; });
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> joined;
    for (auto& [at, bytes] : runs) {
        if (!joined.empty() && at <= joined.back().first + joined.back().second.size()) {
            auto& last = joined.back();
            const uint64_t end = last.first + last.second.size(), skip = end - at;
            if (skip < bytes.size()) last.second.insert(last.second.end(), bytes.begin() + skip, bytes.end());
        } else {
            joined.push_back({at, std::move(bytes)});
        }
    }
    return joined;
}

// The parts of a data run that no code run covers, in order.
inline std::vector<std::pair<uint64_t, std::vector<uint8_t>>> Uncovered(
    uint64_t at, std::span<const uint8_t> bytes, std::span<const std::pair<uint64_t, std::vector<uint8_t>>> code) {
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> out;
    uint64_t from = at;
    const uint64_t end = at + bytes.size();
    const auto keep = [&](uint64_t to) {
        if (to > from) out.push_back({from, {bytes.begin() + (from - at), bytes.begin() + (to - at)}});
    };
    for (const auto& [start, run] : code) { // Sorted and apart.
        const uint64_t stop = start + run.size();
        if (stop <= from || start >= end) continue;
        keep(std::min(start, end));
        from = std::max(from, stop);
    }
    keep(end);
    return out;
}

// Direct calls in the rows to other code in the module, in call order, each
// once: what `save.callees` saves besides the function (`self`).
inline std::vector<uint64_t> Callees(std::span<const dis::Instruction> rows, const Module& module, uint64_t self) {
    std::vector<uint64_t> out;
    for (const auto& row : rows) {
        if (row.branch != "call" || !row.target || row.ripTarget || *row.target == self) continue;
        if (*row.target < module.base || *row.target - module.base >= module.size) continue;
        if (std::ranges::find(out, *row.target) == out.end()) out.push_back(*row.target);
    }
    return out;
}

// Where code in the rows goes on without a call or return: relative jump and
// branch targets, in order, each once, then the byte after the last row when
// it does not end straight-line code (an entry cut where its chained chunk
// follows on). Where a function's chained chunks are found.
inline std::vector<uint64_t> Jumps(std::span<const dis::Instruction> rows) {
    std::vector<uint64_t> out;
    for (const auto& row : rows)
        if ((row.branch == "jump" || row.branch == "branch") && row.target && !row.ripTarget &&
            std::ranges::find(out, *row.target) == out.end()) out.push_back(*row.target);
    if (!rows.empty() && !rows.back().ends) {
        const uint64_t next = rows.back().address + rows.back().bytes.size();
        if (std::ranges::find(out, next) == out.end()) out.push_back(next);
    }
    return out;
}

// Unconditional direct jump targets in the rows, in order, each once, that
// lie in the module but outside every range in `held` [start, end): tail
// calls (MSVC's `jmp` to another function), which `save.callees` saves like calls.
inline std::vector<uint64_t> TailJumps(std::span<const dis::Instruction> rows, const Module& module,
                                       std::span<const Range> held) {
    std::vector<uint64_t> out;
    for (const auto& row : rows) {
        if (!row.directJump || !row.target) continue;
        const uint64_t at = *row.target;
        if (at < module.base || at - module.base >= module.size) continue;
        if (std::ranges::any_of(held, [at](const Range& r) { return r.start <= at && at < r.end; })) continue;
        if (std::ranges::find(out, at) == out.end()) out.push_back(at);
    }
    return out;
}

// One saved part: where it starts and its listed rows. `owner` is 0 for a
// function, else the start of the function whose chained chunk it is.
struct Saved {
    uint64_t start = 0;
    std::span<const dis::Instruction> rows;
    uint64_t owner = 0;
};

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
// there (an unreadable one is left out); `labels` names addresses (from a
// labels file). Names, one per address, first kind wins: each saved function
// (its label, else fn_<rva>), the import slots called through, the direct
// call targets not saved (label, else fn_<rva>), tail jump targets outside
// every saved part (`tail`), labelled RIP-relative targets (globals).
inline std::string Lines(const Module& module, uint32_t timestamp, std::span<const Saved> functions,
                         std::span<const std::pair<uint64_t, std::vector<uint8_t>>> data,
                         const std::map<uint64_t, std::string>& labels = {}) {
    std::string out;
    const auto line = [&](const Json& row) { out += row.dump() + "\n"; };
    const auto endOf = [](const Saved& part) {
        return part.rows.empty() ? part.start : part.rows.back().address + part.rows.back().bytes.size();
    };
    for (const auto& function : functions)
        if (!function.owner)
            line({{"row", "function"}, {"module", module.name}, {"module_size", module.size}, {"timestamp", timestamp},
                {"base", Hex(module.base)}, {"start", Hex(function.start)}, {"end", Hex(endOf(function))}});
    for (const auto& chunk : functions)
        if (chunk.owner)
            line({{"row", "chunk"}, {"start", Hex(chunk.start)}, {"end", Hex(endOf(chunk))}, {"function", Hex(chunk.owner)}});
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> runs;
    for (const auto& function : functions)
        for (auto& run : CodeRuns(function.rows)) runs.push_back(std::move(run));
    const auto code = JoinedRuns(std::move(runs));
    for (const auto& [at, bytes] : code)
        line({{"row", "code"}, {"address", Hex(at)}, {"rva", Hex(at - module.base)}, {"bytes", HexText(bytes)}});
    for (const auto& [from, held] : data)
        for (const auto& [at, bytes] : Uncovered(from, held, code))
            line({{"row", "data"}, {"address", Hex(at)}, {"rva", Hex(at - module.base)}, {"bytes", HexText(bytes)}});
    std::vector<uint64_t> named;
    const auto name = [&](uint64_t at, const std::string& text, std::string_view what) {
        if (std::ranges::find(named, at) != named.end()) return;
        named.push_back(at);
        line({{"row", "name"}, {"address", Hex(at)}, {"rva", Hex(at - module.base)}, {"name", text}, {"what", what}});
    };
    const auto labelled = [&](uint64_t at) {
        const auto found = labels.find(at);
        return found != labels.end() ? found->second : "fn_" + Hex(at - module.base).substr(2);
    };
    for (const auto& function : functions)
        if (!function.owner) name(function.start, labelled(function.start), "function");
    for (const auto& function : functions)
        for (const auto& row : function.rows)
            if (row.import && row.target) name(*row.target, *row.import, "import");
    const auto inside = [&](uint64_t at) { return at >= module.base && at - module.base < module.size; };
    for (const auto& function : functions)
        for (const auto& row : function.rows)
            if (row.branch == "call" && row.target && !row.ripTarget && inside(*row.target))
                name(*row.target, labelled(*row.target), "call");
    std::vector<Range> parts;
    for (const auto& part : functions) parts.push_back({part.start, endOf(part)});
    for (const auto& function : functions)
        for (const uint64_t target : TailJumps(function.rows, module, parts)) name(target, labelled(target), "tail");
    for (const auto& function : functions)
        for (const auto& row : function.rows)
            if (row.ripTarget && row.target && !row.import && labels.contains(*row.target))
                name(*row.target, labels.at(*row.target), "global");
    return out;
}
}
