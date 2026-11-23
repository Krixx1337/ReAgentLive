# Interfaces: which one, and the verb in each

Every interface sends the same verbs with the same argument keys and gets
the same result keys. APIs, setup and options:
clients (`docs/client.md`); console mechanics:
console (`docs/commands/console.md`).

## Which one

| Task | Use |
|---|---|
| One lookup, read, describe, or a few calls | MCP tool |
| Reads that must share a tick | MCP `batch` (or `get`); check `same_tick` |
| A result came back as a `digest` | the call its `next` names, or `budget` for what `next` cannot narrow |
| Fields or a getter across every instance of one class (T3) | `each` (fields) or `call-each` (a getter): one call, one tick, the whole table; Python only when neither can express it (several classes, logic beyond a table) |
| The next step loops, compares, intersects, sweeps instances or diffs many objects | Python: generated functions on a `Session`; print only the summary |
| One step's output feeds the next more than twice | Python |
| A capture over seconds | `watch-start`, then `watch-read` / `watch-stop` (MCP or Python) |
| A script that waits out a capture | Python `s.watch(seconds, ranges)` |
| MCP not registered | console `-c` with JSON calls and literal refs |
| A verb's keys before scripting | `help` with `verb` (console, `s.help('get')`, or the MCP `help` tool) |
| Writes (timed ones too), calls, `game-thread` `on` | any interface, after approval (skill rules) |

Rule: MCP first; Python when the next step is a loop or a computation over
results; the console when MCP is absent or for a human. The C++ client is
for building tools, not for investigating.

## Tool packs

| Job | Pack | Holds |
|---|---|---|
| Find objects, read fields, data, names, memory | `explore` (always on) | `find`, `get`, `each`, `rows`, `name-find`, `read`, `peek`... |
| Read code, signatures, labels across builds | `native` | `disasm`, `xrefs`, `vtable`, `sig-check`, `labels-sign`... |
| Watch over time without changing anything | `observe` | `watch-start`, `census`, `mem-snap`, `trace`, `call-pure`, `call-each` |
| Change the target or call into it | `modify` | `write`, `call`, `calls-start`, `game-thread`... |

