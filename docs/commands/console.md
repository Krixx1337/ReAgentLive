# Console

How to drive any ReAgentLive console: the command line, addresses and
refs, keeping round trips down, one-shot runs and the JSON Lines output.
What each command does lives in its area page ([index](../INDEX.md#commands)).

## Command lines

`Parse` (`frontends/cli/syntax/parse.h`, with the verbs in `parse_*.cpp`)
turns a line into a request.

```text
find [text] [--class C] [--from N]   e.g. find --class PlayerController
read <addr> [len]                 len defaults to 64
read <addr> --as "<layout>"       a block of mixed types: "u8 phase, pad 3, f64[3] start"
peek <addr> [count]               what qwords hold: object, module+rva, heap or value
write <addr> <hex bytes>          "01 02 ff" or "0102ff"
call <object> <[Class:]Function> [hex params]
call <object> <[Class:]Function> Name=value [...] | --typed
func <object|class> <[Owner:]Function>
funcs <object|class> [text] [--from N]
params <object|class> <[Owner:]Function>
base                              exe base and size (module list read on first use, then cached)
modules
fields <object|class>
dump <object|class> [--match T] [--nonzero] [--changed [file]]
                                  fields with current values (console reads the object)
census [--class C] [--match T] [--changed] [--limit N] [--from N]
                                  instances per class; pages of 100 rows (--limit, 0 = all)
name-find <text> [--from N]       FName indices whose text contains text
each <Class> <Field|[Owner:]Function> [--seconds N]   one value per instance; repeat 1..60 s
describe <address|$N|#index:serial>  object name, class and reusable ref
name <index> [index ...]          FName indices as text (up to 32 per line)
sig --check <pattern> [--module M] [--expect <addr>] [--offset N | --resolve next,field[,size]]
                                  is a byte signature unique, and does it land there
sig --check --file <patterns>     one JSON line per signature; names the first that holds
sig --bytes <addr> [len]          live bytes in both forms, address fields wildcarded
disasm <addr> [count] [--function] [--follow]
                                  instructions from live bytes (fast lane)
xrefs <addr> [--module M] [--all] what points here: rel32 fields and pointers
touches <addr> <offset>... [--range N]
                                  functions near addr using [reg + offset], most offsets first
search text <text|"text"> [--wide]  a string's bytes, ASCII or UTF-16; quotes keep it exact
vtable <addr> [count] [--offset N] [--table] [--resolve]
                                  an object's virtual table, slots until one is not code
label <object|Class> <offset> <type>[n] <Name>
                                  name an unreflected member in <console>.native.txt
snap <ref> [file]                 replace the dump --changed baseline; with a file, also save it
record <ref> <seconds> <file> <field> [field ...]
get <object> <Field.Path> [...]   named fields, following pointers (see Fewer round trips)
batch { <command>; ... }          up to 64 commands, held
status | help | quit              handled by the console itself; status reports pid, ticks per
                                  second since attach or the last status (at least 100 ms),
                                  the answering agent's build time, replaced agents,
                                  host_thread (see Host thread) and packs (the engine
                                  packs the host offers; a pack's verbs are refused
                                  without it)
watch <seconds|Nms> <addr>:<len>... [--then call ...]
watch --start <duration> [--every <duration>] [--follow <root>] <addr|offset>:<len>...
watch --read | watch --stop       the agent's capture: rows so far / end it
<any command> --timing            adds trips, wait_ms and total_ms
<any command> --meta              struct values with their member layout
```

- Numbers: `0x` hex or decimal.
- `<addr>` and `<object>` accept `$N`, result N of the last `find`.
- `host_thread` in `status`: [host thread](../hosting.md#host-thread).

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
  one envelope with `results` (each command's own envelope), `all_ok`,
  `same_tick`, `first_tick` and `last_tick`. Each line is parsed just before
  it runs, so a `$N` from an earlier `find` in the same batch works; a value
  read in the batch cannot feed a later line (console syntax has no
  variables). Not allowed inside: `batch`, `watch`, `record`, `status`,
  `help`, `quit`, `each --seconds`. Same tick is best effort: check
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
  typed values: `values[]` with `path`, `ok`, `value`, `address`,
  `object` (the ref the field was read from), or `error`; plus
  `same_tick`. `--meta` adds each value's `type` and `hex`. Up to 16 paths of up to 8 steps. A dependent chain (read a
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
  `truncated`, plus `type` and `hex` on `get` values, `hex` on `each` rows
  and the field metadata on typed outputs. `fields` is the layout command
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
either `result` or `error`. Records are flushed immediately. Startup failures
emit a `startup` error. Exit status is 1 if any command failed. Capture
countdowns and start markers go to stderr.

```powershell
<console> -c 'find --class SceneComponent; params $1 SceneComponent:GetSocketTransform'
```
