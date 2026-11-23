# Tool interface for scripts and agents

*Home of:* the decisions behind the self-describing console and the tool interface (the verb table, `help` with `json`, JSON calls, the generated Python functions, the MCP adapter), with their rejected alternatives and measurements.

**TL;DR:** Why the console describes itself and why scripts and AI agents
call it the way they do. One verb table in the console is the source of
every argument and result shape; Python functions and MCP tools are made
from it, never written by hand. This page is the decision record: how to
use each interface is [clients](client.md), how to drive the console is
[console](commands/console.md).

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
  (`call/verb.h`; rows in `results/verbs.h` and `packs/ue/cli/results/verbs_ue.h`)
  describes each command's args and result type. The checks of a call,
  its refusals, `help`, `help` of one verb, `help` with `json`, the generated
  [result shapes](commands/results.md) page and the Python client's
  strictness all read it, so no shape is written twice.
- **One verb, one result type.** An arg may add optional keys and never
  changes a key's type (`census`'s `diff` adds `before`; `read`'s `type` with a
  layout fills `layout`, not `values`); a mode that would change one stays a
  verb of its own (`sig-bytes`, `watch-start`). One name for a result's list
  (`rows`), one paging convention (`from`, `next_cursor`), `capped` for a
  list cut at a hard limit and `truncated` for an incomplete value.
- **Typed result structs with one field list.** A result is a struct
  whose `Describe` lists key and member once (`results/schema.h`); that
  list writes the JSON and describes it, so a builder cannot emit a key
  the description lacks. Tests check values, not key sets.
- **A small type vocabulary, not a schema language:** `addr`, `hex`,
  `bytes`, `ref`, the plain scalars, string enums, optional keys, lists,
  maps, nested objects, and `value` for game data, which is dynamic and
  stays untyped.
