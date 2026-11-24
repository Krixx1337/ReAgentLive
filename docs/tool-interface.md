# Tool interface for scripts and agents

*Home of:* the decisions behind the self-describing console and the tool interface (the verb table, `help` with `json`, JSON calls, the generated Python functions, the MCP adapter), with their rejected alternatives.

**TL;DR:** Why the console describes itself and why scripts and AI agents
call it the way they do. One verb table in the console is the source of
every argument and result shape; Python functions and MCP tools are made
from it, never written by hand. This page is the decision record: how to
use each interface is [clients](client.md), how to drive the console is
[console](commands/console.md).

## Self-describing console

Nothing in the console used to answer "what does this command take and
return?". A read-only sweep of about 50 commands, written by an AI agent
from memory of the keys, misread three results, skipped a check because of
it and still reported it as passed. Every command was correct; only the
caller was wrong. Two failures: no discoverable keys, and dict access that
lets a wrong guess look like success. It only happens in scripts (a
one-shot caller sees the result), so type safety belongs in the scripting
layer and discovery in the console.

- **The console executable is the source of truth.** One C++ verb table
  (`call/verb.h`; rows in `results/verbs.h` and
  `packs/ue/cli/results/verbs_ue.h`) describes each command's args and
  result type. Call checks, refusals, `help`, `help` with `json`, the
  generated [result shapes](commands/results.md) page and the Python
  client's strictness all read it, so no shape is written twice.
- **One verb, one result type.** An arg may add optional keys and never
  changes a key's type; a mode that would change one is a verb of its own
  (`sig-bytes`, `watch-start`). One name for a result's list (`rows`), one
  paging convention (`from`, `next_cursor`), `capped` for a hard-limit cut
  and `truncated` for an incomplete value.
- **Result structs have one field list.** A result's `Describe` lists key
  and member once (`results/schema.h`); that list writes the JSON and
  describes it, so a builder cannot emit a key the description lacks. The
  type vocabulary is small, not a schema language; game data (`value`) is
  dynamic and stays untyped.
