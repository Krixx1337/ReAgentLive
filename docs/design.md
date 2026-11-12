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
  syntax with `read --type`, so a decoded layout pastes into `label`.
- **A capture that does not hold the console**, without a second channel:
  the capture belongs to the agent instance (`watch --start`, `--read`,
  `--stop`), bounded by the existing buffer, so a long capture and other
  questions overlap. Following a replaced object asks the host: the core
  knows no names, a host offers roots through one optional callback.

From one autonomous read-only agent session through the MCP tools only,
where every question was answered and the cost was output and turns:

- **Wrong-looking answers first, then output, then turns.** A one-hop chain
  (`X->0`) answered the bytes at `X` with `hops: []`: right by the rule
  that the last `->off` only adds, and read as a followed pointer. It is
  refused with the form that follows one. A misspelt JSON key (`class_`)
  was reported as a left-out argument; unknown keys are now checked first
  and named with the verb's own. Refusals state their limit's number.
- **One name per argument, never a Python keyword** (`cls`, `start`,
  `type` for `class`, `from`, `as`), so the typed line, a JSON call, a
  `batch` entry, the generated function and the tool spell it the same.
  Keeping the keywords and letting the generator append `_` was the cause:
  the tool said `class_`, `batch` wanted `class`. A table test refuses a
  keyword.
