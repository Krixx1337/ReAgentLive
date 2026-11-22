// reagentlive Unreal pack: helpers the SDK-free files share (reagentlive::ue-base).
// Internal; hosts use agent_ue.h. Objects are opaque here (SDK::UObject is only
// declared): every field is read at an offset from the layout located at run
// time, so these files compile with or without the host's Dumper-7 SDK.
#pragma once
#include <stdint.h>
#include <cstring>
#include <string>
#include <string_view>
#include "reagentlive/packs/ue/agent_ue.h"
#include "reagentlive/packs/ue/globals.h"
#include "reagentlive/packs/ue/name_pool.h"

namespace reagentlive::ue {
// A full object table takes many ticks; bound each tick (Context::scanMicroseconds).
constexpr int64_t FindBudgetMicroseconds = 1000;
constexpr unsigned MaxClassDepth = 64, MaxChildren = 8192;
// The host's scan budget, set by PackReady before any handler runs.
extern int64_t g_scanMicroseconds;

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
        return now.QuadPart - start_.QuadPart > frequency_.QuadPart * g_scanMicroseconds / 1000000;
    }
private:
    LARGE_INTEGER frequency_{}, start_{};
};

template<class Text> std::string_view View(const Text& text) { return {text.c_str(), text.size()}; }

// Objects of one allocator region share a VirtualQuery. Reset every tick, so
// a cached region is never trusted beyond one budget slice.
struct Region { uintptr_t begin, end; };
extern Region g_region;

// The located layout: the table and object header (globals::TableShape) and
// what the pack knows of UStruct; -1 is unknown. Valid once PackReady said yes.
struct ObjectLayout {
    globals::TableShape shape;
    int32_t superStruct = -1, structSize = -1;
    bool fromSdk = false;
};
const ObjectLayout& Layout();

using Object = SDK::UObject;
struct NameId { int32_t index; uint32_t number; };

// Header reads: the object must be readable (HeaderReadable, or under the
// off-thread fault guard).
inline const uint8_t* Bytes(const Object* object) { return reinterpret_cast<const uint8_t*>(object); }
template<class T> T Field(const Object* object, uint32_t offset) {
    T value;
    memcpy(&value, Bytes(object) + offset, sizeof(value));
    return value;
}
inline int32_t ObjectIndex(const Object* object) { return Field<int32_t>(object, Layout().shape.objectIndexOffset); }
inline Object* ObjectClass(const Object* object) { return Field<Object*>(object, Layout().shape.objectClassOffset); }
inline uint32_t ObjectFlags(const Object* object) { return Field<uint32_t>(object, Layout().shape.objectFlagsOffset); }
inline NameId ObjectName(const Object* object) {
    const auto& shape = Layout().shape;
    return {Field<int32_t>(object, shape.objectNameOffset), Field<uint32_t>(object, shape.objectNameOffset + shape.nameNumberOffset)};
}
// The object's outer: right after its name in the header.
inline Object* ObjectOuter(const Object* object) { return Field<Object*>(object, globals::ObjectOuterOffset(Layout().shape)); }
// A class's parent class (UStruct::SuperStruct), or null when unknown.
inline const Object* SuperOf(const Object* type) {
    return Layout().superStruct < 0 ? nullptr : Field<const Object*>(type, static_cast<uint32_t>(Layout().superStruct));
}

// The located object table.
int32_t TableNum();
Object* TableObject(int32_t index);
// The slot's serial number (0 when not assigned, or unknown to the layout); false for an empty slot.
bool TableItem(int32_t index, Object*& object, int32_t& serial);

bool HeaderReadable(const Object* object);
bool ClassMatches(const Object* object, std::string_view wanted, std::string& scratch);
void ResetClassCache();
std::string_view ClassArgument(const Request& request);
// The request's object filter on outers (protocol_ue.h): true when none is set.
bool OuterMatches(const Request& request, const Object* object, std::string& scratch);
// False (and the response refused as not found) when the filter's outer object is gone.
bool OuterAlive(const Request& request, const Context& pack, Response& response);
// The object's outer as a ref, zero when it has none (a package) or it is not an object.
Ref OuterRef(const Object* object, const Context& pack);
bool Template(const Object* object);
std::string_view FullName(NameId name, std::string& out);
// FullName as a new string.
inline std::string NameOf(NameId name) {
    std::string out;
    FullName(name, out);
    return out;
}
const Context& Pack(const Host& host);
// False (and the response refused) when EngineThreadProblem names one or
// the object table and name pool are not located and checked (globals.h);
// the first call locates them.
// Access::Read handlers also run off the engine's thread when the host
// opted in (Context::offThreadReads).
enum class Access { Engine, Read };
bool PackReady(const Context& pack, const Host& host, Response& response, Access access = Access::Engine);
// True on the engine's thread, or when `before` still names the same live
// object; otherwise the response is cleared and refused.
bool StillSame(const Ref& before, const Context& pack, Response& response);
// Runs a read handler, under a fault guard when off the engine's thread
// (a fault there is a refusal, not a crash).
Progress GuardedRead(Handler body, const Request& request, Response& response, bool first, const Host& host);
// A name from the located pool, without its instance number.
bool PoolName(int32_t index, char* out, size_t capacity, size_t& length);
// The located pool and its entry layout, or 0.
uint64_t NamePool(const names::Layout*& layout);
Ref Identify(const Object* object, const Context& pack);
Object* Resolve(const Ref& ref, const Context& pack);
Object* ResolveRequestObject(const Request& request, const Context& pack);
bool GuardedName(int32_t index, char* out, size_t capacity, size_t& length);
}
