// reagentlive core agent: private vtable copies, for packs, core recorders
// and host helpers (not a wire API).
//
// A vtable entry is swapped without writing the table (read-only data shared
// by every instance): the object's vptr is pointed at a private copy of its
// table, and the entry is swapped in the copy. One copy per (object, vptr
// offset), shared by every user, so several entries of one object coexist.
//
// The copy mirrors the table: the slot before it (table[-1], where MSVC
// keeps the RTTI locator) and every readable qword after it within its
// module's image, up to MaxCopyMirror, so a call through any entry the
// game uses still reaches the table's own. Only the run of entries up to the
// first not in an executable section of a loaded module can be taken; a run
// that reaches MaxCopyMirror (a table that may be longer) is refused. It never
// moves and is never freed: a call that read the vptr may still be inside
// it. A copy whose vptr was put back is reused for another object only
// after CopyReuseGraceMs (at once for the same table: its entries are the
// same).
//
// Use, on one installer thread at a time (a lock serialises the rest):
//   Entry entry; if (Take(object, offset, index, entry) != Taken::Ok) refuse;
//   swap *entry.cell from entry.original by compare-and-swap (Swap, or a
//   recorder's own Start), forward to entry.original;
//   later: restore the cell by compare-and-swap (Unswap, or the recorder's
//   restore), then Give(entry).
// Taking an entry writes the vptr (by compare-and-swap) with the copy still
// identical to the table; the vptr goes back, by compare-and-swap if it still
// holds the copy, only once no entry is taken and every cell holds its
// original again.
#pragma once
#include <stdint.h>

namespace reagentlive::vtable {
constexpr uint32_t MaxCopies = 8, MaxCopyMirror = 1024;
constexpr uint64_t CopyReuseGraceMs = 1000;

// One taken entry. `cell` and `entries` point into the copy and stay valid
// for the life of the process.
struct Entry {
    void* volatile* cell;   // The entry in the copy: swap it, restore it.
    void* original;         // The table's entry: what a swap forwards to.
    void** table;           // The object's table before the copy.
    void* const* entries;   // The copy's first entry (entries[-1] is table[-1]).
    void* object;
    uint32_t offset;        // Of the vptr in the object.
    uint32_t index;         // Of the entry.
    uint32_t count;         // Entries that can be taken (the code run).
    uint32_t mirrored;      // Entries copied (count or more).
    uint32_t copy;          // Which copy (< MaxCopies).
};

enum class Taken {
    Ok,
    Unreadable,  // The object's vptr cannot be read or written.
    Foreign,     // The vptr points outside a loaded module's image (another
                 // owner's copy) or at one of ours for another object.
    NotCode,     // The entry is past the code run or not in the table's module's code.
    TooLong,     // The code run reaches MaxCopyMirror: the table may be longer than the copy.
    Busy,        // The entry is taken already.
    Changed,     // The vptr changed while installing, or no longer holds the
                 // copy taken earlier for this object (another owner).
    Full,        // MaxCopies copies in use or still in their grace.
    NoMemory,
};
// Takes entry `index` of the table whose pointer is at `object + offset`:
// finds or makes its copy and points the vptr at it.
Taken Take(void* object, uint32_t offset, uint32_t index, Entry& out);

enum class Given {
    Restored,  // The last entry: the vptr holds the table again.
    Shared,    // Other entries of the copy are still taken: the vptr stays.
    Left,      // The last entry, but a cell still differs from the table (the
               // copy stays, forwarding) or the vptr no longer holds the copy
               // (another owner; the copy is never reused).
};
// Gives back an entry from Take. The cell should hold its original again.
Given Give(const Entry& entry);

// Compare-and-swap of an entry's cell: original -> replacement, and back.
bool Swap(const Entry& entry, void* replacement);
bool Unswap(const Entry& entry, void* replacement);

// The copy's address range, for a search that skips the agent's own state.
bool InCopy(uintptr_t address);
}
