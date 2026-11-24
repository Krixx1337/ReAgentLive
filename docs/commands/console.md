# Console

*Home of:* how to drive any ReAgentLive console: JSON calls and their keys, addresses and literal refs, holds and `batch`, the result cap (`max_chars`) and digests, the cache file, one-shot runs and console arguments, the usage log and `usage`, the JSON Lines output.

**TL;DR:** The console is the command line every interface ends in: send
`{"verb": "help"}`, then `<console> -c '{"verb": "find", "args": {"cls":
"Pawn"}}'`, and read one JSON object per call. A call names its args as
JSON keys and carries the literal `ref` a result printed. What each
command does lives in its area page ([index](README.md)); what
each takes and returns, in the [reference](reference.md), in
[result shapes](results.md) and in `help`.

## JSON calls

A call is one JSON object:
`{"verb": "read", "args": {"addr": "0x1000", "len": 16, "type": "f32"}}`.
One table says what every verb takes and returns (a row per verb: its args,
each with a key, a type, a default and help, and the result type); the
parser, `help`, the [reference](reference.md), the
[result shapes](results.md) page, the generated Python functions and the
MCP tools all read it, so none of them can disagree with the console
([JSON-only contract](../tool-interface.md#json-only-contract)). Nothing is
quoted or escaped beyond JSON: a path with spaces, search text with `;` or
a layout is one string.

- **Ask the console, not a page.** `{"verb": "help"}` lists the verbs, one
  line each. `{"verb": "help", "args": {"verb": "get"}}` shows one verb's
  args (each with its `key`, `type`, `default` and help), its result's keys
  in [notation](results.md#notation), the named types it uses, its notes
  and an example call. `{"verb": "help", "args": {"json": true}}` prints
  the whole table for programs: every row with its result's `type`, the
  named `types` once, each origin's title (`origins`) and `table_hash`. A
  `-c` run of only `help` calls needs no target running.
- **Keys** are the row's, spelt the same in a call, a `batch` entry, a
  plan step, a generated Python function and an MCP tool. No key is a
  Python keyword, so none needs another spelling there (`cls`, never
  `class`). Output shaping is one `view` object (`start`, `limit`, `match`,
  `fields`, `nonzero`, `elements`); a verb takes only the view keys its row
  lists ([view](reference.md#view)).
- **Values:** a string or number for one value; a list where an arg takes
  several (`"paths": [["Info", "Owner"], ["Health"]]`); bytes, a pattern or
  text as one string (`"data": "00 00 80 3f"`, spaced or packed). A switch
  is `true` (or `false`, left out). An `int` is a number or `"0x..."` text;
  a `dur` is seconds (`1.5` is 1500 ms).
- **Call keys** beside `verb` and `args`: `"meta": true` (struct values
  with their layout), `"timing": true`, `"max_chars": N` and `"big": true`
  ([fewer round trips](#fewer-round-trips)).
- **Marks:** `writes` on a verb that writes the target's memory or calls
  into it, `blocks` on one that holds the console for seconds by design
  (`run`, and a host's countdown capture: the core's and the pack's
  captures return at once).
- **`status`** reports the agent and console (pid, ticks per second,
  build, `version`, `commit`, `host_thread`, `game_thread`, `packs`,
  `tool_packs`, `table_hash` and more: every key in the
  [reference](reference.md#status)). `table_hash` changes when any verb's
  arguments or result change, so a cached script or skill can tell it is
  stale; agent compatibility is the layout hash's job
  ([protocol](../protocol.md#handshake)).
- **One verb, one result type.** An arg may add optional keys and never
  changes a key's type (`census` with `diff` adds `before`; `sig-check`
  with `file` fills `file` and `lines` where one `pattern` fills `pattern`
  and `rows`). A mode that would change a key's type stays a verb of its
  own (`sig-bytes`, `watch-start`).
- **A refused call says what is wrong:** an unknown key (with the keys the
  verb takes, checked before anything else, so a misspelt name is never
  read as a left-out argument), a missing required key or a value its type
  refuses is an error of kind `schema` carrying the key's part of the
  schema and a corrected example. A refused limit states its number
  (`batch`'s 64 calls, `each`'s 64 bytes per instance).
- **Literal refs only.** A string starting with `$` is refused: a call
  carries the `ref` a result printed (works across runs), never "the last
  `find`" ([addresses and refs](#addresses-and-refs)).
- The envelope's `command` is the verb. A host's verbs take JSON calls
  too.

## Addresses and refs

- **An address** is `"0x..."`, a literal ref, a module address
  `{"module": "game.exe", "rva": "0x1a2b3c"}` (name ignoring case; the
  offset must lie inside the image) or a base and offset
  `{"base": "0x1b3eec26700#185680:9", "offset": "0x330"}`. Where an arg
  takes a chain (`read`, `peek`, `mem-snap`, `vtable`, `disasm`),
  `{"base": <addr>, "chain": [16, 8]}` reads a pointer at each offset but
  the last ([reference](reference.md#general-notes)).
- **Address bridge to a static-analysis tool.** Every address the console
  prints inside a loaded module also carries `module` and `rva`: `read`
  (and its 8-byte `ptr` view as `ptr_module`, `ptr_rva`), `write`, `search`
  hits, `func` (exec entry; `implementation_module`, `implementation_rva`
  for an interface), and object pointer values in `get` (`value_module`,
  `value_rva`; a path's row also its field `address`). RVAs, not rebased
  addresses: the image base moves every launch. The module list is read on
  first use and kept in the cache file; a module name it lacks makes one
  re-read per run, and `modules` refreshes it.
- A raw address as `object` is sent address-only; the agent identifies it.
- **Refs** print as literals, `0x<address>#<index>:<serial>`, on the rows
  of `find`, `census`, `each`, `describe`, `get` and `entries`; pass one
  where an arg takes an object or address
  (`{"verb": "get", "args": {"object": "0x1b3eec26700#185680:9"}}`). A
  literal keeps the slot and serial, so a later console run still detects a
  replaced object.

## Agent and console

- Any number of consoles attach to one agent; they take its one request
  slot in turns ([one request at a time](../protocol.md#handshake)). A
  request takes it for its own length, a held command (a `batch`, `get`,
  `peek`...) for its whole length. A long verb (`disp-scan`,
  `labels-sign`, `labels-port`, `labels-check`, `sig-check` with a file,
  `touches`) gives it up while it works on a block or row in the console,
  so a call beside it waits about one block, not the whole verb; inside a
  `batch` it keeps it, so the batch stays one moment.
- A call that finds the slot taken waits up to `--busy-wait <seconds>`
  (default 10, 0 to 600), then that call is refused with kind `busy`,
  naming the holder (its caller kind, pid and since when it took the slot)
  and how long it waited; the console keeps running. Every call's wait is
  in its usage line (`slot_ms`). Waits for a running request longer than a
  second print a line each second saying whether the agent is ticking (the
  request is running) or not.
- Long verbs print `  progress: <done>/<total> <unit>` to stderr at most
  once a second (blocks, rows, bytes, plan steps); a background run's
  status reads the newest.
- **Sharing one agent between MCP and a script.** A long-lived Python
  session frees the slot between its calls, so MCP calls run in between;
  a plan or a long verb can also run as a frontend's background run
  ([Python client](../client.md#python-session)).
- The console checks the agent's op list first and refuses ops the agent does
  not have.

## Fewer round trips

An agent pays per tool call and per byte of output; inside a call, each
request costs up to a host tick. The console cuts both:

- **Hold.** Calls that make several requests (`get` in every mode,
  `batch`, `call` with `params`, `funcs`, `func`, `name`, `read`, `peek`,
  `census`, `each` without `seconds`, any call that resolves a class
  name) mark their requests held. The agent then waits briefly for the next
  request and runs it in the same host tick, within the host's budgets
  ([protocol](../protocol.md#exchange)). A long call spreads over a few
  ticks. A multi-tick op (search, find's scan) ends the hold for the ticks
  it runs. Every response carries the agent tick it finished in.
- **`batch`** runs up to 64 calls held,
  `{"verb": "batch", "args": {"calls": [{"verb": "read", "args": {"addr": "0x10"}}, {"verb": "modules"}]}}`,
  and prints one envelope with `rows` (each call's own envelope), `all_ok`,
  `same_tick`, `first_tick` and `last_tick`. A value read in the batch
  cannot feed a later call: every call carries literal values. Not allowed
  inside: `help`, `status`, `usage`, `quit`, `batch`, `run`, and `each`
  with `seconds`. Same tick is
  best effort: check `same_tick`. Each envelope carries its own `tick` (its
  first response's) and `last_tick` when its responses spanned ticks;
  `spans_ticks` lists (`index`, `command`) the calls during which the
  batch's tick moved, so a skewed batch names the call that broke it. Some
  verbs always span ticks: `each`, `census`, `search`, `name-refs`, `xrefs` and
  `touches` scan over several. For values that must line up, use `get`, or
  a batch without those.
- **`get` with `paths`** reads several fields through pointers and
  structs in one call, values read back to back; with no paths, `view` and
  `own` filter every field ([Unreal fields](unreal-fields.md#get)).
- **`"timing": true`** on any call adds `timing` to its envelope:
  `trips` (requests sent), `wait_ms` (time waiting for responses),
  `slot_ms` (time waiting for the slot another client held) and
  `total_ms`.
- **The result cap (`max_chars`).** A console started with `--caller tool`
  prints at most 25,000 characters a result; scripts and shells have no
  limit. A call's `"max_chars": N` sets its own (`0`: none), `"big": true`
  lifts it, and a batch's covers its items. A result past it is not printed:
  `digest` stands in for `result` (`ok` stays true) with its size
  (`chars`, `max_chars`), the main list (`rows`: its path and count), its
  `largest` parts (paths with array elements folded, `rows[].Price`), the
  verb's narrowing keys by path (`narrow`, such as `view.limit`, and any
  arg taking a list of words, such as `labels-check`'s `status`) and
  `status`: every key the result had that says whether it is complete
  (`capped`, `truncated`, `unnamed`, `matched`, `omitted`, `errors` as a
  count, `tick_accurate`, `same_tick`, `total`, `next_cursor`, `next`,
  `filtered`), names a file the call wrote (`saved`) or names what is
  still running (`job`, `state`, `scanned`), so a capped result never
  reads as whole, a saved file is never lost and a search job can still be
  polled (`status.next` is a cut listing's resume address, not the
  digest's own `next` call); and beside them every other top-level scalar
  of the result (counts such as `labels-check`'s `ok` and `moved`, a
  module, a path up to 160 characters) and every top-level object of
  numbers (counts by kind). The rule is the digest's, for every verb: a
  digest never hides a total the reader acts on.
  Then one of two:
  - `next`: the call again, narrowed by what the result showed, narrowest
    first. A filter on the rows' own values comes first: an arg taking a
    list of words named as a row member (`labels-check`'s `status`) keeps
    the values the rows hold but the commonest (the rows that need work).
    Then a row list's members that fit go in `view.fields` (whole members
    where they fit, else their own members), `view.limit` shrinks to the
    rows that fit, or `view.nonzero` (then `own`, then `view.elements` 2)
    is added. A call whose verb has no key that would help comes back with
    a `max_chars` it fits (the result's size plus a tenth, in thousands),
    so a digest is never a dead end. It
    is for a call that only reads: reads, `each`, `call-pure`, and a
    `call` with `params` on a const or pure function.
  - `file`: a call that may have changed state (a write, timed or not,
    a hook, a raw `call` or one on a function that is neither const nor
    pure) never runs again to show its result: the whole envelope goes to
    `%TEMP%\reagentlive-<pid>-<owner>.results\`, keyed like the cache
    file (a new agent instance starts a new folder; the last 16 are kept).
    When the file cannot be written, the result prints whole.

  A batch item past the cap gets its own digest; the batch's result
  goes to a file when any item may have changed state. How the MCP tools
  hand a digest on: [clients](../client.md#mcp-tools).
- **Compact values.** Decoded values print compact (structs as
  `{Name: value}`, names as text, objects named with their ref); `"meta":
  true` on a call restores the layout
  ([compact values](unreal-fields.md#compact-values)).
- **Cache file.** `%TEMP%\reagentlive-<pid>.cache` keeps class refs, field
  tables, the loaded modules with their headers (exception directory and
  sections) and the
  [census baseline](unreal-objects.md#census-changes) between console runs. It belongs to
  one agent instance (`Section::owner`): a restarted game or a new injection
  starts it fresh. A cached class ref is checked with one `describe` on first
  use in a run and dropped if the agent refuses it; a cached field table is
  used only when page 0 of the live table (which names the type) matches it,
  so a lookup costs one request instead of every page.
- **Client waits** spin for 2 ms (held requests: 50 ms, at raised thread
  priority), then sleep in 1 ms steps with the timer resolution raised in the
  client process, so a round trip costs the host's tick, not Windows' 15.6 ms
  timer.

## Plans

A scripted run is a plan file, not a loop in a script: `run` reads a JSON
list of steps and runs each as its own call, so the file alone replays the
investigation.

```json
[{"verb": "status", "expect": {"ok": true}},
 {"verb": "get", "args": {"object": "0x1f2e3d4c50#123:4", "paths": [["Health"]]},
  "repeat": {"every": 0.5, "for": 10}}]
```

`{"verb": "run", "args": {"plan": "plan.json", "out": "lines.jsonl"}}`

- **One line per result:** `{step, run?, at_ms, ms}` and the call's
  envelope (`command`, `ok`, `result`, `error` or `digest`). With `out` the
  lines go to the file, followed by `{"report": ...}`; without it they come
  back as the result's `rows`.
- **`expect`** is a subset of the envelope (`{"ok": true}`, `{"result":
  {"state": "done"}}`); a line adds `expect` (`met` or `failed`) and
  `expect_failed`, the first path where the envelope departs.
- **Steps run one after another:** values from two steps are never from
  the same moment. For one moment, make one `batch` step; its line holds
  the batch's envelope, every item's included.
- **`repeat`** runs a step at 0, `every`, 2 x `every`, ... seconds while
  before `for`, paced from the step's start: `ceil(for / every)` runs, so
  `{"every": 1, "for": 5}` is 5 runs (up to 600 s and 1000 runs).
- **The packs ceiling applies per step:** a step of a pack outside
  `--packs` is a refused line (kind `pack`) and the plan goes on, so a plan
  with `modify` steps needs `modify` in `--packs` and the user's approval.
- **The report** counts calls, `ok`, `failed`, `refused` by error kind and
  expects, times each step, and names the console (`console`, `commit`),
  the agent's `build` and the `table_hash`.
- A plan is checked whole before a step runs (256 steps at most; `run` and
  `quit` are not allowed in it). `run` blocks while its repeats run, so MCP
  leaves it out; scripts call it through the Python session or `-c`.

## One-shot use

`<console> [pid] -c '{"verb": "find", "args": {"text": "PC_"}}'` attaches
and runs the call. `-c` takes one call or a JSON list of calls, run in
order, in ASCII only (escape other characters as `\u` in JSON, or use a
file); `-c @calls.jsonl` reads a file of one call (or list) per line.
Stdout is [JSON Lines](#json-lines). The exit code is 1 if any call was
malformed or failed. Each run starts with the previous run's census
baselines and last `get` `snap` from the [cache file](#fewer-round-trips)
([across runs](capture.md)). Literal refs always work. Without `-c`, send one JSON call (or list) per
stdin line. The console keeps the baseline until EOF or `quit`.

Console arguments: `<console> [pid] [-c '{...}' | -c @<file>] [--names <file>] [--caller tool|script|shell] [--session <id>] [--context-limit <chars>] [--no-usage] [--packs <list>] [--check]`.
`--check` attaches to nothing: each call (from `-c` or stdin) is checked as
it would be before it runs (schema, builder, a `batch`'s calls, a plan
step's `expect` and `repeat`; any `{"module": ...}` counts as loaded) and
answered `{"checked": true}` or with its refusal.
`--caller`, `--no-usage`, `--context-limit` and the session option (`<console> --session <id>`) are the
[usage log](#usage-log-and-usage)'s. `--names <file>` loads a
[names file](unreal-fields.md#readable-names).

**`--packs explore,native`** is a ceiling: the console runs only the verbs
of those [tool packs](../tool-interface.md#tool-packs) (`explore` always;
default: all four, for a person at the keyboard). Any other verb is
refused before its args are checked, with an error of kind `pack` that
names its pack (`pack`, the one to add) and the option to start with (no
`example`: no arg is at fault); `batch` items are checked one
by one, so a batch cannot carry a verb past it. `status` lists the
console's packs as `tool_packs`. The MCP adapter and the Python session
pass their own.

`{"verb": "help", "args": {"hash": true}}` answers the verb table's hash
alone (`{rows: [], table_hash}`), with no target: a program that keeps a
table reads it to know whether the console changed.

**No target.** When the target is not running or the agent cannot be
attached, the console answers `help` and `usage` from its table and its
log; any other verb is refused with an error of kind `startup` (why there
is no target), and the console ends, so a client starts it again once the
target runs. `-c` calls that are all `help`, `usage` or a frontend's
refusal (`refused`, below) never attach.

**A frontend's own refusal.** A call may carry `"refused": {"kind": "pack"
or "schema", "message": "...", "key": "..."}`: a refusal the frontend made
before the call reached the console (the MCP adapter's pack and input
schema checks). The console runs nothing; it answers that error and logs
the call as refused under the session (`--session`), so `usage`'s
refusals count every refusal, wherever it was made.

## Usage log and `usage`

Every call the console runs appends one line to `<console>.usage.jsonl`
beside the console executable, so an agent can look back at what its
session cost. Every path ends in the console (JSON calls on stdin or `-c`,
the generated functions, the MCP tools), so one log covers them
([design](../design/console-costs.md#usage-log)).

- **A line holds facts about the call, never its values:** time, session,
  verb and origin, caller, the names of the keys given, a hash of the
  input line and of its output (`out_hash`: what it printed, without
  `timing` or a context warning), input and output characters, `ok` or the refusal's error
  kind, milliseconds, `slot_ms` when it waited for the slot another
  client held, `batch` for a call that ran inside one, and
  `digest`, the result's full size, when a [digest](#fewer-round-trips)
  stood in for it (`out` is then the digest's). No addresses, names, paths
  or results.
- **A session** is one console run. One-shot (`-c`) runs chain with each
  other: against the same agent instance and caller they share one through
  the [cache file](#fewer-round-trips), so a burst of `-c` runs is one
  session (a shell's checks never join a tool's session). A long-lived
  console (calls on stdin, as the Python session sends them, with any `run`
  plan inside it) is its own session and is never handed on: one-shot runs
  after it keep chaining with the session the cache held before it.
  `<console> --session <id>` names a session outright (the MCP adapter
  passes one per server to the console it starts for each call); a named
  session is never handed on either.
- **A verb's own calls** (`assets-export` reading each object with `get`
  and `rows`) are not logged: their output goes to files, so only the
  verb's result counts as its output.
- **The caller** is `tool` (an agent reads every result, as through the
  MCP adapter), `script` (the Python session's default: only what the
  script returns reaches the agent) or `shell` (started by hand, the
  default). Only `tool` output is context cost.
- **Not recorded:** `help` with `json` or `hash` and `quit` (a program's
  own start and end), runs with no target, and anything with `--no-usage`.
- **A batch** writes a line per call, under its own verb, and one for
  itself holding only its wrapper, so no output counts twice. The totals'
  `calls`, `calls_by_pack` and `ms` count top-level calls (a batch is
  one); `batched` counts the calls run inside batches. A verb's row counts
  its calls wherever they ran (its own `batched` says how many inside). A `run`
  plan's steps are lines of their own too, so a failed step (or a failed
  item of a batch step) counts in the session's `refused`.
- **Repeats are judged by result:** `repeated` counts a call whose input
  and output both match an earlier call of the session (it learned
  nothing new); the same input with another output is `same_args` (the
  state changed, so the call was needed).
- **Digest follow-through:** after a digest, the same verb's next call in
  the session counts as `followed` when it changed the values of the
  verb's narrowing args (the digest's `narrow`, such as `view.limit`) and
  as `raised` when it only raised `max_chars` or set `big`. A line keeps
  `narrow` (a hash of those values) and `cap` (the `max_chars` it set, or
  the one its digest stood for) for this. A verb with many `raised` and
  few `followed` needs a better filter or default.
- **`unfiltered`** marks only a call that printed past 2000 characters
  with none of its verb's filters; `get`'s `paths` is one (it picks what
  is read), whatever the size.
- **Refused calls** keep the arg the error named (`refused_key`), so
  `usage` lists `refusals` by verb, kind and arg, most first: the help
  examples and arg texts to fix first. They count under `refused`; they never get `unfiltered`, and
  one refused for its pack does not count in `calls_by_pack`.
- **Context limit:** a session's `tool` output is what reached an
  agent's context. `--context-limit <chars>` (default 400,000; `0`: none)
  is a soft limit on it: a `--caller tool` console reads its session's
  total from the log at start (a client that starts a console per call,
  as the MCP adapter does, keeps one session) and adds what it prints;
  past the limit every envelope carries `context_warning`, one line
  naming the total and the limit. Nothing stops: `max_chars` stays the
  per-result cap. `usage` of one session shows `context` (`chars`,
  `limit`, `over`).
- **Capped:** past 4 MB the oldest sessions are dropped when a console
  starts, down to three quarters of that. `usage` with `report` `all` gives
  `since`, the oldest line kept. A line another console appends while one
  trims can be lost: accepted, a few lines at most.

`{"verb": "usage"}` reports the session the console is in (with no
target: the log's latest); with `session` (an id), one session of the log;
with `report` `all`, every session in the log, plus `never_called`, the
verbs of this table none of them used; with `report` `sessions`, the log's
sessions, newest first; with `advice`, also the verbs most often digested or unfiltered,
each with a proposed smaller default `view.limit` or a one-line hint for
its row or help (a report: nothing changes by itself). `save` writes one
session's report to a file; `baseline` compares this session (or
`session`) with such a file and lists in `compare` what got worse and
better: totals, refusals by kind, and each verb's calls, refusals,
digests and time per call. `only` keeps the parts named (`totals`,
`rows`, `largest`, `refusals`, `advice`, `compare` and the rest of the
report's keys), so a check of one figure reads only that part:
`{"verb": "usage", "args": {"only": ["totals", "refusals"]}}`. Ranked by output,
largest first, with per-verb marks
(`unfiltered`, `repeated`, `refused`); every key and mark:
[reference](reference.md#usage).

The marks state what happened; what to do about them is the
[`reagentlive` skill](../../.claude/skills/reagentlive/SKILL.md)'s. Sizes
are the console's JSON Lines: the MCP adapter hands a client each result
twice (as text and as structured content), and which one a model sees is
the client's choice.

## JSON Lines

Each call from `-c` or stdin emits one object with `command`, `ok` and
either `result` or `error` ([envelopes](results.md#envelopes)). A result is
checked against its verb's row before it is printed: one that does not match
is an `internal` error, so what `help` shows for a verb is what arrives.
Records are flushed immediately. Startup failures emit a `startup` error
([no target](#one-shot-use)). Exit status is 1 if any call failed. Capture
countdowns and start markers go to stderr.

```powershell
<console> -c '[{"verb": "find", "args": {"cls": "SceneComponent", "view": {"limit": 1}}}, {"verb": "func", "args": {"object": "SceneComponent", "function": "GetSocketTransform"}}]'
```
