# Interfaces: which one, and the verb in each

Every interface sends the same verbs with the same argument keys and gets
the same result keys. APIs and options: `docs/client.md`; console
mechanics: `docs/commands/console.md`.

## Which one

| Task | Use |
|---|---|
| One lookup, read, describe, or a few calls | MCP tool |
| Reads that must share a tick | MCP `batch` (or `get`); check `same_tick` |
| Fields or a getter across every instance of one class (T3) | `each` (fields) or `call-each` (a getter): one call, one tick, the whole table |
| The next step loops, compares, intersects or diffs many objects | Python: generated functions on a `Session`; print only the summary |
| The same calls repeated over time, or an investigation to replay | a `run` plan (a `batch` step with `repeat`), from Python or the console `-c`; MCP leaves `run` out (it blocks) |
| A capture over seconds | `watch-start`, then `watch-read` / `watch-stop`; a script that waits: `s.watch(seconds, ranges)` |
| MCP not registered, or a human at the shell | the console `-c`: load the console-driving reference |
| Writes, calls, `game-thread` `on` | any interface, approval: SKILL.md rules |

The C++ client is for building tools, not for investigating.

## Tool packs

| Pack | Holds |
|---|---|
| `explore` (always on) | `find`, `get`, `each`, `rows`, `name-find`, `read`, `peek`, `script`, `name-refs`... |
| `native` | `disasm`, `xrefs`, `touches`, `vtable`, `sig-*`, `search`, `search-job`, `member-name`, `labels-export`, `assets-export`, `labels-*`, `layout-*` |
| `observe` | `watch-*`, `census`, `mem-snap`, `mem-diff`, `line-trace`, `call-pure`, `call-each` |
| `modify` | `write`, `timed-writes`, `call`, `release`, `calls-start`/`-read`/`-stop`, `game-thread` |

- **Enable before the first call that needs it:** MCP: the `packs` tool,
  `{"action": "enable", "pack": "native"}`; Python: `Session(console,
  packs=[...])`; console: `--packs`. A `modify` verb (a write, `call`,
  `calls-start`, `game-thread`) needs `modify` on: enable it when it is
  inside the ceiling, else ask the user.
- **`outside` the ceiling, or a refusal of kind `pack`:** the user set
  it; never work around it. Ask the user to restart with the pack in
  `--allow` (MCP) or `--packs`.
- MCP `batch` refuses items from packs that are off, and every verb
  `help` marks `writes`: send those as their own tools.

## Names

- **MCP tool:** the verb if its origin is `core`, else `<origin>_<verb>`;
  `-` becomes `_` (`ue_find`, `sig_check`, `host_world`).
- **Python:** core on `c` (`c.read`), others under the origin
  (`c.ue.find`, `c.host.world`); `-` becomes `_`. Required args
  positional in row order, the rest keyword-only.
- **Arguments** are the row keys everywhere; nested objects (`view`,
  `scope`, `undo`, `save`) are dicts in Python. `batch` items name verbs
  as the console writes them (`sig-check`).

| Verb | MCP tool | Python |
|---|---|---|
| `find` | `ue_find` `{"cls": "Pawn", "view": {"limit": 5}}` | `c.ue.find(cls='Pawn', view={'limit': 5})` |
| `get` | `ue_get` `{"object": "0x1f2e3d4c50#123:4", "paths": [["Health"]]}` | `c.ue.get(ref, paths=[['Health']])` |
| `sig-check` | `sig_check` `{"pattern": "48 8b ?? 89", "module": "game.exe"}` | `c.sig_check(pattern='48 8b ?? 89', module='game.exe')` |
| `batch` | `batch` `{"calls": [{"verb": "read", "args": {"addr": "0x10"}}]}` | `s.batch_results([('read', {'addr': '0x10'})])` |
| host verb `world` | `host_world` `{}` | `c.host.world()` |

## MCP tools

- **Marks drive the client's prompts:** `writes` rows are destructive,
  the rest read-only, exact per verb (`each` reads; `call-each` calls).
- **Left out:** verbs `help` marks `blocks` (`run`, a host's countdown
  capture): use Python or the console.
- **Each call is its own console run:** only literal refs, timed-write
  and capture ids and the cache file carry over. Reconnect only if the
  client ignores the tool-list notice and a tool's arguments changed.
- A `digest` in place of a result: the efficiency reference.
