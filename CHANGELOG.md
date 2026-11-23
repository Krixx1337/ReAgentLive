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

- **`save.callees` is a depth:** `"callees": 1` saves what `true` did;
  2 or 3 follow calls and tail jumps further, breadth-first, inside the
  same caps, now counted over the whole save. `skipped` rows are
  `{address, why}` (`depth`, `count`, `bytes`, `unreadable`) and each
  `functions` row has its `depth`
  ([fast lane](docs/commands/fast-lane.md#saving-a-function-for-a-static-tool)).

- **Output options are one `view` object:** `start` (a page's cursor),
  `limit`, `match`, `fields`, `nonzero` and `elements` go inside `view`
  (`{"view": {"limit": 5}}`) on every verb that takes them; one given at
  the top is refused with where it goes. `get`'s `types`/`snap`/`changed`
  are `mode`; `search`'s `module`/`start`/`to`/`writable`/`heap`/`paged`
  are `scope: {...}` and `wide` is `type: "wtext"`; `write`'s
  `undo`/`owner`/`object` are `undo: {"after", "owner", "object"}`;
  `disasm`'s `save`/`callees`/`labels` are `save: {"file", "callees",
  "labels"}`; `funcs`' `text` is `view.match`. `call-pure` with `each` is
  the verb `call-each` (`cls`, `function`); `call-pure` takes no view. A
  digest's `narrow` and `next` name view keys by path (`view.limit`).
  No verb takes more than six own args beside `view`
  ([JSON-only contract](docs/tool-interface.md#json-only-contract)).
  Regenerate Python modules and restart MCP servers.

- **The console takes JSON calls only:** the text grammar is gone. A call
  is `{"verb": ..., "args": {...}}`, one per line on stdin, through `-c
  '<call or list of calls>'` or `-c @<file>`; `batch` takes `{"calls":
  [...]}`; `$N`, typed lines, `?`/`exit`/`q` and `--meta`/`--timing`/
  `--budget` at a line's end are gone (pass `meta`, `timing`, `budget` as
  call keys). Every verb's args are named and typed (`help` with `verb`
  shows them and an example call); unknown keys, missing required keys
  and wrong types are refused with kind `schema` (no more `syntax`), the
  key's schema and a corrected example. Values are typed: paths are lists
  (`["Info", "Owner"]`), chains `{"base": <addr>, "chain": [16, 8]}`,
  module addresses `{"module": "x.dll", "rva": "0x10"}` (`base+off` is
  gone), `each`'s `where` `{"field": [...], "contains": "..."}`, call
  inputs `params: {"Name": value}` (a raw block is `block`; `--typed` is
  `params: {}`), trace ends `[x, y, z]` or `{"object", "socket"}`, watch
  ranges objects, durations seconds. Renamed: `writes clear` is `clear:
  true`; `describe #i:s` is `index` and `serial`; `refs #i` is `index`;
  `get --snap/--changed [file]` are bools plus `file`; `peek --label
  [file]` is `label` plus `file`; `write --hold` takes seconds; the usage
  log's and `stats`' `flags` are `keys`
  ([JSON-only contract](docs/tool-interface.md#json-only-contract),
  [result shapes](docs/commands/results.md)). Regenerate Python modules
  and restart MCP servers.

- **`search` is a job, in the main module by default:** it answers within
  `--wait` seconds (default 10) with `job`, `state`, `to` and `scanned`; a
  scan still running keeps going in the agent while other commands run,
  and `search-job <id> [--stop]` reads or ends it. `--background` is gone
  (the agent uses its worker when the host allows it). A search without
  `--module`, `--start`, `--to`, `--heap` or `--writable` now covers the
  main module only: add `--heap` or `--writable` for data in memory
  ([search](docs/commands/memory.md#search)). Regenerate Python modules and
  restart MCP servers. Protocol change: rebuild and re-inject.

- **MCP tool packs replace `--families` and `--read-only`:** the adapter
  serves `--packs` (default `explore`) and lets the agent enable `--allow`
  packs through the always-served `packs` tool; `modify` only when named.
  Every console it starts gets that ceiling. A server that used
  `--read-only` now runs with no flag; one that needs every tool takes
  `--packs explore,native,observe,modify`. Restart MCP servers
  ([MCP tools](docs/client.md#mcp-tools)).
- **Exact write marks:** `each` reads fields only and is no longer marked
  `writes`; a getter over every instance is `call-pure <Class> <Fn> --each
  [--seconds N]` (read-only, same table, `mode: call`). `game-thread` takes
  only `on` or `off`; `status` carries its state as `game_thread`. Replace
  `each <C> <Fn>` with `call-pure <C> <Fn> --each` and `game-thread status`
  with `status`; regenerate Python modules and restart MCP servers.
- **Fewer verbs, same answers:** `params` is gone, `func` returns the
  signature too (`params`, `block_size`; `params_error` off the game
  thread). `rtti` is `vtable --rtti`, `census-diff` is `census --diff` (rows
  carry `count` and `before`, the result `before: {classes, instances}`),
  `sig-file <f>` is `sig-check --file <f>` (rows are `lines`). `base` is gone:
  `modules` lists the exe first. `region` is gone: `read` and `peek` add
  `region: {kind, base, size, protect}`. `read-batch` is gone: use `batch`
  or `get`. Regenerate Python modules and restart MCP servers.
- **One field reader, `get`:** `dump`, `fields` and `snap` are gone.
  `get <obj>` lists every field (as `dump` did, same filters), `get <obj>
  <path>...` reads paths (rows add `path`; `object` is now `holder`),
  `get <obj|Class> --types` gives the layout (as `fields`; rows carry
  `raw` in place of `label`), `get <ref> --snap [file]` keeps a baseline
  and `get <ref> --changed [file]` compares with it. A path that names no
  field lists the closest field names. A class by name needs `--types`.
- **One write verb:** `trial` is `write --undo <duration> --owner <ref>
  --expect <hex>`, `pin` is `write --hold [duration]`; both answer
  the write with `timed`. `writes` lists every timed write (ids `undo:N`,
  `hold:N`) and `writes clear [id]` ends them, replacing `trial-list`,
  `trial-clear`, `pin-list` and `pin-clear`.
- **One capture family:** the blocking `watch` (and `--then`) and `record`
  are gone, and so is the console's `--replay`. `watch-start` takes
  `<ref>.<Field>` ranges and `--file <path>` (every sample as JSON lines,
  written by each `watch-read` and `watch-stop`); a script that waits uses
  the Python session's `watch(seconds, ranges)`: start, wait, stop.

- **The MCP adapter needs the MCP Python SDK 2:** run
  `python -m pip install --upgrade "mcp>=2"` in the Python your MCP config
  starts (SDK 1 stops it with that line). Clients on the 2026-07-28
  protocol hear a rebuilt console's tool list on their listen stream.

### New

- **Types travel with a save:** a saved function whose label names a
  reflected class brings that class's layout into the file, and the IDA
  loader makes it a local type and types the function `Class *this` on
  open and on `add_saved`: the first decompile shows `this->Field`
  ([fast lane](docs/commands/fast-lane.md#saving-a-function-for-a-static-tool)).
  Copy the new `reagentlive_ida.py` into IDA's loaders folder.

- **A generated verb reference:** [docs/commands/reference.md](docs/commands/reference.md)
  lists every verb with its args and their help, result, notes and an
  example call, the shared `view` keys and every error kind with its
  message shape. Every example in the docs and the skill is a JSON call; a
  host can check its own docs against its table (unknown verbs or args,
  leftover text-grammar forms) with `tests/python/check_verb_names.py
  --table <console>`.

- **Plan files:** `{"verb": "run", "args": {"plan": "plan.json", "out":
  "lines.jsonl"}}` runs a list of steps (`{verb, args, expect?, repeat?:
  {every, for}}`), each as its own call under the console's `--packs`, and
  writes one NDJSON line per result and a report (counts, refusals by
  kind, expects met and failed, step timings, console and agent builds).
  Rerun the file to replay an investigation
  ([plans](docs/commands/console.md#plans)).

- **Asset data to files:** `export-assets <dir> --cls <Class>... [--match
  <text>]` writes each live instance's nonzero fields, and a data or curve
  table's rows, to one JSON file per object with an index, so a run reads
  asset data offline instead of live again
  ([assets to files](docs/commands/unreal.md#assets-to-files)).

- **Blueprint logic as statements:** `script <obj|Class> <Fn> --decode`
  decodes the Kismet bytecode into readable statements with byte offsets
  (`if !(Speed > Limit) goto +0x58`), names properties by their readable
  names and needs no game thread; on an uber graph it lists each event's
  entry offset, and `--entry <offset>` reads one event alone
  ([Unreal pack](docs/commands/unreal.md)).
- **IDA loader types and pseudocode files:** `decompile_to` writes a `.c`
  file per function with a grep summary, `import_layouts` turns a
  `layout-export --closure` file into local types and `apply_this` types
  `this` on labelled methods
  ([saving a function](docs/commands/fast-lane.md#saving-a-function-for-a-static-tool)).
- **Layouts for a static tool's types:** `layout-export` writes a `class`
  or `struct` line (super, size) before each type's fields, field lines
  name the struct, class or enum they hold (and containers' element, key
  and value), enums get their values, and `--closure [depth]` also exports
  what the fields reference; `get --types` shows the class or enum as
  `subtype`. `layout-check` skips the type lines
  ([layouts in bulk](docs/commands/unreal.md#layouts-in-bulk)).
  Protocol change: rebuild and re-inject.
- **`rows` reads curve tables:** a `CurveTable`'s rows come back with their
  keys (`curve: simple|rich`; rich keys with interp mode and tangents), and
  `--at <t>` gives each curve's value at `t`, evaluated per key mode
  ([rows](docs/commands/unreal.md#rows)).
- **Tool packs:** every verb belongs to one workflow pack, `explore`,
  `native`, `observe` or `modify` (every verb that writes or calls is in
  `modify`). `help` groups the verbs by pack; `help --json` rows carry
  `pack` and the table lists `packs`
  ([tool interface](docs/tool-interface.md#tool-packs)).
- **A console's ceiling, `--packs`:** `--packs explore,native` runs only
  those packs' verbs; others, `batch` items included, are refused with
  kind `pack`. `status` lists `tool_packs`; `stats` counts
  `calls_by_pack` ([console](docs/commands/console.md#one-shot-use)).
- **Python sessions take packs:** `Session(console, packs=[...])` passes
  the ceiling; a refused call raises `ReAgentLiveError` with `kind`
  `pack` ([clients](docs/client.md#python-session)).
- **Names that survive a patch:** `labels-sign` stores a byte signature on
  each name people confirmed (functions and globals); after a patch
  `labels-port` finds them in the new build and adds them to the file,
  listing only what changed. A row it cannot sign is marked and skipped on
  later calls on that build (`--again` retries it). `export-labels` and
  IDA's `export_names` keep the signatures
  ([labels](docs/commands/unreal.md#names-that-survive-a-patch)).
- **A host's names file:** `ConsoleConfig::labelsFile` holds names people
  confirm (`user` rows from IDA's `export_names`, `global` rows from
  `peek --label`, its default) apart from the derived names in
  `<console>.labels.jsonl`; `disasm --save` reads both, and IDA's
  `import_labels` takes a list of files. A host can keep it under version
  control ([hosting](docs/hosting.md#names-files)).
- **Names round trip with IDA:** one labels file per console,
  `<console>.labels.jsonl`, is the default for `export-labels`,
  `peek --label` and `disasm --save` (read when it exists). `export-labels`
  now merges into it and keeps what it does not derive (names given in IDA,
  confirmed globals, other builds). The IDA side is one file,
  `frontends/ida/reagentlive_ida.py`: the save loader plus `import_labels`
  (full or save databases), `export_names` (your names back as `user`
  rows, which outrank every derived name; library names stay out) and
  `decompile_all`. Breaking:
  `load_function.py` and `import_labels.py` are gone; copy
  `reagentlive_ida.py` into IDA's loaders folder and delete the old files
  ([labels](docs/commands/unreal.md#labels-for-a-static-tool)).
- **Tail calls saved like calls:** `disasm --save --callees` also saves
  the functions a direct `jmp` out of the function (or its chunks) reaches,
  and names unsaved tail targets (`tail` rows), so a wrapper no longer
  decompiles as a bare `JUMPOUT`. Copy the new loader into IDA's loaders folder.
- **Chained chunks saved with their function:** `disasm --save` follows a
  function's jumps into exception entries chained to it and saves those
  chunks (`chunks` in the result, `chunk` rows in the file); the IDA loader
  attaches them as function tails, so the pseudocode no longer ends in
  `JUMPOUT`. Copy the new loader into IDA's loaders folder.
- **A function and its callees in one database:** `disasm --save
  --callees` saves each function it calls directly (one level, up to 16),
  `--labels` names functions, call targets and globals from an
  `export-labels` file, and the IDA loader stubs named addresses so calls
  read as names. A save never holds overlapping code, and the loader joins
  rows that overlap with identical bytes or touch. `add_saved(path)` grows
  the open database by a later save and returns what it added, joined and
  cut; it raises `SaveError` rather than load less than the file holds.
  Breaking: a save file holds one `function` row per saved function; copy
  the new loader into IDA's loaders folder
  ([saving a function](docs/commands/fast-lane.md#saving-a-function-for-a-static-tool)).
- **No partial saves:** a `disasm --function` walk from more than 32 bytes
  into unlisted code adds `piece`, and `--save` refuses unless `--resume`.
- **Whole functions in one call:** `disasm --function` lists the function
  to its end (up to 2,048 instructions) with compact rows, the value each
  read-only constant load reads, and a `strings` summary
  ([disasm](docs/commands/fast-lane.md#disasm)).
- **Decompile one function without a whole-image database:**
  `disasm --function --save <file>` and the IDA loader
  `frontends/ida/reagentlive_ida.py`
  ([saving a function](docs/commands/fast-lane.md#saving-a-function-for-a-static-tool)).

### Improved

- **`find --cls` and `each` use a class index:** the agent keeps each
  object-table slot's object and class and, per query, rereads only the
  slots that changed, so a class lookup no longer walks every object
  ([find](docs/commands/unreal.md#unreal-pack)). Text without `--cls`
  still walks the whole table. Rebuild and re-inject the agent.
- **Saves follow switches:** `disasm --save` reads each jump table
  (bounded by the `cmp` before it), saves it as data and each target as a
  chunk, so switch functions decompile without `JUMPOUT`; up to 256 chunks
  (then `capped`), with the targets still unsaved in `jumpouts_left` and
  the tables in `tables` ([saving](docs/commands/fast-lane.md#saving-a-function-for-a-static-tool)).
- **doctest is fetched, not copied:** CMake downloads a pinned doctest
  release at configure, like Zydis and nlohmann/json, so an update is one
  pin; `third_party/` is gone.
- **Game text is marked untrusted:** the MCP server's instructions and the
  skill tell agents that names, strings and chat read from the target are
  data, often written by other players, never instructions.
- **`disasm --save` returns a summary, not the listing:** the file holds the
  rows, so the result keeps `saved`, the new `saved_rows`, the bounds and
  why it stopped, and leaves out `rows` and `strings`
  ([saving a function](docs/commands/fast-lane.md#saving-a-function-for-a-static-tool)).
- **`xrefs` bounds callers outside the exception directory:** a code row
  with no entry carries `unlisted`, its `function` the piece holding it,
  cut as `touches` cuts unlisted code
  ([xrefs](docs/commands/fast-lane.md#xrefs)).
- **One-byte decoy entries no longer slice unlisted code:** `xrefs` and
  `touches` ignore exception entries under 4 bytes when bounding it, so a
  packed image's decoys stop cutting pieces every few dozen bytes
  ([function bounds](docs/commands/fast-lane.md#function-bounds)).
- **`touches` finds code outside the exception directory:** leaf functions
  and the code packed images leave unlisted are scanned too, marked
  `unlisted`; `--reads` ranks by offsets read and drops struct copies
  ([touches](docs/commands/fast-lane.md#touches)).
- **`disasm --function` walks where no entry holds the address:** it lists
  to the function's last return instead of refusing
  ([disasm](docs/commands/fast-lane.md#disasm)).
- **A digest keeps `saved` and `next`:** a `disasm --save` too large to
  print still names its file, and a cut listing its resume address
  ([digests](docs/commands/console.md)).
- **`get`, `rows` and `entries` take a `Default__` name**, as `call` does
  (`get Default__GameSettings Scale`)
  ([class names](docs/commands/unreal.md#class-names)).
- **`search text` rows carry the string they hit** (`text`), so finding
  a string's start needs no reads
  ([search text](docs/commands/fast-lane.md#search-text)).
- **`each` rows carry the instance's `name`**
  ([each](docs/commands/unreal.md#each)).
- **IDA label import:** one undo step, a wait box that cancels, and
  `apply(path)` for headless sessions
  ([labels](docs/commands/unreal.md#labels-for-a-static-tool)).

### Fixed

- **`Host::backgroundSearch` is checked on every search:** once the worker
  had started, a host that turned the opt-in off still had searches run on
  it; now they scan in tick slices or are refused.
- **`name-find` and `refs` scan the whole name pool:** where obfuscated
  entries carry a header longer or shorter than their 22 characters, the walk
  stopped mid-block and missed about a sixth of the names. `name <index>`
  now returns empty for an index that is not an entry start instead of
  decoding text as a header.
- **Limits that refused real calls:** `find --limit N` gathers up to 1,000
  rows over pages; `disasm <addr> <count>` takes up to 1,024 instructions;
  `sig-bytes` cuts a length past 96 to 96 (`capped: true`) instead of
  refusing ([fast lane](docs/commands/fast-lane.md#disasm)).
- **IDA's `decompile_all()` waits for analysis first:** right after a load
  or `add_saved`, or in a script that loads and decompiles at once, queued
  analysis could leave the pseudocode incomplete.
- **No junk import names:** a DLL import with no name table is left
  unnamed in `disasm` and saves instead of read from resolved addresses.
- **Decoy exception entries no longer break function bounds:** unwind data
  of an unknown version counts as a plain entry, as the OS unwinder treats it.
- **`disasm --save` no longer exits the console on an empty listing:** it
  refuses with `start`, and a one-byte decoy entry or chunk is walked past
  ([saving a function](docs/commands/fast-lane.md#saving-a-function-for-a-static-tool)).
- **The Unreal pack's agent cannot crash on a guard page or hide a stack
  overflow:** its startup checks look at the memory region before reading,
  and name decoding catches only bad-memory faults.
- **A request after a timeout can no longer run half-written:** the agent
  could copy the shared request while the console overwrote it with the
  next one, and run a mix of both (a write at the wrong address). The
  request number is now a seqlock. Protocol change: rebuild and re-inject.
- **A stale IDA loader loaded newer saves silently:** the loader now
  refuses a row kind it does not know and says to copy the new
  `reagentlive_ida.py`, instead of loading the file without its chunks or names.
- **Functions whose exception entry begins early listed nothing:** a
  packed image can start an entry a few bytes before the code, on bytes
  that do not decode. `disasm --function` then lists from the address given,
  and `--callees` always lists from the call target, the entry bounding the end.
- **`touches` rows lost their `text` on long instructions:** its formatter
  buffer was 96 characters; it is now 256, as Zydis's examples use. The
  sweep also decodes operands only for instructions whose raw displacement
  can match (always for EVEX and MVEX, whose 8-bit displacement is scaled),
  and its length-only passes use Zydis's minimal decoder mode.
- **IDA label import refused every database loaded the default way:** it
  read the build from header bytes IDA does not load; it now reads the PE
  header IDA keeps.

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
- **Call recorder** ([unreal](docs/commands/unreal.md#call-recorder)):
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
