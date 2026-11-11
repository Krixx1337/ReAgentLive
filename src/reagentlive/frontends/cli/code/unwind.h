// reagentlive CLI: function bounds from a 64-bit image's exception directory.
// Not analysis: the linker lists the start and end of every function that has
// unwind data, sorted by address, and this looks one address up in it with a
// few reads. A function without an entry (a leaf) is a result, never a guess
// from padding or prologue bytes. Pure: reads come in as a callback.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <expected>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "reagentlive/core/client.h"
#include "reagentlive/frontends/cli/code/signature.h"

namespace reagentlive::cli::unwind {
using ReadFn = sig::ReadFn;

// Where the module's exception directory lies (image.h reads it from the
// headers); size 0: it has none.
struct Directory {
    uint64_t rva = 0, size = 0;
    bool operator==(const Directory&) const = default;
};
// One RUNTIME_FUNCTION: RVAs of the first byte, one past the last, and the unwind info.
struct Entry {
    uint32_t begin = 0, end = 0, unwind = 0;
    bool operator==(const Entry&) const = default;
};
constexpr size_t EntrySize = 12;
// The function containing an address. `chunk` is set when the address lies in
// a continuation (chained unwind data): `start`/`end` are then the function's
// first entry, `chunk` the continuation's range.
struct Range {
    uint64_t start = 0, end = 0;
    bool operator==(const Range&) const = default;
};
struct Function {
    Range range;
    std::optional<Range> chunk;
    bool operator==(const Function&) const = default;
};

constexpr size_t Probes = 7;         // Entries sampled per request while the range is wide.
constexpr size_t Window = 340;       // Entries read whole once the range is this narrow (4080 bytes).
constexpr size_t MaxChain = 32;      // Chained entries followed before giving up.
constexpr uint8_t ChainInfo = 0x4;   // UNW_FLAG_CHAININFO.

namespace detail {
inline uint32_t U32(std::span<const uint8_t> bytes, size_t at) {
    uint32_t value = 0;
    memcpy(&value, bytes.data() + at, 4);
    return value;
}
inline Entry Parse(std::span<const uint8_t> bytes, size_t at) {
    return {U32(bytes, at), U32(bytes, at + 4), U32(bytes, at + 8)};
}
inline std::expected<std::vector<uint8_t>, std::string> ReadOne(const ReadFn& read, uint64_t address, uint64_t length) {
    const ReadSpan span{address, length};
    auto items = read(std::span(&span, 1));
    if (!items) return std::unexpected(items.error());
    if (items->empty() || !items->front() || items->front()->size() < length)
        return std::unexpected("unwind data unreadable");
    return std::move(*items->front());
}
}

// Lookups in one module's directory. Sampled entries are kept, so the first
// requests of every search (the same probes for every address) cost once.
class Table {
public:
    Table(ReadFn read, Module module, Directory directory)
        : read_(std::move(read)), module_(std::move(module)), directory_(directory) {}
    size_t Count() const { return static_cast<size_t>(directory_.size / EntrySize); }

    // nullopt: no entry contains the address (a leaf function, data, or
    // outside the module); an error only when the table is unreadable.
    std::expected<std::optional<Function>, std::string> Lookup(uint64_t address) {
        if (address < module_.base || address - module_.base >= module_.size || !Count()) return std::nullopt;
        const auto rva = static_cast<uint32_t>(address - module_.base);
        auto index = Containing(rva);
        if (!index) return std::unexpected(index.error());
        if (!*index) return std::nullopt;
        Entry entry = entries_.at(**index);
        Function function;
        const Entry found = entry;
        for (size_t hop = 0;; ++hop) {
            auto parent = Parent(entry);
            if (!parent) return std::unexpected(parent.error());
            if (!*parent) break;
            if (hop == MaxChain) return std::unexpected("chained unwind data does not end");
            entry = **parent;
        }
        function.range = {module_.base + entry.begin, module_.base + entry.end};
        if (!(entry == found)) function.chunk = Range{module_.base + found.begin, module_.base + found.end};
        return function;
    }