- **Optional keys are omitted, never null** (output weight:
  [design](design/console-costs.md#output-weight-and-dead-ends)).
- **The console checks what it prints.** A result that does not match its
  row's type is an `internal` error; this binds a runner to its row and
  covers a host's verbs, whose results arrive as JSON.
- **`status` and `help` with `json` carry a hash of the table,** so a
  cached script or skill can tell it is stale. `help` answers with no
  target running. Agent compatibility stays the layout hash's job
  ([protocol](protocol.md#handshake)).
- **Host verbs are rows of the same kind** (`ConsoleConfig::verbs`), so
  they get help, schema refusals and checked results. The row type is core;
  the rows are constant lists, not a registry: nothing registers at run
  time, and the core reaches a pack's rows through one plain function in
  `console/pack.h`. Rows live in `results/` so pure tests and the table
  tool reach them without a client. A mixed verb stays on the side of its
  state: `write`'s `undo` is the core's, its owner check is borrowed from
  the pack and refused without one
  ([source layout](design/architecture.md#source-layout)).
- **Each row names its `origin` (code owner, tool-name prefix) and its
  tool `pack`** ([tool packs](#tool-packs)), and says whether it writes and
  whether it blocks: a caller that picks a command, not types one, needs
  them ([JSON-only contract](#json-only-contract)).
- **Named types** shared by several results are listed once under `types`;
  a pack's names carry its prefix (`ue.Name`) so the core never lists a
  pack's shapes, and each origin brings its own title for the generated
  page.

Open: how far `value` can be typed (tagged shapes for FNames, structs and
containers, or documented rules as today).

## Tool interface

An AI agent should pick a command and fill in named values, not write a
command line or a script by hand: hand-written input is where guesses
enter. The [self-describing console](#self-describing-console) made those
guesses fail loudly; this removes the writing. Use: [clients](client.md).

Every fact flows one way: the console's table, then `help` with `json`, then
the generated Python functions and, beside them, the MCP tools. No layer
defines what the table did not.

- **JSON calls are the console's only input** ([JSON-only
  contract](#json-only-contract)): `{verb, args}` with args named by the
  row's keys, read by one reader for the main loop, `batch` items and the
  no-target mode.
- **Literal refs only, no `$N`.** "Result N of the last command that listed
  refs" is session state a picked argument should not depend on.
- **Typed Python functions, generated per console** (`reagentlive_gen.py`),
  host verbs included: `TypedDict` annotations let a type checker read a
  script before it runs, the strict records catch the run
  ([generated functions](client.md#generated-python-functions)).
- **Named by origin:** `c.read` for core, `c.ue.find` for a pack,
  `c.host.world` for the host; as tools `read`, `ue_find`, `host_world`. An
  agent sees what needs a pack when it picks, and a host verb cannot take
  a core verb's name.
- **MCP tools from the rows** (`frontends/mcp/reagentlive_mcp.py`, the only
  file that needs the dependency), made at run time, so no tool or JSON
  Schema is written or exported by hand and no module file goes stale.
  Verbs marked `blocks` are left out (a call would outlast a client's
  timeout); one with a `background` argument (`run`) is served as a
  background run. What the adapter serves: [MCP tools](client.md#mcp-tools).
- **`batch` carries reads only.** Its row is not marked `writes`, so its
  tool is read-only and a client may allow it unasked; a write inside it
  would run with no prompt. The adapter refuses a batch holding a `writes`
  verb or a verb of a pack that is off, and names it, so that verb goes as
  its own tool and is approved.
- **Two strict readers of the type vocabulary:** the generator maps types to
  Python annotations, the adapter to JSON Schema. A type either lacks stops
  it with the type's name, never a silent `Any`; tests generate from the
  real table.
- **Behaviour lives in the console:** paging, filtering and retrying are
  verbs, args and cursors, so the shell, scripts and tools all have them. A
  function returns the console's result key for key; a helper that converts
  says so in its name.
- **A console per tool call, never one kept open.** A kept console locked
  the executable (a rebuild failed to link) and kept running the old build.
  Runs share their state (class refs, field tables, census baselines, the
  last `get` snap) through the cache file.
- **A changed table is the adapter's at once:** each start reads the hash
  (needs no target) and the full table when it changed, then tells the
  client the list changed ([what a client sees](client.md#mcp-tools)). A
  generated module still refuses a session from another table.
- **One usage-log session per server:** the adapter passes one session id
  to every console it starts.
- **The generator and the adapter name no pack;** the layout test checks it,
  and that nothing in `core/` includes `packs/`.
- **The SDK's low-level server** (`mcp.server.lowlevel.Server`): a list
  handler over the live table and a call handler, nothing private.

| Place | Form | Why |
|---|---|---|
| Target and console | binary structs over shared memory | both ends compile the same structs and the layout hash refuses a mismatch; JSON would add a writer, a parser and allocation on the host's thread |
| Console results | JSON Lines | |
| Every call | a JSON call on one line, or `-c @<file>` | one input form; nothing built as text and parsed back |
| Addresses, chains, paths, filters, call inputs | JSON values | checked by the schema; no mini-language to guess |
| A layout, a byte pattern, a reference signature | one string argument | the same notation the labels and sig files keep |

**A smaller tool list.** Result schemas were about two thirds of the tool
list, and every result of 18 sessions together was smaller than the list.
So result schemas are compact by default: each result's top-level keys and
JSON types, widened, never narrowed, so a console result always validates.
The console still checks every result against its full type, and `help` with
`verb` and the Python records keep the full shape. Tool packs
([below](#tool-packs)) cut the rest.

**Confirmed calls:** `call` with `dry` resolves and builds the block and calls
nothing, so an approval request shows exactly what will be sent. A dry run
the client must repeat with a token waits until the marks and the client's
own prompt are not enough.

### Rejected

| Option | Why not |
|---|---|
| A schema data file as the source | needs C++ generated from it, or stays one more copy builders drift from |
| `TypedDict` in place of strict records at run time | fails only under a type checker; `.get()` on a wrong key stays silent |
| Every optional key present as `null` | a fixed shape, paid for in output on every call |
| A separate frontend for AI agents | the console is that frontend; a second one splits the contract |
| A `json` arg on `describe` | `describe` is the Unreal object lookup; one verb with two meanings |
| MCP inside the console (`--mcp`) | the protocol's messages and revisions would be written by hand in C++ |
| Two tools (`help`, `run`) or one dispatcher | `run` takes a text line: input still hand-written and unchecked; one tool cannot be both read-only and destructive |
| Tool definitions or a JSON Schema export written by hand | one more copy of every shape |
| Tools from the generated functions through FastMCP | the table became Python source, was run, read back through signatures, turned into JSON Schema and cut down again; the tools are data, and FastMCP is made for hand-written functions |
| A `reload`, `reconnect` or `disconnect` tool | the agent must know it is stale to call it, and the client's list stays as it was |
| A private copy of the console to run | usage log, native names and labels are files named after the console beside it; a copy splits them |
| Keeping the console and closing it when idle | a timer thread and a window where a rebuild still fails; only if a start per call is measured too slow |
| JSON between the target and the console | see the table above |
| Python paging helpers (`find_all`) | behaviour in one frontend only |
| Full result schemas, by default or as an option | the cost every client pays up front for detail only a script reads; the console already checks |
| A hot set plus a `discover` tool | a second way to reach a tool, and no gain once an agent needs the cold ones |
| Hiding tools by what the live host offers | the list is known before a target runs |
| Marking `batch` destructive | every same-tick read would ask |
| Leaving `batch` to the approval rules | the client never shows the inner call |

## JSON-only contract

The console's callers are AI agents, through MCP, the Python session and
scripts; none types commands. The text grammar was where the refusals came
from (agents guessed argument order and the path syntax in `get`), and every
caller already had a JSON path. So JSON is the only input.

- **An `args` schema per row** (`call/verb.h` `Arg`): `help` with `json`
  lists it with an `example` call; the MCP input schemas and Python
  signatures come from it. No positionals: every arg is named.
- **Strict checks before a builder runs** (`call/check.h`): an unknown key,
  a missing required key or a wrong type is refused with kind `schema`.
- **Typed values, no mini-languages** (`call/values.h`; syntax in
  [JSON calls](commands/console.md#json-calls)). A layout, a byte pattern
  and a reference signature stay one string each.
- **Agent-first refusals:** a refusal carries the failing key's schema (or
  the whole list) and one corrected `example` built from the call, so the
  next call is a fix, not a guess.
- **One source for verb text.** The row is the only place a verb's name,
  pack, args, notes and example are written; the
  [verb reference](commands/reference.md) and
  [result shapes](commands/results.md) are generated from it, hand-written
  pages keep rationale and gotchas. Tests fail on a doc that names a verb or
  arg the table lacks, writes the removed text grammar, or shows a JSON call
  the console would refuse ([contributing](contributing.md#tests)). Error
  kinds and message templates are one catalogue (`call/refusals.h`).
- **Plan files for scripted runs** (`call/plan.h`): `run` executes a JSON
  list of steps as its own calls and writes NDJSON and a report
  ([plans](commands/console.md#plans)). Interactive verbs stay one call.
- **One `view` for output shaping.** What only shapes a result (`start`,
  `limit`, `match`, `fields`, `nonzero`, `elements`) is one object with one
  schema (`ViewKeys`) on every list-returning verb. A view key given at the
  top is refused with where it goes. Options that decide what the agent
  reads or does (`disasm`'s `count`, `search`'s `max`) are not a view.
- **An options budget:** a table test caps own args per verb. Related
  options are one typed object, exclusive flags one enum, and a mode with
  another result shape is another verb (`call-each` beside `call-pure`).

| Rejected | Why |
|---|---|
| Keeping the text grammar as a frontend for people | the users are agents; two input forms double the parser, tests and docs |
| A job file per call for every verb | an interactive `find` or `get` stays one JSON line; replayable plans are their own verb |
| Positionals in the JSON form | order carried meaning a caller had to guess |
| A shorthand string beside each typed value (`"A.B"`) | two forms again, and the schema could no longer check the value |
| Output flags per verb at the top | the same option under different help on every verb, and the count kept growing |
| A view with every key on every verb, unused ones ignored | a silently ignored key reads as a filter that ran |

## Tool packs

A session should load the tools its job needs, and only a person should
decide that it may change the target. Every verb belongs to one workflow
pack, sized so a normal question stays inside the default ones:

| Pack | Job | Holds |
|---|---|---|
| `explore` (always on) | find objects and read their state and data | the table and session verbs, finds, field and data reads, names, function metadata, memory reads |
| `native` | read code and keep names across builds | disassembly, references, signatures, memory search, labels, layout files |
| `observe` | watch behaviour over time without changing it | captures, census, memory snapshots, `line-trace`, `call-pure`, `call-each`, `timed-writes` (listing) |
| `modify` | change the target or call into it | `write` (and `clear`), `call`, `release`, the call recorder, `game-thread` |

Membership is each row's `pack`, a one-line change.

- **Exact marks first.** A verb's mark is per verb, so risk must be too:
  `each` reads fields only and its function form is `call-each`; `game-thread`
  keeps only `on` and `off`, the mode is read in `status`. A test parses
  every form of every verb outside `modify` and checks none writes or calls.
- **The invariant:** a verb marked `writes` is in `modify`; `explore`,
  `native` and `observe` hold none. `calls-read` and `calls-stop` read or
  restore only but live with their recorder.
- **One ceiling, in the console.** `--packs` refuses a verb of any other
  pack with kind `pack`, `batch` items included, so the boundary holds
  whichever frontend sends the call ([console](commands/console.md#one-shot-use)).
  A person at the keyboard gets all four by default.
- **Runtime enable inside the ceiling.** The adapter's `packs` tool turns a
  pack on or off within `--allow` ([MCP tools](client.md#mcp-tools)): only
  the person starting the server can make `modify` reachable.

| Rejected | Why |
|---|---|
| Two servers, read-only and writing | cuts the list by a fifth only, and a read-only server still loads every research tool it never calls; its safety idea is the ceiling here |
| Eight to ten fine packs | a normal question crosses three or four; the agent spends turns enabling |
| Client-side deferral of schemas only | client-specific, and no safety gain |
| Enabling single verbs | packs are the unit; finer is the fine-packs design again |
| A pack per engine | `origin` stays the code owner; another engine reuses the same four workflow packs |
