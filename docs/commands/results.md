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
| `help [verb] [--json] [--hash]` | the verbs, one line each; help <verb>: its flags, result and notes | `{rows: [{verb: str, origin: str, usage: str, summary: str, writes?: bool, blocks?: bool, aliases?: [str], flags?: [{flag: str, key: str, takes: nothing\|one\|maybe\|some\|rest, type: str, required?: bool, value?: str, default?: str, help?: str}], result?: str, notes?: str, positionals?: [{key: str, name: str, arity: one\|optional\|some\|any\|rest, type: str, default?: str, help?: str}], types?: {<key>: str}, type?: value}], notes?: [str], table_hash?: str, types?: {<key>: {notation: str, type: value}}, origins?: [{origin: str, title: str}]}` |
| `status` | agent pid, tick rate, the packs the host offers and the verb table's hash | `{pid: u32, build: str, version: str, commit: str, replaced_agents: u32, ticks: u64, ticks_per_second: u64, interval_ms: u64, ticking: bool, host_thread: engine\|borrowed\|own\|unset, tick_note?: str, roots?: [str], packs: [str], table_hash: str}` |
| `quit` | end the console | `{quit: bool}` |
| `stats [--all] [--sessions] [--session <id>]` | what this session cost: calls, output and refusals per verb, largest output first | `{session?: str, sessions?: u64, since?: str, totals: {calls: u64, in: u64, out: u64, out_by_caller: {<key>: u64}, refused: u64, digested: u64, saved: u64, ms: f64}, rows: [{verb: str, origin: str, calls: u64, out: u64, largest: u64, ms: f64, batched?: u64, unfiltered?: u64, repeated?: u64, refused?: u64, refused_by?: {<key>: u64}, digested?: u64, saved?: u64}], largest: [{verb: str, flags: [str], out: u64, caller: str, batch?: bool, digest?: u64}], never_called?: [str], session_rows?: [{session: str, since: str, until: str, callers: [str], calls: u64, out: u64, tool_out: u64, digested: u64, saved: u64, refused: u64}]}` |
| `batch { <command>; <command>; ... }` | up to 64 commands in as few host ticks as possible; one envelope each | `{all_ok: bool, same_tick: bool, first_tick: u64, last_tick: u64, spans_ticks: [{index: u64, command: str}], rows: [value], tick_accurate?: bool}` |
| `read <addr> [len] [--type type]` | hex dump of 1..4096 bytes (default 64); --type decodes values | `{address: addr, module?: str, rva?: hex, size: u64, hex: bytes, views?: {i32?: i32, u32?: u32, f32?: value, i64?: i64, u64?: u64, ptr?: addr, ptr_module?: str, ptr_rva?: hex, f64?: value}, values?: [value], layout?: value, text?: str, length?: u64, terminated?: bool, region?: Region, chain?: Chain}` |
| `peek <addr> [count] [--label [file]] [--name <name>]` | what 1..64 qwords hold (default 8): object, module, heap or value | `{address: addr, module?: str, rva?: hex, region?: Region, rows: [{address: addr, module?: str, rva?: hex, label: object\|module\|heap\|value\|unreadable\|..., value?: hex, object?: Object, value_module?: str, value_rva?: hex, f32?: [value], f64?: value, name_hint?: str}], labelled?: {module: str, module_size: u64, timestamp: u32, rva: hex, name: str, kind: user\|global\|exec\|vtable\|interface}, chain?: Chain}` |
| `write <addr> <hex bytes>... [--expect <hex bytes>...]` | write up to 4096 bytes **Writes or calls.** | `{address: addr, module?: str, rva?: hex, size: u64}` |
| `trial <addr> <hex bytes>... --owner <ref> --expect <hex bytes>... [--object <addr>] [--seconds <N>] [--ms <N>]` | one write the agent undoes (default 10 s, up to 60 s) **Writes or calls.** | `{id: u32, address: addr, bytes: bytes, original: bytes, owner: addr, object?: addr, state: active\|restored\|conflict\|abandoned\|unknown, changed_tick: u64, remaining_ms: u32, guard?: owner replaced or freed\|object vtable changed\|target unreadable\|unknown}` |
| `trial-list` | trials with their state | `{rows: [Trial]}` |
| `trial-clear [id]` | end one trial or all, restoring active ones **Writes or calls.** | `{cleared: u64, rows: [Trial]}` |
| `modules` | loaded modules, exe first (also refreshes the cached list) | `{rows: [{name: str, base: addr, size: u64}]}` |
| `search <hex\|u32\|i32\|u64\|ptr\|f32\|f64\|text> <value>... [--wide] [--start <addr>] [--to <addr>] [--module <name>] [--align <N>] [--writable] [--heap] [--paged] [--background] [--max <N>]` | addresses holding a value in resident, cacheable memory | `{rows: [{address: addr, module?: str, rva?: hex, text?: str}], from: addr, next_cursor?: addr}` |
| `mem-snap <addr> <len>` | keep up to 1 MiB of memory for mem-diff (a file beside the cache, across runs) | `{address: addr, module?: str, rva?: hex, length: u64, tick: u64}` |
| `mem-diff <changed\|unchanged\|increased\|decreased> <type> [--keep] [--limit <N>]` | elements of the last mem-snap that changed, stayed, rose or fell since; --keep narrows | `{address: addr, module?: str, rva?: hex, length: u64, type: str, test: changed\|unchanged\|increased\|decreased, compared: u64, matched: u64, tick: u64, rows: [{address: addr, before: value, after: value}], capped: bool, kept?: bool}` |
| `pin <addr> <hex bytes>... [--seconds <N>] [--ms <N>] [--expect <hex bytes>...]` | write the bytes every tick (default 10 s, up to 60 s) **Writes or calls.** | `{id: u32, address: addr, bytes: bytes, guarded: bool, state: active\|expired\|object changed\|not writable\|memory differed from --expect\|cleared\|access faulted\|unknown, writes: u32, remaining_ms: u32}` |
| `pin-list` | pins with state and write counts | `{rows: [Pin]}` |
| `pin-clear [id]` | clear one pin or all | `{cleared: u32}` |
| `watch <seconds\|Nms> <addr:len\|addr:type[n]>... [--then call line] [--all]` | sample up to 8 ranges every tick (up to 30 s) and return the rows **Writes or calls.** | `{capacity: u32, complete: bool, stopped?: deadline\|stopped\|buffer full\|object replaced, ranges: [WatchRange], then?: {ok: bool, hex?: bytes, message?: str}, rows: [WatchSample], samples?: u64, dropped?: u64, summary?: [WatchSummary]}` |
| `watch-start <duration> <addr\|offset:len\|type[n]>... [--every <duration>] [--follow <root>]` | a capture the agent keeps (up to 60 min with --every); returns at once | `{capacity: u32, ms: u32, interval_ms: u32, root?: str, ranges: [WatchRange]}` |
| `watch-read [--all]` | the capture so far: its first sample, each change and its last | `{running: bool, stopped?: deadline\|stopped\|buffer full\|object replaced, capacity: u32, ms: u32, interval_ms: u32, root?: str, ranges: [WatchRange], rows: [WatchSample], samples?: u64, dropped?: u64, gaps?: [{from_us: u64, to_us?: u64}], summary?: [WatchSummary]}` |
| `watch-stop [--all]` | end the capture and return it: its first sample, each change and its last | `{running: bool, stopped?: deadline\|stopped\|buffer full\|object replaced, capacity: u32, ms: u32, interval_ms: u32, root?: str, ranges: [WatchRange], rows: [WatchSample], samples?: u64, dropped?: u64, gaps?: [{from_us: u64, to_us?: u64}], summary?: [WatchSummary]}` |
| `sig-check [pattern]... [--file <patterns file>] [--module <name>] [--expect <addr>] [--offset <N>] [--resolve <next,field[,size]>]` | is a byte signature unique in a module, and does it land where expected; --file checks a file of them | `{pattern?: str, list?: str, module?: str, unique?: bool, total?: u64, rows?: [{address: addr, module?: str, rva?: hex, reference?: bool, diverges_at?: u64, resolved?: addr, resolved_module?: str, resolved_rva?: hex}], length?: u64, fixed_bytes?: u64, anchor?: {offset: u64, length: u64, hits: u64}, warnings?: [{offset: u64, size: u64, kind: call\|jump\|branch\|rip\|absolute, target: addr, target_module?: str, target_rva?: hex}], decoded?: u64, unique_at_length?: u64, resolve?: {next: u64, field: u64, size: u64}, expect?: addr, expect_module?: str, expect_rva?: hex, offset?: u64, resolves?: bool, mismatches?: [{offset: u64, wanted: hex, found: hex}], mismatch_error?: str, file?: str, winner?: u64, searches?: u64, lines?: [{line: u64, ok: bool, result?: {pattern: str, list: str, module: str, unique: bool, total: u64, rows: [{address: addr, module?: str, rva?: hex, reference?: bool, diverges_at?: u64, resolved?: addr, resolved_module?: str, resolved_rva?: hex}], length: u64, fixed_bytes: u64, anchor: {offset: u64, length: u64, hits: u64}, warnings: [{offset: u64, size: u64, kind: call\|jump\|branch\|rip\|absolute, target: addr, target_module?: str, target_rva?: hex}], decoded?: u64, unique_at_length?: u64, resolve?: {next: u64, field: u64, size: u64}, expect?: addr, expect_module?: str, expect_rva?: hex, offset?: u64, resolves?: bool, mismatches?: [{offset: u64, wanted: hex, found: hex}], mismatch_error?: str}, error?: str}]}` |
| `sig-bytes <addr> [len]` | live bytes (default 64, up to 96; longer is cut: capped) in both pattern forms, address fields wildcarded | `{address: addr, module?: str, rva?: hex, length: u64, capped?: bool, pattern: str, list: str, suggested: {pattern: str, list: str, fixed_bytes: u64, fields: [{offset: u64, size: u64, kind: call\|jump\|branch\|rip\|absolute, target: addr, target_module?: str, target_rva?: hex}], decoded?: u64}}` |
| `labels-sign [file] [--module <name>] [--again]` | a byte signature on each of this build's user and global names, so they survive a patch | `{path: str, module: str, timestamp: u32, signed: u64, unsigned: u64, skipped: u64, failed_before: u64, searches: u64, rows: [{name: str, kind: str, rva: hex, status: signed\|unsigned, length?: u64, site?: hex, reason?: str}]}` |
| `labels-port [file] [--module <name>]` | find other builds' signed names in the running build and add them to the file | `{path: str, module: str, timestamp: u32, ported: u64, missing: u64, ambiguous: u64, present: u64, unsigned: u64, searches: u64, rows: [{name: str, kind: str, from: u32, status: ported\|missing\|ambiguous, rva?: hex, matches?: u64, reason?: str}]}` |
| `disasm <addr> [count] [--function] [--resume] [--save <file>] [--callees] [--labels <file>] [--follow]` | instructions decoded from live bytes: 1..1024 (default 24; 8 bytes each, at least 512), or a whole function | `{address: addr, module?: str, rva?: hex, size: u64, stop: str, next?: addr, rows: [{address: addr, module?: str, rva?: hex, bytes?: bytes, text: str, target?: addr, target_module?: str, target_rva?: hex, string?: str, wide?: bool, import?: str, fields?: [{offset: u64, size: u64, kind: str}], ends?: bool, value?: f64, value_type?: str, value_hex?: bytes}], strings?: [{text: str, wide?: bool, at: [addr]}], saved?: str, saved_rows?: u64, functions?: [{start: addr, end: addr, rows: u64}], skipped?: [addr], chunks?: [{start: addr, end: addr, rows: u64}], piece?: {start: addr, end: addr}, function?: {start: addr, end: addr, module?: str, rva?: hex, chunk?: {start: addr, end: addr}}, end?: {address: addr, offset: u64, reason: str}, followed?: [{from: addr, to: addr, to_module?: str, to_rva?: hex}], chain?: Chain}` |
| `xrefs <addr> [--module <name>] [--all]` | what points at an address: rel32 fields and stored pointers, each with its function | `{address: addr, module?: str, rva?: hex, scanned: str, kinds: {<key>: u64}, rows: [{kind: call\|jmp\|jcc\|lea\|load\|store\|rip\|pointer\|unknown, field: addr, instruction?: addr, module?: str, rva?: hex, function?: {start: addr, end: addr, module?: str, rva?: hex, chunk?: {start: addr, end: addr}}, unlisted?: bool}], capped: bool, scan_ms: i64}` |
| `touches <addr> <offset>... [--range <N>] [--reads]` | functions near an address whose [reg + disp] operands use the offsets (up to 8) | `{address: addr, module?: str, rva?: hex, offsets: [hex], range: u64, scanned: {from: addr, to: addr, functions: u64, unlisted: u64, bytes: u64}, total: u64, rows: [{function: {start: addr, end: addr, module?: str, rva?: hex, chunk?: {start: addr, end: addr}}, unlisted?: bool, distinct: u64, writes: bool, total: u64, uses: [{offset: hex, instruction: addr, module?: str, rva?: hex, write: bool, text: str}]}], capped: bool, scan_ms: i64}` |
| `vtable <addr> [count] [--offset <N>] [--table] [--resolve] [--rtti]` | an object's virtual table: slots until the first entry that is not code | `{address: addr, module?: str, rva?: hex, offset: hex, table?: addr, table_module?: str, table_rva?: hex, stop?: count\|not code\|end of read, stop_value?: hex, found?: hex, rows: [{slot: u64, offset: hex, target: addr, module?: str, rva?: hex, body?: addr, body_module?: str, body_rva?: hex}], rtti?: {locator: addr, name: str, decorated: str, complete: addr, complete_offset: hex, bases: [{name: str, decorated: str}], capped: bool}, rtti_error?: str, chain?: Chain}` |

