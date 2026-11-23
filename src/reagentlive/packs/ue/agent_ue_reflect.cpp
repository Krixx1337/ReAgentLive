// reagentlive Unreal pack: reflection and calls: call, func, funcs, fields and parameters.
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
#include "reagentlive/packs/ue/agent_ue_internal.h"

namespace reagentlive::ue {
namespace {
bool SameName(const Object* object, std::string_view expected) {
    std::string name;
    return FullName(ObjectName(object), name) == expected;
}
}

const Object* FindFunction(const Object* start, std::string_view owner, std::string_view name,
                           const Context& pack, const Object** declaring) {
    unsigned depth = 0;
    for (const Object* type = start; type && depth < MaxClassDepth && pack.object(type);
         type = SuperStruct(type), ++depth) {
        if (!owner.empty() && !SameName(type, owner)) continue;
        unsigned children = 0;
        for (const Object* field = FirstChild(type); field && children < MaxChildren && pack.object(field);
             field = NextChild(field), ++children)
            if (IsA(field, cast::Function) && SameName(field, name)) {
                if (declaring) *declaring = type;
                return field;
            }
    }
    return nullptr;
}

std::string FieldTypeName(FieldPtr field, const Host& host) {
    const FieldClassPtr type = FieldClass(field);
    if (!host.readable(type, FieldClassBytes())) return {};
    return NameOf(FieldClassName(type));
}

bool PlainData(FieldPtr property, const Host& host) {
    const auto flagsOf = [](FieldPtr p) { return PropertyFlags(p); };
    // A struct's members, its super structs' included; -1 for anything else.
    const auto membersOf = [&](FieldPtr p, const auto& visit) -> int {
        if (FieldTypeName(p, host) != "StructProperty" || !host.readable(p, TypedBytes())) return -1;
        const auto& pack = Pack(host);
        int count = 0;
        unsigned depth = 0;
        for (const Object* type = StructOf(p); type; type = SuperStruct(type), ++depth) {
            if (depth >= MaxClassDepth || !pack.object(type)) return -1;
            unsigned children = 0;
            FieldPtr field = ChildProperties(type);
            for (; field && children < MaxChildren; field = FieldNext(field), ++children) {
                if (!host.readable(field, PropertyBytes())) return -1;
                visit(field);
                ++count;
            }
            if (field) return -1;
        }
        return count;
    };
    return PlainProperty(property, flagsOf, membersOf);
}

namespace {
// One property as it goes on a page: its numbers, struct refs and full names.
struct FieldText {
    FieldRecord numbers{};
    Ref structType{}, valueStructType{};
    std::string name, type, owner, elementType, structName, valueType, valueStructName, subtype, valueSubtype;
    // The record's subtype: a map's is "Key|Value".
    FieldStrings Strings() const {
        if (!valueSubtype.empty()) wire = subtype + "|" + valueSubtype;
        else wire = subtype;
        return {name, type, owner, elementType, structName, valueType, valueStructName, wire};
    }
    mutable std::string wire;
};

// The struct a StructProperty holds, as a ref the client can pass to Fields.
void DescribeStruct(FieldPtr field, Ref& structType, std::string& structName, const Context& pack,
                    const Host& host) {
    if (!host.readable(field, TypedBytes())) return;
    const Object* type = StructOf(field);
    const Ref ref = Identify(type, pack);
    if (!ref.address || !IsA(type, cast::Struct)) return;
    structType = ref;
    structName = NameOf(ObjectName(type));
}

// The class an object-like property points to, or the enum an enum or byte
// property holds, by name (left empty when there is none or it is not one).
void DescribeSubtype(FieldPtr field, std::string_view type, std::string& out, const Context& pack, const Host& host) {
    const bool object = type.ends_with("ObjectProperty") || type.ends_with("ClassProperty") || type == "InterfaceProperty";
    const bool enumerated = type == "EnumProperty" || type == "ByteProperty";
    if ((!object && !enumerated) || !host.readable(field, TypedBytes())) return;
    const Object* target = object ? PropertyClassOf(field) : type == "EnumProperty" ? EnumPropertyEnum(field) : BytePropertyEnum(field);
    if (!target || !Identify(target, pack).address || !IsA(target, object ? cast::Class : cast::Enum)) return;
    out = NameOf(ObjectName(target));
}

// A map's key and value or a set's element, and the element layout when it
// passes ContainerLayoutValid (stride stays 0 otherwise).
void DescribeContainer(FieldPtr field, bool map, FieldText& entry, const Context& pack, const Host& host) {
    const auto* base = reinterpret_cast<const uint8_t*>(field);
    if (!host.readable(base, static_cast<uint32_t>(map ? Engine().mapLayout : Engine().setLayout) + sizeof(ContainerLayout)) ||
        !host.readable(field, TypedBytes())) return;
    const auto inner = [&](FieldPtr property, std::string& type, uint32_t& size, Ref& structType,
                           std::string& structName, std::string& subtype) {
        if (!host.readable(property, PropertyBytes()) || ElementSize(property) <= 0) return false;
        type = FieldTypeName(property, host);
        if (type.empty()) return false;
        size = static_cast<uint32_t>(ElementSize(property));
        if (type == "StructProperty") DescribeStruct(property, structType, structName, pack, host);
        DescribeSubtype(property, type, subtype, pack, host);
        return true;
    };
    const FieldPtr key = map ? MapKey(field) : SetElement(field);
    if (!inner(key, entry.elementType, entry.numbers.elementSize, entry.structType, entry.structName, entry.subtype)) return;
    if (map && !inner(MapValue(field), entry.valueType, entry.numbers.valueSize,
                      entry.valueStructType, entry.valueStructName, entry.valueSubtype)) return;
    ContainerLayout layout{};
    if (map) memcpy(&layout, base + Engine().mapLayout, sizeof(layout));
    else memcpy(&layout.hashNextIdOffset, base + Engine().setLayout, 3 * sizeof(int32_t));
    if (!ContainerLayoutValid(layout, entry.numbers.elementSize, entry.numbers.valueSize)) return;
    entry.numbers.stride = static_cast<uint32_t>(layout.stride);
    entry.numbers.valueOffset = map ? static_cast<uint32_t>(layout.valueOffset) : 0;
}

// Each native interface `start` or a parent implements, from the class's
// TArray at the layout's interfaces of {UClass*, int32 PointerOffset, bool}. Every
// class's array is checked before any entry is used: an interface class (a
// UInterface subclass) whose part lies inside the implementing class's size.
// `visit(interface, offset)` returns false to stop. Returns false when some
// array does not look like implemented interfaces (another engine layout);
// nothing is trusted then.
constexpr unsigned MaxSeenInterfaces = 64;
template<class Visit>
bool ForEachInterface(const Object* start, const Context& pack, const Host& host, Visit&& visit) {
    struct Header { const uint8_t* data; int32_t count, capacity; };
    struct Entry { const Object* type; int32_t offset; };
    // An interface class derives from CoreUObject's "Interface".
    const auto isInterface = [&](const Object* type) {
        unsigned up = 0;
        for (; type && up < MaxClassDepth && pack.object(type) && HeaderReadable(type); type = SuperStruct(type), ++up) {
            const auto name = NameOf(ObjectName(type));
            if (View(name) == "Interface") return true;
        }
        return false;
    };
    // A parent's interfaces can repeat in a child's array: visit each once.
    const Object* seen[MaxSeenInterfaces]{};
    unsigned seenCount = 0;
    unsigned depth = 0;
    for (const Object* type = start; type && depth < MaxClassDepth && pack.object(type);
         type = SuperStruct(type), ++depth) {
        const auto* where = reinterpret_cast<const uint8_t*>(type) + Engine().interfaces;
        Header header{};
        if (!host.readable(where, sizeof(header)) || !GuardedCopy(&header, where, sizeof(header))) return false;
        if (!header.count && header.capacity >= 0 && header.capacity <= static_cast<int32_t>(MaxInterfaces))
            continue; // Implements nothing itself.
        if (header.count <= 0 || header.count > static_cast<int32_t>(MaxInterfaces) || header.capacity < header.count ||
            !header.data || !host.readable(header.data, static_cast<size_t>(header.count) * InterfaceEntrySize))
            return false;
        Entry entries[MaxInterfaces];
        for (int32_t i = 0; i < header.count; ++i) {
            const uint8_t* at = header.data + static_cast<size_t>(i) * InterfaceEntrySize;
            Entry& entry = entries[i];
            const bool plausible = GuardedCopy(&entry.type, at, sizeof(entry.type)) &&
                GuardedCopy(&entry.offset, at + 8, sizeof(entry.offset)) && entry.type && pack.object(entry.type) &&
                HeaderReadable(entry.type) && IsA(entry.type, cast::Class) &&
                isInterface(entry.type) && entry.offset > 0 && StructSize(type) > 0 &&
                static_cast<int64_t>(entry.offset) + static_cast<int64_t>(sizeof(void*)) <= StructSize(type);
            if (!plausible) return false;
        }
        for (int32_t i = 0; i < header.count; ++i) {
            bool repeated = false;
            for (unsigned j = 0; j < seenCount; ++j) repeated |= seen[j] == entries[i].type;
            if (repeated) continue;
            if (seenCount < MaxSeenInterfaces) seen[seenCount++] = entries[i].type;
            if (!visit(entries[i].type, entries[i].offset)) return true;
        }
    }
    return true;
}

// A function declared on an interface that the object's class (or a parent)
// implements, with the interface part's offset in the object.
// `implausible`: some class's array at the layout's interfaces does not look like
// implemented interfaces (another engine layout); nothing is trusted then.
struct InterfaceFunction { const Object* function; const Object* declaring; int32_t offset; bool implausible; };
InterfaceFunction FindInterfaceFunction(const Object* object, std::string_view owner, std::string_view name,
                                        const Context& pack, const Host& host) {
    if (owner.empty()) return {};
    InterfaceFunction found{};
    const bool plausible = ForEachInterface(ObjectClass(object), pack, host, [&](const Object* type, int32_t offset) {
        const Object* declaring = nullptr;
        if (const Object* function = FindFunction(type, owner, name, pack, &declaring)) {
            found = {function, declaring, offset, false};
            return false;
        }
        return true;
    });
    if (!plausible) return {nullptr, nullptr, 0, true};
    return found;
}

// Bytes of the function starting at `exec`, from its module's exception
// directory (the module is the allocation holding `exec`); 0 when unknown.
uint32_t StubSpan(uint64_t exec, const Host& host) {
    MEMORY_BASIC_INFORMATION page{};
    if (!exec || !VirtualQuery(reinterpret_cast<const void*>(exec), &page, sizeof(page)) || !page.AllocationBase) return 0;
    const auto base = reinterpret_cast<uintptr_t>(page.AllocationBase);
    IMAGE_DOS_HEADER dos{};
    if (!host.readable(page.AllocationBase, sizeof(dos)) || !GuardedCopy(&dos, page.AllocationBase, sizeof(dos)) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000) return 0;
    IMAGE_NT_HEADERS64 nt{};
    const auto* where = reinterpret_cast<const void*>(base + static_cast<uintptr_t>(dos.e_lfanew));
    if (!host.readable(where, sizeof(nt)) || !GuardedCopy(&nt, where, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return 0;
    const auto& directory = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!directory.VirtualAddress || !directory.Size || directory.Size % 12 ||
        directory.VirtualAddress > nt.OptionalHeader.SizeOfImage ||
        directory.Size > nt.OptionalHeader.SizeOfImage - directory.VirtualAddress || exec - base > UINT32_MAX) return 0;
    const auto* entries = reinterpret_cast<const uint32_t*>(base + directory.VirtualAddress);
    if (!host.readable(entries, directory.Size)) return 0;
    return FunctionSpan(entries, directory.Size / 12, static_cast<uint32_t>(exec - base));
}

KeptResults g_kept; // Owned call results (protocol_ue.h); constant-initialised.
constexpr size_t MaxCallSlots = 64;

// The function's parameter slots; false when the list is unreadable or too long.
bool ParameterSlots(const Object* function, const Host& host, ParameterSlot* slots, size_t& count,
                    bool& owning) {
    count = 0;
    owning = false;
    unsigned children = 0;
    FieldPtr field = ChildProperties(function);
    for (; field && children < MaxChildren && host.readable(field, PropertyBytes()); field = FieldNext(field), ++children) {
        const uint64_t propertyFlags = PropertyFlags(field);
        if (!(propertyFlags & flags::Parm)) continue;
        const int64_t size = static_cast<int64_t>(ElementSize(field)) * ArrayDim(field);
        const int32_t offset = PropertyOffset(field);
        if (count == MaxCallSlots || offset < 0 || size < 0 || size > static_cast<int64_t>(MaxParams)) return false;
        slots[count++] = {static_cast<uint32_t>(offset), static_cast<uint32_t>(size), propertyFlags};
        owning |= OwningParameter(propertyFlags, PlainData(field, host));
    }
    return !field;
}
}

// No objects that unwind in this frame: SEH and C++ unwinding do not mix.
__declspec(noinline) bool GuardedProcessEvent(Object* object, const Object* function, void* params) {
    using ProcessEvent = void (*)(Object*, const Object*, void*);
    __try {
        const auto* vtable = *reinterpret_cast<ProcessEvent* const*>(object);
        vtable[Engine().processEventSlot](object, function, params);
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ?
                EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

Progress Call(const Request& request, Response& response, bool, const Host& host) {
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response) || !LayoutReady(GroupsCall, host, response)) return Progress::Done;
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    Object* object = ResolveRequestObject(request, pack);
    if (!object) { Refuse(response, Status::NotFound, "object gone or not a valid object"); return Progress::Done; }
    const auto [owner, name] = SplitFunction(request.text);
    const Object* function = FindFunction(ObjectClass(object), owner, name, pack);
    if (!function && FindInterfaceFunction(object, owner, name, pack, host).function) {
        // The reason fits MaxReason; why is in protocol_ue.h (Interfaces).
        Refuse(response, Status::Refused, "native interface function: see func for its implementation");
        return Progress::Done;
    }
    if (!function) { Refuse(response, Status::NotFound, "function not found on the object's class chain"); return Progress::Done; }
    if (request.args[1] && request.args[1] != reinterpret_cast<uintptr_t>(function)) {
        Refuse(response, Status::Refused, "function changed since parameter inspection");
        return Progress::Done;
    }
    const int32_t functionSize = StructSize(function);
    if (functionSize < 0 || static_cast<uint32_t>(functionSize) != request.length) {
        Refuse(response, Status::BadRequest, "parameter block size differs from the function's",
            static_cast<uint64_t>(functionSize < 0 ? 0 : functionSize));
        return Progress::Done;
    }
    ParameterSlot slots[MaxCallSlots];
    size_t slotCount = 0;
    bool owning = false;
    if (!ParameterSlots(function, host, slots, slotCount, owning)) {
        Refuse(response, Status::Refused, "parameter list unreadable");
        return Progress::Done;
    }
    if (request.flags & CallPure) {
        if (!EachFunctionAllowed(FunctionFlags(function))) {
            Refuse(response, Status::Refused, "not a const or pure function: use call");
            return Progress::Done;
        }
        if (owning) {
            Refuse(response, Status::Refused, "an output owns memory: use call");
            return Progress::Done;
        }
    }
    const bool release = (request.flags & CallRelease) != 0;
    const auto key = reinterpret_cast<uintptr_t>(function);
    KeptResult* kept = owning ? g_kept.Find(key) : nullptr;
    if (kept && kept->size != request.length) { // Same address, another function: the old block is not ours to carry.
        KeptResults::Drop(*kept);
        kept = nullptr;
    }
    if (release && !kept) { Refuse(response, Status::NotFound, "no kept result for this function"); return Progress::Done; }
    const bool fresh = owning && !kept;
    if (fresh && !(kept = g_kept.Add(key, request.length))) {
        Refuse(response, Status::Refused, "result table full; release one");
        return Progress::Done;
    }
    alignas(16) uint8_t params[MaxParams];
    memcpy(params, request.data, request.length);
    if (kept && !fresh) CarryOutputs(slots, slotCount, kept->block, params, request.length);
    // The function's own flags, as ProcessEvent sees them.
    if (!GuardedProcessEvent(object, function, params)) {
        if (fresh) KeptResults::Drop(*kept); // Nothing was kept; an older kept block stays as it was.
        Refuse(response, Status::Faulted, "call faulted: the target may be in a bad state", reinterpret_cast<uintptr_t>(object));
        return Progress::Done;
    }
    memcpy(response.data, params, request.length);
    response.length = request.length;
    if (kept && release) KeptResults::Drop(*kept);
    else if (kept) {
        memcpy(kept->block, params, request.length);
        response.detail = CallKept;
    }
    return Progress::Done;
}

namespace {
Progress FunctionBody(const Request& request, Response& response, bool, const Host& host) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response, Access::Read) || !LayoutReady(GroupsFunctions, host, response))
        return Progress::Done;
    const auto* object = ResolveRequestObject(request, pack);
    if (!object) {
        Refuse(response, Status::NotFound, "object gone or not a valid object");
        return Progress::Done;
    }
    const Object* start = IsA(object, cast::Class) ? object : ObjectClass(object);
    const auto [owner, name] = SplitFunction(request.text);
    const Object* declaring = nullptr;
    const Object* function = FindFunction(start, owner, name, pack, &declaring);
    InterfaceFunction viaInterface{};
    if (!function && start == ObjectClass(object)) {
        viaInterface = FindInterfaceFunction(object, owner, name, pack, host);
        if (viaInterface.implausible) {
            Refuse(response, Status::Refused, "interfaces array implausible: engine layout differs");
            return Progress::Done;
        }
        function = viaInterface.function;
        declaring = viaInterface.declaring;
    }
    if (!function) {
        Refuse(response, Status::NotFound, "function not found on the class chain or its interfaces");
        return Progress::Done;
    }
    if (!host.readable(function, FunctionBytes())) {
        Refuse(response, Status::NotReadable, "function metadata is not readable");
        return Progress::Done;
    }
    FunctionEntry entry{};
    entry.address = reinterpret_cast<uintptr_t>(function);
    entry.exec = ExecFunction(function);
    entry.flags = FunctionFlags(function);
    MEMORY_BASIC_INFORMATION page{};
    if (entry.exec && VirtualQuery(reinterpret_cast<const void*>(entry.exec), &page, sizeof(page)) &&
        page.State == MEM_COMMIT && !(page.Protect & PAGE_GUARD)) {
        entry.executable = (page.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
    }
    const auto className = NameOf(ObjectName(declaring));
    const auto functionName = NameOf(ObjectName(function));
    CopyText(entry.owner, View(className));
    CopyText(entry.name, View(functionName));
    entry.slot = -1;
    if (viaInterface.function) {
        // The slot the stub calls, then that slot of the object's interface vtable.
        entry.interface = 1;
        entry.interfaceOffset = static_cast<uint32_t>(viaInterface.offset);
        uint8_t stub[MaxStubScan];
        const uint32_t span = StubSpan(entry.exec, host);
        const uint32_t scan = span < MaxStubScan ? span : MaxStubScan;
        if (entry.executable && scan && host.readable(reinterpret_cast<const void*>(entry.exec), scan) &&
            GuardedCopy(stub, reinterpret_cast<const void*>(entry.exec), scan))
            entry.slot = StubVtableSlot(stub, scan);
        const auto* part = reinterpret_cast<const uint8_t*>(object) + viaInterface.offset;
        uint64_t vtable = 0, implementation = 0;
        if (entry.slot >= 0 && host.readable(part, sizeof(vtable)) && GuardedCopy(&vtable, part, sizeof(vtable)) &&
            vtable && host.readable(reinterpret_cast<const void*>(vtable + entry.slot), sizeof(implementation)) &&
            GuardedCopy(&implementation, reinterpret_cast<const void*>(vtable + entry.slot), sizeof(implementation)))
            entry.implementation = implementation;
    }
    memcpy(response.data, &entry, sizeof(entry));
    response.length = sizeof(entry);
    return Progress::Done;
}

Progress FunctionsBody(const Request& request, Response& response, bool, const Host& host) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response, Access::Read) || !LayoutReady(GroupsFunctions, host, response))
        return Progress::Done;
    const auto* object = ResolveRequestObject(request, pack);
    if (!object) {
        Refuse(response, Status::NotFound, "object gone or not a valid object");
        return Progress::Done;
    }
    const Object* start = IsA(object, cast::Class) ? object : ObjectClass(object);
    auto* entries = reinterpret_cast<FunctionNameEntry*>(response.data);
    uint32_t count = 0, matched = 0;
    const Object* type = start;
    for (unsigned depth = 0; type && depth < MaxClassDepth && pack.object(type);
         type = SuperStruct(type), ++depth) {
        const auto owner = NameOf(ObjectName(type));
        unsigned children = 0;
        for (const Object* field = FirstChild(type); field && children < MaxChildren && pack.object(field);
             field = NextChild(field), ++children) {
            if (!IsA(field, cast::Function)) continue;
            const auto name = NameOf(ObjectName(field));
            if (request.text[0] && !NameMatches(View(name), request.text)) continue;
            if (matched++ < request.length) continue;
            if (count == MaxFunctionsPerPage) {
                response.detail = matched - 1;
                break;
            }
            entries[count] = {};
            CopyText(entries[count].owner, View(owner));
            CopyText(entries[count++].name, View(name));
        }
        if (response.detail) break;
    }
    // Then the functions of each native interface a class implements: only on
    // the interface class, so absent from the chain above. An unreadable
    // interface array lists nothing more (func and call explain it).
    if (!response.detail && IsA(start, cast::Class)) {
        ForEachInterface(start, pack, host, [&](const Object* type, int32_t) {
            const auto owner = NameOf(ObjectName(type));
            unsigned children = 0;
            for (const Object* field = FirstChild(type); field && children < MaxChildren && pack.object(field);
                 field = NextChild(field), ++children) {
                if (!IsA(field, cast::Function)) continue;
                const auto name = NameOf(ObjectName(field));
                if (request.text[0] && !NameMatches(View(name), request.text)) continue;
                if (matched++ < request.length) continue;
                if (count == MaxFunctionsPerPage) {
                    response.detail = matched - 1;
                    return false;
                }
                entries[count] = {};
                entries[count].interface = 1;
                CopyText(entries[count].owner, View(owner));
                CopyText(entries[count++].name, View(name));
            }
            return true;
        });
    }
    response.length = count * sizeof(FunctionNameEntry);
    return Progress::Done;
}

