# Unreal pack

*Home of:* the Unreal pack's rules every command shares: object identity and host hooks, which commands answer off the game thread and which need it, the layout groups checked without an SDK, and locating the engine's globals.

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
keys: [result shapes](results.md); the pages per command: [index](README.md).
This page holds what every command shares.

## Every command

- **Object identity:** an object-table slot plus its serial. Serial 0 means
  not assigned yet and agrees with a later serial for the same slot and
  address. A stale ref is `not found`; an address-only ref is identified
  first.
- **Host hooks:** `Context::object` validates every object the pack touches;
  `Context::gameThread` must return true before any pack handler runs,
  except the reads a host opts into with `Context::offThreadReads`
  ([below](#off-the-engines-thread)). A context without it is refused, never
  treated as being on the right thread.
- **Faults inside engine calls** (`call`, `call-pure`, `call-each`): an
  access fault is caught and answered `access faulted`; the engine may be
  left in a bad state. Other exceptions are not caught.
- **Globals:** every command first needs the object table and name pool
  located and checked ([below](#before-the-first-command)); until then it is
  refused with `pack off: <reason>`.
- **Scans** (`find`, `census`, `each`, `name-refs`, exec entries) take a
  small slice of each host tick, so a host's frame rate is not hit.

## Before the first command

The pack never takes addresses from an SDK, only (with `reagentlive::ue`)
its layouts, so a dump made before a game patch still works once the
globals are found again ([design](../design/unreal.md)). The first pack
command, on whichever thread ticks the agent, locates the object table and
the name pool: the host's guess (`Context::objectsGuess`, `namesGuess`),
else a scan of the image's writable sections for a plausible header, checked
against objects every UE5 game has (the plausibility criteria are in the
code). Every read on the way is fault-guarded, so a garbage guess is a
refusal, never a crash. A failure is the refusal `pack off: <reason>`
and a warning line, tried again on a command at least 2 s later, since an
early injection can run before the engine has built its globals. Core
commands work either way.

Names come from the located pool, not the SDK's `FName::ToString` (which
calls into the engine at an address the patch may have moved).

## Without an SDK

`reagentlive::ue-base` builds every op with no SDK
([hosting](../hosting.md#cmake-targets)): one prebuilt agent for any UE5
game, with no dump (`examples/unreal` is one). Engine types are read at the
offsets of the engine layout (`packs/ue/engine_layout.h`, the one home of
every engine offset): a host's SDK when it links `reagentlive::ue`, else
`StockUE5`. Members an SDK does not describe are `StockUE5` in both. The
layout is compiled in: no layout can be loaded at run time yet
([ideas](../ideas.md#33--layout-profile-loading)).

**Layout groups.** Without an SDK, the first op that needs a group checks it
once on objects every UE5 game has, and the group's ops run only if it
passes; otherwise they are refused with the check that failed. The groups
are `class`, `struct`, `field`, `property`, `typed`, `function`, `enum` and
`call`; what each checks and which ops it gates:
[layout groups](../../.claude/skills/reagentlive/references/unreal/layout-groups.md)
and `engine_layout.h`. Containers, tables and interface arrays are not
checked up front: each is validated where it is read. A passing group does
not lift the thread rule. Finding a layout for an engine that is not stock:
the skill's
[new-game page](../../.claude/skills/reagentlive/references/unreal/new-game.md).

Without an SDK the table, the object header and two struct offsets are
found by trying each known shape at every candidate and letting the global
checks decide, so a wrong shape is refused, never used:

- An engine that moved the header turns the pack off with `table: no layout
  fits`.
- `UStruct::SuperStruct` or `Size` unknown: `find` and `census` with `cls`
  match the exact class only; `describe` reports no class size.
- **Serials** are not known: every ref carries serial 0, which agrees with
  any serial, so a ref is checked by slot and address only. A ref with a
  real serial is the sign of an SDK host.
- **The first command scans** unless the host gives address guesses, a stall
  of up to seconds on the ticking thread.

## Off the engine's thread

This section is the one list of which commands need the game thread; other
pages link here. A host that ticks elsewhere (its own thread, as
`examples/unreal`, or a borrowed frame thread such as Present) may set
`Context::offThreadReads`; then:

| Answer off the game thread (with `offThreadReads`) | Need the game thread |
|---|---|
| `find`, `describe` (and its batch form), `census`, `name`, `name-find`, object reads (`read` of an object ref, `get` in every mode, array previews), `entries`, `enum`, `func` (its metadata), `funcs`, `script` with `decode` | `call`, `call-pure`, `call-each`, `release`, `func`'s `params`, `each`, `script`, `name-refs`, `exec entries` (and `labels-export`, which uses them), `owner`, `route`, the call recorder (`calls-start`, `calls-read`), and what rides `call`: `line-trace` and text values |

Commands that need the game thread are refused off it with `not on the game
thread`. A host gets onto the game thread by ticking there (an SDK host on
the engine's own tick) or, with `reagentlive::ue-draw`, by `game-thread`
with `action` `on`, after which `status` says `host_thread: engine`.
Locating the globals runs on either thread. Without `offThreadReads`, a host
off the game thread gets no pack command at all.

Off-thread reads run under a fault guard, so memory freed mid-read is the
refusal `fault during an off-thread read`, never a crash, and the object's
identity is checked before and after (`object changed during the read`).
Values torn between two fields of one read are not guarded against: the
console marks `get`, `entries` and batches `tick_accurate: false`.
