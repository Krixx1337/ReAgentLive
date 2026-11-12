# Console

How to drive any ReAgentLive console: the command line, JSON calls,
addresses and refs, keeping round trips down, one-shot runs, the usage log
and `stats`, and the JSON Lines output.
What each command does lives in its area page ([index](../INDEX.md#commands));
what each takes and returns, in [result shapes](results.md) and in `help`.

## Command lines

A line is a verb, its positionals and its flags:
`dump $1 --match Health --nonzero`. One table says what every verb takes
and returns (a row per verb: positionals and flags, each with a key, a
type and a default, and the result type); the parser, JSON calls, `help`,
the [result shapes](results.md) page, the generated Python functions and
the MCP tools all read it, so none of them can disagree with the console.

- **Ask the console, not a page.** `help` lists the verbs, one line each
  with usage. `help <verb>` shows one verb's positionals and flags (each
  with its `key`, `type` and `default`), its result's keys in
  [notation](results.md#notation), the named types it uses and its notes.
  `help --json` prints the whole table for programs: every row with its
  result's `type`, the named `types` once, each origin's title
  (`origins`) and `table_hash`. A `-c` run of only `help` lines needs no
  target running.
- **Marks:** `writes` on a verb that writes the target's memory or calls
  into it, `blocks` on one that holds the console for seconds by design
  (`watch`, `record`).
- **`status`** reports the agent's pid, ticks per second since attach or the
  last status (at least 100 ms), the answering agent's build time, replaced
  agents, `host_thread` ([host thread](../hosting.md#host-thread)), `roots`,
  `packs` (the engine packs the host offers; a pack's verbs are refused
  without it) and `table_hash`. The hash changes when any verb's arguments
  or result change, so a cached script or skill can tell it is stale. It
  describes the console build; agent compatibility is the layout hash's job
  ([protocol](../protocol.md#handshake)).
- **One verb, one result type.** A mode is a verb of its own (`sig-check`,
  `sig-bytes`, `watch-start`, `census-diff`, `pin-list`, `trial`). A flag may
  add optional keys and never changes a key's type.
- **A usage error names what is wrong and ends with the verb's usage
  line**: an unknown flag (with the flags the verb takes), a flag given
  twice, a missing value, too few or too many arguments, or a value its type
  refuses (an `int` that is not a number, a word not in an enum). A refused
  limit states its number (`read-batch`'s total, `each`'s 64 bytes per
  instance).
- **Tokens.** Numbers are `0x` hex or decimal. A token in double quotes is
  taken exactly (spaces, `;` and `--` included; `\"` and `\\` escape) and is
  never a flag: quote a path or search text that needs it. Hex bytes may be
  spaced or packed (`01 02 ff`, `0102ff`).
- **`--timing`** and **`--meta`** go at the end of any line
  ([fewer round trips](#fewer-round-trips)).
- `<addr>` and `<object>` accept `$N`, result N of the last `find`.

## JSON calls

A line starting with `{` is a JSON call:
`{"verb": "read", "args": {"addr": "0x1000", "len": 16, "type": "f32"}}`.
The typed line is shorthand for it; both sort into the same arguments and
are checked once, against the row, by the same parser. Nothing is quoted
or escaped: a path with spaces, search text with `;` or a layout is one
string.

- **Keys** are the row's: a positional's `key`, a flag's name without
  `--` (`help <verb>` shows both), spelt the same in a typed line, a JSON
  call, a `batch` entry, a generated Python function and an MCP tool. No key
  is a Python keyword, so none needs another spelling there (`cls`, `start`,
  `type`). An unknown key is a usage error naming the keys the verb takes,
  checked before anything else, so a misspelt name is never read as a
  left-out argument.
- **Values:** a string or number for one value; a list for a positional
  that takes several (`"paths": ["A.B", "C"]`; `read-batch` spans as
  `[["0x10", 8], ["0x20", 4]]`); bytes, a pattern or text as one string
  (`"data": "00 00 80 3f"`). A switch is `true` (or `false`, left out); a
  flag with an optional value is `true` or its value. A number is hex for
  an `addr` and seconds for a `dur` (`1.5` is `1500ms`).
- **Literal refs only.** A string starting with `$` is refused: a call
  carries the `ref` a result printed (works across runs), never "the last
  `find`".
- `"meta": true` and `"timing": true` beside `verb` and `args` do what the
  line flags do.
- **`batch`** takes `"calls"`, a list of calls: `{"verb": "batch", "args":
  {"calls": [{"verb": "read", "args": {"addr": "0x10"}}, {"verb": "base"}]}}`.
- The envelope's `command` is the verb, as for a typed line. A host's
  verbs take JSON calls too.

## Addresses and refs

- **Address bridge to a static-analysis tool.** Addresses also accept
  `$N+offset`, `base+offset` (the exe) and `<module>+offset`
  (`game.exe+0x1a2b3c`, name ignoring case; the offset must lie inside the
  image). Every address the console prints inside a loaded module also
  carries `module` and `rva`: `read` (and its 8-byte `ptr` view as
  `ptr_module`, `ptr_rva`), `read-batch`, `write`, `search` hits, `func`
  (exec entry; `implementation_module`, `implementation_rva` for an
  interface), and object pointer values in `dump` and `get` (`value_module`,
  `value_rva`; `get` also its field `address`). RVAs, not rebased
  addresses: the image base moves every launch. The module list is read on
  first use and kept in the cache file; a module name it lacks makes one
  re-read per run, and `modules` refreshes it.
- A raw address as `<object>` is sent address-only; the agent identifies it.
- Refs print as literals, `0x<address>#<index>:<serial>`, and every `$N` position
  also accepts one (`dump 0x1b3eec26700#185680:9`, `read 0x...#185680:9+0x330 24`).
  A literal keeps the slot and serial, so a later console run still detects a
  replaced object.

## Agent and console

- One console per agent: a second client is refused as `busy` instead of
  overwriting the first one's request. Waits longer than a second print a
  line to stderr each second saying whether the agent is ticking (the request
  is running) or not.
- The console checks the agent's op list first and refuses ops the agent does
  not have.

## Fewer round trips

An agent pays per tool call and per byte of output; inside a command, each
request costs up to a host tick. The console cuts both:

- **Hold.** Commands that make several requests (`dump`, `snap`,
  `get`, `batch`, typed calls, `fields`, `funcs`, `params`, `func`, `name`,
  `census`, `each` without `--seconds`, any command that resolves a class
  name) mark their requests held. The agent then waits briefly for the next
  request and runs it in the same host tick, within the host's budgets
  ([protocol](../protocol.md#exchange)). A long command spreads over a few
  ticks. A multi-tick op (search, find's scan) ends the hold for the ticks
  it runs. Every response carries the agent tick it finished in.
- **`batch { <command>; ... }`** runs up to 64 command lines held and prints
  one envelope with `rows` (each command's own envelope), `all_ok`,
  `same_tick`, `first_tick` and `last_tick`. Each line is parsed just before
  it runs, so a `$N` from an earlier `find` in the same batch works; a value
  read in the batch cannot feed a later line (console syntax has no
  variables). Not allowed inside: `batch`, `watch` (the blocking one; the
  capture verbs are fine), `record`, `status`, `help`, `quit`,
  `each --seconds`. Same tick is best effort: check
  `same_tick`. Each envelope carries its own `tick` (its first response's)
  and `last_tick` when its responses spanned ticks; `spans_ticks` lists
  (`index`, `command`) the commands during which the batch's tick moved,
  so a skewed batch names the command that broke it. Some commands always
  span ticks: `each`, `census`, `search`, `refs`, `xrefs` and `touches` scan
  over several. For values that must line up, use `get`, `read-batch`, or a
  batch without those.
- **`get <object> <Field.Path>...`** resolves reflected names (or readable
  names; `Owner:Field` where ambiguous) through the field tables, descends
  into structs, follows object, class and weak object pointers, and returns
  typed values: `rows` in the order asked, each `value` and `address`, or
  `error` in the value's place (a failed path keeps its row, so positions
  never shift); `object` (the holder of the last field) only when the path
  crossed a pointer to reach it, `truncated` only when true; plus
  `same_tick`. Every path is resolved first (field tables and pointer hops,
  which may span ticks), then the values are read back to back, each
  guarded on its object; `same_tick` says whether those reads shared one
  tick. `--meta` adds each value's `type` and `hex`. Up to 16 paths of up to 8 steps. A dependent chain (read a
  pointer, read through it) only works inside one command like this, where
  the console sees each response before the next request.
- **`dump` filters:** `--match <text>` (field name or readable label,
  case-insensitive), `--nonzero` (a bool tests its own bit, not the byte it
  shares), `--own` (only the fields the object's own class declares, none
  it inherits), `--changed [file]` (against the session's
  `snap` baseline or a snapshot file; refused with neither; rows add
  `before`; refused when the reflected field layout differs from the
  baseline's). Filtered rows cost no pointer lookups or array previews; the
  result adds `filtered` (`shown`, `total`).
- **`--timing`** at the end of any line adds `timing` to its envelope:
  `trips` (requests sent), `wait_ms` (time waiting for responses) and
  `total_ms`.
- **Compact values.** A decoded struct prints as `{Name: value, ...}`,
  nested structs the same way: `{"X": 1.5, "Y": -2, "Z": 3}`, with a
  member's readable name where the [names file](unreal.md#readable-names) has one.
  Members past the 16 shown are counted under `"..."`; a name taken twice
  gets `@<offset>`. This holds for every decoded value: `get`, `dump`
  (values, `before`, previews), `entries`, `each`, typed `call` outputs.
  **`--meta`** at the end of a line (with or without `--timing`; inside a
  `batch` per item, or on the batch for all) restores the layout: a struct
  as `members` (`name`, `owner`, `type`, `offset`, `size`, `value`) with
  `truncated`, plus `meta` (the field's layout) on `dump` rows, `type` and
  `hex` on `get` rows, `hex` on one-column `each` rows and `meta` (the
  parameter's layout) on typed outputs. `fields` is the layout command
  and has no compact form. JSON keys print sorted.
- **Cache file.** `%TEMP%\reagentlive-<pid>.cache` keeps class refs, field
  tables, the loaded modules with their headers (exception directory and
  sections), the `$N`
  list and the
  [census baseline](unreal.md#census-changes) between console runs. It belongs to
  one agent instance (`Section::owner`): a restarted game or a new injection
  starts it fresh. A cached class ref is checked with one `describe` on first
  use in a run and dropped if the agent refuses it; a cached field table is
  used only when page 0 of the live table (which names the type) matches it,
  so a lookup costs one request instead of every page.
- **Client waits** spin for 2 ms (held requests: 50 ms, at raised thread
  priority), then sleep in 1 ms steps with the timer resolution raised in the
  client process, so a round trip costs the host's tick, not Windows' 15.6 ms
  timer.

## One-shot use

`<console> [pid] -c "find PC_; dump $1"` attaches and runs the commands in
order. Split commands with `;` or newlines. Stdout is [JSON Lines](#json-lines).
The exit code is 1 if any command was malformed or failed. A `;` inside
braces belongs to a `batch`. Each run starts with the previous run's `$N`
list from the [cache file](#fewer-round-trips) (same agent instance only) but
without a baseline, so carry baselines as snapshot files (`snap <ref> file`,
then `dump <ref> --changed file` in a later run). Literal refs always work. Without
`-c`, send one command per stdin line. The console keeps refs and the
baseline until EOF or `quit`.

Console arguments: `[pid] [-c "..."] [--names <file>] [--caller
tool|script|shell] [--no-usage]`; the last two are the
[usage log](#usage-log-and-stats)'s.

**No target.** When the target is not running or the agent cannot be
attached, the console answers `help` and `stats` from its table and its
log; any other verb is refused with an error of kind `startup` (why there
is no target) and the console ends, so a client starts it again once the
target runs. `-c` lines that are all `help` or `stats` never attach.

## Usage log and stats

Every call the console runs appends one line to `<console>.usage.jsonl`
beside the console executable, so an agent can look back at what its
session cost. Every path ends in the console (typed lines, JSON calls,
the generated functions, the MCP tools), so one log covers them
([design](../design.md#usage-log)).

- **A line holds facts about the call, never its values:** time, session,
  verb and origin, caller, the names of the flags given, a hash of the
  input line, input and output characters, `ok` or the refusal's error
  kind, milliseconds, and `batch` for a command that ran inside one. No
  addresses, names, paths or results.
- **A session** is one console run. One-shot (`-c`) runs against the same
  agent instance share one through the [cache file](#fewer-round-trips).
- **The caller** is `tool` (an agent reads every result, as through the
  MCP adapter), `script` (the Python session's default: only what the
  script returns reaches the agent) or `shell` (started by hand, the
  default). Only `tool` output is context cost.
- **Not recorded:** `help --json` and `quit` (a program's own start and
  end), runs with no target, and anything with `--no-usage`.
- **A batch** writes a line per command, under its own verb, and one for
  itself holding only its wrapper, so no output counts twice.
- **Capped:** past 4 MB the oldest sessions are dropped when a console
  starts, down to three quarters of that. `stats --all` gives `since`, the
  oldest line kept.

`stats` reports the session the console is in (with no target: the log's
latest); `stats --all` every session in the log, plus `never_called`, the
verbs of this table none of them used. Ranked by output, largest first:

| Key | Holds |
|---|---|
| `totals` | `calls` (what was sent: a batch's commands are inside its call), `in`, `out`, `out_by_caller` (`tool` is the context cost), `refused`, `ms` |
| `rows` | per verb: `calls`, `out` and its `largest`, `ms`, `batched`, and the marks |
| `largest` | the five largest single calls: verb, flag names, output, caller |

| Mark | Counts calls that... |
|---|---|
| `unfiltered` | went to a verb with `--match`, `--nonzero`, `--own`, `--changed`, `--cls` or `--limit` and gave none of them |
| `repeated` | sent an input already sent in the same session |
| `refused`, `refused_by` | the console refused, by error kind (`syntax`, `validation`, `agent`...) |

The marks state what happened; what to do about them is the
[`reagentlive` skill](../../.claude/skills/reagentlive/SKILL.md)'s. Sizes
are the console's JSON Lines: the MCP adapter hands a client each result
twice (as text and as structured content), and which one a model sees is
the client's choice.

## JSON Lines

Each command from `-c` or stdin emits one object with `command`, `ok` and
either `result` or `error` ([envelopes](results.md#envelopes)). A result is
checked against its verb's row before it is printed: one that does not match
is an `internal` error, so what `help <verb>` shows is what arrives. Records are flushed immediately. Startup failures
emit a `startup` error, and so does every verb but `help` and `stats` with
no target. Exit status is 1 if any command failed. Capture
countdowns and start markers go to stderr.

```powershell
<console> -c 'find --cls SceneComponent; params $1 SceneComponent:GetSocketTransform'
```
