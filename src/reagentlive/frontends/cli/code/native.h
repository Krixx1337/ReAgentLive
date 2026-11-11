// reagentlive CLI: native objects without an engine pack: a vtable's slots from
// live bytes, the bodies behind jump stubs, and C strings. Pure: reads and "is this
// code" come in as callbacks, so tests feed fixed bytes.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "reagentlive/core/client.h"
#include "reagentlive/frontends/cli/code/disasm.h"

namespace reagentlive::cli::native {
using ReadFn = sig::ReadFn;
constexpr size_t MaxSlots = MaxRead / 8;   // One read.
constexpr size_t StubBytes = 16;           // Read per hop to decode one instruction.

struct Slot {
    size_t slot = 0;
    uint64_t target = 0;
    std::optional<uint64_t> body;   // --resolve: where a jump stub leads.
    bool operator==(const Slot&) const = default;
};
struct Slots {
    std::vector<Slot> slots;
    std::string stop;               // "count", "not code", "end of read".
    std::optional<uint64_t> stopValue; // "not code": the entry that stopped it.
};

// Entries from `bytes` (a table read), stopping at the first that is not
// code or after `count` (0: until one stops it).
inline Slots ReadSlots(std::span<const uint8_t> bytes, size_t count, const std::function<bool(uint64_t)>& code) {
    Slots out;
    const size_t limit = std::min(count ? count : MaxSlots, bytes.size() / 8);
    for (size_t i = 0; i < limit; ++i) {
        uint64_t value;
        memcpy(&value, bytes.data() + i * 8, 8);
        if (!code(value)) {
            out.stop = "not code";
            out.stopValue = value;
            return out;
        }
        out.slots.push_back({i, value, std::nullopt});
    }
    out.stop = count && limit == count ? "count" : "end of read";
    return out;
}

// The address each slot's target leads to through unconditional direct
// jumps (at most dis::MaxFollow), as `disasm --follow` would end; set only
// where a jump was taken. One read per hop, MaxBatchReads per request.
inline std::expected<void, std::string> ResolveBodies(const ReadFn& read, std::vector<Slot>& slots) {
    std::vector<uint64_t> at(slots.size());
    std::vector<size_t> pending;
    for (size_t i = 0; i < slots.size(); ++i) {
        at[i] = slots[i].target;
        pending.push_back(i);
    }
    for (size_t hop = 0; hop < dis::MaxFollow && !pending.empty(); ++hop) {
        std::vector<size_t> next;
        for (size_t first = 0; first < pending.size(); first += MaxBatchReads) {
            std::vector<ReadSpan> spans;
            const size_t n = std::min(MaxBatchReads, pending.size() - first);
            for (size_t k = 0; k < n; ++k) spans.push_back({at[pending[first + k]], StubBytes});
            auto items = read(spans);
            if (!items) return std::unexpected(items.error());
            for (size_t k = 0; k < n && k < items->size(); ++k) {
                const size_t index = pending[first + k];
                if (!(*items)[k]) continue;
                auto instruction = dis::Decode(*(*items)[k], at[index]);
                if (!instruction || !instruction->directJump) continue;
                at[index] = *instruction->target;
                slots[index].body = at[index];
                next.push_back(index);
            }
        }
        pending = std::move(next);
    }
    return {};
}

// Text read with --as cstr|wstr: up to the terminator, or every byte read
// when there is none (`terminated` false). Unprintable characters are
// escaped (\n, \t, \r, \\, \xNN; wide: \uNNNN), never dropped.
struct Decoded { std::string text; size_t length = 0; bool terminated = false; };
namespace detail {
inline void Escape(std::string& out, uint32_t c, bool wide) {
    char buffer[8];
    if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\t') out += "\\t";
    else if (c == '\r') out += "\\r";
    else if (c >= 0x20 && c < 0x7f) out += static_cast<char>(c);
    else if (!wide) { snprintf(buffer, sizeof(buffer), "\\x%02x", c); out += buffer; }
    else if (c < 0xa0 || (c >= 0xd800 && c < 0xe000)) { snprintf(buffer, sizeof(buffer), "\\u%04x", c); out += buffer; }
    else if (c < 0x800) { out += static_cast<char>(0xc0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3f)); }
    else {
        out += static_cast<char>(0xe0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (c & 0x3f));
    }
}
}
inline Decoded CString(std::span<const uint8_t> bytes) {
    Decoded out;
    for (const uint8_t c : bytes) {
        if (!c) { out.terminated = true; break; }
        detail::Escape(out.text, c, false);
        ++out.length;
    }
    return out;
}
inline Decoded WideString(std::span<const uint8_t> bytes) {
    Decoded out;
    for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
        const uint32_t c = bytes[i] | (bytes[i + 1] << 8);
        if (!c) { out.terminated = true; break; }
        detail::Escape(out.text, c, true);
        ++out.length;
    }
    return out;
}

// What a region is, in one word: code, rodata or data inside an image (by
// its section, else its protection), mapped, private, reserved or free.
// Private read-write memory is `private`: a stack is not told from a heap.
namespace region {
constexpr uint32_t Commit = 0x1000, Reserve = 0x2000, Free = 0x10000;
constexpr uint32_t Private = 0x20000, Mapped = 0x40000, Image = 0x1000000;
constexpr uint32_t Guard = 0x100, NoCache = 0x200, WriteCombine = 0x400;
inline bool Executable(uint32_t protect) { return protect & 0xF0; }
inline bool Writable(uint32_t protect) { return protect & (0x04 | 0x08 | 0x40 | 0x80); }
// "rwx"-style text plus modifiers; "none" for PAGE_NOACCESS or 0.
inline std::string Protection(uint32_t protect) {
    std::string text;
    switch (protect & 0xFF) {
    case 0x02: text = "r"; break;
    case 0x04: text = "rw"; break;
    case 0x08: text = "rc"; break;
    case 0x10: text = "x"; break;
    case 0x20: text = "rx"; break;
    case 0x40: text = "rwx"; break;
    case 0x80: text = "rcx"; break;
    default: text = "none"; break;
    }
    if (protect & Guard) text += "+guard";
    if (protect & NoCache) text += "+nocache";
    if (protect & WriteCombine) text += "+writecombine";
    return text;
}
inline std::string_view State(uint32_t state) {
    return state == Commit ? "commit" : state == Reserve ? "reserve" : state == Free ? "free" : "unknown";
}
inline std::string_view Type(uint32_t type) {
    return type == Private ? "private" : type == Mapped ? "mapped" : type == Image ? "image" : "none";
}
// `exec` and `writable`: the section's flags when the address lies in one.
inline std::string_view Kind(const RegionInfo& info, std::optional<std::pair<bool, bool>> section) {
    if (info.state == Free) return "free";
    if (info.state == Reserve) return "reserved";
    if (info.type == Image) {
        const bool exec = section ? section->first : Executable(info.protect);
        const bool write = section ? section->second : Writable(info.protect);
        return exec ? "code" : write ? "data" : "rodata";
    }
    return info.type == Mapped ? "mapped" : "private";
}
}
}
