# Changelog

ReAgentLive follows semantic versioning. Before 1.0 the API and the wire
layout can change in any minor release; agent and client are always built
from the same revision, and the handshake's layout check refuses a
mismatched pair. The version lives in `src/reagentlive/version.h`.

## Writing entries

- **Write for the user, not the code.** Say what someone can now do, what
  got better or what broke for them, not how it was built.
- **One change, one or two lines.** Lead with the effect in bold; link the
  doc page instead of explaining.
- **Group under New, Improved, Fixed, Breaking.** Merge related changes. A
  feature reworked before release updates its entry; it gets no second one.
- **Leave out** refactors, tests, internal file moves, op numbers, struct
  sizes and fixes to bugs that never shipped.
- **Breaking entries say what to change** ("pass `--all`", "rename X to Y").
- **Protocol changes:** an entry that changes the wire protocol (an op, a
  struct or a constant in `core/protocol.h` or a pack's `protocol_*.h`)
  ends with the exact line `Protocol change: rebuild and re-inject.`
  [The update procedure](docs/agents/update.md#2-show-what-is-coming)
  searches for it.
- **A release opens with one sentence** on what it gives the user.

## Unreleased

### Breaking

- **Verbs renamed so no two differ by a letter and families read alike:**
  `stats` is `usage`, `refs` is `name-refs`, `label` is `member-name`,
  `export-labels` is `labels-export`, `export-assets` is `assets-export`,
  `trace` is `line-trace` and `writes` is `timed-writes`; their MCP tools
  and Python functions follow (`ue_name_refs`, `c.usage()`). Args:
  `labels-sign`'s `again` is `retry`, `mem-diff`'s `test` is `compare`
  (and its result key). Update calls, plans and scripts to the new names.
- **The per-result cap is `max_chars`:** the call key `budget`, the MCP
  tools' `budget` argument, the Python session's `budget=` and the
  digest's `budget` key are all `max_chars` now.
- **The console takes JSON calls only:** the text grammar is gone. A call
  is `{"verb": ..., "args": {...}}`, one per line on stdin, through `-c`
  (one call or a list) or `-c @<file>`; `batch` takes `{"calls": [...]}`.
  Every arg is named and typed (`help` with `verb` shows them and an
  example call), and unknown keys, missing required keys and wrong types
  are refused with kind `schema`, the key's schema and a corrected
  example. Values are typed: paths are lists (`["Info", "Owner"]`),
  chains `{"base": <addr>, "chain": [16, 8]}`, module addresses
  `{"module": "x.dll", "rva": "0x10"}`, call inputs `params`, durations
  seconds. `$N` is gone (pass the literal `ref`), and so are `?`, `exit`,
  `q` and the line-end flags (`meta`, `timing` and `budget` are call
  keys). The usage log's and `stats`' `flags` are `keys`
  ([JSON-only contract](docs/tool-interface.md#json-only-contract)).
  Regenerate Python modules and restart MCP servers.
- **Output options are one `view` object:** `start`, `limit`, `match`,
  `fields`, `nonzero` and `elements` go inside `view`
  (`{"view": {"limit": 5}}`); one given at the top is refused with where
  it goes, and a digest's `narrow` and `next` name them by path
  (`view.limit`). Related options are one object: `search`'s `scope`,
  `write`'s `undo`, `disasm`'s `save`
  ([reference](docs/commands/reference.md#view)).
- **Verbs merged or renamed** (each old form is refused):

  | Was | Now |
  |---|---|
  | `dump`, `fields`, `snap` | `get` (every field; `mode` `types`; `mode` `snap` and `changed`, with `file`); path rows' `object` is `holder`, `get` rows carry `raw` |
  | `params` | `func` (`params`, `block_size`; `params_error` off the game thread) |
  | `trial`, `trial-list`, `trial-clear` | `write` with `undo` (`after`, `owner`, `object`) and `expect`; `writes`, `writes` with `clear` |
  | `pin`, `pin-list`, `pin-clear` | `write` with `hold` (seconds); `writes`, `writes` with `clear` (ids `undo:N`, `hold:N`) |
  | `watch`, `record`, the console's `--replay` | `watch-start` (field ranges, `file`), then `watch-read` or `watch-stop`; a script that waits: the Python session's `watch(seconds, ranges)` |
  | `each` with a function, `call-pure` over a class | `call-each` (`cls`, `function`, `seconds`); `each` only reads |
  | `game-thread` status | `status`'s `game_thread`; `game-thread` takes `action` `on` or `off` |
  | `census-diff` | `census` with `diff` (rows `count` and `before`; `before` holds `classes` and `instances`) |
  | `rtti` | `vtable` with `rtti` |
  | `sig-file` | `sig-check` with `file` (rows are `lines`) |
  | `base` | `modules` (the exe first) |
  | `region` | `read` and `peek` add `region` |
  | `read-batch` | `batch` of `read`s, or `get` |
  | `peek` with a label file | `peek` with `label` and `file` |
  | `funcs`' `text` | `view.match` |
  | `search` with `wide` | `type` `wtext` |

- **`search` is a job, in the main module by default:** it answers within
  `wait` seconds (default 10) with `job`, `state`, `to` and `scanned`
  (the share of the scope's searchable memory done, so it moves while the
  job runs); a scan still running keeps going in the agent while other commands run,
  and `search-job` reads or (with `stop`) ends it. The agent uses its
  worker when the host allows it. A search with no `scope` covers the
  main module only: add `scope.heap` or `scope.writable` for data
  ([search](docs/commands/memory.md#search)).
  Protocol change: rebuild and re-inject.
- **MCP tool packs replace `--families` and `--read-only`:** the adapter
  serves `--packs` (default `explore`) and lets the agent turn on the
  `--allow` packs through the always-served `packs` tool; `modify` only
  when named. A server that used `--read-only` now runs with no flag; one
  that needs every tool takes `--packs explore,native,observe,modify`
  ([MCP tools](docs/client.md#mcp-tools)).
- **The MCP adapter needs the MCP Python SDK 2:** run
  `python -m pip install --upgrade "mcp>=2"` in the Python your MCP config
  starts (SDK 1 stops it with that line).
- **One IDA file:** `frontends/ida/reagentlive_ida.py` replaces
  `import_labels.py`; copy it into IDA's loaders folder, delete the old
  file, and copy it again after every update
  ([IDA loader](docs/commands/ida-loader.md#install)).

### New

- **Decompile live functions without a whole-image database:** `disasm`
  with `function` lists a whole function (up to 2,048 instructions, the
  value each read-only constant load reads, a `strings` summary), and
  with `save` writes it for the IDA loader, which opens it as a small
  database. A save carries the function's chained chunks and switch
  targets (then no `JUMPOUT`), with `save.callees` (1..3) the functions it
  calls or tail-jumps to, breadth-first inside whole-save caps, names from
  the labels files, and the layouts of its methods' classes, so the first
  decompile shows `this->Field`. `add_saved` grows one workspace
  database, and a save that would hold only part of a function is
  refused
  ([saving a function](docs/commands/fast-lane.md#saving-a-function-for-a-static-tool)).
- **`fetch` in IDA:** `rl.fetch(addrs, depth=..., decompile_dir=...)` runs
  the console's save, adds it to the open database and decompiles it: one
  call from a live address to pseudocode. `decompile_to` writes a `.c`
  file per function with a grep summary; `import_layouts` and `apply_this`
  give types
  ([IDA loader](docs/commands/ida-loader.md#from-an-address-to-pseudocode-in-one-call)).
- **Names round trip with IDA:** `<console>.labels.jsonl` holds derived
  names (`export-labels` merges into it and keeps what it does not
  derive), and a host's names file (`ConsoleConfig::labelsFile`) the
  names people confirm (`peek` with `label`, IDA's `export_names`), apart
  and under version control; saves read both
  ([labels](docs/commands/labels.md#labels-files)). `export_names` writes
  only names a person set: never IDA's switch and jump-table names
  (`def_`, `jpt_`), auto-generated or library names, or compiler helpers
  (`__alloca_probe`, `?` mangled names).
- **Names that survive a patch:** `labels-sign` stores a byte signature on
  each confirmed name; after a patch `labels-port` finds them in the new
  build and adds them to the file, listing only what changed. A row it
  cannot sign is marked and skipped on later calls on that build (`again`
  retries it)
  ([labels](docs/commands/labels.md#names-that-survive-a-patch)).
- **Blueprint logic as statements:** `script` with `decode` reads the
  Kismet bytecode as statements with byte offsets
  (`if !(Speed > Limit) goto +0x58`), names properties by their readable
  names and needs no game thread; on an uber graph it lists each event's
  entry offset, and `entry` reads one event alone; `view.match` and an
  offset range (`entry`, `to`) narrow a big decode
  ([Unreal code](docs/commands/unreal-code.md#decode)).
- **Layouts for a static tool's types:** `layout-export` writes a `class`
  or `struct` line (super, size) before each type's fields, field lines
  name the struct, class or enum they hold, enums get their values, and
  `closure` also exports what the fields reference; `get` with `mode`
  `types` shows the class or enum as `subtype`
  ([layouts in bulk](docs/commands/unreal-fields.md#layouts-in-bulk)).
  Protocol change: rebuild and re-inject.
- **`rows` reads curve tables:** a `CurveTable`'s rows come back with their
  keys (`curve: simple|rich`), and `at` gives each curve's value at that
  time, evaluated per key mode
  ([rows](docs/commands/unreal-fields.md#rows)).
- **Asset data to files:** `export-assets` writes each live instance's
  nonzero fields, and a data or curve table's rows, to one JSON file per
  object with an index, so a run reads asset data offline
  ([assets to files](docs/commands/unreal-objects.md#assets-to-files)).
- **Plan files:** `run` runs a JSON list of steps (`{verb, args, expect?,
  repeat?: {every, for}}`), each as its own call, and writes one NDJSON
  line per result and a report. Steps run one after another; a `batch`
  step reads one moment. `repeat` `{"every": 1, "for": 5}` is 5 runs.
  Rerun the file to replay an investigation
  ([plans](docs/commands/console.md#plans)).
- **Tool packs and a ceiling:** every verb belongs to one workflow pack,
  `explore`, `native`, `observe` or `modify` (every verb that writes or
  calls is in `modify`); `help` groups by pack. The console's `--packs`
  and the Python session's `packs=` refuse verbs outside the ceiling with
  kind `pack` (its `pack` names the one to add), `batch` items included;
  `status` lists `tool_packs`, `stats` counts `calls_by_pack` (a call
  refused for its pack counts as a refusal only)
  ([tool packs](docs/tool-interface.md#tool-packs)).
- **A generated verb reference:** [docs/commands/reference.md](docs/commands/reference.md)
  lists every verb with its args, result, notes and an example call, the
  `view` keys and every error kind. A host checks its own docs against its
  table with `tests/python/check_verb_names.py --table <console>`.
- **`--check`:** the console checks calls without a target, each answered
  `{"checked": true}` or with the refusal it would get before running
  ([one-shot use](docs/commands/console.md#one-shot-use)). A host checks the
  JSON examples in its docs with
  `tests/python/check_json_examples.py --table <console>`.

### Improved

- **A `cls` that names no class is refused:** `find`, `census` and `each`
  answer not found, naming up to five classes that contain the name,
  instead of an empty result a typo could pass for "no instances"; `each`
  takes the class in any case, as the others do
  ([object filters](docs/commands/unreal-objects.md#object-filters)).
- **`call-pure` decodes by default:** without `params` or `block` the call
  is typed, its outputs decoded as `call-each`'s; a function with inputs
  still needs `params` ([call-pure](docs/commands/unreal-calls.md#call-pure)).
- **`find` by `cls` and `each` use a class index:** the agent rereads only
  the object-table slots that changed, so a class lookup no longer walks
  every object ([find](docs/commands/unreal-objects.md#find)). Rebuild and
  re-inject the agent.
- **Code outside the exception directory:** `xrefs` and `touches` bound
  it as `unlisted` pieces, ignore one-byte decoy entries, and `touches`
  scans leaf functions too; `touches` with `reads` ranks by offsets read
  ([fast lane](docs/commands/fast-lane.md#function-bounds)).
- **A digest keeps `saved`, `next` and a search job's `job`, `state` and
  `scanned`:** a call too large to print still names the file it wrote, a
  cut listing its resume address and a search job what to poll
  ([output budgets](docs/commands/console.md#fewer-round-trips)).
- **Arg help states each enforced range** (`search`'s `max` 1..507,
  `read`'s `len`, `vtable`'s `count` and `offset`, `touches`, `trace`'s
  `channel` and others), so `help` with `verb` shows a bound before a call
  hits it.
- **`get`, `rows` and `entries` take a `Default__` name**, as `call` does
  ([class names](docs/commands/unreal-objects.md#class-names)).
- **`search` text rows carry the string they hit** (`text`), in
  `search-job` reads too, from a later console run as well
  ([search text](docs/commands/fast-lane.md#search-text)).
- **`each` rows carry the instance's `name`**
  ([each](docs/commands/unreal-objects.md#each)).
- **IDA label import:** one undo step and a wait box that cancels it.
- **Game text is marked untrusted:** the MCP server's instructions and the
  skill tell agents that names, strings and chat read from the target are
  data, never instructions.
- **doctest is fetched, not copied:** CMake downloads a pinned release at
  configure, like Zydis and nlohmann/json; `third_party/` is gone.

### Fixed

- **A request after a timeout can no longer run half-written:** the agent
  could copy the shared request while the console overwrote it with the
  next one, and run a mix of both. The request number is now a seqlock.
  Protocol change: rebuild and re-inject.
- **`Host::backgroundSearch` is checked on every search:** a host that
  turned the opt-in off still had searches run on the worker.
- **`name-find` and `refs` scan the whole name pool:** obfuscated entries
  made the walk stop mid-block and miss about a sixth of the names; `name`
  returns empty for an index that is not an entry start.
- **Limits that refused real calls:** `find`'s `view.limit` gathers up to
  1,000 rows over pages; `disasm` takes up to 1,024 instructions;
  `sig-bytes` cuts a length past 96 to 96 (`capped: true`).
- **Function bounds on packed images:** unwind data of an unknown version
  counts as a plain entry, as the OS unwinder treats it, and a DLL import
  with no name table is left unnamed instead of named from resolved
  addresses.
- **The Unreal pack's agent cannot crash on a guard page or hide a stack
  overflow:** its startup checks look at the memory region before reading,
  and name decoding catches only bad-memory faults.
- **`touches` rows no longer lose their `text` on long instructions.**
- **`stats`:** one-shot runs chain only with each other: they no longer
  join a session named with `--session` (the MCP adapter's) or a
  long-lived console's (a Python session's), where they were counted and
  missing from `stats` with `sessions`. The totals' `calls`,
  `calls_by_pack` and `ms` all count top-level calls, with `batched` for
  the calls inside batches. A refused call is never marked `unfiltered`,
  nor one that printed 2000 characters or less, nor a `get` with `paths`.
- **`disasm` with `save` leaves `rows` out** instead of an empty list.
- **`get` decodes maps and sets of big structs:** the agent refused any
  element slot past 512 bytes as an unknown layout. A container it still
  cannot decode names its key and value types and sizes; `entries` refuses
  slots past 512 bytes and points to `get`. Rebuild and re-inject the agent.
- **`view.nonzero` drops a curve's engine defaults:** a float at its
  maximum (an unset `DefaultValue`) and an empty key-handle map, which
  `get` now prints as `{"count": N}` instead of its header bytes.
- **IDA label import refused databases loaded the default way:** it reads
  the PE header IDA keeps.

## 0.4.0

Any UE5 game without an SDK dump, engine calls from any host, and output
that costs an agent far less.

### New

- **Any UE5 game, no SDK dump:** every Unreal command runs from the engine
  layout alone, each part checked on live objects before use. A ready-made
  DLL and console live in `examples/unreal/`; the skill explains finding a
  new game's layout.
  Protocol change: rebuild and re-inject.
- **`game-thread on`:** engine calls work in hosts that tick on their own
  thread, through a tick on the game viewport's Draw.
  Protocol change: rebuild and re-inject.
- **Call recorder** ([unreal](docs/commands/unreal-calls.md#call-recorder)):
  `calls-start` records a reflected function's calls with decoded
  parameters for up to 60 s, without writing code or vtables.
  Protocol change: rebuild and re-inject.
- **Richer typed calls:** pass bools, numbers, structs, names, gameplay
  tags and objects as text; returned arrays come back as paged rows.
  Interface functions show in `funcs`.
  Protocol change: rebuild and re-inject.
- **New reads:** `trace` (line traces), `rows` (data tables), `enum`, and
  text values shown as strings.
  Protocol change: rebuild and re-inject.
- **Object filters:** `--outer` on `find`, `each` and `census`;
  `each --where Field~text`; `find --live` skips default objects.
  Protocol change: rebuild and re-inject.
- **`watch` on a field** (`object.Field`), returning only the samples that
  changed.
- **Guided setup and update** for AI agents in `docs/agents/`; the skill
  updates with a `git pull`, and `status` shows the console's commit.

### Improved

- **Much smaller output:** names, tags and text print as plain text,
  offsets in hex, floats at float precision, and `disasm` rows compactly.
  A result past 25,000 characters becomes a digest with the narrower call
  to run next; `stats` shows what that saved.
- **Values decode the same everywhere:** sets, maps, arrays and object
  pointers at any depth, in every command. `--nonzero` and `--elements`
  work on `get`, `each`, `rows`, `dump` and `call`.
- **Class fields load several times faster** and long names are no longer
  cut. Protocol change: rebuild and re-inject.
- **MCP tools** are built straight from the console and check arguments
  first; a busy agent is waited for instead of refused.
- **Clearer errors** when the Python session or MCP adapter cannot start a
  console.
- **Docs rewritten** for beginners, with a tier ladder and a short skill.

### Fixed

- Values could be wrong or unnamed the first time a nested struct was seen.
- Safer live reads: a gone object is refused instead of matched to
  whatever reuses its address, and a `dump` of an object freed mid-read
  fails instead of mixing bytes.
- `func` on some interface functions, and typed calls returning a const
  reference.

### Breaking

- **Output:** names, tags and text are plain text (`--meta` for the old
  shape); offsets are hex strings; `each` stops at 100 rows (`--limit`);
  `watch` returns changes only (`--all`); large results come back as a
  digest (`--budget`, `--big`).
- **Arguments:** the object positional is `object` everywhere
  (`calls-start`: `owner`).
- **MCP:** `batch` refuses writing verbs, so send those as their own tool;
  `--output-schemas full` is gone.
- **Python:** `read_bytes`, `get_values`, `call_bytes` and the other
  hand-written helpers are gone; use the generated functions.
- **Hosts:** Unreal headers moved to `reagentlive/packs/ue/`;
  `REAGENTLIVE_PACKS` replaces `REAGENTLIVE_CONSOLE_UE`; engine offsets
  come from `packs/ue/engine_layout.h`, not the protocol.
- **Docs:** setup and update moved to `docs/agents/`; paste the new
  README prompts.

## 0.3.0

Cheaper agent sessions, engine calls that survive a fault, and an Unreal
pack that keeps working after a game patch.

### New

- **`call-pure`:** read-only engine calls, refused before calling unless
  safe; `call --dry` shows the call without making it.
  Protocol change: rebuild and re-inject.
- **Patch checks in bulk:** `layout-export` saves class layouts,
  `layout-check` reports only what changed.
- **Native code without an engine pack:** `rtti` names C++ classes,
  `disasm` names imports, and `mem-snap` / `mem-diff` narrow a memory
  range by how it changes.
- **`stats`:** see which calls cost a session the most output.
- **Typed watches** (`<addr>:f64`) with a summary per value.
- **A console without the Unreal pack** (`REAGENTLIVE_CONSOLE_UE=OFF`).

### Improved

- **Half-size MCP tool list**, and tools that follow a rebuilt console
  without restarting the client.
- **Engine calls survive a fault:** a crash inside a call is reported
  instead of ending the game.
- **Survives game patches:** the Unreal pack finds its globals itself.
- **Faster lookups** by class name, and `each` returns a table of up to
  16 fields.
- **Clearer refusals** for unknown keys, flags and limits.
- **Opt-in reads off the engine's thread** for hosts without a game-thread
  tick.

### Breaking

- `--class` is `--cls`, `--from` is `--start`, `read --as` is
  `read --type`.
- `dump`, `get`, `each` and `watch` results changed shape.

## 0.2.0

AI agents get MCP tools: one tool per console verb, with checked
arguments and typed results.

### New

- **MCP tools** ([clients](docs/client.md)), plus JSON calls and generated
  typed Python functions, all from the console's own verb table.
- **`help`** lists every verb, its arguments and its result.
- **`status` lists the packs** a host offers.

### Improved

- `get` reads all its values in one game tick.

### Fixed

- One cached `$N` could make the next run drop the whole cache.

### Breaking

- Modes became verbs (`sig-check`, `watch-start`, `pin-list`, ...);
  result keys are uniform (`rows`, `next_cursor`, `capped`,
  `truncated`); the Python client was rewritten around the generated
  functions.
- Hosts give verb rows (`ConsoleConfig::verbs`) instead of `hostHelp`;
  `Ref` moved to the core protocol.
  Protocol change: rebuild and re-inject.

## 0.1.0

First versioned release.

- In-process agent: reads, same-tick batches, guarded writes, pins, trial
  writes, `watch`, `search` and `region`.
- Unreal pack: objects, reflected fields, containers, engine calls,
  `census`, `each`, labels for IDA.
- Fast native work: `disasm`, `xrefs`, `touches` and signatures from live
  bytes; vtables, pointer chains and strings.
- Console with JSON Lines output and a cache between runs; Python client;
  IDA label importer.
- A generic host for any x64 process, and the `reagentlive` agent skill.