// Reflected properties of a class or struct chain, most derived first, one page per request.
Progress FieldsBody(const Request& request, Response& response, bool, const Host& host) {
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response, Access::Read) || !LayoutReady(GroupsFields, host, response))
        return Progress::Done;
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    const Object* object = ResolveRequestObject(request, pack);
    if (!object) { Refuse(response, Status::NotFound, "object gone or not a valid object"); return Progress::Done; }
    const Ref before = Identify(object, pack);
    const bool signature = request.op == op::Parameters;
    if (signature && !pack.gameThread()) {
        Refuse(response, Status::Refused, "not on the game thread");
        return Progress::Done;
    }
    if (signature && (!IsA(object, cast::Function) || !host.readable(object, FunctionBytes()))) {
        Refuse(response, Status::BadRequest, "parameters require a UFunction");
        return Progress::Done;
    }
    const Object* type = TypeOf(object);
    FieldsPageWriter page(response.data);
    bool full = false;
    uint32_t index = 0;
    unsigned depth = 0;
    // Fields: the type's object-table index, so a client cache can tell whose
    // table it is (an instance reports its class). Parameters: the block size.
    const uint32_t blockSize = signature ? static_cast<uint32_t>(StructSize(type)) : static_cast<uint32_t>(ObjectIndex(type));
    for (; type && depth < MaxClassDepth && pack.object(type); type = signature ? nullptr : SuperStruct(type), ++depth) {
        const auto owner = NameOf(ObjectName(type));
        unsigned children = 0;
        FieldPtr field = ChildProperties(type);
        for (; field && children < MaxChildren && host.readable(field, PropertyBytes());
             field = FieldNext(field), ++children) {
            const uint64_t propertyFlags = PropertyFlags(field);
            const auto has = [&](uint64_t flag) { return (propertyFlags & flag) != 0; };
            const int32_t arrayDim = ArrayDim(field), offset = PropertyOffset(field);
            if (signature && !has(flags::Parm)) continue;
            if (signature && offset < 0) {
                Refuse(response, Status::BadRequest, "invalid parameter offset");
                return Progress::Done;
            }
            // Past this page's start; once a field did not fit, the rest are only counted.
            if (index++ < request.length || full) continue;
            FieldText entry;
            FieldRecord& numbers = entry.numbers;
            if (signature) {
                numbers.flags = Param;
                if (has(flags::OutParm)) numbers.flags |= Output;
                if (has(flags::ReturnParm)) numbers.flags |= Return;
                if (has(property_flags::ReferenceParm)) numbers.flags |= Reference;
                if (has(flags::ConstParm)) numbers.flags |= Const;
                if (PlainData(field, host)) numbers.flags |= Plain;
                if (arrayDim == 1) numbers.flags |= Scalar;
            }
            const int64_t size = static_cast<int64_t>(ElementSize(field)) * arrayDim;
            numbers.offset = offset > 0 ? static_cast<uint32_t>(offset) : 0;
            numbers.size = size > 0 && size <= UINT32_MAX ? static_cast<uint32_t>(size) : 0;
            entry.name = NameOf(FieldName(field));
            const FieldClassPtr fieldClass = FieldClass(field);
            if (host.readable(fieldClass, FieldClassBytes())) {
                entry.type = NameOf(FieldClassName(fieldClass));
                const bool isBool = (FieldClassCastFlags(fieldClass) & cast::BoolProperty) != 0;
                const bool typed = host.readable(field, TypedBytes());
                if (isBool && typed) {
                    numbers.offset += BoolByteOffset(field);
                    numbers.boolMask = BoolFieldMask(field);
                }
                if (entry.type == "StructProperty" && arrayDim == 1)
                    DescribeStruct(field, entry.structType, entry.structName, pack, host);
                DescribeSubtype(field, entry.type, entry.subtype, pack, host);
                if ((entry.type == "MapProperty" || entry.type == "SetProperty") && arrayDim == 1 && typed)
                    DescribeContainer(field, entry.type == "MapProperty", entry, pack, host);
                if (entry.type == "ArrayProperty" && arrayDim == 1 && typed) {
                    const FieldPtr inner = ArrayInner(field);
                    if (host.readable(inner, PropertyBytes()) && ElementSize(inner) > 0) {
                        entry.elementType = FieldTypeName(inner, host);
                        if (!entry.elementType.empty()) numbers.elementSize = static_cast<uint32_t>(ElementSize(inner));
                        if (entry.elementType == "StructProperty")
                            DescribeStruct(inner, entry.structType, entry.structName, pack, host);
                        DescribeSubtype(inner, entry.elementType, entry.subtype, pack, host);
                    }
                }
            } else {
                entry.type = "?";
            }
            entry.owner = owner;
            if (page.Add(numbers, entry.structType, entry.valueStructType, entry.Strings())) continue;
            if (!page.Count()) {
                Refuse(response, Status::Refused, "a field's names do not fit one page");
                return Progress::Done;
            }
            full = true; // The next page starts at this field.
        }
        if (signature && field) {
            Refuse(response, Status::NotReadable, "incomplete parameter metadata");
            return Progress::Done;
        }
    }
    response.length = page.Finish();
    response.detail = FindDetail(index, blockSize);
    StillSame(before, pack, response);
    return Progress::Done;
}
}

