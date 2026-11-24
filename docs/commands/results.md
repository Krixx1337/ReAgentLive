# Result shapes

<!-- Generated from `help` with `json` (tests/table: `reagentlive-table --write docs/commands/results.md`). Do not edit: change the verb's row or its result struct. -->

What each verb takes and the keys of its `result`, as the console itself reports them:
`help` lists the verbs, `help` with `verb` shows one (args, result, notes, an example call) and
`help` with `json` prints this whole table for programs. `status` carries `table_hash`, which
changes whenever a row does. The Command column lists each verb's arg keys: `?` marks an optional
one, `[]` a list, `{...}` an object's keys (`view` is the shared output-shaping object).

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
- A page carries `from` (where it began) and `next_cursor` (pass it as `view.start`; absent when done).
- `capped`: the rows were cut at a hard limit and there is no cursor. `truncated`: a value in the
  result is incomplete.
- `module` and `rva` stand beside an address inside a loaded module, so it opens at the same RVA
  in a static-analysis tool; other prefixes (`value_`, `target_`) locate the address in that key.
- A key that does not apply is omitted; results hold no nulls outside game values.
- An arg may add optional keys and never changes a key's type.
- **Writes or calls.** marks a verb that writes the target's memory or calls into it; every other verb
  only reads (`help` with `json` carries it as `writes`).
- **Pack** is the workflow a verb serves (`help` with `json` lists them under `packs`): `explore`, `native`,
  `observe` or `modify`. A console's `--packs` and the MCP adapter's packs pick by it; every verb marked
  writes or calls is in `modify`.

## Core

