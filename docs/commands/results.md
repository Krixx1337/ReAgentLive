# Result shapes

<!-- Generated from `help --json` (tests/table: `reagentlive-table --write docs/commands/results.md`). Do not edit: change the verb's row or its result struct. -->

What each command takes and the keys of its `result`, as the console itself reports them:
`help` lists the verbs, `help <verb>` shows one (flags, result, notes) and `help --json` prints
this whole table for programs. `status` carries `table_hash`, which changes whenever a row does.

## Notation

| Type | Meaning |
|---|---|
| `addr` | an address as `"0x..."` text |
| `hex` | a number as `"0x..."` text: an rva, an offset, a size |
| `bytes` | `"01 02 ff"`, in memory order |
| `ref` | an object ref; pass it verbatim to another command, a separate run included |
| `bool`, `u32`, `u64`, `i32`, `i64`, `f64`, `str` | plain JSON values |
| `a\|b` | a string that is one of the listed words (`...`: others occur) |
| `[T]` | a list; `[T\|null]`: elements may be null |
| `{<key>: T}` | a map |
| `{k: T, k?: T}` | an object; `?` marks a key that is omitted when it does not apply |
| `value` | game data: dynamic, shaped by what was read |
| `Name` | a named type, defined once under [Named types](#named-types); a pack's carry its prefix |

## Conventions

- `rows` is a result's list; `total` is how many there are in all when `rows` may show fewer.
- A page carries `from` (where it began) and `next_cursor` (pass it as `--start`; absent when done).
- `capped`: the rows were cut at a hard limit and there is no cursor. `truncated`: a value in the
  result is incomplete.
- `module` and `rva` stand beside an address inside a loaded module, so it opens at the same RVA
  in a static-analysis tool; other prefixes (`value_`, `target_`) locate the address in that key.
- A key that does not apply is omitted; results hold no nulls outside game values.
- A flag may add optional keys and never changes a key's type.
- **Writes or calls.** marks a verb that writes the target's memory or calls into it; every other verb
  only reads (`help --json` carries it as `writes`).

## Core

| Command | What it does | `result` |
|---|---|---|
| `help [verb] [--json]` | the verbs, one line each; help <verb>: its flags, result and notes | `{rows: [{verb: str, origin: str, usage: str, summary: str, writes?: bool, blocks?: bool, aliases?: [str], flags?: [{flag: str, key: str, takes: nothing\|one\|maybe\|some\|rest, type: str, required?: bool, value?: str, default?: str, help?: str}], result?: str, notes?: str, positionals?: [{key: str, name: str, arity: one\|optional\|some\|any\|rest, type: str, default?: str, help?: str}], types?: {<key>: str}, type?: value}], notes?: [str], table_hash?: str, types?: {<key>: {notation: str, type: value}}, origins?: [{origin: str, title: str}]}` |
| `status` | agent pid, tick rate, the packs the host offers and the verb table's hash | `{pid: u32, build: str, version: str, replaced_agents: u32, ticks: u64, ticks_per_second: u64, interval_ms: u64, ticking: bool, host_thread: engine\|borrowed\|own\|unset, tick_note?: str, roots?: [str], packs: [str], table_hash: str}` |
| `quit` | end the console | `{quit: bool}` |
| `batch { <command>; <command>; ... }` | up to 64 commands in as few host ticks as possible; one envelope each | `{all_ok: bool, same_tick: bool, first_tick: u64, last_tick: u64, spans_ticks: [{index: u64, command: str}], rows: [value], tick_accurate?: bool}` |
| `read <addr> [len] [--type type]` | hex dump of 1..4096 bytes (default 64); --type decodes values | `{address: addr, module?: str, rva?: hex, size: u64, hex: bytes, views?: {i32?: i32, u32?: u32, f32?: value, i64?: i64, u64?: u64, ptr?: addr, ptr_module?: str, ptr_rva?: hex, f64?: value}, values?: [value], layout?: value, text?: str, length?: u64, terminated?: bool, chain?: Chain}` |
| `read-batch <addr len>...` | read up to 8 ranges in one host tick | `{tick: u64, rows: [{address: addr, module?: str, rva?: hex, size: u64, ok: bool, hex?: bytes, error?: {status: u32, message: str}}]}` |
| `peek <addr> [count] [--label <file>] [--name <name>]` | what 1..64 qwords hold (default 8): object, module, heap or value | `{address: addr, module?: str, rva?: hex, rows: [{address: addr, module?: str, rva?: hex, label: object\|module\|heap\|value\|unreadable\|..., value?: hex, object?: Object, value_module?: str, value_rva?: hex, f32?: [value], f64?: value, name_hint?: str}], labelled?: {module: str, module_size: u64, timestamp: u32, rva: hex, name: str, kind: global\|exec\|vtable\|interface}, chain?: Chain}` |
| `write <addr> <hex bytes>... [--expect <hex bytes>...]` | write up to 4096 bytes **Writes or calls.** | `{address: addr, module?: str, rva?: hex, size: u64}` |
| `trial <addr> <hex bytes>... --owner <ref> --expect <hex bytes>... [--object <addr>] [--seconds <N>] [--ms <N>]` | one write the agent undoes (default 10 s, up to 60 s) **Writes or calls.** | `{id: u32, address: addr, bytes: bytes, original: bytes, owner: addr, object?: addr, state: active\|restored\|conflict\|abandoned\|unknown, changed_tick: u64, remaining_ms: u32, guard?: owner replaced or freed\|object vtable changed\|target unreadable\|unknown}` |
| `trial-list` | trials with their state | `{rows: [Trial]}` |
| `trial-clear [id]` | end one trial or all, restoring active ones **Writes or calls.** | `{cleared: u64, rows: [Trial]}` |
| `base` | exe base and size | `{name: str, base: addr, size: u64}` |
| `modules` | loaded modules, exe first (also refreshes the cached list) | `{rows: [{name: str, base: addr, size: u64}]}` |
| `search <hex\|u32\|i32\|u64\|ptr\|f32\|f64\|text> <value>... [--wide] [--start <addr>] [--to <addr>] [--module <name>] [--align <N>] [--writable] [--heap] [--paged] [--background] [--max <N>]` | addresses holding a value in resident, cacheable memory | `{rows: [{address: addr, module?: str, rva?: hex}], from: addr, next_cursor?: addr}` |
| `pin <addr> <hex bytes>... [--seconds <N>] [--ms <N>] [--expect <hex bytes>...]` | write the bytes every tick (default 10 s, up to 60 s) **Writes or calls.** | `{id: u32, address: addr, bytes: bytes, guarded: bool, state: active\|expired\|object changed\|not writable\|memory differed from --expect\|cleared\|access faulted\|unknown, writes: u32, remaining_ms: u32}` |
| `pin-list` | pins with state and write counts | `{rows: [Pin]}` |
| `pin-clear [id]` | clear one pin or all | `{cleared: u32}` |
| `watch <seconds\|Nms> <addr:len\|addr:type[n]>... [--then call line]` | sample up to 8 ranges every tick (up to 30 s) and return the rows **Writes or calls.** | `{capacity: u32, complete: bool, stopped?: deadline\|stopped\|buffer full\|object replaced, ranges: [WatchRange], then?: {ok: bool, hex?: bytes, message?: str}, rows: [WatchSample], summary?: [WatchSummary]}` |
| `watch-start <duration> <addr\|offset:len\|type[n]>... [--every <duration>] [--follow <root>]` | a capture the agent keeps (up to 60 min with --every); returns at once | `{capacity: u32, ms: u32, interval_ms: u32, root?: str, ranges: [WatchRange]}` |
| `watch-read` | the capture's rows so far | `{running: bool, stopped?: deadline\|stopped\|buffer full\|object replaced, capacity: u32, ms: u32, interval_ms: u32, root?: str, ranges: [WatchRange], rows: [WatchSample], gaps?: [{from_us: u64, to_us?: u64}], summary?: [WatchSummary]}` |
| `watch-stop` | end the capture and return its rows | `{running: bool, stopped?: deadline\|stopped\|buffer full\|object replaced, capacity: u32, ms: u32, interval_ms: u32, root?: str, ranges: [WatchRange], rows: [WatchSample], gaps?: [{from_us: u64, to_us?: u64}], summary?: [WatchSummary]}` |
| `sig-check <pattern>... [--module <name>] [--expect <addr>] [--offset <N>] [--resolve <next,field[,size]>]` | is a byte signature unique in a module, and does it land where expected | `{pattern: str, list: str, module: str, unique: bool, total: u64, rows: [{address: addr, module?: str, rva?: hex, reference?: bool, diverges_at?: u64, resolved?: addr, resolved_module?: str, resolved_rva?: hex}], length: u64, fixed_bytes: u64, anchor: {offset: u64, length: u64, hits: u64}, warnings: [{offset: u64, size: u64, kind: call\|jump\|branch\|rip\|absolute, target: addr, target_module?: str, target_rva?: hex}], decoded?: u64, unique_at_length?: u64, resolve?: {next: u64, field: u64, size: u64}, expect?: addr, expect_module?: str, expect_rva?: hex, offset?: u64, resolves?: bool, mismatches?: [{offset: u64, wanted: hex, found: hex}], mismatch_error?: str}` |
| `sig-file <patterns file>` | check a file of signatures (one JSON line each); names the first that holds | `{file: str, winner?: u64, searches: u64, rows: [{line: u64, ok: bool, result?: {pattern: str, list: str, module: str, unique: bool, total: u64, rows: [{address: addr, module?: str, rva?: hex, reference?: bool, diverges_at?: u64, resolved?: addr, resolved_module?: str, resolved_rva?: hex}], length: u64, fixed_bytes: u64, anchor: {offset: u64, length: u64, hits: u64}, warnings: [{offset: u64, size: u64, kind: call\|jump\|branch\|rip\|absolute, target: addr, target_module?: str, target_rva?: hex}], decoded?: u64, unique_at_length?: u64, resolve?: {next: u64, field: u64, size: u64}, expect?: addr, expect_module?: str, expect_rva?: hex, offset?: u64, resolves?: bool, mismatches?: [{offset: u64, wanted: hex, found: hex}], mismatch_error?: str}, error?: str}]}` |
| `sig-bytes <addr> [len]` | live bytes (default 64, up to 96) in both pattern forms, address fields wildcarded | `{address: addr, module?: str, rva?: hex, length: u64, pattern: str, list: str, suggested: {pattern: str, list: str, fixed_bytes: u64, fields: [{offset: u64, size: u64, kind: call\|jump\|branch\|rip\|absolute, target: addr, target_module?: str, target_rva?: hex}], decoded?: u64}}` |
| `disasm <addr> [count] [--function] [--follow]` | 1..64 instructions (default 24, up to 512 bytes) decoded from live bytes | `{address: addr, module?: str, rva?: hex, size: u64, stop: str, rows: [{address: addr, module?: str, rva?: hex, bytes: bytes, text: str, target?: addr, target_module?: str, target_rva?: hex, string?: str, wide?: bool, fields?: [{offset: u64, size: u64, kind: str}], ends?: bool}], function?: {start: addr, end: addr, module?: str, rva?: hex, chunk?: {start: addr, end: addr}}, end?: {address: addr, offset: u64, reason: str}, followed?: [{from: addr, to: addr, to_module?: str, to_rva?: hex}], chain?: Chain}` |
| `xrefs <addr> [--module <name>] [--all]` | what points at an address: rel32 fields and stored pointers, each with its function | `{address: addr, module?: str, rva?: hex, scanned: str, kinds: {<key>: u64}, rows: [{kind: call\|jmp\|jcc\|lea\|load\|store\|rip\|pointer\|unknown, field: addr, instruction?: addr, module?: str, rva?: hex, function?: {start: addr, end: addr, module?: str, rva?: hex, chunk?: {start: addr, end: addr}}}], capped: bool, scan_ms: i64}` |
| `touches <addr> <offset>... [--range <N>]` | functions near an address whose [reg + disp] operands use the offsets (up to 8) | `{address: addr, module?: str, rva?: hex, offsets: [hex], range: u64, scanned: {from: addr, to: addr, functions: u64, bytes: u64}, total: u64, rows: [{function: {start: addr, end: addr, module?: str, rva?: hex, chunk?: {start: addr, end: addr}}, distinct: u64, writes: bool, total: u64, uses: [{offset: hex, instruction: addr, module?: str, rva?: hex, write: bool, text: str}]}], capped: bool, scan_ms: i64}` |
| `vtable <addr> [count] [--offset <N>] [--table] [--resolve]` | an object's virtual table: slots until the first entry that is not code | `{address: addr, module?: str, rva?: hex, offset: hex, table?: addr, table_module?: str, table_rva?: hex, stop?: count\|not code\|end of read, stop_value?: hex, found?: hex, rows: [{slot: u64, offset: hex, target: addr, module?: str, rva?: hex, body?: addr, body_module?: str, body_rva?: hex}], chain?: Chain}` |
| `region <addr>` | what the memory at an address is: kind, base, size, protection | `{address: addr, module?: str, rva?: hex, section?: str, kind: code\|rodata\|data\|mapped\|private\|reserved\|free, base: addr, size: hex, allocation_base: addr, state: str, type: str, protect: str, allocation_protect: str, chain?: Chain}` |

## Unreal pack

| Command | What it does | `result` |
|---|---|---|
| `find [text]... [--cls <Class>] [--start <N>]` | objects whose name contains text and/or instances of a class; 24 per page | `{rows: [Object], total?: u64, from: u32, next_cursor?: u32}` |
| `census [--cls <Class>] [--match <text>] [--limit <N>] [--start <N>]` | live instances per class, most first; pages of 100 rows | `{filter?: str, classes: u64, instances: u64, ms: i64, rows: [{class: str, ref: ref, count: u32, samples: [ref]}], total: u64, from: u64, next_cursor?: u64, hint?: str}` |
| `census-diff [--cls <Class>] [--match <text>] [--limit <N>] [--start <N>]` | the classes whose count differs from the last census of that --cls | `{filter?: str, classes: {before: u64, after: u64}, instances: {before: u64, after: u64}, ms: i64, rows: [{class: str, ref: ref, before: u32, after: u32, samples: [ref]}], total: u64, from: u64, next_cursor?: u64, hint?: str}` |
| `name-find <text> [--start <N>]` | FName indices whose text contains text (gameplay tags, row names) | `{rows: [ue.Name], from: u32, next_cursor?: u32}` |
| `name <index>...` | FName comparison indices as text (FName #N in dumps) | `{rows: [ue.Name]}` |
| `each <Class> <Field[.Field...]\|[Owner:]Function>... [--seconds <N>]` | a table over the live instances of a class: a value per column (up to 16 field paths, or one function) **Writes or calls.** | `{class: str, class_ref: ref, columns: [str], mode: field\|call, rows: [{ref: ref, values: [value], hex?: bytes}], same_tick: bool, capped: bool, truncated: bool, errors?: [{ref: ref, column: u32, message: str}], seconds?: u32, polls?: u64, changes?: [{ms: i64, tick: u64, ref: ref, before: value, after: value}], poll_error?: str}` |
| `call <object> <[Class:]Function> [hex params \| Name=value]... [--typed]` | call a reflected function with a raw block or Name=value inputs **Writes or calls.** | `{ref: ref, function: str, mode: raw\|typed\|release, block_size: u64, hex: bytes, kept: bool, kept_reason?: str, outputs?: [{name: str, direction: out\|return, value: value, truncated: bool, meta?: ue.FieldInfo}], truncated?: bool}` |
| `release <object> <[Class:]Function> [hex params]...` | one more call (zeroed inputs by default) that drops the kept block **Writes or calls.** | `{ref: ref, function: str, mode: raw\|typed\|release, block_size: u64, hex: bytes, kept: bool, kept_reason?: str, outputs?: [{name: str, direction: out\|return, value: value, truncated: bool, meta?: ue.FieldInfo}], truncated?: bool}` |
| `fields <object\|Class>` | reflected properties by offset; a class or struct lists its own | `{ref: ref, rows: [{name: str, owner: str, type: str, offset: u32, size: u32, struct?: str, bool_mask?: u32, element_type?: str, element_size?: u32, value_type?: str, value_size?: u32, value_offset?: u32, value_struct?: str, stride?: u32, label: str, native?: bool, stale?: bool}], capped: bool, native_refused?: [{line: u64, name: str, owner: str, offset: u32, reason: str}]}` |
| `dump <object\|Class> [--match <text>] [--nonzero] [--own] [--changed [file]]` | fields with their current values | `{ref: ref, rows: [{name: str, raw?: str, offset: u32, native?: bool, stale?: bool, value: value, before?: value, truncated?: bool, value_module?: str, value_rva?: hex, object?: Object, preview?: {total: u64, elements: [{key?: value, value: value, object?: Object, key_object?: Object, value_object?: Object}]}, preview_error?: str, meta?: ue.FieldInfo}], truncated: bool, capped: bool, object_lookup_limited: bool, filtered?: {shown: u64, total: u64}, native_refused?: [{line: u64, name: str, owner: str, offset: u32, reason: str}]}` |
| `label <object\|Class> <offset> <type[n]> <Name>` | name an unreflected member in <console>.native.txt | `{path: str, line: u64, owner: str, offset: u32, type: str, name: str, size: u32, timestamp: u32}` |
| `export-labels <file> [--module <name>] [--kinds <exec,vtable,interface,global>]` | names for a static-analysis tool (JSON lines, one build) | `{path: str, module: str, timestamp: u32, total: u64, globals: u64, exec: u64, vtables: u64, interfaces: u64, interface_lookups: u64, capped: bool}` |
| `entries <object> <[Owner:]Field> [--start <N>]` | one page of an array, set or map: elements, or key and value | `{ref: ref, field: str, kind: array\|set\|map, total: u64, rows: [{key?: value, value: value, key_object?: Object, value_object?: Object}], from: u32, next_cursor?: u32, truncated: bool}` |
| `get <object> <Field[.Field...]>...` | named fields, following object pointers and structs (up to 16 paths) | `{ref: ref, rows: [{value?: value, address?: addr, module?: str, rva?: hex, object?: ref, truncated?: bool, native?: bool, stale?: bool, value_module?: str, value_rva?: hex, type?: str, hex?: bytes, error?: str}], same_tick: bool, tick_accurate?: bool}` |
| `describe <address\|$N\|#index:serial>` | an object's name, class and reusable ref | `{ref: ref, name: str, class: str, class_size?: u32}` |
| `func <object\|Class> <[Owner:]Function>` | a function's reflected metadata and exec entry address | `{ref: ref, function: str, address: addr, flags: u32, exec?: addr, executable: bool, module?: str, rva?: hex, module_error?: str, interface?: {offset: hex, slot?: i64, implementation?: addr, implementation_module?: str, implementation_rva?: hex}}` |
| `script <object\|Class> <[Owner:]Function>` | a Blueprint function's bytecode: the names, functions and properties it uses | `{ref: ref, function: str, size: u32, names: [{offset: u32, count: u32, index: i32, name: str}], functions: [{offset: u32, count: u32, ref: ref, function: str}], properties: [{offset: u32, count: u32, address: addr, property: str}]}` |
| `refs <name\|#index>` | Blueprint functions whose bytecode names a pooled name (EX_NameConst) | `{index: i32, name?: str, rows: [{ref: ref, owner: str, name: str, offset: u32, count: u32}], capped: bool}` |
| `funcs <object\|Class> [text] [--start <N>]` | functions on the class chain | `{ref: ref, rows: [{owner?: str, name: str}], from: u32, next_cursor?: u32}` |
| `params <object\|Class> <[Owner:]Function>` | a function's reflected parameter signature | `{ref: ref, function: str, address: addr, block_size: u32, rows: [{name: str, owner: str, type: str, offset: u32, size: u32, struct?: str, bool_mask?: u32, element_type?: str, element_size?: u32, value_type?: str, value_size?: u32, value_offset?: u32, value_struct?: str, stride?: u32, direction: in\|out\|in ref\|in/out ref\|return, flags: u32}]}` |
| `snap <ref> [file]` | save a baseline for dump --changed (file: across runs) | `{ref: ref, size: u64, path?: str}` |
| `record <ref> <seconds> <file> <field>...` | sample 1..8 named fields for 1..10 seconds into a new file, after a countdown | `{ref: ref, path: str, total: u64}` |

## Named types

Types that several results share, as `help --json` lists them under `types`.

| Type | Definition |
|---|---|
| `Chain` | `{base: addr, hops: [{hop: u64, address: addr, value: addr, module?: str, rva?: hex}], address?: addr, module?: str, rva?: hex, broken?: u64, reason?: str}` |
| `Object` | `{ref: ref, name: str, class: str, class_size?: u32}` |
| `Pin` | `{id: u32, address: addr, bytes: bytes, guarded: bool, state: active\|expired\|object changed\|not writable\|memory differed from --expect\|cleared\|access faulted\|unknown, writes: u32, remaining_ms: u32}` |
| `Trial` | `{id: u32, address: addr, bytes: bytes, original: bytes, owner: addr, object?: addr, state: active\|restored\|conflict\|abandoned\|unknown, changed_tick: u64, remaining_ms: u32, guard?: owner replaced or freed\|object vtable changed\|target unreadable\|unknown}` |
| `WatchRange` | `{address?: addr, offset?: hex, length: u32, type?: str, guarded: bool}` |
| `WatchSample` | `{tick: u64, us: u64, values?: [value], lost?: bool, resumed?: bool, root?: addr}` |
| `WatchSummary` | `{range: u32, element?: u32, samples: u64, first: f64, last: f64, least: f64, greatest: f64, per_second?: f64}` |
| `ue.FieldInfo` | `{name: str, owner: str, type: str, offset: u32, size: u32, struct?: str, bool_mask?: u32, element_type?: str, element_size?: u32, value_type?: str, value_size?: u32, value_offset?: u32, value_struct?: str, stride?: u32}` |
| `ue.Name` | `{index: i32, name?: str}` |

## Envelopes

Every line is an envelope: `command`, `ok`, and `result` or `error`. Errors carry `kind` and
`message`; client errors (`agent`, `timeout`, `attach`, `busy`) also carry `status`, `detail` and
`reason`. A refused `write --expect` adds `found`; a broken pointer chain adds `chain` with
`broken` and `reason`. Any envelope can carry `timing` when the line ended in `--timing`. A
usage error ends with the verb's usage line. A result that does not match its row's type is an
`internal` error, never printed as a result.

## Game values

A `value` holds what the game's data is. Addresses are hex strings, null for a null pointer.
FNames carry `index`, `number` and `name`. Structs expand to `{Name: value}` (`members` with
`--meta`). Arrays and strings show their header, and `dump` adds a `preview` for top-level
containers. Unsupported layouts give a bounded `hex` prefix. A non-finite float is
`{"non_finite": "nan"}`. Check `truncated` on the result and its rows before treating values as
complete.