// An enum's names and values, one page from args[1]. Two layouts: the
// stock TArray of {FName, int64} pairs at UEnum::Names, or two arrays and a
// count (UE 5.6 and later; a Dumper-7 SDK may still declare the pairs):
// FName* and int64*, bit 0 a dynamic-allocation tag, at Names - 8 and Names, the int32 count at
// Names + 8. A layout is used only when its array is plausible and every
// name in it decodes in the pool.
bool EnumNamesDecode(const EnumLayout& layout) {
    for (uint32_t i = 0; i < layout.count; ++i) {
        int32_t index = 0;
        char text[256];
        size_t length = 0;
        if (!GuardedCopy(&index, layout.names + size_t{i} * layout.nameStride, sizeof(index)) ||
            !GuardedName(index, text, sizeof(text), length) || !length) return false;
    }
    return true;
}
bool FindEnumLayout(const Object* object, EnumLayout& out) {
    const auto* base = reinterpret_cast<const uint8_t*>(object);
    const int32_t namesOffset = Engine().enumNames;
    if (namesOffset < 8) return false;
    // A pair is the FName, then the int64 at the next 8-byte boundary.
    const size_t valueOffset = (NameBytes() + 7) & ~size_t{7}, pairSize = valueOffset + sizeof(int64_t);
    ArrayHeader pairs{};
    if (!GuardedCopy(&pairs, base + namesOffset, sizeof(pairs))) return false;
    if (pairs.count >= 0 && static_cast<uint32_t>(pairs.count) <= MaxEnumEntries && pairs.capacity >= pairs.count &&
        (!pairs.count || pairs.data)) {
        const auto* data = reinterpret_cast<const uint8_t*>(pairs.data);
        out = {data, data + valueOffset, pairSize, pairSize, static_cast<uint32_t>(pairs.count)};
        if (EnumNamesDecode(out)) return true;
    }
    uint64_t names = 0, values = 0;
    int32_t count = 0;
    if (!GuardedCopy(&names, base + namesOffset - 8, sizeof(names)) ||
        !GuardedCopy(&values, base + namesOffset, sizeof(values)) ||
        !GuardedCopy(&count, base + namesOffset + 8, sizeof(count))) return false;
    names &= ~uint64_t{1};
    values &= ~uint64_t{1};
    if (count < 0 || static_cast<uint32_t>(count) > MaxEnumEntries ||
        (count && (!names || !values || (names & 7) || (values & 7)))) return false;
    out = {reinterpret_cast<const uint8_t*>(names), reinterpret_cast<const uint8_t*>(values), NameBytes(),
           sizeof(int64_t), static_cast<uint32_t>(count)};
    return EnumNamesDecode(out);
}

