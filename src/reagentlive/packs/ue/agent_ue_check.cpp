// reagentlive Unreal pack: the engine layout check. Without a host's SDK the
// pack reads with StockUE5 (engine_layout.h); before the first op that needs
// a layout group, each group is checked once on objects every UE5 game has
// (CoreUObject's own classes, Vector, Guid, Actor, KismetMathLibrary,
// ENetRole). A group that fails turns its ops off with the reason; nothing
// reads through an unchecked group. With a host's SDK every group is on.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstring>
#include <string>
#include <string_view>
#include "reagentlive/packs/ue/agent_ue.h"
#include "reagentlive/packs/ue/agent_ue_internal.h"

namespace reagentlive::ue {
namespace {
struct GroupResult {
    Group group;
    const char* problem; // nullptr: on.
};
struct CheckState {
    bool done = false;
    GroupResult results[8]{};
    unsigned count = 0;
};
CheckState g_check;

// The objects the checks read, by name and class name.
struct Known {
    const Object* object = nullptr, *structClass = nullptr, *field = nullptr, *classClass = nullptr;
    const Object* vector = nullptr, *guid = nullptr, *actor = nullptr, *math = nullptr, *netRole = nullptr;
    const Object* defaultObject = nullptr;
};

template<class T> bool Get(const void* base, int32_t offset, T& out) {
    return base && offset >= 0 && CheckedCopy(reinterpret_cast<const uint8_t*>(base) + offset, &out, sizeof(out));
}
bool Readable(const void* address, uint32_t bytes) {
    uint8_t probe[512];
    for (uint32_t at = 0; at < bytes; at += sizeof(probe))
        if (!CheckedCopy(reinterpret_cast<const uint8_t*>(address) + at, probe, std::min<uint32_t>(sizeof(probe), bytes - at)))
            return false;
    return address != nullptr;
}

// One pass over the table; the core classes come first, so it stops early.
Known FindKnown() {
    Known known;
    std::string name, className;
    const int32_t count = TableNum();
    unsigned missing = 10;
    for (int32_t i = 0; i < count && missing; ++i) {
        const Object* object = TableObject(i);
        if (!object || !HeaderReadable(object)) continue;
        const Object* type = ObjectClass(object);
        if (!type || !HeaderReadable(type)) continue;
        const auto text = FullName(ObjectName(object), name);
        const auto typeText = FullName(ObjectName(type), className);
        const auto take = [&](const Object*& slot, std::string_view wantName, std::string_view wantClass) {
            if (!slot && text == wantName && typeText == wantClass) {
                slot = object;
                --missing;
            }
        };
        take(known.object, "Object", "Class");
        take(known.structClass, "Struct", "Class");
        take(known.field, "Field", "Class");
        take(known.classClass, "Class", "Class");
        take(known.vector, "Vector", "ScriptStruct");
        take(known.guid, "Guid", "ScriptStruct");
        take(known.actor, "Actor", "Class");
        take(known.math, "KismetMathLibrary", "Class");
        take(known.netRole, "ENetRole", "Enum");
        take(known.defaultObject, "Default__Object", "Object");
    }
    return known;
}

std::string NameText(NameId id) { return NameOf(id); }

const char* CheckClass(const Known& k) {
    if (!k.object || !k.structClass || !k.field || !k.classClass) return "class: core classes not found";
    uint64_t object = 0, field = 0, structFlags = 0, classFlags = 0;
    const int32_t at = Engine().castFlags;
    if (!Get(k.object, at, object) || !Get(k.field, at, field) || !Get(k.structClass, at, structFlags) ||
        !Get(k.classClass, at, classFlags)) return "class: CastFlags unreadable";
    constexpr uint64_t Core = cast::Field | cast::Struct | cast::Class;
    if ((classFlags & Core) != Core || (structFlags & Core) != (cast::Field | cast::Struct) ||
        (field & Core) != cast::Field || (object & Core) != 0) return "class: CastFlags do not match Class/Struct/Field";
    return nullptr;
}

const char* CheckStruct(const Known& k) {
    if (!k.object || !k.structClass || !k.field || !k.classClass) return "struct: core classes not found";
    const int32_t super = Engine().superStruct;
    const Object *a = nullptr, *b = nullptr, *c = nullptr, *d = reinterpret_cast<const Object*>(1);
    if (!Get(k.classClass, super, a) || !Get(k.structClass, super, b) || !Get(k.field, super, c) || !Get(k.object, super, d))
        return "struct: SuperStruct unreadable";
    if (a != k.structClass || b != k.field || c != k.object || d) return "struct: SuperStruct chain is not Class>Struct>Field";
    int32_t guid = 0, vector = 0;
    if (!k.guid || !k.vector || !Get(k.guid, Engine().structSize, guid) || !Get(k.vector, Engine().structSize, vector))
        return "struct: Guid/Vector not found";
    if (guid != 16 || (vector != 12 && vector != 24)) return "struct: Size wrong on Guid or Vector";
    return nullptr;
}

// A struct's own properties, at most `max`; false when the chain is unreadable.
bool Members(const Object* type, FieldPtr* out, unsigned max, unsigned& count) {
    count = 0;
    FieldPtr field = nullptr;
    if (!Get(type, Engine().childProperties, field)) return false;
    for (; field; field = FieldNext(field)) {
        if (count == max || !Readable(field, PropertyBytes())) return false;
        out[count++] = field;
    }
    return true;
}

const char* CheckField(const Known& k) {
    if (!k.vector) return "field: Vector not found";
    FieldPtr members[4];
    unsigned count = 0;
    if (!Members(k.vector, members, 4, count) || count != 3) return "field: Vector does not have 3 properties";
    constexpr std::string_view Names[] = {"X", "Y", "Z"};
    for (unsigned i = 0; i < 3; ++i) {
        if (NameText(FieldName(members[i])) != Names[i]) return "field: Vector members are not X, Y, Z";
        const FieldClassPtr type = FieldClass(members[i]);
        if (!Readable(type, FieldClassBytes())) return "field: FFieldClass unreadable";
        const auto typeName = NameText(FieldClassName(type));
        if (typeName != "DoubleProperty" && typeName != "FloatProperty") return "field: Vector.X class is not a float type";
    }
    return nullptr;
}

const char* CheckProperty(const Known& k) {
    if (!k.vector || !k.guid) return "property: Vector/Guid not found";
    FieldPtr vector[4], guid[5];
    unsigned vectorCount = 0, guidCount = 0;
    if (!Members(k.vector, vector, 4, vectorCount) || vectorCount != 3 || !Members(k.guid, guid, 5, guidCount) ||
        guidCount != 4) return "property: Vector/Guid members unreadable";
    const int32_t element = ElementSize(vector[0]);
    if (element != 4 && element != 8) return "property: ElementSize wrong on Vector.X";
    constexpr uint64_t Plain = 0x40000000 | 0x200; // IsPlainOldData | ZeroConstructor.
    for (unsigned i = 0; i < 3; ++i)
        if (ElementSize(vector[i]) != element || PropertyOffset(vector[i]) != static_cast<int32_t>(i) * element ||
            ArrayDim(vector[i]) != 1 || (PropertyFlags(vector[i]) & Plain) != Plain)
            return "property: Vector members' Offset/ArrayDim/flags wrong";
    for (unsigned i = 0; i < 4; ++i)
        if (ElementSize(guid[i]) != 4 || PropertyOffset(guid[i]) != static_cast<int32_t>(i) * 4 || ArrayDim(guid[i]) != 1 ||
            (PropertyFlags(guid[i]) & Plain) != Plain)
            return "property: Guid members' Offset/ArrayDim/flags wrong";
    const FieldClassPtr intClass = FieldClass(guid[0]);
    if (!Readable(intClass, FieldClassBytes()) ||
        (FieldClassCastFlags(intClass) & (cast::Field | cast::Property | cast::IntProperty)) !=
            (cast::Field | cast::Property | cast::IntProperty))
        return "property: FFieldClass CastFlags wrong on IntProperty";
    return nullptr;
}

const char* CheckTyped(const Known& k) {
    if (!k.actor) return "typed: Actor not found";
    FieldPtr members[512];
    unsigned count = 0;
    if (!Members(k.actor, members, 512, count)) return "typed: Actor properties unreadable";
    unsigned bools = 0, structs = 0, arrays = 0;
    for (unsigned i = 0; i < count; ++i) {
        const FieldPtr field = members[i];
        const FieldClassPtr type = FieldClass(field);
        if (!Readable(type, FieldClassBytes()) || !Readable(field, TypedBytes())) return "typed: a property is unreadable";
        const auto typeName = NameText(FieldClassName(type));
        if (FieldClassCastFlags(type) & cast::BoolProperty) {
            uint8_t bytes[4];
            if (!Get(field, Engine().boolBase, bytes)) return "typed: bool bytes unreadable";
            const uint8_t byteMask = bytes[2], fieldMask = bytes[3];
            const bool single = byteMask && !(byteMask & (byteMask - 1));
            if (bytes[0] != 1 || bytes[1] >= 8 || !single || (fieldMask != byteMask && fieldMask != 0xFF))
                return "typed: FBoolProperty bytes wrong on an Actor bool";
            ++bools;
        } else if (typeName == "StructProperty") {
            const Object* inner = StructOf(field);
            if (!inner || !HeaderReadable(inner) || !Readable(inner, StructBytes()) ||
                NameText(ObjectName(ObjectClass(inner))) != "ScriptStruct")
                return "typed: FStructProperty.Struct is not a ScriptStruct";
            ++structs;
        } else if (typeName == "ArrayProperty") {
            const FieldPtr inner = ArrayInner(field);
            if (!Readable(inner, PropertyBytes()) || !Readable(FieldClass(inner), FieldClassBytes()) ||
                !NameText(FieldClassName(FieldClass(inner))).ends_with("Property"))
                return "typed: FArrayProperty.Inner is not a property";
            ++arrays;
        }
    }
    if (bools < 2 || !structs || !arrays) return "typed: Actor lacks bools, structs or arrays";
    return nullptr;
}

const char* CheckFunction(const Known& k, const Context& pack) {
    if (!k.math) return "function: KismetMathLibrary not found";
    const Object* function = FindFunction(k.math, {}, "Add_IntInt", pack);
    if (!function || !Readable(function, FunctionBytes())) return "function: Add_IntInt not found";
    uint16_t parms = 0, returnOffset = 0;
    if (!Get(function, Engine().parmsSize, parms) || !Get(function, Engine().returnValueOffset, returnOffset))
        return "function: Add_IntInt unreadable";
    if (!(FunctionFlags(function) & flags::FunctionNative)) return "function: FunctionFlags lack Native on Add_IntInt";
    if (StructSize(function) != 12 || parms != 12 || returnOffset != 8)
        return "function: ParmsSize/ReturnValueOffset wrong on Add_IntInt";
    MEMORY_BASIC_INFORMATION page{};
    const auto exec = reinterpret_cast<const void*>(ExecFunction(function));
    constexpr DWORD Execute = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!exec || !VirtualQuery(exec, &page, sizeof(page)) || page.State != MEM_COMMIT || !(page.Protect & Execute))
        return "function: ExecFunction is not code";
    return nullptr;
}

const char* CheckEnum(const Known& k) {
    if (!k.netRole) return "enum: ENetRole not found";
    EnumLayout layout{};
    if (!FindEnumLayout(k.netRole, layout) || layout.count != 5) return "enum: ENetRole names do not decode";
    int32_t index = 0;
    char text[64];
    size_t length = 0;
    if (!GuardedCopy(&index, layout.names, sizeof(index)) || !GuardedName(index, text, sizeof(text), length) ||
        std::string_view(text, length) != "ROLE_None") return "enum: ENetRole's first name is not ROLE_None";
    return nullptr;
}

// ProcessEvent's slot: the function there tests FunctionFlags against
// FUNC_Native early (test dword ptr [reg+FunctionFlags], 0x400).
const char* CheckCall(const Known& k) {
    if (!k.defaultObject) return "call: Default__Object not found";
    const uint8_t* vtable = nullptr;
    const uint8_t* function = nullptr;
    if (!Get(k.defaultObject, 0, vtable) || !Get(vtable, Engine().processEventSlot * 8, function))
        return "call: vtable unreadable";
    uint8_t code[0x80];
    if (!GuardedCopy(code, function, sizeof(code))) return "call: ProcessEvent slot unreadable";
    for (size_t i = 0; i + 10 <= sizeof(code); ++i) {
        const uint8_t modrm = code[i + 1];
        if (code[i] != 0xF7 || (modrm & 0xF8) != 0x80 || (modrm & 7) == 4) continue;
        int32_t disp = 0;
        uint32_t imm = 0;
        memcpy(&disp, code + i + 2, 4);
        memcpy(&imm, code + i + 6, 4);
        if (disp == Engine().functionFlags && imm == flags::FunctionNative) return nullptr;
    }
    return "call: ProcessEvent slot does not test FUNC_Native";
}

void RunChecks(const Context& pack) {
    const Known known = FindKnown();
    auto& state = g_check;
    state.count = 0;
    const auto put = [&](Group group, const char* problem) { state.results[state.count++] = {group, problem}; };
    put(GroupClass, CheckClass(known));
    put(GroupStruct, CheckStruct(known));
    put(GroupField, CheckField(known));
    put(GroupProperty, CheckProperty(known));
    put(GroupTyped, CheckTyped(known));
    put(GroupFunction, CheckFunction(known, pack));
    put(GroupEnum, CheckEnum(known));
    put(GroupCall, CheckCall(known));
    state.done = true;
}
}

bool LayoutReady(uint32_t groups, const Host& host, Response& response) {
#ifdef REAGENTLIVE_UE_SDK
    (void)groups, (void)host, (void)response;
    return true; // The SDK's layout: every group is on.
#else
    if (!g_check.done) RunChecks(Pack(host));
    // A group's check reads the groups before it (fields before typed...), so
    // report the first failure among those asked.
    for (unsigned i = 0; i < g_check.count; ++i) {
        const auto& result = g_check.results[i];
        if ((groups & result.group) && result.problem) {
            Refuse(response, Status::Refused, result.problem);
            return false;
        }
    }
    return true;
#endif
}
}
