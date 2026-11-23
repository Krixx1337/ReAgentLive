# Verb reference

<!-- Generated from the verb table (tests/table: `reagentlive-table --write-reference docs/commands/reference.md`). Do not edit: change the verb's row, `ViewKeys`, `GeneralNotes` or `ErrorKinds`. -->

Every verb as the console's table holds it: its args with their help, its result, its notes and one
example call. A call is `{"verb": ..., "args": {...}}` with the keys below; `help` with `verb` prints the
same for one verb. Result keys and the notation: [result shapes](results.md); the contract:
[JSON-only contract](../tool-interface.md#json-only-contract).

## General notes

- A call is one JSON object per line: {"verb": "read", "args": {"addr": "0x1000", "len": 16}}, args named as help shows them; "meta", "timing" and "big" as true, "budget" as a number. A line may also hold a JSON list of calls.
- help with verb shows its args (type, default, help), its result's keys, notes and an example call; help with json the whole table.
- Unknown keys, missing required ones and values of the wrong type are refused (kind schema) with the key's part of the schema and a corrected example.
- An int is a number or "0x..." text. An addr is "0x...", a literal ref, {"module": "x.dll", "rva": N} or {"base": <addr>, "offset": N}; addresses inside a module carry module and rva.
- A chain is {"base": <addr>, "chain": [N, N]} in read, peek, mem-snap, vtable and disasm: each offset but the last reads a pointer, so one alone is refused ([0, N] reads the pointer at base and adds N).
- A path is a list of field names: ["Info", "Owner"]. A key that does not apply is omitted from a result.
- meta prints struct values with their layout; timing adds trips, wait_ms and total_ms.
- A --caller tool console prints at most 25000 characters a result; past that a digest stands in for it, with its largest parts and next, the call narrowed (or file, for a call that must not run again). budget sets the limit, big lifts it.
- One console per agent: a second one is refused as busy.
- The module list and a pack's lookups persist for the agent instance in %TEMP%\reagentlive-<pid>.cache.
- One-shot: <console> [pid] -c '<call or list of calls>' (or -c @<file>, one call per line) emits JSON Lines.
- Each call is logged beside the console as facts (verb, keys, sizes, refusal), never values; stats reads it, also with no target. --caller tool|script|shell says who reads the output; --no-usage turns it off.
- Unreal pack verbs are refused when the host does not offer the pack (status lists the packs).
- Refs: pass the literal 0x<address>#<index>:<serial> a result printed (find, census, each, describe, get, entries); it works across runs.
- A bare class or struct name works where an arg says so; the first lookup scans the table.
- Struct values print as {Name: value}; meta adds their member layout.

## View

The shared output-shaping object: one schema, the same on every verb that takes `view`. A
verb takes the keys that apply to its result (its row lists them); a key it does not take is refused.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `start` | `int` |  |  | a page's next_cursor: the page after it |
| `limit` | `int` |  |  | rows at most (the verb's notes say whether 0 means all) |
| `match` | `str` |  |  | keep rows whose name contains the text, any case (the verb's notes say which name) |
| `fields` | `[path]` |  |  | the row members to keep (paths into structs and arrays) |
| `nonzero` | `bool` |  |  | leave out zero members inside values (0, false, empty, None) |
| `elements` | `int` |  | `8` | elements shown per array, set or map (1..256) |

## Error kinds

An error's `kind`, and the shape of its `message` (`<...>` stands for a value).

| Kind | When | Message |
|---|---|---|
| `schema` | the call does not fit its verb's row, or a builder refuses a value; it carries key, arg (or args) and a corrected example | unknown key '<key>' (did you mean <key>?); <verb> takes <keys> \| missing <key>: <type> (<help>) \| <key>: <type>, not <value> \| '<key>' goes inside <object>: {"<object>": {"<key>": ...}} \| <key>: <why> |
| `pack` | the verb's tool pack is outside this console's --packs | <verb> is in the <pack> pack, outside this console's --packs <packs>: start it with <pack> in --packs |
| `validation` | the target refused what the call asks (a guard, a state, a mismatch) | <what> refused: <why> |
| `unsupported` | the agent or host lacks the operation | agent does not support <operation> |
| `file` | a file the call names cannot be read or written | cannot read\|write <path>: <why> |
| `agent` | the agent refused the request; carries status, detail and reason | <op>: <reason> |
| `timeout` | the agent did not answer in time; carries status | <op> timed out |
| `attach` | the console cannot reach an agent | no agent in <pid>: <why> |
| `busy` | another console holds the agent | another client is attached |
| `startup` | the console could not start (arguments, packs, agent) | <why> |
| `read` | memory the call needs is unreadable | <address>: <why> |
| `module` | the address or name is in no loaded module | the address lies in no loaded module |
| `unwind` | the exception table has no usable entry for the code | <why> |
| `start` | a code address is no place to start (a walk or a save) | nothing to save at <address>: <why> |
| `label` | a labels file or a label cannot be used | <why> |
| `snapshot` | get's baseline is missing or unreadable | changed needs get with snap first (or a snapshot file) |
| `field` | the object has no such field, or it is of another kind | <field> is <type>, not <wanted> |
| `layout` | a layout the console relies on failed its check | <why> |
| `chain` | a pointer chain broke; carries chain with broken and reason | hop <n>: <reason> |
| `host` | a host verb returned nothing usable | host command returned no result |
| `internal` | a console fault: a result that does not match its row, or no result | <why> |

## Core

### help

the verbs, one line each; with verb: its args, result, notes and an example.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `verb` | `str` |  |  | a verb to show in full |
| `json` | `bool` |  |  | the whole table for programs |
| `hash` | `bool` |  |  | only the table's hash: has it changed since a program read it? |

Result: `{rows: [{verb: str, origin: str, pack: explore\|native\|observe\|modify, summary: str, writes?: bool, blocks?: bool, args?: [value], result?: str, notes?: str, example?: value, types?: {<key>: str}, type?: value}], notes?: [str], table_hash?: str, types?: {<key>: {notation: str, type: value}}, origins?: [{origin: str, title: str}], packs?: [{pack: str, job: str}]}`

```json
{"args":{},"verb":"help"}
```

### status

agent pid, tick rate, the packs the host offers and the verb table's hash.

Pack `explore`.

Result: `{pid: u32, build: str, version: str, commit: str, replaced_agents: u32, ticks: u64, ticks_per_second: u64, interval_ms: u64, ticking: bool, host_thread: engine\|borrowed\|own\|unset, tick_note?: str, roots?: [str], packs: [str], game_thread?: value, tool_packs: [str], table_hash: str}`

table_hash changes when any verb's args or result change: a cached script or skill is then stale.

```json
{"args":{},"verb":"status"}
```

### quit

end the console.

Pack `explore`.

Result: `{quit: bool}`

```json
{"args":{},"verb":"quit"}
```

### stats

what this session cost: calls, output and refusals per verb, largest output first.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `all` | `bool` |  |  | every session in the log, and the verbs none called |
| `sessions` | `bool` |  |  | the log's sessions, newest first: one row each |
| `session` | `str` |  |  | one session from the log, by its id |

Result: `{session?: str, sessions?: u64, since?: str, totals: {calls: u64, in: u64, out: u64, out_by_caller: {<key>: u64}, calls_by_pack: {<key>: u64}, refused: u64, digested: u64, saved: u64, ms: f64}, rows: [{verb: str, origin: str, calls: u64, out: u64, largest: u64, ms: f64, batched?: u64, unfiltered?: u64, repeated?: u64, refused?: u64, refused_by?: {<key>: u64}, digested?: u64, saved?: u64}], largest: [{verb: str, keys: [str], out: u64, caller: str, batch?: bool, digest?: u64}], never_called?: [str], session_rows?: [{session: str, since: str, until: str, callers: [str], calls: u64, out: u64, tool_out: u64, digested: u64, saved: u64, refused: u64}]}`

From the usage log beside the console; works with no target (then: the log's latest session). out is characters; out_by_caller.tool is what reached an agent's context. digested counts results past the output budget, and saved the characters their digests kept out (full size less the digest). Marks per verb: unfiltered (it takes a view, own or cls, the call gave none of view's match, nonzero or limit, own or cls, and printed whole; never a refused call), repeated (the same input again in its session), refused (by error kind; one refused for its pack is not in calls_by_pack). A batch's calls count under their own verbs (batched); the batch row holds its wrapper. sessions lists up to 30 sessions as session_rows, with totals over the whole log.

```json
{"args":{},"verb":"stats"}
```

### batch

up to 64 calls in as few host ticks as possible; one envelope each.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `calls` | `calls` | yes |  | the calls, each {"verb": ..., "args": {...}} (up to 64) |

Result: `{all_ok: bool, same_tick: bool, first_tick: u64, last_tick: u64, spans_ticks: [{index: u64, command: str}], rows: [value], tick_accurate?: bool}`

rows holds each call's envelope, with tick (and last_tick when its responses spanned ticks). help, status, stats, quit, batch and run are not allowed inside.

```json
{"args":{"calls":[{"args":{"addr":"0x140001000"},"verb":"read"}]},"verb":"batch"}
```

### run

a plan file's steps, each its own call: one NDJSON line per result, then a report.

Pack `explore`; blocks (left out of MCP).

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `plan` | `file` | yes |  | the plan: a JSON list of {"verb", "args"?, "expect"?, "repeat"?: {"every", "for"}} |
| `out` | `file` |  |  | write the lines here (rewritten), not in the result |

Result: `{file?: str, lines: u64, rows?: [value], report: RunReport}`

Each step runs as its own call under this console's --packs: a step of a pack outside them is a refused line, not a stop, so a plan with modify steps needs modify in --packs (and its approval). repeat runs the step every `every` seconds for `for` seconds (up to 600 s, 1000 runs). A line is {step, run?, at_ms, ms, command, ok, result|error|digest}, plus expect (met or failed) and expect_failed (where the envelope departs) when the step has expect: a subset of the envelope, {"ok": true, "result": {...}}. The report (the result's, and out's last line as {report}) counts calls, ok, failed, refused by kind and expects, with each step's time and the console's and agent's builds. Up to 256 steps; run and quit are not allowed in a plan. A plan file is the replay key: run it again to repeat an investigation.

```json
{"args":{"plan":"out.json"},"verb":"run"}
```

### read

hex dump of 1..4096 bytes (default 64); type decodes values.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `addr` | `chain` | yes |  | the address, or a pointer chain to follow first |
| `len` | `int` |  | `64` | how many bytes (1..4096) |
| `type` | `layout` |  |  | i8..u64\|f32\|f64\|ptr: values; cstr\|wstr: text; "u8 phase, pad 3, f64[3] start": a layout |

Result: `{address: addr, module?: str, rva?: hex, size: u64, hex: bytes, views?: {i32?: i32, u32?: u32, f32?: value, i64?: i64, u64?: u64, ptr?: addr, ptr_module?: str, ptr_rva?: hex, f64?: value}, values?: [value], layout?: value, text?: str, length?: u64, terminated?: bool, region?: Region, chain?: Chain}`

type adds values (one per element); cstr|wstr adds text, length and terminated (default 256 bytes); a layout "<type>[n] [name], pad <n>, ..." adds layout, {name: value}. A 4- or 8-byte read adds views. A chain adds chain. region is the memory the address lies in (kind: code, rodata, data, mapped, private, reserved or free).

```json
{"args":{"addr":"0x140001000"},"verb":"read"}
```

### peek

what 1..64 qwords hold (default 8): object, module, heap or value.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `addr` | `chain` | yes |  | the address, or a pointer chain to follow first |
| `count` | `int` |  | `8` | how many qwords (1..64) |
| `label` | `bool` |  |  | append this one qword as a confirmed global to a labels file |
| `file` | `file` |  |  | with label: the labels file (default: the host's names file, else the console's) |
| `name` | `str` |  |  | with label: the label's name (default FName_<hint>) |

Result: `{address: addr, module?: str, rva?: hex, region?: Region, rows: [{address: addr, module?: str, rva?: hex, label: object\|module\|heap\|value\|unreadable\|..., value?: hex, object?: Object, value_module?: str, value_rva?: hex, f32?: [value], f64?: value, name_hint?: str}], labelled?: {module: str, module_size: u64, timestamp: u32, rva: hex, name: str, kind: user\|global\|exec\|vtable\|interface}, chain?: Chain}`

object needs a pack; value rows carry floats, a double and name_hint. region is the memory the address lies in, as read gives it.

```json
{"args":{"addr":"0x140001000"},"verb":"peek"}
```

### write

write up to 256 bytes; undo restores it after a while, hold rewrites it every tick for a while.

Pack `modify`; **writes or calls**.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `addr` | `addr` | yes |  | the address |
| `data` | `bytes` | yes |  | bytes as hex pairs, spaced or packed: "00 00 80 3f" (up to 256; 64 with undo or hold) |
| `expect` | `bytes` |  |  | write only while memory still holds these bytes (with undo: required, restored afterwards) |
| `undo` | `obj` |  |  | restore the bytes after a while, guarded on their owner (needs expect) |
| `undo.after` | `dur` | yes |  | restore after this long (up to 60 s) |
| `undo.owner` | `ref` | yes |  | the live actor or component holding the address (a pack names it) |
| `undo.object` | `addr` |  |  | a native object inside the owner whose vtable is guarded too |
| `hold` | `dur` |  |  | rewrite the bytes every tick for this long (up to 60 s) |

Result: `{address: addr, module?: str, rva?: hex, size: u64, timed?: TimedWrite}`

A refused expect answers an error whose `found` holds the bytes there. undo and hold answer `timed`, the timed write as writes lists it; up to 64 bytes. undo stops as conflict if the game changes the value, abandoned if the owner or object's vtable changes; the owner's check is the pack's, so without one it is refused. hold on an addr based on a literal ref ({"base": <ref>, "offset": N}) stops when that object is freed or replaced. Options: expect guards every write; undo and hold are the two timed forms (undo's owner and object only guard an undo, so they live inside it).

```json
{"args":{"addr":"0x140001000","data":"00 00 80 3f"},"verb":"write"}
```

### writes

the timed writes (write undo, hold) with their state; clear ends one or all, restoring undo ones.

Pack `modify`; **writes or calls**.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `clear` | `bool` |  |  | end them |
| `id` | `str` |  |  | with clear: one write's id as writes lists it (undo:N or hold:N); default all |

Result: `{rows: [TimedWrite], cleared?: u64}`

Rows are undo writes (restored, conflict or abandoned ones stay listed until cleared) then hold ones. clear answers cleared and the rows it ended.

```json
{"args":{},"verb":"writes"}
```

### modules

loaded modules, exe first (also refreshes the cached list).

Pack `explore`.

Result: `{rows: [{name: str, base: addr, size: u64}]}`

```json
{"args":{},"verb":"modules"}
```

### search

addresses holding a value in resident, cacheable memory, as a job the agent runs.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `type` | `hex\|u32\|i32\|u64\|ptr\|f32\|f64\|text\|wtext` | yes |  | the value's type (wtext: text as UTF-16) |
| `value` | `scalar` | yes |  | the value: text or hex pairs as a string, a number otherwise (u64 and ptr also "0x...") |
| `scope` | `obj` |  |  | where to look (default: the main module's resident pages) |
| `scope.module` | `str` |  |  | one module, in place of start and to |
| `scope.start` | `addr` |  |  | start, or a job's next_cursor |
| `scope.to` | `addr` |  |  | end |
| `scope.writable` | `bool` |  |  | writable memory only, every module and heap |
| `scope.heap` | `bool` |  |  | private memory only (heaps, stacks) |
| `scope.paged` | `bool` |  |  | also non-resident pages (slow) |
| `align` | `int` |  |  | 1, 2, 4, 8 or 16 (typed values default to their size) |
| `wait` | `int` |  | `10` | seconds to wait for the job before answering with it running (0..60) |
| `max` | `int` |  | `64` | hits at most (1..507); the job stops when full |

Result: `{rows: [{address: addr, module?: str, rva?: hex, text?: str}], job: u32, state: running\|done\|stopped, from: addr, to: addr, scanned: f64, next_cursor?: addr}`

The range is the main module unless scope says otherwise; scope.start alone ends with the module it lies in. A job still running after wait comes back with state running and its hits so far: search-job with its id for more, with stop to end it. A new search stops the last job. next_cursor (a stopped or full job) resumes as scope.start. Options: scope picks the memory, align the step, wait how long the call blocks, max when the job stops (what the agent does, so not a view).

```json
{"args":{"type":"hex","value":"00 00 80 3f"},"verb":"search"}
```

### search-job

a search job's state and hits so far; stop ends it first.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `job` | `int` | yes |  | the job a search returned |
| `stop` | `bool` |  |  | end the job first; its next_cursor resumes it |

Result: `{rows: [{address: addr, module?: str, rva?: hex, text?: str}], job: u32, state: running\|done\|stopped, from: addr, to: addr, scanned: f64, next_cursor?: addr}`

Only the last job answers. scanned is the share of the range's searchable bytes (the committed, readable memory scope takes) behind the cursor, 0 while the job first measures them.

```json
{"args":{"job":16},"verb":"search-job"}
```

### mem-snap

keep up to 1 MiB of memory for mem-diff (a file beside the cache, across runs).

Pack `observe`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `addr` | `chain` | yes |  | the address, or a pointer chain to follow first |
| `len` | `int` | yes |  | how many bytes, up to 1048576 |

Result: `{address: addr, module?: str, rva?: hex, length: u64, tick: u64}`

A new snap replaces the last one and its candidates. Refused when part of the range is unreadable.

```json
{"args":{"addr":"0x140001000","len":16},"verb":"mem-snap"}
```

### mem-diff

elements of the last mem-snap that changed, stayed, rose or fell since; keep narrows.

Pack `observe`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `test` | `changed\|unchanged\|increased\|decreased` | yes |  | what an element must have done |
| `type` | `str` | yes |  | the element type: i8..u64, f32, f64 or ptr; elements every size bytes from the start |
| `keep` | `bool` |  |  | keep only the matches as candidates and today's bytes as the new baseline |
| `view` | `obj` |  |  | how the result is shown: page, row limit, filters (the shared view keys) |
| `view.limit` | `int` |  | `64` | rows at most (the verb's notes say whether 0 means all) |

Result: `{address: addr, module?: str, rva?: hex, length: u64, type: str, test: changed\|unchanged\|increased\|decreased, compared: u64, matched: u64, tick: u64, rows: [{address: addr, before: value, after: value}], capped: bool, kept?: bool}`

Reads the range again. changed and unchanged compare bytes; increased and decreased compare numbers (NaN never passes). Narrowing: mem-snap, act, mem-diff changed f32 with keep, act, mem-diff ... with keep until few remain; matched counts all, rows show the first (view.limit: 1..256). A snap from another agent instance is refused.

```json
{"args":{"test":"changed","type":"f32"},"verb":"mem-diff"}
```

### watch-start

a capture the agent keeps (up to 60 min with every); returns at once.

Pack `observe`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `duration` | `dur` | yes |  | how long, in seconds |
| `ranges` | `[range]` | yes |  | up to 8 ranges to sample, each {"addr": <addr>, "len": N}, {"addr": <addr>, "type": "f64", "count"?: N} to decode values, {"object": <ref>, "field": <path>} for a reflected field (a pack resolves it); following a root, {"offset": N, ...} in place of addr |
| `every` | `dur` |  |  | sample interval (default: every tick, then at most 30 s) |
| `follow` | `str` |  |  | continue on a host root's replacement; ranges are offsets from it |
| `file` | `file` |  |  | keep every sample in this file (JSON lines), rewritten by each watch-read and watch-stop |

Result: `{capacity: u32, ms: u32, interval_ms: u32, root?: str, ranges: [WatchRange], file?: str}`

status lists the roots. The capture outlives the console, its types too. A typed range's samples are numbers (a list for count), and watch-read and watch-stop add its summary: first, last, least, greatest and the change per second, per value. A field range takes the field's place and type at the start and is guarded on its object; an addr based on a literal ref is guarded on that object. file writes the ranges as the first line now; the file persists for the agent instance in the cache, and a later watch-start without file drops it. For a blocking capture, start, wait and stop (the Python session's watch helper).

```json
{"args":{"duration":5,"ranges":[{"addr":"0x140001000","len":4}]},"verb":"watch-start"}
```

### watch-read

the capture so far: its first sample, each change and its last.

Pack `observe`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `all` | `bool` |  |  | every sample, not only the changes |

Result: `{running: bool, stopped?: deadline\|stopped\|buffer full\|object replaced, capacity: u32, ms: u32, interval_ms: u32, root?: str, ranges: [WatchRange], rows: [WatchSample], samples?: u64, dropped?: u64, gaps?: [{from_us: u64, to_us?: u64}], summary?: [WatchSummary], file?: str}`

Rows are the first sample, every sample that differs from the one before (each with its time) and the last; samples counts them all. Past 200 rows the middle is dropped (dropped). The summary covers every sample, with changes per value. all returns every sample. With watch-start's file, every sample also goes to the file (file names it).

```json
{"args":{},"verb":"watch-read"}
```

### watch-stop

end the capture and return it: its first sample, each change and its last.

Pack `observe`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `all` | `bool` |  |  | every sample, not only the changes |

Result: `{running: bool, stopped?: deadline\|stopped\|buffer full\|object replaced, capacity: u32, ms: u32, interval_ms: u32, root?: str, ranges: [WatchRange], rows: [WatchSample], samples?: u64, dropped?: u64, gaps?: [{from_us: u64, to_us?: u64}], summary?: [WatchSummary], file?: str}`

As watch-read.

```json
{"args":{},"verb":"watch-stop"}
```

### sig-check

is a byte signature unique in a module, and does it land where expected; file checks a file of them.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `pattern` | `pattern` |  |  | the signature: "48 8b ?? 89" or "0x48, 0x8B, -1, 0x89", up to 96 bytes |
| `file` | `file` |  |  | a JSON-lines file, one signature per line, in place of pattern |
| `module` | `str` |  |  | the module (default: the exe) |
| `expect` | `addr` |  |  | where it should land |
| `offset` | `int` |  |  | the match starts this far past expect |
| `resolve` | `str` |  |  | a reference signature, "next,field[,size]" as labels files keep it; expect is then its target |

Result: `{pattern?: str, list?: str, module?: str, unique?: bool, total?: u64, rows?: [{address: addr, module?: str, rva?: hex, reference?: bool, diverges_at?: u64, resolved?: addr, resolved_module?: str, resolved_rva?: hex}], length?: u64, fixed_bytes?: u64, anchor?: {offset: u64, length: u64, hits: u64}, warnings?: [{offset: u64, size: u64, kind: call\|jump\|branch\|rip\|absolute, target: addr, target_module?: str, target_rva?: hex}], decoded?: u64, unique_at_length?: u64, resolve?: {next: u64, field: u64, size: u64}, expect?: addr, expect_module?: str, expect_rva?: hex, offset?: u64, resolves?: bool, mismatches?: [{offset: u64, wanted: hex, found: hex}], mismatch_error?: str, file?: str, winner?: u64, searches?: u64, lines?: [{line: u64, ok: bool, result?: {pattern: str, list: str, module: str, unique: bool, total: u64, rows: [{address: addr, module?: str, rva?: hex, reference?: bool, diverges_at?: u64, resolved?: addr, resolved_module?: str, resolved_rva?: hex}], length: u64, fixed_bytes: u64, anchor: {offset: u64, length: u64, hits: u64}, warnings: [{offset: u64, size: u64, kind: call\|jump\|branch\|rip\|absolute, target: addr, target_module?: str, target_rva?: hex}], decoded?: u64, unique_at_length?: u64, resolve?: {next: u64, field: u64, size: u64}, expect?: addr, expect_module?: str, expect_rva?: hex, offset?: u64, resolves?: bool, mismatches?: [{offset: u64, wanted: hex, found: hex}], mismatch_error?: str}, error?: str}]}`

Reports mismatches, diverges_at and warnings for unmasked addresses. file takes no other key and answers file, winner (the first line that holds) and a row per line. Options: pattern or file is the signature; module, expect, offset and resolve say where it must land, each a field labels files keep.

```json
{"args":{"pattern":"48 8b 05 ?? ?? ?? ?? 48 85 c0"},"verb":"sig-check"}
```

### sig-bytes

live bytes (default 64, up to 96; longer is cut: capped) in both pattern forms, address fields wildcarded.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `addr` | `addr` | yes |  | the address |
| `len` | `int` |  | `64` | how many bytes |

Result: `{address: addr, module?: str, rva?: hex, length: u64, capped?: bool, pattern: str, list: str, suggested: {pattern: str, list: str, fixed_bytes: u64, fields: [{offset: u64, size: u64, kind: call\|jump\|branch\|rip\|absolute, target: addr, target_module?: str, target_rva?: hex}], decoded?: u64}}`

```json
{"args":{"addr":"0x140001000"},"verb":"sig-bytes"}
```

### labels-sign

a byte signature on each of this build's user and global names, so they survive a patch.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `file` | `file` |  |  | the labels file (default: the host's names file, else the console's) |
| `module` | `str` |  |  | the module (default: the exe) |
| `again` | `bool` |  |  | sign rows that have a signature or failed before too |

Result: `{path: str, module: str, timestamp: u32, signed: u64, unsigned: u64, skipped: u64, failed_before: u64, searches: u64, rows: [{name: str, kind: str, rva: hex, status: signed\|unsigned, length?: u64, site?: hex, reason?: str}]}`

A function's signature starts at it, a global's at the first code that loads it (a reference signature); each is cut on whole instructions and grows from 16 bytes until exactly one match lands on the row (up to 96). It is stored as the row's sig (pattern, offset or resolve, as sig-check takes them); a row it cannot sign is listed with why and marked sig_failed (timestamp, reason): later calls on that build skip it (failed_before) unless again. The file is rewritten in place; other builds' lines stay as they are.

```json
{"args":{},"verb":"labels-sign"}
```

### labels-port

find other builds' signed names in the running build and add them to the file.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `file` | `file` |  |  | the labels file (default: the host's names file, else the console's) |
| `module` | `str` |  |  | the module (default: the exe) |

Result: `{path: str, module: str, timestamp: u32, ported: u64, missing: u64, ambiguous: u64, present: u64, unsigned: u64, searches: u64, rows: [{name: str, kind: str, from: u32, status: ported\|missing\|ambiguous, rva?: hex, matches?: u64, reason?: str}]}`

Per name this build lacks, the newest signed row of another build is matched in the image (one shared search per anchor). One match appends a row for this build with the same sig and ported_from (the old timestamp); none (missing: the code changed) or several (ambiguous) are listed, never written. Old rows stay.

```json
{"args":{},"verb":"labels-port"}
```

### disasm

instructions decoded from live bytes: 1..1024 (default 24; 8 bytes each, at least 512), or a whole function.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `addr` | `chain` | yes |  | the address, or a pointer chain to follow first |
| `count` | `int` |  | `24` | how many instructions (1..1024; with function: up to 2048) |
| `function` | `bool` |  |  | list the whole containing function (exception table; else walk from addr) |
| `resume` | `bool` |  |  | with function: start at addr (a cut listing's next), not the function's start |
| `save` | `obj` |  |  | with function: write the function and its read-only data for a static tool, not the rows |
| `save.file` | `file` | yes |  | the file to write |
| `save.callees` | `int` |  |  | also save the functions it calls or tail-jumps to, this many levels deep (1..3) |
| `save.labels` | `file` |  |  | name from this labels file (default: the console's and the host's names file) |
| `follow` | `bool` |  |  | step through up to 3 unconditional direct jumps |

Result: `{address: addr, module?: str, rva?: hex, size: u64, stop: str, next?: addr, rows?: [{address: addr, module?: str, rva?: hex, bytes?: bytes, text: str, target?: addr, target_module?: str, target_rva?: hex, string?: str, wide?: bool, import?: str, fields?: [{offset: u64, size: u64, kind: str}], ends?: bool, value?: f64, value_type?: str, value_hex?: bytes}], strings?: [{text: str, wide?: bool, at: [addr]}], saved?: str, saved_rows?: u64, functions?: [{start: addr, end: addr, rows: u64, depth?: u32}], skipped?: [{address: addr, why: str}], chunks?: [{start: addr, end: addr, rows: u64, depth?: u32}], tables?: [{jump: addr, table: addr, targets: u32}], jumpouts_left?: [addr], capped?: bool, layouts?: {classes: [str], types: u64}, piece?: {start: addr, end: addr}, function?: {start: addr, end: addr, module?: str, rva?: hex, chunk?: {start: addr, end: addr}}, end?: {address: addr, offset: u64, reason: str}, followed?: [{from: addr, to: addr, to_module?: str, to_rva?: hex}], chain?: Chain}`

An answer about one address: the default count stays small, and a whole function or more belongs in a save file for a static tool (save), not in a reply. Rows carry rva, bytes, text, targets, string hints, import names and the value a read-only [rip + disp] load reads; the listing names the module once. meta adds each row's module and address fields. function lists to the function's end, up to 2048 instructions or 16 KB (a cut gives next: continue with resume), with compact rows (no bytes, rva on the first row; meta for full rows) and strings: each string referenced, with the rows using it. Where no exception-table entry holds addr it walks from addr to the first return or jump no earlier forward branch passes: stop "walk end", no function key. Strings built at run time, reached through a pointer, or held as names are not seen. save leaves rows and strings out of the result (the file holds them) and adds saved and saved_rows, and saves each chunk the function jumps into: chained entries and switch targets (chunks, up to 256, then capped), each switch's table as data (tables), and the function's targets still unsaved in jumpouts_left; callees follows direct calls and tail jumps breadth-first and adds functions (each saved one, with its depth) and skipped (each target left out, why: depth, count past 16 callees, bytes past 256 KB of code in the whole save, unreadable). With an engine pack, a saved function whose label names a reflected class (Class::Method, or Class_Method by the longest class prefix) brings that class's layout (layout-export with closure 1) into the file as layout rows, counted in layouts (classes, types). A walk from more than 32 bytes past the last return, jump or padding in its unlisted piece adds piece (the piece holding addr, as xrefs gives it), and save then refuses unless resume. Options: count and function decide what is read (not a view: they bound the decoding), resume continues a cut listing, save writes files, follow steps through jumps.

```json
{"args":{"addr":"0x140001000"},"verb":"disasm"}
```

### xrefs

what points at an address: rel32 fields and stored pointers, each with its function.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `addr` | `addr` | yes |  | the address |
| `module` | `str` |  |  | scan another loaded module |
| `all` | `bool` |  |  | also list unknown hits (likely chance bytes) |

Result: `{address: addr, module?: str, rva?: hex, scanned: str, kinds: {<key>: u64}, rows: [{kind: call\|jmp\|jcc\|lea\|load\|store\|rip\|pointer\|unknown, field: addr, instruction?: addr, module?: str, rva?: hex, function?: {start: addr, end: addr, module?: str, rva?: hex, chunk?: {start: addr, end: addr}}, unlisted?: bool}], capped: bool, scan_ms: i64}`

A code row outside every exception-table entry carries unlisted, its function the piece holding it, cut as touches cuts unlisted code (between returns and int3 padding, within 16 KB each side): a function with several returns spans several pieces, and junk bytes may lead one. Entries under 4 bytes (a packed image's decoys) bound nothing.

```json
{"args":{"addr":"0x140001000"},"verb":"xrefs"}
```

### touches

functions near an address whose [reg + disp] operands use the offsets (up to 8).

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `addr` | `addr` | yes |  | the address |
| `offsets` | `[int]` | yes |  | the struct offsets to look for (up to 8, each above 0) |
| `range` | `int` |  | `"0x80000"` | bytes each side of addr (1..0x400000) |
| `reads` | `bool` |  |  | rank by offsets read; drop rows writing every offset they read |

Result: `{address: addr, module?: str, rva?: hex, offsets: [hex], range: u64, scanned: {from: addr, to: addr, functions: u64, unlisted: u64, bytes: u64}, total: u64, rows: [{function: {start: addr, end: addr, module?: str, rva?: hex, chunk?: {start: addr, end: addr}}, unlisted?: bool, distinct: u64, writes: bool, total: u64, uses: [{offset: hex, instruction: addr, module?: str, rva?: hex, write: bool, text: str}]}], capped: bool, scan_ms: i64}`

Most offsets first, writes marked: the code behind an unreflected member. Code outside every exception-table entry is scanned too, in pieces cut at returns and int3 padding: those rows carry unlisted, their bounds the piece's. Entries under 4 bytes (a packed image's decoys) count as unlisted code. reads leaves out struct copies and constructors.

```json
{"args":{"addr":"0x140001000","offsets":[16]},"verb":"touches"}
```

### vtable

an object's virtual table: slots until the first entry that is not code.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `addr` | `chain` | yes |  | the address, or a pointer chain to follow first |
| `count` | `int` |  |  | how many slots at most (1..512) |
| `offset` | `int` |  |  | the table pointer at addr+offset (an interface part; 0..0x100000) |
| `table` | `bool` |  |  | addr is the table itself |
| `resolve` | `bool` |  |  | where jump stubs lead |
| `rtti` | `bool` |  |  | also the class and bases its run-time type information names (MSVC images) |

Result: `{address: addr, module?: str, rva?: hex, offset: hex, table?: addr, table_module?: str, table_rva?: hex, stop?: count\|not code\|end of read, stop_value?: hex, found?: hex, rows: [{slot: u64, offset: hex, target: addr, module?: str, rva?: hex, body?: addr, body_module?: str, body_rva?: hex}], rtti?: {locator: addr, name: str, decorated: str, complete: addr, complete_offset: hex, bases: [{name: str, decorated: str}], capped: bool}, rtti_error?: str, chain?: Chain}`

rtti adds rtti, read from the locator in the slot before the table; complete is the whole object when addr is one of its parts. Where that slot holds no locator (no type information, or not an MSVC image; engines often build without it) it adds rtti_error instead. Options: count bounds what is read (not a view); offset and table say where the table pointer is; resolve and rtti add reads.

```json
{"args":{"addr":"0x140001000"},"verb":"vtable"}
```

## Unreal pack

### find

objects whose name contains text and/or instances of a class; 24 per page, or limit.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `text` | `text` |  |  | part of the object's name, any case |
| `cls` | `str` |  |  | instances of the class or a subclass |
| `live` | `bool` |  |  | live instances only: no Default__ or archetype objects |
| `outer` | `str` |  |  | objects with this object up their outer chain (a literal ref or "0x..."), or an outer whose name contains the text |
| `view` | `obj` |  |  | how the result is shown: page, row limit, filters (the shared view keys) |
| `view.start` | `int` |  |  | a page's next_cursor: the page after it |
| `view.limit` | `int` |  |  | rows at most (the verb's notes say whether 0 means all) |

Result: `{rows: [Object], total?: u64, from: u32, next_cursor?: u32}`

Give text, cls or outer. Only the first page counts total. view.limit (1..1000) pages until that many rows or the end; next_cursor continues after the last row (view.start). cls uses the agent's class index; text alone walks the whole object table, so add cls when the class is known.

```json
{"args":{"text":"Player"},"verb":"find"}
```

### census

live instances per class, most first; pages of 100 rows; diff: only the counts that changed.

Pack `observe`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `cls` | `str` |  |  | instances of the class or a subclass |
| `outer` | `str` |  |  | objects with this object up their outer chain (a literal ref or "0x..."), or an outer whose name contains the text |
| `diff` | `bool` |  |  | only the classes whose count differs from the last census of that cls and outer |
| `view` | `obj` |  |  | how the result is shown: page, row limit, filters (the shared view keys) |
| `view.start` | `int` |  |  | a page's next_cursor: the page after it |
| `view.limit` | `int` |  | `100` | rows at most (the verb's notes say whether 0 means all) |
| `view.match` | `str` |  |  | keep rows whose name contains the text, any case (the verb's notes say which name) |

Result: `{filter?: str, classes: u64, instances: u64, before?: {classes: u64, instances: u64}, ms: i64, rows: [{class: str, ref: ref, count: u32, before?: u32, samples: [ref]}], total: u64, from: u64, next_cursor?: u64, hint?: str}`

view.match keeps class names; view.limit is rows per page (0: all); view.start (next_cursor) pages the stored census without a new scan. Every census becomes the next diff's baseline. diff takes a new census and answers only the classes whose count changed, each row's count now with before, and before as the earlier census's classes and instances; refused with no earlier census under the same filter.

```json
{"args":{},"verb":"census"}
```

### name-find

FName indices whose text contains text (gameplay tags, row names).

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `text` | `text` | yes |  | part of the pooled name |
| `view` | `obj` |  |  | how the result is shown: page, row limit, filters (the shared view keys) |
| `view.start` | `int` |  |  | a page's next_cursor: the page after it |

Result: `{rows: [ue.Name], from: u32, next_cursor?: u32}`

```json
{"args":{"text":"text"},"verb":"name-find"}
```

### name

FName comparison indices as text (FName #N in dumps).

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `indices` | `[int]` | yes |  | FName comparison indices (up to 1024) |

Result: `{rows: [ue.Name]}`

A row has no name when the index is not where a pool entry starts (inside an entry) or does not decode.

```json
{"args":{"indices":[16]},"verb":"name"}
```

### each

a table over the live instances of a class: a value per column (up to 16 field paths).

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `cls` | `str` | yes |  | a class or struct name |
| `paths` | `[path]` | yes |  | a field path per column (structs and object pointers, as get takes) |
| `seconds` | `int` |  |  | one column: repeat for 1..60 s and report changes |
| `where` | `filter` |  |  | only instances whose field's value contains the text, any case (a string, a name or text; else its JSON); the field need not be a column; Outer, Name and Class match the object's own (Class is the class's name: cls takes subclasses) |
| `outer` | `str` |  |  | objects with this object up their outer chain (a literal ref or "0x..."), or an outer whose name contains the text |
| `view` | `obj` |  |  | how the result is shown: page, row limit, filters (the shared view keys) |
| `view.limit` | `int` |  | `100` | rows at most (the verb's notes say whether 0 means all) |
| `view.nonzero` | `bool` |  |  | leave out zero members inside values (0, false, empty, None) |
| `view.elements` | `int` |  | `8` | elements shown per array, set or map (1..256) |

Result: `{class: str, class_ref: ref, columns: [str], mode: field\|call, rows: [{ref: ref, name?: str, values: [value], hex?: bytes}], same_tick: bool, capped: bool, truncated: bool, errors?: [{ref: ref, column: u32, message: str}], seconds?: u32, polls?: u64, changes?: [{ms: i64, tick: u64, ref: ref, before: value, after: value}], poll_error?: str, matched?: u64, omitted?: u64, unnamed?: u32}`

rows: {ref, name, values} in the columns' order (name: the instance's); a failed cell is null with an errors entry. Spans inside the instance are read in passes of up to 64 bytes, the first in one tick; a path through a pointer is followed per instance (same_tick false). view.limit is rows shown (0: all); omitted counts the rest. Reads only: a function is refused, call-each calls one on every instance.

```json
{"args":{"cls":"Name","paths":[["Field"]]},"verb":"each"}
```

### call

call a reflected function with inputs by name (typed) or a raw block.

Pack `modify`; **writes or calls**.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `str` | yes |  | an object: a literal ref, "0x..." or a Default__ name |
| `function` | `str` | yes |  | the function, as Owner:Function where the name is ambiguous |
| `params` | `params` |  |  | the inputs by name, which makes a typed call ({} for none): a struct as a list in member order or an object by member name, an object as its ref, a name as its text (or "#index[:number]"), null for None |
| `block` | `bytes` |  |  | the raw parameter block as hex pairs (none: zeroed) |
| `dry` | `bool` |  |  | resolve the function and build the block; call nothing |
| `view` | `obj` |  |  | how the result is shown: page, row limit, filters (the shared view keys) |
| `view.start` | `int` |  |  | a page's next_cursor: the page after it |
| `view.limit` | `int` |  | `100` | rows at most (the verb's notes say whether 0 means all) |
| `view.fields` | `[path]` |  |  | the row members to keep (paths into structs and arrays) |
| `view.nonzero` | `bool` |  |  | leave out zero members inside values (0, false, empty, None) |

Result: `{ref: ref, function: str, mode: raw\|typed\|release\|dry, block_size: u64, hex: bytes, kept: bool, kept_reason?: str, outputs?: [{name: str, direction: out\|return, value: value, truncated: bool, meta?: ue.FieldInfo}], truncated?: bool, exec?: addr, pure?: bool, unnamed?: u32}`

A raw block must match the function's size (func shows it as block_size); none sends a zeroed block when every parameter is an output. hex is the block after the call; a typed call adds outputs, a returned array as {count, rows, next_cursor} with each element decoded from its struct (nested arrays up to 256 elements, 3 deep; names resolved). A view pages a typed call's returned arrays (fields: the row members, start: next_cursor, limit: rows per page, 0 all; nonzero inside the outputs) and makes the call typed. kept: an output owns memory (a handle, a string), so the agent reuses the block and the next call releases the old values. dry answers mode dry: hex is the block that would be sent, with exec and pure; show it when asking to call. A call that faults in the engine is refused as faulted: the target may be left in a bad state.

```json
{"args":{"function":"Name","object":"0x1f2e3d4c50#123:4"},"verb":"call"}
```

### call-pure

call a const or pure function that keeps nothing (read-only); as call takes.

Pack `observe`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `str` | yes |  | an object: a literal ref, "0x..." or a Default__ name |
| `function` | `str` | yes |  | the function, as Owner:Function where the name is ambiguous |
| `params` | `params` |  |  | the inputs by name, which makes a typed call ({} for none): a struct as a list in member order or an object by member name, an object as its ref, a name as its text (or "#index[:number]"), null for None |
| `block` | `bytes` |  |  | the raw parameter block as hex pairs (none: zeroed) |
| `dry` | `bool` |  |  | resolve the function and build the block; call nothing |

Result: `{ref: ref, function: str, mode: raw\|typed\|release\|dry, block_size: u64, hex: bytes, kept: bool, kept_reason?: str, outputs?: [{name: str, direction: out\|return, value: value, truncated: bool, meta?: ue.FieldInfo}], truncated?: bool, exec?: addr, pure?: bool, unnamed?: u32}`

The agent refuses, before calling, a function that is neither const nor pure, or one with an output that owns memory (use call). Pure is the engine's flag, not a proof: a function can still change state. The result is call's; no view, since a returned array owns memory. On every instance of a class: call-each.

```json
{"args":{"function":"Name","object":"0x1f2e3d4c50#123:4"},"verb":"call-pure"}
```

### call-each

call a const or pure function with no inputs on every live instance of a class, in one tick: each's table.

Pack `observe`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `cls` | `str` | yes |  | a class name: its live instances and its subclasses' |
| `function` | `str` | yes |  | the function, as Owner:Function where the name is ambiguous |
| `seconds` | `int` |  |  | repeat for 1..60 s and report changes |
| `outer` | `str` |  |  | objects with this object up their outer chain (a literal ref or "0x..."), or an outer whose name contains the text |
| `view` | `obj` |  |  | how the result is shown: page, row limit, filters (the shared view keys) |
| `view.limit` | `int` |  | `100` | rows at most (the verb's notes say whether 0 means all) |
| `view.nonzero` | `bool` |  |  | leave out zero members inside values (0, false, empty, None) |

Result: `{class: str, class_ref: ref, columns: [str], mode: field\|call, rows: [{ref: ref, name?: str, values: [value], hex?: bytes}], same_tick: bool, capped: bool, truncated: bool, errors?: [{ref: ref, column: u32, message: str}], seconds?: u32, polls?: u64, changes?: [{ms: i64, tick: u64, ref: ref, before: value, after: value}], poll_error?: str, matched?: u64, omitted?: u64, unnamed?: u32}`

call-pure's checks, per instance: answers as each does (mode call; rows {ref, name, values}, the one value its return value or {return, outputs}). The function takes no inputs and its block is 1..64 bytes. view.limit is rows shown (0: all); omitted counts the rest. A verb of its own: its result is each's table, not call's.

```json
{"args":{"cls":"Name","function":"Name"},"verb":"call-each"}
```

### release

one more call (zeroed inputs by default) that drops the kept block.

Pack `modify`; **writes or calls**.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `ref` | yes |  | an object: a literal ref or "0x..." |
| `function` | `str` | yes |  | the function, as Owner:Function where the name is ambiguous |
| `block` | `bytes` |  |  | the raw parameter block as hex pairs (none: zeroed) |

Result: `{ref: ref, function: str, mode: raw\|typed\|release\|dry, block_size: u64, hex: bytes, kept: bool, kept_reason?: str, outputs?: [{name: str, direction: out\|return, value: value, truncated: bool, meta?: ue.FieldInfo}], truncated?: bool, exec?: addr, pure?: bool, unnamed?: u32}`

```json
{"args":{"function":"Name","object":"0x1f2e3d4c50#123:4"},"verb":"release"}
```

### label

name an unreflected member in <console>.native.txt.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `str` | yes |  | an object (a literal ref, "0x..."), or a class or struct by name |
| `offset` | `int` | yes |  | the member's offset in the object |
| `type` | `str` | yes |  | its type, with a count for an array: f64[3] |
| `name` | `str` | yes |  | the member's name (a trailing ? marks it unconfirmed) |

Result: `{path: str, line: u64, owner: str, offset: hex, type: str, name: str, size: u32, timestamp: u32}`

get then shows it (native; stale after a patch).

```json
{"args":{"name":"Name","object":"0x1f2e3d4c50#123:4","offset":16,"type":"f32"},"verb":"label"}
```

### export-labels

names for a static-analysis tool (JSON lines, one build).

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `file` | `file` |  |  | the labels file (default: the console's, <console>.labels.jsonl) |
| `module` | `str` |  |  | the module (default: the exe) |
| `kinds` | `[exec\|vtable\|interface\|global]` |  |  | which rows to derive (default: all) |

Result: `{path: str, module: str, timestamp: u32, total: u64, globals: u64, user: u64, exec: u64, vtables: u64, interfaces: u64, interface_lookups: u64, other_builds: u64, capped: bool}`

Native exec thunks, vtables and interface implementations, merged into the file: names a static tool wrote back (user), globals peek's label confirmed, kinds not derived now and other builds' lines are kept; per address the highest rank wins (user, global, exec, interface, vtable). global derives nothing (peek's label writes those). After another tool's mappings: kinds ["interface"].

```json
{"args":{},"verb":"export-labels"}
```

### export-assets

each object of the classes into its own JSON file: every nonzero field, and a table's rows.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `dir` | `file` | yes |  | the folder to write (created when missing) |
| `cls` | `[str]` | yes |  | the classes (and their subclasses) to export, up to 16 |
| `view` | `obj` |  |  | how the result is shown: page, row limit, filters (the shared view keys) |
| `view.match` | `str` |  |  | keep rows whose name contains the text, any case (the verb's notes say which name) |

Result: `{dir: str, index: str, exported: u64, tables: u64, failed: u64, capped: bool, rows: [ExportedAsset], omitted?: u64}`

Live instances only (no Default__ or archetype objects), up to 2000 per call (then capped). A file <dir>/<Class>/<name>.json holds {ref, name, class, get, rows}: get is get's result with nonzero, rows (a data or curve table) is rows' with limit 0; a read that failed is {error} in its place. <dir>/index.json lists every file; rows here shows the first 100. view.match keeps objects whose name contains the text. Run it again to refresh: files are replaced.

```json
{"args":{"cls":["Name"],"dir":"out.json"},"verb":"export-assets"}
```

### layout-export

classes', structs' and enums' reflected layouts into a file (JSON lines): for layout-check and a static tool's types.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `file` | `file` | yes |  | the file to write |
| `classes` | `[str]` | yes |  | class, struct or enum names (up to 256) |
| `closure` | `int` |  |  | also the types their fields reference, to this depth (1..8), and each one's super chain |

Result: `{path: str, classes: u64, structs: u64, enums: u64, fields: u64, missing?: [str], capped?: bool}`

A class or struct line (name, super, size) then one field line per field it declares: owner, field, offset, size, type, and the struct, class or enum it names, with element, key and value parts for containers; an enum line holds its values. missing lists names that are no class, struct or enum; capped: closure stopped at 1024 types.

```json
{"args":{"classes":["Name"],"file":"out.json"},"verb":"layout-export"}
```

### layout-check

a file of expected fields against the live layout; only the mismatches.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `file` | `file` | yes |  | the file layout-export wrote |

Result: `{path: str, classes: u64, checked: u64, mismatched: u64, ok: bool, rows: [{owner: str, field: str, problem: no class\|no field\|offset\|size\|type, expected: {offset: hex, size: u32, type: str}, found?: {offset: hex, size: u32, type: str}}], capped: bool}`

Lines as layout-export writes them; offset as a number or "0x..", size and type optional. problem: no class, no field, offset, size or type; ok when every line holds. Rows stop at 200 (capped).

```json
{"args":{"file":"out.json"},"verb":"layout-check"}
```

### entries

one page of an array, set or map: elements, or key and value.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `str` | yes |  | an object: a literal ref, "0x..." or a Default__ name |
| `field` | `str` | yes |  | the field, as Owner:Field where the name is ambiguous |
| `view` | `obj` |  |  | how the result is shown: page, row limit, filters (the shared view keys) |
| `view.start` | `int` |  |  | a page's next_cursor: the page after it |

Result: `{ref: ref, field: str, kind: array\|set\|map, total: u64, rows: [{key?: value, value: value, key_object?: Object, value_object?: Object}], from: u32, next_cursor?: u32, truncated: bool, unnamed?: u32, tick_accurate?: bool}`

Names and object pointers are resolved.

```json
{"args":{"field":"Name","object":"0x1f2e3d4c50#123:4"},"verb":"entries"}
```

### rows

a data table's rows decoded with its RowStruct, or a curve table's curves and their keys.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `str` | yes |  | an object: a literal ref, "0x..." or a Default__ name |
| `at` | `num` |  |  | a curve table: each row's value at this time |
| `view` | `obj` |  |  | how the result is shown: page, row limit, filters (the shared view keys) |
| `view.start` | `int` |  |  | a page's next_cursor: the page after it |
| `view.limit` | `int` |  | `100` | rows at most (the verb's notes say whether 0 means all) |
| `view.match` | `str` |  |  | keep rows whose name contains the text, any case (the verb's notes say which name) |
| `view.fields` | `[path]` |  |  | the row members to keep (paths into structs and arrays) |
| `view.nonzero` | `bool` |  |  | leave out zero members inside values (0, false, empty, None) |

Result: `{ref: ref, row_struct?: str, row_struct_ref?: ref, curve?: simple\|rich, at?: f64, total: u64, rows: [{name: str, address: addr, value?: value, interp?: linear\|constant\|cubic\|none, keys?: value, weighted?: bool}], from: u32, next_cursor?: u32, truncated: bool, unnamed?: u32}`

RowMap is unreflected: read as the stock UE5 TMap<FName, uint8*> right after RowStruct, its slot count checked against its allocation bits (refused when they disagree). Nested arrays up to 256 elements, 3 deep; names resolved. view.match keeps row names; view.limit is rows per page (0: all); total counts the rows match kept. A CurveTable: curve simple|rich, per row keys ([t, v]; rich: [t, v, interp, arrive, leave]) and a simple curve's interp; at adds value, evaluated per key interp (constant, linear, cubic), the end keys' values outside them.

```json
{"args":{"object":"0x1f2e3d4c50#123:4"},"verb":"rows"}
```

### get

an object's fields with their values, or named paths through pointers and structs; types: the layout.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `str` | yes |  | an object (a literal ref, "0x...", a Default__ name), or with types a class or struct by name |
| `paths` | `[path]` |  |  | a field path per value (up to 16); none: every field |
| `mode` | `values\|types\|snap\|changed` |  | `"values"` | values; types: offsets, types and sizes, no values (a class or struct: its own fields); snap: keep the object's bytes as the baseline; changed: only fields changed since snap, with before |
| `own` | `bool` |  |  | only the fields the object's own class declares, none it inherits |
| `file` | `file` |  |  | with snap or changed: the snapshot file (across runs) |
| `view` | `obj` |  |  | how the result is shown: page, row limit, filters (the shared view keys) |
| `view.match` | `str` |  |  | keep rows whose name contains the text, any case (the verb's notes say which name) |
| `view.nonzero` | `bool` |  |  | leave out zero members inside values (0, false, empty, None) |
| `view.elements` | `int` |  | `8` | elements shown per array, set or map (1..256) |

Result: `{ref: ref, rows: [{path?: str, name?: str, raw?: str, owner?: str, type?: str, offset?: hex, size?: u32, struct?: str, bool_mask?: u32, element_type?: str, element_size?: u32, value_type?: str, value_size?: u32, value_offset?: hex, value_struct?: str, subtype?: str, value_subtype?: str, stride?: u32, native?: bool, stale?: bool, value?: value, before?: value, truncated?: bool, address?: addr, module?: str, rva?: hex, holder?: ref, value_module?: str, value_rva?: hex, object?: Object, preview?: {total: u64, elements: [{key?: value, value: value, object?: Object, key_object?: Object, value_object?: Object}]}, preview_error?: str, meta?: ue.FieldInfo, hex?: bytes, error?: str}], same_tick?: bool, truncated?: bool, capped?: bool, filtered?: {shown: u64, total: u64}, native_refused?: [{line: u64, name: str, owner: str, offset: hex, reason: str}], snap?: {size: u64, path?: str}, tick_accurate?: bool, unnamed?: u32}`

No paths: every field, compact (name, offset, value; raw beside a recovered name, object for a pointer, preview for a container); check truncated on the result and its rows before treating values as complete. Paths: one row per path in order (path, value, address, holder when a pointer led there), Owner:Field where a name is ambiguous; read back to back, same_tick says whether in one tick; a bad path fails its row with error naming the closest fields. types: the layout (owner, type, offset, size and the container's parts), no values. snap answers only snap {size, path} and keeps the baseline; changed then lists the fields that differ, with before. view.match keeps field names or readable labels; view.nonzero leaves out zero fields and the zero members inside values. view.match, own and the modes but values take no paths (there nonzero only trims values); types reads no values. Options: one mode, not four flags, since the modes exclude each other; own and paths pick fields, file names the baseline.

```json
{"args":{"object":"0x1f2e3d4c50#123:4"},"verb":"get"}
```

### trace

the first thing a line hits: actor, component, physical material, distance (read-only).

Pack `observe`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `start` | `point` | yes |  | the start: a world point [x, y, z] in cm, an object's location (actor, component, camera manager) by its ref, or {"object": <ref>, "socket": "Name"} on a mesh component |
| `end` | `point` | yes |  | the end, as start takes |
| `channel` | `int` |  | `0` | the trace type query (0..31): 0 visibility, 1 camera, N: TraceTypeQuery N+1 |
| `simple` | `bool` |  |  | simple collision instead of complex |
| `ignore` | `ref` |  |  | an actor the line passes through; also the world it runs in |

Result: `{start: [f64], end: [f64], channel: str, complex: bool, ignored?: ref, hit: bool, initial_overlap?: bool, distance?: f64, impact?: [f64], normal?: [f64], actor?: Object, component?: Object, material?: Object, bone?: str, face?: i32, item?: i32}`

KismetSystemLibrary.LineTraceSingle on the game thread, decoded by GameplayStatics.BreakHitResult; it changes no state. The world comes from ignore, else from an end's object: two points need ignore. Distance and points are in cm; distance counts from start. A socket needs a mesh component (get the actor's Mesh).

```json
{"args":{"end":[0,0,0],"start":[0,0,0]},"verb":"trace"}
```

### calls-start

record a reflected function's calls, with their arguments, for a while.

Pack `modify`; **writes or calls**.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `owner` | `str` | yes |  | whose function: an object or a class by name (object narrows to one object) |
| `function` | `str` | yes |  | the function, as Owner:Function where the name is ambiguous |
| `duration` | `dur` | yes |  | how long, in seconds (at most 60) |
| `object` | `ref` |  |  | only calls on this object |
| `dry` | `bool` |  |  | resolve and show the pointer that would be swapped; write nothing |

Result: `{function: str, slot: u32, exec: addr, thunk: addr, block_size: u32, object?: addr, seconds: f64, dry: bool, note?: str}`

Swaps the function's native pointer (UFunction::Func, heap data; no code, vtable or import) for a recording thunk that forwards every call, and puts it back at the end or on calls-stop (only while it still holds the thunk). Calls through ProcessEvent (C++ callers, RPCs, Blueprint events, delegates) carry their parameters after the call; Blueprint graphs calling native code show only the object (via vm); direct C++ calls are not seen. 256 rows per recording; older ones are dropped.

```json
{"args":{"duration":5,"function":"Name","owner":"Name"},"verb":"calls-start"}
```

### calls-read

a recording's calls so far, decoded by the function's parameters.

Pack `modify`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `slot` | `int` |  |  | the recording, 0..7 (default: the last started) |
| `view` | `obj` |  |  | how the result is shown: page, row limit, filters (the shared view keys) |
| `view.start` | `int` |  |  | a page's next_cursor: the page after it |

Result: `{function: str, state: recording\|expired\|finished\|replaced, slot: u32, calls: u64, events: u64, vm: u64, dropped: u64, rows: [{sequence: u64, ms: f64, thread: u32, object: ref, object_name?: str, object_class?: str, via: event\|vm, values?: [{name: str, direction: in\|out\|return, value: value, truncated: bool}]}], next_cursor?: u64, unnamed?: u32}`

values: in, out and return by name, read after the call (inputs as the function left them). Objects are named while they live; strings, arrays, sets and maps print their headers (the caller may have freed them). A recording past its time is restored first.

```json
{"args":{},"verb":"calls-read"}
```

### calls-stop

end a recording now (restores the pointer) and return its calls.

Pack `modify`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `slot` | `int` |  |  | the recording, 0..7 (default: the last started) |

Result: `{function: str, state: recording\|expired\|finished\|replaced, slot: u32, calls: u64, events: u64, vm: u64, dropped: u64, rows: [{sequence: u64, ms: f64, thread: u32, object: ref, object_name?: str, object_class?: str, via: event\|vm, values?: [{name: str, direction: in\|out\|return, value: value, truncated: bool}]}], next_cursor?: u64, unnamed?: u32}`

state replaced: something else swapped the pointer since; it was left alone and the thunk keeps forwarding.

```json
{"args":{},"verb":"calls-stop"}
```

### describe

an object's name, class and reusable ref.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `ref` |  |  | the object: a literal ref or "0x..." |
| `index` | `int` |  |  | in place of object: an object-table slot (a weak pointer's), with serial |
| `serial` | `int` |  |  | with index: the slot's serial number |

Result: `{ref: ref, name: str, class: str, class_size?: u32, outer?: {name: str, ref: ref}}`

Give object, or index and serial.

```json
{"args":{"object":"0x1f2e3d4c50#123:4"},"verb":"describe"}
```

### func

a function's reflected metadata, exec entry address and parameter signature.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `str` | yes |  | an object (a literal ref, "0x..."), or a class or struct by name |
| `function` | `str` | yes |  | the function, as Owner:Function where the name is ambiguous |

Result: `{ref: ref, function: str, address: addr, flags: u32, exec?: addr, executable: bool, module?: str, rva?: hex, module_error?: str, interface?: {offset: hex, slot?: i64, implementation?: addr, implementation_module?: str, implementation_rva?: hex}, block_size?: u32, params?: [{name: str, owner: str, type: str, offset: hex, size: u32, struct?: str, bool_mask?: u32, element_type?: str, element_size?: u32, value_type?: str, value_size?: u32, value_offset?: hex, value_struct?: str, stride?: u32, subtype?: str, value_subtype?: str, direction: in\|out\|in ref\|in/out ref\|return, flags: u32}], params_error?: str}`

params lists the parameters in block order (direction in, out, in ref, in/out ref or return) and block_size is the call block's size; reading them needs the game thread, so without it params_error says why instead.

```json
{"args":{"function":"Name","object":"0x1f2e3d4c50#123:4"},"verb":"func"}
```

### script

a Blueprint function's bytecode: the names, functions and properties it uses; decode: as statements.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `str` | yes |  | an object (a literal ref, "0x..."), or a class or struct by name |
| `function` | `str` | yes |  | the function, as Owner:Function where the name is ambiguous |
| `decode` | `bool` |  |  | read the bytecode as statements with their offsets |
| `entry` | `int` |  |  | with decode: one uber-graph event, from this offset to its return, pop or goto |

Result: `{ref: ref, function: str, size: u32, names: [{offset: hex, count: u32, index: i32, name: str}], functions: [{offset: hex, count: u32, ref: ref, function: str}], properties: [{offset: hex, count: u32, address: addr, property: str, raw?: str}], statements?: [{offset: hex, op: str, text: str}], events?: [{offset: hex, event: str}], stopped?: str}`

Each entry is read right after its opcode and confirmed live. decode reads every expression instead (no game thread): statements, the lists from the decoded operands, properties by their readable name (raw: the reflected one), functions as Owner.Function, math operators as infix; an opcode it does not know stops it (stopped). An uber graph adds events: each event's entry offset, from the calls that enter it.

```json
{"args":{"function":"Name","object":"0x1f2e3d4c50#123:4"},"verb":"script"}
```

### enum

an enum's names and values, in order.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `enum` | `str` | yes |  | an enum object (a literal ref, "0x..."), or an enum by name |

Result: `{ref: ref, name: str, rows: [{name: str, value: i64}]}`

Byte and enum properties print numbers; this names them. Names keep the enum's prefix (EFoo::Bar).

```json
{"args":{"enum":"Name"},"verb":"enum"}
```

### game-thread

arm or remove the host's game-thread tick through the game viewport's Draw (status shows it).

Pack `modify`; **writes or calls**.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `action` | `on\|off` | yes |  | on arms it, off puts the viewport's table back |

Result: `{mode: off\|waiting\|conflict\|on\|stopped, text: str, thread?: u32, viewport?: addr, table?: addr, draw?: addr, draw_module?: str, draw_rva?: hex}`

call, call-pure, call-each, release, func's params, each, script, refs, exec entries and the recorder need the game thread. on swaps one pointer in the viewport object for a copy of its Draw table: confirm Draw first (the skill's references/unreal/game-thread.md). Agents whose host does not link reagentlive::ue-draw answer unsupported.

```json
{"args":{"action":"on"},"verb":"game-thread"}
```

### refs

Blueprint functions whose bytecode names a pooled name (EX_NameConst).

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `name` | `str` |  |  | the exact pooled name |
| `index` | `int` |  |  | in place of name: its comparison index |

Result: `{index: i32, name?: str, rows: [{ref: ref, owner: str, name: str, offset: hex, count: u32}], capped: bool}`

Give name or index.

```json
{"args":{"name":"None"},"verb":"refs"}
```

### funcs

functions on the class chain, then on the native interfaces it implements.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `str` | yes |  | an object (a literal ref, "0x..."), or a class or struct by name |
| `view` | `obj` |  |  | how the result is shown: page, row limit, filters (the shared view keys) |
| `view.start` | `int` |  |  | a page's next_cursor: the page after it |
| `view.match` | `str` |  |  | keep rows whose name contains the text, any case (the verb's notes say which name) |

Result: `{ref: ref, rows: [{owner?: str, name: str, via?: str}], from: u32, next_cursor?: u32}`

view.match keeps function names. A row with via "interface" is declared only on that native interface: call refuses it on the object; func shows the implementation to call instead.

```json
{"args":{"object":"0x1f2e3d4c50#123:4"},"verb":"funcs"}
```
