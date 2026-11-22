// reagentlive Unreal pack: helpers the reflection files share (reflection,
// calls, containers, script, each). Internal; hosts use agent_ue.h. SDK-free:
// every engine type is read at an offset of the engine layout (Engine(),
// engine_layout.h), so these files build with or without a host's SDK.
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
#include "reagentlive/packs/ue/agent_ue_base.h"

namespace reagentlive::ue {
// UE's class cast flags (EClassCastFlags), stable across UE5.
namespace cast {
constexpr uint64_t Field = 0x1, Enum = 0x4, Struct = 0x8, ScriptStruct = 0x10, Class = 0x20, IntProperty = 0x80,
    Property = 0x8000, BoolProperty = 0x20000, Function = 0x80000;
}
// UE's property flags the handlers test (EPropertyFlags), besides protocol_ue.h's.
namespace property_flags {
constexpr uint64_t ReferenceParm = 0x8000000;
}

// An FField (FProperty) and an FFieldClass: opaque, read through the layout.
struct FFieldTag;
struct FFieldClassTag;
using FieldPtr = const FFieldTag*;
using FieldClassPtr = const FFieldClassTag*;

template<class T, class P> T At(const P* base, int32_t offset) {
    T value;
    memcpy(&value, reinterpret_cast<const uint8_t*>(base) + offset, sizeof(value));
    return value;
}
inline NameId NameAt(const void* base, int32_t offset) {
    return {At<int32_t>(base, offset), At<uint32_t>(base, offset + Engine().nameNumber)};
}

// Bytes a reader must be able to read before using each kind of engine object.
inline uint32_t Span(std::initializer_list<int32_t> ends) {
    int32_t most = 0;
    for (const int32_t end : ends) most = std::max(most, end);
    return static_cast<uint32_t>(most);
}
inline uint32_t StructBytes() {
    const auto& e = Engine();
    return Span({e.superStruct + 8, e.children + 8, e.childProperties + 8, e.structSize + 4, e.script + 16});
}
inline uint32_t PropertyBytes() {
    const auto& e = Engine();
    return Span({e.fieldClass + 8, e.fieldNext + 8, e.fieldName + 8, e.arrayDim + 4, e.elementSize + 4,
                 e.propertyFlags + 8, e.propertyOffset + 4});
}
inline uint32_t TypedBytes() {
    const auto& e = Engine();
    return std::max(PropertyBytes(), Span({e.boolBase + 4, e.structStruct + 8, e.arrayInner + 8, e.setElement + 8,
                                           e.mapKey + 8, e.mapValue + 8}));
}
inline uint32_t FieldClassBytes() {
    const auto& e = Engine();
    return Span({e.fieldClassName + 8, e.fieldClassCastFlags + 8});
}
inline uint32_t FunctionBytes() {
    const auto& e = Engine();
    return std::max(StructBytes(), Span({e.functionFlags + 4, e.parmsSize + 2, e.returnValueOffset + 2,
                                         e.execFunction + 8}));
}
inline uint32_t EnumBytes() { return static_cast<uint32_t>(Engine().enumNames + 12); }
// An FName's bytes (index and number).
inline uint32_t NameBytes() { return static_cast<uint32_t>(Engine().nameNumber + 4); }

// UStruct, UClass, UField. The object must be readable (StructBytes).
inline const Object* SuperStruct(const Object* type) { return At<const Object*>(type, Engine().superStruct); }
inline const Object* FirstChild(const Object* type) { return At<const Object*>(type, Engine().children); }
inline const Object* NextChild(const Object* field) { return At<const Object*>(field, Engine().uFieldNext); }
inline FieldPtr ChildProperties(const Object* type) { return At<FieldPtr>(type, Engine().childProperties); }
inline int32_t StructSize(const Object* type) { return At<int32_t>(type, Engine().structSize); }
inline uint64_t ClassCastFlags(const Object* type) { return At<uint64_t>(type, Engine().castFlags); }
// The object's class has every one of `flags` (UObject::IsA by cast flags).
inline bool IsA(const Object* object, uint64_t flags) {
    const Object* type = ObjectClass(object);
    return type && (ClassCastFlags(type) & flags) != 0;
}
// The struct an instance's fields come from: itself for a struct, else its class.
inline const Object* TypeOf(const Object* object) { return IsA(object, cast::Struct) ? object : ObjectClass(object); }

// FField and FProperty. The field must be readable (PropertyBytes).
inline FieldPtr FieldNext(FieldPtr field) { return At<FieldPtr>(field, Engine().fieldNext); }
inline NameId FieldName(FieldPtr field) { return NameAt(field, Engine().fieldName); }
inline FieldClassPtr FieldClass(FieldPtr field) { return At<FieldClassPtr>(field, Engine().fieldClass); }
inline int32_t ArrayDim(FieldPtr field) { return At<int32_t>(field, Engine().arrayDim); }
inline int32_t ElementSize(FieldPtr field) { return At<int32_t>(field, Engine().elementSize); }
inline uint64_t PropertyFlags(FieldPtr field) { return At<uint64_t>(field, Engine().propertyFlags); }
inline int32_t PropertyOffset(FieldPtr field) { return At<int32_t>(field, Engine().propertyOffset); }
// Typed properties: readable to TypedBytes.
inline uint8_t BoolByteOffset(FieldPtr field) { return At<uint8_t>(field, Engine().boolBase + 1); }
inline uint8_t BoolFieldMask(FieldPtr field) { return At<uint8_t>(field, Engine().boolBase + 3); }
inline const Object* StructOf(FieldPtr field) { return At<const Object*>(field, Engine().structStruct); }
inline FieldPtr ArrayInner(FieldPtr field) { return At<FieldPtr>(field, Engine().arrayInner); }
inline FieldPtr SetElement(FieldPtr field) { return At<FieldPtr>(field, Engine().setElement); }
inline FieldPtr MapKey(FieldPtr field) { return At<FieldPtr>(field, Engine().mapKey); }
inline FieldPtr MapValue(FieldPtr field) { return At<FieldPtr>(field, Engine().mapValue); }
// FFieldClass: readable to FieldClassBytes.
inline NameId FieldClassName(FieldClassPtr type) { return NameAt(type, Engine().fieldClassName); }
inline uint64_t FieldClassCastFlags(FieldClassPtr type) { return At<uint64_t>(type, Engine().fieldClassCastFlags); }

// UFunction: readable to FunctionBytes.
inline uint32_t FunctionFlags(const Object* function) { return At<uint32_t>(function, Engine().functionFlags); }
inline uint64_t ExecFunction(const Object* function) { return At<uint64_t>(function, Engine().execFunction); }
inline uint64_t* ExecFunctionSlot(Object* function) {
    return reinterpret_cast<uint64_t*>(reinterpret_cast<uint8_t*>(function) + Engine().execFunction);
}

// The engine layout groups an op needs (layout::Members' groups), checked once
// on known objects after the pack is ready (agent_ue_check.cpp). With a
// host's SDK every group is on.
enum Group : uint32_t {
    GroupStruct = 1, GroupClass = 2, GroupField = 4, GroupProperty = 8, GroupTyped = 16, GroupFunction = 32,
    GroupEnum = 64, GroupCall = 128,
};
constexpr uint32_t GroupsFields = GroupStruct | GroupClass | GroupField | GroupProperty | GroupTyped;
constexpr uint32_t GroupsFunctions = GroupStruct | GroupClass | GroupFunction;
constexpr uint32_t GroupsCall = GroupsFields | GroupFunction | GroupCall;
// False (and the response refused with the first failed group's check) when
// any group in `groups` is off. Call after PackReady.
bool LayoutReady(uint32_t groups, const Host& host, Response& response);

const Object* FindFunction(const Object* start, std::string_view owner, std::string_view name,
                           const Context& pack, const Object** declaring = nullptr);
// Plain data by the property's flags or, where it lacks one, by its struct's members (protocol_ue.h).
bool PlainData(FieldPtr property, const Host& host);
// ProcessEvent (the layout's vtable slot) under a fault guard: false when the
// call raised an access fault (the engine may be left in a bad state).
bool GuardedProcessEvent(Object* object, const Object* function, void* params);
// An enum's name and value arrays (UEnum::Names: pairs, or two arrays since
// UE 5.6), found and checked on the object.
struct EnumLayout {
    const uint8_t* names;
    const uint8_t* values;
    size_t nameStride, valueStride;
    uint32_t count;
};
bool FindEnumLayout(const Object* object, EnumLayout& out);
// A field's type name, or empty when its class is unreadable.
std::string FieldTypeName(FieldPtr field, const Host& host);
}
