// reagentlive core agent: pointer-swap recorder slots (wire side: the
// recorder section of protocol.h). For packs and core recorders, not hosts.
//
// A kind of recording brings its start op, its MaxRecorderSlots thunks (one
// per slot: thunk I uses slot I) and its row payload. The core owns the
// rest: the slot table, each slot's ring of MaxRecorderRows rows of a fixed
// stride, the in-flight count, the deadline, the compare-and-swap restore,
// the Recording/Expired/Finished/Replaced states, the sweep, slot picking,
// and reading and stopping any kind.
//
// Thunk side (any thread; no allocation, lock or log):
//   int64_t now; const bool armed = Enter(slot, object, now);
//   ... forward to slot.original ...
//   if (armed) { Pending row = Claim(slot, object, now); fill Payload(row);
//                Publish(row, size, tag); }
//   Leave(slot);
// Installer side (the Tick thread): Pick, fill the slot's kind fields
// (setup), Start; Sweep, StopAll, and the Read and Stop handler bodies.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include "reagentlive/core/agent.h"
#include "reagentlive/core/protocol.h"

namespace reagentlive::recorder {
// One recording. Written on the Tick thread while not recording; thunks only
// read the setup and append rows.
struct Slot {
    volatile LONG state;        // RecorderState.
    volatile LONG inFlight;     // Calls inside the thunk now: a slot is reused only at zero.
    uint32_t kind;              // RecorderStatus::kind.
    uint32_t payload;           // RecorderStatus::payload (kind-defined).
    void* volatile* cell;       // The pointer swapped.
    void* original;             // What the cell held: the thunk forwards to it.
    void* thunk;                // What the cell holds while recording.
    uint64_t target;            // RecorderStatus::target (kind-defined).
    uint64_t filter;            // 0: every call; else only calls whose object is this.
    uint64_t setup[4];          // Kind-defined, written before Start and read by the kind's thunk.
    int64_t start, deadline, frequency;
    volatile LONG64 written;
    volatile LONG counts[2];    // Kind-defined tallies (RecorderStatus::counts).
    int64_t restoredMs;         // When the restore succeeded: reuse waits RecorderReuseGraceMs.
    uint8_t* rows;              // MaxRecorderRows * RowStride, allocated on first use and kept.
};
constexpr size_t RowStride = sizeof(RecorderRow) + MaxRecorderPayload;
extern Slot g_slots[MaxRecorderSlots];

inline int64_t Now() {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

// Thunk entry: counts the call in flight (Leave ends it) and says whether to
// record it: recording, the filter met and in time (`now` is then the call's
// time). Past the deadline it marks the slot Expired; the Tick thread restores.
inline bool Enter(Slot& slot, uint64_t object, int64_t& now) {
    InterlockedIncrement(&slot.inFlight);
    if (slot.state != static_cast<LONG>(RecorderState::Recording) || (slot.filter && object != slot.filter)) return false;
    now = Now();
    if (now <= slot.deadline) return true;
    InterlockedCompareExchange(&slot.state, static_cast<LONG>(RecorderState::Expired),
        static_cast<LONG>(RecorderState::Recording));
    return false;
}
inline void Leave(Slot& slot) { InterlockedDecrement(&slot.inFlight); }

// A row being written: invisible to readers until Publish.
struct Pending { RecorderRow* row; uint64_t sequence; };
inline Pending Claim(Slot& slot, uint64_t object, int64_t now) {
    const LONG64 sequence = InterlockedIncrement64(&slot.written);
    auto* row = reinterpret_cast<RecorderRow*>(slot.rows + static_cast<size_t>((sequence - 1) % MaxRecorderRows) * RowStride);
    row->sequence = 0;
    MemoryBarrier();
    row->object = object;
    row->microseconds = slot.frequency ? (now - slot.start) * 1000000 / slot.frequency : 0;
    row->thread = GetCurrentThreadId();
    row->tag = 0;
    row->size = 0;
    return {row, static_cast<uint64_t>(sequence)};
}
// Up to MaxRecorderPayload bytes.
inline uint8_t* Payload(const Pending& pending) { return reinterpret_cast<uint8_t*>(pending.row + 1); }
inline void Publish(const Pending& pending, uint32_t size, uint8_t tag) {
    pending.row->size = size;
    pending.row->tag = tag;
    MemoryBarrier();
    pending.row->sequence = pending.sequence;
}
inline void Count(Slot& slot, unsigned tally) { InterlockedIncrement(&slot.counts[tally]); }

// Installer side, on the Tick thread.

// The slot a new recording takes, or MaxRecorderSlots when every one is busy
// or just stopped.
uint32_t Pick();
// A recording to arm in a picked slot whose kind fields (setup) are written.
struct Arm {
    uint32_t kind, payload;
    void* volatile* cell;
    void* original;             // What the caller read from the cell.
    void* thunk;                // The kind's thunk for this slot.
    uint64_t target, filter, milliseconds;
};
enum class Started { Armed, NoMemory, Changed };
// Allocates the slot's rows on first use, resets it, marks it recording and
// swaps the cell from original to thunk. Changed: the cell no longer held
// original; the slot is free again and nothing was written.
Started Start(uint32_t index, const Arm& arm);
// Restores every recording past its time.
void Sweep();
// Ends and restores every recording (the host stopping).
void StopAll();
// The handler bodies behind a read op and a stop op (the protocol.h recorder
// section); any kind. The request's own checks are the caller's.
Progress Read(const Request& request, Response& response);
Progress Stop(const Request& request, Response& response);
}
