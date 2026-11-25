// reagentlive core agent: private vtable copies (core/vtable_copy.h).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <new>
#include "reagentlive/core/agent.h"
#include "reagentlive/core/vtable_copy.h"

namespace reagentlive::vtable {
namespace {
enum class CopyState : uint32_t { Free, Installed, Abandoned };

// Heap, allocated on first need and never freed: calls may still run through it.
struct Copy {
    CopyState state;
    uint32_t offset, count, mirrored, users; // count: the entries that can be taken; mirrored: all copied.
    void* object;
    void** table;
    uint64_t restoredMs;                     // When the vptr went back: reuse for another table waits the grace.
    uint8_t taken[MaxCopyMirror];
    void* originals[MaxCopyMirror];
    void* volatile slots[MaxCopyMirror + 1]; // slots[0] is table[-1]; the vptr points at slots + 1.
};

Copy* g_copies[MaxCopies];
SRWLOCK g_lock = SRWLOCK_INIT;

struct Locked {
    Locked() { AcquireSRWLockExclusive(&g_lock); }
    ~Locked() { ReleaseSRWLockExclusive(&g_lock); }
};

void* const* First(Copy& copy) { return const_cast<void* const*>(copy.slots + 1); }

HMODULE ModuleOf(const void* address) {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        static_cast<LPCWSTR>(address), &module) ? module : nullptr;
}

// The end of a loaded module's image, or 0.
uintptr_t ImageEnd(HMODULE module) {
    const auto* base = reinterpret_cast<const uint8_t*>(module);
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!GuardedCopy(&dos, base, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        !GuardedCopy(&nt, base + dos.e_lfanew, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE) return 0;
    return reinterpret_cast<uintptr_t>(base) + nt.OptionalHeader.SizeOfImage;
}

// The loaded module whose executable section holds `address`, or null.
HMODULE CodeModule(const void* address) {
    const HMODULE module = address ? ModuleOf(address) : nullptr;
    if (!module) return nullptr;
    const auto* base = reinterpret_cast<const uint8_t*>(module);
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!GuardedCopy(&dos, base, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        !GuardedCopy(&nt, base + dos.e_lfanew, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE) return nullptr;
    const auto rva = static_cast<uint64_t>(static_cast<const uint8_t*>(address) - base);
    const uint8_t* sections = base + dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
    for (uint32_t i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        if (!GuardedCopy(&section, sections + i * sizeof(section), sizeof(section))) return nullptr;
        const uint64_t size = section.Misc.VirtualSize ? section.Misc.VirtualSize : section.SizeOfRawData;
        if (rva >= section.VirtualAddress && rva < section.VirtualAddress + size)
            return (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) ? module : nullptr;
    }
    return nullptr;
}

// Compare-and-swap that survives a freed object; false on a fault.
bool GuardedExchange(void* volatile* cell, void* value, void* expected, void*& previous) {
    __try {
        previous = InterlockedCompareExchangePointer(cell, value, expected);
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ?
                EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

Taken Hand(Copy& copy, uint32_t number, uint32_t index, Entry& out) {
    if (index >= copy.count || CodeModule(copy.originals[index]) != ModuleOf(copy.table)) return Taken::NotCode;
    if (copy.taken[index]) return Taken::Busy;
    copy.taken[index] = 1;
    ++copy.users;
    out = {&copy.slots[index + 1], copy.originals[index], copy.table, First(copy), copy.object, copy.offset, index,
        copy.count, copy.mirrored, number};
    return Taken::Ok;
}

// Storage for a copy of `table`: a free copy of the same table, a new one,
// or the free copy restored longest ago past the grace. MaxCopies when none.
uint32_t Pick(void** table) {
    for (uint32_t i = 0; i < MaxCopies; ++i)
        if (g_copies[i] && g_copies[i]->state == CopyState::Free && g_copies[i]->table == table) return i;
    for (uint32_t i = 0; i < MaxCopies; ++i)
        if (!g_copies[i]) return i;
    const uint64_t now = GetTickCount64();
    uint32_t best = MaxCopies;
    for (uint32_t i = 0; i < MaxCopies; ++i) {
        const Copy& copy = *g_copies[i];
        if (copy.state != CopyState::Free || now - copy.restoredMs < CopyReuseGraceMs) continue;
        if (best == MaxCopies || copy.restoredMs < g_copies[best]->restoredMs) best = i;
    }
    return best;
}
}

Taken Take(void* object, uint32_t offset, uint32_t index, Entry& out) {
    const Locked lock;
    auto* vptr = reinterpret_cast<void* volatile*>(static_cast<uint8_t*>(object) + offset);
    void* current = nullptr;
    if (!object || !GuardedCopy(&current, const_cast<void**>(vptr), sizeof(current))) return Taken::Unreadable;
    for (uint32_t i = 0; i < MaxCopies; ++i) {
        Copy* copy = g_copies[i];
        if (!copy || copy->state != CopyState::Installed) continue;
        const bool ours = current == First(*copy), same = copy->object == object && copy->offset == offset;
        if (ours && !same) return Taken::Foreign;
        if (ours) return Hand(*copy, i, index, out);
        if (!same) continue;
        // This object's copy, but the vptr moved on: another owner (or the
        // object was freed and its memory reused). Never reused again.
        if (copy->users) return Taken::Changed;
        copy->state = CopyState::Abandoned;
    }
    auto** table = static_cast<void**>(current);
    const HMODULE module = table ? ModuleOf(table) : nullptr;
    if (!module || ModuleOf(table - 1) != module) return Taken::Foreign;
    // Copied: within the table's image, at most MaxCopyMirror entries.
    const uintptr_t end = ImageEnd(module);
    const auto room = end > reinterpret_cast<uintptr_t>(table) ? (end - reinterpret_cast<uintptr_t>(table)) / sizeof(void*) : 0;
    const auto limit = static_cast<uint32_t>(room < MaxCopyMirror ? room : MaxCopyMirror);
    // Takeable: the run of entries in a loaded module's code. Reaching the
    // cap, the table may go on past what the copy holds: refused.
    uint32_t count = 0;
    void* entry = nullptr;
    for (; count < limit; ++count)
        if (!GuardedCopy(&entry, table + count, sizeof(entry)) || !CodeModule(entry)) break;
    if (count == MaxCopyMirror) return Taken::TooLong;
    if (index >= count || !GuardedCopy(&entry, table + index, sizeof(entry)) || CodeModule(entry) != module)
        return Taken::NotCode;
    const uint32_t number = Pick(table);
    if (number == MaxCopies) return Taken::Full;
    if (!g_copies[number] && !(g_copies[number] = new (std::nothrow) Copy{})) return Taken::NoMemory;
    Copy& copy = *g_copies[number];
    // Free: nothing points at it now, and a call that read it before is
    // past the grace or sees the same entries. Every slot up to the old
    // mirror length is rewritten: no entry of an earlier table survives.
    void* value = nullptr;
    if (!GuardedCopy(&value, table - 1, sizeof(value))) return Taken::Unreadable;
    copy.object = nullptr;
    copy.slots[0] = value;
    // Mirrored: every readable qword after the table (the game may call an
    // entry past the run, such as one another owner swapped).
    uint32_t mirrored = 0;
    for (; mirrored < limit; ++mirrored) {
        if (!GuardedCopy(&value, table + mirrored, sizeof(value))) break;
        copy.slots[mirrored + 1] = value;
    }
    const uint32_t stale = copy.mirrored;
    copy.mirrored = mirrored > stale ? mirrored : stale; // What a later fill rewrites.
    if (mirrored < count) return Taken::Unreadable;      // Changed under us.
    for (uint32_t i = mirrored; i < stale; ++i) copy.slots[i + 1] = nullptr;
    for (uint32_t i = 0; i < count; ++i) {
        copy.originals[i] = copy.slots[i + 1];
        copy.taken[i] = 0;
    }
    copy.mirrored = mirrored;
    copy.table = table;
    copy.count = count;
    copy.users = 0;
    copy.object = object;
    copy.offset = offset;
    void* previous = nullptr;
    const bool exchanged = GuardedExchange(vptr, const_cast<void**>(First(copy)), table, previous);
    if (!exchanged || previous != table) {
        copy.object = nullptr;
        return exchanged ? Taken::Changed : Taken::Unreadable;
    }
    copy.state = CopyState::Installed;
    return Hand(copy, number, index, out);
}

Given Give(const Entry& entry) {
    const Locked lock;
    Copy* copy = entry.copy < MaxCopies ? g_copies[entry.copy] : nullptr;
    if (!copy || copy->state != CopyState::Installed || copy->object != entry.object || copy->offset != entry.offset ||
        entry.index >= copy->count || !copy->taken[entry.index]) return Given::Left;
    copy->taken[entry.index] = 0;
    if (--copy->users) return Given::Shared;
    for (uint32_t i = 0; i < copy->count; ++i)
        if (copy->slots[i + 1] != copy->originals[i]) return Given::Left; // Still swapped: the copy keeps forwarding.
    auto* vptr = reinterpret_cast<void* volatile*>(static_cast<uint8_t*>(copy->object) + copy->offset);
    void* previous = nullptr;
    void* first = const_cast<void**>(First(*copy));
    if (!GuardedExchange(vptr, copy->table, first, previous) || previous != first) {
        copy->state = CopyState::Abandoned;
        return Given::Left;
    }
    copy->state = CopyState::Free;
    copy->restoredMs = GetTickCount64();
    return Given::Restored;
}

bool Swap(const Entry& entry, void* replacement) {
    return InterlockedCompareExchangePointer(entry.cell, replacement, entry.original) == entry.original;
}

bool Unswap(const Entry& entry, void* replacement) {
    return InterlockedCompareExchangePointer(entry.cell, entry.original, replacement) == replacement;
}

bool InCopy(uintptr_t address) {
    for (Copy* copy : g_copies) {
        const auto start = reinterpret_cast<uintptr_t>(copy);
        if (copy && address >= start && address < start + sizeof(Copy)) return true;
    }
    return false;
}
}
