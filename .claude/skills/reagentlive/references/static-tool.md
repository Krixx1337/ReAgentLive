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

## One-time setup (the user's; ask, never edit their install)

- Copy `src/reagentlive/frontends/ida/reagentlive_ida.py` into
  `%APPDATA%\Hex-Rays\IDA Pro\loaders`, and copy it again after every
  ReAgentLive update: an old loader refuses new saves.
- Set `REAGENTLIVE_CONSOLE` to the console's path, or pass `console=` to
  `fetch`.
- Disable any IDA plugin that fails to load under idalib.

## The loop

1. **Save the first function by hand:**
   `{"verb": "disasm", "args": {"addr": "0x7ff600001000", "function": true, "save": {"file": "fn.jsonl", "callees": 1}}}`.
   Save from the function's start.
2. **Open that file in IDA** (the loader claims it): this database is
   the workspace for the whole investigation. Never one database per
   function.
3. **Per hop, one call:** `rl.fetch(addr, depth=1, decompile_dir=<dir>, grep=<regex>)`
   (load `rl` as in `docs/commands/ida-loader.md#from-an-automated-session`).
   Read its summary, open only the files it points to; the next address
   comes from the pseudocode. `depth` 2 or 3 only when the callees
   matter; `skipped` says what the depth or caps left out. A `FetchError`
   carries the console's refusal: fix the call it names.
4. **Before closing:** `rl.export_names(<names file>)` with a path, so
   later saves show what you named (without a path it opens a dialog and
   hangs a headless session).

- A `JUMPOUT(<addr>)` or cast `fn_<rva>(...)` left is code not saved:
  `fetch` it. A typed-pointer index (`p[27]`) is offset = index × size.
- `this->Field` comes from the layouts a save carries for the classes its
  labels name; for others, `layout-export` with `closure`, then
  `import_layouts` and `apply_this`.
- `add_saved` refuses when a saved function's start has no loaded bytes:
  save what it names, add again.

## Hygiene

- Each work cycle starts with `list_databases`: close every headless
  (idalib) database that is not the workspace or a reference database.
  Leaked workers pile up and die.
- Close the workspace when the investigation ends; before opening a new
  save under the same name, delete the database files beside it.
- After a game patch: a new full database (the RVAs change), then
  `labels-port` (the signatures reference) and import the labels again.
