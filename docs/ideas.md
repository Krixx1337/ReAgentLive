# ReAgentLive backlog

What may come next, each waiting for the trigger it names, and what is
deliberately out. Numbers are stable ids, not ranks. Decisions behind the
"no" answers: [design](design.md).

## Pull forward when an investigation needs it

### 6 · Bulk read paging

`read-batch` covers same-tick sampling of related ranges. Revisit paging
when an investigation needs a larger read budget across ticks.

### 2 · Class-wide snapshot and compare

`each` reads up to 16 fields or one getter across a class. Whole-object
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

### 18 · Best-effort pack reads off the engine's thread

On a host that ticks on a borrowed thread, let the Unreal pack answer
reads while still refusing calls. **Built behind the opt-in for measuring**
(`Context::offThreadReads`, [unreal](commands/unreal.md#off-the-engines-thread));
it moves to [design](design.md) only if the measurement below is clean,
and is removed otherwise.

- Opt-in per host, off by default. Lookups and reads only: `find`,
  `describe`, `fields`, object range reads, container entries, names.
  Never `call`, never `each` with a function, never anything that runs
  engine code.
- Every handler runs under a fault guard off the engine's thread, so a
  freed page is a refusal, not a crash; the object's identity is checked
  before and after the read, and a mismatch discards the result.
- Results carry `tick_accurate: false` and may still be torn (a value can
  change between two fields of one read); the docs say so.
- **Measure first:** walking the object table and reflection data while
  the engine's thread runs has not been tried. Run the read handlers
  off-thread in a live session under load (level change, garbage
  collection) and record faults, refusals and wrong answers. If that is not
  clean, this stays out.

### 19 · Checks for native members (`label --verify`)

Waits for the first game patch that moves a labelled offset. Native lines
already go `stale` under another image timestamp; this makes re-confirming
one command, as `layout-check` already does for reflected fields. An optional check per line (`--check finite`, `--check range
a..b`, or `--check equals <Owner:Getter>`, a pure getter returning the same
value); `label --verify <Class>` runs them on a live instance and, when all
pass, rewrites the timestamp. Tests: a check passing and one failing.

### 20 · Example client and SDK guide

What else lets someone host the core and drive it from outside
([outside programs](outside-programs.md)); the host checklist, client
guide and own-thread example host already exist ([hosting](hosting.md),
[clients](client.md)). Build it on the first of two triggers: a public
release, or ReAgentLive wanted in a second Unreal game.

- **Minimal client** (`examples/minimal_client.cpp`): attaches, reads a
  value, holds one with a pin, exits cleanly; built and run by the tests
  against the own-thread host.
- **"Bring your own SDK" guide** (docs only): which generated SDK headers
  the Unreal pack needs, which UE5 layout constants to verify in a new
  game, and how to verify each with the memory commands. Finding a tick on
  the engine's thread stays the host author's work.
- Done when: the docs alone are enough to write a client and to host the
  Unreal pack in another game.

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

`watch` samples fixed address ranges, so state that lives briefly inside a
manager object (a projectile in a subsystem, reached through a getter that
takes a handle) cannot be followed: the value has no stable address, and a
console round trip per sample is slower than the object's lifetime. A
session had to infer a projectile's first frames from disassembly instead
of measuring them. Shape: `watch-call <dur> <object> <Fn> [Name=value...]`,
const or pure functions with plain outputs only (the `each` guard),
sampled inside the agent like `watch`, with the same summary. Pull forward
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
tests. Shape: a Windows build and `ctest` on every push; a small test
process with a synthetic object table and name pool so `find`, `fields`,
`dump` and `each` run end to end; prebuilt console and own-thread
binaries. Comes before #30.

### 30 · An Unreal pack that needs no SDK

The pack names SDK types about 150 times, so it is compiled per game.
Replacing them with a layout table found and checked at start (as the
object table and name pool already are) gives `find`, `fields`, `dump`,
`get` and `entries` in any Unreal game from one prebuilt DLL, with no
dump and no mod; calls still need a host on the engine's thread
([outside programs](outside-programs.md)). Needs #18 measured clean and
#29 for tests. Weeks of work.

### 31 · Finding a function again after a patch

Match a function across builds by body hash, then by a hash of its calls
and strings. It sweeps over functions, which the fast lane excludes:
build it as a script over the Python client, not a verb. Pull forward
when a patch moves many hand-made signatures at once.

## Not ideas

Deliberately out of scope, see [design](design.md#not-now): other transports,
async client calls, plugin DLLs, other engines before a project needs them.
Also drawing of any kind (a draw list, an overlay, a frame hook): the core
reads, writes and calls, and stays free of hooks and renderers. A tool that
wants visuals draws in its own window
([positioning](positioning.md#what-it-is-not)).
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
in the agent for a question `census-diff` plus `find --cls` answers.
Also showing a function's first instructions inside `touches`: `disasm` on
the row is one more command, and the result stays small.
Also saved, named layouts or a type system: `label` names members one by
one; anything richer is the static tool's.
Also renaming result keys so they can be guessed: the keys are documented
and the Python client hides them; a rename moves the problem.
Also reloading the host without restarting the game: the image stays loaded
by the host's rule, not a ReAgentLive matter.
