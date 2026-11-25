// reagentlive core agent: the pointer-swap recorder slots (core/recorder.h):
// arming, restoring, sweeping, and reading and stopping any kind.
#include <cstring>
#include <new>
#include "reagentlive/core/recorder.h"

namespace reagentlive::recorder {
Slot g_slots[MaxRecorderSlots];

namespace {
uint32_t g_latest = RecorderLatest;

RecorderState StateOf(const Slot& slot) { return static_cast<RecorderState>(slot.state); }

// Puts the original pointer back if the cell still holds this slot's thunk;
// otherwise something else replaced it, and the thunk forwards for good.
void Restore(uint32_t index) {
    Slot& slot = g_slots[index];
    const auto state = StateOf(slot);
    if (state != RecorderState::Recording && state != RecorderState::Expired) return;
    const auto previous = InterlockedCompareExchangePointer(slot.cell, slot.original, slot.thunk);
    const bool restored = previous == slot.thunk;
    if (restored) slot.restoredMs = static_cast<int64_t>(GetTickCount64());
    slot.state = static_cast<LONG>(restored ? RecorderState::Finished : RecorderState::Replaced);
}

void End(uint32_t index) {
    if (StateOf(g_slots[index]) == RecorderState::Recording)
        InterlockedExchange(&g_slots[index].state, static_cast<LONG>(RecorderState::Expired));
    Restore(index);
}

bool SlotIndex(uint64_t requested, uint32_t& index) {
    index = static_cast<uint32_t>(requested == RecorderLatest ? g_latest : requested);
    return index < MaxRecorderSlots && StateOf(g_slots[index]) != RecorderState::Free;
}

RecorderStatus StatusOf(uint32_t index) {
    const Slot& slot = g_slots[index];
    RecorderStatus out{};
    out.target = slot.target;
    out.original = reinterpret_cast<uint64_t>(slot.original);
    out.written = static_cast<uint64_t>(slot.written);
    out.slot = index;
    out.state = static_cast<uint32_t>(slot.state);
    out.kind = slot.kind;
    out.payload = slot.payload;
    out.counts[0] = static_cast<uint32_t>(slot.counts[0]);
    out.counts[1] = static_cast<uint32_t>(slot.counts[1]);
    return out;
}
}

uint32_t Pick() {
    RecorderSlotView views[MaxRecorderSlots];
    for (uint32_t i = 0; i < MaxRecorderSlots; ++i)
        views[i] = {StateOf(g_slots[i]), static_cast<int32_t>(g_slots[i].inFlight), g_slots[i].restoredMs};
    return PickRecorderSlot(views, MaxRecorderSlots, static_cast<int64_t>(GetTickCount64()));
}

Started Start(uint32_t index, const Arm& arm) {
    Slot& slot = g_slots[index];
    if (!slot.rows && !(slot.rows = new (std::nothrow) uint8_t[MaxRecorderRows * RowStride])) return Started::NoMemory;
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    slot.kind = arm.kind;
    slot.payload = arm.payload;
    slot.cell = arm.cell;
    slot.original = arm.original;
    slot.thunk = arm.thunk;
    slot.target = arm.target;
    slot.filter = arm.filter;
    slot.frequency = frequency.QuadPart;
    slot.start = Now();
    slot.deadline = slot.start + static_cast<int64_t>(arm.milliseconds) * frequency.QuadPart / 1000;
    slot.written = 0;
    slot.counts[0] = slot.counts[1] = 0;
    for (uint32_t r = 0; r < MaxRecorderRows; ++r) reinterpret_cast<RecorderRow*>(slot.rows + r * RowStride)->sequence = 0;
    InterlockedExchange(&slot.state, static_cast<LONG>(RecorderState::Recording));
    if (InterlockedCompareExchangePointer(arm.cell, arm.thunk, arm.original) != arm.original) {
        InterlockedExchange(&slot.state, static_cast<LONG>(RecorderState::Free));
        return Started::Changed;
    }
    g_latest = index;
    return Started::Armed;
}

void Sweep() {
    for (uint32_t i = 0; i < MaxRecorderSlots; ++i)
        if (StateOf(g_slots[i]) == RecorderState::Expired ||
            (StateOf(g_slots[i]) == RecorderState::Recording && Now() > g_slots[i].deadline))
            Restore(i);
}

void StopAll() {
    for (uint32_t i = 0; i < MaxRecorderSlots; ++i) End(i);
}

Progress Read(const Request& request, Response& response) {
    Sweep();
    uint32_t index = 0;
    if (!SlotIndex(request.args[0], index)) { Refuse(response, Status::NotFound, "no such recording"); return Progress::Done; }
    const Slot& slot = g_slots[index];
    const RecorderStatus status = StatusOf(index);
    memcpy(response.data, &status, sizeof(status));
    size_t used = sizeof(status);
    const uint64_t written = status.written;
    uint64_t sequence = request.args[1] + 1;
    if (written > MaxRecorderRows && sequence < written - MaxRecorderRows + 1) sequence = written - MaxRecorderRows + 1;
    uint64_t last = request.args[1];
    for (; sequence <= written; ++sequence) {
        const auto* row = reinterpret_cast<const RecorderRow*>(slot.rows + static_cast<size_t>((sequence - 1) % MaxRecorderRows) * RowStride);
        RecorderRow copy = *row;
        if (copy.sequence != sequence) break; // Still being written, or overwritten since.
        MemoryBarrier(); // The payload is read after the sequence that vouches for it.
        if (copy.size > MaxRecorderPayload || used + sizeof(copy) + copy.size > MaxRead) break;
        memcpy(response.data + used, &copy, sizeof(copy));
        memcpy(response.data + used + sizeof(copy), row + 1, copy.size);
        MemoryBarrier();
        if (row->sequence != sequence) break; // Overwritten while copied: not sent.
        used += sizeof(copy) + copy.size;
        last = sequence;
    }
    response.length = static_cast<uint32_t>(used);
    response.detail = last;
    return Progress::Done;
}

Progress Stop(const Request& request, Response& response) {
    uint32_t index = 0;
    if (!SlotIndex(request.args[0], index)) { Refuse(response, Status::NotFound, "no such recording"); return Progress::Done; }
    End(index);
    const RecorderStatus status = StatusOf(index);
    memcpy(response.data, &status, sizeof(status));
    response.length = sizeof(status);
    return Progress::Done;
}
}
