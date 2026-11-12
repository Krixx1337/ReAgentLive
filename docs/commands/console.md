# Console

How to drive any ReAgentLive console: the command line, addresses and
refs, keeping round trips down, one-shot runs and the JSON Lines output.
What each command does lives in its area page ([index](../INDEX.md#commands));
what each takes and returns, in [result shapes](results.md) and in `help`.

## Command lines

A line is a verb, its positionals and its flags:
`dump $1 --match Health --nonzero`. One table says what every verb takes
and returns (a row per verb: positionals, flags, result type); the parser,
`help`, the [result shapes](results.md) page and the Python client's strict
results all read it, so none of them can disagree with the console.

- **Ask the console, not a page.** `help` lists the verbs, one line each
  with usage. `help <verb>` shows one verb's flags, its result's keys in
  [notation](results.md#notation) and its notes. `help --json` prints the
  whole table for programs. A `-c` run of only `help` lines needs no target
  running.
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
  line**: an unknown flag, a flag given twice, a missing value, too few or
  too many arguments.
- **Tokens.** Numbers are `0x` hex or decimal. A token in double quotes is
  taken exactly (spaces, `;` and `--` included; `\"` and `\\` escape) and is
  never a flag: quote a path or search text that needs it. Hex bytes may be
  spaced or packed (`01 02 ff`, `0102ff`).
- **`--timing`** and **`--meta`** go at the end of any line
  ([fewer round trips](#fewer-round-trips)).
- `<addr>` and `<object>` accept `$N`, result N of the last `find`.

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
  typed values: `rows` with `path`, `ok`, `value`, `address`,
  `object` (the ref the field was read from), or `error`; plus
  `same_tick`. Every path is resolved first (field tables and pointer hops,
  which may span ticks), then the values are read back to back, each
  guarded on its object; `same_tick` says whether those reads shared one
  tick. `--meta` adds each value's `type` and `hex`. Up to 16 paths of up to 8 steps. A dependent chain (read a
  pointer, read through it) only works inside one command like this, where
  the console sees each response before the next request.
- **`dump` filters:** `--match <text>` (field name or readable label,
  case-insensitive), `--nonzero`, `--changed [file]` (against the session's
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
  `truncated`, plus `type` and `hex` on `get` rows, `hex` on `each` rows
  and `meta` (the parameter's layout) on typed outputs. `fields` is the layout command
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

## JSON Lines

Each command from `-c` or stdin emits one object with `command`, `ok` and
either `result` or `error` ([envelopes](results.md#envelopes)). A result is
checked against its verb's row before it is printed: one that does not match
is an `internal` error, so what `help <verb>` shows is what arrives. Records are flushed immediately. Startup failures
emit a `startup` error. Exit status is 1 if any command failed. Capture
countdowns and start markers go to stderr.

```powershell
<console> -c 'find --class SceneComponent; params $1 SceneComponent:GetSocketTransform'
```
