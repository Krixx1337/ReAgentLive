# A new Unreal game with no dump: finding the engine layout

The pack's reflected ops (`fields`, `dump`, `get`, `each`, `entries`,
`enum`, `func`, `params`, `call`) need the engine layout:
`packs/ue/engine_layout.h` (`EngineLayout`, one member per offset, grouped
by profile group). With no SDK, find each group live, or in a static tool
(IDA or Ghidra) where live data can't settle it, and write the result as a
profile (`ProfileJson` format: `{"groups": {group: {member: "0x.."}}}`).

Start from `StockUE5`: most UE 5.x games match it. Confirm every member
anyway; never ship a value only because it is stock.

## Rules

- **A candidate counts only when every sample agrees.** Use at least two
  known objects per member, from different classes. One sample that
  disagrees: the candidate is wrong, or that sample is.
- **More than one offset fits: stop.** Add samples until one is left. Don't
  pick the first.
- **Names may be hashed.** Many shipping games hash cooked `FProperty`
  names. If a member list shows hash-like names, confirm by structure
  instead: counts, sizes, offsets, cast flags, pointer targets.
- **Read, don't call.** Everything before the `call` group is reads only.
  The first call is the known-answer call, approved by the user.
- **A group you can't settle stays out of the profile.** A missing group
  turns its ops off. A wrong one returns garbage, or crashes for `call`.

## 0. Prepare

- Inject the SDK-free host. Run `status`. The object table and name pool
  are located at the first Unreal command (one freeze of up to seconds).
- Note the module and both globals' RVAs, and label them in the static tool.
- `find Class`, `find Struct`, `find Field`, `find Object`: the core classes
  every recipe below starts from. `describe` each.

## 1. Engine version and variants (static first)

- Strings: `++UE5+Release-5.x`, engine source paths in asserts
  (`Runtime/CoreUObject/...`), the build string. Often UTF-16: search both.
- The version tells you what to expect:
  - 4.25 and later use `FField`/`FProperty`. Earlier: `UProperty`, a
    different recipe, not covered here.
  - Before 5.6 enum names are a `{FName, int64}` pair array. From 5.6 on
    they are two arrays and a count (see `enum`).
  - `FName` is 8 bytes unless the game is built with case-preserving names
    (16). The `nameNumber` offset tells: 4 with 8-byte names.
  - Hashed names: a pool entry with an 8-byte hash prefix. The pack reads
    both pool layouts; `name` decoding shows which one this game uses.

## 2. Groups, in dependency order

Each section: who leads, known answers, the check.

### object (found by level A)

The header (`Flags` 0x08, `Index` 0x0C, `Class` 0x10, `Name` 0x18,
`Outer` 0x20) and table shape are found and checked at attach. `status`
reports them. Copy them into the profile.

### struct (live)

| Member | Known answer |
|---|---|
| `SuperStruct` | `Struct` → `Field`, `Class` → `Struct`: the one pointer offset where both hold |
| `Size` (int32) | `Color` 0x4, `Guid` 0x10, `Transform` 0x60 (with doubles) |
| `ChildProperties` | `Vector`, `Vector4`, `Vector2D`: a pointer to an `FField` whose name is `X` (`Guid`: `A`). `PropertyLink` (stock 0x70) points at the same field for any struct with no parent, so two offsets fit: keep the one that is null in classes with no own properties but inherited ones (`Property`, `NumericProperty`, many others) while the other is set. Hashed names: a pointer whose target's `FField.Owner` (+0x10) is the struct itself |
| `Children` | `PlayerController` → a `UFunction` object (`describe` it). The `UField` chain, not the `FField` one |
| `Script` | a Blueprint function's `TArray<uint8>` (count > 0, capacity ≥ count). Native functions read 0 |

`SuperStruct` and `Size` sit within 0x40 of each other after the 0x28-byte
`UField` header. `StockUE5` has `SuperStruct` 0x40 (a 0x10 base chain
before it in shipping builds).

### field (live)

From any `ChildProperties` pointer (an `FField`):

| Member | Known answer |
|---|---|
| `FField.ClassPrivate` | a pointer to an `FFieldClass` whose `Name` is `FloatProperty` / `DoubleProperty` (for `Vector.X`) |
| `FField.Next` | `Vector.X` → `Y` → `Z` → null |
| `FField.Name` | an `FName` that decodes to `X` |
| `FFieldClass.Name` | at 0 in `FFieldClass` |
| `FFieldClass.CastFlags` | uint64 that contains the class's own `Id` (a single bit: `ByteProperty` 0x40, `IntProperty` 0x80) plus its parents' bits (`Field` 0x1, `Property` 0x8000) |
| `UField.Next` | `PlayerController` functions chain: each `Next` is another `UFunction` of the class |

### property (live)

