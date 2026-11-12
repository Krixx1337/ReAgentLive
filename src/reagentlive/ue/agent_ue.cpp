// reagentlive Unreal pack handlers.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // windows.h min/max macros break std::min/max.
#endif
#include <windows.h>
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
#include "reagentlive/ue/agent_ue.h"
#include "reagentlive/ue/name_pool.h"
#include "SDK/CoreUObject_classes.hpp"
#include "reagentlive/ue/agent_ue_internal.h"

namespace reagentlive::ue {
namespace {
constexpr size_t MaxNameCacheBytes = 64u << 20;

struct FindState { int32_t cursor; uint32_t count, total, next; };
FindState g_find;

// Base names by FName comparison index. The name pool only grows and never
// changes an entry, so a decoded name stays valid for the process; repeated
// finds skip almost all name decoding. Heap-allocated on first use (no dynamic
// initialisation); disabled if allocation fails or it grows too large.
struct NameCache {
    std::unordered_map<int32_t, std::string> names;
    size_t bytes = 0;
};
NameCache* g_names;
bool g_namesDisabled;

}

Region g_region;

bool HeaderReadable(const SDK::UObject* object) {
    const auto address = reinterpret_cast<uintptr_t>(object);
    constexpr size_t size = sizeof(SDK::UObject);
    if (address >= g_region.begin && address + size <= g_region.end) return true;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(object, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !(info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                          PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
        return false;
    g_region = {reinterpret_cast<uintptr_t>(info.BaseAddress), reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize};
    return address + size <= g_region.end;
}

namespace {
// Base name (without the _N instance suffix) of an FName comparison index.
std::string_view BaseName(int32_t index, std::string& scratch) {
    if (!g_names && !g_namesDisabled) {
        g_names = new (std::nothrow) NameCache;
        g_namesDisabled = !g_names;
    }
    if (g_names) {
        if (const auto found = g_names->names.find(index); found != g_names->names.end()) return found->second;
    }
    // From the located pool, without the path before the last '/' (as the
    // SDK's FName::ToString prints it); undecodable is empty.
    char text[1024];
    size_t length = 0;
    if (!PoolName(index, text, sizeof(text), length)) length = 0;
    std::string_view view(text, length);
    if (const auto slash = view.rfind('/'); slash != std::string_view::npos) view.remove_prefix(slash + 1);
    scratch.assign(view);
    if (!g_names) return scratch;
    try {
        if (g_names->bytes + scratch.size() + 48 > MaxNameCacheBytes) throw std::bad_alloc();
        g_names->bytes += scratch.size() + 48; // Rough per-entry overhead.
        return g_names->names.emplace(index, scratch).first->second;
    } catch (...) {
        delete g_names;
        g_names = nullptr;
        g_namesDisabled = true;
        return scratch;
    }
}

bool SameText(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const char x = a[i] >= 'A' && a[i] <= 'Z' ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
        const char y = b[i] >= 'A' && b[i] <= 'Z' ? static_cast<char>(b[i] - 'A' + 'a') : b[i];
        if (x != y) return false;
    }
    return true;
}

// Class verdicts for the running find, keyed by class pointer: a table has
// tens of thousands of classes but hundreds of thousands of objects, and class
// headers live outside the objects' allocator regions. Cleared per search.
std::unordered_map<const void*, bool>* g_classes;

bool ClassChainMatches(const SDK::UStruct* type, std::string_view wanted, std::string& scratch) {
    MEMORY_BASIC_INFORMATION info{};
    for (unsigned depth = 0; type && depth < MaxClassDepth; type = type->SuperStruct, ++depth) {
        if (!VirtualQuery(type, &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            reinterpret_cast<uintptr_t>(type) + sizeof(SDK::UStruct) >
                reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize) return false;
        if (SameText(BaseName(type->Name.ComparisonIndex, scratch), wanted)) return true;
    }
    return false;
}
}

// Whether the object's class or a parent class is named `wanted`. Headers only:
// full validation stays for matches.
bool ClassMatches(const SDK::UObject* object, std::string_view wanted, std::string& scratch) {
    const SDK::UStruct* type = object->Class;
    if (!type) return false;
    if (g_classes) {
        if (const auto found = g_classes->find(type); found != g_classes->end()) return found->second;
    }
    const bool match = ClassChainMatches(type, wanted, scratch);
    if (g_classes) {
        try { g_classes->emplace(type, match); } catch (...) { g_classes->clear(); }
    }
    return match;
}

void ResetClassCache() {
    if (!g_classes) g_classes = new (std::nothrow) std::unordered_map<const void*, bool>;
    if (g_classes) g_classes->clear();
}

std::string_view ClassArgument(const Request& request) {
    return request.length ? std::string_view(reinterpret_cast<const char*>(request.data)) : std::string_view{};
}

// Live instances only: class default and archetype objects are templates.
bool Template(const SDK::UObject* object) {
    constexpr uint32_t templates = static_cast<uint32_t>(SDK::EObjectFlags::ClassDefaultObject) |
        static_cast<uint32_t>(SDK::EObjectFlags::ArchetypeObject);
    return (static_cast<uint32_t>(object->Flags) & templates) != 0;
}

// Full name as the engine prints it: base, then _<number - 1> when numbered.
std::string_view FullName(const SDK::FName& name, std::string& out) {
    std::string scratch;
    const auto base = BaseName(name.ComparisonIndex, scratch);
    if (!name.Number) return out.assign(base);
    char digits[12];
    const auto end = std::to_chars(digits, digits + sizeof(digits), name.Number - 1).ptr;
    out.assign(base).append(1, '_').append(digits, end);
    return out;
}

const Context& Pack(const Host& host) { return *static_cast<const Context*>(host.context); }

// Object-table identity: slot index plus serial (0 = not assigned yet, which
// agrees with any later serial for the same slot and address).
Ref Identify(const SDK::UObject* object, const Context& pack) {
    if (!object || !pack.object(object) || object->Index < 0) return {};
    const auto* item = Table()->GetItemByIndex(object->Index);
    if (!item || item->Object != object || item->SerialNumber < 0) return {};
    return {reinterpret_cast<uintptr_t>(object), object->Index, item->SerialNumber};
}

SDK::UObject* Resolve(const Ref& ref, const Context& pack) {
    if (!ref.address || ref.index < 0 || ref.serial < 0) return nullptr;
    const auto* item = Table()->GetItemByIndex(ref.index);
    if (!item || reinterpret_cast<uintptr_t>(item->Object) != ref.address || item->SerialNumber < 0 ||
        (ref.serial && ref.serial != item->SerialNumber)) return nullptr;
    SDK::UObject* object = item->Object;
    if (!pack.object(object)) return nullptr;
    return object->Index == ref.index ? object : nullptr;
}

// Index < 0: address only, identified now. Refs from find keep their serial.
SDK::UObject* ResolveRequestObject(const Request& request, const Context& pack) {
    Ref ref = GetRef(request);
    if (ref.index < 0) ref = Identify(reinterpret_cast<const SDK::UObject*>(ref.address), pack);
    return Resolve(ref, pack);
}

namespace {
bool SameName(const SDK::UObject* object, std::string_view expected) {
    std::string name;
    return FullName(object->Name, name) == expected;
}
}

SDK::UFunction* FindFunction(const SDK::UStruct* start, std::string_view owner, std::string_view name,
                             const Context& pack, const SDK::UStruct** declaring) {
    unsigned depth = 0;
    for (const SDK::UStruct* type = start; type && depth < MaxClassDepth && pack.object(type);
         type = type->SuperStruct, ++depth) {
        if (!owner.empty() && !SameName(type, owner)) continue;
        unsigned children = 0;
        for (SDK::UField* field = type->Children; field && children < MaxChildren && pack.object(field);
             field = field->Next, ++children)
            if (field->HasTypeFlag(SDK::EClassCastFlags::Function) && SameName(field, name)) {
                if (declaring) *declaring = type;
                return static_cast<SDK::UFunction*>(field);
            }
    }
    return nullptr;
}

namespace {
// Decodes one name into out. SEH needs a frame without unwinding objects, so
// the std::string lives here and the guard in GuardedName.
__declspec(noinline) size_t NameInto(int32_t index, char* out, size_t capacity) {
    std::string scratch;
    const auto name = BaseName(index, scratch);
    const size_t length = name.size() < capacity ? name.size() : capacity - 1;
    memcpy(out, name.data(), length);
    out[length] = 0;
    return length;
}
}

bool GuardedName(int32_t index, char* out, size_t capacity, size_t& length) {
    __try {
        length = NameInto(index, out, capacity);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

Progress FindBody(const Request& request, Response& response, bool first, const Host& host) {
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response, Access::Read)) return Progress::Done;
    const auto from = static_cast<int32_t>(request.args[1]);
    if (first) {
        if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
        g_find = {};
        g_find.cursor = from; // Later pages start at their cursor instead of rescanning from slot 0.
        ResetClassCache();
    }
    const bool counting = from == 0; // Only the first page counts every match.
    const std::string_view query(request.text);
    const std::string_view wantedClass = ClassArgument(request);
    auto* entries = reinterpret_cast<FindEntry*>(response.data);
    Budget budget;
    const int32_t count = Table()->Num();
    g_region = {};
    std::string name, scratch;
    for (unsigned checked = 0; g_find.cursor < count; ++g_find.cursor, ++checked) {
        if ((checked & 63) == 63 && budget.Spent()) return Progress::Continue;
        // Cheap header check while scanning; full validation only for matches.
        SDK::UObject* object = Table()->GetByIndex(g_find.cursor);
        if (!object || !HeaderReadable(object) || object->Index != g_find.cursor) continue;
        if (!query.empty() && !NameMatches(FullName(object->Name, name), query)) continue;
        if (!wantedClass.empty() && !ClassMatches(object, wantedClass, scratch)) continue;
        if (!pack.object(object)) continue;
        if (query.empty()) FullName(object->Name, name);
        ++g_find.total;
        if (g_find.count >= MaxFindResults) {
            if (!g_find.next) g_find.next = static_cast<uint32_t>(g_find.cursor);
            if (!counting) break;
            continue;
        }
        FindEntry& entry = entries[g_find.count++];
        entry.object = Identify(object, pack);
        CopyText(entry.name, name);
        if (pack.object(object->Class)) {
            CopyText(entry.className, FullName(object->Class->Name, scratch));
        } else {
            CopyText(entry.className, "?");
        }
    }
    response.length = static_cast<uint32_t>(g_find.count * sizeof(FindEntry));
    response.detail = FindDetail(counting ? g_find.total : 0, g_find.next);
    return Progress::Done;
}

static_assert(offsetof(SDK::UObject, Index) == ObjectIdentityOffset &&
              offsetof(SDK::UObject, Class) == ObjectIdentityOffset + 4 && ObjectIdentityLength == 4 + sizeof(void*));

namespace {
// One ref (address-only, full, or weak slot and serial) to its entry; false when gone or not an object.
// `classSize`: the size of the object's class (0 when unknown), which covers
// native members past the last reflected field.
bool DescribeRef(Ref ref, const Context& pack, FindEntry& entry, uint32_t* classSize = nullptr) {
    if (!ref.address) {
        if (ref.index < 0 || ref.serial <= 0) return false;
        const auto* item = Table()->GetItemByIndex(ref.index);
        if (item && item->SerialNumber == ref.serial) ref.address = reinterpret_cast<uintptr_t>(item->Object);
    } else if (ref.index < 0) {
        ref = Identify(reinterpret_cast<const SDK::UObject*>(ref.address), pack);
    }
    const auto* object = Resolve(ref, pack);
    if (!object) return false;
    entry = {};
    entry.object = Identify(object, pack);
    std::string name;
    CopyText(entry.name, FullName(object->Name, name));
    if (pack.object(object->Class)) {
        CopyText(entry.className, FullName(object->Class->Name, name));
        if (classSize && object->Class->Size > 0) *classSize = static_cast<uint32_t>(object->Class->Size);
    } else {
        CopyText(entry.className, "?");
    }
    return true;
}
}

Progress DescribeBody(const Request& request, Response& response, bool, const Host& host) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response, Access::Read)) return Progress::Done;
    FindEntry entry{};
    uint32_t classSize = 0;
    if (!DescribeRef(GetRef(request), pack, entry, &classSize)) {
        Refuse(response, Status::NotFound, "object gone or not a valid object");
        return Progress::Done;
    }
    if (!StillSame(entry.object, pack, response)) return Progress::Done;
    memcpy(response.data, &entry, sizeof(entry));
    response.length = sizeof(entry);
    response.detail = classSize;
    return Progress::Done;
}

