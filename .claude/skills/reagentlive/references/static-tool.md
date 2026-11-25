# Static tool: decompiling live functions in IDA

Loader behaviour: `docs/commands/ida-loader.md`; what a save holds:
`docs/commands/fast-lane.md#saving-a-function-for-a-static-tool`.

## When

- A function needs decompiling, types or its callees: use IDA on saves.
  One address, a constant, what points here: the fast lane
  (the native-code reference), no database.
- No analysed database of this build, or not worth one (hours on a large
  image, again after every patch): one workspace database of saves.
- A full database of the same build exists: open it at `module` + `rva`,
  `import_labels` the labels files.
- A UFunction's native code: `ue_func`'s `exec` is the thunk that unpacks
  the parameters, not the body. Save from `exec` with `callees` 1: the
  loader names the body `Class::Method` and types its `this`
  (`bodies_named`). None named: the thunk reaches the body through the
  vtable (an indirect call through `[reg+0xA50]`, reg loaded from `this`):
  slot = offset / 8, so `vtable` with `slots` `[330]` on the live object
  gives the address;
  `rl.fetch` it, then `rl.name(<addr>, 'Class::Method')`. A thunk save
  ending `stop` `walk end` is normal: small thunks have no exception
  entry (`docs/commands/fast-lane.md`).

## Setup (the user's; ask, never edit their install)

- Before the first `fetch` of a session run `python -I tools/ida_setup.py`
  (read-only): `stale` or `missing`: give the user its `why` and ask them
  to run it with `--install` (`docs/commands/ida-loader.md`;
  `docs/agents/setup.md#9-the-ida-side-once-per-machine` also writes the
  machine record's `ida`). In an IDA session open since an update,
  `rl.reload()` first.
- Never set `REAGENTLIVE_CONSOLE` on the user's machine yourself.

## The loop

1. **Save the first function by hand:**
   `{"verb": "disasm", "args": {"addr": "0x7ff600001000", "whole": true, "save": {"file": "<abs dir>/fn.jsonl", "callees": 1}}}`.
   Save from the function's start, to an absolute path (a relative one
   lands in the console's working folder).
2. **Open that file in IDA** (the loader claims it): this database is
   the workspace for the whole investigation. Never one database per
   function. Then `rl.workspace()`: the labels files in. Most rows land
   `outside` a save database; that is not a failure. Then
   `rl.decompile_to(<the saved starts>, <dir>, grep=<regex>)`: the first
   save's own pseudocode (`fetch` decompiles only what it adds).
3. **Per hop, one call:** `rl.fetch(addr, depth=1, decompile_dir=<dir>, grep=<regex>)`
   (`import reagentlive_ida as rl`, in any database once set up).
   Read its summary, open only the files it points to; the next address
   comes from the pseudocode. `depth` 0 for the function alone, 2 or 3
   only when the callees matter; `skipped` says what the depth or caps left out. A `FetchError`
   carries the console's refusal: fix the call it names.
4. **Name as you go** (`rl.name(<addr>, 'Class::Method')`: your name, and
   `this` typed when the save holds the class), then **before closing:**
   `rl.finish()`: your names into the host's names file, then signed, so
   later saves show them and they survive a patch. Report its
   `unsigned_rows` to the user; then close the database.

- A `JUMPOUT(<addr>)` or cast `fn_<rva>(...)` left is code not saved:
  `fetch` it. A typed-pointer index (`p[27]`) is offset = index × size.
- `this->Field` comes from the layouts a save carries for the classes its
  labels name; for others, `layout-export` with `closure`, then
  `import_layouts` and `apply_this`.
- `add_saved` refuses when a saved function's start has no loaded bytes:
  save what it names, add again.

## Hygiene

- Each work cycle starts with `list_databases`: close every headless
  database that is not the workspace or a reference; leaked workers pile
  up. Close the workspace when done, and delete stale database files
  before reopening a save under the same name.
- After the target updates: a new full database (the RVAs change), then
  `labels-port` and import the labels again.
