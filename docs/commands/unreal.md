# Unreal pack

*Home of:* the Unreal pack's rules every command shares: object identity and host hooks, which commands answer off the game thread and which need it, the layout groups checked without an SDK, and locating the engine's globals; the index of its command pages.

**TL;DR:** The Unreal pack finds objects, reads their fields by name and
calls the game's own functions in any UE5 game, with or without a dump.
Lookups and reads work from any thread the host ticks on; calls and
everything that runs engine code need the game thread ([the list](#off-the-engines-thread)),
which the host gives by ticking there or after `game-thread` with `action` `on`. Without a
dump, each part of the engine layout is checked on objects every UE5 game
has before its commands run, so a non-stock engine refuses instead of
guessing. Where this sits in the ladder: [tiers](../tiers.md).

Every op works without an SDK ([without an SDK](#without-an-sdk)); a host's
SDK, when it has one, only supplies the layout. Each verb's args and an
example call: the [reference](reference.md) (or `help` with `verb`); result
keys: [result shapes](results.md). This page holds what every command
shares; each command's behaviour and guards are on its page:

| Page | Commands |
|---|---|
| [objects](unreal-objects.md) | `find`, `describe`, `name`, `name-find`, `enum`, `census`, `each`, object filters, class names, `assets-export` |
| [fields](unreal-fields.md) | `get` in every mode, compact values, `entries`, `rows`, text values, readable names, native members (`member-name`), `layout-export`, `layout-check` |
| [calls](unreal-calls.md) | `call`, `call-pure`, `call-each`, `release`, `func`, `funcs`, named calls, the call recorder, `line-trace`, `game-thread` |
| [code](unreal-code.md) | `script` (and its `decode`), `name-refs`, exec entries |
| [labels](labels.md) (core) | `labels-export`, labels files, `peek` with `label`, `labels-sign`, `labels-check`, `labels-port` |

## Every command

- **Object identity:** an object-table slot plus its serial. Serial 0 means
  not assigned yet and agrees with a later serial for the same slot and
  address. A stale ref is `not found`; an address-only ref is identified
  first.
- **Host hooks:** `Context::object` validates every object the pack touches;
  `Context::gameThread` is required and must return true before any pack
  handler runs, except the reads a host opts into with
  `Context::offThreadReads` ([below](#off-the-engines-thread)). A context
  without it is refused by every handler (`host gave no engine-thread
  check`), never treated as being on the right thread; one returning false
  is refused as `not on the game thread`.
- **Faults inside engine calls** (`call`, `call-pure`, `call-each`):
  an access fault is caught and answered `access faulted`; the
  engine may be left in a bad state. Other exceptions are not caught.
- **Globals:** every command first needs the object table and name pool
  located and checked ([below](#before-the-first-command)); until then it
  is refused with `pack off: <reason>`.
- **Scans** (`find`, `census`, `each`, `name-refs`, exec entries) take at most
  1 ms of the host's tick each tick (`Progress::Continue`; the host may
  allow more, `Context::scanMicroseconds`, up to 50 ms).

## Before the first command

The pack never takes addresses from an SDK, only (with `reagentlive::ue`)
its layouts, so a dump made before a game patch still works once the
globals are found again ([design](../design/unreal.md)). The first
pack command, on whichever thread ticks the agent, locates them:

1. **Object table:** the host's guess (`Context::objectsGuess`), else every
   8-byte aligned address in the image's writable sections whose bytes look
   like a table header. A candidate passes when its counts and chunks are
   plausible, every chunk pointer reads, and each object in the first 64
   slots carries its own slot as its index and has a class chain ending in
   the same class of classes (the one object that is its own class).
2. **Name pool:** the host's guess (`Context::namesGuess`), else the same
   scan for a plausible pool header. A candidate passes when one entry
   layout decodes name 0 as `None` and the class of classes' name as
   `Class`; that layout is used for every name from then on.

Every read on the way is a preflighted, fault-guarded copy, so a garbage
guess is a refusal, never a crash. The host log gets one line with both
addresses and whether each came from the guess or the scan. A failure is
the refusal `pack off: <reason>` (`table: index is not slot; scan: none`)
and a warning line; it is tried again on a command at least 2 s later,
since an early injection can run before the engine has built its globals.
Core commands work either way.

Built without an SDK, the pack also finds the layout the globals are read
with ([without an SDK](#without-an-sdk)); the log line then says
`layout probed` with the item size and the two UStruct offsets.

Names come from the located pool, not the SDK's `FName::ToString` (which
calls into the engine at an address the patch may have moved): base name,
the path before the last `/` dropped, `_<number - 1>` added for a numbered
name; wide characters above 0x7f read `?`.

## Without an SDK

`reagentlive::ue-base` builds every op with no SDK
([hosting](../hosting.md#cmake-targets)): one prebuilt agent for any UE5
game, with no dump (`examples/unreal` is one). Engine types are read at
the offsets of the engine layout (`packs/ue/engine_layout.h`, the one home
of every engine offset, grouped as below): a host's SDK when it links
`reagentlive::ue`, else `StockUE5`. Members an SDK does not describe (the
interfaces array, container layouts, `Script`, the frame, the curve table
group) are `StockUE5` in both. The layout is compiled in: no layout can be
loaded at run time yet ([ideas](../ideas.md#33--layout-profile-loading)).

**Layout groups.** Without an SDK, the first op that needs a group checks
it once on objects every UE5 game has, and the group's ops run only if it
passes; otherwise they are refused with the check that failed (e.g.
"field: Vector members are not X, Y, Z"):

| Group | Checked on | Ops |
|---|---|---|
| class | cast flags of `Class`, `Struct`, `Field`, `Object` | every op below |
| struct | `Class` > `Struct` > `Field` > `Object` > null; `Guid` size 16, `Vector` 12 or 24 | fields, params, func, funcs, script, refs, call |
| field | `Vector`'s properties are `X`, `Y`, `Z`, float or double | fields, params, call |
| property | `Vector` and `Guid` member offsets, sizes, `ArrayDim` 1, plain-data flags; `IntProperty`'s field cast flags | fields, params, call |
| typed | every bool on `Actor` (bytes at the bool base), its struct and array properties | fields, params, call |
| function | `KismetMathLibrary:Add_IntInt`: native, parameters 12 bytes, return at 8, exec in code | func, funcs, exec entries, call |
| enum | `ENetRole` decodes to 5 names, `ROLE_None` first | enum |
| call | `UObject`'s vtable slot tests `FunctionFlags` against `FUNC_Native` | call, call-pure, call-each, the recorder |

`FFrame` offsets (the recorder) and the set, map, interface, data table
and curve table layouts are not checked up front: the recorder needs the
call group, and each container, table and interface array is validated
where it is read. A passing group does not
lift the thread rule: a host off the game thread still gets only the
[off-thread commands](#off-the-engines-thread). Finding a layout for an
engine that is not stock: the skill's [new-game page](../../.claude/skills/reagentlive/references/unreal/new-game.md).

The table, header and two struct offsets come from the engine, checked like the globals:

- **Table and object header:** UE5's chunked table (64K items per chunk)
  and object header (flags, index, class, name, outer: the `object` members
  of `StockUE5`), with the item in one of three shapes (24 bytes with the
  object first or after an 8-byte field, or 16 bytes). Each is tried at
  every candidate address; the table check above decides, so a wrong shape
  is refused, never used. An engine that moved the header turns the pack
  off with `table: no layout fits`.
- **`UStruct::SuperStruct`:** the first pointer field after the header of
  the class of classes whose chain reads `Struct`, `Field`, `Object`, then
  null. Unknown, `find` and `census` with `cls` match the exact class only.
- **`UStruct::Size`:** the int32 after `SuperStruct` that holds the header
  size in `Object` and one pointer more in `Field`. Unknown,
  `describe` reports no class size.
- **Serials** are not known: every ref carries serial 0, which agrees with
  any serial, so a ref is checked by slot and address only. A ref with a
  real serial is the sign of an SDK host.
- **The first command scans** unless the host gives address guesses
  (`Context::objectsGuess`, `namesGuess`; an SDK host usually takes them
  from its dump). A host that gives none, as `examples/unreal`, scans the
  image's writable sections for both globals on its first pack command, a
  stall of up to seconds on the ticking thread.

## Off the engine's thread

This section is the one list of which commands need the game thread; other
pages link here. A host that ticks elsewhere (its own thread, as
`examples/unreal`, or a borrowed frame thread such as Present) may set
`Context::offThreadReads`; then:

| Answer off the game thread (with `offThreadReads`) | Need the game thread |
|---|---|
| `find`, `describe` (and its batch form), `census`, `name`, `name-find`, object reads (`read` of an object ref, `get` in every mode, array previews), `entries`, `enum`, `func` (its metadata), `funcs`, `script` with `decode` | `call`, `call-pure`, `call-each`, `release`, `func`'s `params`, `each`, `script`, `name-refs`, `exec entries` (and `labels-export`, which uses them), `owner`, `route`, the call recorder (`calls-start`, `calls-read`, `calls-stop`), and what rides `call`: `line-trace` and text values |

Commands that need the game thread are refused off it with `not on the
game thread`. A host gets onto the game thread by ticking there (an SDK
host on the engine's own tick) or, with `reagentlive::ue-draw`, by
`game-thread` with `action` `on`, after which `status` says `host_thread: engine`.
Locating the globals runs on either thread. Without `offThreadReads`, a
host off the game thread gets no pack command at all.

- **Guards:** each answered handler runs under a fault guard, so memory
  freed mid-read is the refusal `fault during an off-thread read` (also a
  log warning), never a crash. The object's identity (slot and serial) is
  checked before and after the read; a change is `object changed during
  the read` and the data is dropped.
- **Not guarded against:** values torn between two fields of one read;
  the console marks `get`, `entries` and batches
  `tick_accurate: false`, and `status` explains the host tick.
