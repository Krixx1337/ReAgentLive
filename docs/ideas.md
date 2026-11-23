# ReAgentLive backlog

*Home of:* the backlog: ideas waiting for a named trigger, what shipped from it, and what is deliberately out.

**TL;DR:** A list of features that may come next, each with the event
that would justify building it. Check here before proposing something:
it may be waiting, done, or ruled out on purpose. Numbers are stable ids,
not ranks, so a link to an idea never breaks.

Decisions behind the "no" answers: [design](design.md).

## Pull forward when an investigation needs it

### 6 · Bulk read paging

`batch` and `get` cover same-tick sampling of related ranges. Revisit paging
when an investigation needs a larger read budget across ticks.

### 2 · Class-wide snapshot and compare

`each` reads up to 16 fields across a class, `call-pure --each` one getter. Whole-object
snapshots across instances (every reflected field) remain open.

### 17 · `call-native` (caution)

Call a native function by address: `call-native <addr> [arg ...]`, or
`<object> --slot N [--offset M]` for a virtual call (the object, or the
interface part at `object + M`, as the first argument). Build it on the
first of two triggers, recorded here first: a live investigation needs a
call a read cannot replace, or an outside program is used on a game with no
engine pack, where it is the only way to call anything. It is the one
native-object feature that can crash the target, and what an
instrumentation tool cannot do is call on the engine's thread, between two
ticks, after a dry run.

- Core op with a client call; off unless the host opts in
  (`Host::nativeCalls`), and a host enables it only where `Tick` runs on a
  thread safe for engine calls. Approval stays the host's rule.
- Only a target inside a loaded module on an executable page; up to 6
  integer or pointer arguments (every address form), `@out:N` for a zeroed
  buffer of up to 256 bytes returned afterwards; `--returns
  u64|ptr|f32|f64`. No floating-point arguments or callbacks.
- `--dry` resolves everything and calls nothing: target with `module` and
  `rva`, the arguments, and the first instructions (`disasm`): the text for
  the approval request.
- An access fault is caught and answered as `access faulted` with the
  address, saying the target may be left in a bad state; nothing else is
  caught. Arguments travel in `data`; an older agent answers `unsupported`.
- Tests (loopback, calling the test program): integer and float returns,
  six arguments in order, `@out`, slot calls with and without `--offset`,
  heap and non-executable targets refused, no opt-in refused, a faulting
  function reported, `--dry` calling nothing. Live check (approved): an
  interface getter through `--offset` with `--returns f32` gives the value
  already recorded for the field it reads.
- When it ships: the `reagentlive` skill gains it (always `--dry` first,
  ask with its output, read a plain field instead of calling its getter),
  and each host's docs say whether it enables it.

### 19 · Checks for native members (`label --verify`)

Waits for the first game patch that moves a labelled offset. Native lines
already go `stale` under another image timestamp; this makes re-confirming
one command, as `layout-check` already does for reflected fields. An optional check per line (`--check finite`, `--check range
a..b`, or `--check equals <Owner:Getter>`, a pure getter returning the same
value); `label --verify <Class>` runs them on a live instance and, when all
pass, rewrites the timestamp. Tests: a check passing and one failing.

### 20 · Example client

What else lets someone drive the core from an outside program
([outside programs](outside-programs.md)); the host checklist, client
guide and both example hosts already exist ([hosting](hosting.md),
[clients](client.md)), and any UE5 game needs no SDK
(`examples/unreal`). Build it on a public release.

- **Minimal client** (`examples/minimal_client.cpp`): attaches, reads a
  value, holds one with a pin, exits cleanly; built and run by the tests
  against the own-thread host.
- Done when: the docs and that example alone are enough to write a client.

### 21 · Other engine packs (Unity, Source 2)