| Command | Pack | What it does | `result` |
|---|---|---|---|
| `help {verb?, json?, hash?}` | explore | the verbs; with verb: its args, result, notes and example | `{rows: [{verb: str, origin: str, pack: explore\|native\|observe\|modify, summary: str, writes?: bool, blocks?: bool, args?: [value], result?: str, notes?: str, example?: value, types?: {<key>: str}, type?: value}], notes?: [str], table_hash?: str, types?: {<key>: {notation: str, type: value}}, origins?: [{origin: str, title: str}], packs?: [{pack: str, job: str}]}` |
| `status` | explore | agent pid, exe, tick rate, host packs, table_hash | `{pid: u32, exe: str, build: str, version: str, commit: str, replaced_agents: u32, ticks: u64, ticks_per_second: u64, interval_ms: u64, ticking: bool, host_thread: engine\|borrowed\|own\|unset, tick_note?: str, roots?: [str], packs: [str], game_thread?: value, tool_packs: [str], table_hash: str}` |
| `quit` | explore | end the console | `{quit: bool}` |
| `usage {report?, session?, advice?, baseline?, save?, only[]?}` | explore | this session's cost: calls, output and refusals per verb, largest first | `{session?: str, sessions?: u64, since?: str, totals?: {calls: u64, batched: u64, in: u64, out: u64, out_by_caller: {<key>: u64}, calls_by_pack: {<key>: u64}, refused: u64, digested: u64, saved: u64, ms: f64, slot_ms: f64, slot_ms_max: f64}, rows?: [{verb: str, origin: str, calls: u64, out: u64, largest: u64, ms: f64, slot_ms?: f64, batched?: u64, unfiltered?: u64, repeated?: u64, same_args?: u64, refused?: u64, refused_by?: {<key>: u64}, digested?: u64, saved?: u64, followed?: u64, raised?: u64}], largest?: [{verb: str, keys: [str], out: u64, caller: str, batch?: bool, digest?: u64}], never_called?: [str], session_rows?: [{session: str, since: str, until: str, callers: [str], calls: u64, out: u64, tool_out: u64, digested: u64, saved: u64, refused: u64}], context?: {chars: u64, limit: u64, over: bool}, refusals?: [{verb: str, kind: str, key?: str, count: u64}], advice?: [{verb: str, calls: u64, digested: u64, unfiltered: u64, followed: u64, raised: u64, limit?: {key: str, now: u64, proposed: u64}, hint: str}], saved?: str, compare?: {baseline: str, checked: u64, worse: [{what: str, verb?: str, before: f64, after: f64}], better: [{what: str, verb?: str, before: f64, after: f64}]}}` |
| `batch {calls}` | explore | up to 64 calls in as few host ticks as possible; one envelope each | `{all_ok: bool, same_tick: bool, first_tick: u64, last_tick: u64, spans_ticks: [{index: u64, command: str}], rows: [value], tick_accurate?: bool}` |
| `run {plan, out?, background?}` | explore | a plan file's steps, each its own call: NDJSON lines, then a report | `{file?: str, lines: u64, rows?: [value], report: RunReport}` |
| `read {addr, len?, type?}` | explore | hex dump of bytes; type decodes values | `{address: addr, module?: str, rva?: hex, size: u64, hex: bytes, views?: {i32?: i32, u32?: u32, f32?: value, i64?: i64, u64?: u64, ptr?: addr, ptr_module?: str, ptr_rva?: hex, f64?: value}, values?: [value], layout?: value, text?: str, length?: u64, terminated?: bool, region?: Region, chain?: Chain}` |
| `peek {addr, count?, label?, file?, name?}` | explore | what 1..64 qwords hold: object, module, heap or value | `{address: addr, module?: str, rva?: hex, region?: Region, rows: [{address: addr, module?: str, rva?: hex, label: object\|module\|heap\|value\|unreadable\|..., value?: hex, object?: Object, value_module?: str, value_rva?: hex, f32?: [value], f64?: value, name_hint?: str}], labelled?: {module: str, module_size: u64, timestamp: u32, rva: hex, name: str, kind: user\|global\|exec\|vtable\|interface}, chain?: Chain}` |
| `write {addr?, data?, expect?, undo{after, owner, object?}?, hold?, clear?}` | modify | write up to 256 bytes, optionally timed (undo, hold); clear ends timed writes **Writes or calls.** | `{address?: addr, module?: str, rva?: hex, size?: u64, timed?: TimedWrite, cleared?: [TimedWrite]}` |
| `timed-writes` | modify | timed writes (undo, hold) and their state | `{rows: [TimedWrite]}` |
| `modules` | explore | loaded modules, exe first (also refreshes the cached list) | `{rows: [{name: str, base: addr, size: u64}]}` |
| `search {type, value, scope{module?, start?, to?, writable?, heap?, paged?}?, align?, wait?, max?}` | native | addresses holding a value in resident memory, as a job the agent runs | `{rows: [{address: addr, module?: str, rva?: hex, text?: str}], job: u32, state: running\|done\|stopped, from: addr, to: addr, scanned: f64, next_cursor?: addr}` |
| `search-job {job, stop?}` | native | a search job's state and hits so far; stop ends it first | `{rows: [{address: addr, module?: str, rva?: hex, text?: str}], job: u32, state: running\|done\|stopped, from: addr, to: addr, scanned: f64, next_cursor?: addr}` |
| `mem-snap {addr, len}` | observe | keep up to 1 MiB for mem-diff | `{address: addr, module?: str, rva?: hex, length: u64, tick: u64}` |
| `mem-diff {compare, type, keep?, view{limit?}?}` | observe | elements of the last mem-snap that changed, stayed, rose or fell; keep narrows | `{address: addr, module?: str, rva?: hex, length: u64, type: str, compare: changed\|unchanged\|increased\|decreased, compared: u64, matched: u64, tick: u64, rows: [{address: addr, before: value, after: value}], capped: bool, kept?: bool}` |
| `watch-start {duration, ranges[], every?, follow?, file?}` | observe | a capture the agent keeps (up to 60 min); returns at once | `{capacity: u32, ms: u32, interval_ms: u32, root?: str, ranges: [WatchRange], file?: str}` |
| `watch-read {all?, stop?}` | observe | the capture so far: first sample, changes, last; stop ends it first | `{running: bool, stopped?: deadline\|stopped\|buffer full\|object replaced, capacity: u32, ms: u32, interval_ms: u32, root?: str, ranges: [WatchRange], rows: [WatchSample], samples?: u64, dropped?: u64, gaps?: [{from_us: u64, to_us?: u64}], summary?: [WatchSummary], file?: str}` |
| `sig-check {pattern?, file?, module?, expect?, offset?, resolve?}` | native | is a byte signature unique in a module and where it lands; file checks a file of them | `{pattern?: str, list?: str, module?: str, unique?: bool, total?: u64, rows?: [{address: addr, module?: str, rva?: hex, reference?: bool, diverges_at?: u64, resolved?: addr, resolved_module?: str, resolved_rva?: hex}], length?: u64, fixed_bytes?: u64, anchor?: {offset: u64, length: u64, hits: u64}, warnings?: [{offset: u64, size: u64, kind: call\|jump\|branch\|rip\|absolute, target: addr, target_module?: str, target_rva?: hex}], decoded?: u64, unique_at_length?: u64, resolve?: {next: u64, field: u64, size: u64}, expect?: addr, expect_module?: str, expect_rva?: hex, offset?: u64, resolves?: bool, mismatches?: [{offset: u64, wanted: hex, found: hex}], mismatch_error?: str, file?: str, winner?: u64, searches?: u64, lines?: [{line: u64, ok: bool, result?: {pattern: str, list: str, module: str, unique: bool, total: u64, rows: [{address: addr, module?: str, rva?: hex, reference?: bool, diverges_at?: u64, resolved?: addr, resolved_module?: str, resolved_rva?: hex}], length: u64, fixed_bytes: u64, anchor: {offset: u64, length: u64, hits: u64}, warnings: [{offset: u64, size: u64, kind: call\|jump\|branch\|rip\|absolute, target: addr, target_module?: str, target_rva?: hex}], decoded?: u64, unique_at_length?: u64, resolve?: {next: u64, field: u64, size: u64}, expect?: addr, expect_module?: str, expect_rva?: hex, offset?: u64, resolves?: bool, mismatches?: [{offset: u64, wanted: hex, found: hex}], mismatch_error?: str}, error?: str}]}` |
| `sig-bytes {addr, len?}` | native | live bytes in both pattern forms, address fields wildcarded (up to 96; longer is capped) | `{address: addr, module?: str, rva?: hex, length: u64, capped?: bool, pattern: str, list: str, suggested: {pattern: str, list: str, fixed_bytes: u64, fields: [{offset: u64, size: u64, kind: call\|jump\|branch\|rip\|absolute, target: addr, target_module?: str, target_rva?: hex}], decoded?: u64}}` |
| `labels-sign {file?, module?, retry?}` | native | sign each user and global name of this build so it survives a patch | `{path: str, module: str, timestamp: u32, signed: u64, unsigned: u64, skipped: u64, failed_before: u64, searches: u64, rows: [{name: str, kind: str, rva: hex, status: signed\|unsigned, length?: u64, site?: hex, reason?: str}]}` |
| `labels-port {file?, module?}` | native | find other builds' signed names in the running build and add them to the file | `{path: str, module: str, timestamp: u32, ported: u64, missing: u64, ambiguous: u64, present: u64, unsigned: u64, searches: u64, rows: [{name: str, kind: str, from: u32, status: ported\|missing\|ambiguous, rva?: hex, matches?: u64, reason?: str}]}` |
| `labels-check {file?, module?, status[]?, view{limit?}?}` | native | re-verify this build's signed names: does each sig still land | `{path: str, module: str, timestamp: u32, ok: u64, moved: u64, missing: u64, ambiguous: u64, invalid: u64, unsigned: u64, searches: u64, rows: [{name: str, kind: str, rva: hex, status: ok\|moved\|missing\|ambiguous\|invalid, found?: hex, matches?: u64, reason?: str}], matched?: u64, capped: bool, scan_ms: i64}` |
| `disasm {addr, count?, function?, resume?, save{file, callees?, labels?}?, follow?}` | native | instructions from live bytes (default 24), or a whole function | `{address: addr, module?: str, rva?: hex, size: u64, stop: str, next?: addr, rows?: [{address: addr, module?: str, rva?: hex, bytes?: bytes, text: str, target?: addr, target_module?: str, target_rva?: hex, string?: str, wide?: bool, import?: str, fields?: [{offset: u64, size: u64, kind: str}], ends?: bool, value?: f64, value_type?: str, value_hex?: bytes}], strings?: [{text: str, wide?: bool, at: [addr]}], saved?: str, saved_rows?: u64, functions?: [{start: addr, end: addr, rows: u64, depth?: u32}], skipped?: [{address: addr, why: str}], chunks?: [{start: addr, end: addr, rows: u64, depth?: u32}], tables?: [{jump: addr, table: addr, targets: u32}], jumpouts_left?: [addr], capped?: bool, layouts?: {classes: [str], types: u64}, piece?: {start: addr, end: addr}, function?: {start: addr, end: addr, module?: str, rva?: hex, chunk?: {start: addr, end: addr}}, end?: {address: addr, offset: u64, reason: str}, followed?: [{from: addr, to: addr, to_module?: str, to_rva?: hex}], chain?: Chain}` |
| `xrefs {addr, module?, all?}` | native | what points at an address: rel32 fields and stored pointers, each with its function | `{address: addr, module?: str, rva?: hex, scanned: str, kinds: {<key>: u64}, rows: [{kind: call\|jmp\|jcc\|lea\|load\|store\|rip\|pointer\|unknown, field: addr, instruction?: addr, module?: str, rva?: hex, function?: {start: addr, end: addr, module?: str, rva?: hex, chunk?: {start: addr, end: addr}}, unlisted?: bool}], capped: bool, scan_ms: i64}` |
| `touches {addr, offsets[], range?, access?, bases[]?}` | native | functions near an address whose [reg + disp] operands use the offsets (up to 8) | `{address: addr, module?: str, rva?: hex, offsets: [hex], range: u64, scanned: {from: addr, to: addr, functions: u64, unlisted: u64, bytes: u64}, total: u64, rows: [{function: {start: addr, end: addr, module?: str, rva?: hex, chunk?: {start: addr, end: addr}}, unlisted?: bool, distinct: u64, writes: bool, total: u64, uses: [{offset: hex, instruction: addr, module?: str, rva?: hex, write: bool, text: str}]}], capped: bool, scan_ms: i64}` |
| `disp-scan {offsets[], access?, width?, imm?, bases[]?, same_base?, view{start?, limit?}?}` | native | instructions in the main module with a [reg + disp] operand at these offsets, paged | `{disps: [hex], access: str, module: str, from: addr, to: addr, scanned: f64, total: u64, rows: [{address: addr, rva: hex, disp: hex, base: str, access: write\|read\|both\|address, width: u64, text: str, function?: hex}], functions?: [{function: hex, base: str, hits: u64}], capped: bool, next_cursor?: addr, scan_ms: i64, read_ms: i64}` |
| `vtable {addr, offset?, slots[]?, parts?, resolve?, rtti?, view{limit?}?}` | native | an object's virtual table: slots until the first entry that is not code | `{address: addr, module?: str, rva?: hex, offset: hex, table?: addr, table_module?: str, table_rva?: hex, stop?: count\|not code\|end of read, stop_value?: hex, found?: hex, rows: [{slot: u64, offset: hex, target: addr, module?: str, rva?: hex, body?: addr, body_module?: str, body_rva?: hex}], parts?: [{offsets: [hex], table: addr, table_module?: str, table_rva?: hex, rows: [{slot: u64, offset: hex, target: addr, module?: str, rva?: hex, body?: addr, body_module?: str, body_rva?: hex}]}], rtti?: {locator: addr, name: str, decorated: str, complete: addr, complete_offset: hex, bases: [{name: str, decorated: str}], capped: bool}, rtti_error?: str, chain?: Chain}` |