## Unreal pack

| Command | What it does | `result` |
|---|---|---|
| `find [text]... [--cls <Class>] [--live] [--outer <object\|text>] [--start <N>] [--limit <N>]` | objects whose name contains text and/or instances of a class; 24 per page, or --limit N | `{rows: [Object], total?: u64, from: u32, next_cursor?: u32}` |
| `census [--cls <Class>] [--outer <object\|text>] [--match <text>] [--limit <N>] [--start <N>] [--diff]` | live instances per class, most first; pages of 100 rows; --diff: only the counts that changed | `{filter?: str, classes: u64, instances: u64, before?: {classes: u64, instances: u64}, ms: i64, rows: [{class: str, ref: ref, count: u32, before?: u32, samples: [ref]}], total: u64, from: u64, next_cursor?: u64, hint?: str}` |
| `name-find <text> [--start <N>]` | FName indices whose text contains text (gameplay tags, row names) | `{rows: [ue.Name], from: u32, next_cursor?: u32}` |
| `name <index>...` | FName comparison indices as text (FName #N in dumps) | `{rows: [ue.Name]}` |
| `each <Class> <Field[.Field...]\|[Owner:]Function>... [--seconds <N>] [--where <Field~text>] [--outer <object\|text>] [--limit <N>] [--nonzero] [--elements <N>]` | a table over the live instances of a class: a value per column (up to 16 field paths, or one function) **Writes or calls.** | `{class: str, class_ref: ref, columns: [str], mode: field\|call, rows: [{ref: ref, name?: str, values: [value], hex?: bytes}], same_tick: bool, capped: bool, truncated: bool, errors?: [{ref: ref, column: u32, message: str}], seconds?: u32, polls?: u64, changes?: [{ms: i64, tick: u64, ref: ref, before: value, after: value}], poll_error?: str, matched?: u64, omitted?: u64, unnamed?: u32}` |
| `call <object> <[Class:]Function> [hex params \| Name=value]... [--typed] [--dry] [--fields <a,b.c>] [--start <N>] [--limit <N>] [--nonzero]` | call a reflected function with a raw block or Name=value inputs **Writes or calls.** | `{ref: ref, function: str, mode: raw\|typed\|release\|dry, block_size: u64, hex: bytes, kept: bool, kept_reason?: str, outputs?: [{name: str, direction: out\|return, value: value, truncated: bool, meta?: ue.FieldInfo}], truncated?: bool, exec?: addr, pure?: bool, unnamed?: u32}` |
| `call-pure <object> <[Class:]Function> [hex params \| Name=value]... [--typed] [--dry]` | call a const or pure function that keeps nothing (read-only); as call takes | `{ref: ref, function: str, mode: raw\|typed\|release\|dry, block_size: u64, hex: bytes, kept: bool, kept_reason?: str, outputs?: [{name: str, direction: out\|return, value: value, truncated: bool, meta?: ue.FieldInfo}], truncated?: bool, exec?: addr, pure?: bool, unnamed?: u32}` |
| `release <object> <[Class:]Function> [hex params]...` | one more call (zeroed inputs by default) that drops the kept block **Writes or calls.** | `{ref: ref, function: str, mode: raw\|typed\|release\|dry, block_size: u64, hex: bytes, kept: bool, kept_reason?: str, outputs?: [{name: str, direction: out\|return, value: value, truncated: bool, meta?: ue.FieldInfo}], truncated?: bool, exec?: addr, pure?: bool, unnamed?: u32}` |
| `fields <object\|Class> [--match <text>]` | reflected properties by offset; a class or struct lists its own | `{ref: ref, rows: [{name: str, owner: str, type: str, offset: hex, size: u32, struct?: str, bool_mask?: u32, element_type?: str, element_size?: u32, value_type?: str, value_size?: u32, value_offset?: hex, value_struct?: str, stride?: u32, label: str, native?: bool, stale?: bool}], capped: bool, native_refused?: [{line: u64, name: str, owner: str, offset: hex, reason: str}]}` |
| `dump <object\|Class> [--match <text>] [--nonzero] [--own] [--changed [file]] [--elements <N>]` | fields with their current values | `{ref: ref, rows: [{name: str, raw?: str, offset: hex, native?: bool, stale?: bool, value: value, before?: value, truncated?: bool, value_module?: str, value_rva?: hex, object?: Object, preview?: {total: u64, elements: [{key?: value, value: value, object?: Object, key_object?: Object, value_object?: Object}]}, preview_error?: str, meta?: ue.FieldInfo}], truncated: bool, capped: bool, filtered?: {shown: u64, total: u64}, native_refused?: [{line: u64, name: str, owner: str, offset: hex, reason: str}], tick_accurate?: bool, unnamed?: u32}` |
| `label <object\|Class> <offset> <type[n]> <Name>` | name an unreflected member in <console>.native.txt | `{path: str, line: u64, owner: str, offset: hex, type: str, name: str, size: u32, timestamp: u32}` |
| `export-labels [file] [--module <name>] [--kinds <exec,vtable,interface,global>]` | names for a static-analysis tool (JSON lines, one build) | `{path: str, module: str, timestamp: u32, total: u64, globals: u64, user: u64, exec: u64, vtables: u64, interfaces: u64, interface_lookups: u64, other_builds: u64, capped: bool}` |
| `layout-export <file> <Class>...` | the reflected fields classes declare, into a file (JSON lines) for layout-check | `{path: str, classes: u64, fields: u64, missing?: [str]}` |
| `layout-check <file>` | a file of expected fields against the live layout; only the mismatches | `{path: str, classes: u64, checked: u64, mismatched: u64, ok: bool, rows: [{owner: str, field: str, problem: no class\|no field\|offset\|size\|type, expected: {offset: hex, size: u32, type: str}, found?: {offset: hex, size: u32, type: str}}], capped: bool}` |
| `entries <object> <[Owner:]Field> [--start <N>]` | one page of an array, set or map: elements, or key and value | `{ref: ref, field: str, kind: array\|set\|map, total: u64, rows: [{key?: value, value: value, key_object?: Object, value_object?: Object}], from: u32, next_cursor?: u32, truncated: bool, unnamed?: u32, tick_accurate?: bool}` |
| `rows <object> [--match <text>] [--fields <a,b.c>] [--start <N>] [--limit <N>] [--nonzero]` | a data table's rows: each row's name and its RowStruct members decoded | `{ref: ref, row_struct: str, row_struct_ref: ref, total: u64, rows: [{name: str, address: addr, value: value}], from: u32, next_cursor?: u32, truncated: bool, unnamed?: u32}` |
| `get <object> <Field[.Field...]>... [--nonzero] [--elements <N>]` | named fields, following object pointers and structs (up to 16 paths) | `{ref: ref, rows: [{value?: value, address?: addr, module?: str, rva?: hex, object?: ref, truncated?: bool, native?: bool, stale?: bool, value_module?: str, value_rva?: hex, type?: str, hex?: bytes, error?: str}], same_tick: bool, tick_accurate?: bool, unnamed?: u32}` |
| `trace <x,y,z\|object[@Socket]> <x,y,z\|object[@Socket]> [--channel <visibility\|camera\|N>] [--simple] [--ignore <actor>]` | the first thing a line hits: actor, component, physical material, distance (read-only) | `{start: [f64], end: [f64], channel: str, complex: bool, ignored?: ref, hit: bool, initial_overlap?: bool, distance?: f64, impact?: [f64], normal?: [f64], actor?: Object, component?: Object, material?: Object, bone?: str, face?: i32, item?: i32}` |
| `calls-start <object\|Class> <[Owner:]Function> <duration> [--object <ref>] [--dry]` | record a reflected function's calls, with their arguments, for a while **Writes or calls.** | `{function: str, slot: u32, exec: addr, thunk: addr, block_size: u32, object?: addr, seconds: f64, dry: bool, note?: str}` |
| `calls-read [--slot <N>] [--start <N>]` | a recording's calls so far, decoded by the function's parameters | `{function: str, state: recording\|expired\|finished\|replaced, slot: u32, calls: u64, events: u64, vm: u64, dropped: u64, rows: [{sequence: u64, ms: f64, thread: u32, object: ref, object_name?: str, object_class?: str, via: event\|vm, values?: [{name: str, direction: in\|out\|return, value: value, truncated: bool}]}], next_cursor?: u64, unnamed?: u32}` |
| `calls-stop [--slot <N>]` | end a recording now (restores the pointer) and return its calls | `{function: str, state: recording\|expired\|finished\|replaced, slot: u32, calls: u64, events: u64, vm: u64, dropped: u64, rows: [{sequence: u64, ms: f64, thread: u32, object: ref, object_name?: str, object_class?: str, via: event\|vm, values?: [{name: str, direction: in\|out\|return, value: value, truncated: bool}]}], next_cursor?: u64, unnamed?: u32}` |
| `describe <address\|$N\|#index:serial>` | an object's name, class and reusable ref | `{ref: ref, name: str, class: str, class_size?: u32, outer?: {name: str, ref: ref}}` |
| `func <object\|Class> <[Owner:]Function>` | a function's reflected metadata, exec entry address and parameter signature | `{ref: ref, function: str, address: addr, flags: u32, exec?: addr, executable: bool, module?: str, rva?: hex, module_error?: str, interface?: {offset: hex, slot?: i64, implementation?: addr, implementation_module?: str, implementation_rva?: hex}, block_size?: u32, params?: [{name: str, owner: str, type: str, offset: hex, size: u32, struct?: str, bool_mask?: u32, element_type?: str, element_size?: u32, value_type?: str, value_size?: u32, value_offset?: hex, value_struct?: str, stride?: u32, direction: in\|out\|in ref\|in/out ref\|return, flags: u32}], params_error?: str}` |
| `script <object\|Class> <[Owner:]Function>` | a Blueprint function's bytecode: the names, functions and properties it uses | `{ref: ref, function: str, size: u32, names: [{offset: hex, count: u32, index: i32, name: str}], functions: [{offset: hex, count: u32, ref: ref, function: str}], properties: [{offset: hex, count: u32, address: addr, property: str}]}` |
| `enum <object\|Enum>` | an enum's names and values, in order | `{ref: ref, name: str, rows: [{name: str, value: i64}]}` |
| `game-thread [on\|off\|status]` | the host's game-thread tick through the game viewport's Draw: status, on, off **Writes or calls.** | `{mode: off\|waiting\|conflict\|on\|stopped, text: str, thread?: u32, viewport?: addr, table?: addr, draw?: addr, draw_module?: str, draw_rva?: hex}` |
| `refs <name\|#index>` | Blueprint functions whose bytecode names a pooled name (EX_NameConst) | `{index: i32, name?: str, rows: [{ref: ref, owner: str, name: str, offset: hex, count: u32}], capped: bool}` |
| `funcs <object\|Class> [text] [--start <N>]` | functions on the class chain, then on the native interfaces it implements | `{ref: ref, rows: [{owner?: str, name: str, via?: str}], from: u32, next_cursor?: u32}` |
| `snap <ref> [file]` | save a baseline for dump --changed (file: across runs) | `{ref: ref, size: u64, path?: str}` |
| `record <ref> <seconds> <file> <field>...` | sample 1..8 named fields for 1..10 seconds into a new file, after a countdown | `{ref: ref, path: str, total: u64}` |

## Named types

Types that several results share, as `help --json` lists them under `types`.

| Type | Definition |
|---|---|
| `Chain` | `{base: addr, hops: [{hop: u64, address: addr, value: addr, module?: str, rva?: hex}], address?: addr, module?: str, rva?: hex, broken?: u64, reason?: str}` |
| `Object` | `{ref: ref, name: str, class: str, class_size?: u32, outer?: {name: str, ref: ref}}` |
| `Pin` | `{id: u32, address: addr, bytes: bytes, guarded: bool, state: active\|expired\|object changed\|not writable\|memory differed from --expect\|cleared\|access faulted\|unknown, writes: u32, remaining_ms: u32}` |
| `Region` | `{kind: code\|rodata\|data\|mapped\|private\|reserved\|free, base: addr, size: hex, protect: str}` |
| `Trial` | `{id: u32, address: addr, bytes: bytes, original: bytes, owner: addr, object?: addr, state: active\|restored\|conflict\|abandoned\|unknown, changed_tick: u64, remaining_ms: u32, guard?: owner replaced or freed\|object vtable changed\|target unreadable\|unknown}` |
| `WatchRange` | `{address?: addr, offset?: hex, length: u32, type?: str, guarded: bool}` |
| `WatchSample` | `{tick: u64, us: u64, values?: [value], lost?: bool, resumed?: bool, root?: addr}` |
| `WatchSummary` | `{range: u32, element?: u32, samples: u64, changes: u64, first: f64, last: f64, least: f64, greatest: f64, per_second?: f64}` |
| `ue.FieldInfo` | `{name: str, owner: str, type: str, offset: hex, size: u32, struct?: str, bool_mask?: u32, element_type?: str, element_size?: u32, value_type?: str, value_size?: u32, value_offset?: hex, value_struct?: str, stride?: u32}` |
| `ue.Name` | `{index: i32, name?: str}` |

## Envelopes

Every line is an envelope: `command`, `ok`, and `result` or `error`. Errors carry `kind` and
`message`; client errors (`agent`, `timeout`, `attach`, `busy`) also carry `status`, `detail` and
`reason`. A refused `write --expect` adds `found`; a broken pointer chain adds `chain` with
`broken` and `reason`. Any envelope can carry `timing` when the line ended in `--timing`. A
result past the line's output budget is not printed: `digest` stands in for `result`, with `ok`
true: `{chars, budget, rows?: {path, count}, largest: [{path, chars}], narrow?: [flag], next?: str,
file?: str}`. `next` is the line narrowed (literal refs; a JSON call's text for a JSON call);
`file` holds the whole envelope of a line that must not run again. A
usage error ends with the verb's usage line. A result that does not match its row's type is an
`internal` error, never printed as a result.

## Game values

A `value` holds what the game's data is. Addresses are hex strings, null for a null pointer.
An FName is its text (`Name_N` for an instance, `#index` when the pool has none), and a struct
holding only a name (a gameplay tag) is that name; `--meta` gives `index`, `number` and `name`.
Structs expand to `{Name: value}` (`members` with `--meta`). Arrays and strings show their header, and `dump` adds a `preview` for top-level
containers. Unsupported layouts give a bounded `hex` prefix. A non-finite float is
`{"non_finite": "nan"}`. Check `truncated` on the result and its rows before treating values as
complete.
