// reagentlive Unreal pack: object lookup (find, describe, raw object reads,
// names) and the shared helpers with their caches. SDK-free (reagentlive::ue-base):
// objects are read at the located layout's offsets.
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
#include "reagentlive/packs/ue/agent_ue.h"
#include "reagentlive/packs/ue/name_pool.h"
#include "reagentlive/packs/ue/agent_ue_base.h"

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

bool HeaderReadable(const Object* object) {
    const auto address = reinterpret_cast<uintptr_t>(object);
    const size_t size = globals::ObjectHeaderBytes(Layout().shape);
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

// Without a known SuperStruct only the exact class matches.
bool ClassChainMatches(const Object* type, std::string_view wanted, std::string& scratch) {
    MEMORY_BASIC_INFORMATION info{};
    const auto& layout = Layout();
    const size_t bytes = std::max<size_t>(globals::ObjectHeaderBytes(layout.shape),
                                          layout.superStruct < 0 ? 0 : static_cast<size_t>(layout.superStruct) + sizeof(void*));
    for (unsigned depth = 0; type && depth < MaxClassDepth; type = SuperOf(type), ++depth) {
        if (!VirtualQuery(type, &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            reinterpret_cast<uintptr_t>(type) + bytes >
                reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize) return false;
        if (SameText(BaseName(ObjectName(type).index, scratch), wanted)) return true;
    }
    return false;
}
}

// Whether the object's class or a parent class is named `wanted`. Headers only:
// full validation stays for matches.
bool ClassMatches(const Object* object, std::string_view wanted, std::string& scratch) {
    const Object* type = ObjectClass(object);
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

bool OuterMatches(const Request& request, const Object* object, std::string& scratch) {
    const std::string_view text = detail::FilterOuter(request);
    if (!request.address && text.empty()) return true;
    const Object* outer = object;
    for (uint32_t step = 0; step < MaxOuterSteps; ++step) {
        outer = ObjectOuter(outer);
        if (!outer || !HeaderReadable(outer)) return false;
        if (request.address && reinterpret_cast<uintptr_t>(outer) == request.address) return true;
        if (!text.empty() && NameMatches(FullName(ObjectName(outer), scratch), text)) return true;
    }
    return false;
}

bool OuterAlive(const Request& request, const Context& pack, Response& response) {
    if (!request.address) return true;
    Ref ref = detail::FilterRef(request);
    if (ref.index < 0) ref = Identify(reinterpret_cast<const Object*>(ref.address), pack);
    if (Resolve(ref, pack)) return true;
    Refuse(response, Status::NotFound, "outer object gone or not a valid object");
    return false;
}

Ref OuterRef(const Object* object, const Context& pack) {
    const Object* outer = ObjectOuter(object);
    if (!outer || !HeaderReadable(outer) || !pack.object(outer)) return {};
    return Identify(outer, pack);
}

const SDK::UObject* FirstLiveInstance(std::string_view className) {
    if (!Located()) return nullptr;
    try {
        std::string scratch;
        ResetClassCache();
        const int32_t count = TableNum();
        for (int32_t i = 0; i < count; ++i) {
            const Object* object = TableObject(i);
            if (object && HeaderReadable(object) && ObjectIndex(object) == i && !Template(object) &&
                ClassMatches(object, className, scratch)) return object;
        }
    } catch (...) {}
    return nullptr;
}

// Live instances only: class default (RF_ClassDefaultObject, 0x10) and
// archetype (RF_ArchetypeObject, 0x20) objects are templates.
bool Template(const Object* object) {
    constexpr uint32_t templates = 0x10 | 0x20;
    return (ObjectFlags(object) & templates) != 0;
}

// Full name as the engine prints it: base, then _<number - 1> when numbered.
std::string_view FullName(NameId name, std::string& out) {
    std::string scratch;
    const auto base = BaseName(name.index, scratch);
    if (!name.number) return out.assign(base);
    char digits[12];
    const auto end = std::to_chars(digits, digits + sizeof(digits), name.number - 1).ptr;
    out.assign(base).append(1, '_').append(digits, end);
    return out;
}

const Context& Pack(const Host& host) { return *static_cast<const Context*>(host.context); }

// Object-table identity: slot index plus serial (0 = not assigned yet, which
// agrees with any later serial for the same slot and address).
// Without a known serial (no SDK) every serial is 0.
Ref Identify(const Object* object, const Context& pack) {
    if (!object || !pack.object(object)) return {};
    const int32_t index = ObjectIndex(object);
    Object* slot = nullptr;
    int32_t serial = 0;
    if (index < 0 || !TableItem(index, slot, serial) || slot != object || serial < 0) return {};
    return {reinterpret_cast<uintptr_t>(object), index, serial};
}

Object* Resolve(const Ref& ref, const Context& pack) {
    if (!ref.address || ref.index < 0 || ref.serial < 0) return nullptr;
    Object* object = nullptr;
    int32_t serial = 0;
    if (!TableItem(ref.index, object, serial) || reinterpret_cast<uintptr_t>(object) != ref.address || serial < 0 ||
        (ref.serial && ref.serial != serial)) return nullptr;
    if (!pack.object(object)) return nullptr;
    return ObjectIndex(object) == ref.index ? object : nullptr;
}

// Index < 0: address only, identified now. Refs from find keep their serial.
Object* ResolveRequestObject(const Request& request, const Context& pack) {
    Ref ref = GetRef(request);
    if (ref.index < 0) ref = Identify(reinterpret_cast<const Object*>(ref.address), pack);
    return Resolve(ref, pack);
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
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ?
                EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false; // Only bad memory: a stack overflow or a breakpoint is not ours to swallow.
    }
}

Progress FindBody(const Request& request, Response& response, bool first, const Host& host) {
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response, Access::Read)) return Progress::Done;
    const auto from = static_cast<int32_t>(request.args[1]);
    if (first) {
        if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
        if (!OuterAlive(request, pack, response)) return Progress::Done;
        g_find = {};
        g_find.cursor = from; // Later pages start at their cursor instead of rescanning from slot 0.
        ResetClassCache();
    }
    const bool exact = (request.flags & FindExact) != 0;
    const bool live = (request.flags & FindLive) != 0;
    const bool counting = from == 0 && !exact; // Only the first page counts every match.
    const std::string_view query(request.text);
    const std::string_view wantedClass = ClassArgument(request);
    auto* entries = reinterpret_cast<FindEntry*>(response.data);
    Budget budget;
    const int32_t count = TableNum();
    g_region = {};
    std::string name, scratch;
    for (unsigned checked = 0; g_find.cursor < count; ++g_find.cursor, ++checked) {
        if ((checked & 63) == 63 && budget.Spent()) return Progress::Continue;
        // Cheap header check while scanning; full validation only for matches.
        Object* object = TableObject(g_find.cursor);
        if (!object || !HeaderReadable(object) || ObjectIndex(object) != g_find.cursor) continue;
        if (!query.empty() && (exact ? FullName(ObjectName(object), name) != query : !NameMatches(FullName(ObjectName(object), name), query)))
            continue;
        if (!wantedClass.empty() && !ClassMatches(object, wantedClass, scratch)) continue;
        if (live && Template(object)) continue;
        if (!OuterMatches(request, object, scratch)) continue;
        if (!pack.object(object)) continue;
        if (query.empty()) FullName(ObjectName(object), name);
        ++g_find.total;
        if (g_find.count >= MaxFindResults) {
            if (!g_find.next) g_find.next = static_cast<uint32_t>(g_find.cursor);
            if (!counting) break;
            continue;
        }
        FindEntry& entry = entries[g_find.count++];
        entry = {};
        entry.object = Identify(object, pack);
        entry.outer = OuterRef(object, pack);
        CopyText(entry.name, name);
        if (const Object* type = ObjectClass(object); pack.object(type)) {
            CopyText(entry.className, FullName(ObjectName(type), scratch));
        } else {
            CopyText(entry.className, "?");
        }
        if (exact) { // The first match is the answer; the next slot lets a caller look on.
            g_find.next = g_find.cursor + 1 < count ? static_cast<uint32_t>(g_find.cursor + 1) : 0;
            break;
        }
    }
    response.length = static_cast<uint32_t>(g_find.count * sizeof(FindEntry));
    response.detail = FindDetail(counting ? g_find.total : 0, g_find.next);
    return Progress::Done;
}

