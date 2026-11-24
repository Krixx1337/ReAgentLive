# ReAgentLive backlog

*Home of:* the backlog: ideas waiting for a named trigger, and what is deliberately out.

**TL;DR:** Features that may come next, each with the event that would
justify building it. Check here before proposing something: it may be
waiting or ruled out on purpose. Built ideas are deleted, not kept as
history. Decisions behind the "no" answers: [design](design/README.md).

## Pull forward when an investigation needs it

### 6 · Bulk read paging

Image bytes already have a 4 MB window ([design](design/native.md#fast-lane)).
Page values (heap, data) when an investigation needs a larger read budget
across ticks.

### 2 · Class-wide snapshot and compare

`each` reads a few fields across a class. Whole-object snapshots across
instances (every reflected field) remain open.

### 17 · `call-native` (caution)

Call a native function by address, or an object's vtable slot, with
integer or pointer arguments. Build it on the first trigger: a live
investigation needs a call a read cannot replace, or an outside program
runs on a game with no engine pack. It is the one native-object feature
that can crash the target.

- Core op, off unless the host opts in (`Host::nativeCalls`); a host
  enables it only where `Tick` runs on a thread safe for engine calls.
- Always a `dry` form first (resolved target, arguments, first
  instructions) as the text for the approval request.
- Only executable pages in loaded modules; an access fault is caught and
  reported as a possibly bad target state.

### 19 · Checks for native members (`member-name` verify)

Waits for the first game patch that moves a labelled offset. Native lines
already go `stale`; this adds an optional check per line so re-confirming
is one command, as `layout-check` is for reflected fields.

### 20 · Example client

A small client example (attach, read, pin, exit) so the docs plus one file
are enough to write a client ([clients](client.md)). Build it on a public
release.

### 21 · Other engine packs (Unity, Source 2)

The core is engine-agnostic; a pack adds object lookup, reflected fields
and engine-thread calls ([hosting](hosting.md#host-checklist)). Build one
only for a real project in that engine
([design](design/principles.md#not-now)).

### 22 · `entries` through a struct path

`entries` takes one field, so a container inside a struct member (a
curve's keys) is out of reach. Pull forward when that is the question
again: take `get`'s paths and page the container the path ends at.

### 23 · `each` over the last instance list

`each` costs a scan of the object table. A later `each` of the same class
could reuse the agent's instance list, at the price of missing new
instances. Pull forward when a session repeats `each` on one class: an
explicit flag, never a silent reuse.

### 24 · `watch-call`: a pure getter sampled every tick

`watch-start` samples fixed ranges, so short-lived state behind a getter
(a projectile in a subsystem) cannot be followed. Sample a const or pure
function inside the agent (the `call-each` guard) with the same summary.
Pull forward when a second session needs it.

### 25 · `entries` over an unreflected engine container

Native `TMap`/set members have no reflected property, so `entries` cannot
page them. Accept caller-given sizes, reusing the container layout checks.
Pull forward with #22.

### 26 · Triggered capture

A timed capture can expire before the user does the step. Add a trigger on
`watch-start` with a short pre-trigger ring. Pull forward when a capture
misses its moment again.

### 27 · Function facts in one call

Bounds, callers, callees, strings and member offsets of one function in one
verb built from existing pieces. Stays inside the
[fast-lane](design/native.md#fast-lane) fence: facts, no summary. Pull
forward when sessions keep making four to six calls per function.

### 28 · CI and a fake-engine target

No CI runs the tests, and the Unreal handlers have no automated tests
against engine data. Add a Windows build with `ctest` on every push and a
test process with a synthetic object table and name pool.

### 29 · Finding a function again after a patch

Match a function across builds by body hash, then by calls and strings.
It sweeps over functions, which the fast lane excludes: build it as a
script over the Python client, not a verb. Pull forward when a patch moves
many signatures at once.

### 30 · `where <object>`: place and net relevance in one call

Distance from a `from` object, net role, relevance and cull distance in
one Unreal verb, from root component fields with no engine call. Pull
forward when a second investigation asks the same.

### 33 · Layout profile loading

The agent reads `StockUE5` or a compiled-in SDK and cannot load a layout,
so a non-stock engine gets refusals for groups that fail
([without an SDK](commands/unreal.md#without-an-sdk)) even after the right
offsets are found. Load a profile in `ProfileJson`'s format, check every
group on known objects first, and report each group's source and state
in `status`; a failed check keeps the group off. Fills the gap between T2
and T4 ([tiers](tiers.md#the-gap)). Same cluster, built together:

- `offset-find`: given objects and typed values, which offsets hold them
  and agree across objects. Core, plain guarded reads.
- A `tier` key in `status` ([tiers](tiers.md#read-your-tier-from-status)),
  with the layout's source and the failed group.

Pull forward with the first engine whose groups fail where the skill found
the offsets.

### 37 · The skill as a Claude Code plugin

Ship a marketplace file so `/plugin marketplace add <clone>` installs the
skill in place, as a fourth setup option. Lessons already live outside any
install, so a cached plugin copy is safe. Pull forward when a user asks.

### 38 · Two requests in flight (channels)

Clients share one request slot in short turns
([design](design/console-costs.md#round-trips)); a long sliced `search` or
held `batch` still makes the others wait. Two steps, each useful alone:
a per-request context instead of handler globals, then N channels served
round-robin on the one tick thread, sharing one tick and hold budget.
Protocol change. Pull forward when `usage` shows interactive calls waiting
on the slot (`slot_ms`).

## Not ideas

Out of scope, with the reasoning in [design](design/principles.md#not-now):
other transports, async client calls, plugin DLLs, other engines before a
project needs them. Also:

- Drawing of any kind: the core reads, writes and calls. A tool that wants
  visuals draws in its own window ([positioning](positioning.md#what-it-is-not)).
- More `search` modes: whole-process scans, tolerances and pointer scans are
  a memory scanner's ([positioning](positioning.md#pairings)); change over
  one range is `mem-diff`.
- Sweeps over the image: the fast lane answers one address at a time; a
  sweep is a Python script or the static tool's job.
- Scripts inside the target (`eval`): a fixed command set keeps refusals
  and output shapes documented.
- "What is the camera looking at": meaningless without a person pointing
  it; use `census`, `find` by `cls` and the host's roots.
- A census marking new instances: `census`'s `diff` plus `find` answers it.
- First instructions inside `touches`: `disasm` on the row is one command.
- Saved, named layouts or a type system: `member-name` names members one by
  one; anything richer is the static tool's.
- Renaming result keys so they can be guessed: the keys are documented and
  the Python client hides them.
