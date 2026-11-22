// reagentlive Unreal pack: the call recorder (protocol_ue.h, CallsStart).
// One UFunction's native pointer is swapped for a fixed thunk that records
// the call and forwards it. Only heap data is written; code, vtables and
// imports are never touched.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <cstddef>
#include <cstring>
#include <new>
#include <utility>
#include "reagentlive/packs/ue/agent_ue.h"
#include "reagentlive/packs/ue/agent_ue_internal.h"

namespace reagentlive::ue {
namespace {
using Native = void (*)(void* context, void* stack, void* result);
constexpr size_t RowStride = sizeof(CallsRow) + MaxCallsBlock;

// One recording. Written on the game thread while not recording; the thunk
// (any thread) only reads the setup and appends rows.
struct Slot {
    volatile LONG state;        // CallsSlotState.
    volatile LONG inFlight;     // Calls inside the thunk now: a slot is reused only at zero.
    Object* function;           // The UFunction whose native pointer is swapped.
    Native original;
    uint64_t filter;
    uint32_t blockSize, returnOffset, returnSize;
    int64_t start, deadline, frequency;
    volatile LONG64 written;
    volatile LONG events, vm;
    int64_t restoredMs;         // When the restore succeeded: reuse waits CallsReuseGraceMs.
    uint8_t* rows;              // MaxCallsRows * RowStride, allocated on first use and kept.
};
Slot g_slots[MaxCallsSlots];
uint32_t g_latest = CallsLatest;

int64_t Now() {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

template<unsigned I> void Thunk(void* context, void* stack, void* result) {
    Slot& slot = g_slots[I];
    InterlockedIncrement(&slot.inFlight);
    const Native original = slot.original;
    const bool armed = slot.state == static_cast<LONG>(CallsSlotState::Recording) &&
        (!slot.filter || reinterpret_cast<uint64_t>(context) == slot.filter);
    if (!armed) {
        original(context, stack, result);
        InterlockedDecrement(&slot.inFlight);
        return;
    }
    const int64_t now = Now();
    if (now > slot.deadline) {
        // Past its time: stop recording; the game thread restores the pointer.
        InterlockedCompareExchange(&slot.state, static_cast<LONG>(CallsSlotState::Expired),
            static_cast<LONG>(CallsSlotState::Recording));
        original(context, stack, result);
        InterlockedDecrement(&slot.inFlight);
        return;
    }
    void* node = nullptr;
    uint8_t* locals = nullptr;
    const auto* frame = static_cast<const uint8_t*>(stack);
    const bool read = frame && GuardedCopy(&node, frame + Engine().frameNode, sizeof(node)) &&
        GuardedCopy(&locals, frame + Engine().frameLocals, sizeof(locals));
    // ProcessEvent builds a frame for this function whose locals are the
    // parameter block; a Blueprint VM call passes its own frame.
    const bool event = read && node == slot.function && locals;
    original(context, stack, result);
    const LONG64 sequence = InterlockedIncrement64(&slot.written);
    auto* row = reinterpret_cast<CallsRow*>(slot.rows + static_cast<size_t>((sequence - 1) % MaxCallsRows) * RowStride);
    row->sequence = 0;
    MemoryBarrier();
    row->object = reinterpret_cast<uint64_t>(context);
    row->microseconds = slot.frequency ? (now - slot.start) * 1000000 / slot.frequency : 0;
    row->thread = GetCurrentThreadId();
    row->via = event ? CallsEvent : CallsVm;
    row->size = 0;
    auto* block = reinterpret_cast<uint8_t*>(row + 1);
    if (event && GuardedCopy(block, locals, slot.blockSize)) {
        // ProcessEvent writes the return value to the caller's block, not the frame's.
        if (result && slot.returnSize) GuardedCopy(block + slot.returnOffset, result, slot.returnSize);
        row->size = slot.blockSize;
        InterlockedIncrement(&slot.events);
    } else {
        InterlockedIncrement(&slot.vm);
    }
    MemoryBarrier();
    row->sequence = static_cast<uint64_t>(sequence);
    InterlockedDecrement(&slot.inFlight);
}

template<unsigned... I> constexpr auto MakeThunks(std::integer_sequence<unsigned, I...>) {
    return std::array<Native, sizeof...(I)>{&Thunk<I>...};
}
constexpr auto Thunks = MakeThunks(std::make_integer_sequence<unsigned, MaxCallsSlots>{});

Native* FuncField(Object* function) { return reinterpret_cast<Native*>(ExecFunctionSlot(function)); }

// Puts the original pointer back if it still holds this slot's thunk;
// otherwise something else replaced it, and the thunk forwards for good.
void Restore(unsigned index) {
    Slot& slot = g_slots[index];
    const auto state = static_cast<CallsSlotState>(slot.state);
    if (state != CallsSlotState::Recording && state != CallsSlotState::Expired) return;
    const auto previous = InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(FuncField(slot.function)),
        reinterpret_cast<void*>(slot.original), reinterpret_cast<void*>(Thunks[index]));
    const bool restored = previous == reinterpret_cast<void*>(Thunks[index]);
    if (restored) slot.restoredMs = static_cast<int64_t>(GetTickCount64());
    slot.state = static_cast<LONG>(restored ? CallsSlotState::Finished : CallsSlotState::Replaced);
}

void Sweep() {
    for (unsigned i = 0; i < MaxCallsSlots; ++i)
        if (static_cast<CallsSlotState>(g_slots[i].state) == CallsSlotState::Expired ||
            (static_cast<CallsSlotState>(g_slots[i].state) == CallsSlotState::Recording && Now() > g_slots[i].deadline))
            Restore(i);
}

bool Executable(const void* address) {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || !VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT) return false;
    constexpr DWORD execute = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (info.Protect & execute) != 0 && !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS));
}