- **Compact rows, layout on request**, the rule values already followed.
  A `dump` row is its name, offset and value (about 25 KB for one actor
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
- **Not built:** a tolerance search (a narrowing scan is a memory scanner's,
  [backlog](ideas.md#16--narrowing-scan)) and a mark on pooled instances
  (game knowledge, a host's verb). A bool's `--nonzero` already tested its
  bit; readable names already worked in `get` paths.

## Usage log

Those session lessons were reconstructed afterwards from memory, as
estimates. The console now records each call, and `stats` reads it back
([console](commands/console.md#usage-log-and-stats)), for two readers: the
agent in the session (before a long step, and for its final report) and
whoever improves the tool or a skill across sessions.

- **The console records, because every path ends there:** typed lines,
  JSON calls, the generated functions and the MCP tools all run a console
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
  filter flags (`--match`, `--nonzero`, `--own`, `--changed`, `--cls`,
  `--limit`) rather than a mark on each flag's row: the same names mean
  the same thing on every verb, and a row mark would change `help --json`
  and every generated module for no caller's gain.
- **A batch's commands get their own lines**, under their own verbs, and
  the batch one for its wrapper alone, so per-verb figures stay true and
  totals count nothing twice.
- **The caller's own calls are not recorded:** `help --json` (every
  session client reads the table at start) and `quit`.

| Rejected | Why |
|---|---|
| Recording in the MCP adapter | misses scripts and the text line, and a second log would be wanted for them |
| A stats service or a database | the console keeps state in files; there is no daemon ([round trips](#round-trips)) |
| Storing arguments or results | size, and the target's data in a file that outlives the session |
| A warning inside a large result | more output to cure output; the agent asks `stats` when it wants to know |
| Counting tokens | depends on the model; characters rank the same verbs |
| Advice in the result ("use `--match`") | a rule wrong for one call misleads; the marks state what happened |
| Per-session files | all-time figures would read many files; one file with a session on each line gives both |
| A `caller` key on each JSON call (to leave out a client's table read) | another key on every call for one line; leaving `help --json` out covers it |

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
- **Tools come from the table, not by hand.** A tool per operation was
  declined while each would have been one more copy to keep in step; the
  verb table removed that cost, so the MCP tools are generated from it
  ([tool interface](#tool-interface)). Their schemas still cost context
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
  cli/             syntax, formatting, console on the client API     exists
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
| **CLI** | text REPL and JSON calls, `$N` refs (typed lines), `base+off`, hex dump | exists, on the client API; one [verb table](#self-describing-console) describes every command |
| **GUI** (later) | ImGui desktop window, calls from a worker thread | tables, watch panels |
| **Python** | `frontends/python/reagentlive_session.py`: starts the console and exchanges one JSON call per command; `reagentlive_gen.py` generates typed functions per console | exists; no logic of its own, so every command lands in the console first; results are strict against the console's table |
| **Static-tool script** | `frontends/ida/import_labels.py`: imports the console's label file | exists; reads a file, never talks to the agent |
| **MCP** | `frontends/mcp/reagentlive_mcp.py`: one tool per function generated in memory from the console's table | exists; on the console's JSON calls ([tool interface](#tool-interface)) |
| **C++ callers** | link the client | test harnesses, scripts, other tools |

The console has become the command layer: `dump`, `get`, `batch`, `entries`
and the cache exist only there. A frontend that wants those commands goes
through the console's JSON calls, as the Python client does; only a
frontend that needs raw requests links the client. A new console command
ships with its row in the verb table; its Python function and its tool are
generated from it.

## Self-describing console

Nothing in the console used to answer "what does this command take and
return?". A read-only sweep of about 50 commands, written by an AI agent
from memory of the keys, misread three results (`suggested.text` for
`list`, `hops` at the top for `chain.hops`, `protection` for `protect`),
skipped a check because of it and still reported it as passed. Every
command was correct; only the caller was wrong. Two failures: no
discoverable keys, and dict access that lets a wrong guess look like
success. The failure only happens in scripts (a one-shot caller sees the
result), so type safety belongs in the scripting layer and discovery in
the console.

- **The console executable is the source of truth.** One C++ verb table
  (`syntax/verb.h`; rows in `results/verbs.h` and `results/ue/verbs_ue.h`)
  describes each command's positionals, flags and result type. Parsing,
  usage errors, `help`, `help <verb>`, `help --json`, the generated
  [result shapes](commands/results.md) page and the Python client's
  strictness all read it. Each shape used to be written five times
  (builder, key pins in a test, the docs row, a Python docstring, the
  skill) and agreed only because they were edited together.
- **One verb, one result type.** Modes became verbs (`sig-check`,
  `sig-bytes`, `watch-start`, `census-diff`, `pin-list`, `trial`). A flag
  may add optional keys and never changes a key's type (`read --type` with a
  layout fills `layout`, not `values`). One name for a result's list
  (`rows`), one paging convention (`from`, `next_cursor`), `capped` for a
  list cut at a hard limit and `truncated` for an incomplete value.
- **Typed result structs with one field list.** A result is a struct
  whose `Describe` lists key and member once (`results/schema.h`); that
  list writes the JSON and describes it, so a builder cannot emit a key
  the description lacks. The tests that pinned key sets are gone; value
  checks stay, each also checked against its own type.
- **A small type vocabulary, not a schema language:** `addr`, `hex`,
  `bytes`, `ref`, the plain scalars, string enums, optional keys, lists,
  maps, nested objects, and `value` for game data, which is dynamic and
  stays untyped.
- **Optional keys are omitted, never null.** Output weight is a
  [principle](#output-weight-and-dead-ends), and the strict layer gives
  `None` either way. Nulls remain only inside game values and as list
  elements where position matters (a `watch` row's `values`).
- **The console checks what it prints.** A result that does not match its
  row's type is an `internal` error. This binds a runner to its row and
  covers a host's verbs, whose results arrive as JSON.
- **`status` carries a hash of the table,** so a cached script or skill
  can tell it is stale. It describes the console build; agent
  compatibility stays the layout hash's job
  ([protocol](protocol.md#handshake)). `help --json` carries it too and
  answers with no target running, so tools can read the table before a
  game does.
- **Not `describe --json`:** `describe` is the Unreal pack's object lookup,
  and one verb with two meanings is what the first decision removes.
- **Strict Python results.** The session reads `help --json` at start and
  wraps each result; an unknown key raises and names the valid ones, an
  absent optional key gives `None`. The typed functions generated on top
  ([tool interface](#tool-interface)) return these same records.
- **Host verbs are rows of the same kind.** A host hands the console a
  constant list of rows (`ConsoleConfig::verbs`) in place of help text, so
  its commands get help, usage errors and checked results the same way.
- **The row type is core; the rows are not a registry.** The core's rows
  and the pack's are constant lists; the core reaches the pack's through
  one more plain function in `console/pack.h`, as it reaches the pack's
  state. Nothing registers at run time. The rows live in `results/`
  (not `console/`) so the pure tests and the table tool reach them without
  a client. What this settles for a second pack: that a pack's verbs and
  help are rows. State, the cache block, the `status` line and what core
  commands borrow stay plain functions, undecided.
- **Each row also says where the verb comes from (`origin`), whether it
  writes** (the target's memory, or a call into it) **and whether it
  blocks** (holds the console for seconds), and each argument has a key, a
  type from a small vocabulary, a default where it has one and a line of
  description: a caller that picks a command, not types one, needs them
  all. The types that need no target state (`int`, an enum) are checked
  with the counts, before a verb's parser runs.
- **Named types.** A struct several results share names itself
  (`TypeName`): `help --json` lists it once under `types`, rows refer to it
  (`{"named": "Chain"}`), and `help <verb>` prints the name and its
  definition. A pack's names carry its prefix (`ue.Name`), so the core
  never lists a pack's shapes. `Object` (ref, name, class) is the core's,
  filled by a pack: `peek` is core and its object rows are typed without
  naming the pack.
- **A pack gives its own title.** Each origin (`core`, a pack, `host`)
  comes with a title in `help --json`; the generated results page takes
  its headings from there, so no core file spells a pack's id.
- **Mixed verbs stay on the side of their state.** `trial` is the core's
  (the write, the undo, the guard); its owner check is borrowed from the
  pack through `console/pack.h`, and without a pack it is refused. Its
  row is marked `needsPack`, so a console built without a pack leaves it
  out of its table ([source layout](#source-layout)).

Rejected:

| Option | Why not |
|---|---|
| A schema data file as the source | needs C++ generated from it, or stays one more copy the builders can drift from |
| `TypedDict` in place of strict records at run time | fails only under a type checker; `.get()` on a wrong key stays silent. The generated functions annotate with them and return records |
| Every optional key present as `null` | a fixed shape, paid for in output on every call |
| A separate frontend for AI agents | the console is that frontend; a second one splits the contract |
| `--select <key>` on every command | deferred until a shell caller needs the strict layer's error where a JSON filter prints null |

Open: how far `value` can be typed (a tagged shape for FNames, structs and
containers, or documented rules as today).

## Tool interface

An AI agent should pick a command and fill in named values, not write a
command line or a script by hand: hand-written input is where guesses
enter. The [self-describing console](#self-describing-console) made those
guesses fail loudly; this removes the writing. Use: [clients](client.md).

Every fact flows one way: the console's table, then `help --json`, then
the generated Python functions, then the MCP tools. No layer defines what
the one before it did not.

- **JSON calls are the console's input; the text line is shorthand.** A
  line starting with `{` is `{verb, args}` with arguments named by the
  row's keys. It sorts into the same arguments as a typed line, so one
  check and one parser per verb cover both, and nothing is quoted or
  escaped. The text line stays while agents and people type in a shell.
- **Literal refs only in JSON calls.** `$N` is refused: "the last `find`"
  is session state a picked argument should not depend on. A call takes
  the `ref` a result carries, and the ids of pins, trials and captures.
- **Typed Python functions, generated per console** (`reagentlive_gen.py`)
  from its `help --json`, so a host's verbs are included. One method per
  verb, positionals then keyword-only flags, typed from the row; results
  annotated with `TypedDict`s of the console's keys, so a type checker
  reads a script before it runs. At run time they return the strict
  records: the annotations for the checker, the records for the run. The
  module carries the table hash and refuses a console whose table differs.
- **Named by origin:** `c.read` for core, `c.ue.find` for a pack,
  `c.host.world` for the host; as tools `read`, `ue_find`, `host_world`. An
  agent sees what needs a pack when it picks, and a host verb cannot take
  a core verb's name.
- **MCP tools wrap the functions** (`frontends/mcp/reagentlive_mcp.py`, the
  only file that needs the dependency): the adapter reads the console's
  `help --json`, makes the functions in memory with the generator's code,
  and takes schemas from their types, descriptions from their docstrings,
  `readOnlyHint` or `destructiveHint` from `writes`. No tool and no JSON
  Schema is written by hand or exported from the console, and there is no
  module file to go stale. Verbs marked `blocks` and `quit` are left out;
  `batch` takes a list of calls and runs them held.
- **Same shape as the console.** A function returns the console's result
  key for key; an `addr` stays its hex text. A helper that converts says
  so in its name. **Behaviour lives in the console:** paging to the end,
  filtering and retrying are verbs, flags and cursors, so the shell,
  scripts and tools all have them; the Python paging helpers went.
- **A console per tool call, never one kept open.** A kept console locked
  the executable (a rebuild failed to link) and kept running the old build;
  the tools, made once from a module file, kept the old list until the
  client reconnected. Now each call starts a console and ends it: nothing
  holds the executable, and the next call runs the new build. JSON calls
  carry no session state; what runs share (class refs, field tables,
  census baselines, the last `snap`) is in the cache file. Calls hold a
  lock, since a second console would be refused as busy. Measured with the
  own-thread host: about 17 ms a call against 2.4 ms on a kept console,
  most of it the table read that a hash read replaced; a 530 KB cache file
  adds under 1 ms. Not yet measured: a pack-heavy `dump` with the game's
  cache loaded.
- **A changed table remakes the tools before the call runs.** Each start
  reads only the hash (`help --hash`, no target needed; `status` would
  need one) and the full table when it changed. The functions still refuse
  a session from another table, which keeps scripts honest, so the call
  that saw the change runs through the new tools; the adapter then tells
  the client the list changed. A client that ignores the notice keeps its
  list: a tool whose verb kept its arguments and result works; changed
  arguments are refused with the usage, a changed result by the client's
  own check against the old type, a removed verb by the adapter. Those
  need a reconnect.
- **One usage-log session per server:** the adapter passes the same
  `--session` to every console it starts.

| Rejected for the adapter | Why |
|---|---|
| A `reload`, `reconnect` or `disconnect` tool | the agent has to know it is stale to call it, and the client's list stays as it was |
| A private copy of the console to run | the usage log, native names and labels are files named after the console beside it; a copy splits them |
| Keeping the console and closing it when idle | a timer thread and a window in which a rebuild still fails; only if a start per call is measured too slow |
| Regenerating the module file on every build, and nothing else | a second copy, and the running server keeps its old list |
| Watching the executable for changes | the next call sees the new table anyway |
| Skipping the stale-table check for the adapter | reopens for scripts what the check closes |
- **The generator and the adapter name no pack**; the layout test checks
  that they never spell a pack's id, that no core console file does, and
  that nothing in `core/` includes `ue/`.
- **FastMCP as the MCP Python SDK ships it** (`mcp.server.fastmcp`). The
  plan named the standalone `fastmcp` package; the SDK's FastMCP already
  makes a tool from a typed function with its argument and result schemas,
  and the SDK was at hand. The adapter returns the console's result as it
  is, with the schema from the annotation: the SDK would otherwise dump an
  absent optional key as `null`, which its own output schema refuses.
- **The protocol revision is the SDK's.** The plan targeted the
  2026-07-28 revision only; the SDK in use negotiates up to 2025-11-25.
  Nothing in the adapter depends on the revision.

| Place | Form | Why |
|---|---|---|
| Target and console | binary structs over shared memory | both ends compile the same structs and the layout hash refuses a mismatch; JSON would add a writer, a parser and allocation to the agent, on the host's thread |
| Console results | JSON Lines | |
| A call from the generated functions or the adapter | a JSON call on one line | no text line is built and parsed back, so nothing is escaped |
| A call typed in a shell | the text line | JSON is hard to quote in a shell and several times the length |
| An address or chain, a layout, a byte pattern | text, as one string argument | a compact notation for one value |

Rejected:

| Option | Why not |
|---|---|
| MCP inside the console (`<console> --mcp`) | no second process, but the protocol's messages and every later revision would be written and followed by hand in C++ |
| Two tools (`help`, `run`) | `run` takes a text line, so the input is still written by hand and unchecked |
| Tool definitions written by hand, or a JSON Schema export from the console | one more copy of every shape; the functions' types already give the schemas |
| A remote server (HTTP, authorisation) | one local console and one target; nothing here is remote |
| Dropping the text line now | an agent in a shell still types it |
| JSON between the target and the console | see the table above |
| Python paging helpers (`find_all`, `search_addresses`) | behaviour in one frontend only; the console's cursors and limits serve all of them |

Open: whether a smaller default tool set is needed (core lookups only, the
rest on request): every tool's schema together is about 110 KB. Confirmed
writes (a dry run the client repeats with a confirmation) wait until the
marks and the client's own prompt are not enough.

## Unreal pack

- **Compile model:** compiled inside the host against its Dumper-7 SDK
  (`UObject`, `TUObjectArray`, `UStruct`, `FField`, `FProperty`, `UFunction`,
  `ProcessEvent`, `EClassCastFlags`). Those names are consistent across
  Dumper-7 dumps of UE4 and UE5 games, so no abstract engine interface.
- **Layouts from the SDK, addresses found at run time.** A game patch moves
  the object table and the name pool but rarely the engine's types, and
  right after a patch is when the tool is needed most. So the pack takes
  only types from the SDK, locates both globals itself (host guess, then a
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
  Best-effort pack reads off the engine's thread are built behind a host
  opt-in for measuring, still a
  [backlog](ideas.md#18--best-effort-pack-reads-off-the-engines-thread) item
  until that is clean.
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
- **Strings** (`--type cstr|wstr`) escape what they cannot print, so a dump
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
  engine pack, the host's SDK or the static tool; `read --type` and the
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
- **The pack's part of each layer is its `ue/` folder**, and nothing
  outside one names the pack. A reader sees at a glance what works in any
  process, and a core change cannot lean on Unreal types by accident: the
  layout test fails. Object identity (`Ref`) is core, since `$N`, pins and
  watches carry it whatever the engine.
- **The core reaches the pack through plain functions** (`console/pack.h`):
  its state, cache block, its rows of the [verb table](#self-describing-console),
  `status` line, its commands, and what core commands borrow. With one pack
  they need no interface.
- **A console without the pack is a build of it** (`REAGENTLIVE_CONSOLE_UE`
  off, [hosting](hosting.md#using-it-in-your-project)). `console/no_pack.cpp` answers the seams
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
  [tool interface](#tool-interface)).
- **Rejected for now:** a pack interface or registry (a table of packs
  each registering verbs, help and hooks). Designed around one pack it would
  guess the shape a second one needs; the seams list exactly what that
  table must hold, so a second pack (Unity, Source 2) turns them into one.
- **Rejected:** one `frontends/cli/ue/` tree beside the layers. The layer
  rules (no JSON in syntax, no I/O outside console) would need restating
  for it, and the pack's syntax would join the JSON-carrying target.

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
