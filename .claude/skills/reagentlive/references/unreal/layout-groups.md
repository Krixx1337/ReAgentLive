# Layout groups: known answers per group

Step 2 of the new-game procedure. Per group: who leads, known answers, the
check. Stock offsets: `StockUE5` in `src/reagentlive/packs/ue/engine_layout.h`;
compare every candidate with it. The new-game rules apply (every sample
agrees; more than one fit: stop; hashed names: structure).

## object (found at attach)

The header and table shape are found and checked at the first Unreal verb
(without an SDK: `docs/commands/unreal.md#without-an-sdk`).
Copy them into the profile.

## struct (live)

| Member | Known answer |
|---|---|
| `SuperStruct` | `Struct` → `Field`, `Class` → `Struct`: the one pointer offset where both hold |
| `Size` (int32) | `Color` 0x4, `Guid` 0x10, `Transform` 0x60 (with doubles) |
| `ChildProperties` | `Vector`, `Vector4`, `Vector2D`: a pointer to an `FField` whose name is `X` (`Guid`: `A`). `PropertyLink` points at the same field for any struct with no parent, so two offsets fit: keep the one that is null in classes with no own properties but inherited ones (`Property`, `NumericProperty`) while the other is set. Hashed names: a pointer whose target's `FField.Owner` is the struct itself |
| `Children` | `PlayerController` → a `UFunction` object (`describe` it). The `UField` chain, not the `FField` one |
| `Script` | a Blueprint function's `TArray<uint8>` (count > 0, capacity ≥ count). Native functions read 0 |

`SuperStruct` and `Size` sit within 0x40 of each other after the `UField`
header.

## field (live)

From any `ChildProperties` pointer (an `FField`):

| Member | Known answer |
|---|---|
| `FField.ClassPrivate` | a pointer to an `FFieldClass` whose `Name` is `FloatProperty` / `DoubleProperty` (for `Vector.X`) |
| `FField.Next` | `Vector.X` → `Y` → `Z` → null |
| `FField.Name` | an `FName` that decodes to `X` |
| `FFieldClass.Name` | at 0 in `FFieldClass` |
| `FFieldClass.CastFlags` | uint64 that contains the class's own `Id` (a single bit: `ByteProperty` 0x40, `IntProperty` 0x80) plus its parents' bits (`Field` 0x1, `Property` 0x8000) |
| `UField.Next` | `PlayerController` functions chain: each `Next` is another `UFunction` of the class |

## property (live)