Progress DescribeManyBody(const Request& request, Response& response, bool, const Host& host) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response, Access::Read)) return Progress::Done;
    const size_t count = request.length / sizeof(Ref);
    uint64_t found = 0;
    for (size_t i = 0; i < count; ++i) {
        Ref ref{};
        memcpy(&ref, request.data + i * sizeof(Ref), sizeof(ref));
        FindEntry entry{};
        if (DescribeRef(ref, pack, entry) && (pack.gameThread() || Resolve(entry.object, pack))) ++found;
        else entry = {};
        memcpy(response.data + i * sizeof(entry), &entry, sizeof(entry));
    }
    response.length = static_cast<uint32_t>(count * sizeof(FindEntry));
    response.detail = found;
    return Progress::Done;
}

Progress ReadObjectBody(const Request& request, Response& response, bool first, const Host& host) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response, Access::Read)) return Progress::Done;
    const auto* object = Resolve(GetRef(request), pack);
    if (!object) {
        Refuse(response, Status::NotFound, "object gone or not a valid object");
        return Progress::Done;
    }
    const Ref before = Identify(object, pack);
    Request read{};
    read.op = reagentlive::op::Read;
    read.address = request.address + request.args[1];
    read.length = request.length;
    const Progress progress = reagentlive::Read(read, response, first, host);
    if (response.status == Status::Ok) StillSame(before, pack, response);
    return progress;
}

