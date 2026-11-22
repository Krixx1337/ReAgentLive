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
    int32_t fieldClass, fieldNext, fieldName, fieldClassName, fieldClassCastFlags, uFieldNext;
    // property: FProperty.
    int32_t arrayDim, elementSize, propertyFlags, propertyOffset;
    // typed: FBoolProperty's FieldSize, ByteOffset, ByteMask, FieldMask bytes
    // start at boolBase; one pointer for struct, array inner, set element and
    // map key/value; FScriptSetLayout and FScriptMapLayout after them.
    int32_t boolBase, structStruct, arrayInner, setElement, mapKey, mapValue, setLayout, mapLayout;
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
};

// The layout of stock UE 5.x as this pack has read it live (UE 5.7,
// FField-based, 8-byte FName, two-array enums). A game's profile is checked
// on known objects, never assumed from this.
inline constexpr EngineLayout StockUE5 = {
    .objectFlags = 0x08, .objectIndex = 0x0C, .objectClass = 0x10, .objectName = 0x18, .objectOuter = 0x20,
    .nameNumber = 0x04,
    .superStruct = 0x40, .children = 0x48, .childProperties = 0x50, .structSize = 0x58, .script = 0x60,
    .castFlags = 0xD8, .defaultObject = 0x110, .interfaces = 0x1E0,
    .fieldClass = 0x08, .fieldNext = 0x18, .fieldName = 0x20, .fieldClassName = 0x00, .fieldClassCastFlags = 0x18,
    .uFieldNext = 0x28,
    .arrayDim = 0x30, .elementSize = 0x34, .propertyFlags = 0x38, .propertyOffset = 0x44,
    .boolBase = 0x70, .structStruct = 0x70, .arrayInner = 0x78, .setElement = 0x70, .mapKey = 0x70, .mapValue = 0x78,
    .setLayout = 0x78, .mapLayout = 0x80,
    .functionFlags = 0xB0, .parmsSize = 0xB6, .returnValueOffset = 0xB8, .execFunction = 0xD8,
    .enumNames = 0x48,
    .processEventSlot = 0x4C, .frameNode = 0x10, .frameLocals = 0x28,
    .rowMap = 0x30,
};

// The interface array's entry; an object's identity guard (index, then class).
inline constexpr uint32_t InterfaceEntrySize = 16, ObjectIdentityLength = 12;

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
    {"function", "FunctionFlags", &EngineLayout::functionFlags},
    {"function", "ParmsSize", &EngineLayout::parmsSize},
    {"function", "ReturnValueOffset", &EngineLayout::returnValueOffset},
    {"function", "ExecFunction", &EngineLayout::execFunction},
    {"enum", "Names", &EngineLayout::enumNames},
    {"call", "ProcessEventSlot", &EngineLayout::processEventSlot},
    {"call", "FFrame.Node", &EngineLayout::frameNode},
    {"call", "FFrame.Locals", &EngineLayout::frameLocals},
    {"datatable", "RowMap", &EngineLayout::rowMap},
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