    // Every entry (functions and their chained continuations) starting in
    // [from, to), in address order: the code a scan around an address decodes.
    std::expected<std::vector<Range>, std::string> Between(uint64_t from, uint64_t to) {
        std::vector<Range> ranges;
        if (!Count() || to <= module_.base || from >= module_.base + module_.size) return ranges;
        from = std::max(from, module_.base);
        to = std::min(to, module_.base + module_.size);
        const auto fromRva = static_cast<uint32_t>(from - module_.base), toRva = static_cast<uint32_t>(to - module_.base);
        auto floor = Containing(fromRva, true);
        if (!floor) return std::unexpected(floor.error());
        for (size_t i = floor->value_or(0); i < Count(); i += Window) {
            const size_t count = std::min(Window, Count() - i);
            auto bytes = detail::ReadOne(read_, Address(i), count * EntrySize);
            if (!bytes) return std::unexpected(bytes.error());
            for (size_t j = 0; j < count; ++j) {
                const Entry entry = detail::Parse(*bytes, j * EntrySize);
                if (entry.begin >= toRva) return ranges;
                if (entry.begin >= fromRva && entry.end > entry.begin)
                    ranges.push_back({module_.base + entry.begin, module_.base + entry.end});
            }
        }
        return ranges;
    }

private:
    // The index of the entry whose range holds `rva`, nullopt for none. With
    // `floor`: the last entry starting at or below `rva`, whether or not it holds it.
    std::expected<std::optional<size_t>, std::string> Containing(uint32_t rva, bool floor = false) {
        size_t lo = 0, hi = Count(); // The last entry starting at or below rva lies in [lo, hi), if any.
        while (hi - lo > Window) {
            std::vector<size_t> probes;
            for (size_t j = 1; j <= Probes; ++j) probes.push_back(lo + (hi - lo) * j / (Probes + 1));
            if (auto loaded = Load(probes); !loaded) return std::unexpected(loaded.error());
            size_t nextLo = lo, nextHi = hi;
            for (const size_t probe : probes) {
                if (entries_.at(probe).begin <= rva) nextLo = probe;
                else { nextHi = probe; break; }
            }
            lo = nextLo;
            hi = nextHi;
        }
        auto bytes = detail::ReadOne(read_, Address(lo), (hi - lo) * EntrySize);
        if (!bytes) return std::unexpected(bytes.error());
        std::optional<size_t> found;
        for (size_t i = lo; i < hi; ++i) {
            const Entry entry = detail::Parse(*bytes, (i - lo) * EntrySize);
            entries_[i] = entry;
            if (entry.begin > rva) break;
            found = i;
        }
        if (!found || floor) return found;
        const Entry& entry = entries_.at(*found);
        if (rva >= entry.end) return std::nullopt;
        return found;
    }
    // Samples, MaxBatchReads per request; cached ones cost nothing.
    std::expected<void, std::string> Load(std::span<const size_t> indices) {
        std::vector<ReadSpan> spans;
        std::vector<size_t> wanted;
        for (const size_t index : indices)
            if (!entries_.contains(index)) {
                spans.push_back({Address(index), EntrySize});
                wanted.push_back(index);
            }
        for (size_t first = 0; first < spans.size(); first += MaxBatchReads) {
            const size_t count = std::min(MaxBatchReads, spans.size() - first);
            auto items = read_(std::span(spans).subspan(first, count));
            if (!items) return std::unexpected(items.error());
            for (size_t i = 0; i < count; ++i) {
                if (i >= items->size() || !(*items)[i] || (*items)[i]->size() < EntrySize)
                    return std::unexpected("unwind data unreadable");
                entries_[wanted[first + i]] = detail::Parse(*(*items)[i], 0);
            }
        }
        return {};
    }
    // The entry a chained one continues, nullopt for a first entry. Bit 0 of
    // the unwind RVA set: it names the parent entry directly. Otherwise the
    // unwind info's CHAININFO flag puts the parent after the unwind codes.
    std::expected<std::optional<Entry>, std::string> Parent(const Entry& entry) {
        uint64_t at = 0;
        if (entry.unwind & 1) {
            at = entry.unwind & ~1u;
        } else {
            if (entry.unwind >= module_.size) return std::nullopt;
            auto header = detail::ReadOne(read_, module_.base + entry.unwind, 4);
            if (!header) return std::unexpected(header.error());
            if (!(((*header)[0] >> 3) & ChainInfo)) return std::nullopt;
            const uint32_t codes = (*header)[2];
            at = entry.unwind + 4 + 2 * ((codes + 1) & ~1u);
        }
        if (at >= module_.size || module_.size - at < EntrySize) return std::unexpected("chained unwind data malformed");
        auto bytes = detail::ReadOne(read_, module_.base + at, EntrySize);
        if (!bytes) return std::unexpected(bytes.error());
        const Entry parent = detail::Parse(*bytes, 0);
        if (parent == entry || parent.begin >= parent.end) return std::unexpected("chained unwind data malformed");
        return parent;
    }
    uint64_t Address(size_t index) const { return module_.base + directory_.rva + index * EntrySize; }

    ReadFn read_;
    Module module_;
    Directory directory_;
    std::map<size_t, Entry> entries_;
};
}