Progress PreviewArrayBody(const Request& request, Response& response, bool first, const Host& host) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response, Access::Read)) return Progress::Done;
    const auto* object = ResolveRequestObject(request, pack);
    if (!object || object->IsA(SDK::EClassCastFlags::Struct)) {
        Refuse(response, Status::NotFound, "array preview requires a live instance");
        return Progress::Done;
    }
    const Ref before = Identify(object, pack);
    Request read{};
    read.op = reagentlive::op::Read;
    read.address = request.address + request.args[1];
    read.length = sizeof(ArrayHeader);
    reagentlive::Read(read, response, first, host);
    if (response.status != Status::Ok) return Progress::Done;
    ArrayHeader header{};
    memcpy(&header, response.data, sizeof(header));
    response.length = 0;
    if (!header.count && header.capacity >= 0) return Progress::Done;
    read.address = header.data;
    read.length = ArrayPreviewBytes(header, request.length);
    if (!read.length) {
        Refuse(response, Status::Refused, "invalid array header");
        return Progress::Done;
    }
    reagentlive::Read(read, response, first, host);
    if (response.status == Status::Ok && StillSame(before, pack, response)) response.detail = static_cast<uint32_t>(header.count);
    return Progress::Done;
}

Progress EntriesBody(const Request& request, Response& response, bool, const Host& host) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response, Access::Read)) return Progress::Done;
    const auto* object = ResolveRequestObject(request, pack);
    if (!object || object->IsA(SDK::EClassCastFlags::Struct)) {
        Refuse(response, Status::NotFound, "entries require a live instance");
        return Progress::Done;
    }
    const Ref before = Identify(object, pack);
    const uint32_t stride = request.length, first = EntriesFirst(request.args[1]);
    const uint32_t fit = static_cast<uint32_t>(MaxRead / stride);
    const auto* at = reinterpret_cast<const uint8_t*>(request.address) + EntriesOffset(request.args[1]);
    const auto kind = static_cast<ContainerKind>(request.flags);
    response.length = 0;
    if (kind == ContainerKind::Array) {
        ArrayHeader header{};
        if (!host.readable(at, sizeof(header)) || !GuardedCopy(&header, at, sizeof(header))) {
            Refuse(response, Status::NotReadable, "container header unreadable");
            return Progress::Done;
        }
        if (header.count < 0 || header.capacity < header.count || (header.count && !header.data) ||
            static_cast<uint64_t>(header.count) * stride > UINT32_MAX) {
            Refuse(response, Status::Refused, "invalid array header");
            return Progress::Done;
        }
        const uint32_t count = static_cast<uint32_t>(header.count);
        const uint32_t shown = first < count ? std::min(count - first, fit) : 0;
        const auto* data = reinterpret_cast<const uint8_t*>(header.data) + static_cast<uint64_t>(first) * stride;
        // Checked, then copied under the fault guard: another thread can free
        // the storage between the two.
        if (shown && (!host.readable(data, static_cast<size_t>(shown) * stride) ||
                      !GuardedCopy(response.data, data, static_cast<size_t>(shown) * stride))) {
            Refuse(response, Status::NotReadable, "array elements unreadable");
            return Progress::Done;
        }
        response.length = shown * stride;
        response.detail = FindDetail(count, first + shown < count ? first + shown : 0);
        StillSame(before, pack, response);
        return Progress::Done;
    }
    SparseHeader header{};
    if (!host.readable(at, sizeof(header)) || !GuardedCopy(&header, at, sizeof(header))) {
        Refuse(response, Status::NotReadable, "container header unreadable");
        return Progress::Done;
    }
    if (!SparseHeaderValid(header)) { Refuse(response, Status::Refused, "invalid set header"); return Progress::Done; }
    const uint32_t num = static_cast<uint32_t>(header.num), words = (num + 31) / 32;
    // The bits are walked from a guarded copy (the header's own for inline
    // bits), never from live memory. A failed allocation is answered by the
    // agent's dispatch as a refusal.
    std::vector<uint32_t> bitsCopy;
    const uint32_t* bits = header.inlineBits;
    if (header.secondaryBits) {
        const auto* live = reinterpret_cast<const uint32_t*>(header.secondaryBits);
        bitsCopy.resize(words);
        if (words && (!host.readable(live, words * sizeof(uint32_t)) ||
                      !GuardedCopy(bitsCopy.data(), live, words * sizeof(uint32_t)))) {
            Refuse(response, Status::NotReadable, "allocation bits unreadable");
            return Progress::Done;
        }
        bits = bitsCopy.data();
    }
    uint32_t occupied = 0;
    for (uint32_t slot = 0; slot < num; ++slot) occupied += SlotAllocated(bits, slot);
    if (occupied != num - static_cast<uint32_t>(header.numFree)) {
        Refuse(response, Status::Refused, "allocation bits disagree with the free count");
        return Progress::Done;
    }
    const auto* data = reinterpret_cast<const uint8_t*>(header.data);
    bool unreadable = false;
    const uint32_t next = WalkSlots(bits, num, first, fit, [&](uint32_t slot) {
        const auto* element = data + static_cast<uint64_t>(slot) * stride;
        if (unreadable || !host.readable(element, stride) ||
            !GuardedCopy(response.data + response.length, element, stride)) { unreadable = true; return; }
        response.length += stride;
    });
    if (unreadable) {
        response.length = 0;
        Refuse(response, Status::NotReadable, "set elements unreadable");
        return Progress::Done;
    }
    response.detail = FindDetail(occupied, next);
    StillSame(before, pack, response);
    return Progress::Done;
}

