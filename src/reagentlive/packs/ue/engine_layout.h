// reagentlive Unreal pack: the one home of every engine layout offset the pack
// reads, the part that can differ between Unreal games. Pure (no SDK, no
// Windows): the agent, the client and the tests include it. A member is a byte
// offset (a vtable slot for processEventSlot), -1 when unknown.
//
// Where a layout comes from: a host's Dumper-7 SDK (SdkLayout() in
// engine_layout_sdk.h, from offsetof) or StockUE5 below. Members the SDK does
// not describe (interfaces, container layouts, Script, the frame) are StockUE5
// in both. No other pack file defines an engine offset (the scatter test).
// The client uses StockUE5 until the agent reports its layout at attach.
#pragma once
#include <stdint.h>
#include <string>
#include <string_view>

namespace reagentlive::ue::layout {
struct EngineLayout {
    // object: the UObject header and FName (also in globals::TableShape, the
    // object table's shape, which the pack locates at run time).
    int32_t objectFlags, objectIndex, objectClass, objectName, objectOuter, nameNumber;
    // struct: UStruct.
    int32_t superStruct, children, childProperties, structSize, script;
    // class: UClass. interfaces: TArray of {UClass*, int32 PointerOffset, bool}.
    int32_t castFlags, defaultObject, interfaces;
    // field: FField and FFieldClass; UField::Next for the UFunction chain.
    // FField::Owner is an FFieldVariant: one pointer, bit 0 set when it is a UObject.
    int32_t fieldClass, fieldOwner, fieldNext, fieldName, fieldClassName, fieldClassCastFlags, uFieldNext;
    // property: FProperty.
    int32_t arrayDim, elementSize, propertyFlags, propertyOffset;
    // typed: FBoolProperty's FieldSize, ByteOffset, ByteMask, FieldMask bytes
    // start at boolBase; one pointer for struct, array inner, set element and
    // map key/value; FScriptSetLayout and FScriptMapLayout after them; the
    // class an object property points to (FObjectPropertyBase::PropertyClass,
    // FInterfaceProperty::InterfaceClass at the same place) and the enum of
    // an enum or byte property.
    int32_t boolBase, structStruct, arrayInner, setElement, mapKey, mapValue, setLayout, mapLayout;
    int32_t propertyClass, enumPropertyEnum, bytePropertyEnum;
    // function: UFunction.
    int32_t functionFlags, parmsSize, returnValueOffset, execFunction;
    // enum: UEnum::Names. Before UE 5.6 a TArray of {FName, int64} pairs here;
    // since, the int64 value array here, the FName array 8 before and the
    // int32 count 8 after. The agent tells them apart per enum.
    int32_t enumNames;
    // call: UObject::ProcessEvent's vtable slot; FFrame::Node and ::Locals.
    int32_t processEventSlot, frameNode, frameLocals;
    // datatable: UDataTable::RowMap (the client finds it after RowStruct).
    int32_t rowMap;
    // curvetable: UCurveTable's unreflected TMap<FName, FRealCurve*> and its
    // ECurveTableMode byte (1 simple, 2 rich); FSimpleCurve's InterpMode and
    // Keys, FRichCurve's Keys (TArrays of CurveKeySize bytes each).
    int32_t curveRowMap, curveTableMode, simpleCurveInterp, simpleCurveKeys, richCurveKeys;
};

// The layout of stock UE 5.x as this pack has read it live (UE 5.7,
// FField-based, 8-byte FName, two-array enums). A game's profile is checked
// on known objects, never assumed from this.
inline constexpr EngineLayout StockUE5 = {
    .objectFlags = 0x08, .objectIndex = 0x0C, .objectClass = 0x10, .objectName = 0x18, .objectOuter = 0x20,
    .nameNumber = 0x04,
    .superStruct = 0x40, .children = 0x48, .childProperties = 0x50, .structSize = 0x58, .script = 0x60,
    .castFlags = 0xD8, .defaultObject = 0x110, .interfaces = 0x1E0,
    .fieldClass = 0x08, .fieldOwner = 0x10, .fieldNext = 0x18, .fieldName = 0x20, .fieldClassName = 0x00, .fieldClassCastFlags = 0x18,
    .uFieldNext = 0x28,
    .arrayDim = 0x30, .elementSize = 0x34, .propertyFlags = 0x38, .propertyOffset = 0x44,
    .boolBase = 0x70, .structStruct = 0x70, .arrayInner = 0x78, .setElement = 0x70, .mapKey = 0x70, .mapValue = 0x78,
    .setLayout = 0x78, .mapLayout = 0x80,
    .propertyClass = 0x70, .enumPropertyEnum = 0x78, .bytePropertyEnum = 0x70,
    .functionFlags = 0xB0, .parmsSize = 0xB6, .returnValueOffset = 0xB8, .execFunction = 0xD8,
    .enumNames = 0x48,
    .processEventSlot = 0x4C, .frameNode = 0x10, .frameLocals = 0x28,
    .rowMap = 0x30,
    .curveRowMap = 0x30, .curveTableMode = 0x98, .simpleCurveInterp = 0x70, .simpleCurveKeys = 0x78,
    .richCurveKeys = 0x70,
};

// The game viewport client's FViewportClient part and Draw's entry in its
// vtable (destructor, Precache, RedrawRequested, RequestInvalidateHitProxy,
// Draw): host_draw.h ticks there. Not a profile member: the host confirms it
// per game before hooking it.
inline constexpr uint32_t ViewportInterfaceOffset = 0x28, ViewportDrawIndex = 4;

// The interface array's entry; an object's identity guard (index, then class).
inline constexpr uint32_t InterfaceEntrySize = 16, ObjectIdentityLength = 12;

// Curve keys: FSimpleCurveKey {Time, Value}; FRichCurveKey {InterpMode,
// TangentMode, TangentWeightMode, pad, Time, Value, ArriveTangent,
// ArriveTangentWeight, LeaveTangent, LeaveTangentWeight}.
inline constexpr uint32_t SimpleCurveKeySize = 8, RichCurveKeySize = 0x1C;

// Profile groups: each turns on the ops that need it (engine_layout.h's
// members, in dependency order).
struct Member {
    std::string_view group, name;
    int32_t EngineLayout::*field;
};
inline constexpr Member Members[] = {
    {"object", "Flags", &EngineLayout::objectFlags},
    {"object", "Index", &EngineLayout::objectIndex},
    {"object", "Class", &EngineLayout::objectClass},
    {"object", "Name", &EngineLayout::objectName},
    {"object", "Outer", &EngineLayout::objectOuter},
    {"object", "FName.Number", &EngineLayout::nameNumber},
    {"struct", "SuperStruct", &EngineLayout::superStruct},
    {"struct", "Children", &EngineLayout::children},
    {"struct", "ChildProperties", &EngineLayout::childProperties},
    {"struct", "Size", &EngineLayout::structSize},
    {"struct", "Script", &EngineLayout::script},
    {"class", "CastFlags", &EngineLayout::castFlags},
    {"class", "ClassDefaultObject", &EngineLayout::defaultObject},
    {"interface", "Interfaces", &EngineLayout::interfaces},
    {"field", "FField.ClassPrivate", &EngineLayout::fieldClass},
    {"field", "FField.Owner", &EngineLayout::fieldOwner},
    {"field", "FField.Next", &EngineLayout::fieldNext},
    {"field", "FField.Name", &EngineLayout::fieldName},
    {"field", "FFieldClass.Name", &EngineLayout::fieldClassName},
    {"field", "FFieldClass.CastFlags", &EngineLayout::fieldClassCastFlags},
    {"field", "UField.Next", &EngineLayout::uFieldNext},
    {"property", "ArrayDim", &EngineLayout::arrayDim},
    {"property", "ElementSize", &EngineLayout::elementSize},
    {"property", "PropertyFlags", &EngineLayout::propertyFlags},
    {"property", "Offset", &EngineLayout::propertyOffset},
    {"typed", "FBoolProperty.FieldSize", &EngineLayout::boolBase},
    {"typed", "FStructProperty.Struct", &EngineLayout::structStruct},
    {"typed", "FArrayProperty.Inner", &EngineLayout::arrayInner},
    {"typed", "FSetProperty.ElementProp", &EngineLayout::setElement},
    {"typed", "FMapProperty.KeyProp", &EngineLayout::mapKey},
    {"typed", "FMapProperty.ValueProp", &EngineLayout::mapValue},
    {"typed", "FSetProperty.SetLayout", &EngineLayout::setLayout},
    {"typed", "FMapProperty.MapLayout", &EngineLayout::mapLayout},
    {"typed", "FObjectPropertyBase.PropertyClass", &EngineLayout::propertyClass},
    {"typed", "FEnumProperty.Enum", &EngineLayout::enumPropertyEnum},
    {"typed", "FByteProperty.Enum", &EngineLayout::bytePropertyEnum},
    {"function", "FunctionFlags", &EngineLayout::functionFlags},
    {"function", "ParmsSize", &EngineLayout::parmsSize},
    {"function", "ReturnValueOffset", &EngineLayout::returnValueOffset},
    {"function", "ExecFunction", &EngineLayout::execFunction},
    {"enum", "Names", &EngineLayout::enumNames},
    {"call", "ProcessEventSlot", &EngineLayout::processEventSlot},
    {"call", "FFrame.Node", &EngineLayout::frameNode},
    {"call", "FFrame.Locals", &EngineLayout::frameLocals},
    {"datatable", "RowMap", &EngineLayout::rowMap},
    {"curvetable", "RowMap", &EngineLayout::curveRowMap},
    {"curvetable", "CurveTableMode", &EngineLayout::curveTableMode},
    {"curvetable", "FSimpleCurve.InterpMode", &EngineLayout::simpleCurveInterp},
    {"curvetable", "FSimpleCurve.Keys", &EngineLayout::simpleCurveKeys},
    {"curvetable", "FRichCurve.Keys", &EngineLayout::richCurveKeys},
};

// The layout as a profile: {"groups": {group: {member: "0x..", ...}, ...}},
// unknown members left out. Members in Members order, one group per line.
inline std::string ProfileJson(const EngineLayout& layout) {
    constexpr char Digits[] = "0123456789abcdef";
    std::string out = "{\n  \"groups\": {";
    std::string_view group;
    bool firstGroup = true, firstMember = true;
    for (const auto& member : Members) {
        const int32_t value = layout.*member.field;
        if (value < 0) continue;
        if (member.group != group) {
            if (!firstGroup) out += "},";
            out += "\n    \"";
            out += member.group;
            out += "\": {";
            group = member.group;
            firstGroup = false;
            firstMember = true;
        }
        if (!firstMember) out += ", ";
        firstMember = false;
        out += '"';
        out += member.name;
        out += "\": \"0x";
        char hex[8];
        int length = 0;
        auto rest = static_cast<uint32_t>(value);
        do hex[length++] = Digits[rest & 15]; while (rest >>= 4);
        while (length) out += hex[--length];
        out += '"';
    }
    if (!firstGroup) out += '}';
    out += "\n  }\n}\n";
    return out;
}
}
