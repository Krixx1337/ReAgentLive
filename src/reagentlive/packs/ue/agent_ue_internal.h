// reagentlive Unreal pack: helpers the SDK files share (reflection, calls,
// containers, script, each). Internal; hosts use agent_ue.h. Compiled in the
// host against its Dumper-7 SDK, on top of the SDK-free helpers.
#pragma once
#include <algorithm>
#include <charconv>
#include <cstring>
#include <iterator>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "SDK/CoreUObject_classes.hpp"
#include "reagentlive/packs/ue/agent_ue_base.h"

namespace reagentlive::ue {
// An SDK name as the SDK-free helpers take it.
inline NameId Id(const SDK::FName& name) { return {name.ComparisonIndex, static_cast<uint32_t>(name.Number)}; }
inline std::string_view FullName(const SDK::FName& name, std::string& out) { return FullName(Id(name), out); }
// FullName as a new string (the SDK's GetName and ToString without its name lookup).
inline std::string NameOf(const SDK::FName& name) { return NameOf(Id(name)); }

SDK::UFunction* FindFunction(const SDK::UStruct* start, std::string_view owner, std::string_view name,
                             const Context& pack, const SDK::UStruct** declaring = nullptr);
// Plain data by the property's flags or, where it lacks one, by its struct's members (protocol_ue.h).
bool PlainData(const SDK::FProperty* property, const Host& host);
// ProcessEvent under a fault guard: false when the call raised an access
// fault (the engine may be left in a bad state). Other exceptions pass.
bool GuardedProcessEvent(SDK::UObject* object, SDK::UFunction* function, void* params);
}