// The identity a client guards with (protocol_ue.h) is the header's index and class.
static_assert(globals::StockShapes[0].objectIndexOffset == static_cast<uint32_t>(layout::StockUE5.objectIndex) &&
              globals::StockShapes[0].objectClassOffset == static_cast<uint32_t>(layout::StockUE5.objectClass) && ObjectIdentityLength == 4 + sizeof(void*));

namespace {
// One ref (address-only, full, or weak slot and serial) to its entry; false when gone or not an object.
// `classSize`: the size of the object's class (0 when unknown), which covers
// native members past the last reflected field.
bool DescribeRef(Ref ref, const Context& pack, FindEntry& entry, uint32_t* classSize = nullptr) {
    if (!ref.address) {
        if (ref.index < 0 || ref.serial <= 0) return false;
        Object* object = nullptr;
        int32_t serial = 0;
        if (TableItem(ref.index, object, serial) && serial == ref.serial) ref.address = reinterpret_cast<uintptr_t>(object);
    } else if (ref.index < 0) {
        ref = Identify(reinterpret_cast<const Object*>(ref.address), pack);
    }
    const auto* object = Resolve(ref, pack);
    if (!object) return false;
    entry = {};
    entry.object = Identify(object, pack);
    entry.outer = OuterRef(object, pack);
    std::string name;
    CopyText(entry.name, FullName(ObjectName(object), name));
    if (const Object* type = ObjectClass(object); pack.object(type)) {
        CopyText(entry.className, FullName(ObjectName(type), name));
        if (const int32_t sizeAt = Layout().structSize; classSize && sizeAt >= 0) {
            if (const auto size = Field<int32_t>(type, static_cast<uint32_t>(sizeAt)); size > 0) *classSize = static_cast<uint32_t>(size);
        }
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

Progress NamesBody(const Request& request, Response& response, bool, const Host& host) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response, Access::Read)) return Progress::Done;
    const uint32_t count = request.length / 4 < MaxNames ? request.length / 4 : MaxNames;
    int32_t indices[MaxNames];
    bool starts[MaxNames];
    memcpy(indices, request.data, count * 4);
    NameStarts(indices, count, starts, host);
    size_t used = 0;
    uint32_t done = 0;
    char name[256];
    for (; done < count; ++done) {
        const int32_t index = indices[done];
        size_t length = 0;
        // Not an entry start or undecodable: empty. Decoding a slot inside an
        // entry would read its text as a header.
        if (!starts[done] || !GuardedName(index, name, sizeof(name), length)) length = 0;
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
Progress Names(const Request& request, Response& response, bool first, const Host& host) {
    return GuardedRead(&NamesBody, request, response, first, host);
}
}
