# Verb reference

<!-- Generated from the verb table (tests/table: `reagentlive-table --write-reference docs/commands/reference.md`). Do not edit: change the verb's row, `ViewKeys`, `GeneralNotes` or `ErrorKinds`. -->

Every verb as the console's table holds it: its args with their help, its result, its notes and one
example call. A call is `{"verb": ..., "args": {...}}` with the keys below; `help` with `verb` prints the
same for one verb. Result keys and the notation: [result shapes](results.md); the contract:
[JSON-only contract](../tool-interface.md#json-only-contract).

## General notes

- Load the reagentlive skill before investigating: method, approvals, pitfalls.
- A call is one JSON object per line: {"verb": "read", "args": {"addr": "0x1000", "len": 16}}; "meta", "timing", "big" as true, "max_chars" a number. A line may be a list of calls.
- help with verb: args, result, notes, example; with json: the whole table. Unknown keys, missing required ones and wrong types are refused (kind schema) with a corrected example.
- An int is a number or "0x..." text. An addr is "0x...", a literal ref, {"module": "x.dll", "rva": N} or {"base": <addr>, "offset": N}.
- A chain is {"base": <addr>, "chain": [N, N]} (read, peek, mem-snap, vtable, disasm, xrefs, touches, sig-bytes): each offset but the last reads a pointer; one alone is refused.
- A path is a list of field names: ["Info", "Owner"]. Keys that do not apply are omitted from a result.
- A --caller tool console caps a result at 25000 characters (a digest instead, with next); max_chars sets the cap, big lifts it. Past --context-limit (default 400000) envelopes carry context_warning.
- One console per agent: a second one is refused as busy.
- One-shot: <console> [pid] -c '<call or list of calls>' (or -c @<file>, one call per line). Calls are logged as facts, never values (usage; --no-usage).
- Refs: pass the literal 0x<address>#<index>:<serial> a result printed; it works across runs.
- A bare class or struct name works where an arg says so.

## View

The shared output-shaping object: one schema, the same on every verb that takes `view`. A
verb takes the keys that apply to its result (its row lists them); a key it does not take is refused.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `cursor` | `int` |  |  | the previous result's next_cursor, as it gave it (a number or "0x...") |
| `limit` | `int` |  |  | rows at most (0: see notes) |
| `match` | `str` |  |  | keep rows whose name contains this (any case) |
| `fields` | `[path]` |  |  | the row members to keep (paths into structs and arrays) |
| `nonzero` | `bool` |  |  | leave out zero values (0, false, empty, None) |
| `elements` | `int` |  | `8` | elements per array, set or map (1..256) |

## Error kinds

An error's `kind`, and the shape of its `message` (`<...>` stands for a value).

| Kind | When | Message |
|---|---|---|
| `schema` | the call does not fit its verb's row, or a builder refuses a value | unknown key '<key>' (did you mean <key>?); <verb> takes <keys> \| missing <key>: <type> \| <key>: <type>, not <value> \| '<key>' goes inside <object> \| <key>: <why> |
| `pack` | the verb's tool pack is outside this console's --packs | <verb> is in the <pack> pack, outside this console's --packs <packs>: start it with <pack> in --packs |
| `validation` | the target refused what the call asks (a guard, a state, a mismatch) | <what> refused: <why> |
| `unsupported` | the agent or host lacks the operation | agent does not support <operation> |
| `file` | a file the call names cannot be read or written | cannot read\|write <path>: <why> |
| `agent` | the agent refused the request | <op>: <reason> |
| `timeout` | the agent did not answer in time | <op> timed out |
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
| `chain` | a pointer chain broke | hop <n>: <reason> |
| `host` | a host verb returned nothing usable | host command returned no result |
| `internal` | a console fault: a result that does not match its row | <why> |

## Core

### help

the verbs; with verb: its args, result, notes and example.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `verb` | `str` |  |  | the verb |
| `json` | `bool` |  |  | the whole table for programs |
| `hash` | `bool` |  |  | only the table's hash |

Result: `{rows: [{verb: str, origin: str, pack: explore\|native\|observe\|modify, summary: str, writes?: bool, blocks?: bool, args?: [value], result?: str, notes?: str, example?: value, types?: {<key>: str}, type?: value}], notes?: [str], table_hash?: str, types?: {<key>: {notation: str, type: value}}, origins?: [{origin: str, title: str}], packs?: [{pack: str, job: str}]}`

```json
{"args":{},"verb":"help"}
```

### status

agent pid, exe, tick rate, host packs, table_hash.

Pack `explore`.

Result: `{pid: u32, exe: str, build: str, version: str, commit: str, replaced_agents: u32, ticks: u64, ticks_per_second: u64, interval_ms: u64, ticking: bool, host_thread: engine\|borrowed\|own\|unset, tick_note?: str, roots?: [str], packs: [str], game_thread?: value, tool_packs: [str], table_hash: str}`

table_hash changes when a verb's args or result change: a cached script or skill is stale.

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

### usage

this session's cost: calls, output and refusals per verb, largest first.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `report` | `sessions\|all` |  |  | sessions: list the log's sessions; all: every session together |
| `session` | `str` |  |  | a session id |
| `advice` | `bool` |  |  | verbs often digested or unfiltered, with a proposed view.limit or hint |
| `baseline` | `file` |  |  | a saved report: what got worse or better |
| `save` | `file` |  |  | write this report to a file as a baseline |
| `only` | `[totals\|rows\|largest\|never_called\|session_rows\|context\|refusals\|advice\|compare]` |  |  | keep only these parts (advice turns it on) |

Result: `{session?: str, sessions?: u64, since?: str, totals?: {calls: u64, batched: u64, in: u64, out: u64, out_by_caller: {<key>: u64}, calls_by_pack: {<key>: u64}, refused: u64, digested: u64, saved: u64, ms: f64, slot_ms: f64, slot_ms_max: f64}, rows?: [{verb: str, origin: str, calls: u64, out: u64, largest: u64, ms: f64, slot_ms?: f64, batched?: u64, unfiltered?: u64, repeated?: u64, same_args?: u64, refused?: u64, refused_by?: {<key>: u64}, digested?: u64, saved?: u64, followed?: u64, raised?: u64}], largest?: [{verb: str, keys: [str], out: u64, caller: str, batch?: bool, digest?: u64}], never_called?: [str], session_rows?: [{session: str, since: str, until: str, callers: [str], calls: u64, out: u64, tool_out: u64, digested: u64, saved: u64, refused: u64}], context?: {chars: u64, limit: u64, over: bool}, refusals?: [{verb: str, kind: str, key?: str, count: u64}], advice?: [{verb: str, calls: u64, digested: u64, unfiltered: u64, followed: u64, raised: u64, limit?: {key: str, now: u64, proposed: u64}, hint: str}], saved?: str, compare?: {baseline: str, checked: u64, worse: [{what: str, verb?: str, before: f64, after: f64}], better: [{what: str, verb?: str, before: f64, after: f64}]}}`

Reads the usage log beside the console; works with no target. out is characters; out_by_caller.tool is what reached an agent's context. Marks and compare rules: docs/commands/console.md#usage-log-and-usage. Past --context-limit (default 400000) every envelope carries context_warning; nothing stops.

```json
{"args":{},"verb":"usage"}
```

### batch

up to 64 calls in as few host ticks as possible; one envelope each.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `calls` | `calls` | yes |  | calls, each {"verb", "args"} (up to 64) |

Result: `{all_ok: bool, same_tick: bool, first_tick: u64, last_tick: u64, spans_ticks: [{index: u64, command: str}], rows: [value], tick_accurate?: bool}`

rows: each call's envelope (tick, last_tick). Not allowed inside: help, status, usage, quit, batch, run.

```json
{"args":{"calls":[{"args":{"addr":"0x140001000"},"verb":"read"}]},"verb":"batch"}
```

### run

a plan file's steps, each its own call: NDJSON lines, then a report.

Pack `explore`; blocks (left out of MCP).

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `plan` | `file` | yes |  | JSON list of {"verb", "args"?, "expect"?, "repeat"?: {"every", "for"}} |
| `out` | `file` |  |  | write the lines here, not in the result |
| `background` | `bool` |  |  | frontends only: run in its own console, answer with a run id (a console refuses) |

Result: `{file?: str, lines: u64, rows?: [value], report: RunReport}`

A step of a pack outside --packs is a refused line, not a stop. Up to 256 steps; run and quit not allowed. Steps are not from one moment: use a batch step for that. repeat: up to 600 s, 1000 runs. Line and report shape: docs/commands/console.md#plans. Background runs: run_status, run_read, run_stop.

```json
{"args":{"plan":"out.json"},"verb":"run"}
```

### read

hex dump of bytes; type decodes values.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `addr` | `chain` | yes |  | the address, or a pointer chain to follow first |
| `len` | `int` |  | `64` | how many bytes (1..4096) |
| `type` | `layout` |  |  | i8..u64\|f32\|f64\|ptr\|cstr\|wstr, or a layout "u8 phase, pad 3, f64[3] start" |

Result: `{address: addr, module?: str, rva?: hex, size: u64, hex: bytes, views?: {i32?: i32, u32?: u32, f32?: value, i64?: i64, u64?: u64, ptr?: addr, ptr_module?: str, ptr_rva?: hex, f64?: value}, values?: [value], layout?: value, text?: str, length?: u64, terminated?: bool, region?: Region, chain?: Chain}`

cstr|wstr: text (default 256 bytes). region is the memory kind at the address.

```json
{"args":{"addr":"0x140001000"},"verb":"read"}
```

### peek

what 1..64 qwords hold: object, module, heap or value.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `addr` | `chain` | yes |  | the address, or a pointer chain to follow first |
| `count` | `int` |  | `8` | how many qwords (1..64) |
| `label` | `bool` |  |  | append this qword as a confirmed global to a labels file |
| `file` | `file` |  |  | with label: the labels file (default: names file) |
| `name` | `str` |  |  | with label: the name (default FName_<hint>) |

Result: `{address: addr, module?: str, rva?: hex, region?: Region, rows: [{address: addr, module?: str, rva?: hex, label: object\|module\|heap\|value\|unreadable\|..., value?: hex, object?: Object, value_module?: str, value_rva?: hex, f32?: [value], f64?: value, name_hint?: str}], labelled?: {module: str, module_size: u64, timestamp: u32, rva: hex, name: str, kind: user\|global\|exec\|vtable\|interface}, chain?: Chain}`

object needs a pack. region as read.

```json
{"args":{"addr":"0x140001000"},"verb":"peek"}
```

### write

write up to 256 bytes, optionally timed (undo, hold); clear ends timed writes.

Pack `modify`; **writes or calls**.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `addr` | `addr` |  |  | the address (required unless clear) |
| `data` | `bytes` |  |  | hex pairs (up to 256; 64 with undo or hold; required unless clear) |
| `expect` | `bytes` |  |  | write only while memory holds these bytes (required with undo, restored after) |
| `undo` | `obj` |  |  | restore after a while, guarded on the owner (needs expect) |
| `undo.after` | `dur` | yes |  | restore after this long (up to 60 s) |
| `undo.owner` | `ref` | yes |  | the live actor or component holding the address (a pack names it) |
| `undo.object` | `addr` |  |  | a native object inside the owner whose vtable is guarded too |
| `hold` | `dur` |  |  | rewrite the bytes every tick for this long (up to 60 s) |
| `clear` | `str` |  |  | end timed writes: "all" or an id from timed-writes |

Result: `{address?: addr, module?: str, rva?: hex, size?: u64, timed?: TimedWrite, cleared?: [TimedWrite]}`

A refused expect answers found = the bytes there. undo and hold answer timed. undo ends conflict (value changed) or abandoned (owner or vtable changed); without a pack's owner check it is refused. clear takes no other key.

```json
{"args":{"addr":"0x140001000","data":"00 00 80 3f"},"verb":"write"}
```

### timed-writes

timed writes (undo, hold) and their state.

Pack `modify`.

Result: `{rows: [TimedWrite]}`

Rows: undo writes (restored, conflict or abandoned stay until cleared), then hold ones.

```json
{"args":{},"verb":"timed-writes"}
```

### modules

loaded modules, exe first (also refreshes the cached list).

Pack `explore`.

Result: `{rows: [{name: str, base: addr, size: u64}]}`

```json
{"args":{},"verb":"modules"}
```

### search

addresses holding a value in resident memory, as a job the agent runs.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `type` | `hex\|u32\|i32\|u64\|ptr\|f32\|f64\|text\|wtext` | yes |  | the value's type (wtext: text as UTF-16) |
| `value` | `scalar` | yes |  | a string for text or hex pairs, else a number (u64, ptr also "0x...") |
| `scope` | `obj` |  |  | where to look (default: main module) |
| `scope.module` | `str` |  |  | one module, in place of from and to |
| `scope.from` | `addr` |  |  | where to start, in place of module |
| `scope.to` | `addr` |  |  | end |
| `scope.writable` | `bool` |  |  | writable memory only, every module and heap |
| `scope.heap` | `bool` |  |  | private memory only (heaps, stacks) |
| `scope.paged` | `bool` |  |  | also non-resident pages (slow) |
| `align` | `int` |  |  | 1, 2, 4, 8 or 16 (default: the type's size) |
| `wait` | `dur` |  | `10` | how long to wait for the job (up to 60 s) |
| `view` | `obj` |  |  | paging and filters |
| `view.cursor` | `int` |  |  | the previous result's next_cursor, as it gave it (a number or "0x...") |
| `max` | `int` |  | `64` | hits at most (1..507); the job stops when full |

Result: `{rows: [{address: addr, module?: str, rva?: hex, text?: str}], job: u32, state: running\|done\|stopped, from: addr, to: addr, scanned: f64, next_cursor?: addr}`

Still running after wait: state running and hits so far; search-job with its id for more or stop. A new search stops the last job. next_cursor continues it: the same search with view.cursor.

```json
{"args":{"type":"hex","value":"00 00 80 3f"},"verb":"search"}
```

### search-job

a search job's state and hits so far; stop ends it first.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `job` | `int` | yes |  | the job a search returned |
| `stop` | `bool` |  |  | end the job first; its next_cursor continues it (search with view.cursor) |

Result: `{rows: [{address: addr, module?: str, rva?: hex, text?: str}], job: u32, state: running\|done\|stopped, from: addr, to: addr, scanned: f64, next_cursor?: addr}`

Only the last job answers. scanned: share of searchable bytes behind the cursor.

```json
{"args":{"job":16},"verb":"search-job"}
```

### mem-snap

keep up to 1 MiB for mem-diff.

Pack `observe`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `addr` | `chain` | yes |  | the address, or a pointer chain to follow first |
| `len` | `int` | yes |  | bytes (up to 1048576) |

Result: `{address: addr, module?: str, rva?: hex, length: u64, tick: u64}`

A new snap replaces the last one. Refused if part of the range is unreadable.

```json
{"args":{"addr":"0x140001000","len":16},"verb":"mem-snap"}
```

### mem-diff

elements of the last mem-snap that changed, stayed, rose or fell; keep narrows.

Pack `observe`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `compare` | `changed\|unchanged\|increased\|decreased` | yes |  | what an element must have done |
| `type` | `str` | yes |  | element type i8..u64, f32, f64 or ptr; one per size bytes |
| `keep` | `bool` |  |  | keep only the matches as candidates and today's bytes as the new baseline |
| `view` | `obj` |  |  | paging and filters |
| `view.limit` | `int` |  | `64` | rows at most (0: see notes) |

Result: `{address: addr, module?: str, rva?: hex, length: u64, type: str, compare: changed\|unchanged\|increased\|decreased, compared: u64, matched: u64, tick: u64, rows: [{address: addr, before: value, after: value}], capped: bool, kept?: bool}`

changed and unchanged compare bytes, increased and decreased numbers (NaN never passes). matched counts all; rows show view.limit (1..256). A snap from another agent instance is refused.

```json
{"args":{"compare":"changed","type":"f32"},"verb":"mem-diff"}
```

### watch-start

a capture the agent keeps (up to 60 min); returns at once.

Pack `observe`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `duration` | `dur` | yes |  | how long, in seconds |
| `ranges` | `[range]` | yes |  | up to 8: {addr, len}, {addr, type, count?} to decode, {object, field} for a reflected field; {offset, ...} instead of addr with follow |
| `every` | `dur` |  |  | interval (default: each tick, then at most 30 s) |
| `follow` | `str` |  |  | a host root: ranges are offsets from it |
| `file` | `file` |  |  | keep every sample here (JSON lines) |

Result: `{capacity: u32, ms: u32, interval_ms: u32, root?: str, ranges: [WatchRange], file?: str}`

status lists the roots. The capture outlives the console. Typed ranges add a summary (first, last, least, greatest, change/s). file persists in the cache; a later watch-start without it drops it. For a blocking capture: start, wait, stop.

```json
{"args":{"duration":5,"ranges":[{"addr":"0x140001000","len":4}]},"verb":"watch-start"}
```

### watch-read

the capture so far: first sample, changes, last; stop ends it first.

Pack `observe`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `all` | `bool` |  |  | every sample, not only the changes |
| `stop` | `bool` |  |  | end the capture first (the answer is the final record) |

Result: `{running: bool, stopped?: deadline\|stopped\|buffer full\|object replaced, capacity: u32, ms: u32, interval_ms: u32, root?: str, ranges: [WatchRange], rows: [WatchSample], samples?: u64, dropped?: u64, gaps?: [{from_us: u64, to_us?: u64}], summary?: [WatchSummary], file?: str}`

Past 200 rows the middle is dropped (dropped). samples counts all; the summary covers every sample.

```json
{"args":{},"verb":"watch-read"}
```

### sig-check

is a byte signature unique in a module and where it lands; file checks a file of them.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `pattern` | `pattern` |  |  | the signature: "48 8b ?? 89" or "0x48, 0x8B, -1, 0x89", up to 96 bytes |
| `file` | `file` |  |  | a JSON-lines file, one signature per line, in place of pattern |
| `module` | `str` |  |  | the module (default: the exe) |
| `expect` | `addr` |  |  | where it should land |
| `offset` | `int` |  |  | the match starts this far past expect |
| `resolve` | `str` |  |  | a reference signature "next,field[,size]"; expect is then its target |

Result: `{pattern?: str, list?: str, module?: str, unique?: bool, total?: u64, rows?: [{address: addr, module?: str, rva?: hex, reference?: bool, diverges_at?: u64, resolved?: addr, resolved_module?: str, resolved_rva?: hex}], length?: u64, fixed_bytes?: u64, anchor?: {offset: u64, length: u64, hits: u64}, warnings?: [{offset: u64, size: u64, kind: call\|jump\|branch\|rip\|absolute, target: addr, target_module?: str, target_rva?: hex}], decoded?: u64, unique_at_length?: u64, resolve?: {next: u64, field: u64, size: u64}, expect?: addr, expect_module?: str, expect_rva?: hex, offset?: u64, resolves?: bool, mismatches?: [{offset: u64, wanted: hex, found: hex}], mismatch_error?: str, file?: str, winner?: u64, searches?: u64, lines?: [{line: u64, ok: bool, result?: {pattern: str, list: str, module: str, unique: bool, total: u64, rows: [{address: addr, module?: str, rva?: hex, reference?: bool, diverges_at?: u64, resolved?: addr, resolved_module?: str, resolved_rva?: hex}], length: u64, fixed_bytes: u64, anchor: {offset: u64, length: u64, hits: u64}, warnings: [{offset: u64, size: u64, kind: call\|jump\|branch\|rip\|absolute, target: addr, target_module?: str, target_rva?: hex}], decoded?: u64, unique_at_length?: u64, resolve?: {next: u64, field: u64, size: u64}, expect?: addr, expect_module?: str, expect_rva?: hex, offset?: u64, resolves?: bool, mismatches?: [{offset: u64, wanted: hex, found: hex}], mismatch_error?: str}, error?: str}]}`

Reports mismatches, diverges_at, warnings. file takes no other key: answers file, winner, a row per line.

```json
{"args":{"pattern":"48 8b 05 ?? ?? ?? ?? 48 85 c0"},"verb":"sig-check"}
```

### sig-bytes

live bytes in both pattern forms, address fields wildcarded (up to 96; longer is capped).

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `addr` | `chain` | yes |  | the address, or a pointer chain to follow first |
| `len` | `int` |  | `64` | bytes |

Result: `{address: addr, module?: str, rva?: hex, length: u64, capped?: bool, pattern: str, list: str, suggested: {pattern: str, list: str, fixed_bytes: u64, fields: [{offset: u64, size: u64, kind: call\|jump\|branch\|rip\|absolute, target: addr, target_module?: str, target_rva?: hex}], decoded?: u64}, chain?: Chain}`

```json
{"args":{"addr":"0x140001000"},"verb":"sig-bytes"}
```

### labels-sign

sign each user and global name of this build so it survives a patch.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `file` | `file` |  |  | the labels file (default: names file) |
| `module` | `str` |  |  | the module (default: the exe) |
| `retry` | `bool` |  |  | also rows with a signature or that failed before |

Result: `{path: str, module: str, timestamp: u32, signed: u64, unsigned: u64, skipped: u64, failed_before: u64, searches: u64, rows: [{name: str, kind: str, rva: hex, status: signed\|unsigned, length?: u64, site?: hex, reason?: str}]}`

Signs each name by a byte signature (a global by a reference signature) stored as the row's sig; unsignable rows get sig_failed and are skipped later (failed_before) unless retry. The file is rewritten in place; other builds' lines stay.

```json
{"args":{},"verb":"labels-sign"}
```

### labels-port

find other builds' signed names in the running build and add them to the file.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `file` | `file` |  |  | the labels file (default: names file) |
| `module` | `str` |  |  | the module (default: the exe) |

Result: `{path: str, module: str, timestamp: u32, ported: u64, missing: u64, ambiguous: u64, present: u64, unsigned: u64, searches: u64, rows: [{name: str, kind: str, from: u32, status: ported\|missing\|ambiguous, rva?: hex, matches?: u64, reason?: str}]}`

Per name this build lacks, matches the newest signed row of another build; one match appends a row (sig, ported_from); none (missing) or several (ambiguous) are listed, never written. Old rows stay.

```json
{"args":{},"verb":"labels-port"}
```

### labels-check

re-verify this build's signed names: does each sig still land.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `file` | `file` |  |  | the labels file (default: names file) |
| `module` | `str` |  |  | the module (default: the exe) |
| `status` | `[ok\|moved\|missing\|ambiguous\|invalid]` |  |  | only rows with these statuses (counts still cover every row) |
| `view` | `obj` |  |  | paging and filters |
| `view.limit` | `int` |  |  | rows at most (0: see notes) |

Result: `{path: str, module: str, timestamp: u32, ok: u64, moved: u64, missing: u64, ambiguous: u64, invalid: u64, unsigned: u64, searches: u64, rows: [{name: str, kind: str, rva: hex, status: ok\|moved\|missing\|ambiguous\|invalid, found?: hex, matches?: u64, reason?: str}], matched?: u64, capped: bool, scan_ms: i64}`

Per signed row: ok, moved (found), missing, ambiguous (matches) or invalid. unsigned counts rows with no sig. status keeps only those statuses; view.limit the first rows (0: all). Read-only.

```json
{"args":{},"verb":"labels-check"}
```

### disasm

instructions from live bytes (default 24), or a whole function.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `addr` | `chain` | yes |  | the address, or a pointer chain to follow first |
| `count` | `int` |  | `24` | instructions (1..1024; with whole: up to 2048) |
| `whole` | `bool` |  |  | the whole containing function (exception table, else a walk from addr) |
| `save` | `obj` |  |  | with whole: write it and its read-only data to files for a static tool |
| `save.file` | `file` | yes |  | the file to write |
| `save.callees` | `int` |  |  | also the functions it calls, this many levels (1..3) |
| `save.labels` | `file` |  |  | name from this labels file |
| `jumps` | `bool` |  |  | step through up to 3 unconditional direct jumps |
| `view` | `obj` |  |  | paging and filters |
| `view.cursor` | `int` |  |  | the previous result's next_cursor, as it gave it (a number or "0x...") |

Result: `{address: addr, module?: str, rva?: hex, size: u64, stop: str, next_cursor?: addr, rows?: [{address: addr, module?: str, rva?: hex, bytes?: bytes, text: str, target?: addr, target_module?: str, target_rva?: hex, string?: str, wide?: bool, import?: str, fields?: [{offset: u64, size: u64, kind: str}], ends?: bool, value?: f64, value_type?: str, value_hex?: bytes}], strings?: [{text: str, wide?: bool, at: [addr]}], saved?: str, saved_rows?: u64, functions?: [{start: addr, end: addr, rows: u64, depth?: u32}], skipped?: [{address: addr, why: str}], chunks?: [{start: addr, end: addr, rows: u64, depth?: u32}], tables?: [{jump: addr, table: addr, targets: u32}], jumpouts_left?: [addr], capped?: bool, layouts?: {classes: [str], types: u64}, piece?: {start: addr, end: addr}, function?: {start: addr, end: addr, module?: str, rva?: hex, chunk?: {start: addr, end: addr}}, end?: {address: addr, offset: u64, reason: str}, followed?: [{from: addr, to: addr, to_module?: str, to_rva?: hex}], chain?: Chain}`

Keep count small: a whole function belongs in save, not a reply. whole stops at 2048 instructions or 16 KB (next_cursor: the same call with view.cursor continues there; a cursor also starts a save at that exact address). save leaves rows out of the result. Without an exception-table entry, save refuses a walk from past padding unless view.cursor names the address.

```json
{"args":{"addr":"0x140001000"},"verb":"disasm"}
```

### xrefs

what points at an address: rel32 fields and stored pointers, each with its function.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `addr` | `chain` | yes |  | the address, or a pointer chain to follow first |
| `module` | `str` |  |  | scan another loaded module |
| `loose` | `bool` |  |  | also list unverified hits (likely chance bytes) |

Result: `{address: addr, module?: str, rva?: hex, scanned: str, kinds: {<key>: u64}, rows: [{kind: call\|jmp\|jcc\|lea\|load\|store\|rip\|pointer\|unknown, field: addr, instruction?: addr, module?: str, rva?: hex, function?: {start: addr, end: addr, module?: str, rva?: hex, chunk?: {start: addr, end: addr}}, unlisted?: bool}], capped: bool, scan_ms: i64, chain?: Chain}`

A code row outside every exception-table entry carries unlisted, its function the piece holding it.

```json
{"args":{"addr":"0x140001000"},"verb":"xrefs"}
```

### touches

functions near an address whose [reg + disp] operands use the offsets (up to 8).

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `addr` | `chain` | yes |  | the address, or a pointer chain to follow first |
| `offsets` | `[int]` | yes |  | the struct offsets to look for (up to 8, each above 0) |
| `range` | `int` |  | `524288` | bytes each side of addr (1..0x400000) |
| `access` | `write\|read\|any` |  | `"any"` | write: stores and read-modify-writes; read: loads and read-modify-writes, ranked by offsets loaded, rows storing every offset they load dropped; any: all uses, lea too |
| `bases` | `[rax\|rbx\|rcx\|rdx\|rsi\|rdi\|rbp\|rsp\|r8\|r9\|r10\|r11\|r12\|r13\|r14\|r15]` |  |  | only operands on these base registers (default: any but rsp and rbp) |

Result: `{address: addr, module?: str, rva?: hex, offsets: [hex], range: u64, scanned: {from: addr, to: addr, functions: u64, unlisted: u64, bytes: u64}, total: u64, rows: [{function: {start: addr, end: addr, module?: str, rva?: hex, chunk?: {start: addr, end: addr}}, unlisted?: bool, distinct: u64, writes: bool, total: u64, uses: [{offset: hex, instruction: addr, module?: str, rva?: hex, write: bool, text: str}]}], capped: bool, scan_ms: i64, chain?: Chain}`

Most offsets first, writes marked. Unlisted code is scanned in pieces (rows carry unlisted). Stack slots ([rsp + d], [rbp + d]) are left out unless bases names their register. access read drops struct copies and constructors. The whole image for a few offsets: disp-scan.

```json
{"args":{"addr":"0x140001000","offsets":[16],"range":"0x80000"},"verb":"touches"}
```

### disp-scan

instructions in the main module with a [reg + disp] operand at these offsets, paged.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `offsets` | `[int]` | yes |  | the displacements (the fields' offsets; up to 8, each above 0) |
| `access` | `write\|read\|any` |  | `"write"` | write: stores and read-modify-writes; read: loads and read-modify-writes; any: also lea |
| `width` | `int` |  |  | only operands of this many bytes (1, 2, 4, 8, 16, 32 or 64) |
| `imm` | `int` |  |  | only instructions with this immediate (compared in its width) |
| `bases` | `[rax\|rbx\|rcx\|rdx\|rsi\|rdi\|rbp\|rsp\|r8\|r9\|r10\|r11\|r12\|r13\|r14\|r15]` |  |  | only operands on these base registers (default: any but rsp) |
| `same_base` | `bool` |  |  | only functions using every disp on one base register (2+ disps): functions lists them |
| `view` | `obj` |  |  | paging and filters |
| `view.cursor` | `int` |  |  | the previous result's next_cursor, as it gave it (a number or "0x...") |
| `view.limit` | `int` |  | `128` | rows at most (0: see notes) |

Result: `{disps: [hex], access: str, module: str, from: addr, to: addr, scanned: f64, total: u64, rows: [{address: addr, rva: hex, disp: hex, base: str, access: write\|read\|both\|address, width: u64, text: str, function?: hex}], functions?: [{function: hex, base: str, hits: u64}], next_cursor?: addr, scan_ms: i64, read_ms: i64}`

Each call scans up to 8 s and stops at view.limit hits (default 128, at most 4096). view.cursor continues at next_cursor (absent at the end). A full sweep takes seconds through the agent's image window, else about 1 MB/s. Sweeps over many pages belong in a run plan. Jump tables can decode as code: check a surprising hit with disasm. Near one function: touches.

```json
{"args":{"offsets":[16]},"verb":"disp-scan"}
```

### vtable

an object's virtual table: slots until the first entry that is not code.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `addr` | `chain` | yes |  | the address, or a pointer chain to follow first |
| `offset` | `int` |  |  | the table pointer at addr+offset (an interface part; 0..0x100000) |
| `slots` | `[int]` |  |  | only these slots (each 0..511) |
| `parts` | `int` |  |  | list the table pointers in the object's first bytes (8..0x10000) |
| `resolve` | `bool` |  |  | where jump stubs lead |
| `rtti` | `bool` |  |  | also the class and bases its run-time type information names (MSVC images) |
| `view` | `obj` |  |  | paging and filters |
| `view.limit` | `int` |  |  | rows at most (0: see notes) |

Result: `{address: addr, module?: str, rva?: hex, offset: hex, table?: addr, table_module?: str, table_rva?: hex, stop?: count\|not code\|end of read, stop_value?: hex, found?: hex, rows: [{slot: u64, offset: hex, target: addr, module?: str, rva?: hex, body?: addr, body_module?: str, body_rva?: hex}], parts?: [{offsets: [hex], table: addr, table_module?: str, table_rva?: hex, rows: [{slot: u64, offset: hex, target: addr, module?: str, rva?: hex, body?: addr, body_module?: str, body_rva?: hex}]}], rtti?: {locator: addr, name: str, decorated: str, complete: addr, complete_offset: hex, bases: [{name: str, decorated: str}], capped: bool}, rtti_error?: str, chain?: Chain}`

addr is the object, or the table itself when its first qword points into code. view.limit: 1..512 slots. rtti_error when the image has no locator.

```json
{"args":{"addr":"0x140001000"},"verb":"vtable"}
```

## Unreal pack

### find

objects by name text and/or class; 24 per page, or limit.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `text` | `text` |  |  | part of the object's name, any case |
| `cls` | `str` |  |  | instances of the class or a subclass |
| `live` | `bool` |  |  | live instances only: no Default__ or archetype objects |
| `outer` | `str` |  |  | objects under this outer: a ref or "0x...", or an outer's name text |
| `view` | `obj` |  |  | paging and filters |
| `view.cursor` | `int` |  |  | the previous result's next_cursor, as it gave it (a number or "0x...") |
| `view.limit` | `int` |  |  | rows at most (0: see notes) |

Result: `{rows: [Object], total?: u64, from: u32, next_cursor?: u32}`

Give text, cls or outer. Only the first page counts total. view.limit 1..1000; next_cursor continues.

```json
{"args":{"text":"Player"},"verb":"find"}
```

### census

live instances per class, most first; 100 rows a page; diff: only changed counts.

Pack `observe`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `cls` | `str` |  |  | instances of the class or a subclass |
| `outer` | `str` |  |  | objects under this outer: a ref or "0x...", or an outer's name text |
| `diff` | `bool` |  |  | only the classes whose count differs from the last census of that cls and outer |
| `view` | `obj` |  |  | paging and filters |
| `view.cursor` | `int` |  |  | the previous result's next_cursor, as it gave it (a number or "0x...") |
| `view.limit` | `int` |  | `100` | rows at most (0: see notes) |
| `view.match` | `str` |  |  | keep rows whose name contains this (any case) |

Result: `{filter?: str, classes: u64, instances: u64, before?: {classes: u64, instances: u64}, ms: i64, rows: [{class: str, ref: ref, count: u32, before?: u32, samples: [ref]}], total: u64, from: u64, next_cursor?: u64, hint?: str}`

view.cursor (next_cursor) pages the stored census without a rescan. Every census is the next diff's baseline; diff is refused with no earlier census under the same filter.

```json
{"args":{},"verb":"census"}
```

### name-find

FName indices whose text contains text (gameplay tags, row names).

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `text` | `text` | yes |  | part of the pooled name |
| `view` | `obj` |  |  | paging and filters |
| `view.cursor` | `int` |  |  | the previous result's next_cursor, as it gave it (a number or "0x...") |

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

No name where the index is not an entry start or does not decode.

```json
{"args":{"indices":[16]},"verb":"name"}
```

### each

a table over a class's live instances: a value per column (up to 16 paths).

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `cls` | `str` | yes |  | a class or struct name |
| `paths` | `[path]` | yes |  | a field path per column, as get takes |
| `duration` | `dur` |  |  | one column: repeat for this long (up to 60 s) and report changes |
| `where` | `filter` |  |  | only instances whose field's value contains the text, any case (JSON text if not a string or name); Outer, Name and Class match the object's own |
| `outer` | `str` |  |  | objects under this outer: a ref or "0x...", or an outer's name text |
| `view` | `obj` |  |  | paging and filters |
| `view.limit` | `int` |  | `100` | rows at most (0: see notes) |
| `view.nonzero` | `bool` |  |  | leave out zero values (0, false, empty, None) |
| `view.elements` | `int` |  | `8` | elements per array, set or map (1..256) |

Result: `{class: str, class_ref: ref, columns: [str], mode: field\|call, rows: [{ref: ref, name?: str, values: [value], hex?: bytes}], same_tick: bool, capped: bool, truncated: bool, errors?: [{ref: ref, column: u32, message: str}], seconds?: f64, polls?: u64, changes?: [{ms: i64, tick: u64, ref: ref, before: value, after: value}], poll_error?: str, matched?: u64, omitted?: u64, unnamed?: u32}`

Read-only: a function is refused (call-each calls one). A path through a pointer is followed per instance (same_tick false). A failed cell is null with an errors entry.

```json
{"args":{"cls":"Name","paths":[["Field"]]},"verb":"each"}
```

### call

call a reflected function with inputs by name (typed) or a raw block.

Pack `modify`; **writes or calls**.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `str` | yes |  | an object: ref, "0x..." or Default__ name |
| `function` | `str` | yes |  | the function, as Owner:Function where the name is ambiguous |
| `params` | `params` |  |  | inputs by name (typed call; {} for none): struct as a list in member order or by member name, object as ref, name as text or "#index[:number]", null for None |
| `block` | `bytes` |  |  | the raw parameter block as hex pairs (none: zeroed) |
| `dry` | `bool` |  |  | resolve the function and build the block; call nothing |
| `view` | `obj` |  |  | paging and filters |
| `view.cursor` | `int` |  |  | the previous result's next_cursor, as it gave it (a number or "0x...") |
| `view.limit` | `int` |  | `100` | rows at most (0: see notes) |
| `view.fields` | `[path]` |  |  | the row members to keep (paths into structs and arrays) |
| `view.nonzero` | `bool` |  |  | leave out zero values (0, false, empty, None) |

Result: `{ref: ref, function: str, mode: raw\|typed\|release\|dry, block_size: u64, hex: bytes, kept: bool, kept_reason?: str, outputs?: [{name: str, direction: out\|return, value: value, truncated: bool, meta?: ue.FieldInfo}], truncated?: bool, exec?: addr, pure?: bool, unnamed?: u32}`

A raw block must equal the function's size (func: block_size). dry: show its block when asking to call. A fault in the engine answers faulted: treat the target as suspect.

```json
{"args":{"function":"Name","object":"0x1f2e3d4c50#123:4"},"verb":"call"}
```

### call-pure

call a const or pure function that keeps nothing; read-only; args as call.

Pack `observe`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `str` | yes |  | an object: ref, "0x..." or Default__ name |
| `function` | `str` | yes |  | the function, as Owner:Function where the name is ambiguous |
| `params` | `params` |  |  | inputs by name (typed call; {} for none): struct as a list in member order or by member name, object as ref, name as text or "#index[:number]", null for None |
| `block` | `bytes` |  |  | the raw parameter block as hex pairs (none: zeroed) |
| `dry` | `bool` |  |  | resolve the function and build the block; call nothing |

Result: `{ref: ref, function: str, mode: raw\|typed\|release\|dry, block_size: u64, hex: bytes, kept: bool, kept_reason?: str, outputs?: [{name: str, direction: out\|return, value: value, truncated: bool, meta?: ue.FieldInfo}], truncated?: bool, exec?: addr, pure?: bool, unnamed?: u32}`

Refused before the call: a function neither const nor pure, or with an output that owns memory (use call). Pure is the engine's flag, not a proof. No view. Inputs are never zeroed: a function with inputs needs params.

```json
{"args":{"function":"Name","object":"0x1f2e3d4c50#123:4"},"verb":"call-pure"}
```

### call-each

call a const or pure function with no inputs on every live instance of a class, in one tick.

Pack `observe`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `cls` | `str` | yes |  | a class name: its live instances and its subclasses' |
| `function` | `str` | yes |  | the function, as Owner:Function where the name is ambiguous |
| `duration` | `dur` |  |  | repeat for this long (up to 60 s) and report changes |
| `outer` | `str` |  |  | objects under this outer: a ref or "0x...", or an outer's name text |
| `view` | `obj` |  |  | paging and filters |
| `view.limit` | `int` |  | `100` | rows at most (0: see notes) |
| `view.nonzero` | `bool` |  |  | leave out zero values (0, false, empty, None) |

Result: `{class: str, class_ref: ref, columns: [str], mode: field\|call, rows: [{ref: ref, name?: str, values: [value], hex?: bytes}], same_tick: bool, capped: bool, truncated: bool, errors?: [{ref: ref, column: u32, message: str}], seconds?: f64, polls?: u64, changes?: [{ms: i64, tick: u64, ref: ref, before: value, after: value}], poll_error?: str, matched?: u64, omitted?: u64, unnamed?: u32}`

call-pure's checks per instance. The function takes no inputs; its block is 1..64 bytes.

```json
{"args":{"cls":"Name","function":"Name"},"verb":"call-each"}
```

### release

one more call (zeroed inputs by default) that drops the kept block.

Pack `modify`; **writes or calls**.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `str` | yes |  | an object: ref, "0x..." or Default__ name |
| `function` | `str` | yes |  | the function, as Owner:Function where the name is ambiguous |
| `block` | `bytes` |  |  | the raw parameter block as hex pairs (none: zeroed) |

Result: `{ref: ref, function: str, mode: raw\|typed\|release\|dry, block_size: u64, hex: bytes, kept: bool, kept_reason?: str, outputs?: [{name: str, direction: out\|return, value: value, truncated: bool, meta?: ue.FieldInfo}], truncated?: bool, exec?: addr, pure?: bool, unnamed?: u32}`

```json
{"args":{"function":"Name","object":"0x1f2e3d4c50#123:4"},"verb":"release"}
```

### member-name

name an unreflected member in <console>.native.txt.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `str` | yes |  | an object (ref or "0x..."), or a class or struct by name |
| `offset` | `int` | yes |  | the member's offset in the object |
| `type` | `str` | yes |  | its type, with a count for an array: f64[3] |
| `name` | `str` | yes |  | the member's name (a trailing ? marks it unconfirmed) |

Result: `{path: str, line: u64, owner: str, offset: hex, type: str, name: str, size: u32, timestamp: u32}`

get then shows it (native; stale after a patch).

```json
{"args":{"name":"Name","object":"0x1f2e3d4c50#123:4","offset":16,"type":"f32"},"verb":"member-name"}
```

### labels-export

names for a static tool (JSON lines, one build).

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `file` | `file` |  |  | the labels file (default: the console's, <console>.labels.jsonl) |
| `module` | `str` |  |  | the module (default: the exe) |
| `kinds` | `[exec\|vtable\|interface\|global]` |  |  | which rows to derive (default: all) |

Result: `{path: str, module: str, timestamp: u32, total: u64, globals: u64, user: u64, exec: u64, vtables: u64, interfaces: u64, interface_lookups: u64, other_builds: u64, capped: bool}`

Merged into the file: names written back by a static tool and globals confirmed by peek's label are kept, as are other builds' lines and kinds not derived now. global derives nothing.

```json
{"args":{},"verb":"labels-export"}
```

### assets-export

each live object of the classes into its own JSON file: nonzero fields, table rows.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `dir` | `file` | yes |  | the folder to write (created when missing) |
| `classes` | `[str]` | yes |  | the classes (and their subclasses) to export, up to 16 |
| `view` | `obj` |  |  | paging and filters |
| `view.match` | `str` |  |  | keep rows whose name contains this (any case) |

Result: `{dir: str, index: str, exported: u64, tables: u64, failed: u64, capped: bool, rows: [ExportedAsset], omitted?: u64}`

Up to 2000 objects per call (capped). Files <dir>/<Class>/<name>.json and <dir>/index.json; files are replaced on rerun.

```json
{"args":{"classes":["Name"],"dir":"out.json"},"verb":"assets-export"}
```

### layout-export

reflected class, struct and enum layouts to a JSON-lines file.

Pack `native`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `file` | `file` | yes |  | the file to write |
| `classes` | `[str]` | yes |  | class, struct or enum names (up to 256) |
| `closure` | `int` |  |  | also the types their fields reference, to this depth (1..8), and each one's super chain |

Result: `{path: str, classes: u64, structs: u64, enums: u64, fields: u64, missing?: [str], capped?: bool}`

missing lists names that are no class, struct or enum; capped: closure stopped at 1024 types.

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

Lines as layout-export writes them; offset a number or "0x.."; size and type optional. Rows stop at 200 (capped).

```json
{"args":{"file":"out.json"},"verb":"layout-check"}
```

### entries

one page of an array, set or map: elements, or key and value.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `str` | yes |  | an object: ref, "0x..." or Default__ name |
| `field` | `str` | yes |  | the field, as Owner:Field where the name is ambiguous |
| `view` | `obj` |  |  | paging and filters |
| `view.cursor` | `int` |  |  | the previous result's next_cursor, as it gave it (a number or "0x...") |

Result: `{ref: ref, field: str, kind: array\|set\|map, total: u64, rows: [{key?: value, value: value, key_object?: Object, value_object?: Object}], from: u32, next_cursor?: u32, truncated: bool, unnamed?: u32, tick_accurate?: bool}`

Elements and slots up to 512 bytes; a bigger one is refused: get the field.

```json
{"args":{"field":"Name","object":"0x1f2e3d4c50#123:4"},"verb":"entries"}
```

### rows

a data table's rows decoded by RowStruct, or a curve table's curves and keys.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `str` | yes |  | an object: ref, "0x..." or Default__ name |
| `at` | `num` |  |  | a curve table: each row's value at this time |
| `view` | `obj` |  |  | paging and filters |
| `view.cursor` | `int` |  |  | the previous result's next_cursor, as it gave it (a number or "0x...") |
| `view.limit` | `int` |  | `100` | rows at most (0: see notes) |
| `view.match` | `str` |  |  | keep rows whose name contains this (any case) |
| `view.fields` | `[path]` |  |  | the row members to keep (paths into structs and arrays) |
| `view.nonzero` | `bool` |  |  | leave out zero values (0, false, empty, None) |

Result: `{ref: ref, row_struct?: str, row_struct_ref?: ref, curve?: simple\|rich, at?: f64, total: u64, rows: [{name: str, address: addr, value?: value, interp?: linear\|constant\|cubic\|none, keys?: value, weighted?: bool}], from: u32, next_cursor?: u32, truncated: bool, unnamed?: u32}`

view.match keeps row names. Nested arrays up to 256 elements, 3 deep. A CurveTable gives per row keys ([t, v]; rich: [t, v, interp, arrive, leave]); at adds the value at that time.

```json
{"args":{"object":"0x1f2e3d4c50#123:4"},"verb":"rows"}
```

### get

an object's fields with values, or named paths; types: the layout.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `str` | yes |  | an object (ref, "0x...", Default__ name), or with types a class or struct by name |
| `paths` | `[path]` |  |  | a field path per value (up to 16); none: every field |
| `mode` | `values\|types\|snap\|changed` |  | `"values"` | values; types: layout, no values (class or struct: its own fields); snap: keep bytes as baseline; changed: fields changed since snap, with before |
| `own` | `bool` |  |  | only the fields the object's own class declares, none it inherits |
| `file` | `file` |  |  | with snap or changed: the snapshot file (across runs) |
| `view` | `obj` |  |  | paging and filters |
| `view.match` | `str` |  |  | keep rows whose name contains this (any case) |
| `view.nonzero` | `bool` |  |  | leave out zero values (0, false, empty, None) |
| `view.elements` | `int` |  | `8` | elements per array, set or map (1..256) |

Result: `{ref: ref, rows: [{path?: str, name?: str, raw?: str, owner?: str, type?: str, offset?: hex, size?: u32, struct?: str, bool_mask?: u32, element_type?: str, element_size?: u32, value_type?: str, value_size?: u32, value_offset?: hex, value_struct?: str, subtype?: str, value_subtype?: str, stride?: u32, native?: bool, stale?: bool, value?: value, before?: value, truncated?: bool, address?: addr, module?: str, rva?: hex, holder?: ref, value_module?: str, value_rva?: hex, object?: Object, preview?: {total: u64, elements: [{key?: value, value: value, object?: Object, key_object?: Object, value_object?: Object}]}, preview_error?: str, meta?: ue.FieldInfo, hex?: bytes, error?: str}], same_tick?: bool, truncated?: bool, capped?: bool, filtered?: {shown: u64, total: u64}, native_refused?: [{line: u64, name: str, owner: str, offset: hex, reason: str}], snap?: {size: u64, path?: str}, tick_accurate?: bool, unnamed?: u32}`

No paths: every field, compact; check truncated before treating values as complete. Paths: one row per path (path, value, address), Owner:Field where ambiguous; a bad path fails its row naming the closest fields. snap answers only {size, path}. view.match, own and the modes but values take no paths; types reads no values.

```json
{"args":{"object":"0x1f2e3d4c50#123:4"},"verb":"get"}
```

### line-trace

the first thing a line hits: actor, component, material, distance (read-only).

Pack `observe`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `start` | `point` | yes |  | a world point [x, y, z] in cm, an object's location (ref), or {"object": <ref>, "socket": "Name"} on a mesh |
| `end` | `point` | yes |  | the end, as start takes |
| `channel` | `int` |  | `0` | the trace type query (0..31): 0 visibility, 1 camera, N: TraceTypeQuery N+1 |
| `simple` | `bool` |  |  | simple collision instead of complex |
| `ignore` | `ref` |  |  | an actor the line passes through; also the world it runs in |

Result: `{start: [f64], end: [f64], channel: str, complex: bool, ignored?: ref, hit: bool, initial_overlap?: bool, distance?: f64, impact?: [f64], normal?: [f64], actor?: Object, component?: Object, material?: Object, bone?: str, face?: i32, item?: i32}`

Runs on the game thread; changes no state. Two points need ignore (the world comes from ignore, else an end's object). cm; distance from start. A socket needs a mesh component.

```json
{"args":{"end":[0,0,0],"start":[0,0,0]},"verb":"line-trace"}
```

### calls-start

record a reflected function's calls, with their arguments, for a while.

Pack `modify`; **writes or calls**.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `str` | yes |  | whose function: an instance (ref or "0x...": records only its calls) or a class (ref, "0x..." or name: every instance) |
| `function` | `str` | yes |  | the function, as Owner:Function where the name is ambiguous |
| `duration` | `dur` | yes |  | how long, in seconds (at most 60) |
| `dry` | `bool` |  |  | resolve and show the pointer that would be swapped; write nothing |

Result: `{function: str, slot: u32, exec: addr, thunk: addr, block_size: u32, object?: addr, seconds: f64, dry: bool, note?: str}`

Swaps the function's native pointer (UFunction::Func, heap data) for a forwarding thunk; restores it at the end or on calls-read with stop. Sees ProcessEvent calls (C++, RPCs, Blueprint events, delegates) with parameters; Blueprint-to-native calls show only the object; direct C++ calls are not seen. 256 rows per recording.

```json
{"args":{"duration":5,"function":"Name","object":"0x1f2e3d4c50#123:4"},"verb":"calls-start"}
```

### calls-read

a recording's calls so far, decoded by the function's parameters; stop ends it first.

Pack `modify`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `slot` | `int` |  |  | the recording, 0..7 (default: the last started) |
| `stop` | `bool` |  |  | end the recording now (restores the pointer) and return its calls |
| `view` | `obj` |  |  | paging and filters |
| `view.cursor` | `int` |  |  | the previous result's next_cursor, as it gave it (a number or "0x...") |

Result: `{function: str, state: recording\|expired\|finished\|replaced, slot: u32, calls: u64, events: u64, vm: u64, dropped: u64, rows: [{sequence: u64, ms: f64, thread: u32, object: ref, object_name?: str, object_class?: str, via: event\|vm, values?: [{name: str, direction: in\|out\|return, value: value, truncated: bool}]}], next_cursor?: u64, unnamed?: u32}`

values: in, out and return by name, read after the call. Objects named while they live; strings and containers print headers only. A recording past its time is restored first. state replaced: something else swapped the pointer since; it was left alone and the thunk keeps forwarding.

```json
{"args":{},"verb":"calls-read"}
```

### describe

an object's name, class and reusable ref.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `ref` |  |  | the object: ref or "0x..." |
| `slot` | `int` |  |  | in place of object: an object-table slot (a weak pointer's), with serial |
| `serial` | `int` |  |  | with slot: the slot's serial number |

Result: `{ref: ref, name: str, class: str, class_size?: u32, outer?: {name: str, ref: ref}}`

Give object, or slot and serial.

```json
{"args":{"object":"0x1f2e3d4c50#123:4"},"verb":"describe"}
```

### func

a function's reflected metadata, exec entry address and parameter signature.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `str` | yes |  | an object (ref or "0x..."), or a class or struct by name |
| `function` | `str` | yes |  | the function, as Owner:Function where the name is ambiguous |

Result: `{ref: ref, function: str, address: addr, flags: u32, exec?: addr, executable: bool, module?: str, rva?: hex, module_error?: str, interface?: {offset: hex, slot?: i64, implementation?: addr, implementation_module?: str, implementation_rva?: hex}, block_size?: u32, params?: [{name: str, owner: str, type: str, offset: hex, size: u32, struct?: str, bool_mask?: u32, element_type?: str, element_size?: u32, value_type?: str, value_size?: u32, value_offset?: hex, value_struct?: str, stride?: u32, subtype?: str, value_subtype?: str, direction: in\|out\|in ref\|in/out ref\|return, flags: u32}], params_error?: str}`

params in block order (direction in, out, in ref, in/out ref, return); block_size is the call block's size. Without the game thread, params_error says why.

```json
{"args":{"function":"Name","object":"0x1f2e3d4c50#123:4"},"verb":"func"}
```

### script

a Blueprint function's bytecode: names, functions, properties it uses; decode: as statements.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `str` | yes |  | an object or class (ref, "0x...", class name), or the Function itself (ref, as name-refs gives it) |
| `function` | `str` |  |  | the function's name (Owner:Name to pick one; none when object is the Function) |
| `decode` | `bool` |  |  | read the bytecode as statements with their offsets |
| `entry` | `int` |  |  | with decode: one event from this offset to its return, pop or goto |
| `to` | `int` |  |  | with decode: statements before this offset; with entry, to here, past returns |
| `view` | `obj` |  |  | paging and filters |
| `view.match` | `str` |  |  | keep rows whose name contains this (any case) |

Result: `{ref: ref, function: str, size: u32, names: [{offset: hex, count: u32, index: i32, name: str}], functions: [{offset: hex, count: u32, ref: ref, function: str}], properties: [{offset: hex, count: u32, address: addr, property: str, raw?: str}], statements?: [{offset: hex, op: str, text: str}], events?: [{offset: hex, event: str}], stopped?: str, filtered?: {shown: u64, total: u64}}`

decode reads every expression (no game thread) and stops at an unknown opcode (stopped); an uber graph adds events. view.match keeps statements whose text or op contains it; entry and to bound an offset range. A Function ref (name-refs' rows) as object needs no function.

```json
{"args":{"object":"0x1f2e3d4c50#123:4"},"verb":"script"}
```

### enum

an enum's names and values, in order.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `enum` | `str` | yes |  | an enum object (ref or "0x..."), or an enum by name |

Result: `{ref: ref, name: str, rows: [{name: str, value: i64}]}`

Names keep the enum's prefix (EFoo::Bar).

```json
{"args":{"enum":"Name"},"verb":"enum"}
```

### game-thread

arm or remove the host's game-thread tick (status shows it).

Pack `modify`; **writes or calls**.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `action` | `on\|off` | yes |  | on arms it, off puts the viewport's table back |

Result: `{mode: off\|waiting\|conflict\|on\|stopped, text: str, thread?: u32, viewport?: addr, table?: addr, draw?: addr, draw_module?: str, draw_rva?: hex}`

Needed by call, call-pure, call-each, release, func's params, each, script, refs, owner, route, exec entries and the recorder. on swaps one pointer in the viewport object for a copy of its Draw table; confirm Draw first (skill: references/unreal/game-thread.md). Unsupported when the host does not link reagentlive::ue-draw.

```json
{"args":{"action":"on"},"verb":"game-thread"}
```

### name-refs

Blueprint functions whose bytecode names a pooled name (EX_NameConst).

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `name` | `str` |  |  | the exact pooled name |
| `name_index` | `int` |  |  | in place of name: its comparison index |

Result: `{index: i32, name?: str, rows: [{ref: ref, owner: str, name: str, offset: hex, count: u32}], capped: bool}`

Give name or name_index.

```json
{"args":{"name":"None"},"verb":"name-refs"}
```

### owner

the object holding each address: the field, the Outer pointer or the container element.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `addrs` | `[addr]` | yes |  | addresses, as search rows give them (up to 256) |
| `within` | `[ref]` |  |  | objects whose arrays, sets and maps to check for an address no object holds (up to 16) |

Result: `{rows: [{address: addr, object?: Object, offset?: hex, path?: str, raw?: str, type?: str, outer?: bool, container?: bool}], owned: u32}`

One object-table scan per 64 addresses. An address held by heap storage has no owner: the containers of the within objects and of other rows' objects are checked (container). Needs the game thread.

```json
{"args":{"addrs":["0x140001000"]},"verb":"owner"}
```

### route

the shortest routes of reflected references from a root or object to an object.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `ref` | yes |  | the object to reach |
| `sources` | `[str]` |  |  | up to 4: host roots by name (status lists them) or objects; default: every root |
| `depth` | `int` |  | `8` | hops at most (1..16) |
| `budget` | `int` |  | `20000` | objects visited at most (1..1048576) |
| `up` | `bool` |  |  | also step from each object up to its Outer (child to parent only) |

Result: `{object: Object, rows: [{root?: str, source: Object, path: str, steps: [{field: str, object: Object}]}], end: found\|depth\|budget\|none, visited: u32, depth: u32}`

Breadth-first over object fields, arrays, sets and map values; no class, weak, soft or lazy pointers, no containers inside container elements. Up to 4 shortest routes; end says why none: depth, budget or none. Objects can die mid-walk: verify a route with get (entries for indexed steps). Needs the game thread.

```json
{"args":{"object":"0x1f2e3d4c50#123:4","sources":["pawn"]},"verb":"route"}
```

### funcs

functions on the class chain, then on the native interfaces it implements.

Pack `explore`.

| Key | Type | Required | Default | What |
|---|---|---|---|---|
| `object` | `str` | yes |  | an object (ref or "0x..."), or a class or struct by name |
| `view` | `obj` |  |  | paging and filters |
| `view.cursor` | `int` |  |  | the previous result's next_cursor, as it gave it (a number or "0x...") |
| `view.match` | `str` |  |  | keep rows whose name contains this (any case) |

Result: `{ref: ref, rows: [{owner?: str, name: str, via?: str}], from: u32, next_cursor?: u32}`

A row with via "interface" exists only on that interface: call refuses it; use func's implementation.

```json
{"args":{"object":"0x1f2e3d4c50#123:4"},"verb":"funcs"}
```
