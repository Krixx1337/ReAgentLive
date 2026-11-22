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

### New

- **A function and its callees in one database:** `disasm --save
  --callees` saves each function it calls directly (one level, up to 16),
  `--labels` names functions, call targets and globals from an
  `export-labels` file, and the IDA loader stubs named addresses so calls
  read as names. `add_saved(path)` adds a later save to the open database.
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
  `frontends/ida/load_function.py`
  ([saving a function](docs/commands/fast-lane.md#saving-a-function-for-a-static-tool)).

### Improved

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