| Member | Known answer |
|---|---|
| `Offset` (int32) | `Color`: `B` 0x0, `G` 0x1; `Guid`: `C` 0x8; `Vector`: 0, 8, 16 (doubles) |
| `ElementSize` (int32) | `Guid.A` 4, `Vector.X` 8 (UE5 doubles), `Color.R` 1 |
| `ArrayDim` (int32) | 1 for `Guid`'s members, `ElementSize - 4` before it in stock |
| `PropertyFlags` (uint64) | every member of `Guid`, `Color` and `Vector` has `ZeroConstructor` (0x200), `NoDestructor` (0x1000000) and `IsPlainOldData` (0x40000000); the rest differs (`Guid`'s members lack `BlueprintVisible`) |

Hashed names: find `Offset` by structure instead. Every member of a struct
has a distinct offset, the offsets are within the struct's `Size`, and
offset + element size of the last member ≤ `Size`.

### typed (live)

- `FBoolProperty`: four bytes (`FieldSize`, `ByteOffset`, `ByteMask`,
  `FieldMask`) right after `FProperty`. Collect every bool property of
  `Engine`, `PlayerController`, `Actor`, `PrimitiveComponent`,
  `SceneComponent` and `CharacterMovementComponent` (by `FFieldClass.Name`,
  or by cast flags when names are hashed). The base is the one offset where
  every bool has `FieldSize` 1, `ByteOffset` below 8, a single-bit
  `ByteMask`, and `FieldMask` equal to `ByteMask` (a bitfield bool) or 0xFF
  (a native `bool`: `ByteMask` 1). Expect both kinds. More than one offset
  fits: refuse.
- `FStructProperty.Struct`: `TwoVectors`' members point to the `Vector`
  struct object.
- `FObjectProperty.PropertyClass`: `Controller.Pawn` → class `Pawn`,
  `Controller.PlayerState` → `PlayerState`.
- `FArrayProperty.Inner`, `FSetProperty.ElementProp`, `FMapProperty`
  key and value: an inner `FField` pointer right after `FProperty`
  (`GameViewportClient.DebugProperties` is an array, `LevelCollection.Levels`
  a set).
- Set and map layouts: static (see below).

### class (live)

| Member | Known answer |
|---|---|
| `CastFlags` (uint64) | `Actor` has `CASTCLASS_AActor`; `Class` has `Field | Struct | Class` |
| `ClassDefaultObject` | `Object` → `Default__Object`, `Field` → `Default__Field`, between 0x28 and 0x200 |

### function (live, then static)

| Member | Known answer |
|---|---|
| `FunctionFlags` (uint32) | `PlayerController:WasInputKeyJustPressed` has at least `Final | Native | Public | BlueprintCallable | BlueprintPure | Const` (0x54020401); `ToggleSpeaking` and `SwitchLevel` at least `Exec | Native | Public` (0x20600). Match a superset: engine builds add `RequiredAPI` (0x2) |
| `ExecFunction` | a pointer into the main module's `.text`, different per function, 8-byte aligned |
| `ParmsSize` (uint16) | `KismetMathLibrary:Add_IntInt`: 12 (A, B, return) |
| `ReturnValueOffset` (uint16) | `Add_IntInt`: 8; 0xFFFF for a function with no return |

`ParmsSize` and `ReturnValueOffset` follow `FunctionFlags` in stock UE5
(`NumParms` uint8 at +4, `ParmsSize` at +6, `ReturnValueOffset` at +8).
Confirm in static: `UFunction::Invoke` or ProcessEvent reads
`ReturnValueOffset` and compares it with 0xFFFF.

### enum (live)

- Before 5.6: a `TArray` of `{FName, int64}` pairs. `ENetRole` has 5
  entries, `ETraceTypeQuery` 0x22.
- 5.6 and later: `FName*` at `Names - 8`, `int64*` at `Names`, int32
  count at `Names + 8`; bit 0 of each pointer is an allocation tag. The
  values array reads 0, 1, 2, ... for `ECollisionChannel`.
- The profile records `Names`: the pair array, or the value array.

### datatable (live)

`RowMap` follows `RowStruct`, the one object property `DataTable` declares.
Check on any data table: a sparse array of `{FName, uint8*}` pairs.

### interface (static, then live)

`UClass::Interfaces`: a `TArray` of `{UClass*, int32 PointerOffset, bool}`
(16 bytes each). Static: find a function that walks it, `UClass::ImplementsInterface`
(compares each entry's class with the asked one, 16-byte stride). Live:
every entry's class is a `UInterface` subclass, and its `PointerOffset` is
inside the implementing class's `Size`.

### set and map layouts (static, then live)

`FScriptSetLayout` and `FScriptMapLayout` sit right after the inner
pointers (stock: set 0x78, map 0x80). Static: `FSetProperty`'s and
`FMapProperty`'s constructors or `LinkInternal` store them. Live: the
pack's `ContainerLayoutValid` (key at 0, value after it, adjacent hash
links, stride ≤ 512) must hold on every map and set property you try.

### call (static)

Data alone is guesswork here, and a wrong call crashes. Every member needs
static evidence.

- **ProcessEvent slot.** In `UObject`'s vtable (from any object's first
  qword, `vtable`), the slot whose function tests `UFunction::FunctionFlags`
  against `FUNC_Native` (0x400), e.g. `test dword ptr [reg+0B0h], 400h`,
  early in its body. Static: also reached from
  `UObject::ProcessEvent`'s callers, such as the `CallFunctionByNameWithArguments`
  path. Stock UE5: slot 0x4C, but games move it.
- **`FFrame::Node` and `::Locals`.** The `FFrame` constructor stores the
  function at `Node` (0x10) and the parameter block at `Locals` (0x28).
  ProcessEvent builds a frame on its stack before calling `ExecFunction`.
- **Known-answer call** (user approval required):
  `call-pure Default__KismetMathLibrary Add_IntInt A=2 B=3` returns 5.
  Only then turn on `call`, `each` with functions, and the recorder.

### Reading names while probing

Query each comparison index with its own `name` command. A garbage index
(any pointer or flag word read as an index) can misalign the other names in
the same `name` reply.

## 3. Finish

- Write the profile beside the host (`<game>.layout.json`).
- Compare every group again on a second set of objects (e.g. after a level
  change). A value that moved is not an engine offset.
- Record which groups are in, which are out, and why.
