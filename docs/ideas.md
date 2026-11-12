# ReAgentLive backlog

What may come next, each waiting for the trigger it names, and what is
deliberately out. Numbers are stable ids, not ranks. Decisions behind the
"no" answers: [design](design.md).

## Pull forward when an investigation needs it

### 6 · Bulk read paging

`read-batch` covers same-tick sampling of related ranges. Revisit paging
when an investigation needs a larger read budget across ticks.

### 2 · Class-wide snapshot and compare

`each` compares one field or getter across a class. Whole-object snapshots
across instances (every reflected field) remain open.

### 11 · A smaller default tool set

The MCP tools ([clients](client.md#mcp-tools)) serve every verb; with the
Unreal pack their schemas are about 110 KB. When a client that loads every
schema up front runs short of context: serve core lookups by default and
the rest on request (an adapter flag naming origins or verbs).

### 15 · Call probe (caution)

Log who calls one function, and with what arguments, for a few seconds: the
one question `xrefs` (static callers) and `watch` (what changed) cannot
answer. Parked: no investigation has been blocked on it yet. A neighbour
having the feature is not a case; wait for a real one the neighbours
([positioning](positioning.md#pairings)) cannot cover.

**Try a host op first.** A host that already owns a hook on the engine's
event dispatch can record the calls passing through it and expose them as
a host op, beside its other game-specific ops. No new hook anywhere, a
correct parameter block for every call, and ReAgentLive itself stays
hook-free.

**If ReAgentLive ever builds one, these rules hold:**

- The core stays hook-free. A probe lives in its own optional part, built
  only when a host asks for it and off unless the host opts in, with its
  own op range.
- Every probe is a guarded write: shown by a dry run first (address,
  current value, new value), under the host's approval rule, with a
  deadline, a target identity it stops with, and an exact restore, as
  trial writes have. A newer agent restores an older one's probes.
- Never another owner's slot: refused when the slot does not hold the
  value the engine pack or the image says is original.
- Record only. No argument edits, skipped calls or changed returns.

**What a design must settle first (Unreal; from reading, not verified
live):** a considered approach was swapping a reflected function's native
entry pointer for a recording thunk. That pointer is data, so no code
bytes change, but it is still a hook, and it sees less than it seems:

- calls through event dispatch and script calling a native function go
  through the pointer;
- script calling a script function may bypass the per-function pointer;
- native code calling native code never touches it;
- the arguments sit in a parameter block only on the event-dispatch path.
  When script calls a native function they are still unevaluated bytecode,
  so copying "the parameter block" there records the caller's locals:
  wrong data that looks right.

Confirm each path live before choosing where to record.

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
reads while still refusing calls. Wait for a real request.

- Opt-in per host, off by default. Lookups and reads only: `find`,
  `describe`, `fields`, object range reads, container entries, names.
  Never `call`, never `each` with a function, never anything that runs
  engine code.
- Every read already goes through a guarded copy, so a freed page is a
  refusal, not a crash; the object's identity is checked before and after
  the read, and a mismatch discards the result.
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
one command. An optional check per line (`--check finite`, `--check range
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

The core is engine-agnostic; engine knowledge lives in a pack (op range
256-4095, its own agent handlers and client calls), as the Unreal pack
does. Natural candidates: Unity (Mono and IL2CPP) and Source 2, each with
its own object and type system to walk. Build one only for a real project
in that engine ([design](design.md#not-now)); no pack is promised.

- **What a pack adds:** object lookup, reflected fields, calls on the
  engine's thread, and the console verbs over them. Memory, the fast lane
  and native objects already work in any engine.
- **What it needs from the host:** a tick on the engine's thread and an
  object check, as the Unreal pack does ([hosting](hosting.md#host-checklist)).

### 16 · Narrowing scan

Changed, unchanged, increased or decreased since the last pass over a
range, aligned to the tick. A memory scanner does this from outside; build
it only for a case where the outside tool is not usable or its timing is
not good enough.

## Not ideas

Deliberately out of scope, see [design](design.md#not-now): other transports,
async client calls, plugin DLLs, other engines before a project needs them.
Also drawing of any kind (a draw list, an overlay, a frame hook): the core
reads, writes and calls, and stays free of hooks and renderers. A tool that
wants visuals draws in its own window
([positioning](positioning.md#what-it-is-not)).
Also more `search` modes or filters: it finds addresses that hold a known
value, and that is its whole job. Narrowing by change is idea 16; anything
beyond that is a memory scanner's
([positioning](positioning.md#pairings)).
Also sweeps over the image (every vtable, every function, tables grouped or
named from nearby strings): the fast lane answers one address at a time,
and a sweep is a script over the Python client or the static tool's job
([design](design.md#fast-lane)).
Also running scripts inside the target (an `eval`): a fixed command set
keeps refusals, approvals and output shapes documented; scripts stay
outside, in the Python client.
Also "what is the camera looking at": it only means something while a
person points the camera, so it cannot be checked without one; `census`,
`find --class` and the host's roots are the way in.
Also a census that marks which instances are new: it needs per-instance state
in the agent for a question `census-diff` plus `find --class` answers.
Also showing a function's first instructions inside `touches`: `disasm` on
the row is one more command, and the result stays small.
Also saved, named layouts or a type system: `label` names members one by
one; anything richer is the static tool's.
Also renaming result keys so they can be guessed: the keys are documented
and the Python client hides them; a rename moves the problem.
Also reloading the host without restarting the game: the image stays loaded
by the host's rule, not a ReAgentLive matter.
