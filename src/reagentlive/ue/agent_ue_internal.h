// reagentlive Unreal pack: helpers the agent_ue*.cpp files share. Internal; hosts
// use agent_ue.h. Compiled in the host against its Dumper-7 SDK.
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
#include "reagentlive/ue/name_pool.h"
#include "SDK/CoreUObject_classes.hpp"
#include "reagentlive/ue/agent_ue.h"

namespace reagentlive::ue {
// A full object table takes many ticks; bound each tick.
constexpr int64_t FindBudgetMicroseconds = 1000;
constexpr unsigned MaxClassDepth = 64, MaxChildren = 8192;

// One tick's share of a long scan.
class Budget {
public:
    Budget() {
        QueryPerformanceFrequency(&frequency_);
        QueryPerformanceCounter(&start_);
    }
    bool Spent() const {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        return now.QuadPart - start_.QuadPart > frequency_.QuadPart * FindBudgetMicroseconds / 1000000;
    }
private:
    LARGE_INTEGER frequency_{}, start_{};
};

template<class Text> std::string_view View(const Text& text) { return {text.c_str(), text.size()}; }

// Objects of one allocator region share a VirtualQuery. Reset every tick, so
// a cached region is never trusted beyond one budget slice.
struct Region { uintptr_t begin, end; };
extern Region g_region;

bool HeaderReadable(const SDK::UObject* object);
bool ClassMatches(const SDK::UObject* object, std::string_view wanted, std::string& scratch);
void ResetClassCache();
std::string_view ClassArgument(const Request& request);
bool Template(const SDK::UObject* object);
std::string_view FullName(const SDK::FName& name, std::string& out);
const Context& Pack(const Host& host);
// False (and the response refused) when EngineThreadProblem names one.
bool OnEngineThread(const Context& pack, Response& response);
Ref Identify(const SDK::UObject* object, const Context& pack);
SDK::UObject* Resolve(const Ref& ref, const Context& pack);
SDK::UObject* ResolveRequestObject(const Request& request, const Context& pack);
SDK::UFunction* FindFunction(const SDK::UStruct* start, std::string_view owner, std::string_view name,
                             const Context& pack, const SDK::UStruct** declaring = nullptr);
bool GuardedName(int32_t index, char* out, size_t capacity, size_t& length);
// Plain data by the property's flags or, where it lacks one, by its struct's members (protocol_ue.h).
bool PlainData(const SDK::FProperty* property, const Host& host);
}
