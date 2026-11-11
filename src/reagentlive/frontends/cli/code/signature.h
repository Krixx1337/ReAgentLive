// reagentlive CLI: byte signatures checked against the live image (`sig`).
// The agent (or user) makes a signature; this says whether it is unique in a
// module, lands where intended, and what to change when it does not. Pure:
// searching and reading come in as callbacks, so tests stub them.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "reagentlive/core/client.h"
#include "reagentlive/core/protocol.h"
#include "reagentlive/frontends/cli/syntax/pattern.h"

namespace reagentlive::cli::sig {
namespace detail {
inline const Module* ModuleAt(uint64_t address, std::span<const Module> modules) {
    for (const auto& module : modules)
        if (address >= module.base && address - module.base < module.size) return &module;
    return nullptr;
}
}

// A fixed field that holds an address which moves with every build: a
// relative call, jump or branch target, a RIP-relative displacement, or an
// absolute 8-byte value. Flagged only where, read that way at `address`, it
// points inside a loaded module. A byte heuristic, not a decoder.
struct AddressField {
    size_t offset = 0, size = 0;
    const char* kind = "";  // call, jump, branch, rip, absolute.
    uint64_t target = 0;
    bool operator==(const AddressField& other) const {
        return offset == other.offset && size == other.size && std::string_view(kind) == other.kind &&
            target == other.target;
    }
};

namespace detail {
// Immediate bytes after a ModRM operand for the opcode before `modrm`, or -1
// when that byte is not a ModRM-taking opcode this heuristic knows.
inline int ModRmImmediate(std::span<const uint8_t> b, size_t modrm) {
    if (!modrm) return -1;
    const uint8_t op = b[modrm - 1];
    const bool twoByte = modrm >= 2 && b[modrm - 2] == 0x0F;
    const bool threeByte = modrm >= 3 && b[modrm - 3] == 0x0F && (b[modrm - 2] == 0x38 || b[modrm - 2] == 0x3A);
    const bool operand16 = (modrm >= 2 && b[modrm - 2] == 0x66) || (modrm >= 3 && b[modrm - 3] == 0x66);
    const int imm32 = operand16 ? 2 : 4;
    if (threeByte) return b[modrm - 2] == 0x3A ? 1 : 0;
    if (twoByte) {
        if (op >= 0x80 && op <= 0x8F) return -1; // Jcc rel32: a branch, not ModRM.
        if (op == 0x05 || op == 0x0B || op == 0x31 || op == 0xA2) return -1;
        if (op == 0x70 || op == 0x71 || op == 0x72 || op == 0x73 || op == 0xBA || op == 0xC2 || op == 0xC4 ||
            op == 0xC5 || op == 0xC6 || op == 0xA4 || op == 0xAC) return 1;
        return 0;
    }
    if (op < 0x40 && (op & 7) < 4) return 0;               // add, or, adc, sbb, and, sub, xor, cmp r/m forms.
    switch (op) {
    case 0x63: case 0x84: case 0x85: case 0x86: case 0x87: case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8D:
    case 0x8F: case 0xD0: case 0xD1: case 0xD2: case 0xD3: case 0xFE: case 0xFF: return 0;
    case 0x6B: case 0x80: case 0x83: case 0xC0: case 0xC1: case 0xC6: return 1;
    case 0x69: case 0x81: case 0xC7: return imm32;
    case 0xF6: case 0xF7: {
        const int reg = (b[modrm] >> 3) & 7;
        return reg <= 1 ? (op == 0xF6 ? 1 : imm32) : 0;
    }
    default: return -1;
    }
}
inline int64_t Signed(std::span<const uint8_t> bytes, size_t at, size_t size) {
    switch (size) {
    case 1: return static_cast<int8_t>(bytes[at]);
    case 2: { int16_t v; memcpy(&v, bytes.data() + at, 2); return v; }
    case 4: { int32_t v; memcpy(&v, bytes.data() + at, 4); return v; }
    default: { int64_t v; memcpy(&v, bytes.data() + at, 8); return v; }
    }
}
}

inline std::vector<AddressField> AddressFields(const Pattern& pattern, uint64_t address,
                                               std::span<const Module> modules) {
    std::vector<AddressField> fields;
    const auto& b = pattern.bytes;
    const auto fixed = [&](size_t from, size_t count) {
        if (from + count > pattern.Size()) return false;
        for (size_t i = from; i < from + count; ++i) if (!pattern.fixed[i]) return false;
        return true;
    };
    const auto flag = [&](size_t field, size_t size, const char* kind, uint64_t target) {
        if (detail::ModuleAt(target, modules)) fields.push_back({field, size, kind, target});
    };
    for (size_t p = 0; p < pattern.Size(); ++p) {
        if (!pattern.fixed[p]) continue;
        if ((b[p] == 0xE8 || b[p] == 0xE9) && fixed(p + 1, 4))
            flag(p + 1, 4, b[p] == 0xE8 ? "call" : "jump", address + p + 5 + detail::Signed(b, p + 1, 4));
        if (b[p] == 0x0F && fixed(p + 1, 5) && (b[p + 1] & 0xF0) == 0x80)
            flag(p + 2, 4, "branch", address + p + 6 + detail::Signed(b, p + 2, 4));
        // ModRM with mod 00 and r/m 101: [rip + disp32].
        if ((b[p] & 0xC7) == 0x05 && p && pattern.fixed[p - 1] && fixed(p + 1, 4)) {
            const int immediate = detail::ModRmImmediate(b, p);
            if (immediate >= 0)
                flag(p + 1, 4, "rip", address + p + 5 + static_cast<uint64_t>(immediate) + detail::Signed(b, p + 1, 4));
        }
        if (fixed(p, 8)) flag(p, 8, "absolute", static_cast<uint64_t>(detail::Signed(b, p, 8)));
    }
    return fields;
}

// The same bytes with every flagged field wildcarded.
inline Pattern Masked(Pattern pattern, std::span<const AddressField> fields) {
    for (const auto& field : fields)
        for (size_t i = field.offset; i < field.offset + field.size && i < pattern.Size(); ++i) {
            pattern.fixed[i] = false;
            pattern.bytes[i] = 0;
        }
    return pattern;
}


// What the matcher needs from the agent. A search page: hits from `from` up
// to `to`, and where to resume (0: done). A read batch: each span's bytes,
// or nullopt where it was unreadable.
struct SearchPage { std::vector<uint64_t> hits; uint64_t resume = 0; };
using SearchFn = std::function<std::expected<SearchPage, std::string>(
    uint64_t from, uint64_t to, std::span<const uint8_t> anchor, size_t maxHits)>;
using ReadFn = std::function<std::expected<std::vector<std::optional<std::vector<uint8_t>>>, std::string>(
    std::span<const ReadSpan> reads)>;

struct Check {
    Pattern pattern;
    std::optional<uint64_t> expect;   // Where the signature (or with resolve, its target) should land.
    uint64_t offset = 0;              // The match lies this far past `expect`.
    std::optional<Resolve> resolve;
};
struct Mismatch { size_t offset; uint8_t wanted, found; bool operator==(const Mismatch&) const = default; };
struct Match {
    uint64_t address = 0;
    std::vector<uint8_t> bytes;         // From the match: the pattern and up to Extension more.
    std::optional<size_t> divergesAt;   // Not the reference: first offset past the pattern where it differs.
    std::optional<uint64_t> resolved;   // With resolve: the target.
};
struct CheckResult {
    std::vector<Match> matches;          // Every match, ascending.
    Anchor anchor;
    size_t anchorHits = 0;
    std::optional<size_t> reference;     // The match others are compared with: the expected one, else the first.
    std::optional<size_t> uniqueAtLength; // Several matches, all diverging: the length that separates them.
    // With --expect (no resolve) and no match there: the fixed bytes that
    // differ at expect + offset; nullopt when those bytes are unreadable.
    std::optional<std::vector<Mismatch>> mismatches;
    bool compared = false;               // mismatches was attempted.
    std::vector<AddressField> warnings;  // Fixed fields at the reference (or expect) that hold an address.
    // Pattern bytes the console's decoder read the warnings from (with
    // --expect, from the instruction boundary there); nullopt: the byte
    // heuristic alone.
    std::optional<size_t> decoded;
    std::optional<bool> resolves;        // With an expect.
};

// Runs checks against one agent, sharing anchor searches between checks
// (sig --check --file): one search per module and anchor.
class Checker {
public:
    Checker(SearchFn search, ReadFn read, std::span<const Module> modules)
        : search_(std::move(search)), read_(std::move(read)), modules_(modules.begin(), modules.end()) {}
    size_t Searches() const { return searches_; }

