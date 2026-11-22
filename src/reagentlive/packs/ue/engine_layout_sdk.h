// reagentlive Unreal pack: the engine layout a host's Dumper-7 SDK describes.
// Needs the SDK's CoreUObject headers on the include path as SDK/...; members
// the SDK does not declare come from StockUE5 (engine_layout.h).
#pragma once
#include <cstddef>
#include "SDK/CoreUObject_classes.hpp"
#include "reagentlive/packs/ue/engine_layout.h"

namespace reagentlive::ue::layout {
consteval EngineLayout SdkLayout() {
    auto at = [](size_t offset) { return static_cast<int32_t>(offset); };
    EngineLayout out = StockUE5;
    out.objectFlags = at(offsetof(SDK::UObject, Flags));
    out.objectIndex = at(offsetof(SDK::UObject, Index));
    out.objectClass = at(offsetof(SDK::UObject, Class));
    out.objectName = at(offsetof(SDK::UObject, Name));
    out.objectOuter = at(offsetof(SDK::UObject, Outer));
    out.nameNumber = at(offsetof(SDK::FName, Number));
    out.superStruct = at(offsetof(SDK::UStruct, SuperStruct));
    out.children = at(offsetof(SDK::UStruct, Children));
    out.childProperties = at(offsetof(SDK::UStruct, ChildProperties));
    out.structSize = at(offsetof(SDK::UStruct, Size));
    out.castFlags = at(offsetof(SDK::UClass, CastFlags));
    out.defaultObject = at(offsetof(SDK::UClass, ClassDefaultObject));
    out.fieldClass = at(offsetof(SDK::FField, ClassPrivate));
    out.fieldNext = at(offsetof(SDK::FField, Next));
    out.fieldName = at(offsetof(SDK::FField, Name));
    out.fieldClassName = at(offsetof(SDK::FFieldClass, Name));
    out.fieldClassCastFlags = at(offsetof(SDK::FFieldClass, CastFlags));
    out.uFieldNext = at(offsetof(SDK::UField, Next));
    out.arrayDim = at(offsetof(SDK::FProperty, ArrayDim));
    out.elementSize = at(offsetof(SDK::FProperty, ElementSize));
    out.propertyFlags = at(offsetof(SDK::FProperty, PropertyFlags));
    out.propertyOffset = at(offsetof(SDK::FProperty, Offset));
    out.boolBase = at(offsetof(SDK::FBoolProperty, FieldSize));
    out.structStruct = at(offsetof(SDK::FStructProperty, Struct));
    out.arrayInner = at(offsetof(SDK::FArrayProperty, InnerProperty));
    out.setElement = at(offsetof(SDK::FSetProperty, ElementProperty));
    out.mapKey = at(offsetof(SDK::FMapProperty, KeyProperty));
    out.mapValue = at(offsetof(SDK::FMapProperty, ValueProperty));
    out.functionFlags = at(offsetof(SDK::UFunction, FunctionFlags));
    out.execFunction = at(offsetof(SDK::UFunction, ExecFunction));
    out.enumNames = at(offsetof(SDK::UEnum, Names));
    out.processEventSlot = SDK::Offsets::ProcessEventIdx;
    return out;
}
}