bool SlotIndex(uint64_t requested, unsigned& index) {
    index = static_cast<unsigned>(requested == CallsLatest ? g_latest : requested);
    return index < MaxCallsSlots && static_cast<CallsSlotState>(g_slots[index].state) != CallsSlotState::Free;
}

CallsState StateOf(unsigned index) {
    const Slot& slot = g_slots[index];
    CallsState out{};
    out.function = reinterpret_cast<uint64_t>(slot.function);
    out.written = static_cast<uint64_t>(slot.written);
    out.slot = index;
    out.state = static_cast<uint32_t>(slot.state);
    out.events = static_cast<uint32_t>(slot.events);
    out.vm = static_cast<uint32_t>(slot.vm);
    out.blockSize = slot.blockSize;
    return out;
}
}

void SweepCallRecordings() { Sweep(); }

void StopCallRecordings() {
    for (unsigned i = 0; i < MaxCallsSlots; ++i) {
        if (static_cast<CallsSlotState>(g_slots[i].state) == CallsSlotState::Recording)
            InterlockedExchange(&g_slots[i].state, static_cast<LONG>(CallsSlotState::Expired));
        Restore(i);
    }
}

Progress CallsStart(const Request& request, Response& response, bool, const Host& host) {
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response) || !LayoutReady(GroupsCall, host, response)) return Progress::Done;
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    Sweep();
    Object* object = ResolveRequestObject(request, pack);
    if (!object) { Refuse(response, Status::NotFound, "object gone or not a valid object"); return Progress::Done; }
    const auto [owner, name] = SplitFunction(request.text);
    // The recorder swaps the function's native pointer: UFunctions are heap
    // data the engine itself writes, never code.
    auto* function = const_cast<Object*>(FindFunction(TypeOf(object), owner, name, pack));
    if (!function) { Refuse(response, Status::NotFound, "function not found on the class chain"); return Progress::Done; }
    const Native current = *FuncField(function);
    for (unsigned i = 0; i < MaxCallsSlots; ++i)
        if (current == Thunks[i]) { Refuse(response, Status::Refused, "already recorded by slot", i); return Progress::Done; }
    if (!Executable(reinterpret_cast<const void*>(current))) {
        Refuse(response, Status::Refused, "the function's native pointer is not code", reinterpret_cast<uint64_t>(current));
        return Progress::Done;
    }
    uint16_t returnOffset = 0xFFFF;
    if (!GuardedCopy(&returnOffset, reinterpret_cast<const uint8_t*>(function) + Engine().returnValueOffset, sizeof(returnOffset))) {
        Refuse(response, Status::Refused, "function unreadable");
        return Progress::Done;
    }
    const auto size = StructSize(function);
    if (size < 0 || static_cast<uint32_t>(size) > MaxCallsBlock) {
        Refuse(response, Status::Refused, "parameter block larger than a row holds", static_cast<uint64_t>(size < 0 ? 0 : size));
        return Progress::Done;
    }
    const uint32_t blockSize = static_cast<uint32_t>(size);
    const bool hasReturn = returnOffset != 0xFFFF && returnOffset < blockSize;
    CallsSlotView views[MaxCallsSlots];
    for (unsigned i = 0; i < MaxCallsSlots; ++i)
        views[i] = {static_cast<CallsSlotState>(g_slots[i].state), static_cast<int32_t>(g_slots[i].inFlight), g_slots[i].restoredMs};
    const unsigned index = PickCallsSlot(views, MaxCallsSlots, static_cast<int64_t>(GetTickCount64()));
    if (index == MaxCallsSlots) {
        Refuse(response, Status::Refused, "every recording slot is busy or just stopped; stop one or retry in a second");
        return Progress::Done;
    }
    CallsStarted started{reinterpret_cast<uint64_t>(function), reinterpret_cast<uint64_t>(current),
        reinterpret_cast<uint64_t>(Thunks[index]), index, blockSize, hasReturn ? returnOffset : 0xFFFFu,
        hasReturn ? blockSize - returnOffset : 0};
    if (!(request.flags & CallsDry)) {
        Slot& slot = g_slots[index];
        if (!slot.rows && !(slot.rows = new (std::nothrow) uint8_t[MaxCallsRows * RowStride])) {
            Refuse(response, Status::Refused, "no memory for the recording");
            return Progress::Done;
        }
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        slot.function = function;
        slot.original = current;
        uint64_t filter = 0;
        if (request.length == sizeof(filter)) memcpy(&filter, request.data, sizeof(filter));
        slot.filter = filter;
        slot.blockSize = blockSize;
        slot.returnOffset = hasReturn ? returnOffset : 0;
        slot.returnSize = hasReturn ? blockSize - returnOffset : 0;
        slot.frequency = frequency.QuadPart;
        slot.start = Now();
        slot.deadline = slot.start + static_cast<int64_t>(request.args[1]) * frequency.QuadPart / 1000;
        slot.written = 0;
        slot.events = slot.vm = 0;
        for (unsigned r = 0; r < MaxCallsRows; ++r) reinterpret_cast<CallsRow*>(slot.rows + r * RowStride)->sequence = 0;
        InterlockedExchange(&slot.state, static_cast<LONG>(CallsSlotState::Recording));
        const auto previous = InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(FuncField(function)),
            reinterpret_cast<void*>(Thunks[index]), reinterpret_cast<void*>(current));
        if (previous != reinterpret_cast<void*>(current)) {
            InterlockedExchange(&slot.state, static_cast<LONG>(CallsSlotState::Free));
            Refuse(response, Status::Refused, "the native pointer changed while starting");
            return Progress::Done;
        }
        g_latest = index;
    }
    memcpy(response.data, &started, sizeof(started));
    response.length = sizeof(started);
    return Progress::Done;
}

