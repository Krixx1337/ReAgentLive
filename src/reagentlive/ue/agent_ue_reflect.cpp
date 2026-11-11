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
#include "reagentlive/ue/agent_ue.h"
#include "reagentlive/ue/name_pool.h"
#include "SDK/CoreUObject_classes.hpp"
#include "reagentlive/ue/agent_ue_internal.h"

namespace reagentlive::ue {
bool PlainData(const SDK::FProperty* property, const Host& host) {
    const auto flagsOf = [](const SDK::FProperty* p) { return static_cast<uint64_t>(p->PropertyFlags); };
    // A struct's members, its super structs' included; -1 for anything else.
    const auto membersOf = [&](const SDK::FProperty* p, const auto& visit) -> int {
        if (!host.readable(p->ClassPrivate, sizeof(SDK::FFieldClass)) ||
            View(p->ClassPrivate->Name.ToString()) != "StructProperty" || !host.readable(p, sizeof(SDK::FStructProperty)))
            return -1;
        const auto& pack = Pack(host);
        int count = 0;
        unsigned depth = 0;
        for (const SDK::UStruct* type = static_cast<const SDK::FStructProperty*>(p)->Struct; type;
             type = type->SuperStruct, ++depth) {
            if (depth >= MaxClassDepth || !pack.object(type)) return -1;
            unsigned children = 0;
            const SDK::FField* field = type->ChildProperties;
            for (; field && children < MaxChildren; field = field->Next, ++children) {
                if (!host.readable(field, sizeof(SDK::FProperty))) return -1;
                visit(static_cast<const SDK::FProperty*>(field));
                ++count;
            }
            if (field) return -1;
        }
        return count;
    };
    return PlainProperty(property, flagsOf, membersOf);
}

namespace {
// The struct a StructProperty holds, as a ref the client can pass to Fields.
void DescribeStruct(const SDK::FField* field, Ref& structType, char (&structName)[48], const Context& pack,
                    const Host& host) {
    if (!host.readable(field, sizeof(SDK::FStructProperty))) return;
    const SDK::UStruct* type = static_cast<const SDK::FStructProperty*>(field)->Struct;
    const Ref ref = Identify(type, pack);
    if (!ref.address || !type->IsA(SDK::EClassCastFlags::Struct)) return;
    structType = ref;
    const auto name = type->GetName();
    CopyText(structName, View(name));
}
void DescribeStruct(const SDK::FField* field, FieldEntry& entry, const Context& pack, const Host& host) {
    DescribeStruct(field, entry.structType, entry.structName, pack, host);
}

// A map's key and value or a set's element, and the element layout when it
// passes ContainerLayoutValid (stride stays 0 otherwise).
void DescribeContainer(const SDK::FField* field, bool map, FieldEntry& entry, const Context& pack, const Host& host) {
    const auto* base = reinterpret_cast<const uint8_t*>(field);
    if (!host.readable(base, (map ? MapLayoutOffset : SetLayoutOffset) + sizeof(ContainerLayout))) return;
    const auto inner = [&](const SDK::FProperty* property, char (&type)[32], uint32_t& size, Ref& structType,
                           char (&structName)[48]) {
        if (!host.readable(property, sizeof(SDK::FProperty)) ||
            !host.readable(property->ClassPrivate, sizeof(SDK::FFieldClass)) || property->ElementSize <= 0) return false;
        size = static_cast<uint32_t>(property->ElementSize);
        CopyText(type, View(property->ClassPrivate->Name.ToString()));
        if (std::string_view(type) == "StructProperty") DescribeStruct(property, structType, structName, pack, host);
        return true;
    };
    const SDK::FProperty* key = map ? static_cast<const SDK::FMapProperty*>(field)->KeyProperty
                                    : static_cast<const SDK::FSetProperty*>(field)->ElementProperty;
    if (!inner(key, entry.elementType, entry.elementSize, entry.structType, entry.structName)) return;
    if (map && !inner(static_cast<const SDK::FMapProperty*>(field)->ValueProperty, entry.valueType, entry.valueSize,
                      entry.valueStructType, entry.valueStructName)) return;
    ContainerLayout layout{};
    if (map) memcpy(&layout, base + MapLayoutOffset, sizeof(layout));
    else memcpy(&layout.hashNextIdOffset, base + SetLayoutOffset, 3 * sizeof(int32_t));
    if (!ContainerLayoutValid(layout, entry.elementSize, entry.valueSize)) return;
    entry.stride = static_cast<uint32_t>(layout.stride);
    entry.valueOffset = map ? static_cast<uint32_t>(layout.valueOffset) : 0;
}

// A function declared on an interface that the object's class (or a parent)
// implements, with the interface part's offset in the object.
// `implausible`: some class's array at InterfacesOffset does not look like
// implemented interfaces (another engine layout); nothing is trusted then.
struct InterfaceFunction { SDK::UFunction* function; const SDK::UStruct* declaring; int32_t offset; bool implausible; };
InterfaceFunction FindInterfaceFunction(const SDK::UObject* object, std::string_view owner, std::string_view name,
                                        const Context& pack, const Host& host) {
    if (owner.empty()) return {};
    struct Header { const uint8_t* data; int32_t count, capacity; };
    struct Entry { const SDK::UStruct* type; int32_t offset; };
    // An interface class derives from CoreUObject's "Interface" (the SDK has no UInterface type).
    const auto isInterface = [&](const SDK::UStruct* type) {
        unsigned up = 0;
        for (; type && up < MaxClassDepth && pack.object(type) && HeaderReadable(type); type = type->SuperStruct, ++up) {
            const auto name = type->GetName();
            if (View(name) == "Interface") return true;
        }
        return false;
    };
    unsigned depth = 0;
    for (const SDK::UStruct* type = object->Class; type && depth < MaxClassDepth && pack.object(type);
         type = type->SuperStruct, ++depth) {
        const auto* where = reinterpret_cast<const uint8_t*>(type) + InterfacesOffset;
        Header header{};
        if (!host.readable(where, sizeof(header)) || !GuardedCopy(&header, where, sizeof(header)))
            return {nullptr, nullptr, 0, true};
        if (!header.count && header.capacity >= 0 && header.capacity <= static_cast<int32_t>(MaxInterfaces))
            continue; // Implements nothing itself.
        if (header.count <= 0 || header.count > static_cast<int32_t>(MaxInterfaces) || header.capacity < header.count ||
            !header.data || !host.readable(header.data, static_cast<size_t>(header.count) * InterfaceEntrySize))
            return {nullptr, nullptr, 0, true};
        // Every entry must be an interface class with its part inside this
        // class's size before any is used.
        Entry entries[MaxInterfaces];
        for (int32_t i = 0; i < header.count; ++i) {
            const uint8_t* at = header.data + static_cast<size_t>(i) * InterfaceEntrySize;
            Entry& entry = entries[i];
            const bool plausible = GuardedCopy(&entry.type, at, sizeof(entry.type)) &&
                GuardedCopy(&entry.offset, at + 8, sizeof(entry.offset)) && entry.type && pack.object(entry.type) &&
                HeaderReadable(entry.type) && entry.type->IsA(SDK::EClassCastFlags::Class) &&
                isInterface(entry.type) && entry.offset > 0 && type->Size > 0 &&
                static_cast<int64_t>(entry.offset) + static_cast<int64_t>(sizeof(void*)) <= type->Size;
            if (!plausible) return {nullptr, nullptr, 0, true};
        }
        for (int32_t i = 0; i < header.count; ++i) {
            const SDK::UStruct* declaring = nullptr;
            if (auto* function = FindFunction(entries[i].type, owner, name, pack, &declaring))
                return {function, declaring, entries[i].offset, false};
        }
    }
    return {};
}

KeptResults g_kept; // Owned call results (protocol_ue.h); constant-initialised.
constexpr size_t MaxCallSlots = 64;

// The function's parameter slots; false when the list is unreadable or too long.
bool ParameterSlots(const SDK::UFunction* function, const Host& host, ParameterSlot* slots, size_t& count,
                    bool& owning) {
    count = 0;
    owning = false;
    unsigned children = 0;
    const SDK::FField* field = function->ChildProperties;
    for (; field && children < MaxChildren && host.readable(field, sizeof(SDK::FProperty)); field = field->Next, ++children) {
        const auto* property = static_cast<const SDK::FProperty*>(field);
        const auto propertyFlags = static_cast<uint64_t>(property->PropertyFlags);
        if (!(propertyFlags & flags::Parm)) continue;
        const int64_t size = static_cast<int64_t>(property->ElementSize) * property->ArrayDim;
        if (count == MaxCallSlots || property->Offset < 0 || size < 0 || size > static_cast<int64_t>(MaxParams)) return false;
        slots[count++] = {static_cast<uint32_t>(property->Offset), static_cast<uint32_t>(size), propertyFlags};
        owning |= OwningParameter(propertyFlags, PlainData(property, host));
    }
    return !field;
}
}

Progress Call(const Request& request, Response& response, bool, const Host& host) {
    const Context& pack = Pack(host);
    if (!OnEngineThread(pack, response)) return Progress::Done;
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    SDK::UObject* object = ResolveRequestObject(request, pack);
    if (!object) { Refuse(response, Status::NotFound, "object gone or not a valid object"); return Progress::Done; }
    const auto [owner, name] = SplitFunction(request.text);
    SDK::UFunction* function = FindFunction(object->Class, owner, name, pack);
    if (!function && FindInterfaceFunction(object, owner, name, pack, host).function) {
        // The reason fits MaxReason; why is in protocol_ue.h (InterfacesOffset).
        Refuse(response, Status::Refused, "native interface function: see func for its implementation");
        return Progress::Done;
    }
    if (!function) { Refuse(response, Status::NotFound, "function not found on the object's class chain"); return Progress::Done; }
    if (request.args[1] && request.args[1] != reinterpret_cast<uintptr_t>(function)) {
        Refuse(response, Status::Refused, "function changed since parameter inspection");
        return Progress::Done;
    }
    if (function->Size < 0 || static_cast<uint32_t>(function->Size) != request.length) {
        Refuse(response, Status::BadRequest, "parameter block size differs from the function's",
            static_cast<uint64_t>(function->Size < 0 ? 0 : function->Size));
        return Progress::Done;
    }
    ParameterSlot slots[MaxCallSlots];
    size_t slotCount = 0;
    bool owning = false;
    if (!ParameterSlots(function, host, slots, slotCount, owning)) {
        Refuse(response, Status::Refused, "parameter list unreadable");
        return Progress::Done;
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
    object->ProcessEvent(function, params); // The function's own flags, as ProcessEvent sees them.
    memcpy(response.data, params, request.length);
    response.length = request.length;
    if (kept && release) KeptResults::Drop(*kept);
    else if (kept) {
        memcpy(kept->block, params, request.length);
        response.detail = CallKept;
    }
    return Progress::Done;
}

Progress Function(const Request& request, Response& response, bool, const Host& host) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    const Context& pack = Pack(host);
    if (!OnEngineThread(pack, response)) return Progress::Done;
    const auto* object = ResolveRequestObject(request, pack);
    if (!object) {
        Refuse(response, Status::NotFound, "object gone or not a valid object");
        return Progress::Done;
    }
    const auto* start = object->IsA(SDK::EClassCastFlags::Class)
        ? static_cast<const SDK::UClass*>(object) : object->Class;
    const auto [owner, name] = SplitFunction(request.text);
    const SDK::UStruct* declaring = nullptr;
    const auto* function = FindFunction(start, owner, name, pack, &declaring);
    InterfaceFunction viaInterface{};
    if (!function && start == object->Class) {
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
    if (!host.readable(function, sizeof(SDK::UFunction))) {
        Refuse(response, Status::NotReadable, "function metadata is not readable");
        return Progress::Done;
    }
    FunctionEntry entry{};
    entry.address = reinterpret_cast<uintptr_t>(function);
    entry.exec = reinterpret_cast<uintptr_t>(function->ExecFunction);
    entry.flags = static_cast<uint32_t>(function->FunctionFlags);
    MEMORY_BASIC_INFORMATION page{};
    if (entry.exec && VirtualQuery(reinterpret_cast<const void*>(entry.exec), &page, sizeof(page)) &&
        page.State == MEM_COMMIT && !(page.Protect & PAGE_GUARD)) {
        entry.executable = (page.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
    }
    const auto className = declaring->GetName();
    const auto functionName = function->GetName();
    CopyText(entry.owner, View(className));
    CopyText(entry.name, View(functionName));
    entry.slot = -1;
    if (viaInterface.function) {
        // The slot the stub calls, then that slot of the object's interface vtable.
        entry.interface = 1;
        entry.interfaceOffset = static_cast<uint32_t>(viaInterface.offset);
        uint8_t stub[MaxStubScan];
        if (entry.executable && host.readable(reinterpret_cast<const void*>(entry.exec), sizeof(stub)) &&
            GuardedCopy(stub, reinterpret_cast<const void*>(entry.exec), sizeof(stub)))
            entry.slot = StubVtableSlot(stub, sizeof(stub));
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

Progress Functions(const Request& request, Response& response, bool, const Host& host) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    const Context& pack = Pack(host);
    if (!OnEngineThread(pack, response)) return Progress::Done;
    const auto* object = ResolveRequestObject(request, pack);
    if (!object) {
        Refuse(response, Status::NotFound, "object gone or not a valid object");
        return Progress::Done;
    }
    const SDK::UStruct* type = object->IsA(SDK::EClassCastFlags::Class)
        ? static_cast<const SDK::UClass*>(object) : object->Class;
    auto* entries = reinterpret_cast<FunctionNameEntry*>(response.data);
    uint32_t count = 0, matched = 0;
    for (unsigned depth = 0; type && depth < MaxClassDepth && pack.object(type);
         type = type->SuperStruct, ++depth) {
        const auto owner = type->GetName();
        unsigned children = 0;
        for (const auto* field = type->Children; field && children < MaxChildren && pack.object(field);
             field = field->Next, ++children) {
            if (!field->HasTypeFlag(SDK::EClassCastFlags::Function)) continue;
            const auto name = field->GetName();
            if (request.text[0] && !NameMatches(View(name), request.text)) continue;
            if (matched++ < request.length) continue;
            if (count == MaxFunctionsPerPage) {
                response.detail = matched - 1;
                break;
            }
            CopyText(entries[count].owner, View(owner));
            CopyText(entries[count++].name, View(name));
        }
        if (response.detail) break;
    }
    response.length = count * sizeof(FunctionNameEntry);
    return Progress::Done;
}

// Reflected properties of a class or struct chain, most derived first, one page per request.
Progress Fields(const Request& request, Response& response, bool, const Host& host) {
    const Context& pack = Pack(host);
    if (!OnEngineThread(pack, response)) return Progress::Done;
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    const SDK::UObject* object = ResolveRequestObject(request, pack);
    if (!object) { Refuse(response, Status::NotFound, "object gone or not a valid object"); return Progress::Done; }
    const bool signature = request.op == op::Parameters;
    if (signature && !pack.gameThread()) {
        Refuse(response, Status::Refused, "not on the game thread");
        return Progress::Done;
    }
    if (signature && (!object->IsA(SDK::EClassCastFlags::Function) ||
        !host.readable(object, sizeof(SDK::UFunction)))) {
        Refuse(response, Status::BadRequest, "parameters require a UFunction");
        return Progress::Done;
    }
    const SDK::UStruct* type = object->IsA(SDK::EClassCastFlags::Struct) ?
        static_cast<const SDK::UStruct*>(object) : object->Class;
    auto* entries = reinterpret_cast<FieldEntry*>(response.data);
    uint32_t index = 0, count = 0;
    unsigned depth = 0;
    // Fields: the type's object-table index, so a client cache can tell whose
    // table it is (an instance reports its class). Parameters: the block size.
    const uint32_t blockSize = signature ? static_cast<uint32_t>(type->Size) : static_cast<uint32_t>(type->Index);
    for (; type && depth < MaxClassDepth && pack.object(type); type = signature ? nullptr : type->SuperStruct, ++depth) {
        const auto owner = type->GetName();
        unsigned children = 0;
        const SDK::FField* field = type->ChildProperties;
        for (; field && children < MaxChildren && host.readable(field, sizeof(SDK::FProperty));
             field = field->Next, ++children) {
            const auto* property = static_cast<const SDK::FProperty*>(field); // UE5 child properties.
            using Flags = SDK::EPropertyFlags;
            const auto has = [&](Flags flag) { return (property->PropertyFlags & static_cast<uint64_t>(flag)) != 0; };
            if (signature && !has(Flags::Parm)) continue;
            if (signature && property->Offset < 0) {
                Refuse(response, Status::BadRequest, "invalid parameter offset");
                return Progress::Done;
            }
            if (index++ < request.length || count >= MaxFieldsPerPage) continue;
            FieldEntry& entry = entries[count++];
            entry = {};
            if (signature) {
                entry.reserved[0] = Param;
                if (has(Flags::OutParm)) entry.reserved[0] |= Output;
                if (has(Flags::ReturnParm)) entry.reserved[0] |= Return;
                if (has(Flags::ReferenceParm)) entry.reserved[0] |= Reference;
                if (PlainData(property, host)) entry.reserved[0] |= Plain;
                if (property->ArrayDim == 1) entry.reserved[0] |= Scalar;
            }
            const int64_t size = static_cast<int64_t>(property->ElementSize) * property->ArrayDim;
            entry.offset = property->Offset > 0 ? static_cast<uint32_t>(property->Offset) : 0;
            entry.size = size > 0 && size <= UINT32_MAX ? static_cast<uint32_t>(size) : 0;
            entry.boolMask = 0;
            CopyText(entry.name, View(field->Name.ToString()));
            if (host.readable(field->ClassPrivate, sizeof(SDK::FFieldClass))) {
                CopyText(entry.type, View(field->ClassPrivate->Name.ToString()));
                const bool isBool = (field->ClassPrivate->CastFlags & static_cast<uint64_t>(SDK::EClassCastFlags::BoolProperty)) != 0;
                if (isBool && host.readable(field, sizeof(SDK::FBoolProperty))) {
                    const auto* flag = static_cast<const SDK::FBoolProperty*>(field);
                    entry.offset += flag->ByteOffset;
                    entry.boolMask = flag->FieldMask;
                }
                if (std::string_view(entry.type) == "StructProperty" && property->ArrayDim == 1)
                    DescribeStruct(field, entry, pack, host);
                const std::string_view typeName(entry.type);
                if ((typeName == "MapProperty" || typeName == "SetProperty") && property->ArrayDim == 1 &&
                    host.readable(field, sizeof(SDK::FSetProperty)))
                    DescribeContainer(field, typeName == "MapProperty", entry, pack, host);
                if (std::string_view(entry.type) == "ArrayProperty" && property->ArrayDim == 1 &&
                    host.readable(field, sizeof(SDK::FArrayProperty))) {
                    const auto* inner = static_cast<const SDK::FArrayProperty*>(field)->InnerProperty;
                    if (host.readable(inner, sizeof(SDK::FProperty)) &&
                        host.readable(inner->ClassPrivate, sizeof(SDK::FFieldClass)) && inner->ElementSize > 0) {
                        entry.elementSize = static_cast<uint32_t>(inner->ElementSize);
                        CopyText(entry.elementType, View(inner->ClassPrivate->Name.ToString()));
                        if (std::string_view(entry.elementType) == "StructProperty")
                            DescribeStruct(inner, entry, pack, host);
                    }
                }
            } else {
                CopyText(entry.type, "?");
            }
            CopyText(entry.owner, View(owner));
        }
        if (signature && field) {
            Refuse(response, Status::NotReadable, "incomplete parameter metadata");
            return Progress::Done;
        }
    }
    response.length = count * static_cast<uint32_t>(sizeof(FieldEntry));
    response.detail = FindDetail(index, blockSize);
    return Progress::Done;
}
}
