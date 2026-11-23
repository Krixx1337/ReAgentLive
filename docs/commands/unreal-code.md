# Unreal code

*Home of:* reading Blueprint bytecode and finding native code by reflection: `script` (the anchored scan and `decode`), `refs`, and exec entries.

**TL;DR:** `script` lists what a Blueprint function's bytecode uses, or
with `decode` reads it as statements (`if !(A > B) goto +0x58`) with no
game thread; `refs` finds every function whose bytecode names a pooled
name; exec entries list every native function's thunk, for labels.
Native code by address: [fast lane](fast-lane.md). Rules every pack
command shares: [Unreal pack](unreal.md).

## script

`script` reads the function's bytecode (`UStruct::Script`, a
`TArray<uint8>`, `EngineLayout::script`) and scans it anchored: a value
counts only right after its opcode byte. Names follow `EX_NameConst`
(0x21, a 12-byte script name), functions follow `EX_FinalFunction`
(0x1C), `EX_LocalFinalFunction` (0x46) or `EX_CallMath` (0x68), and
properties follow `EX_LocalVariable` (0x00), `EX_InstanceVariable` (0x01)
or `EX_LocalOutVariable` (0x48). Every entry is confirmed before it is
listed: a name must decode, a function must be a live UFunction in the
object table, and a property pointer's field class name must end in
`Property`. Repeats are folded into `count`. The opcodes are UE5's public
`Script.h` values (the SDK has no `EExprToken`); each was seen live before
a value of its kind. A native function has no bytecode (`not found`).

### decode

`script` with `decode` reads the bytecode through plain memory reads and
decodes it in the console, so it needs no game thread: every expression,
recursively, as one statement per top-level expression with its offset
and opcode (`Speed = Vehicle.GetSpeedKPH()`, `if !(A > B) goto +0x58`,
`return ReturnValue`).

- Properties print by their readable name from the
  [names file](unreal-fields.md#readable-names) where it has one (the
  property list's `raw` keeps the reflected name), functions as
  `Owner.Function`, `KismetMathLibrary` operators of two arguments as
  infix (`A * B`), implicit casts as `double(x)` / `float(x)`. The names,
  functions and properties lists then come from the decoded operands, not
  the anchored scan.
- The opcode values and operand sizes are UE5's `Script.h` and
  `ScriptCore.cpp`: an 8-byte property or object pointer, a 12-byte script
  name, 4-byte jump and skip offsets, double-precision vector and rotator
  constants. An opcode the decoder does not list stops it with `stopped`
  (`unknown 0xNN at +0xOFF`); nothing is guessed.
- On an uber graph (`ExecuteUbergraph_*`), `events` lists where each event
  enters it, from the class's functions that call it with an integer
  constant; `entry` (an offset) decodes one event from there to its first
  return, pop, goto or end.

## refs

Scans every function's bytecode for `EX_NameConst` with that comparison
index, one table scan spread over ticks; later pages come from the cached
result; up to 512 functions (`capped`). `name` must match a pooled name
exactly (the console resolves it through `name-find`).

## Exec entries

The agent op behind `export-labels`' `exec` rows: one object-table scan
for native functions (`FUNC_Native`, with an exec thunk of their own),
cached, then paged (`ExecEntry`: `exec`, `owner`, `name`). What the
labels file does with them: [labels](labels.md#export-labels).