`help` groups verbs by pack (`help` with `json`: each row's `pack`).

- **MCP:** `packs` lists each pack, its state and its tools; enable one
  before the first call that needs it (`{"action": "enable", "pack":
  "native"}`) and the tool list refreshes. `outside` is the ceiling the
  user set when starting the server: never work around it; ask the user,
  who restarts it with the pack in `--allow`.
- **A refusal of kind `pack`** (console, Python, a `batch` item): the verb
  is past that console's `--packs`. Same rule: enable inside the ceiling,
  ask the user past it.
- **Python:** `Session(console, packs=['explore', 'native', 'observe'])`
  for a script that must not change the target; add `modify` only for an
  approved write.

## Naming rule

- **MCP tool:** the verb if its origin is `core`, else `<origin>_<verb>`;
  `-` becomes `_` (`ue_find`, `sig_check`, `host_world`).
- **Python:** core on `c` (`c.read`), others under the origin
  (`c.ue.find`, `c.host.world`); `-` and `.` become `_`; a Python keyword
  gets a trailing `_`. Required args positional in row order, the rest
  keyword-only.
- **Arguments** are the row keys everywhere: MCP argument, Python keyword,
  JSON call key. Nested objects (`view`, `scope`, `undo`, `save`) are dicts
  in Python. `help` with `verb` lists them.
- **Batch items** name verbs as the console writes them (`sig-check`, not
  `sig_check`), in MCP and Python alike.

| Verb | MCP tool | Python | Console (`-c`) |
|---|---|---|---|
| `find` | `ue_find` `{"text": "Pawn", "cls": "Pawn"}` | `c.ue.find(text='Pawn', cls='Pawn')` | `{"verb": "find", "args": {"text": "Pawn", "cls": "Pawn"}}` |
| `read` | `read` `{"addr": "0x7ff600001000", "len": 16, "type": "u32"}` | `c.read('0x7ff600001000', len=16, type='u32')` | `{"verb": "read", "args": {"addr": "0x7ff600001000", "len": 16, "type": "u32"}}` |
| `get` | `ue_get` `{"object": "0x1f2e3d4c50#123:4", "paths": [["Health"], ["Controller", "PlayerState"]]}` | `c.ue.get(ref, paths=[['Health'], ['Controller', 'PlayerState']])` | `{"verb": "get", "args": {"object": "0x1f2e3d4c50#123:4", "paths": [["Health"], ["Controller", "PlayerState"]]}}` |
| `sig-check` | `sig_check` `{"pattern": "48 8b ?? 89", "module": "game.exe"}` | `c.sig_check(pattern='48 8b ?? 89', module='game.exe')` | `{"verb": "sig-check", "args": {"pattern": "48 8b ?? 89", "module": "game.exe"}}` |
| `census` | `ue_census` `{"cls": "Actor", "diff": true, "view": {"limit": 0}}` | `c.ue.census(cls='Actor', diff=True, view={'limit': 0})` | `{"verb": "census", "args": {"cls": "Actor", "diff": true, "view": {"limit": 0}}}` |
| `game-thread` | `ue_game_thread` `{"action": "on"}` | `c.ue.game_thread('on')` | `{"verb": "game-thread", "args": {"action": "on"}}` |
| `batch` | `batch` `{"calls": [{"verb": "read", "args": {"addr": "0x10"}}, {"verb": "modules"}]}` | `s.batch_results([('read', {'addr': '0x10'}), ('modules', {})])` | `{"verb": "batch", "args": {"calls": [{"verb": "read", "args": {"addr": "0x10"}}, {"verb": "modules"}]}}` |
| `watch-start` | `watch_start` `{"duration": 2, "ranges": [{"object": "0x1f2e3d4c50#123:4", "field": ["Health"]}]}` | `c.watch_start(2, [{'object': ref, 'field': ['Health']}])`; blocking: `s.watch(2, [{'object': ref, 'field': ['Health']}])` | `{"verb": "watch-start", "args": {"duration": 2, "ranges": [{"object": "0x1f2e3d4c50#123:4", "field": ["Health"]}]}}` |
| host verb `world` | `host_world` `{}` | `c.host.world()` | `world` as the call's `verb`, no args |

## MCP tools

- **Literal refs only.** Pass the `ref` a result carries; `$N` is refused.
- **Marks drive the client's prompts:** `writes` rows are destructive,
  the rest read-only, and exact per verb: `each` reads fields only (a
  getter over every instance is `call-each`), `status`
  shows the game-thread mode, `game-thread` only arms and removes it. `call-pure` is read-only, but its first call on a new host or in a new game still needs approval (SKILL.md rules).
- **`batch` refuses writing verbs inside it** (every verb `help` marks
  `writes`, `game-thread` too): send each as its own tool so
  it is approved. Use `batch` for reads at one moment.
- **Blocking verbs are left out** (`help` marks them `blocks`: a host's
  countdown capture): use the console or Python. Captures are
  `watch-start` / `watch-read` / `watch-stop`.
- **Each call is its own console run.** Nothing holds state between calls
  but literal refs, timed-write and capture ids, and the console's cache
  file. The console can be rebuilt between calls; reconnect only if the
  client ignores the tool-list notice and a tool's arguments changed.
- Output past the budget comes back as a `digest`: efficiency reference.

## Python

- Generate the module once per console build; setup recipe in the
  console-driving reference, API in clients (`docs/client.md#generated-python-functions`).
- JSON calls refuse `$N`: take `rows[0]['ref']` and pass it on.
- Results are strict records: a wrong key raises at once. Never wrap result
  access in `try`/`except` or a default.
- Blocking verbs (a host's) are callable and block the script for their
  duration; `s.watch(seconds, ranges)` is a blocking capture. `Session` has no timeout: a call waits until the console
  answers or exits.
- Start a read-only script's session with its packs (Tool packs above):
  a typo then cannot reach a write.
- `caller='tool'` when you read every result yourself; the default
  `script` keeps a script's output apart in `stats`.

## Console

- JSON calls only, one per line, as in the table above; no `$N`: pass
  the literal ref a result printed
  (addresses and refs: `docs/commands/console.md#addresses-and-refs`).
- Quoting, parsing and one-shot use: console-driving reference.