## Unreal pack

| Command | Pack | What it does | `result` |
|---|---|---|---|
| `find {text?, cls?, live?, outer?, view{start?, limit?}?}` | explore | objects by name text and/or class; 24 per page, or limit | `{rows: [Object], total?: u64, from: u32, next_cursor?: u32}` |
| `census {cls?, outer?, diff?, view{start?, limit?, match?}?}` | observe | live instances per class, most first; 100 rows a page; diff: only changed counts | `{filter?: str, classes: u64, instances: u64, before?: {classes: u64, instances: u64}, ms: i64, rows: [{class: str, ref: ref, count: u32, before?: u32, samples: [ref]}], total: u64, from: u64, next_cursor?: u64, hint?: str}` |
| `name-find {text, view{start?}?}` | explore | FName indices whose text contains text (gameplay tags, row names) | `{rows: [ue.Name], from: u32, next_cursor?: u32}` |
| `name {indices[]}` | explore | FName comparison indices as text (FName #N in dumps) | `{rows: [ue.Name]}` |
| `each {cls, paths[], duration?, where?, outer?, view{limit?, nonzero?, elements?}?}` | explore | a table over a class's live instances: a value per column (up to 16 paths) | `{class: str, class_ref: ref, columns: [str], mode: field\|call, rows: [{ref: ref, name?: str, values: [value], hex?: bytes}], same_tick: bool, capped: bool, truncated: bool, errors?: [{ref: ref, column: u32, message: str}], seconds?: u32, polls?: u64, changes?: [{ms: i64, tick: u64, ref: ref, before: value, after: value}], poll_error?: str, matched?: u64, omitted?: u64, unnamed?: u32}` |
| `call {object, function, params?, block?, dry?, view{start?, limit?, fields[]?, nonzero?}?}` | modify | call a reflected function with inputs by name (typed) or a raw block **Writes or calls.** | `{ref: ref, function: str, mode: raw\|typed\|release\|dry, block_size: u64, hex: bytes, kept: bool, kept_reason?: str, outputs?: [{name: str, direction: out\|return, value: value, truncated: bool, meta?: ue.FieldInfo}], truncated?: bool, exec?: addr, pure?: bool, unnamed?: u32}` |
| `call-pure {object, function, params?, block?, dry?}` | observe | call a const or pure function that keeps nothing; read-only; args as call | `{ref: ref, function: str, mode: raw\|typed\|release\|dry, block_size: u64, hex: bytes, kept: bool, kept_reason?: str, outputs?: [{name: str, direction: out\|return, value: value, truncated: bool, meta?: ue.FieldInfo}], truncated?: bool, exec?: addr, pure?: bool, unnamed?: u32}` |
| `call-each {cls, function, duration?, outer?, view{limit?, nonzero?}?}` | observe | call a const or pure function with no inputs on every live instance of a class, in one tick | `{class: str, class_ref: ref, columns: [str], mode: field\|call, rows: [{ref: ref, name?: str, values: [value], hex?: bytes}], same_tick: bool, capped: bool, truncated: bool, errors?: [{ref: ref, column: u32, message: str}], seconds?: u32, polls?: u64, changes?: [{ms: i64, tick: u64, ref: ref, before: value, after: value}], poll_error?: str, matched?: u64, omitted?: u64, unnamed?: u32}` |
| `release {object, function, block?}` | modify | one more call (zeroed inputs by default) that drops the kept block **Writes or calls.** | `{ref: ref, function: str, mode: raw\|typed\|release\|dry, block_size: u64, hex: bytes, kept: bool, kept_reason?: str, outputs?: [{name: str, direction: out\|return, value: value, truncated: bool, meta?: ue.FieldInfo}], truncated?: bool, exec?: addr, pure?: bool, unnamed?: u32}` |
| `member-name {object, offset, type, name}` | native | name an unreflected member in <console>.native.txt | `{path: str, line: u64, owner: str, offset: hex, type: str, name: str, size: u32, timestamp: u32}` |
| `labels-export {file?, module?, kinds[]?}` | native | names for a static tool (JSON lines, one build) | `{path: str, module: str, timestamp: u32, total: u64, globals: u64, user: u64, exec: u64, vtables: u64, interfaces: u64, interface_lookups: u64, other_builds: u64, capped: bool}` |
| `assets-export {dir, classes[], view{match?}?}` | native | each live object of the classes into its own JSON file: nonzero fields, table rows | `{dir: str, index: str, exported: u64, tables: u64, failed: u64, capped: bool, rows: [ExportedAsset], omitted?: u64}` |
| `layout-export {file, classes[], closure?}` | native | reflected class, struct and enum layouts to a JSON-lines file | `{path: str, classes: u64, structs: u64, enums: u64, fields: u64, missing?: [str], capped?: bool}` |
| `layout-check {file}` | native | a file of expected fields against the live layout; only the mismatches | `{path: str, classes: u64, checked: u64, mismatched: u64, ok: bool, rows: [{owner: str, field: str, problem: no class\|no field\|offset\|size\|type, expected: {offset: hex, size: u32, type: str}, found?: {offset: hex, size: u32, type: str}}], capped: bool}` |
| `entries {object, field, view{start?}?}` | explore | one page of an array, set or map: elements, or key and value | `{ref: ref, field: str, kind: array\|set\|map, total: u64, rows: [{key?: value, value: value, key_object?: Object, value_object?: Object}], from: u32, next_cursor?: u32, truncated: bool, unnamed?: u32, tick_accurate?: bool}` |
| `rows {object, at?, view{start?, limit?, match?, fields[]?, nonzero?}?}` | explore | a data table's rows decoded by RowStruct, or a curve table's curves and keys | `{ref: ref, row_struct?: str, row_struct_ref?: ref, curve?: simple\|rich, at?: f64, total: u64, rows: [{name: str, address: addr, value?: value, interp?: linear\|constant\|cubic\|none, keys?: value, weighted?: bool}], from: u32, next_cursor?: u32, truncated: bool, unnamed?: u32}` |
| `get {object, paths[]?, mode?, own?, file?, view{match?, nonzero?, elements?}?}` | explore | an object's fields with values, or named paths; types: the layout | `{ref: ref, rows: [{path?: str, name?: str, raw?: str, owner?: str, type?: str, offset?: hex, size?: u32, struct?: str, bool_mask?: u32, element_type?: str, element_size?: u32, value_type?: str, value_size?: u32, value_offset?: hex, value_struct?: str, subtype?: str, value_subtype?: str, stride?: u32, native?: bool, stale?: bool, value?: value, before?: value, truncated?: bool, address?: addr, module?: str, rva?: hex, holder?: ref, value_module?: str, value_rva?: hex, object?: Object, preview?: {total: u64, elements: [{key?: value, value: value, object?: Object, key_object?: Object, value_object?: Object}]}, preview_error?: str, meta?: ue.FieldInfo, hex?: bytes, error?: str}], same_tick?: bool, truncated?: bool, capped?: bool, filtered?: {shown: u64, total: u64}, native_refused?: [{line: u64, name: str, owner: str, offset: hex, reason: str}], snap?: {size: u64, path?: str}, tick_accurate?: bool, unnamed?: u32}` |
| `line-trace {start, end, channel?, simple?, ignore?}` | observe | the first thing a line hits: actor, component, material, distance (read-only) | `{start: [f64], end: [f64], channel: str, complex: bool, ignored?: ref, hit: bool, initial_overlap?: bool, distance?: f64, impact?: [f64], normal?: [f64], actor?: Object, component?: Object, material?: Object, bone?: str, face?: i32, item?: i32}` |
| `calls-start {object, function, duration, dry?}` | modify | record a reflected function's calls, with their arguments, for a while **Writes or calls.** | `{function: str, slot: u32, exec: addr, thunk: addr, block_size: u32, object?: addr, seconds: f64, dry: bool, note?: str}` |
| `calls-read {slot?, stop?, view{start?}?}` | modify | a recording's calls so far, decoded by the function's parameters; stop ends it first | `{function: str, state: recording\|expired\|finished\|replaced, slot: u32, calls: u64, events: u64, vm: u64, dropped: u64, rows: [{sequence: u64, ms: f64, thread: u32, object: ref, object_name?: str, object_class?: str, via: event\|vm, values?: [{name: str, direction: in\|out\|return, value: value, truncated: bool}]}], next_cursor?: u64, unnamed?: u32}` |
| `describe {object?, slot?, serial?}` | explore | an object's name, class and reusable ref | `{ref: ref, name: str, class: str, class_size?: u32, outer?: {name: str, ref: ref}}` |
| `func {object, function}` | explore | a function's reflected metadata, exec entry address and parameter signature | `{ref: ref, function: str, address: addr, flags: u32, exec?: addr, executable: bool, module?: str, rva?: hex, module_error?: str, interface?: {offset: hex, slot?: i64, implementation?: addr, implementation_module?: str, implementation_rva?: hex}, block_size?: u32, params?: [{name: str, owner: str, type: str, offset: hex, size: u32, struct?: str, bool_mask?: u32, element_type?: str, element_size?: u32, value_type?: str, value_size?: u32, value_offset?: hex, value_struct?: str, stride?: u32, subtype?: str, value_subtype?: str, direction: in\|out\|in ref\|in/out ref\|return, flags: u32}], params_error?: str}` |
| `script {object, function?, decode?, entry?, to?, view{match?}?}` | explore | a Blueprint function's bytecode: names, functions, properties it uses; decode: as statements | `{ref: ref, function: str, size: u32, names: [{offset: hex, count: u32, index: i32, name: str}], functions: [{offset: hex, count: u32, ref: ref, function: str}], properties: [{offset: hex, count: u32, address: addr, property: str, raw?: str}], statements?: [{offset: hex, op: str, text: str}], events?: [{offset: hex, event: str}], stopped?: str, filtered?: {shown: u64, total: u64}}` |
| `enum {enum}` | explore | an enum's names and values, in order | `{ref: ref, name: str, rows: [{name: str, value: i64}]}` |
| `game-thread {action}` | modify | arm or remove the host's game-thread tick (status shows it) **Writes or calls.** | `{mode: off\|waiting\|conflict\|on\|stopped, text: str, thread?: u32, viewport?: addr, table?: addr, draw?: addr, draw_module?: str, draw_rva?: hex}` |
| `name-refs {name?, name_index?}` | explore | Blueprint functions whose bytecode names a pooled name (EX_NameConst) | `{index: i32, name?: str, rows: [{ref: ref, owner: str, name: str, offset: hex, count: u32}], capped: bool}` |
| `owner {addrs[], within[]?}` | explore | the object holding each address: the field, the Outer pointer or the container element | `{rows: [{address: addr, object?: Object, offset?: hex, path?: str, raw?: str, type?: str, outer?: bool, container?: bool}], owned: u32}` |
| `route {object, sources[]?, depth?, budget?, up?}` | explore | the shortest routes of reflected references from a root or object to an object | `{object: Object, rows: [{root?: str, source: Object, path: str, steps: [{field: str, object: Object}]}], end: found\|depth\|budget\|none, visited: u32, depth: u32}` |
| `funcs {object, view{start?, match?}?}` | explore | functions on the class chain, then on the native interfaces it implements | `{ref: ref, rows: [{owner?: str, name: str, via?: str}], from: u32, next_cursor?: u32}` |

## Named types

Types that several results share, as `help` with `json` lists them under `types`.

| Type | Definition |
|---|---|
| `Chain` | `{base: addr, hops: [{hop: u64, address: addr, value: addr, module?: str, rva?: hex}], address?: addr, module?: str, rva?: hex, broken?: u64, reason?: str}` |
| `ExportedAsset` | `{name: str, class: str, ref: ref, file: str, table_rows?: u64, error?: str}` |
| `Object` | `{ref: ref, name: str, class: str, class_size?: u32, outer?: {name: str, ref: ref}}` |
| `Region` | `{kind: code\|rodata\|data\|mapped\|private\|reserved\|free, base: addr, size: hex, protect: str}` |
| `RunReport` | `{steps: u64, calls: u64, ok: u64, failed: u64, refused?: {<key>: u64}, expect_met: u64, expect_failed: u64, ms: f64, timings: [{step: u64, verb: str, calls: u64, ms: f64}], console: str, commit: str, agent: str, table_hash: str}` |
| `TimedWrite` | `{id: str, mode: undo\|hold, address: addr, bytes: bytes, state: active\|restored\|conflict\|abandoned\|expired\|object changed\|not writable\|memory differed from expect\|cleared\|access faulted\|unknown, remaining_ms: u32, original?: bytes, owner?: addr, object?: addr, changed_tick?: u64, guard?: owner replaced or freed\|object vtable changed\|target unreadable\|unknown, guarded?: bool, writes?: u32}` |
| `WatchRange` | `{address?: addr, offset?: hex, length: u32, type?: str, guarded: bool}` |
| `WatchSample` | `{tick: u64, us: u64, values?: [value], lost?: bool, resumed?: bool, root?: addr}` |
| `WatchSummary` | `{range: u32, element?: u32, samples: u64, changes: u64, first: f64, last: f64, least: f64, greatest: f64, per_second?: f64}` |
| `ue.FieldInfo` | `{name: str, owner: str, type: str, offset: hex, size: u32, struct?: str, bool_mask?: u32, element_type?: str, element_size?: u32, value_type?: str, value_size?: u32, value_offset?: hex, value_struct?: str, stride?: u32, subtype?: str, value_subtype?: str}` |
| `ue.Name` | `{index: i32, name?: str}` |

## Envelopes

Every line is an envelope: `command`, `ok`, and `result` or `error`. Errors carry `kind` and
`message`; client errors (`agent`, `timeout`, `attach`, `busy`) also carry `status`, `detail` and
`reason`. A refused `write` with `expect` adds `found`; a broken pointer chain adds `chain` with
`broken` and `reason`; kind `pack` adds `pack`, the pack to start the console with (no arg is at
fault, so no `example`). Any envelope can carry `timing` when the call set `timing`. A call refused
before it ran has kind `schema`: `key` (the key at fault, when one is), `arg` (that key's part of
the schema) or `args` (the verb's whole list), and `example`, a corrected `{verb, args}` built from
the call. A result past the call's `max_chars` is not printed: `digest` stands in for `result`,
with `ok` true: `{chars, max_chars, rows?: {path, count}, largest: [{path, chars}], narrow?: [key],
next?: str, file?: str}`. `next` is the call narrowed, as JSON text; `file` holds the whole envelope
of a call that must not run again. Once a tool caller's session is past the console's
`--context-limit`, every envelope also carries `context_warning`, one line of text (`usage` shows
the total). A result that does not match its row's type is an `internal`
error, never printed as a result.

## Game values

A `value` holds what the game's data is. Addresses are hex strings, null for a null pointer.
An FName is its text (`Name_N` for an instance, `#index` when the pool has none), and a struct
holding only a name (a gameplay tag) is that name; `meta` gives `index`, `number` and `name`.
Structs expand to `{Name: value}` (`members` with `meta`). Arrays and strings show their header, and `get` with no paths adds a `preview` for top-level
containers. Unsupported layouts give a bounded `hex` prefix. A non-finite float is
`{"non_finite": "nan"}`. Check `truncated` on the result and its rows before treating values as
complete.
