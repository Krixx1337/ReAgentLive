# ReAgentLive design

*Home of:* why ReAgentLive is built this way: principles, decisions with their rejected alternatives, and what is deliberately not built.

**TL;DR:** Read this before proposing a change: most "why not just..."
ideas were tried or weighed here, and the entry says why they lost. Each
decision states what was chosen, what was rejected and the evidence. It
describes reasons, not behaviour: what a command does lives on its command
page.

Current behaviour lives in the [command pages](INDEX.md#commands),
[protocol](protocol.md) and [hosting](hosting.md); the ranked backlog in
[ideas](ideas.md). Anything marked *planned* is a proposal.

## Principles

- **Read freely; never write code.** The line is invasiveness. Reading and
  analysing live memory of any size is in scope; hooks or patches in
  executable code and debugging (breakpoints, debug registers, exception
  handlers in the target) are out for good. Writes to data are opt-in and
  approved one by one.
- **The core never hooks, and starts no threads unless the host opts in.**
  The host calls `Tick()` from a thread it trusts. The one exception is
  the search worker, allowed only by `Host::backgroundSearch`: a single
  thread that runs search jobs and the console's internal scans (reads
  only, no engine calls) while the host tick only polls.
- **Two opt-in pointer swaps, never code.** The Unreal pack's
  `calls-start` swaps one `UFunction`'s native pointer (heap data) for a
  fixed thunk that records and forwards each call, and puts it back with a
  compare-and-swap; a pointer something else replaced since is left alone
  and the thunk keeps forwarding. A host opts in by listing the ops; each
  recording is a write the host approves. Taken because "what did the game
  pass to this function" has no read-only route, a watch sees only state,
  and the swap touches nothing the engine does not itself rewrite. The
  second is the host helper `reagentlive::ue-draw`: when `game-thread` arms it, it
  writes one pointer in the game viewport object, pointing it at a private
  copy of its interface vtable
  ([decision](#a-game-thread-tick-through-the-viewports-draw)). Code, the
  game's own vtables and imports are never written by either.
- **One safety boundary, set by a person, held by the console.** Every
  verb that writes the target or calls into it carries `writes` and sits
  in the `modify` [tool pack](tool-interface.md#tool-packs); the mark is
  exact per verb, so a read-only verb has no argument form that writes or
  calls (`each` reads; its getter form is the verb `call-each`). The
  console's `--packs` is the ceiling: a verb past it is refused (kind
  `pack`), `batch` items included, whichever frontend sent it. The MCP
  adapter and the Python session pass their ceiling on, so one check
  covers every path; the agent may enable packs inside it at run time, never
  widen it. Rejected: the guard only in the adapter (a script's session
  ran most calls and nothing stopped a `write` typed by mistake), and a
  mark that depends on arguments (a verb's mark is what a client prompts
  on, before it sees arguments).
- **Extensible by static tables, not plugins:** agent handlers and CLI verbs
  are constant arrays; op ranges separate core, packs and games.
- **One command layer:** the console. It sits on the C++ client API,
  which sits on the transport; Python and MCP drive the console's JSON
  calls, and only the console and raw C++ callers link the client. No
  frontend touches the wire protocol.
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
  every frontend is synchronous; `watch-start` already samples every tick.
- **No `ReadProcessMemory` from the client.** Fastest for raw reads, but the
  client would open the target process, which the design avoids.
- **One client at a time, kept short.** An agent takes one client. A
  console that finds it busy asks again for up to 2 s, then refuses as
  `busy`, so a script beside the MCP tools waits for the other call
  instead of failing at once.
- **No session daemon or several clients.** The console's cache file gives
  one-shot runs their state (class refs, field tables, exe module),
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
- **Not built, measured unneeded:** larger responses (a large `get`'s
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
  a shorter special case for vectors was rejected. `meta` restores the
  layout; `get` with `mode` `types` keeps it, being the layout mode.
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
  pages of one filter join. A `view.limit` of 0 stays for all rows at once.
- **`get` reads its values after resolving every path.** Reading each value
  as its path resolved put console work (field table pages, pointer hops)
  between held requests; past the agent's short wait for the next request,
  the values split over ticks. Resolving first and then reading the values
  back to back keeps the reads under that wait, so `same_tick` holds without
  raising the host's budgets. Warm field tables are shared, not copied, for
  the same reason.
- **A batch says which command moved its tick.** `same_tick` alone did not
  stop a skewed reading; per-command ticks and `spans_ticks` come from the
  client's hold, no agent change.
- **Native names in their own file.** The readable-names file is generated
  by the host and would wipe appended lines, so `label` writes a hand-owned
  `<console>.native.txt`. Reflected names follow their field across patches;
  native offsets do not, so each line carries the main image's PE
  timestamp and shows `stale` under another one. Typed layouts share one
  syntax with `read`'s `type`, so a decoded layout pastes into `label`.
- **A capture that does not hold the console**, without a second channel:
  the capture belongs to the agent instance (`watch-start`, `watch-read`,
  `watch-stop`), bounded by the existing buffer, so a long capture and other
  questions overlap. Following a replaced object asks the host: the core
  knows no names, a host offers roots through one optional callback.

From one autonomous read-only agent session through the MCP tools only,
where every question was answered and the cost was output and turns:

- **Wrong-looking answers first, then output, then turns.** A one-hop chain
  (offset 0 after an address) answered the bytes at that address with
  `hops: []`: right by the rule that the last offset only adds, and read as
  a followed pointer. It is
  refused with the form that follows one. A misspelt JSON key (`class_`)
  was reported as a left-out argument; unknown keys are now checked first
  and named with the verb's own. Refusals state their limit's number.
- **One name per argument, never a Python keyword** (`cls`, `start`,
  `type` for `class`, `from`, `as`), so a JSON call, a `batch` entry, the
  generated function and the tool spell it the same.
  Keeping the keywords and letting the generator append `_` was the cause:
  the tool said `class_`, `batch` wanted `class`. A table test refuses a
  keyword.
- **Compact rows, layout on request**, the rule values already followed.
  A `get` row (every field) is its name, offset and value (about 25 KB for one actor
  before); the offset stays because it is the bridge to raw reads, and the
  recovered readable name is the row's name, the reflected one beside it.
  A `get` row is its value and address; `path` and `ok` went because rows
  keep the order asked and a failed path keeps its place. The `batch`
  envelope kept its shape: one envelope for every command beats the bytes.
- **`each` became a table** because most questions were "these fields over
  every instance": four values over 17 objects had taken 17 `get` calls.
  The scan dominates its cost and a column adds none, so columns share
  one scan; a function stays a single column (calls are the slow case and
  their outputs do not fit one). An agent op for several spans was not
  needed: passes of 64 bytes over the cached instance list do it.
- **Typed watches** decode and summarise in the console, so every client
  has the arithmetic. The type rides in a field the agent already kept
  verbatim (`WatchRange::note`, formerly unused), so a later run decodes a
  capture with no agent change.
- **Not built:** a tolerance search (narrowing by change over one range is
  [`mem-diff`](commands/memory.md#mem-snap-and-mem-diff); the rest is a
  memory scanner's) and a mark on pooled instances
  (game knowledge, a host's verb). A bool's `view.nonzero` already tested its
  bit; readable names already worked in `get` paths.

## Usage log

Those session lessons were reconstructed afterwards from memory, as
estimates. The console now records each call, and `stats` reads it back
([console](commands/console.md#usage-log-and-stats)), for two readers: the
agent in the session (before a long step, and for its final report) and
whoever improves the tool or a skill across sessions.

- **The console records, because every path ends there:** JSON calls
  from a shell, the generated functions and the MCP tools all run a console
  command. Nothing is recorded in the target or the adapter.
- **One file beside the console, one line per call.** Per-session and
  all-time figures come from the same lines, so neither had to be chosen.
- **Facts, never values.** No addresses, names, paths or results: they are
  large, mean nothing later and would keep the target's data in a file
  that outlives the session. A hash of the input finds the same call made
  again.
- **The caller says who it is** (`--caller`): only a tool's output lands in
  an agent's context, so context cost is its own number.
- **`stats` is a core verb**, a tool and a function like any other, and it
  answers with no target, so an agent can reflect after the target has
  gone. For that, a console with no target answers `help` and `stats` and
  ends on any other verb (kind `startup`), so a client starts it again.
- **Marks, not advice.** `unfiltered`, `repeated` and `refused` state what
  happened; the skill says what to do. `unfiltered` uses a fixed list of
  filter keys (`view`'s `match`, `nonzero` and `limit`, `own`, `cls`)
  rather than a mark on each key's row: the same names mean
  the same thing on every verb, and a row mark would change `help`'s `json`
  and every generated module for no caller's gain.
- **A batch's commands get their own lines**, under their own verbs, and
  the batch one for its wrapper alone, so per-verb figures stay true and
  totals count nothing twice.
- **The caller's own calls are not recorded:** `help` with `json` (every
  session client reads the table at start) and `quit`.

| Rejected | Why |
|---|---|
| Recording in the MCP adapter | misses scripts and shell calls, and a second log would be wanted for them |
| A stats service or a database | the console keeps state in files; there is no daemon ([round trips](#round-trips)) |
| Storing arguments or results | size, and the target's data in a file that outlives the session |
| A warning inside a large result | more output to cure output; the agent asks `stats` when it wants to know |
| Counting tokens | depends on the model; characters rank the same verbs |
| Advice in the result ("use `view.match`") | a rule wrong for one call misleads; the marks state what happened |
| Per-session files | all-time figures would read many files; one file with a session on each line gives both |
| A `caller` key on each JSON call (to leave out a client's table read) | another key on every call for one line; leaving `help` with `json` out covers it |

## Place among other tools

ReAgentLive overlaps general tools at its base and should not grow further into
their ground. Its raw memory commands repeat what a memory scanner does, and
its Unreal lookups repeat what an engine scripting bridge does. Those layers
were cheap and carry the parts no other tool has:

- **In the host's own image.** No second injector, debugger or hook set; it
  reuses the host's thread and object checks.
- **Tick accuracy.** Same-tick reads, per-tick `watch-start`, hold, and `each` over
  every instance in one tick. A reader outside the process sees torn values.
- **Identity and guards.** Refs that go stale safely, guarded pins, writes
  that refuse reused memory.
- **Reflected and native memory in one tool**, plus the host's own ops.
- **Shaped for an AI's costs:** few commands, each doing a whole lookup, with
  filtered output.

**Build or borrow.** A feature is built here when it needs at least one of:
the host's thread, tick timing, object identity, or host knowledge; **or
when it reads live code an agent should not wait on whole-program analysis
for** (the [fast lane](#fast-lane)). What another tool already does well is
borrowed:

| Need | Borrow from | Not built here because |
|---|---|---|
| What writes or reads an address (data breakpoints) | nothing: debuggers are out of scope | needs debug registers and an exception handler in the target. Reach the answer in steps instead: the static tool for code that touches the field's offset, `watch-start` for the tick a value changes on, `write` with `undo` or `hold` to tell a value recomputed every tick from one set once, and `script` or `refs` for Blueprint readers |
| Narrowing scans over the whole process, pointer scans | a memory scanner | large, and needs no tick or identity. A bounded one over one range (`mem-snap`, `mem-diff`, up to 1 MiB) is built: an AI agent cannot drive the scanner's window, and the range is usually an object it already found |
| Tracing native code, direct C++ calls, code hooks | a dynamic instrumentation tool | code patches in the target; the core writes none. Reflected calls are covered by the call recorder (`calls-start`), one `UFunction` pointer |
| What is at one address, what points at it, what uses a member offset, whether a signature holds there | nothing: the [fast lane](#fast-lane) answers from live bytes | a static tool needs its whole-program analysis first, which takes hours on a large image and again after every patch |
| Decompiling, types, whole-program call graphs | a static analysis tool, fed by fast-lane saves (`disasm`'s `save` with `callees`) | it has the decompiler and the types; ReAgentLive hands it the live functions it needs in seconds |

- **Be a good neighbour, not a rival.** Hand addresses over in a form other
  tools take (`module` and `rva`) and accept them back; that is ReAgentLive's
  whole integration surface. No link between the tools: the agent driving
  them is the bridge.
- **Tools come from the table, not by hand.** A tool per operation was
  declined while each would have been one more copy to keep in step; the
  verb table removed that cost, so the MCP tools are generated from it
  ([tool interface](tool-interface.md#tool-interface)). Their schemas still cost context
  in a client that loads them all up front.
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

```text
core/              no engine, no game, no UI
  protocol.h       wire layout, op ranges, statuses                 exists
  agent.h/.cpp     Tick, dispatch, core handlers, the agent's end of the section
  client.h/.cpp    C++ API: attach, read, write, modules, Send; the client's end
packs/<id>/        one engine pack each, with its pack.cmake
  ue/              Unreal: every op with no SDK; an SDK optional, as the layout
    protocol_ue.h, agent_ue.h/.cpp
    client_ue.h/.cpp Find, Fields (paging hidden), Call
    cli/<layer>/   the pack's part of each CLI layer
frontends/
  cli/             calls (args schema, checks), formatting, the console on the client API
  python/, mcp/    on the console's JSON calls
  ida/             reads the console's label file
```

| Layer | Job | Knows about |
|---|---|---|
| `protocol.h` | wire structs, op ranges, statuses | nothing |
| `agent` | dispatch and handlers, in the target; its end of the section | protocol |
| `client` | typed C++ API, errors, timeouts; its end of the section | protocol |
| Unreal client calls | `Find`, `Fields`, `Call` | client |
| console | JSON calls → client calls → JSON Lines | client |
| Python, MCP | a script's or a tool's call → a JSON call | the console |

## Frontends

The protocol and client API are implemented; current behaviour is in
[protocol](protocol.md) and the [README](../README.md). The console and C++
callers sit on the client API, never on the section; every other frontend
sits on the console.

| Frontend | Shape | Notes |
|---|---|---|
| **CLI** | JSON calls only (one per line, from stdin, `-c` or `-c @<file>`), typed values, hex dump | exists, on the client API; one [verb table](tool-interface.md#self-describing-console) describes every command and its args ([JSON-only contract](tool-interface.md#json-only-contract)) |
| **GUI** (later) | ImGui desktop window on the console's JSON calls | tables, watch panels |
| **Python** | `frontends/python/reagentlive_session.py`: starts the console and exchanges one JSON call per command; `reagentlive_gen.py` generates typed functions per console | exists; no logic of its own, so every command lands in the console first; results are strict against the console's table |
| **Static-tool side** | `frontends/ida/reagentlive_ida.py`: loads files `disasm`'s `save` wrote, imports the console's labels file and exports the names you gave back into it | exists; reads and writes files, never talks to the agent: the shared labels file is the link, the agent driving both tools is the bridge |
| **MCP** | `frontends/mcp/reagentlive_mcp.py`: one tool per row of the console's table | exists; on the console's JSON calls ([tool interface](tool-interface.md#tool-interface)) |
| **C++ callers** | link the client | test harnesses, scripts, other tools |

The console has become the command layer: `get`, `batch`, `entries`
and the cache exist only there. A frontend that wants those commands goes
through the console's JSON calls, as the Python client does; only a
frontend that needs raw requests links the client. A new console command
ships with its row in the verb table; its Python function and its tool are
made from it.

## Self-describing console and tool interface

The verb table, `help` with `json`, the generated Python functions and the MCP
tools, with their decisions and rejected alternatives:
[tool interface](tool-interface.md).

## Unreal pack

- **Compile model:** compiled inside the host, from its sources. Every
  handler reads engine types through one layout (`EngineLayout`,
  `packs/ue/engine_layout.h`, the one home of engine offsets): `StockUE5`
  checked at run time (`reagentlive::ue-base`), or a host's Dumper-7 SDK
  read with `offsetof` (`reagentlive::ue`, `engine_layout_sdk.h`). Only that
  header and `agent_ue_globals.cpp` see `SDK/`, so no abstract engine
  interface and no second copy of the handlers.
- **Layouts from the SDK or the stock layout, addresses found at run
  time.** A game patch moves the object table and the name pool but rarely
  the engine's types, and right after a patch is when the tool is needed
  most. So the pack never takes addresses from an SDK (at most a guess it
  checks), locates both globals itself (host guess, then a
  shape scan of the image's writable sections) and checks them before its
  first command: index equals slot, one class of classes, `None` and
  `Class` decode ([before the first command](commands/unreal.md#before-the-first-command)).
  A failed check turns the pack off with its reason; core commands stay.
  Names are decoded from the located pool, not through the SDK's
  `FName::ToString`, whose engine call sits at a baked address. Rejected:
  trusting the SDK's offsets (stale after every patch, and a stale name
  function is a call into random code); code signatures for the globals
  (they break on patches too, and are game-specific work); a host-supplied
  name decoder (it carries the host's stale address with it). An engine
  version change that moves the types fails the check and turns the pack
  off rather than reading garbage.
- **Lookups without an SDK.** The table, the object header and two
  UStruct offsets are all `find`, `census` and names need, and UE5 keeps
  them in a few known shapes, so `ue-base` tries those and probes the
  rest from the engine's own class tree, under the same checks as the
  globals ([without an SDK](commands/unreal.md#without-an-sdk)). Every
  other op reads the stock layout once its group passes
  ([decision](#without-an-sdk-the-stock-layout-checked-per-group-on-known-objects)).
- **String type:** the pack only uses `.c_str()` and `.size()` on SDK strings,
  so stock `std::string` dumps and adapted string types both compile.
- **Three thread kinds, stated by the host.** `Host::thread` is `engine`,
  `borrowed` (a thread the game runs for another purpose, such as the frame
  present) or `own`, with no default: the agent will not open until the
  host says. The core publishes it (`status`, `tick_accurate`) and the pack
  enforces the engine-call rule; neither verifies the other. On the
  engine's thread everything works and a tick is a moment of the game;
  elsewhere memory commands work, pack reads answer when the host opts in
  (`Context::offThreadReads`), everything that runs engine code is refused
  ([the list](commands/unreal.md#off-the-engines-thread)), and a tick is a
  host tick. A host that wants calls from there moves its tick onto the
  game thread with `reagentlive::ue-draw`
  ([decision](#a-game-thread-tick-through-the-viewports-draw)). Rejected:
  detecting the kind (the core cannot know which thread is the engine's,
  and a wrong guess is worse than a required statement); running engine
  calls off the engine's thread behind a flag (unsafe in a way no guard
  fixes); queuing requests from a borrowed thread to the engine's (that
  needs a second tick source, which is what the Draw tick is).
- **Best-effort pack reads off the engine's thread, behind a host
  opt-in.** Lookups and reads (`find`, field tables, object reads, `entries`,
  `enum`, `func`, ...) read memory the engine owns but call none of its
  code, so a host ticking on its own thread can answer them under a fault
  guard, checking the object's identity before and after each read; values
  may tear between two fields, and the console marks such results
  `tick_accurate: false`. Shipped on in `examples/unreal` and used by
  library hosts; live-tested on a UE 5.7 game. A function's parameters
  (`func`'s `params`) stay on the game thread with the calls. Rejected: on by default (a host must accept torn
  values knowingly); answering calls the same way (no guard makes an
  engine call off its thread safe).
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
  `export-labels` with `kinds` interface and global then named 213 more, the
  generator's own static-FName naming having failed on an unanalysed
  database). Not checked: whether a full export after the mappings would
  comment rather than rename the generator's names; the docs say not to run
  one. A host whose SDK
  generator writes IDA mappings (Dumper-7) imports those first for exec
  thunks, vtables, types and engine globals; `export-labels` with `kinds`
  interface and global then adds only what needs the running process. ReAgentLive
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
- **Calls run under a fault guard.** An access fault inside `ProcessEvent`
  is answered `access faulted`, saying the target may be in a bad state,
  instead of ending the game. It is no safety net: a fault can leave a lock
  held or state half-changed, so the session is suspect afterwards. Only
  access faults are caught, as in the agent's reads. Rejected: catching
  everything (a C++ exception or a debug break belongs to the engine) and
  no guard (one wrong block ended the process and the investigation).
- **`call-pure` is a read the agent checks.** A const or blueprint-pure
  function whose outputs own nothing is what `call-each` calls on every instance,
  and the skill already called such getters without approval; the verb
  makes the claim checkable (`CallPure`, refused before calling) and gives
  it a read-only tool. The flag is the engine's convention, not a proof,
  and the docs say so. Rejected: marking `call` read-only when the
  function is pure (a verb's mark cannot depend on its arguments).
- **Class names by exact find.** A bare class name was resolved by
  `find`'s first page, which scans and counts the whole table even after
  the match, so every new class cost a full scan: the likely cause of a
  925 ms median for field tables in one host's usage log, where a table on
  class names was most of the time (not yet measured live against the
  fix). `FindExact` stops at the first match. Rejected for now: a
  name-to-class index built by one scan (thousands of pages for the
  classes of a large game; the exact find makes a miss the only full scan).
- **A class index for `find` by `cls` and `each`.** Measured live on a table
  of about 410 000 objects at 55 ticks per second and a 4 ms scan budget:
  `find` by `cls` took 4.4 s and `find` by `text` 0.7 s, both in the agent (two
  round trips). The class walk cost about six times the name walk per
  object: each distinct class's parent chain was checked with a
  `VirtualQuery` per step. Now the agent keeps per slot the object last
  seen and its class; a query compares slot pointers (a sequential read)
  and reads only changed headers, and parent chains go through a cache of
  the last readable regions. Rejected: rebuilding only when the table's
  count changes or after N seconds (slots are reused, so the count barely
  moves while objects come and go; a fixed age either rebuilds on most
  calls or misses new objects for that long); building it on a thread of
  its own (the pass is a few milliseconds, inside the existing scan
  budget, and a pack thread would need a host opt-in).
- **Every search is a job.** A blocking search over the whole address
  space ran for minutes, timed out and lost its work, holding the
  console's one request slot meanwhile. Now the agent keeps one job,
  scanned on the worker or between requests, and the console polls it up
  to `wait`; the default range is the main module. Rejected: a job
  option beside the blocking form (two result shapes for one verb); a job
  kept in the console (gone when a one-shot console exits, and the
  agent's slot still busy); separate status and stop verbs (one
  `search-job` with `stop`). The console's own scans (`xrefs`, `sig-check`,
  `labels-sign`) stay blocking: they cover one module and need every page.
- **Known debt: the interface stub pattern decodes bytes in the agent.**
  `func` on an interface function matches one stub shape
  (`mov rax, [reg]; call [rax + disp]`, `StubVtableSlot`) inside the agent,
  against the rule that the agent reads bytes and the console decodes them.
  It stays because resolving a live object's implementation needs it in one
  request; the console's decoder ([fast lane](#fast-lane)) is where it
  would move. Any other stub shape reports a null slot.

### Without an SDK: the stock layout, checked per group on known objects

**Decision.** `reagentlive::ue-base` reads every engine type at the offsets
of `StockUE5` (`engine_layout.h`) and checks them in groups (class, struct,
field, property, typed, function, enum, call) on objects every UE5 game
has: the cast flags of `Class`, `Struct`, `Field`, `Object`; the chain
`Class` > `Struct` > `Field` > `Object`; `Vector`'s and `Guid`'s members,
sizes and flags; every bool on `Actor`; `KismetMathLibrary:Add_IntInt`;
`ENetRole`; `UObject`'s `ProcessEvent` slot. The first op that needs a
group runs its check once; a failed group refuses its ops with the check
that failed, and the others still run
([groups](commands/unreal.md#without-an-sdk)). With `reagentlive::ue` a
host's SDK is the layout and no group is checked.

**Why.** One prebuilt agent then works in any stock UE5 game with no dump,
including right after a patch, and a non-stock engine refuses instead of
reading garbage. Evidence: live-tested on a UE 5.7 game with no dump.

**Rejected.**
- *SDK only:* a dump per game and per patch before the first lookup, and
  nothing right after a patch, when the tool is needed most.
- *A probe framework* searching each member's offset per engine version:
  much more code to keep right per version, and a search finds a wrong but
  plausible offset as readily as the right one; a known answer on a known
  object either holds or it does not.
- *An in-agent probe* that guesses a non-stock layout at run time: the
  same wrong-but-plausible risk inside the target, with no one to check
  the guess. Finding a non-stock layout stays the agent's (the skill's)
  investigation, read-only and checked.

**Consequences.** A non-stock engine gets the groups that pass and refusals
for the rest; filling the gap needs a layout profile the agent can load
([ideas](ideas.md#33--layout-profile-loading)). Refs carry serial 0
without an SDK (serials are not known).

### A game-thread tick through the viewport's Draw

**Decision.** `reagentlive::ue-draw` gives a host that ticks off the game
thread a tick on it, with no per-build address: when `game-thread` arms it, it
finds the first live `GameViewportClient` with `FirstLiveInstance`, copies
the vtable of its Draw interface (`ViewportInterfaceOffset`, entry
`ViewportDrawIndex` in `engine_layout.h`, confirmed per game before the
first `on`), swaps Draw in the copy and writes one pointer, that
interface's vtable pointer inside the viewport object, to the copy. The hook forwards every call first, then
ticks the host only for that viewport and only on the process's first
thread. `off` restores the pointer only if it still holds the copy; the
copy stays allocated. Host API: [hosting](hosting.md#game-thread-tick).

**Why.** Calls, `each`, `script`, `refs` and the recorder need the game
thread, and a host on its own thread (`examples/unreal`) has none. Draw
runs once per frame on the game thread in every UE5 game, and a private
table copy touches nothing shared.

**Rejected.**
- *Writing the game's vtable entry:* read-only data, shared by every
  instance of the class, and a clash with any other owner hooking it.
- *A per-build profile* of a game-thread function to hook: a signature or
  RVA per game and per patch, the work a stock-layout agent avoids.
- *Hooking `ProcessEvent`:* code or a shared vtable entry again, on the
  engine's hottest path, ticking far more often than once a frame.
- *An inline code hook on Draw* or a present/graphics hook: patches code,
  or ticks on a render thread with a graphics-API dependency.

**Consequences.** `game-thread` is marked `writes` (one pointer in a heap
object), so a client asks before it; the host must allow only one tick at
a time, since its own thread and Draw both tick the agent.

### Light reversing

- **Scope:** ReAgentLive answers what only a running process can (the value,
  object or function at a live address, what a global holds once the engine
  filled it, which implementation a live object dispatches to). A static
  tool answers callers, references, whole functions and types. The bridge
  is `module` and `rva` both ways, `peek` for live globals, and
  `export-labels` for names.
- **No decoder in the agent:** it would grow the injected image for
  display-only work. The agent reads bytes; the console decodes them.
- **Signatures: the agent makes them, ReAgentLive verifies them.** One
  exception, below: the labels mass signer. Making one is judgement (which instructions are distinctive, which operands move,
  whether a caller is the better place) that an agent does from the static
  tool's disassembly without a generator. What it cannot know alone is the
  result in the image the host really scans, DLLs with no database
  included, so `sig-check` is a strict checker whose feedback names the
  next correction: `mismatches` at the expected address, `diverges_at` per
  extra hit, `warnings` for unmasked addresses (a few byte patterns, not a
  decoder), `resolve` for reference signatures, `file` for many at
  once, and `sig-bytes` as a start without a static tool. With a boundary
  the reader gives (`expect`, `sig-bytes`) the warnings and masks come from
  the console's decoder, and the byte patterns only past where decoding
  stops. It runs in the
  console on the exact `search` (anchor) and `read-batch` (mask compare);
  the agent is unchanged. Unique means one match in the whole module image,
  stricter than a host scanner that reads only code. Rejected: a signature
  maker for signatures that matter, in the console or as a static-tool
  script (two earlier drafts had one each; the checker's feedback covers the
  mistakes a generator would prevent; revisit a script only if hand-made
  signatures keep needing many attempts), wildcards in the agent's `search` (the anchor plus a masked
  compare needs none; take it up only if a masked search over the heap, a
  struct found by shape, is wanted twice), scanning the exe on disk (the
  static tool and a host's offline verifier do that) and writing into
  host source files (the agent or the user pastes).
- **The labels mass signer stays one fixed loop.** `labels-sign` signs a
  labels file's names so `labels-port` can find them after a patch: cheap,
  the same every time, and a miss costs one re-find, so quality is not worth
  judgement there. The line: ReAgentLive checks, stores and ports
  signatures, and the only ones it makes come from that loop. Rejected:
  anything that makes its signatures better (later starts, caller
  fallbacks, ranking, looser masks, a generator's tiers), and a way to store
  a hand-made one (a pattern option; the row is one JSON line and
  `sig-check` with `file` checks it). A name the loop cannot sign is signed by hand or left
  unsigned; a failure is never a reason to extend the loop.
- **Rejected:** call graphs, decompiling, types and naming in ReAgentLive (the
  static tool has the whole image); symbol loading; ReAgentLive talking to the
  static tool directly (the agent driving both is the bridge).

### Fast lane

A static tool answers a question about one address only after analysing the
whole image: hours for a large game, again after every patch, and longer on
a packed image whose exception directory hides or fakes functions. The fast
lane answers code questions from live bytes in well under a second, with no
database:

| Question | Command |
|---|---|
| What are the instructions here, or the whole function? | `disasm` (`function`) |
| What points here? | `xrefs` |
| Which code near here uses these member offsets? | `touches` |
| Is this pattern unique, and does it land here? | `sig-check` |
| Decompile this function and what it calls | `disasm` with `function` and `save` (`callees`), then the static tool |

- **The fence is invasiveness, not size.** Anything that reads live bytes
  and hands its result on is in scope, however much analysis it does:
  following calls and jumps, naming from a labels file, bounded sweeps.
  Out for good: hooks or patches in executable code, and debugging
  (breakpoints, debug registers, exception handlers in the target); see
  [principles](#principles). What the static tool already does well
  (decompiling, types, whole-program call graphs) is borrowed, not rebuilt.
- **Why the disassembler was un-parked.** It was parked because all code
  read so far matched the file the static tool shows. That still holds; the
  reason is analysis time on large and packed images, not live code
  differing from the file.
- **A whole function and its constants** (`disasm` with `function`): decoded
  linearly to its entry's end, with the values its read-only loads read
  and the strings it points at. Reading every instruction by hand cost more
  calls than any other step in a live session.
- **Saves for the static tool** (`save`): the bytes and constants of a
  function, its chunks (chained entries, switch targets) and, with `callees`, every function it
  calls, named from a labels file; a small IDA loader builds one database
  from them and `add_saved` grows it. The decompiler is borrowed: a
  database of the whole image takes hours, a save seconds. Measured on a
  packed image: a handler and 16 callees in one save (0.75 s) and three tool
  calls to readable pseudocode, against about forty for seven functions one
  at a time.
- **Function bounds** come from the exception directory, a table lookup.
  Code it leaves out is never called a function: `xrefs` and `touches`
  label it `unlisted`, with the bounds of the piece between returns and
  padding. Packed images fake the directory (one-byte decoys at a fixed
  stride, entries a few bytes before the code, functions split into chained
  chunks); each trick is handled where it was met, with the evidence on the
  command page.
- **Sweeps:** bounded, read-only ones are in (`touches` over a range, the
  piece around one address). An image-wide sweep (every vtable, every
  function) stays a script over the Python client: it is the one-address
  chain run thousands of times, and its output is a file, not a reply.
- **Where it lands.** Only the relative-reference scan behind `xrefs` is a
  core agent op (with a client call), since it reads memory only and works
  with any host. The decoder (Zydis, pinned, formatter on) links into the
  console alone; a configure check fails when an agent target links it.
  Kinds, bounds, pieces, saves and `sig` feedback are console work.
- **Not built, no case yet:** structured operands, struct recovery from
  offsets, and walking backwards from an unrecognised reference to its
  instruction start (unreliable on x86 without analysis; `disasm`
  with `function` shows it in place). **Out for good:** hooking a function to
  log its callers (a code hook; `xrefs` gives the static callers, and
  `calls-start` records reflected calls through a pointer swap).

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
  follows a chain of offsets the same way the console does; the console only reads
  the `{"base", "chain"}` value. Writes, pins and trials refuse chains: a write goes where the
  caller read, never through pointers that may have moved since.
- **Strings** (`read`'s `type` `cstr` or `wstr`) escape what they cannot print, so a dump
  never hides a byte.
- **`region` is a core op** because only the target can ask Windows about
  its own memory; the agent passes `VirtualQuery`'s numbers through and the
  console names them, as the `region` that `read` and `peek` add. Not a verb
  of its own: every question it answered started from a read or a peek. A stack is not claimed: private read-write memory is
  `private`, since telling a thread's stack from a heap needs every
  thread's limits.
- **Calls are opt-in and deferred.** `call-native` (the one item that can
  crash the target) waits for a live case a read cannot replace, or an
  outside program on a game with no engine pack
  ([backlog](ideas.md#17--call-native-caution)); a host will enable it only where engine
  calls are safe, and approval stays the host's rule. Out for good: code
  hooks, native tracing and breakpoints (the call recorder swaps a
  `UFunction` pointer, never code); calling by symbol name (shipping images have
  none); floating-point arguments and callbacks until a case needs them.
- **`vtable`'s `rtti`: class names where the image has them.** Declined while the
  one host checked (an engine game) had none for its game classes; built
  because a target with no engine pack is the case native objects exist
  for, and most native programs and libraries are built with type
  information. It is a one-address answer from bytes (locator, descriptor,
  hierarchy), console only, and refuses instead of guessing when the slot
  before the table holds no locator. An arg of `vtable`, not a verb: the
  same input and the same question. Not used to label `peek` rows: one more
  read per row for what one `vtable` with `rtti` answers.
- **Rejected:** a struct or type system in ReAgentLive (layouts come from
  the engine pack, the host's SDK or the static tool; `read`'s `type`
  covers ad hoc ones).

## Source layout

The layout itself and its rules are in
[contributing](contributing.md#source-layout).

- **`frontends/cli/` is grouped by layer** (call, values, state, results,
  code, console), each with one include rule a test checks.
- **Rejected:** feature folders (`memory/`, `unreal/`, each with parser,
  logic and console). Commands cross areas (`get` uses value formatting,
  state files and results), and the two CMake targets and the
  no-decoder rule would be split across every folder.
- **Each pack is one folder, `packs/<id>/`**: its agent and client files,
  its `pack.cmake` (targets and CLI source lists) and `cli/<layer>/`, its
  part of each layer under that layer's rule. Nothing outside it names
  the pack, and no pack includes another. A reader sees at a glance what works in any
  process, and a core change cannot lean on Unreal types by accident: the
  layout test fails. Object identity (`Ref`) is core, since refs, pins and
  watches carry it whatever the engine.
- **The core reaches the pack through plain functions** (`console/pack.h`):
  its state, cache block, its rows of the [verb table](tool-interface.md#self-describing-console),
  `status` line, its commands, and what core commands borrow. With one pack
  they need no interface.
- **A console without the pack is a build of it** (`REAGENTLIVE_PACKS`
  empty, [hosting](hosting.md#using-it-in-your-project)). `console/no_pack.cpp` answers the seams
  for "no pack" (no rows, no state, no cache block, nothing offered); the
  core files take no `#if`. Its table holds core and host rows only, less
  the core rows marked `needsPack`, and `help`, the generated functions and
  the MCP tools all read the table, so none of them needs a switch. A
  non-Unreal target then pays for no pack tool schemas (about two fifths
  of the full set; the core's are the rest). The tests build this console beside the default one: a core
  console file that leans on the pack no longer compiles, which the layout
  test only approximates from `#include` lines.
- **Rejected for the pack-less console:** a flag on the MCP adapter or the
  generator (the console's own `help` would still list the verbs, and the
  module would no longer match the table it was made from); a run-time
  console switch (every reader would have to pass it alike or the table
  hash differs, and the pack stays linked for nothing); hiding tools by
  what the live host offers (the tool list is known before a target runs,
  [tool interface](tool-interface.md#tool-interface)).
- **Rejected for now:** a pack interface or registry (a table of packs
  each registering verbs, help and hooks). Designed around one pack it would
  guess the shape a second one needs; the seams list exactly what that
  table must hold, so a second pack (Unity, Source 2) turns them into one.
- **Adding a pack is mechanical; supporting several in one console is not
  built.** A new folder, its `pack.cmake`, the next op slot
  ([protocol](protocol.md#ops)) and an entry in `REAGENTLIVE_PACKS`
  give its targets. A console still carries one pack: configure fails on
  two. The second pack turns the `console/pack.h` seams into a table of
  packs and moves Unreal-shaped fields out of core types (the parsed
  command's class name); both wait for it, for the reason above.
- **Rejected:** the pack's part as a `ue/` folder in each layer (the
  earlier layout): a pack spread over six folders has no single home, and
  a second pack would double the spread. The layer rules hold in
  `packs/<id>/cli/<layer>/` because the layout test applies them by layer
  name, and the pack's builders still join `reagentlive::cli` without the decoder.
- **Rejected:** `add_subdirectory` per pack (its variables, the CLI
  source lists the tests build from, would stay in the pack's scope; the
  root `include`s `pack.cmake`), and renaming the pack `unreal` (the
  namespace, targets and `status` id are `ue`; one name everywhere).

## Not now

- A transport interface, or a channel split out of agent and client (add
  either with a second real transport).
- Async/futures/callbacks in the client API.
- A C ABI or native Python bindings over `Client` (the Python client drives
  the console instead).
- A GUI frontend until the CLI stops being enough.
- Plugin DLLs or runtime loading of packs.
- Protocol negotiation beyond the layout hash and supported-op list.
- Packs for other engines until a real project needs one.
- Package-manager packaging (`install()`, `find_package`): the library is
  its own repository, consumed as a submodule or with `FetchContent` at a
  pinned tag, and each consumer compiles its sources with its own flags
  ([hosting](hosting.md#using-it-in-your-project)).
