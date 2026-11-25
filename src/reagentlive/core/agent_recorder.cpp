// reagentlive core agent: the pointer-swap recorder slots (core/recorder.h):
// arming, restoring, sweeping, and reading and stopping any kind.
#include <cstring>
#include <new>
#include "reagentlive/core/agent_internal.h"
#include "reagentlive/core/recorder.h"

reagentlive::recorder::Slot reagentlive_recorder_slots[reagentlive::MaxRecorderSlots];

namespace reagentlive::recorder {
namespace {
uint32_t g_latest = RecorderLatest;
// Every thunk Start has put in a cell, with its slot: a kind has at most one
// per slot, so a few kinds fill it.
constexpr uint32_t MaxKnownThunks = 64;
struct KnownThunk { const void* thunk; uint32_t slot; };
KnownThunk g_thunks[MaxKnownThunks];
uint32_t g_thunkCount;

RecorderState StateOf(const Slot& slot) { return static_cast<RecorderState>(slot.state); }

void Remember(const void* thunk, uint32_t index) {
    for (uint32_t i = 0; i < g_thunkCount; ++i)
        if (g_thunks[i].thunk == thunk) return;
    if (g_thunkCount < MaxKnownThunks) g_thunks[g_thunkCount++] = {thunk, index};
}

// Puts the original pointer back if the cell still holds this slot's thunk;
// otherwise something else replaced it (or its memory is gone), and the
// thunk forwards for good.
void Restore(uint32_t index) {
    Slot& slot = g_slots[index];
    const auto state = StateOf(slot);
    if (state != RecorderState::Recording && state != RecorderState::Expired) return;
    void* previous = nullptr;
    const bool restored = detail::GuardedExchange(slot.cell, slot.original, slot.thunk, previous) && previous == slot.thunk;
    if (restored) slot.restoredMs = static_cast<int64_t>(GetTickCount64());
    slot.state = static_cast<LONG>(restored ? RecorderState::Finished : RecorderState::Replaced);
    if (restored && slot.release) slot.release(index);
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

// Microseconds per time-stamp tick, measured from the slot's start to now
// against the performance counter (the counter is invariant on x64 parts
// Windows supports); 0 before a tick has passed.
double TscScale(const Slot& slot) {
    const int64_t ticks = static_cast<int64_t>(__rdtsc()) - slot.tscStart;
    const int64_t counted = Now() - slot.start;
    if (ticks <= 0 || counted <= 0 || !slot.frequency) return 0;
    return static_cast<double>(counted) * 1e6 / static_cast<double>(slot.frequency) / static_cast<double>(ticks);
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
    slot.clock = arm.clock;
    slot.release = arm.release;
    slot.frequency = frequency.QuadPart;
    slot.start = Now();
    slot.tscStart = static_cast<int64_t>(__rdtsc());
    slot.deadline = slot.start + static_cast<int64_t>(arm.milliseconds) * frequency.QuadPart / 1000;
    slot.written = 0;
    slot.counts[0] = slot.counts[1] = 0;
    for (uint32_t r = 0; r < MaxRecorderRows; ++r) reinterpret_cast<RecorderRow*>(slot.rows + r * RowStride)->sequence = 0;
    Remember(arm.thunk, index);
    InterlockedExchange(&slot.state, static_cast<LONG>(RecorderState::Recording));
    void* previous = nullptr;
    if (!detail::GuardedExchange(arm.cell, arm.thunk, arm.original, previous) || previous != arm.original) {
        InterlockedExchange(&slot.state, static_cast<LONG>(RecorderState::Free));
        return Started::Changed;
    }
    g_latest = index;
    return Started::Armed;
}

bool IsThunk(const void* pointer, uint32_t& slot) {
    for (uint32_t i = 0; i < g_thunkCount; ++i)
        if (pointer && g_thunks[i].thunk == pointer) {
            slot = g_thunks[i].slot;
            return true;
        }
    return false;
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
    const double scale = slot.clock == RecorderClock::Tsc ? TscScale(slot) : 0;
    uint64_t sequence = request.args[1] + 1;
    if (written > MaxRecorderRows && sequence < written - MaxRecorderRows + 1) sequence = written - MaxRecorderRows + 1;
    uint64_t last = request.args[1];
    for (; sequence <= written; ++sequence) {
        const auto* row = reinterpret_cast<const RecorderRow*>(slot.rows + static_cast<size_t>((sequence - 1) % MaxRecorderRows) * RowStride);
        RecorderRow copy = *row;
        if (copy.sequence != sequence) break; // Still being written, or overwritten since.
        MemoryBarrier(); // The payload is read after the sequence that vouches for it.
        if (copy.size > MaxRecorderPayload || used + sizeof(copy) + copy.size > MaxRead) break;
        if (slot.clock == RecorderClock::Tsc) copy.microseconds = static_cast<int64_t>(static_cast<double>(copy.microseconds) * scale);
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

namespace reagentlive {
Progress RecorderRead(const Request& request, Response& response, bool, const Host&) {
    if ((response.status = Check(request)) != Status::Ok) return Progress::Done;
    return recorder::Read(request, response);
}

Progress RecorderStop(const Request& request, Response& response, bool, const Host&) {
    if ((response.status = Check(request)) != Status::Ok) return Progress::Done;
    return recorder::Stop(request, response);
}

void StopRecorders() { recorder::StopAll(); }
}