The core is engine-agnostic; engine knowledge lives in a pack (its own op
slot in 256-4095, its own agent handlers and client calls), as the Unreal pack
does. Natural candidates: Unity (Mono and IL2CPP) and Source 2, each with
its own object and type system to walk. Build one only for a real project
in that engine ([design](design.md#not-now)); no pack is promised.

- **What a pack adds:** object lookup, reflected fields, calls on the
  engine's thread, and the console verbs over them. Memory, the fast lane
  and native objects already work in any engine.
- **What it needs from the host:** a tick on the engine's thread and an
  object check, as the Unreal pack does ([hosting](hosting.md#host-checklist)).

### 22 · `entries` through a struct path

`entries` takes one field of the object, so a container inside a struct
member is out of reach: a curve's keys (`FloatCurve.Keys`) came back as
an address and a count from `get`, and `entries` refused `FloatCurve` as
"StructProperty, not an array". Pull forward when a container inside a
struct is the question again: take `get`'s paths and page the container
the path ends at.

### 23 · `each` over the last instance list

`each`'s cost is its scan of the object table (about 315 host ticks on a
370 000-object table, field or call alike). The agent keeps the instance
list; a later `each` of the same class could read over it, at the price of
missing instances spawned since. Pull forward when a session repeats
`each` on one class: an explicit flag, never a silent reuse.

### 24 · `watch-call`: a pure getter sampled every tick

`watch-start` samples fixed address ranges, so state that lives briefly inside a
manager object (a projectile in a subsystem, reached through a getter that
takes a handle) cannot be followed: the value has no stable address, and a
console round trip per sample is slower than the object's lifetime. A
session had to infer a projectile's first frames from disassembly instead
of measuring them. Shape: `watch-call <dur> <object> <Fn> [Name=value...]`,
const or pure functions with plain outputs only (the `each` guard),
sampled inside the agent like `watch-start`, with the same summary. Pull forward
when a second session needs a short-lived value behind a getter.

### 25 · `entries` over an unreflected engine container

Native members such as a curve table's row map are a `TMap` with no
reflected property, so `entries` cannot page them and a session walked the
sparse array with `peek`. Shape: `entries <addr> --set <stride>` or
`--map <key>:<value>`, reusing the existing layout checks
(`ContainerLayoutValid`) on caller-given sizes. Pull forward with #22 when
a container is the question again.

### 26 · Longer `search text`

`search text` takes at most 32 UTF-16 characters, which refused a whole
console-variable name; a prefix worked. Raise the limit or name the
workaround in the refusal when the request block allows it.

### 27 · Triggered capture

A timed capture can expire before the user does the step it is for.
Shape: `watch-start --when <range> changes`, with a short ring buffer of
rows from before the trigger, and stop reasons that name the limit to
raise. Pull forward when a capture misses its moment again.

### 28 · Function facts in one call

Bounds, callers, callees, strings, imports and the member offsets one
function uses, in one console verb built from existing pieces (`disasm
--function`, `xrefs`, `touches`). Inside the [fast-lane](design.md#fast-lane)
fence: facts about one address, no summary of what it does. Pull forward
when sessions keep making four to six calls for one function.

### 29 · CI and a fake-engine target

No CI runs the tests, and the Unreal agent handlers have no automated
tests against engine data (the Unreal example's test loads its DLL with
no engine). Shape: a Windows build and `ctest` on every push; a small test
process with a synthetic object table and name pool so `find`, `get`
and `each` run end to end; prebuilt console and own-thread
binaries.

### 31 · Finding a function again after a patch

Match a function across builds by body hash, then by a hash of its calls
and strings. It sweeps over functions, which the fast lane excludes:
build it as a script over the Python client, not a verb. Pull forward
when a patch moves many hand-made signatures at once.

### 32 · `where <object>`: place and net relevance in one call

"How far is this from me, and why is it missing on a client?" took four
calls and hand arithmetic in one session (two root locations, the net
role, the class default object's `bAlwaysRelevant` and
`NetCullDistanceSquared`). Shape: an Unreal pack verb returning world
location, distance from `--from` (default the host's `pawn` root), `Role`
and `RemoteRole`, relevance and cull distance in metres; location from the
root component's fields, no engine call. Pull forward when a second
investigation asks the same.

### 33 · Layout profile loading

The agent reads `StockUE5` or a compiled-in SDK; it cannot load a layout,
so an engine that is not stock gets refusals for the groups that fail
([without an SDK](commands/unreal.md#without-an-sdk)) even after the
skill's investigation has found the right offsets. Shape: the agent loads
a profile in `ProfileJson`'s format (`engine_layout.h`, which already
writes one), checks every group on the same known objects before using it,
and reports each group's source (`stock`, `sdk`, `profile`) and state in
`status`; a failed check keeps the group off, never a guess. This fills the
gap between T2 and T4 ([tiers](tiers.md#the-gap)). Pull forward with the
first engine whose groups fail where the skill found the offsets.

### 34 · `offset-find`: which offsets hold known values

`offset-find <object>... --<type> <value>...`: which offsets in each
object hold the given values, and which agree across all of them. Core,
plain guarded reads, game-agnostic: a dumper's offset finder as one verb.
Pull forward when finding a layout group by hand (the skill's new-game
work) shows batched read probing as the slow part, or with #33.

### 35 · A `tier` key in `status`

`status` prints `packs` and `host_thread` but no tier, so an agent reads
its tier off several keys and a first Unreal command
([tiers](tiers.md#read-your-tier-from-status)); an SDK host (T4) has no
signal but real serials in refs. Shape: `tier` (`T0`..`T4`) and the
layout's source (`stock`, `sdk`, later `profile`), with the failed group
when one refused. Pull forward when an agent misreads its tier, or with
idea 33, which needs the layout source in `status` anyway.

## Done

- **36 · `game-thread status` as a read:** `status` carries the mode as
  `game_thread`; `game-thread` only arms and removes the tick.
- **18 · Best-effort pack reads off the engine's thread:** shipped behind
  the host opt-in `Context::offThreadReads`, on in `examples/unreal` and
  used by library hosts; the list of what answers is in
  [unreal](commands/unreal.md#off-the-engines-thread), the decision in
  [design](design.md#unreal-pack).
- **30 · Fields without an SDK:** every op runs with no SDK, each layout
  group checked on known objects
  ([design](design.md#without-an-sdk-the-stock-layout-checked-per-group-on-known-objects)).

## Not ideas

Deliberately out of scope, see [design](design.md#not-now): other transports,
async client calls, plugin DLLs, other engines before a project needs them.
Also drawing of any kind (a draw list, an overlay, a frame hook to draw
in): the core reads, writes and calls, and stays free of renderers. The
opt-in Draw tick (`reagentlive::ue-draw`) only borrows Draw as a
game-thread tick source and draws nothing. A tool that wants visuals draws
in its own window ([positioning](positioning.md#what-it-is-not)).
Also more `search` modes or filters: it finds addresses that hold a known
value, and that is its whole job. Narrowing by change over one range is
`mem-diff` ([memory](commands/memory.md#mem-snap-and-mem-diff)); anything
beyond that (the whole process, tolerances, pointer scans) is a memory
scanner's ([positioning](positioning.md#pairings)).
Also sweeps over the image (every vtable, every function, tables grouped or
named from nearby strings): the fast lane answers one address at a time,
and a sweep is a script over the Python client or the static tool's job
([design](design.md#fast-lane)).
Also running scripts inside the target (an `eval`): a fixed command set
keeps refusals, approvals and output shapes documented; scripts stay
outside, in the Python client.
Also "what is the camera looking at": it only means something while a
person points the camera, so it cannot be checked without one; `census`,
`find --cls` and the host's roots are the way in.
Also a census that marks which instances are new: it needs per-instance state
in the agent for a question `census --diff` plus `find --cls` answers.
Also showing a function's first instructions inside `touches`: `disasm` on
the row is one more command, and the result stays small.
Also saved, named layouts or a type system: `label` names members one by
one; anything richer is the static tool's.
Also renaming result keys so they can be guessed: the keys are documented
and the Python client hides them; a rename moves the problem.
Also reloading the host without restarting the game: the image stays loaded
by the host's rule, not a ReAgentLive matter.

