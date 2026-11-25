# Console

*Home of:* how to drive any ReAgentLive console: JSON calls and literal refs, holds and `batch`, the result cap (`max_chars`) and digests, one-shot runs, plans, the usage log and `usage`, the JSON Lines output.

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
One table says what every verb takes and returns; the parser, `help`, the
[reference](reference.md), the [result shapes](results.md), the generated
Python functions and the MCP tools all read it, so none can disagree with
the console ([JSON-only contract](../tool-interface.md#json-only-contract)).
Nothing is quoted or escaped beyond JSON: a path with spaces, search text
with `;` or a layout is one string.

- **Ask the console, not a page.** `{"verb": "help"}` lists the verbs;
  `{"verb": "help", "args": {"verb": "get"}}` shows one verb's args, result
  keys, notes and an example; `{"verb": "help", "args": {"json": true}}`
  prints the whole table for programs. A `-c` run of only `help` needs no
  target.
- **Keys** are the row's, spelt the same in a call, a `batch` entry, a plan
  step, a generated Python function and an MCP tool. No key is a Python
  keyword (`cls`, never `class`). Output shaping is one `view` object; a
  verb takes only the view keys its row lists ([view](reference.md#view)).
  Per-call keys beside `verb` and `args`: `meta`, `timing`, `max_chars`,
  `big`.
- **`status`** reports the agent and console ([keys](reference.md#status)).
  `table_hash` changes when any verb's arguments or result change, so a
  cached script or skill can tell it is stale; agent compatibility is the
  layout hash's job ([protocol](../protocol.md#handshake)).
- **One verb, one result type.** An arg may add optional keys and never
  changes a key's type (`census` with `diff` adds `before`). A mode that
  would change a key's type stays a verb of its own (`sig-bytes`,
  `watch-start`).
- **A refused call says what is wrong:** an unknown key (checked first, so a
  misspelt name is never read as a left-out argument), a missing required
  key or a value its type refuses is an error of kind `schema` carrying the
  key's part of the schema and a corrected example.
- **Literal refs only.** A string starting with `$` is refused: a call
  carries the `ref` a result printed (works across runs), never "the last
  `find`" ([addresses and refs](#addresses-and-refs)).

## Addresses and refs

- **An address** is `"0x..."`, a literal ref, a module address
  `{"module": "game.exe", "rva": "0x1a2b3c"}` (name ignoring case; the
  offset must lie inside the image) or a base and offset
  `{"base": "0x1b3eec26700#185680:9", "offset": "0x330"}`. Where an arg
  takes a chain (`read`, `peek`, `mem-snap`, `vtable`, `disasm`, `xrefs`,
  `touches`, `sig-bytes`),
  `{"base": <addr>, "chain": [16, 8]}` reads a pointer at each offset but
  the last ([reference](reference.md#general-notes)).
- **Address bridge to a static tool.** Every address the console prints
  inside a loaded module also carries `module` and `rva`: RVAs, not rebased
  addresses, because the image base moves every launch. `modules` refreshes
  the module list the cache holds.
- **Refs** print as literals, `0x<address>#<index>:<serial>`, on the rows
  of `find`, `census`, `each`, `describe`, `get` and `entries`; pass one
  where an arg takes an object or address
  (`{"verb": "get", "args": {"object": "0x1b3eec26700#185680:9"}}`). A
  literal keeps the slot and serial, so a later console run still detects a
  replaced object. A raw address as `object` is sent address-only; the
  agent identifies it.

## Agent and console

- Any number of consoles attach to one agent; they take its one request
  slot in turns ([one request at a time](../protocol.md#handshake)). A
  held command (a `batch`, `get`, `peek`...) keeps it for its whole length.
  A long verb (`disp-scan`, `labels-sign`, `labels-port`, `labels-check`,
  `sig-check` with a file, `touches`) gives it up between blocks, so a call
  beside it waits about one block; inside a `batch` it keeps it, so the
  batch stays one moment.
- A call that finds the slot taken waits up to `--busy-wait <seconds>`
  (default 10), then is refused with kind `busy`, naming the holder. The
  console keeps running.
- **Sharing one agent between MCP and a script.** A long-lived Python
  session frees the slot between its calls, so MCP calls run in between
  ([Python client](../client.md#python-session)).

## Fewer round trips

An agent pays per tool call and per byte of output; inside a call, each
request costs up to a host tick. The console cuts both:

- **Hold.** Calls that make several requests (`get`, `batch`, `call` with
  `params`, `funcs`, `func`, `name`, `read`, `peek`, `census`, `each`
  without `duration`, any call that resolves a class name) mark their
  requests held: the agent runs the next one in the same host tick, within
  the host's budgets ([protocol](../protocol.md#exchange)). Every response
  carries the agent tick it finished in.
- **`batch`** runs calls held,
  `{"verb": "batch", "args": {"calls": [{"verb": "read", "args": {"addr": "0x10"}}, {"verb": "modules"}]}}`,
  and prints one envelope with each call's own envelope in `rows`. A value
  read in the batch cannot feed a later call: every call carries literal
  values. Same tick is best effort: check `same_tick`; `spans_ticks` names
  the call that broke it. Some verbs always span ticks (`each`, `census`,
  `search`, `xrefs`, `touches`...); for values that must line up use `get`.
- **`get` with `paths`** reads several fields through pointers and structs
  in one call ([Unreal fields](unreal-fields.md#get)).
- **`"timing": true`** adds request counts and waits to the envelope.
- **Compact values.** Decoded values print compact; `"meta": true`
  restores the layout ([compact values](unreal-fields.md#compact-values)).
- **The result cap (`max_chars`).** A console started with `--caller tool`
  has a default cap; scripts and shells have none. A call's `max_chars`
  sets its own (`0`: none), `big` lifts it. A result past it is not
  printed: `digest` stands in for `result` (`ok` stays true) with its size,
  its main list, its largest parts and every key that says whether the
  result is complete (`capped`, `truncated`, `next_cursor`, a file it
  wrote, a job still running). A digest never hides a total the reader
  acts on. Then one of two:
  - `next`: the call again, narrowed by what the result showed (a filter on
    the rows' own values first, then `view.fields`, `view.limit`,
    `view.nonzero`); a verb with no such key gets a `max_chars` that fits,
    so a digest is never a dead end. Only for calls that read.
  - `file`: a call that may have changed state (a write, a hook, a raw
    `call`) never runs again to show its result: the whole envelope goes to
    a file under `%TEMP%`, keyed like the cache file.

  A batch item past the cap gets its own digest; the batch's result goes to
  a file when any item may have changed state. Rationale:
  [console costs](../design/console-costs.md); MCP hand-off:
  [clients](../client.md#mcp-tools).
- **Cache file.** `%TEMP%\reagentlive-<pid>.cache` keeps class refs, field
  tables, loaded modules and the
  [census baseline](unreal-objects.md#census-changes) between console runs.
  It belongs to one agent instance: a restarted game or a new injection
  starts it fresh, and cached entries are re-validated on first use.

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

- One line per result: `{step, run?, at_ms, elapsed_ms, command, ok,
  result|error|digest}`. With `out` the lines go to the file, followed by a
  `{report}` line; without it they come back as `rows`. The report counts
  calls, ok, failed, refused by kind and expects, with each step's time and
  the console's and agent's builds.
- **`expect`** is a subset of the envelope (`{"ok": true, "result": {...}}`);
  a line says whether it was met (`expect`) and where the envelope departed
  (`expect_failed`).
- **Steps run one after another:** values from two steps are never from the
  same moment. For one moment, make one `batch` step.
- **`repeat`** runs a step at 0, `every`, 2*`every`... seconds while before
  `for`: ceil(`for` / `every`) runs, so `every` 1 for 5 is 5. Up to 600 s
  and 1000 runs; a plan holds up to 256 steps, and `run` and `quit` are not
  allowed in one.
- **`background`** (a frontend's: the MCP adapter, a Python session) starts
  the run as its own console, which takes the agent's slot in turns like any
  client under the packs enabled at the start. `run_status` (state and
  progress), `run_read` (the result, once done) and `run_stop` (ends it,
  deletes its files) follow it by id. A frontend that exits ends its runs; a
  console refuses `background`.
- **The packs ceiling applies per step:** a step of a pack outside
  `--packs` is a refused line (kind `pack`) and the plan goes on, so a plan
  with `modify` steps needs `modify` in `--packs` and the user's approval.
- A plan is checked whole before a step runs. `run` blocks while its
  repeats run, so MCP leaves it out; scripts call it through the Python
  session or `-c`.

## One-shot use

`<console> [pid] -c '{"verb": "find", "args": {"text": "PC_"}}'` attaches
and runs the call. `-c` takes one call or a JSON list of calls, in ASCII
only (escape other characters as `\u` in JSON, or use a file);
`-c @calls.jsonl` reads a file of one call (or list) per line. Stdout is
[JSON Lines](#json-lines); the exit code is 1 if any call was malformed or
failed. Each run starts with the previous run's census baselines and last
`get` `snap` from the [cache file](#fewer-round-trips). Without `-c`, send
one JSON call (or list) per stdin line; the console keeps its baseline until
EOF or `quit`. `--check` attaches to nothing and answers each call with
`{"checked": true}` or its refusal. `--names <file>` loads a
[names file](unreal-fields.md#readable-names); `--caller`, `--no-usage`,
`--context-limit` and `--session` belong to the
[usage log](#usage-log-and-usage).

**`--packs explore,native`** is a ceiling: the console runs only the verbs
of those [tool packs](../tool-interface.md#tool-packs) (`explore` always;
default: all four, for a person at the keyboard). Any other verb is refused
before its args are checked, with an error of kind `pack` naming the pack to
add; `batch` items are checked one by one, so a batch cannot carry a verb
past it. The MCP adapter and the Python session pass their own.

**No target.** When the target is not running or the agent cannot be
attached, the console answers `help` and `usage` from its table and its log;
any other verb is refused with an error of kind `startup`, and the console
ends, so a client starts it again once the target runs.

**A frontend's own refusal.** A call may carry `"refused": {...}`: a refusal
the frontend made before the call reached the console (the MCP adapter's
pack and schema checks). The console runs nothing; it answers that error and
logs the call as refused, so `usage` counts every refusal, wherever made.

## Usage log and `usage`

Every call the console runs appends one line to `<console>.usage.jsonl`
beside the console executable, so an agent can look back at what its
session cost. Every path ends in the console, so one log covers them
([design](../design/console-costs.md#usage-log)).

- **A line holds facts about the call, never its values:** verb, caller,
  the names of the keys given, hashes of the input and output, sizes, the
  outcome and timing. No addresses, names, paths or results.
- **A session** is one console run. One-shot (`-c`) runs against the same
  agent instance and caller chain into one session through the cache file;
  a long-lived console (stdin, the Python session) is its own. `--session
  <id>` names one outright (the MCP adapter passes one per server).
- **The caller** is `tool` (an agent reads every result, as through MCP),
  `script` (only what the script returns reaches the agent) or `shell` (the
  default). Only `tool` output is context cost.
- **A batch** writes a line per call and one for its wrapper, so no output
  counts twice.
- **Marks** state what happened; what to do about them is the
  [`reagentlive` skill](../../.claude/skills/reagentlive/SKILL.md)'s. Result
  keys: [results](results.md). Per verb:
  - `digested` counts results past the cap (`max_chars`); `saved` the
    characters their digests kept out. Of the next call after a digest,
    `followed` counts one that changed its narrowing args and `raised` one
    that only raised `max_chars` or set `big`.
  - `unfiltered`: the verb takes a view (own or `cls`) and the call gave none
    of `match`, `nonzero` or `limit`, `own`, `cls` or an optional `paths`
    (`get`'s), and it printed whole, past 2000 characters. Never a refused
    call.
  - `repeated`: the same input and result again in its session. The same
    input with another result is `same_args`: the state changed, the call was
    needed.
  - `refused` counts by error kind; one refused for its pack is not in
    `calls_by_pack`.
- **Counting:** `calls` counts top-level calls (a batch is one, its calls
  are `batched`), as do `calls_by_pack` and `elapsed_ms`; a verb's row counts its
  calls wherever they ran, and the `batch` row holds its wrapper. `out` is
  characters; `out_by_caller.tool` is what reached an agent's context.
- **Context limit:** `--context-limit <chars>` (default 400000; 0: none) is
  a soft limit on a session's `tool` output; past it every envelope carries
  `context_warning`. Nothing stops: `max_chars` stays the per-result cap. One
  session's report adds `context`: `chars`, `limit` and `over`.
- **Capped:** the log is trimmed to its newest sessions at 4 MB; a line
  another console appends mid-trim can be lost, accepted.

`{"verb": "usage"}` reports the current session; `scope` picks another:
`all` every session, `sessions` the list of sessions, any other value that
session's id. `only` keeps parts (`advice` proposes smaller defaults for
verbs often digested), `save` and `baseline` compare two runs:
`{"verb": "usage", "args": {"only": ["totals", "refusals"]}}`. Sizes are
the console's JSON Lines: the MCP adapter hands a client each result twice
(as text and as structured content), and which one a model sees is the
client's choice.

- `scope` `sessions` lists up to 30 sessions as `session_rows`, with totals
  over the whole log. `only` keeps the parts named (`session`, `sessions`,
  `since` and `saved` always stay); `save` still writes the whole report.
- `only` `advice` lists verbs with at least two digested or unfiltered calls (or
  half their calls), most first: `limit` proposes a smaller default
  `view.limit` (the default scaled to what the cap held of the largest
  digested result), `hint` says what else to change in the row or its help.
- `save` writes the report (JSON) and answers `saved`; `baseline` compares
  this session with such a file. `compare.worse` and `compare.better` list
  the figures that moved (totals `calls`, `refused`, `digested`, `context`
  and `elapsed_ms`; `refused_by.<kind>`; per verb `calls`, `refused`, `digested` and
  `elapsed_ms_per_call`), totals first, largest move first. A count is worse when it
  grew, time when it grew by a quarter and past noise (100 ms in all, 50 ms
  a call). `baseline` and `save` take one session (no `scope`, or a session id), never `all` or `sessions`.
- `refusals` lists refused calls by verb, error kind and the arg at fault.

## JSON Lines

Each call from `-c` or stdin emits one object with `command`, `ok` and
either `result` or `error` ([envelopes](results.md#envelopes)). A result is
checked against its verb's row before it is printed: one that does not match
is an `internal` error, so what `help` shows for a verb is what arrives.
Startup failures emit a `startup` error ([no target](#one-shot-use)).
Capture countdowns and start markers go to stderr.
