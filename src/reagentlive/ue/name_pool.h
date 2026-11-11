// reagentlive Unreal pack: walking the UE5 name pool's entry blocks. Pure; tests cover it.
#pragma once
#include <stdint.h>
#include <string.h>
#include <span>

// FNamePool: a lock (8 bytes), CurrentBlock, CurrentByteCursor, then block
// pointers. A comparison index is block << 16 | (byte offset / stride). Each
// entry holds a 16-bit header (bit 0 wide, Len in bits 6-15) and its
// characters, aligned to the stride. Engines differ in what precedes the
// header, so the agent picks the layout whose decoded entries agree with the
// engine's own name lookup.
namespace reagentlive::ue::names {
constexpr uint32_t BlockOffsetBits = 16;
constexpr uint32_t MaxBlocks = 8192;
struct PoolHeader { uint64_t lock; uint32_t currentBlock, currentByteCursor; };
constexpr size_t BlocksOffset = sizeof(PoolHeader);

struct Layout { uint32_t stride, headerOffset, textOffset; };
// Stock UE5 (header first, 2-byte stride) and a variant with an 8-byte hash
// before a 4-byte header slot and an 8-byte stride.
constexpr Layout Layouts[] = {{2, 0, 2}, {8, 8, 12}};

constexpr uint64_t BlockBytes(const Layout& layout) { return uint64_t{layout.stride} << BlockOffsetBits; }

struct Entry { uint32_t offset, length; bool wide; };

// The entry at `offset` in a block's bytes; false at the block's end (a zero
// header or not enough bytes).
inline bool EntryAt(std::span<const uint8_t> block, uint32_t offset, const Layout& layout, Entry& out) {
    if (uint64_t{offset} + layout.textOffset > block.size()) return false;
    uint16_t header;
    memcpy(&header, block.data() + offset + layout.headerOffset, sizeof(header));
    const uint32_t length = header >> 6;
    const bool wide = header & 1;
    if (!length || uint64_t{offset} + layout.textOffset + length * (wide ? 2u : 1u) > block.size()) return false;
    out = {offset, length, wide};
    return true;
}

// Byte offset of the entry after `entry`.
inline uint64_t NextOffset(const Entry& entry, const Layout& layout) {
    const uint64_t end = uint64_t{entry.offset} + layout.textOffset + entry.length * (entry.wide ? 2u : 1u);
    return (end + layout.stride - 1) / layout.stride * layout.stride;
}

inline int32_t Index(uint32_t block, uint32_t offset, const Layout& layout) {
    return static_cast<int32_t>(block << BlockOffsetBits | offset / layout.stride);
}
inline uint32_t Block(int32_t index) { return static_cast<uint32_t>(index) >> BlockOffsetBits; }
inline uint32_t Offset(int32_t index, const Layout& layout) {
    return (static_cast<uint32_t>(index) & ((1u << BlockOffsetBits) - 1)) * layout.stride;
}

// Narrow text of an entry into out (wide characters above 0x7f become '?');
// returns the characters written, at most capacity - 1, then a terminator.
inline size_t Text(std::span<const uint8_t> block, const Entry& entry, const Layout& layout, char* out, size_t capacity) {
    const uint8_t* text = block.data() + entry.offset + layout.textOffset;
    const size_t count = entry.length < capacity ? entry.length : capacity - 1;
    for (size_t i = 0; i < count; ++i) {
        if (!entry.wide) { out[i] = static_cast<char>(text[i]); continue; }
        uint16_t c;
        memcpy(&c, text + i * 2, sizeof(c));
        out[i] = c < 0x80 ? static_cast<char>(c) : '?';
    }
    out[count] = 0;
    return count;
}
}
