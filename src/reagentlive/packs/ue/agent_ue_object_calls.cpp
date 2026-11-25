// reagentlive Unreal pack: the object recorder (object_calls.h). The object's
// vptr is pointed at its private vtable copy (core/vtable_copy.h) and the
// ProcessEvent entry swapped there for slot I's thunk, which forwards the
// call and then records the function and a guarded copy of its parameter
// block. Read after the call: inputs are as the caller passed them (the
// engine copies a Blueprint's parameters into its own frame and reads a
// native's through its exec stub), and outputs and the return value are
// filled. Only the copy is written; the table, code and imports never.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <cstring>
#include <utility>
#include "reagentlive/core/agent.h"
#include "reagentlive/core/recorder.h"
#include "reagentlive/core/vtable_copy.h"
#include "reagentlive/packs/ue/object_calls.h"

namespace reagentlive::ue::object_calls {
namespace {
using ProcessEvent = void (*)(void* self, void* function, void* params);
// The kind's setup word in a slot: UFunction::ParmsSize's offset.
constexpr unsigned SetupParmsSize = 0;

template<unsigned I> void Thunk(void* self, void* function, void* params) {
    recorder::Slot& slot = recorder::g_slots[I];
    const uint64_t object = reinterpret_cast<uint64_t>(self);
    int64_t now = 0;
    // Enter checks self against the filter (the object): a call through the
    // copy with another `this` is forwarded only.
    const bool armed = recorder::Enter(slot, ObjectCallsKind, object) && recorder::InTime(slot, now);
    reinterpret_cast<ProcessEvent>(slot.original)(self, function, params);
    if (armed) {
        const recorder::Pending row = recorder::Claim(slot, object, now);
        uint8_t* payload = recorder::Payload(row);
        const ObjectCall call{reinterpret_cast<uint64_t>(function)};
        memcpy(payload, &call, sizeof(call));
        uint32_t size = sizeof(call);
        uint8_t tag = 0;
        uint16_t parms = 0;
        const auto* field = static_cast<const uint8_t*>(function) + slot.setup[SetupParmsSize];
        if (!function || !GuardedCopy(&parms, field, sizeof(parms))) {
            tag = ObjectCallFaulted;
        } else if (parms) {
            const uint32_t bytes = parms < MaxObjectCallParams ? parms : MaxObjectCallParams;
            if (parms > bytes) tag |= ObjectCallTruncated;
            if (params && GuardedCopy(payload + sizeof(call), params, bytes)) size += bytes;
            else tag = ObjectCallFaulted;
        }
        if (tag & ObjectCallTruncated) recorder::Count(slot, ObjectTruncatedCount);
        if (tag & ObjectCallFaulted) recorder::Count(slot, ObjectFaultedCount);
        recorder::Publish(row, size, tag);
    }
    recorder::Leave(slot);
}

template<unsigned... I> constexpr auto MakeThunks(std::integer_sequence<unsigned, I...>) {
    return std::array<ProcessEvent, sizeof...(I)>{&Thunk<I>...};
}
constexpr auto Thunks = MakeThunks(std::make_integer_sequence<unsigned, MaxRecorderSlots>{});

// A recording's entry, given back when its cell is restored.
vtable::Entry g_entries[MaxRecorderSlots];
void GiveEntry(uint32_t index) { vtable::Give(g_entries[index]); }
}

Refusal Start(void* object, const Layout& layout, uint64_t milliseconds, bool dry, ObjectCallsStarted& out) {
    recorder::Sweep();
    out = {};
    // Checked first without writing: dry stops here, and a recorded or
    // non-code entry is refused before any copy is made.
    void** table = nullptr;
    void* entry = nullptr;
    if (!GuardedCopy(&table, object, sizeof(table)) || !table ||
        !GuardedCopy(&entry, table + layout.processEventIndex, sizeof(entry)))
        return {"the object's vptr or its ProcessEvent entry cannot be read", 0};
    if (uint32_t owner = 0; recorder::IsThunk(entry, owner)) return {"already recorded by slot", owner};
    if (!vtable::InCode(entry))
        return {"the ProcessEvent entry is not code in a loaded module", reinterpret_cast<uint64_t>(entry)};
    const uint32_t index = recorder::Pick();
    if (index == MaxRecorderSlots) return {"every recording slot is busy or just stopped; stop one or retry in a second", 0};
    out = {reinterpret_cast<uint64_t>(object), reinterpret_cast<uint64_t>(table),
        reinterpret_cast<uint64_t>(table + layout.processEventIndex), reinterpret_cast<uint64_t>(entry),
        reinterpret_cast<uint64_t>(Thunks[index]), index, layout.processEventIndex, 0, 0};
    if (dry) return {nullptr, 0};
    vtable::Entry taken{};
    if (const auto result = vtable::Take(object, 0, layout.processEventIndex, taken); result != vtable::Taken::Ok)
        return {vtable::TakenText(result), 0};
    out.cell = reinterpret_cast<uint64_t>(taken.cell);
    out.original = reinterpret_cast<uint64_t>(taken.original);
    out.count = taken.count;
    g_entries[index] = taken;
    recorder::Slot& slot = recorder::g_slots[index];
    slot.setup[SetupParmsSize] = static_cast<uint64_t>(layout.parmsSize);
    const recorder::Arm arm{ObjectCallsKind, MaxObjectCallParams, taken.cell, taken.original,
        reinterpret_cast<void*>(Thunks[index]), reinterpret_cast<uint64_t>(object), reinterpret_cast<uint64_t>(object),
        milliseconds, recorder::RecorderClock::Qpc, &GiveEntry};
    switch (recorder::Start(index, arm)) {
    case recorder::Started::Armed: return {nullptr, 0};
    case recorder::Started::NoMemory: vtable::Give(taken); return {"no memory for the recording", 0};
    case recorder::Started::Changed: vtable::Give(taken); return {"the ProcessEvent entry changed while starting", 0};
    }
    return {"refused", 0};
}
}
