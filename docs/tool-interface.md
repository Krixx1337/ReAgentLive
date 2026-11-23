# Tool interface for scripts and agents

*Home of:* the decisions behind the self-describing console and the tool interface (the verb table, `help --json`, JSON calls, the generated Python functions, the MCP adapter), with their rejected alternatives and measurements.

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
  (`syntax/verb.h`; rows in `results/verbs.h` and `packs/ue/cli/results/verbs_ue.h`)
  describes each command's positionals, flags and result type. Parsing,
  usage errors, `help`, `help <verb>`, `help --json`, the generated
  [result shapes](commands/results.md) page and the Python client's
  strictness all read it. Each shape used to be written five times
  (builder, key pins in a test, the docs row, a Python docstring, the
  skill) and agreed only because they were edited together.
- **One verb, one result type.** A flag may add optional keys and never
  changes a key's type (`census --diff` adds `before`; `read --type` with a
  layout fills `layout`, not `values`); a mode that would change one stays a
  verb of its own (`sig-bytes`, `watch-start`). One name for a result's list
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
  [principle](design.md#output-weight-and-dead-ends), and the strict layer gives
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
- **Mixed verbs stay on the side of their state.** `write --undo` is the
  core's (the write, the undo, the guard); its owner check is borrowed
  from the pack through `console/pack.h`, and without a pack it is
  refused ([source layout](design.md#source-layout)).

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
the generated Python functions and, beside them, the MCP tools. No layer
defines what the table did not.

- **JSON calls are the console's input; the text line is shorthand.** A
  line starting with `{` is `{verb, args}` with arguments named by the
  row's keys. It sorts into the same arguments as a typed line, so one
  check and one parser per verb cover both, and nothing is quoted or
  escaped. The text line stays while agents and people type in a shell.
  The console reads each line once into its verb, its arguments and the
  line flags (`--meta`, `--timing`, or a call's `meta` and `timing`);
  the main loop, a `batch`'s items and the no-target mode share that one
  reader.
- **Literal refs only in JSON calls.** `$N` is refused: "result N of the
  last command that listed refs" is session state a picked argument should
  not depend on. A call takes the `ref` a result carries, and the ids of
  pins, trials and captures.
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
- **MCP tools from the rows** (`frontends/mcp/reagentlive_mcp.py`, the
  only file that needs the dependency): the adapter reads the console's
  `help --json` and makes each tool from its row: the input schema from
  its positionals and flags, the output schema from its result's
  top-level keys, the description from its summary, usage and notes,
  `readOnlyHint` or `destructiveHint` from `writes`. A call goes to the
  console as a JSON call with the row's keys, since those are the tool's
  argument names. No tool and no JSON Schema is written by hand or
  exported from the console, and there is no module file to go stale.
  Verbs marked `blocks` and `quit` are left out (a call would outlast a
  client's timeout; the generated Python functions keep them, since a
  script may wait). `batch` takes a list of calls and runs them held.
  Every tool also takes `budget` (the console's
  [output budget](commands/console.md#fewer-round-trips), 25,000 characters
  by default); a result past it comes back as `{digest}` with a text block
  naming the call to make next, and the output schema allows either.
- **`batch` carries reads only.** Its row is not marked `writes`, so its
  tool is read-only and a client may allow it unasked; a write, call, pin
  or `game-thread` inside it would then run with no prompt. The adapter
  refuses a batch holding any verb marked `writes` and names it, so that
  verb goes as its own tool and is approved. Rejected: marking `batch`
  destructive (every same-tick read would ask), and leaving it to the
  approval rules (the client never shows the inner call).
- **Two readers of the type vocabulary, both strict.** The generator maps
  the argument and result types to Python annotations, the adapter to
  JSON Schema. A type either lacks stops it with the type's name, never a
  silent `Any` or `str`; the session and MCP tests generate from the real
  table, so a type added to `syntax/verb.h` alone fails them.
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
  census baselines, the last `get --snap`) is in the cache file. Calls hold a
  lock, since a second console would wait and then be refused as busy
  ([round trips](design.md#round-trips)). Measured with the
  own-thread host: about 17 ms a call against 2.4 ms on a kept console,
  most of it the table read that a hash read replaced; a 530 KB cache file
  adds under 1 ms. Not yet measured: a pack-heavy `get` with the game's
  cache loaded.
- **A changed table is the adapter's at once.** Each start reads only the
  hash (`help --hash`, no target needed; `status` would need one) and the
  full table when it changed. The call that saw the change is checked
  against its verb's new row before it runs, the tool list is read from
  the new table, and the adapter tells the client the list changed. A
  client that ignores the notice keeps its list: a tool whose verb kept
  its arguments and result works; changed arguments are refused with the
  usage, a changed result by the client's own check against the old type,
  a removed verb by the adapter. Those need a reconnect. A generated
  module still refuses a session from another table, which keeps scripts
  honest.
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
| Tools from the generated functions through FastMCP | the table became Python source, was run, read back through signatures, turned into JSON Schema and cut down again, and four of the steps reached into the SDK's private parts; the tools are data, and FastMCP is made for functions written by hand |
| A null argument refused | clients send null for a left-out argument; the adapter drops nulls and checks the rest against the row's schema itself |
- **The generator and the adapter name no pack**; the layout test checks
  that they never spell a pack's id, that no core console file does, and
  that nothing in `core/` includes `packs/`.
- **The SDK's low-level server** (`mcp.server.lowlevel.Server`): a list
  handler that reads the live table and a call handler, nothing private.
  The adapter returns the console's result as it is, as text and as
  structured content; it says it sends the list-changed notice through the
  server's own initialisation options.
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

**A smaller tool list.** Measured, result schemas were about two thirds of
the list (about 72 KB of 112), and every result of 18 sessions together
was smaller than the list. Measured once after the change, as the
serialized tool list: about 58 KB for the own-thread example's console
(core and pack verbs) with compact schemas; on one host's console with two
host verbs, before the adapter made schemas from the rows, about 45 KB with
`off` and 30 KB with `--families core --read-only`. So:

- **Compact result schemas by default:** each result's top-level keys and
  JSON types, widened, never narrowed, so a console result always
  validates. The strictness that matters stays where it was: the console
  checks every result against its full type, and `help <verb>` and the
  Python records keep the full shape. `off` is an adapter flag.
- **Families and read-only** are adapter flags over origins and `writes`,
  the marks the table already has. Nothing in the console changes, so
  `help`, the hash and the generated module stay the same for every
  client.

| Rejected | Why |
|---|---|
| Full schemas, by default or as an option | the cost every client pays up front for detail only a script reads, and the console already checks; as an option it needed a whole result type in JSON Schema, kept for no caller |
| A hot set plus a `discover` tool (tools loaded on request) | a second way to reach a tool, and no gain once an agent needs the cold tools; revisit if the compact list is still too large for a client |
| Hiding tools by what the live host offers | the list is known before a target runs ([source layout](design.md#source-layout)) |

**Confirmed calls:** `call --dry` resolves and builds the block and calls
nothing, so an approval request shows exactly what will be sent. A dry run
the client must repeat with a token waits until the marks and the
client's own prompt are not enough.
