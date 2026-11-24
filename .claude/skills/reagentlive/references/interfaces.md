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
| A capture over seconds | `watch-start`, then `watch-read` (with `stop` to end); a script that waits: `s.watch(seconds, ranges)` |
| MCP not registered, or a human at the shell | the console `-c` |
| Writes, calls, `game-thread` `on` | any interface, approval: SKILL.md rules |

## Packs

`explore` is always on; `native`, `observe` and `modify` (writes, `call`,
`calls-start`, `game-thread`) when the task needs one. `packs` lists what
each holds.

- **Enable before the first call that needs it:** MCP: the `packs` tool,
  `{"action": "enable", "pack": "native"}`; Python: `Session(console,
  packs=[...])`; console: `--packs`. Enable `modify` only inside the
  user's ceiling; past it, ask.
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
- Example: `ue_get` `{"object": "0x1f2e3d4c50#123:4", "paths": [["Health"]]}`
  is `c.ue.get(ref, paths=[['Health']])`; a batch is
  `items, same_tick = s.batch_results([('read', {'addr': '0x10'})])`.

## MCP tools

- **Marks drive the client's prompts:** `writes` rows are destructive,
  the rest read-only, exact per verb (`each` reads; `call-each` calls).
- **Left out:** verbs `help` marks `blocks` (`run`, a host's countdown
  capture): use Python or the console.
- **Each call is its own console run:** only literal refs, timed-write
  and capture ids and the cache file carry over. Reconnect only if the
  client ignores the tool-list notice and a tool's arguments changed.
- A `digest` in place of a result: the efficiency reference.

## Console

- One-shot: `<console> --caller tool --packs explore,native -c '{"verb": "find", "args": {"cls": "Pawn", "view": {"limit": 5}}}'`
  (or a JSON list of calls, or `-c @calls.jsonl`, one call per line); exit
  code 1 on any error. Independent calls you read go here; a chain where
  one step feeds the next goes to Python.
- **`--caller tool`** whenever you read the output: it gives you the
  result cap and digests, and `usage` counts it as context.
- **`--packs`:** `-c` runs all four by default. Pass `explore,native` (add
  `observe`) for read-only work; `modify` only for an approved write.
- `-c` takes ASCII only: escape others as `\u` in the JSON, or use
  `-c @<file>`. PowerShell: single-quote the JSON; Windows PowerShell 5
  strips inner `"` from native arguments, so use `-c @<file>`.
- Stdout is one JSON record per call (stderr: wait notices, countdowns).
  Parse per line, check `ok` first, read only `result`, take key names
  from `help`, never from memory.

## Python

Generate the module once per console build (no target needed):

```
python <reagentlive>/src/reagentlive/frontends/python/reagentlive_gen.py --console <console exe> -o <scratch>/console_api.py
```

```python
import sys
sys.path[:0] = [r'<reagentlive>/src/reagentlive/frontends/python', r'<scratch>']
from reagentlive_session import Session
from console_api import Console
with Session(r'<console exe>', packs=['explore', 'native', 'observe']) as s:
    c = Console(s)
```

- **Strict results:** a wrong key raises `ResultKeyError` naming the valid
  keys. Never wrap result access in `try`/`except` or a default: a check
  that cannot run must fail, not pass. Look keys up with `s.help('<verb>')`.
- `StaleTableError` at `Console(s)`: the console was rebuilt; generate
  again.
- `caller='tool'` when you read every result yourself (a result past the
  cap is a `Digest`, also in `s.digest`); the default `script` keeps a
  script's output apart in `usage`.
- Page with the console's cursor: pass `next_cursor` back as `view.cursor`,
  or raise `view.limit`; check a list a verb can cut before treating it as
  complete.
- `Session` has no timeout and blocking verbs (`run`) block the script.
  Long work: `s.call('run', {'plan': p, 'background': True})`, keep
  exploring, poll `s.run_status(id)`, then `s.run_read(id)` and
  `s.run_stop(id)`; through MCP the `run` tool, then `run_status`,
  `run_read`, `run_stop`.
- Stop after three identical refusals and fix the call. Never write your
  own JSON parser or session loop.
