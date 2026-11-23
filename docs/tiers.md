# Tiers

*Home of:* the tier ladder (T0-T4): what each rung gives, what it needs, what stops it, which artifact ships it, how to read your tier from `status`, the two axes under it and the gap between T2 and T4.

**TL;DR:** What ReAgentLive can do in a process depends on how it got in:
five rungs, each adding commands to the one below. Any process gives raw
memory and code (T0); any UE5 game, with no dump, adds objects and fields
by name (T1-T2); the game thread adds engine calls (T3); a mod with its own
SDK adds the rest (T4). Don't expect engine calls before T3: they are
refused, not guessed.

## The ladder

```text
T4 SDK host       layout from the dump, real serials, no first-command scan, host verbs   (a mod's own host)
T3 Game thread    call, call-pure, params, each, script, refs, the call recorder          (+ game-thread on, or an engine host)
T2 Reflection     fields, dump, get, entries, enum, func, funcs                           (+ the stock layout groups pass)
T1 Unreal located find, describe, census, name, name-find, object reads                   (+ object table and name pool found)
T0 Core           read, write, search, xrefs, disasm, sig-*, vtable, pin, watch, trial    (any x64 process)
```

Each rung keeps everything below it. Which Unreal commands answer off the
game thread and which need it is stated once, in
[Off the engine's thread](commands/unreal.md#off-the-engines-thread).

| Tier | What you get | What it needs | What stops it | Ships in |
|---|---|---|---|---|
| **T0 Core** | memory reads and guarded writes, search, the [fast lane](commands/fast-lane.md) (disassembly, references, signatures), [native objects](commands/native-objects.md) (vtables, RTTI, pointer chains), pins, trials, watches | the DLL loaded into any Windows x64 process | nothing, once loaded | [`examples/own_thread`](../examples/own_thread/README.md) |
| **T1 Unreal located** | objects by name: `find`, `describe`, `census`, `name`, `name-find`, reads of an object's bytes | the Unreal pack, and its first command locating the engine's object table and name pool ([before the first command](commands/unreal.md#before-the-first-command)) | not a UE5 game, or the globals not found: `pack off: <reason>` | [`examples/unreal`](../examples/unreal/README.md) |
| **T2 Reflection** | fields and values by name: `fields`, `dump`, `get`, `entries`, `enum`, `func`, `funcs` | the engine layout: each layout group checked on objects every UE5 game has ([without an SDK](commands/unreal.md#without-an-sdk)) | a non-stock engine: the failing group's commands are refused with the check that failed; T1 and T0 still work | [`examples/unreal`](../examples/unreal/README.md) |
| **T3 Game thread** | the game's own functions: `call`, `call-pure`, `release`, `func`'s `params`, `each`, `script`, `refs`, `trace`, the [call recorder](commands/unreal.md#call-recorder) ([full list](commands/unreal.md#off-the-engines-thread)); `same_tick` means one moment of the game | the agent ticking on the game thread: `game-thread on` in the Unreal example, or a host that ticks there | `game-thread` answers `conflict` (the viewport's Draw is not where it expects), or unsupported in a host without `reagentlive::ue-draw` | [`examples/unreal`](../examples/unreal/README.md) after `game-thread on`; a mod's own host |
| **T4 SDK host** | the layout from the game's SDK dump (every group trusted), refs with real serials, the globals' addresses given so the first command does not scan, host verbs of the mod's own | a mod built with `reagentlive::ue` and its SDK, ticking on the game thread | a game patch until the SDK is dumped again | a mod's own host ([hosting](hosting.md)) |

An AI agent can build either example for you: [agent setup](agents/setup.md).

Words used here: an *SDK* or *dump* is a set of C++ headers a dumper tool
generates from a game; the *game thread* is the engine's own thread, the
only one where its functions may be called; a *layout group* is one set of
engine offsets checked together. More in the [glossary](INDEX.md#glossary).

## Read your tier from `status`

`status` prints no tier. Read it from three things:

| What you see | Tier |
|---|---|
| `packs: []` | **T0** |
| `packs: ["ue"]`, `host_thread: own` or `borrowed` | **T1**; **T2** once `fields Vector` answers |
| `packs: ["ue"]`, `host_thread: engine` | **T3** (after `game-thread on`, or an engine host) |
| T3, and refs carry real serials (`0x...#<index>:<serial>`, not `:0`) | **T4** (an SDK host; without an SDK every serial is 0) |

The first Unreal command decides T1
([before the first command](commands/unreal.md#before-the-first-command)). How an agent climbs from one rung to the next is the
[`reagentlive` skill](../.claude/skills/reagentlive/SKILL.md)'s first section.

## Two axes

The ladder mixes two separate questions:

- **Where it ticks.** On its own thread (both examples) or a thread the
  game runs for something else (`borrowed`), the agent reads memory and,
  with the Unreal pack and a host that opts in (the Unreal example does), objects
  and fields; values read across ticks need not line up. On the game thread (`engine`), engine calls work and a
  `batch` reads one moment of the game. The Unreal example moves from one
  to the other with `game-thread on` and back with `game-thread off`.
  Details: [host thread](hosting.md#host-thread).
- **Where the layout comes from.** Without an SDK, the stock UE5 layout,
  each group checked on known objects before its commands run (T1-T3).
  With a mod's SDK, the dump's layout (T4). Every engine offset has one
  home, `packs/ue/engine_layout.h`.

So T3 is "game thread + stock layout" and T4 is "game thread + SDK layout".

## The gap

A game whose engine is not stock fails one or more layout groups and stays
at T1 for those commands, with nothing between that and building an SDK
host. The missing rung is a layout profile: offsets measured once for that
engine and loaded by the agent. It is a backlog item ([ideas](ideas.md));
until then, the skill's `references/unreal/new-game.md` covers finding a
layout by hand.

## Beside the ladder: static tools

IDA, Ghidra and the like are not a rung. They pair with any tier: every
address comes back as `module` + `rva`, `sig-check` tests your signatures
against live bytes, and `export-labels` sends live names to a static tool
([labels](commands/unreal.md#labels-for-a-static-tool)). How ReAgentLive
fits next to each tool: [positioning](positioning.md#role-among-other-tools).
