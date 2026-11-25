// reagentlive Unreal pack: the call recorder (protocol_ue.h, CallsStart) on
// the core's recorder slots (core/recorder.h). One UFunction's native pointer
// is swapped for a fixed thunk that records the call and forwards it. Only
// heap data is written; code, vtables and imports are never touched.
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
#include <utility>
#include "reagentlive/core/recorder.h"
#include "reagentlive/packs/ue/agent_ue.h"
#include "reagentlive/packs/ue/agent_ue_internal.h"

namespace reagentlive::ue {
namespace {
using Native = void (*)(void* context, void* stack, void* result);
// The kind's setup words in a slot.
constexpr unsigned SetupReturnAt = 0, SetupReturnBytes = 1;

template<unsigned I> void Thunk(void* context, void* stack, void* result) {
    recorder::Slot& slot = recorder::g_slots[I];
    const uint64_t object = reinterpret_cast<uint64_t>(context);
    int64_t now = 0;
    const bool armed = recorder::Enter(slot, CallsKind, object) && recorder::InTime(slot, now);
    const auto original = reinterpret_cast<Native>(slot.original);
    if (!armed) {
        original(context, stack, result);
        recorder::Leave(slot);
        return;
    }
    void* node = nullptr;
    uint8_t* locals = nullptr;
    const auto* frame = static_cast<const uint8_t*>(stack);
    const bool read = frame && GuardedCopy(&node, frame + Engine().frameNode, sizeof(node)) &&
        GuardedCopy(&locals, frame + Engine().frameLocals, sizeof(locals));
    // ProcessEvent builds a frame for this function whose locals are the
    // parameter block; a Blueprint VM call passes its own frame.
    const bool event = read && reinterpret_cast<uint64_t>(node) == slot.target && locals;
    original(context, stack, result);
    const recorder::Pending row = recorder::Claim(slot, object, now);
    uint8_t* block = recorder::Payload(row);
    uint32_t size = 0;
    const auto returnOffset = static_cast<uint32_t>(slot.setup[SetupReturnAt]);
    const auto returnSize = static_cast<uint32_t>(slot.setup[SetupReturnBytes]);
    if (event && GuardedCopy(block, locals, slot.payload)) {
        // ProcessEvent writes the return value to the caller's block, not the frame's.
        if (result && returnSize) GuardedCopy(block + returnOffset, result, returnSize);
        size = slot.payload;
        recorder::Count(slot, CallsEventCount);
    } else {
        recorder::Count(slot, CallsVmCount);
    }
    recorder::Publish(row, size, event ? CallsEvent : CallsVm);
    recorder::Leave(slot);
}

template<unsigned... I> constexpr auto MakeThunks(std::integer_sequence<unsigned, I...>) {
    return std::array<Native, sizeof...(I)>{&Thunk<I>...};
}
constexpr auto Thunks = MakeThunks(std::make_integer_sequence<unsigned, MaxRecorderSlots>{});

Native* FuncField(Object* function) { return reinterpret_cast<Native*>(ExecFunctionSlot(function)); }

bool Executable(const void* address) {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || !VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT) return false;
    constexpr DWORD execute = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (info.Protect & execute) != 0 && !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS));
}
}

Progress CallsStart(const Request& request, Response& response, bool, const Host& host) {
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response) || !LayoutReady(GroupsCall, host, response)) return Progress::Done;
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    recorder::Sweep();
    Object* object = ResolveRequestObject(request, pack);
    if (!object) { Refuse(response, Status::NotFound, "object gone or not a valid object"); return Progress::Done; }
    const auto [owner, name] = SplitFunction(request.text);
    // The recorder swaps the function's native pointer: UFunctions are heap
    // data the engine itself writes, never code.
    auto* function = const_cast<Object*>(FindFunction(TypeOf(object), owner, name, pack));
    if (!function) { Refuse(response, Status::NotFound, "function not found on the class chain"); return Progress::Done; }
    const Native current = *FuncField(function);
    if (uint32_t recorded = 0; recorder::IsThunk(reinterpret_cast<const void*>(current), recorded)) {
        Refuse(response, Status::Refused, "already recorded by slot", recorded);
        return Progress::Done;
    }
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
    if (size < 0 || static_cast<uint32_t>(size) > MaxRecorderPayload) {
        Refuse(response, Status::Refused, "parameter block larger than a row holds", static_cast<uint64_t>(size < 0 ? 0 : size));
        return Progress::Done;
    }
    const uint32_t blockSize = static_cast<uint32_t>(size);
    const bool hasReturn = returnOffset != 0xFFFF && returnOffset < blockSize;
    const uint32_t index = recorder::Pick();
    if (index == MaxRecorderSlots) {
        Refuse(response, Status::Refused, "every recording slot is busy or just stopped; stop one or retry in a second");
        return Progress::Done;
    }
    CallsStarted started{reinterpret_cast<uint64_t>(function), reinterpret_cast<uint64_t>(current),
        reinterpret_cast<uint64_t>(Thunks[index]), index, blockSize, hasReturn ? returnOffset : 0xFFFFu,
        hasReturn ? blockSize - returnOffset : 0};
    if (!(request.flags & CallsDry)) {
        recorder::Slot& slot = recorder::g_slots[index];
        slot.setup[SetupReturnAt] = hasReturn ? returnOffset : 0;
        slot.setup[SetupReturnBytes] = hasReturn ? blockSize - returnOffset : 0;
        uint64_t filter = 0;
        if (request.length == sizeof(filter)) memcpy(&filter, request.data, sizeof(filter));
        const recorder::Arm arm{CallsKind, blockSize, reinterpret_cast<void* volatile*>(FuncField(function)),
            reinterpret_cast<void*>(current), reinterpret_cast<void*>(Thunks[index]), reinterpret_cast<uint64_t>(function),
            filter, request.args[1]};
        switch (recorder::Start(index, arm)) {
        case recorder::Started::Armed: break;
        case recorder::Started::NoMemory: Refuse(response, Status::Refused, "no memory for the recording"); return Progress::Done;
        case recorder::Started::Changed: Refuse(response, Status::Refused, "the native pointer changed while starting"); return Progress::Done;
        }
    }
    memcpy(response.data, &started, sizeof(started));
    response.length = sizeof(started);
    return Progress::Done;
}
}