| Member | Known answer |
|---|---|
| `Offset` (int32) | `Color`: `B` 0x0, `G` 0x1; `Guid`: `C` 0x8; `Vector`: 0, 8, 16 (doubles) |
| `ElementSize` (int32) | `Guid.A` 4, `Vector.X` 8 (UE5 doubles), `Color.R` 1 |
| `ArrayDim` (int32) | 1 for `Guid`'s members; its neighbour in `StockUE5` |
| `PropertyFlags` (uint64) | every member of `Guid`, `Color` and `Vector` has `ZeroConstructor` (0x200), `NoDestructor` (0x1000000) and `IsPlainOldData` (0x40000000); the rest differs (`Guid`'s members lack `BlueprintVisible`) |

Hashed names: find `Offset` by structure. Every member of a struct has a
distinct offset within the struct's `Size`, and offset + element size of
the last member ≤ `Size`.

## typed (live)

- `FBoolProperty`: four bytes (`FieldSize`, `ByteOffset`, `ByteMask`,
  `FieldMask`) right after `FProperty`. Collect every bool property of
  `Engine`, `PlayerController`, `Actor`, `PrimitiveComponent`,
  `SceneComponent` and `CharacterMovementComponent` (by `FFieldClass.Name`,
  or cast flags when names are hashed). The base is the one offset where
  every bool has `FieldSize` 1, `ByteOffset` below 8, a single-bit
  `ByteMask`, and `FieldMask` equal to `ByteMask` (a bitfield bool) or 0xFF
  (a native `bool`: `ByteMask` 1). Expect both kinds. More than one offset
  fits: refuse.
- `FStructProperty.Struct`: `TwoVectors`' members point to the `Vector`
  struct object.
- `FObjectProperty.PropertyClass`: `Controller.Pawn` → class `Pawn`,
  `Controller.PlayerState` → `PlayerState`.
- `FArrayProperty.Inner`, `FSetProperty.ElementProp`, `FMapProperty` key
  and value: an inner `FField` pointer right after `FProperty`
  (`GameViewportClient.DebugProperties` is an array,
  `LevelCollection.Levels` a set).

## class (live)

| Member | Known answer |
|---|---|
| `CastFlags` (uint64) | `Actor` has `CASTCLASS_AActor`; `Class` has `Field | Struct | Class` |
| `ClassDefaultObject` | `Object` → `Default__Object`, `Field` → `Default__Field`, between 0x28 and 0x200 |

## function (live, then static)

| Member | Known answer |
|---|---|
| `FunctionFlags` (uint32) | `PlayerController:WasInputKeyJustPressed` has at least `Final | Native | Public | BlueprintCallable | BlueprintPure | Const` (0x54020401); `ToggleSpeaking` and `SwitchLevel` at least `Exec | Native | Public` (0x20600). Match a superset: engine builds add `RequiredAPI` (0x2) |
| `ExecFunction` | a pointer into the main module's `.text`, different per function, 8-byte aligned |
| `ParmsSize` (uint16) | `KismetMathLibrary:Add_IntInt`: 12 (A, B, return) |
| `ReturnValueOffset` (uint16) | `Add_IntInt`: 8; 0xFFFF for a function with no return |

`NumParms`, `ParmsSize` and `ReturnValueOffset` follow `FunctionFlags` in
stock. Confirm in static: `UFunction::Invoke` or ProcessEvent reads
`ReturnValueOffset` and compares it with 0xFFFF.

## enum (live)

- Before 5.6: a `TArray` of `{FName, int64}` pairs. `ENetRole` has 5
  entries, `ETraceTypeQuery` 0x22.
- 5.6 and later: `FName*` at `Names - 8`, `int64*` at `Names`, int32 count
  at `Names + 8`; bit 0 of each pointer is an allocation tag. The values
  array reads 0, 1, 2, ... for `ECollisionChannel`.
- The profile records `Names`: the pair array, or the value array.

## datatable (live)

`RowMap` follows `RowStruct`, the one object property `DataTable` declares.
Check on any data table: a sparse array of `{FName, uint8*}` pairs.

## interface (static, then live)

`UClass::Interfaces`: a `TArray` of `{UClass*, int32 PointerOffset, bool}`
(16 bytes each). Static: a function that walks it,
`UClass::ImplementsInterface` (compares each entry's class with the asked
one, 16-byte stride). Live: every entry's class is a `UInterface`
subclass, and its `PointerOffset` is inside the implementing class's
`Size`.

## set and map layouts (static, then live)

`FScriptSetLayout` and `FScriptMapLayout` sit right after the inner
pointers. Static: `FSetProperty`'s and `FMapProperty`'s constructors or
`LinkInternal` store them. Live: the pack's `ContainerLayoutValid` (key at
0, value after it, adjacent hash links, stride ≤ 512) must hold on every
map and set property you try.

## call (live slot, static frame)

A wrong call crashes, so every member needs code evidence, not data.

- **ProcessEvent slot.** In `UObject`'s vtable (`vtable` on any object), the
  slot whose function tests `UFunction::FunctionFlags` against
  `FUNC_Native` (0x400), e.g. `test dword ptr [reg+0B0h], 400h`, early in
  its body. Static: also reached from `UObject::ProcessEvent`'s callers
  (the `CallFunctionByNameWithArguments` path). Games move it from
  `StockUE5`'s slot.
- **`FFrame::Node` and `::Locals`.** The `FFrame` constructor stores the
  function at `Node` and the parameter block at `Locals`. ProcessEvent
  builds a frame on its stack before calling `ExecFunction`.
- Then the known-answer call, on the game thread, with the user's
  approval (SKILL.md rules): the game-thread reference.
