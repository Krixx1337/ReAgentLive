# Unreal code

*Home of:* reading Blueprint bytecode and finding native code by reflection: `script` (the anchored scan and `decode`), `name-refs`, and exec entries.

**TL;DR:** `script` lists what a Blueprint function's bytecode uses, or
with `decode` reads it as statements (`if !(A > B) goto +0x58`) with no
game thread; `name-refs` finds every function whose bytecode names a pooled
name; exec entries list every native function's thunk, for labels.
Native code by address: [fast lane](fast-lane.md). Rules every pack
command shares: [Unreal pack](unreal.md).

## script

`script` reads the function's bytecode and scans it anchored: a value counts
only right after its opcode byte, so a byte that merely looks like a name or
pointer is never listed. Every entry is confirmed before it is listed: a
name must decode, a function must be a live UFunction in the object table,
and a property pointer's field class name must end in `Property`. Repeats
are folded into `count`. The opcodes are UE5's public `Script.h` values (the
SDK has no `EExprToken`). A native function has no bytecode (`not found`).

### decode

`script` with `decode` reads the bytecode through plain memory reads and
decodes it in the console, so it needs no game thread: one statement per
top-level expression with its offset (`Speed = Vehicle.GetSpeedKPH()`,
`return ReturnValue`). Functions print as `Owner.Function`, math operators
as infix; `raw` holds the reflected one. Properties print by their readable name from the
[names file](unreal-fields.md#readable-names) where it has one, and the
names, functions and properties lists come from the decoded operands. An
opcode the decoder does not list stops it with `stopped`; nothing is
guessed.

On an uber graph (`ExecuteUbergraph_*`), `events` lists where each event
enters it, and `entry` decodes one event from there to its first return,
pop, goto or end. A big decode narrows with `view.match` and `to`; a digest
names both.

## name-refs

Scans every function's bytecode for a name constant, one table scan spread
over ticks (capped, with `capped` set). `name` must match a pooled name
exactly (or give `name_index`, its comparison index). Each row's ref is the
Function itself: `script` takes it as `object` with no `function`.

## Exec entries

The agent op behind `labels-export`' `exec` rows: one object-table scan for
native functions with an exec thunk of their own, cached, then paged. What
the labels file does with them: [labels](labels.md#labels-export).
