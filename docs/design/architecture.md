# Design: architecture

*Home of:* ReAgentLive's place among other tools, its layers and frontends, and why the source is laid out by layer with one folder per pack; with the rejected alternatives.

**TL;DR:** What ReAgentLive builds and what it borrows from other tools, how the
layers stack from the agent to the frontends, and why the source is
grouped by layer. Part of [design](README.md).

## Place among other tools

ReAgentLive overlaps general tools at its base and should not grow further into
their ground. Its raw memory commands repeat what a memory scanner does, and
its Unreal lookups repeat what an engine scripting bridge does. Those layers
were cheap and carry the parts no other tool has:

- **In the host's own image.** No second injector, debugger or hook set; it
  reuses the host's thread and object checks.
- **Tick accuracy.** Same-tick reads, per-tick `watch-start`, hold, and `each` over
  every instance in one tick. A reader outside the process sees torn values.
- **Identity and guards.** Refs that go stale safely, guarded holds, writes
  that refuse reused memory.
- **Reflected and native memory in one tool**, plus the host's own ops.
- **Shaped for an AI's costs:** few commands, each doing a whole lookup, with
  filtered output.

**Build or borrow.** A feature is built here when it needs at least one of:
the host's thread, tick timing, object identity, or host knowledge; **or
when it reads live code an agent should not wait on whole-program analysis
for** (the [fast lane](native.md#fast-lane)). What another tool already does well is
borrowed:

| Need | Borrow from | Not built here because |
|---|---|---|
| What writes or reads an address (data breakpoints) | nothing: debuggers are out of scope | needs debug registers and an exception handler in the target. Reach the answer in steps instead: the static tool for code that touches the field's offset, `watch-start` for the tick a value changes on, `write` with `undo` or `hold` to tell a value recomputed every tick from one set once, and `script` or `name-refs` for Blueprint readers |
| Narrowing scans over the whole process, pointer scans | a memory scanner | large, and needs no tick or identity. A bounded one over one range (`mem-snap`, `mem-diff`) is built: an AI agent cannot drive the scanner's window, and the range is usually an object it already found |
| Tracing native code, direct C++ calls, code hooks | a dynamic instrumentation tool | code patches in the target; the core writes none. Calls through one pointer are covered by pointer swaps: reflected calls by `calls-start` (one `UFunction` pointer), virtual and pointer dispatch by `native-calls-start` (one vtable entry in a private copy, or one writable cell) |
| What is at one address, what points at it, what uses a member offset, whether a signature holds there | nothing: the [fast lane](native.md#fast-lane) answers from live bytes | a static tool needs its whole-program analysis first, which takes hours on a large image and again after every patch |
| Decompiling, types, whole-program call graphs | a static analysis tool, fed by fast-lane saves (`disasm`'s `save` with `callees`) | it has the decompiler and the types; ReAgentLive hands it the live functions it needs in seconds |

**Be a good neighbour, not a rival.** Hand addresses over in a form other
tools take (`module` and `rva`) and accept them back; that is the whole
integration surface. No link between the tools: the agent driving them is
the bridge. A tool that only reads from outside is harmless beside it; one
that hooks or injects can clash with the host's own hooks, and a debugger is
not paired at all.

## Target architecture

```text
     Python scripts        MCP tools           (one console per call)
            └──── JSON calls, verb table ────┐
     a shell's -c, JSON calls ───────────────┤
                                     console (frontends/cli): the command layer
                                             │ typed C++ calls, std::expected results
     your C++ code ──────────────► reagentlive::client  (+ Unreal client calls)
                                             │ shared memory
   ── process boundary ──────────────────────┼──────────────────────
                                   reagentlive::agent   dispatch + core handlers
                                   reagentlive::ue-base Unreal handlers, stock layout checked
                                     (reagentlive::ue   the same, a host's SDK as the layout)
                                   reagentlive::ue-draw opt-in game-thread tick through Draw
                                   host glue            Tick() from the thread it states
```

The folders and what each layer may include: [contributing](../contributing.md#layers).

## Frontends

The console is the command layer: `get`, `batch`, `entries` and the cache
exist only there. The console and C++ callers sit on the client API, never
on the section; every other frontend goes through the console's JSON calls,
as the Python client does ([clients](../client.md)). A new console command
ships with its row in the verb table; its Python function and its tool are
made from it. The IDA side reads and writes files and never talks to the
agent: the shared labels file is the link.

## Source layout

The layout itself and its rules are in
[contributing](../contributing.md#source-layout).

- **`frontends/cli/` is grouped by layer** (call, values, state, results,
  code, console), each with one include rule a test checks.
- **Each pack is one folder, `packs/<id>/`**: its agent and client files,
  its `pack.cmake` and `cli/<layer>/`. Nothing outside it names the pack,
  and no pack includes another. A reader sees at a glance what works in any
  process, and a core change cannot lean on Unreal types by accident: the
  layout test fails. Object identity (`Ref`) is core, since refs, holds and
  watches carry it whatever the engine.
- **The core reaches the pack through plain functions** (`console/pack.h`):
  its state, cache block, its rows of the
  [verb table](../tool-interface.md#self-describing-console), `status` line,
  commands, and what core commands borrow. With one pack they need no
  interface.
- **A console without the pack is a build of it** (`REAGENTLIVE_PACKS`
  empty, [hosting](../hosting.md#using-it-in-your-project)).
  `console/no_pack.cpp` answers the seams for "no pack"; the core files take
  no `#if`. Its table holds core and host rows only, and `help`, the
  generated functions and the MCP tools all read the table, so none needs a
  switch. The tests build this console beside the default one: a core file
  that leans on the pack no longer compiles, which the layout test only
  approximates from `#include` lines.
- **Adding a pack is mechanical; several in one console is not built.** A
  new folder, its `pack.cmake`, the next op slot
  ([protocol](../protocol.md#ops)) and an entry in `REAGENTLIVE_PACKS`; a
  console still carries one pack and configure fails on two. A second pack
  would turn the `console/pack.h` seams into a table and move
  Unreal-shaped fields out of core types.

Rejected:

| Option | Why not |
|---|---|
| Feature folders (`memory/`, `unreal/`, each with parser, logic and console) | commands cross areas, and the CMake targets and the no-decoder rule would be split across every folder |
| The pack's part as a `ue/` folder in each layer | a pack spread over six folders has no single home |
| A flag on the MCP adapter or generator for a pack-less console | `help` would still list the verbs, and the module would no longer match the table it was made from |
| A run-time console switch | every reader would have to pass it alike or the table hash differs, and the pack stays linked for nothing |
| Hiding tools by what the live host offers | the tool list is known before a target runs |
| A pack interface or registry now | designed around one pack it would guess the shape a second needs; the seams list what that table must hold |
| `add_subdirectory` per pack | its variables, the CLI source lists the tests build from, would stay in the pack's scope |
| Renaming the pack `unreal` | the namespace, targets and `status` id are `ue`; one name everywhere |