namespace {
Progress EnumBody(const Request& request, Response& response, bool, const Host& host) {
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response, Access::Read) || !LayoutReady(GroupClass | GroupEnum, host, response))
        return Progress::Done;
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    const Object* object = ResolveRequestObject(request, pack);
    if (!object) { Refuse(response, Status::NotFound, "object gone or not a valid object"); return Progress::Done; }
    if (!IsA(object, cast::Enum) || !host.readable(object, EnumBytes())) {
        Refuse(response, Status::BadRequest, "not an enum");
        return Progress::Done;
    }
    const Ref before = Identify(object, pack);
    EnumLayout layout{};
    if (!FindEnumLayout(object, layout)) {
        Refuse(response, Status::Refused, "the enum's names match no known layout");
        return Progress::Done;
    }
    const uint32_t numberOffset = Layout().shape.nameNumberOffset;
    const uint32_t total = layout.count;
    const uint32_t first = request.args[1] < total ? static_cast<uint32_t>(request.args[1]) : total;
    const uint32_t shown = static_cast<uint32_t>(std::min<size_t>(total - first, MaxEnumPerPage));
    auto* entries = reinterpret_cast<EnumEntry*>(response.data);
    for (uint32_t i = 0; i < shown; ++i) {
        EnumEntry& entry = entries[i];
        const uint8_t* name = layout.names + size_t{first + i} * layout.nameStride;
        if (!GuardedCopy(&entry.name, name, sizeof(entry.name)) ||
            !GuardedCopy(&entry.number, name + numberOffset, sizeof(entry.number)) ||
            !GuardedCopy(&entry.value, layout.values + size_t{first + i} * layout.valueStride, sizeof(entry.value))) {
            Refuse(response, Status::NotReadable, "the enum's names are unreadable");
            return Progress::Done;
        }
    }
    response.length = shown * static_cast<uint32_t>(sizeof(EnumEntry));
    response.detail = FindDetail(total, first + shown < total ? first + shown : 0);
    StillSame(before, pack, response);
    return Progress::Done;
}
}

// Off the engine's thread (Context::offThreadReads) these run under a fault guard.
Progress Function(const Request& request, Response& response, bool first, const Host& host) {
    return GuardedRead(&FunctionBody, request, response, first, host);
}
Progress Functions(const Request& request, Response& response, bool first, const Host& host) {
    return GuardedRead(&FunctionsBody, request, response, first, host);
}
Progress Fields(const Request& request, Response& response, bool first, const Host& host) {
    return GuardedRead(&FieldsBody, request, response, first, host);
}
Progress Enum(const Request& request, Response& response, bool first, const Host& host) {
    return GuardedRead(&EnumBody, request, response, first, host);
}
}