- **Optional keys are omitted, never null.** Output weight is a
  [principle](design/console-costs.md#output-weight-and-dead-ends), and the strict layer gives
  `None` either way. Nulls remain only inside game values and as list
  elements where position matters (a capture row's `values`).
- **The console checks what it prints.** A result that does not match its
  row's type is an `internal` error. This binds a runner to its row and
  covers a host's verbs, whose results arrive as JSON.
- **`status` carries a hash of the table,** so a cached script or skill
  can tell it is stale. It describes the console build; agent
  compatibility stays the layout hash's job
  ([protocol](protocol.md#handshake)). `help` with `json` carries it too and
  answers with no target running, so tools can read the table before a
  game does.
- **Not a `json` arg on `describe`:** `describe` is the Unreal pack's object lookup,
  and one verb with two meanings is what the first decision removes.
- **Strict Python results:** a guessed key raises at the line that
  guessed it ([Python session](client.md#python-session)).
- **Host verbs are rows of the same kind.** A host hands the console a
  constant list of rows (`ConsoleConfig::verbs`) in place of help text, so
  its commands get help, schema refusals and checked results the same way.
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
  type from a small vocabulary, whether it is required, a default where it
  has one, whether it is a list, the words of an enum and a line of
  description: a caller that picks a command, not types one, needs them
  all ([JSON-only contract](#json-only-contract)).
- **Each row names its tool pack** (`pack`), set where the row is
  defined: a second axis beside `origin`, which stays the code owner and
  the tool-name prefix ([tool packs](#tool-packs)).
- **Named types.** A struct several results share names itself
  (`TypeName`): `help` with `json` lists it once under `types`, rows refer to it
  (`{"named": "Chain"}`), and `help` with `verb` prints the name and its
  definition. A pack's names carry its prefix (`ue.Name`), so the core
  never lists a pack's shapes. `Object` (ref, name, class) is the core's,
  filled by a pack: `peek` is core and its object rows are typed without
  naming the pack.
- **A pack gives its own title.** Each origin (`core`, a pack, `host`)
  comes with a title in `help` with `json`; the generated results page takes
  its headings from there, so no core file spells a pack's id.
- **Mixed verbs stay on the side of their state.** `write`'s `undo` is the
  core's (the write, the undo, the guard); its owner check is borrowed
  from the pack through `console/pack.h`, and without a pack it is
  refused ([source layout](design/architecture.md#source-layout)).

Rejected:

| Option | Why not |
|---|---|
| A schema data file as the source | needs C++ generated from it, or stays one more copy the builders can drift from |
| `TypedDict` in place of strict records at run time | fails only under a type checker; `.get()` on a wrong key stays silent. The generated functions annotate with them and return records |
| Every optional key present as `null` | a fixed shape, paid for in output on every call |
| A separate frontend for AI agents | the console is that frontend; a second one splits the contract |
| An option selecting one result key on every command | deferred until a shell caller needs the strict layer's error where a JSON filter prints null |

Open: how far `value` can be typed (a tagged shape for FNames, structs and
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
  row's keys, nothing quoted or escaped, read by one reader for the main
  loop, `batch` items and the no-target mode.
- **Literal refs only.** There is no `$N`: "result N of the last command
  that listed refs" is session state a picked argument should not depend
  on. A call takes the `ref` a result carries, and the ids of timed
  writes and captures.
- **Typed Python functions, generated per console** (`reagentlive_gen.py`),
  so a host's verbs are included: `TypedDict` annotations let a type
  checker read a script before it runs, and the strict records catch the
  run ([generated functions](client.md#generated-python-functions)).
- **Named by origin:** `c.read` for core, `c.ue.find` for a pack,
  `c.host.world` for the host; as tools `read`, `ue_find`, `host_world`. An
  agent sees what needs a pack when it picks, and a host verb cannot take
  a core verb's name.
- **MCP tools from the rows** (`frontends/mcp/reagentlive_mcp.py`, the
  only file that needs the dependency): each tool is made from its row at
  run time, so no tool or JSON Schema is written by hand or exported, and
  no module file can go stale. Verbs marked `blocks` are left out: a call
  would outlast a client's timeout, while a script may wait. What the
  adapter serves: [MCP tools](client.md#mcp-tools).
- **`batch` carries reads only.** Its row is not marked `writes`, so its
  tool is read-only and a client may allow it unasked; a write, call or
  `game-thread` inside it would then run with no prompt. The adapter
  refuses a batch holding any verb marked `writes` or a verb of a pack
  that is off, and names it, so that verb goes as its own tool and is
  approved. Rejected: marking `batch`
  destructive (every same-tick read would ask), and leaving it to the
  approval rules (the client never shows the inner call).
- **Two readers of the type vocabulary, both strict.** The generator maps
  the argument and result types to Python annotations, the adapter to
  JSON Schema. A type either lacks stops it with the type's name, never a
  silent `Any` or `str`; the session and MCP tests generate from the real
  table, so a type added to `call/verb.h` alone fails them.
- **Same shape as the console.** A function returns the console's result
  key for key; an `addr` stays its hex text. A helper that converts says
  so in its name. **Behaviour lives in the console:** paging to the end,
  filtering and retrying are verbs, args and cursors, so the shell,
  scripts and tools all have them.
- **A console per tool call, never one kept open.** A kept console locked
  the executable (a rebuild failed to link) and kept running the old build.
  Each call starts a console and ends it, so the next call runs the new
  build; what runs share (class refs, field tables, census baselines, the
  last `get` snap) is in the cache file. Measured with the own-thread
  host: about 17 ms a call against 2.4 ms on a kept console; a 530 KB
  cache file adds under 1 ms.
- **A changed table is the adapter's at once.** Each start reads only the
  hash (`help` with `hash`, no target needed; `status` would need one) and
  the full table when it changed, then tells the client the list changed
  ([what a client sees](client.md#mcp-tools)). A generated module still
  refuses a session from another table, which keeps scripts honest.
- **One usage-log session per server:** the adapter passes the same
  session id to every console it starts (a console option).

| Rejected for the adapter | Why |
|---|---|
| A `reload`, `reconnect` or `disconnect` tool | the agent has to know it is stale to call it, and the client's list stays as it was |
| A private copy of the console to run | the usage log, native names and labels are files named after the console beside it; a copy splits them |
| Keeping the console and closing it when idle | a timer thread and a window in which a rebuild still fails; only if a start per call is measured too slow |
| Regenerating the module file on every build, and nothing else | a second copy, and the running server keeps its old list |
| Watching the executable for changes | the next call sees the new table anyway |
| Skipping the stale-table check for the adapter | reopens for scripts what the check closes |
| Tools from the generated functions through FastMCP | the table became Python source, was run, read back through signatures, turned into JSON Schema and cut down again, and four of the steps reached into the SDK's private parts; the tools are data, and FastMCP is made for functions written by hand |
| A null argument refused | clients send null for a left-out argument; the adapter drops nulls and checks the rest against the row's schema itself |
- **The generator and the adapter name no pack**; the layout test checks
  that they never spell a pack's id, that no core console file does, and
  that nothing in `core/` includes `packs/`.
- **The SDK's low-level server** (`mcp.server.lowlevel.Server`): a list
  handler that reads the live table and a call handler, nothing private;
  the SDK it needs: [MCP tools](client.md#mcp-tools).

| Place | Form | Why |
|---|---|---|
| Target and console | binary structs over shared memory | both ends compile the same structs and the layout hash refuses a mismatch; JSON would add a writer, a parser and allocation to the agent, on the host's thread |
| Console results | JSON Lines | |
| Every call (generated functions, the adapter, a shell's `-c`) | a JSON call on one line, or `-c @<file>` | one input form; nothing is built as text and parsed back |
| An address, a chain, a path, a filter, call inputs | JSON values (`{"module", "rva"}`, `{"base", "chain"}`, `["A", "B"]`, `{"field", "contains"}`, `{"Name": value}`) | checked by the schema; no mini-language to guess |
| A layout, a byte pattern, a reference signature | text, as one string argument | the same notation the labels and sig files keep |

Rejected:

| Option | Why not |
|---|---|
| MCP inside the console (`<console> --mcp`) | no second process, but the protocol's messages and every later revision would be written and followed by hand in C++ |
| Two tools (`help`, `run`) | `run` takes a text line, so the input is still written by hand and unchecked |
| Tool definitions written by hand, or a JSON Schema export from the console | one more copy of every shape; the functions' types already give the schemas |
| A remote server (HTTP, authorisation) | one local console and one target; nothing here is remote |
| JSON between the target and the console | see the table above |
| Python paging helpers (`find_all`, `search_addresses`) | behaviour in one frontend only; the console's cursors and limits serve all of them |

**A smaller tool list.** Measured, result schemas were about two thirds of
the list (about 72 KB of 112), and every result of 18 sessions together
was smaller than the list. So:

- **Compact result schemas by default:** each result's top-level keys and
  JSON types, widened, never narrowed, so a console result always
  validates. The strictness that matters stays where it was: the console
  checks every result against its full type, and `help` with `verb` and the
  Python records keep the full shape. `off` is an adapter flag.
- **Tool packs** pick the rest ([below](#tool-packs)).

| Rejected | Why |
|---|---|
| Full schemas, by default or as an option | the cost every client pays up front for detail only a script reads, and the console already checks; as an option it needed a whole result type in JSON Schema, kept for no caller |
| A hot set plus a `discover` tool (tools loaded on request) | a second way to reach a tool, and no gain once an agent needs the cold tools; revisit if the compact list is still too large for a client |
| Hiding tools by what the live host offers | the list is known before a target runs ([source layout](design/architecture.md#source-layout)) |

**Confirmed calls:** `call` with `dry` resolves and builds the block and calls
nothing, so an approval request shows exactly what will be sent. A dry run
the client must repeat with a token waits until the marks and the
client's own prompt are not enough.

## JSON-only contract

The console's callers are AI agents, through MCP, the Python session and
scripts; none types commands. The text grammar was where the refusals came
from (91 in one usage log, 20 of them `get` syntax: agents guessed
argument order and the path syntax), and every caller already had a JSON
path. So JSON is the only input.

- **An `args` schema per row** (`call/verb.h` `Arg`): key, type, help,
  default, required, list, and an example value where the type's own would
  be refused. `help` with `json` lists it per row as `args` (an enum as `str`
  with `enum`) with an `example` call; the MCP input schemas and the
  Python signatures come from it. There are no positionals: every arg is
  named.
- **Strict checks before a builder runs** (`call/check.h`): an unknown key,
  a missing required key or a value of the wrong type is refused with kind
  `schema`, naming the key and the type it takes.
- **Typed values, no mini-languages** (`call/values.h`): a path is a list
  of field names, a pointer chain `{"base", "chain"}`, an address `"0x..."`,
  a literal ref, `{"module", "rva"}` or `{"base", "offset"}`, a filter
  `{"field", "contains"}`, call inputs an object of name to value, a trace
  end `[x, y, z]`, a ref or `{"object", "socket"}`, a watch range an
  object. A layout, a byte pattern and `sig-check`'s reference signature
  stay one string each: the files that keep them (labels, sig files) use
  the same text.
- **Agent-first refusals.** A refusal carries the failing key's part of
  the schema (`arg`), or the whole list (`args`) when no one key is at
  fault, and one corrected `example` built from the call: the values that
  fit are kept, a misspelt key moves to the key it was meant for, the
  key at fault is dropped (an optional one) or replaced by an example of
  its type, and missing required keys get one. A builder's own refusal
  (a range, a combination) starts with the key it is about, so it gets
  the same treatment. Name and path errors from the target keep naming
  the nearest fields.
- **Input:** `-c`, `@<file>` and stdin carry the same JSON calls
  ([JSON calls](commands/console.md#json-calls)).
- **One source for verb text.** The row is the only place a verb's name,
  pack, args (with help), notes and example are written;
  [the verb reference](commands/reference.md) is generated from it like
  [result shapes](commands/results.md), and hand-written pages keep
  rationale, workflows and gotchas. `check_verb_names.py` (ctest
  `reagentlive.verb-names`) fails on a doc that names a verb or arg the
  table lacks, or still writes a call in the removed text grammar (a verb
  followed by words or flags, `-c` without JSON); a host runs it on its
  own docs. `check_json_examples.py` (`reagentlive.json-examples`) runs
  every JSON call a doc shows through the console's own checks with no
  target, the way the help examples are tested. Error kinds and their
  message templates are one catalogue (`ErrorKinds`, `call/refusals.h`):
  a handler names an `ErrorKind`, never a string.
- **Plan files for scripted runs** (`call/plan.h`): `run` executes a JSON
  list of steps (`{verb, args, expect?, repeat?}`) as its own calls and
  writes NDJSON lines and a report; the file is the replay key
  ([plans](commands/console.md#plans)). Interactive verbs stay one call.
- **One `view` for output shaping.** What only shapes a result (a page's
  `start`, `limit`, `match`, `fields`, `nonzero`, `elements`) is one
  object, `view`, with one schema (`ViewKeys` in `call/verb.h`) on every
  list-returning verb; a verb takes the keys that apply to its result,
  and its notes say what `match` matches and what `limit` 0 means. A view
  key given at the top is refused with where it goes, and the example
  moves it there. Options that decide what the agent reads or does
  (`disasm`'s `count`, `search`'s `max`) are not a view.
- **An options budget.** No verb takes more than six own args beside
  `view` (a table test). Related options are one typed object (`search`'s
  `scope`, `write`'s `undo`, `disasm`'s `save`), exclusive flags one enum
  (`get`'s `mode`), and a mode with another result shape is another verb
  (`call-each` beside `call-pure`). A new option replaces one or says in
  the verb's notes why nothing else covers it. An object's keys are read
  as the verb's own once checked, so a row never uses a name twice.

| Rejected | Why |
|---|---|
| Keeping the text grammar as a frontend for people | the console's users are agents; two input forms double the parser, its tests and the docs, and the text form was where the refusals came from |
| A job file per call (the "timeless CLI" model) for every verb | an interactive `find` or `get` stays one JSON line; replayable plans are a verb of their own |
| Positionals in the JSON form | order carried meaning a caller had to guess |
| A shorthand string beside each typed value (`"A.B"` for `["A", "B"]`) | two forms again, and the schema could no longer check the value |
| `$N` in JSON calls | session state a picked argument should not depend on; literal refs work across runs |
| Output flags per verb (`limit`, `nonzero`, ... at the top) | the same option under different help on every verb, and the count per verb kept growing (`search` 12, `get` 10) |
| A view with every key on every verb, unused ones ignored | a silently ignored key reads as a filter that ran |

## Tool packs

A session should load the tools its job needs, and only a person should
decide that it may change the target. Every verb belongs to one workflow
pack, sized so a normal question stays inside the default ones:

| Pack | Job | Holds |
|---|---|---|
| `explore` (always on) | find objects and read their state and data | the table and session verbs, finds, field and data reads, names, functions' metadata, memory reads |
| `native` | read code and keep names across builds | disassembly, references, signatures, memory search (`search`, `search-job`), labels, layout files |
| `observe` | watch behaviour over time without changing it | captures, census, memory snapshots, `line-trace`, `call-pure`, `call-each` |
| `modify` | change the target or call into it | `write`, `timed-writes`, `call`, `release`, the call recorder, `game-thread` |

`help` with `json` is the list's source (each row's `pack`); membership is a
one-line change in the row.

- **Exact marks first.** A verb's mark is per verb, so risk must be too:
  `each` reads fields only and its function form is `call-each`; `status` reads the game-thread mode and
  `game-thread` keeps only `on` and `off`. A test parses every form of
  every verb outside `modify` and checks none writes or calls (a `line-trace`
  calls a line trace that changes no state; a `batch`'s items are checked
  as their own verbs).
- **The invariant:** a verb marked `writes` is in `modify`; `explore`,
  `native` and `observe` hold none. `calls-read` and `calls-stop` are
  read or restore only, but live with their recorder.
- **One ceiling, in the console.** `--packs` on the console
  ([console](commands/console.md#one-shot-use)) refuses a verb of any
  other pack with kind `pack`, `batch` items included, so the boundary
  holds whichever frontend sends the call (the adapter and the Python
  session pass theirs down). A person at the keyboard gets all four by
  default.
- **Runtime enable inside the ceiling.** The adapter's `packs` tool turns
  a pack on or off within `--allow` ([MCP tools](client.md#mcp-tools)):
  only the person starting the server can make `modify` reachable.

Measured on one host's console with two host verbs (tool JSON as the
adapter builds it: name, description, input schema and compact output
schema, plus the `packs` tool):

| Served | Tools | Characters |
|---|---|---|
| every pack | 51 | about 73,000 |
| `explore`, `native`, `observe` | 43 | about 61,000 |
| `explore`, `native` | 35 | about 47,000 |
| `explore` | 22 | about 26,000 |

| Rejected | Why |
|---|---|
| Two servers, read-only and writing | cuts the list by a fifth only, and a read-only server still loads every research tool it never calls; its safety idea is the ceiling here |
| Eight to ten fine packs (objects, code, signatures, names, captures...) | a normal question crosses three or four; the agent spends turns enabling |
| One dispatcher tool (`run` and `help`) | one tool cannot be both read-only and destructive, and with no schemas more calls fail on syntax |
| Client-side deferral of schemas only | client-specific, and no safety gain |
| Enabling single verbs | packs are the unit; anything finer is the fine-packs design again |
| A pack per engine | `origin` stays the code owner and the tool prefix; another engine pack reuses the same four workflow packs |