Progress NamesBody(const Request& request, Response& response, bool, const Host& host) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response, Access::Read)) return Progress::Done;
    const uint32_t count = request.length / 4;
    size_t used = 0;
    uint32_t done = 0;
    char name[256];
    for (; done < count; ++done) {
        int32_t index;
        memcpy(&index, request.data + done * 4, 4);
        size_t length = 0;
        if (index < 0 || !GuardedName(index, name, sizeof(name), length)) length = 0; // Undecodable: empty.
        if (used + length + 1 > MaxRead) break;
        memcpy(response.data + used, name, length);
        response.data[used + length] = 0;
        used += length + 1;
    }
    response.length = static_cast<uint32_t>(used);
    response.detail = done;
    return Progress::Done;
}

// Off the engine's thread (Context::offThreadReads) these run under a fault guard.
Progress Find(const Request& request, Response& response, bool first, const Host& host) {
    return GuardedRead(&FindBody, request, response, first, host);
}
Progress Describe(const Request& request, Response& response, bool first, const Host& host) {
    return GuardedRead(&DescribeBody, request, response, first, host);
}
Progress DescribeMany(const Request& request, Response& response, bool first, const Host& host) {
    return GuardedRead(&DescribeManyBody, request, response, first, host);
}
Progress ReadObject(const Request& request, Response& response, bool first, const Host& host) {
    return GuardedRead(&ReadObjectBody, request, response, first, host);
}
Progress PreviewArray(const Request& request, Response& response, bool first, const Host& host) {
    return GuardedRead(&PreviewArrayBody, request, response, first, host);
}
Progress Entries(const Request& request, Response& response, bool first, const Host& host) {
    return GuardedRead(&EntriesBody, request, response, first, host);
}
Progress Names(const Request& request, Response& response, bool first, const Host& host) {
    return GuardedRead(&NamesBody, request, response, first, host);
}
}