    // Finds the anchor inside the module, then reads the bytes around each
    // hit (MaxBatchReads per request) and compares them under the mask. A
    // match must lie wholly inside the module.
    std::expected<CheckResult, std::string> Run(const Check& check, const Module& module) {
        const Pattern& pattern = check.pattern;
        auto anchor = ChooseAnchor(pattern);
        if (!anchor) return std::unexpected(anchor.error());
        CheckResult result;
        result.anchor = *anchor;
        auto hits = AnchorHits(module, std::span(pattern.bytes).subspan(anchor->offset, anchor->length));
        if (!hits) {
            return std::unexpected(hits.error().empty() ? "the anchor (" + std::to_string(anchor->length) +
                " fixed bytes at offset " + std::to_string(anchor->offset) + ") has more than " +
                std::to_string(MaxAnchorHits) + " hits: lengthen the pattern" : hits.error());
        }
        result.anchorHits = hits->size();
        const uint64_t end = module.base + module.size;
        std::vector<uint64_t> starts;
        for (const uint64_t hit : *hits) {
            if (hit < module.base + anchor->offset) continue;
            const uint64_t start = hit - anchor->offset;
            if (start + pattern.Size() <= end) starts.push_back(start);
        }
        std::ranges::sort(starts);
        starts.erase(std::ranges::unique(starts).begin(), starts.end());
        for (size_t first = 0; first < starts.size(); first += MaxBatchReads) {
            std::vector<ReadSpan> spans;
            for (size_t i = first; i < std::min(starts.size(), first + MaxBatchReads); ++i)
                spans.push_back({starts[i], std::min<uint64_t>(pattern.Size() + Extension, end - starts[i])});
            auto items = read_(spans);
            if (!items) return std::unexpected(items.error());
            for (size_t i = 0; i < spans.size() && i < items->size(); ++i)
                if ((*items)[i] && Matches(pattern, *(*items)[i]))
                    result.matches.push_back({spans[i].address, std::move(*(*items)[i]), std::nullopt, std::nullopt});
        }
        if (check.resolve) {
            const Resolve& r = *check.resolve;
            for (auto& match : result.matches)
                if (r.field + r.size <= match.bytes.size())
                    match.resolved = match.address + r.next + detail::Signed(match.bytes, r.field, r.size);
        }
        const std::optional<uint64_t> wanted = check.expect && !check.resolve ?
            std::optional(*check.expect + check.offset) : std::nullopt;
        if (!result.matches.empty()) {
            size_t reference = 0;
            for (size_t i = 0; wanted && i < result.matches.size(); ++i)
                if (result.matches[i].address == *wanted) reference = i;
            result.reference = reference;
            Diverge(result, pattern.Size());
            result.warnings = AddressFields(pattern, result.matches[reference].address, modules_);
        }
        const bool atWanted = wanted && result.reference && result.matches[*result.reference].address == *wanted;
        if (wanted && !atWanted) {
            result.compared = true;
            const ReadSpan span{*wanted, pattern.Size()};
            auto items = read_(std::span(&span, 1));
            if (!items) return std::unexpected(items.error());
            if (!items->empty() && items->front()) {
                std::vector<Mismatch> mismatches;
                const auto& bytes = *items->front();
                for (size_t i = 0; i < pattern.Size() && i < bytes.size(); ++i)
                    if (pattern.fixed[i] && bytes[i] != pattern.bytes[i]) mismatches.push_back({i, pattern.bytes[i], bytes[i]});
                result.mismatches = std::move(mismatches);
            }
            if (result.matches.empty()) result.warnings = AddressFields(pattern, *wanted, modules_);
        }
        if (check.expect) {
            const bool unique = result.matches.size() == 1;
            result.resolves = unique && (check.resolve ? result.matches.front().resolved == *check.expect :
                result.matches.front().address == *wanted);
        }
        return result;
    }

private:
    // Every anchor hit in the module, cached; an empty error means too many.
    std::expected<std::vector<uint64_t>, std::string> AnchorHits(const Module& module, std::span<const uint8_t> anchor) {
        auto key = std::pair(module.base, std::vector<uint8_t>(anchor.begin(), anchor.end()));
        if (const auto cached = anchors_.find(key); cached != anchors_.end()) return cached->second;
        ++searches_;
        std::vector<uint64_t> hits;
        std::expected<std::vector<uint64_t>, std::string> outcome;
        for (uint64_t from = module.base; from;) {
            auto page = search_(from, module.base + module.size, anchor,
                std::min(MaxSearchHits, MaxAnchorHits + 1 - hits.size()));
            if (!page) return std::unexpected(page.error()); // Not cached: a transport error may pass.
            hits.insert(hits.end(), page->hits.begin(), page->hits.end());
            if (hits.size() > MaxAnchorHits) break;
            if (page->resume && page->resume <= from) break; // No progress: never loop.
            from = page->resume;
        }
        if (hits.size() > MaxAnchorHits) outcome = std::unexpected(std::string());
        else outcome = std::move(hits);
        return anchors_.emplace(std::move(key), std::move(outcome)).first->second;
    }
    // Where each other match first differs from the reference past the
    // pattern, and the length that would separate them all.
    static void Diverge(CheckResult& result, size_t length) {
        if (result.matches.size() < 2) return;
        const auto& reference = result.matches[*result.reference].bytes;
        size_t needed = 0;
        bool all = true;
        for (size_t i = 0; i < result.matches.size(); ++i) {
            if (i == *result.reference) continue;
            auto& match = result.matches[i];
            for (size_t at = length; at < reference.size() && at < match.bytes.size(); ++at)
                if (reference[at] != match.bytes[at]) { match.divergesAt = at; break; }
            if (match.divergesAt) needed = std::max(needed, *match.divergesAt + 1);
            else all = false;
        }
        if (all) result.uniqueAtLength = needed;
    }

    SearchFn search_;
    ReadFn read_;
    std::vector<Module> modules_;
    std::map<std::pair<uint64_t, std::vector<uint8_t>>, std::expected<std::vector<uint64_t>, std::string>> anchors_;
    size_t searches_ = 0;
};

// One check against one module, without sharing.
inline std::expected<CheckResult, std::string> Run(const Check& check, const Module& module, const SearchFn& search,
                                                   const ReadFn& read, std::span<const Module> modules = {}) {
    return Checker(search, read, modules).Run(check, module);
}

// The first result that is unique and, when it has an expect, resolves.
inline std::optional<size_t> Winner(std::span<const std::optional<CheckResult>> results) {
    for (size_t i = 0; i < results.size(); ++i)
        if (results[i] && results[i]->matches.size() == 1 && results[i]->resolves.value_or(true)) return i;
    return std::nullopt;
}
}