Progress CallsRead(const Request& request, Response& response, bool, const Host& host) {
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response)) return Progress::Done;
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    Sweep();
    unsigned index = 0;
    if (!SlotIndex(request.args[0], index)) { Refuse(response, Status::NotFound, "no such recording"); return Progress::Done; }
    const Slot& slot = g_slots[index];
    const CallsState state = StateOf(index);
    memcpy(response.data, &state, sizeof(state));
    size_t used = sizeof(state);
    const uint64_t written = state.written;
    uint64_t sequence = request.args[1] + 1;
    if (written > MaxCallsRows && sequence < written - MaxCallsRows + 1) sequence = written - MaxCallsRows + 1;
    uint64_t last = request.args[1];
    for (; sequence <= written; ++sequence) {
        const auto* row = reinterpret_cast<const CallsRow*>(slot.rows + static_cast<size_t>((sequence - 1) % MaxCallsRows) * RowStride);
        CallsRow copy = *row;
        if (copy.sequence != sequence) break; // Still being written, or overwritten since.
        MemoryBarrier(); // The block is read after the sequence that vouches for it.
        if (used + sizeof(copy) + copy.size > MaxRead) break;
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

Progress CallsStop(const Request& request, Response& response, bool, const Host& host) {
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response)) return Progress::Done;
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    unsigned index = 0;
    if (!SlotIndex(request.args[0], index)) { Refuse(response, Status::NotFound, "no such recording"); return Progress::Done; }
    if (static_cast<CallsSlotState>(g_slots[index].state) == CallsSlotState::Recording)
        InterlockedExchange(&g_slots[index].state, static_cast<LONG>(CallsSlotState::Expired));
    Restore(index);
    const CallsState state = StateOf(index);
    memcpy(response.data, &state, sizeof(state));
    response.length = sizeof(state);
    return Progress::Done;
}
}
