// reagentlive Unreal pack: finding and checking the object table and the name
// pool at run time, so a stale SDK address after a game patch turns the pack
// off instead of reading garbage. Pure: every read goes through Memory, a
// guarded copy the caller provides; tests cover it with fake tables.
#pragma once
#include <stdint.h>
#include <string.h>
#include <iterator>
#include <span>
#include <string_view>
#include "reagentlive/ue/name_pool.h"

namespace reagentlive::ue::globals {
// A copy that fails instead of faulting (preflight plus a fault guard).
struct Memory {
    bool (*copy)(const void* user, uint64_t address, void* out, size_t bytes);
    const void* user;
    bool Get(uint64_t address, void* out, size_t bytes) const { return address && copy(user, address, out, bytes); }
    template<class T> bool Get(uint64_t address, T& out) const { return Get(address, &out, sizeof(out)); }
};

// User-mode, 8-byte aligned and not null: anything else is not a heap or image pointer.
constexpr bool Pointer(uint64_t value) { return value && !(value & 7) && value < 0x0000800000000000ull; }

// Where the object table keeps its fields, from the host's SDK.
struct TableShape {
    uint32_t headerSize;                    // Bytes of the table header (at most MaxHeader).
    uint32_t objectsOffset, maxOffset, numOffset;
    int32_t numChunksOffset, maxChunksOffset; // -1 for a flat table.
    uint32_t perChunk;                      // Items per chunk; 0 for a flat table.
    uint32_t itemSize, itemObjectOffset;
    uint32_t objectIndexOffset, objectClassOffset, objectNameOffset;
};
constexpr uint32_t MaxHeader = 64;
constexpr int32_t MaxObjects = 1 << 26, MaxChunks = 4096;
// Leading slots each check reads: the engine's first objects, created at start.
constexpr int32_t SampleSlots = 64;

struct TableHeader { uint64_t objects; int32_t max, num, numChunks, maxChunks; };

inline TableHeader ParseHeader(const uint8_t* bytes, const TableShape& shape) {
    TableHeader header{};
    memcpy(&header.objects, bytes + shape.objectsOffset, sizeof(header.objects));
    memcpy(&header.max, bytes + shape.maxOffset, sizeof(header.max));
    memcpy(&header.num, bytes + shape.numOffset, sizeof(header.num));
    if (shape.perChunk) {
        memcpy(&header.numChunks, bytes + shape.numChunksOffset, sizeof(header.numChunks));
        memcpy(&header.maxChunks, bytes + shape.maxChunksOffset, sizeof(header.maxChunks));
    }
    return header;
}

// Why a header cannot be an object table, or nullptr. Cheap: the scan runs it
// at every aligned address.
inline const char* HeaderProblem(const TableHeader& header, const TableShape& shape) {
    if (!Pointer(header.objects)) return "table: no item storage";
    if (header.num < 2 || header.num > header.max || header.max > MaxObjects) return "table: bad counts";
    if (!shape.perChunk) return nullptr;
    if (header.numChunks < 1 || header.numChunks > header.maxChunks || header.maxChunks > MaxChunks ||
        header.num > static_cast<int64_t>(header.numChunks) * shape.perChunk) return "table: bad chunks";
    return nullptr;
}

// The object in a slot, or 0 when the slot is empty or unreadable.
inline uint64_t ObjectAt(const Memory& memory, const TableHeader& header, const TableShape& shape, int32_t index) {
    uint64_t items = header.objects;
    uint32_t slot = static_cast<uint32_t>(index);
    if (shape.perChunk) {
        if (!memory.Get(header.objects + uint64_t{slot / shape.perChunk} * sizeof(uint64_t), items) || !Pointer(items)) return 0;
        slot %= shape.perChunk;
    }
    uint64_t object = 0;
    if (!memory.Get(items + uint64_t{slot} * shape.itemSize + shape.itemObjectOffset, object) || !Pointer(object)) return 0;
    return object;
}

// Follows an object's class pointers to the class of classes (the one object
// that is its own class); 0 when the chain breaks or is longer than a few steps.
inline uint64_t ClassOfClasses(const Memory& memory, const TableShape& shape, uint64_t object) {
    for (int step = 0; step < 4; ++step) {
        uint64_t type = 0;
        if (!memory.Get(object + shape.objectClassOffset, type) || !Pointer(type)) return 0;
        if (type == object) return type;
        object = type;
    }
    return 0;
}

// Why `address` is not the object table, or nullptr. On success, the class of
// classes. Reads the header, every chunk pointer and the leading slots: each
// object there must carry its own slot as its index, and every class chain
// must end in the same class of classes.
inline const char* CheckTable(const Memory& memory, uint64_t address, const TableShape& shape, uint64_t& classOfClasses) {
    uint8_t bytes[MaxHeader];
    if (shape.headerSize > sizeof(bytes) || !memory.Get(address, bytes, shape.headerSize)) return "table: header unreadable";
    const TableHeader header = ParseHeader(bytes, shape);
    if (const char* problem = HeaderProblem(header, shape)) return problem;
    if (shape.perChunk) {
        uint64_t last = 0;
        if (!memory.Get(header.objects + uint64_t(header.numChunks - 1) * sizeof(uint64_t), last) || !Pointer(last))
            return "table: chunks unreadable";
    }
    const int32_t samples = header.num < SampleSlots ? header.num : SampleSlots;
    int32_t empty = 0;
    classOfClasses = 0;
    for (int32_t slot = 0; slot < samples; ++slot) {
        const uint64_t object = ObjectAt(memory, header, shape, slot);
        if (!object) { ++empty; continue; }
        int32_t index = -1;
        if (!memory.Get(object + shape.objectIndexOffset, index) || index != slot) return "table: index is not slot";
        const uint64_t root = ClassOfClasses(memory, shape, object);
        if (!root || (classOfClasses && root != classOfClasses)) return "table: class chains differ";
        classOfClasses = root;
    }
    if (!classOfClasses || empty * 4 > samples) return "table: first slots empty";
    return nullptr;
}

// One name by comparison index, narrowed like names::Text, without the
// instance number. False when the index is outside the pool or not a name.
inline bool DecodeName(const Memory& memory, uint64_t pool, const names::Layout& layout, int32_t index,
                       char* out, size_t capacity, size_t& length) {
    names::PoolHeader header{};
    const uint32_t block = names::Block(index);
    uint64_t data = 0;
    if (index < 0 || capacity == 0 || !memory.Get(pool, header) || block > header.currentBlock || block >= names::MaxBlocks ||
        !memory.Get(pool + names::BlocksOffset + uint64_t{block} * sizeof(uint64_t), data) || !Pointer(data)) return false;
    const uint64_t entry = data + names::Offset(index, layout);
    uint8_t bytes[16 + 2 * 1024];
    uint32_t count;
    bool wide;
    if (layout.textOffset > 16 || !memory.Get(entry, bytes, layout.textOffset) ||
        !names::HeaderAt(bytes, layout, count, wide) || count > 1023 ||
        !memory.Get(entry + layout.textOffset, bytes + layout.textOffset, static_cast<size_t>(names::TextBytes(count, wide))))
        return false;
    const std::span<const uint8_t> view(bytes, layout.textOffset + static_cast<size_t>(names::TextBytes(count, wide)));
    length = names::Text(view, {0, count, wide}, layout, out, capacity);
    return true;
}

inline bool NameIs(const Memory& memory, uint64_t pool, const names::Layout& layout, int32_t index, std::string_view expected) {
    char text[64];
    size_t length = 0;
    return DecodeName(memory, pool, layout, index, text, sizeof(text), length) && std::string_view(text, length) == expected;
}

// Cheap pool header test from bytes already in hand (at least BlocksOffset + 8).
inline bool PoolHeaderPlausible(const uint8_t* bytes) {
    names::PoolHeader header{};
    uint64_t first = 0;
    memcpy(&header, bytes, sizeof(header));
    memcpy(&first, bytes + names::BlocksOffset, sizeof(first));
    return header.currentBlock < names::MaxBlocks && header.currentByteCursor <= names::BlockBytes(names::Layouts[1]) &&
        Pointer(first);
}

// Why `pool` is not the name pool, or nullptr and the layout's index in
// names::Layouts. The layout must decode name 0 as "None" and the class of
// classes' name as "Class".
inline const char* CheckPool(const Memory& memory, uint64_t pool, int32_t classNameIndex, int& layout) {
    uint8_t bytes[names::BlocksOffset + sizeof(uint64_t)];
    if (!memory.Get(pool, bytes, sizeof(bytes))) return "pool: header unreadable";
    if (!PoolHeaderPlausible(bytes)) return "pool: bad header";
    for (int candidate = 0; candidate < static_cast<int>(std::size(names::Layouts)); ++candidate) {
        if (NameIs(memory, pool, names::Layouts[candidate], 0, "None") &&
            NameIs(memory, pool, names::Layouts[candidate], classNameIndex, "Class")) {
            layout = candidate;
            return nullptr;
        }
    }
    return "pool: no layout fits";
}

// Writable sections of a loaded image, from its PE headers.
struct Section { uint64_t begin, end; };
inline size_t WritableSections(const Memory& memory, uint64_t image, Section* out, size_t capacity) {
    uint32_t nt = 0, signature = 0;
    uint16_t sections = 0, optional = 0;
    if (!memory.Get(image + 0x3c, nt) || nt > 0x1000 || !memory.Get(image + nt, signature) || signature != 0x4550 ||
        !memory.Get(image + nt + 6, sections) || !memory.Get(image + nt + 20, optional)) return 0;
    size_t count = 0;
    for (uint16_t i = 0; i < sections && count < capacity; ++i) {
        const uint64_t at = image + nt + 24 + optional + uint64_t{i} * 40;
        uint32_t size = 0, rva = 0, flags = 0;
        if (!memory.Get(at + 8, size) || !memory.Get(at + 12, rva) || !memory.Get(at + 36, flags)) return count;
        if ((flags & 0x80000000u) && size) out[count++] = {image + rva, image + rva + size};
    }
    return count;
}

// Calls visit(address, bytes, available) at every 8-byte aligned address of
// a section, with the bytes there copied into `buffer` (at least `overlap`
// bytes past the address unless the section ends sooner) until visit returns
// true; returns that address or 0. Unreadable windows are skipped.
template<class Visit>
uint64_t ScanSection(const Memory& memory, Section section, std::span<uint8_t> buffer, size_t overlap, Visit&& visit) {
    if (buffer.size() <= overlap) return 0;
    const size_t step = (buffer.size() - overlap) & ~size_t{7};
    for (uint64_t window = (section.begin + 7) & ~uint64_t{7}; window < section.end; window += step) {
        const size_t bytes = static_cast<size_t>(section.end - window < buffer.size() ? section.end - window : buffer.size());
        if (!memory.Get(window, buffer.data(), bytes)) continue;
        const size_t last = bytes < step ? bytes : step;
        for (size_t offset = 0; offset + 8 <= last; offset += 8)
            if (visit(window + offset, buffer.data() + offset, bytes - offset)) return window + offset;
    }
    return 0;
}
}
