# Tool interface (plan)

Status: planned, nothing built. It builds on the
[self-describing console](design.md#self-describing-console), which
shipped: everything here is generated from its verb table. A plan, not a
feature: delete this page when it ships, after moving the decisions to
[design](design.md) and the behaviour to the command pages.

## The goal

An AI agent should pick a command and fill in named values, not write a
command line or a script by hand. Hand-written input is where guesses
enter; a picked tool with checked arguments leaves no room for one.

## What is still written by hand

| What | Written by hand | Failure |
|---|---|---|
| A command line | verb, flag names, order, quoting | refused with the usage: loud, and one round trip lost |
| A layout, pattern or path in a shell | escapes | a mangled argument that may still parse |
| A script | every call and every key | loud at run time, after the session has started |

The self-describing console makes these failures loud. This plan removes the writing.

## Decision

Two layers, both generated from `help --json`, the second built on the
first.

1. **Typed Python functions, generated per console.** One function per
   verb: positionals and flags as typed parameters, the result as a typed
   object, with a stub file so a type checker reads a script before it
   runs. Generated from the console it will drive, so a host's verbs are
   included; the table hash says when to generate again. The strict
   results the session client has today stay underneath, and it stays
   standard library only.
2. **An MCP server that wraps those functions,** on
   [FastMCP](https://github.com/PrefectHQ/fastmcp): it makes a tool from a
   typed Python function, argument and result schemas included. The
   adapter is one small file in `frontends/mcp/`, the only one that needs
   the dependency. It starts the console as the session client does and
   keeps it open.
3. **No tool is written by hand,** and no JSON Schema is exported from
   the console: the schemas come from the functions' types.
4. **The tool list is the console's verbs,** known without a target. A
   verb the live host cannot run (a pack it lacks) is refused with the
   message it gives today.
5. **A `batch` tool** takes a list of calls (verb and arguments) and runs
   them held, as `batch` does. Without it every tool call is its own
   round trip and same-tick reads are lost.
6. **Filters and limits are arguments** (`match`, `nonzero`, `limit`,
   `from`). A tool result lands whole in the caller's context, so the
   defaults stay small.
7. **Tools state whether they write.** Read-only verbs are marked so a
   client can allow them without asking; verbs that write memory or call
   into the target are marked so the client asks first. The host's
   approval rule stays the host's.
8. **State goes in arguments, not in the session.** A tool takes the
   literal `ref` a result carries, and the ids of pins, trials and
   captures; `$N` is refused. The protocol's current revision has no
   sessions, and "the last `find`" is a guess a picked argument should
   not depend on.
9. **Target the 2026-07-28 protocol revision only,** over standard input
   and output. No support for earlier revisions: there is no older client
   to serve.

## Where JSON is used

The callers are programs and AI agents, not people, so JSON is the form
wherever one of them touches the console.

| Place | Form | Why |
|---|---|---|
| Target and console | binary structs over shared memory, as today | both ends compile the same structs and the layout hash refuses a mismatch; JSON would add a writer, a parser and allocation to the agent, on the host's thread, to replace checked fields with string keys |
| Console results | JSON Lines, as today | |
| A call from the generated functions or the adapter | JSON: `{verb, args}` on one line | no text line is built and parsed back, so nothing is escaped; a `batch` is a list of calls |
| A call typed in a shell | the text line (`read 0x1000 16`) | JSON is hard to quote in a shell and several times the length |
| An address or chain, a layout, a byte pattern | text, as one string argument | a compact notation for one value |

- **The JSON call is the console's input; the text line is shorthand for
  it.** One generic parser reads a text line from the verb's row
  (positionals in order, then flags) into the same arguments a JSON call
  carries. The parsers written per verb go as it covers them.
- **Arguments are checked once,** against the row, whichever form they
  arrived in.
- **The shorthand goes when nothing types in a shell any more.** Until
  the tools are the usual way in, an agent still does.

## Rules that keep the layers apart

Every fact flows one way: the console's table, then `help --json`, then
the Python functions, then the tools. No layer defines what the one
before it did not.

- **Same shape as the console.** A generated function returns the
  console's result, key for key, with its types: an `addr` stays the hex
  string the console prints. A helper that converts (to bytes, to ints)
  is separate and says so in its name.
- **Behaviour lives in the console.** Paging to the end, filtering,
  retrying: a convenience wanted in Python becomes a verb or a flag, so
  the shell, scripts and tools all have it.
- **A stale module does not start.** The adapter compares the generated
  module's table hash with the console's and refuses on a mismatch.

## What the table still lacks

The self-describing console shipped with these already in its table: a
`writes` mark per verb, a line of description per argument, `help --json`
with no target attached, the table hash in `help --json`, and an `origin`
per verb (`core`, a pack's name, or `host`). What is still missing is this
plan's step 0:

| Needed | For | Why |
|---|---|---|
| Argument types and defaults in `help --json` (today: a name, an arity or what a flag takes, a placeholder and a description) | decision 1 | a generator given only names would guess whether `count` is a number |
| `Object` {`ref`, `name`, `class`} as a core type a pack fills | core and pack, below | `peek` is core and its rows carry a pack's object; today that is an untyped `value` so the core need not know its shape |
| Named types a pack adds to the vocabulary (names, tags, field metadata) | core and pack, below | the core must not list a pack's shapes; today nested types repeat inline (every `chain`) |
| A side chosen for each mixed verb | core and pack, below | `trial` is core and its owner check needs the pack |

The hand-written Python methods need no more than they have: decision 1
replaces them.

## Core and pack

The console's layout already keeps them apart
([design](design.md#source-layout)): the pack's part of each layer is
its `ue/` folder, and a test fails when anything outside names the pack.
Rows and result types follow that rule. This plan adds what the layout
cannot say.

- **The origin reaches the caller.** Functions and tools are named by
  origin: `read` for core, `ue.find` for a pack, `host.<verb>` for a
  host (as a tool name, `ue_find`). An agent sees what needs the pack
  when it picks, and a host verb cannot take a core verb's name.
- **The generator and the adapter name no pack.** They read
  `help --json` and nothing else; the layout test covers them as it
  covers the console's core.
- **A second pack adds rows and types under its own name,** and changes
  nothing in the generator, the adapter or the core's types.
- **A pack gives its own display name.** The core maps no pack id to a
  heading (`results/docs.h` does today, for the generated results page);
  the name comes with the pack's rows.
- **The boundary is tested wherever it exists,** not only in
  `frontends/cli`: a check that nothing in `core/` includes `ue/`, and
  that no core file of the console spells a pack's id as a string, which
  the test's `ue::` rule does not see.

## Pick or script

| Work | Use | Why |
|---|---|---|
| One lookup, or a few | a tool | checked before it is sent; the result is in view |
| Related reads at one moment | the `batch` tool | one tick |
| A loop, a sweep, a comparison over many objects | a script on the generated functions | one call; only the summary returns |
| A capture that outlasts one call | `watch-start`, then a tool or a script to read it | a tool call should not block for seconds |

## What this reverses

It takes effect when this plan starts; until then these stand.

- **"No large tool surface"** in
  [design](design.md#place-among-other-tools) and the declined
  [MCP frontend](ideas.md#11--mcp-frontend). The reasons were the cost of
  keeping a tool per operation in step and of loading every schema before
  any work. The table removes the first. Clients that load a tool's
  schema only when it is picked remove most of the second; a client that
  loads all of them still pays it.
- **A JSON Schema export from the console,** once considered for outside
  tools: decision 3 makes it unnecessary.

- **"JSON input lines", rejected** when the console was made
  self-describing because the text line is compact and what agents type.
  That holds in a shell and nowhere else
  ([where JSON is used](#where-json-is-used)).

## Order of work

| Step | What |
|---|---|
| 0 | What the table still lacks (above) |
| 1 | JSON calls into the console, and the generic text parser in place of the per-verb ones |
| 2 | The generator: typed functions and stubs from `help --json`, sending JSON calls (decision 1) |
| 3 | The adapter: tools from those functions, read or write marks, literal refs (2, 3, 4, 7, 8, 9) |
| 4 | The `batch` tool and filter arguments (5, 6) |

Step 2 is useful alone: scripts get checked arguments and keys before
any tool exists. Step 3 is checked with the MCP Inspector, so no AI
client is needed to see the tool list and call a tool.

## Rejected

| Option | Why not |
|---|---|
| MCP inside the console (`<console> --mcp`) | no second process, but the protocol's messages and every later revision would be written and followed by hand in C++ |
| The official Python SDK in place of FastMCP | fewer dependencies and enough for today's adapter, but the richer library was chosen: its in-process test client, middleware and server composition are there when the adapter grows |
| Two tools (`help`, `run`) | `run` takes a text line, so the input is still written by hand and unchecked |
| Tool definitions written by hand | one more copy of every shape |
| A remote server (HTTP, authorisation) | one local console and one target; nothing here is remote |
| Supporting earlier protocol revisions | a second code path for clients that do not exist here |
| Dropping the text line now | an agent in a shell still types it; it stays as shorthand until the tools are the usual way in |
| JSON between the target and the console | see [where JSON is used](#where-json-is-used) |

## Later, on demand

- **Confirmed writes.** A write tool answers with a dry run (address,
  bytes there, new bytes) and runs only when the client repeats the
  request with the confirmation; the protocol's `input_required` result
  is made for this. Until then decision 7's marks and the client's own
  prompt cover it.

## Check before step 3

Not verified; each can change the adapter, none changes the steps before
it.

- FastMCP supports the 2026-07-28 revision, over standard input and
  output.
- It carries result schemas and the read or write marks through to the
  client.
- The client in use speaks that revision.

## Open questions

- **The context cost, measured:** the size of the tool list in a client
  that loads every schema, and whether a smaller default set (core lookups
  only, the rest on request) is needed.
- **Slow verbs** (`each --seconds`, `record`, a blocking `watch`): excluded
  from the tool list, or capped below a client's timeout.
- **Typed results in Python:** frozen dataclasses built from the strict
  results, or the strict wrapper with generated annotations.
