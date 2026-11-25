// reagentlive Unreal pack, agent side: the object recorder (protocol_ue.h,
// CallsStart with CallsObject) on the core's recorder slots and vtable
// copies. Needs no object table: CallsStart resolves the object first, and
// the loopback tests drive it on a fake object.
#pragma once
#include <stdint.h>
#include "reagentlive/packs/ue/protocol_ue.h"

namespace reagentlive::ue::object_calls {
// Where ProcessEvent is: its entry in the object's primary table (at offset
// 0) and UFunction::ParmsSize (uint16), both from the engine layout.
struct Layout { uint32_t processEventIndex; int32_t parmsSize; };
struct Refusal { const char* why; uint64_t detail; };
// Checks, then (unless dry) takes the entry in the object's vtable copy and
// arms a slot. Refusal::why is null when started; out is the reply. On the
// Tick thread.
Refusal Start(void* object, const Layout& layout, uint64_t milliseconds, bool dry, ObjectCallsStarted& out);
}
