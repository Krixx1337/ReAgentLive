# ReAgentLive design

Why ReAgentLive is built the way it is: principles, decisions with their
rejected alternatives, and what is deliberately not built. Current
behaviour lives in the [command pages](INDEX.md#commands),
[protocol](protocol.md) and [hosting](hosting.md); the ranked backlog in
[ideas](ideas.md). Anything marked *planned* is a proposal.

## Principles

- **The core never hooks, and starts no threads unless the host opts in.**
  The host calls `Tick()` from a thread it trusts. The one exception is
  `search --background`, allowed only by `Host::backgroundSearch`: a single
  worker thread that reads memory (no engine calls) while the host tick
  only polls.
- **Extensible by static tables, not plugins:** agent handlers and CLI verbs
  are constant arrays; op ranges separate core, packs and games.
- **Interface-agnostic:** a C++ client API sits between the
  transport and every frontend. CLI, GUI, MCP and plain C++ callers use typed
  calls; none touch the wire protocol.
- **One transport** (shared memory). No transport interface until a second
  real transport exists.
- **Grow on demand:** a pack, frontend or feature is added when a real
  project needs it.

## Round trips

AI agents drive ReAgentLive one tool call at a time and pay for every call and
every byte of output; inside a command, each request waits for a host tick.
So the order of work is fewer tool calls (state kept between runs, commands
that do a whole lookup), then less output (filters), then fewer and cheaper
requests inside a command.

- **Hold, not a packed batch op.** A request may say more follow; the agent
  waits a bounded time on the host's thread and runs the next one in the same
  tick. A packed op was rejected: sub-requests need the 128-byte `text`
  field, all results would share one 4 KB response, and it cannot do
  dependent reads. Hold needs no packing format and lets compound commands
  chain reads. Same tick is best effort; every response carries its tick so a
  miss is visible. The per-tick budget bounds the stall the host pays.
- **No fast lane for memory reads on a worker thread.** Reads off the tick
  would be torn against the game; a second request slot helps no one while
  every frontend is synchronous; `watch` already samples every tick.
- **No `ReadProcessMemory` from the client.** Fastest for raw reads, but the
  client would open the target process, which the design avoids.
- **No session daemon or several clients.** The console's cache file gives
  one-shot runs their state (class refs, field tables, `$N`, exe module),
  tied to one agent instance by `Section::owner`. Field tables are trusted
  only when page 0 of the live table matches, so a cache never shows a stale
  layout.
- **Client waits spin, then sleep at 1 ms** with the timer resolution raised
  in the client process only. Polling with `Sleep(1)` at the default 15.6 ms
  timer made every round trip cost 15.5 ms against a 10 ms host tick; now a
  read costs one tick. Held requests spin up to 50 ms at raised thread
  priority: a client that slept through one response missed the agent's
  short wait for every later request too, so held batches ran either in one
  tick or one tick per request.
- **Not built, measured unneeded:** larger responses (a large `dump`'s
  requests are pointer lookups and array previews, not 4 KB paging) and a
  wake-up event (a read already costs one host tick).
- **Host logging counts against the hold budget.** A host that logs every
  successful lookup synchronously gets one held request per tick; suppress
  success logs for read-only ops.

## Output weight and dead ends

From one long live session (an unreflected block found, decoded and proved
on live samples), where time and context went for no gain.

- **Compact values by default.** A struct prints as `{Name: value}`; full
  member metadata made up most of nearly every command's output and was
  flattened by hand per call. One rule for every struct, vectors included:
  a shorter special case for vectors was rejected. `--meta` restores the
  layout; `fields` keeps it, being the layout command.
- **Calls explain themselves.** An outputs-only call gets a zeroed block;
  refusals name the parameter and the reason in terms of it. A message that
  reads as true of the input it refuses ("parameters are plain outputs:
  ReturnValue", for a function whose only parameter is ReturnValue) cost a
  detour.
- **Plain is "nothing to destroy, zero is a valid start"** (`NoDestructor`
  and `ZeroConstructor`), on the property or, where it lacks a flag, on
  every member of its struct. The earlier `IsPlainOldData && NoDestructor`
  refused gameplay tags, which own nothing; a hand-kept list of struct
  names was rejected.
- **Census pages slice the stored census.** A rescan per page would shift
  rows between pages as counts change; the last census (with samples) and
  the one before it are kept in the session and cache file instead, so the
  pages of one filter join. `--limit 0` stays for all rows at once.
- **A batch says which command moved its tick.** `same_tick` alone did not
  stop a skewed reading; per-command ticks and `spans_ticks` come from the
  client's hold, no agent change.
- **Native names in their own file.** The readable-names file is generated
  by the host and would wipe appended lines, so `label` writes a hand-owned
  `<console>.native.txt`. Reflected names follow their field across patches;
  native offsets do not, so each line carries the main image's PE
  timestamp and shows `stale` under another one. Typed layouts share one
  syntax with `read --as`, so a decoded layout pastes into `label`.
- **A capture that does not hold the console**, without a second channel:
  the capture belongs to the agent instance (`watch --start`, `--read`,
  `--stop`), bounded by the existing buffer, so a long capture and other
  questions overlap. Following a replaced object asks the host: the core
  knows no names, a host offers roots through one optional callback.

## Place among other tools

ReAgentLive overlaps general tools at its base and should not grow further into
their ground. Its raw memory commands repeat what a memory scanner does, and
its Unreal lookups repeat what an engine scripting bridge does. Those layers
were cheap and carry the parts no other tool has:

- **In the host's own image.** No second injector, debugger or hook set; it
  reuses the host's thread and object checks.
- **Tick accuracy.** Same-tick reads, per-tick `watch`, hold, and `each` over
  every instance in one tick. A reader outside the process sees torn values.
- **Identity and guards.** Refs that go stale safely, guarded pins, writes
  that refuse reused memory.
- **Reflected and native memory in one tool**, plus the host's own ops.
- **Shaped for an AI's costs:** few commands, each doing a whole lookup, with
  filtered output.

**Build or borrow.** A feature is built here only when it needs at least one
of: the host's thread, tick timing, object identity, or host knowledge;
**or when it is a local question about one address that should not wait for
whole-program analysis** (the [fast lane](#fast-lane)). Anything else is
borrowed from the tool that already does it:

| Need | Borrow from | Not built here because |
|---|---|---|
| What writes or reads an address (data breakpoints) | nothing: debuggers are out of scope | needs debug registers and an exception handler in the target. Reach the answer in steps instead: the static tool for code that touches the field's offset, `watch` for the tick a value changes on, a trial write or `pin` to tell a value recomputed every tick from one set once, and `func --script` or `refs` for Blueprint readers |
| Narrowing scans (changed, unchanged, increased), pointer scans | a memory scanner | large, and needs no tick or identity |
| Function tracing and hooking | a dynamic instrumentation tool | hooks in the target; the core never hooks |
| What is at one address, what points at it, whether a signature holds there | nothing: the [fast lane](#fast-lane) answers from live bytes | a static tool needs its whole-program analysis first, which takes hours on a large image and again after every patch |
| Call graphs, whole-function logic, decompiling, types, naming | a static analysis tool | it has the whole image, names and types |

- **Be a good neighbour, not a rival.** Hand addresses over in a form other
  tools take (`module` and `rva`) and accept them back; that is ReAgentLive's
  whole integration surface. No link between the tools: the agent driving
  them is the bridge.
- **No large tool surface.** A frontend with a tool per operation costs an
  agent context before it does anything; the console and its Python wrapper
  stay the way agents drive ReAgentLive.
- **Before pairing in one process:** a tool that only reads from outside is
  harmless. One that hooks or injects can clash with the host's own hooks;
  try it on a throwaway session first. A debugger is not paired at all.

This comparison rests on the other tools' published descriptions, not on
trials against a host.

## Goals and non-goals

**Goals**

- Drop the core into any Windows process that has a safe place to call
  `Tick()`.
- Add an engine through a pack and a game through host glue, without
  touching core.
- Work under any injection style: mapped image, LoadLibrary or ASI loader.

**Non-goals**

- Other platforms, network transport, protocol versioning, plugin DLLs,
  scripting languages.
- Packs for engines nobody is using yet (Unity/IL2CPP, Source, custom).
- Use against online or anti-cheat-protected games.

## Target architecture

```text
            ┌──────── frontends (any number) ─────────┐
            │  CLI      GUI      MCP     your C++ code │
            └───────────────┬─────────────────────────┘
                            │ typed C++ calls, std::expected results
               reagentlive::client        (+ Unreal client calls)
                            │
               reagentlive::channel       shared memory (client endpoint)
   ── process boundary ─────┼──────────────────────────────
               reagentlive::channel       (agent endpoint)
               reagentlive::agent         dispatch + core handlers
               reagentlive::ue            Unreal handlers
               host glue              Tick() from a safe thread
```

```text
core/              no engine, no game, no UI
  protocol.h       wire layout, op ranges, statuses                 exists
  agent.h/.cpp     Tick, dispatch, core handlers                    exists
  channel.h/.cpp   section open/attach, sequence counters           planned (now inside agent and client)
  client.h/.cpp    C++ API: attach, read, write, modules, Send      exists
ue/                Unreal over a Dumper-7 SDK
  protocol_ue.h, agent_ue.h/.cpp                                    exist
  client_ue.h/.cpp Find, Fields (paging hidden), Call               exists
frontends/
  cli/             syntax, formatting, console on the client API     exists (verb table planned)
  (gui/, mcp/ later)
```

| Layer | Job | Knows about |
|---|---|---|
| `protocol.h` | wire structs, op ranges, statuses | nothing |
| `channel` | open or attach the section, send and poll | protocol |
| `agent` | dispatch and handlers, in the target | channel, protocol |
| `client` | typed C++ API, errors, timeouts | channel, protocol |
| Unreal client calls | `Find`, `Fields`, `Call` | client |
| frontends | user input → client calls → display | client only |

## Frontends

The protocol and client API are implemented; current behaviour is in
[protocol](protocol.md) and the [README](../README.md). Frontends sit on the
client API, never on the section.

| Frontend | Shape | Notes |
|---|---|---|
| **CLI** | text REPL, `$N` refs, `base+off`, hex dump | exists, on the client API; a verb table is planned |
| **GUI** (later) | ImGui desktop window, calls from a worker thread | tables, watch panels |
| **Python** | `frontends/python/reagentlive_session.py`: starts the console and exchanges one JSON line per command | exists; a thin wrapper with no logic of its own, so every command lands in the console first |
| **Static-tool script** | `frontends/ida/import_labels.py`: imports the console's label file | exists; reads a file, never talks to the agent |
| **MCP** | not planned (see [place among other tools](#place-among-other-tools)) | if ever built, on the console's JSON mode |
| **C++ callers** | link the client | test harnesses, scripts, other tools |

The console has become the command layer: `dump`, `get`, `batch`, `entries`
and the cache exist only there. A frontend that wants those commands goes
through the console's JSON mode, as the Python client does; only a frontend
that needs raw requests links the client. A new console command ships with
its Python wrapper, or its row in [result shapes](commands/results.md)
says why it has none.

## Unreal pack

- **Compile model:** compiled inside the host against its Dumper-7 SDK
  (`UObject`, `GObjects`, `UStruct`, `FField`, `FProperty`, `UFunction`,
  `ProcessEvent`, `EClassCastFlags`). Those names are consistent across
  Dumper-7 dumps of UE4 and UE5 games, so no abstract engine interface.
- **String type:** the pack only uses `.c_str()` and `.size()` on SDK strings,
  so stock `std::string` dumps and adapted string types both compile.
- **Three thread kinds, stated by the host.** `Host::thread` is `engine`,
  `borrowed` (a thread the game runs for another purpose, such as the frame
  present) or `own`, with no default: the agent will not open until the
  host says. The core publishes it (`status`, `tick_accurate`) and the pack
  enforces the engine-call rule; neither verifies the other. On the
  engine's thread everything works and a tick is a moment of the game;
  elsewhere memory commands work, pack handlers are refused, and a tick is
  a host tick. Rejected: detecting the kind (the core cannot know which
  thread is the engine's, and a wrong guess is worse than a required
  statement); getting onto the engine's thread for the host (game-specific
  work); running engine calls off the engine's thread behind a flag
  (unsafe in a way no guard fixes); queuing requests from a borrowed thread
  to the engine's (that needs the second tick source such a host lacks).
  Best-effort pack reads off the engine's thread are in the
  [backlog](ideas.md#18--best-effort-pack-reads-off-the-engines-thread).
- **The engine-thread check is required.** Every pack handler reads engine
  state, so each asks `Context::gameThread` first. A host that leaves it
  out is refused, not trusted: a host ticking off the engine's thread with
  no check would otherwise run engine calls on the wrong thread unnoticed.
  A host on the engine's thread passes a check that says so.
- **Readable names live in the client, not the agent:** a host-supplied names
  file keyed by owner, offset, size and bit ([readable names](commands/unreal.md#readable-names)).
  Rejected: a `displayName` hook in the agent. It would cost the game thread
  and the injected image for display-only data, and offsets already identify
  a field within one build.
- **Static-tool names: borrow mappings where they exist** (seen live: the
  SDK generator's mappings named 17,652 exec functions and 21,613 types;
  `export-labels --kinds interface,global` then named 213 more, the
  generator's own static-FName naming having failed on an unanalysed
  database). Not checked: whether a full export after the mappings would
  comment rather than rename the generator's names; the docs say not to run
  one. A host whose SDK
  generator writes IDA mappings (Dumper-7) imports those first for exec
  thunks, vtables, types and engine globals; `export-labels --kinds
  interface,global` then adds only what needs the running process. ReAgentLive
  never requires or reads those mappings, and keeps every kind so it can
  name a database alone. Rejected: dropping the `exec` and `vtable` kinds
  (another tool would become a requirement).
- **Trials and a stopped host.** A replaced agent restores its active trials
  once, but only while its host still ticks it. A host that stops the agent
  before a new injection (the usual way) silences it first, so the rule is
  to clear trials before stopping. Rejected for now: restoring trials in the
  host's stop path (small, but the risk is one value until restart, and only
  inside a trial's 60 s), and an unloadable agent (it would still need that
  restore, and the host keeps its image loaded by design).
- **Known debt: the interface stub pattern decodes bytes in the agent.**
  `func` on an interface function matches one stub shape
  (`mov rax, [reg]; call [rax + disp]`, `StubVtableSlot`) inside the agent,
  against the rule that the agent reads bytes and the console decodes them.
  It stays because resolving a live object's implementation needs it in one
  request; the console's decoder ([fast lane](#fast-lane)) is where it
  would move. Any other stub shape reports a null slot.

### Light reversing

- **Scope:** ReAgentLive answers what only a running process can (the value,
  object or function at a live address, what a global holds once the engine
  filled it, which implementation a live object dispatches to). A static
  tool answers callers, references, whole functions and types. The bridge
  is `module` and `rva` both ways, `peek` for live globals, and
  `export-labels` for names.
- **No decoder in the agent:** it would grow the injected image for
  display-only work. The agent reads bytes; the console decodes them.
- **Signatures: the agent makes them, ReAgentLive verifies them.** Making one
  is judgement (which instructions are distinctive, which operands move,
  whether a caller is the better place) that an agent does from the static
  tool's disassembly without a generator. What it cannot know alone is the
  result in the image the host really scans, DLLs with no database
  included, so `sig --check` is a strict checker whose feedback names the
  next correction: `mismatches` at the expected address, `diverges_at` per
  extra hit, `warnings` for unmasked addresses (a few byte patterns, not a
  decoder), `--resolve` for reference signatures, `--file` for many at
  once, and `sig --bytes` as a start without a static tool. With a boundary
  the reader gives (`--expect`, `--bytes`) the warnings and masks come from
  the console's decoder, and the byte patterns only past where decoding
  stops. It runs in the
  console on the exact `search` (anchor) and `read-batch` (mask compare);
  the agent is unchanged. Unique means one match in the whole module image,
  stricter than a host scanner that reads only code. Rejected: a signature
  maker in the console or as a static-tool script (two earlier drafts had
  one each; the checker's feedback covers the mistakes a generator would
  prevent; revisit a script only if hand-made signatures keep needing many
  attempts), wildcards in the agent's `search` (the anchor plus a masked
  compare needs none; take it up only if a masked search over the heap, a
  struct found by shape, is wanted twice), scanning the exe on disk (the
  static tool and a host's offline verifier do that) and writing into
  host source files (the agent or the user pastes).
- **Rejected:** call graphs, decompiling, types and naming in ReAgentLive (the
  static tool has the whole image); symbol loading; ReAgentLive talking to the
  static tool directly (the agent driving both is the bridge).

### Fast lane

A static tool answers a question about one address only after analysing the
whole image: hours for a large game, and again after every patch. The fast
lane answers such local questions from live bytes in well under a second,
with no database:

| Question | Command |
|---|---|
| What are the instructions here? | `disasm` |
| What points here? | `xrefs` |
| Is this pattern unique, and does it land here? | `sig --check` |

- **The fence: these three, and nothing that needs the whole program.** No
  decompiler, types, call graphs, naming, signature maker, or sweep over
  every function or table. A fourth command qualifies only as a local,
  one-address question answered from bytes. Everything else stays with the
  static tool, reached through `module` and `rva`.
- **Why the disassembler was un-parked.** It was parked because all code read
  so far matched the file the static tool shows. That still holds; the
  reason is analysis time on large images, not live code differing from the
  file.
- **Function bounds are in** because they are a table lookup, not analysis:
  the exception directory lists every function with unwind data, sorted, and
  a lookup is a few held reads (sampled entries, then one window). No entry
  is `function: null`; a leaf function is never guessed from padding or
  prologues.
- **Sweeps are out.** Finding every vtable, grouping them, naming them from
  nearby strings is the one-address chain run thousands of times: a script
  over the Python client, or the static tool's job. Layout habits such as
  "the string sits after the table" belong to one linker, so nothing is
  built on them.
- **Where it lands.** Only the relative-reference scan behind `xrefs` is a
  core agent op (with a client call), since it reads memory only and works
  with any host. The decoder (Zydis, pinned, formatter on) links into the console alone; a configure check
  fails when an agent target links it. Kinds, bounds, string hints and
  `sig` feedback are console work.
- **Rejected:** following calls or conditional jumps in `disasm` (exploring a
  function), structured operands or struct recovery from offsets, walking
  backwards from an unrecognised reference to its instruction start (not
  reliable on x86 without analysis; `disasm --function` shows it in place),
  hooking a function to log callers (hooks stay out; `xrefs` gives the static
  callers), and the step-0 check of whether the static tool decompiles an
  unanalysed image fast enough to replace `disasm --function` (not run: the
  host's database was already analysed).

### Native objects

Much of a game is native and unreflected even in a reflected engine (value
objects behind a handle, interface parts, engine internals), and a game
with no engine pack is nothing else. The engine-agnostic side therefore
answers what a native object is from live bytes, without a type system.

- **`vtable`** lists slots until the first entry outside an executable
  section of a loaded module. The section table is read once per module,
  by the same header reader as the fast lane's function bounds, and cached.
  A table is never inferred from neighbouring bytes.
- **Pointer chains are a client library call**, so an outside program
  follows `X->a->b` the same way the console does; the console only parses
  the syntax. Writes, pins and trials refuse chains: a write goes where the
  caller read, never through pointers that may have moved since.
- **Strings** (`--as cstr|wstr`) escape what they cannot print, so a dump
  never hides a byte.
- **`region` is a core op** because only the target can ask Windows about
  its own memory; the agent passes `VirtualQuery`'s numbers through and the
  console names them. A stack is not claimed: private read-write memory is
  `private`, since telling a thread's stack from a heap needs every
  thread's limits.
- **Calls are opt-in and deferred.** `call-native` (the one item that can
  crash the target) waits for a live case a read cannot replace, or an
  outside program on a game with no engine pack
  ([backlog](ideas.md#17--call-native-caution)); a host will enable it only where engine
  calls are safe, and approval stays the host's rule. Out for good: hooks,
  tracing and breakpoints; calling by symbol name (shipping images have
  none); floating-point arguments and callbacks until a case needs them.
- **Rejected:** class names from run-time type information (engines often
  build without it; the one host checked has a few hundred type names in
  its image, none of them game classes: revisit for a host whose image has
  them), and a struct or type system in ReAgentLive (layouts come from the
  engine pack, the host's SDK or the static tool; `read --as` and the
  Python `read_struct` cover ad hoc ones).

## Source layout

The layout itself and its rules are in
[contributing](contributing.md#source-layout).

- **`frontends/cli/` is grouped by layer** (syntax, values, state, results,
  code, console), each with one include rule a test checks.
- **Rejected:** feature folders (`memory/`, `unreal/`, each with parser,
  logic and console). Commands cross areas (`dump` uses value formatting,
  state files and results), and the two CMake targets and the no-JSON rule
  would be split across every folder.

## Not now

- A transport interface (add one with a second real transport).
- Async/futures/callbacks in the client API.
- A C ABI or native Python bindings over `Client` (the Python client drives
  the console instead).
- A GUI frontend until the CLI stops being enough.
- Plugin DLLs or runtime loading of packs.
- Protocol negotiation beyond the layout hash and supported-op list.
- Packs for other engines until a real project needs one.
- Its own repository, until a second project needs it; then a git
  submodule or copied sources with a pinned revision. Tests and their
  framework already live in the folder.
