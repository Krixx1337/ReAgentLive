# Interfaces: which one, and the verb in each

Every interface sends the same verbs with the same argument keys and gets
the same result keys. APIs, setup and options:
[clients](https://github.com/Krixx1337/ReAgentLive/blob/main/docs/client.md); console mechanics:
[console](https://github.com/Krixx1337/ReAgentLive/blob/main/docs/commands/console.md).

## Which one

| Task | Use |
|---|---|
| One lookup, read, describe, or a few calls | MCP tool |
| Reads that must share a tick | MCP `batch` (or `get`, `read-batch`); check `same_tick` |
| A result came back as a `digest` | the call its `next` names, or `budget` for what `next` cannot narrow |
| One value or getter across every instance of one class (T3) | `each`: one call, one tick, the whole table; Python only when `each` cannot express it (several classes, logic beyond a table) |
| The next step loops, compares, intersects, sweeps instances or diffs many objects | Python: generated functions on a `Session`; print only the summary |
| One step's output feeds the next more than twice | Python |
| A capture over seconds | `watch-start`, then `watch-read` / `watch-stop` (MCP or Python) |
| Blocking `watch` or `record` | console or Python only |
| MCP not registered | console `-c` with literal refs |
| A verb's keys before scripting | `help <verb>` (console, `s.help`, or the MCP `help` tool) |
| Replay a record file | console `--replay <file>` |
| Writes, calls, pins, `game-thread on` | any interface, after approval (skill rules) |

Rule: MCP first; Python when the next step is a loop or a computation over
results; the console when MCP is absent or for a human. The C++ client is
for building tools, not for investigating.

## Naming rule

- **MCP tool:** the verb if its origin is `core`, else `<origin>_<verb>`;
  `-` becomes `_` (`ue_find`, `sig_check`, `host_world`).
- **Python:** core on `c` (`c.read`), others under the origin
  (`c.ue.find`, `c.host.world`); `-` and `.` become `_`; a Python keyword
  gets a trailing `_`. Positionals in order, flags keyword-only.
- **Arguments** are the row keys everywhere: MCP argument, Python keyword,
  JSON call key, typed flag without `--`. `help <verb>` lists them.
- **Batch items** name verbs as the console writes them (`sig-check`, not
  `sig_check`), in MCP and Python alike.

| Verb | MCP tool | Python | Console line |
|---|---|---|---|
| `find` | `ue_find` `{text:"Pawn", cls:"Pawn"}` | `c.ue.find('Pawn', cls='Pawn')` | `find Pawn --cls Pawn` |
| `read` | `read` `{addr:"0x7ff600001000", len:16, type:"u32"}` | `c.read(0x7ff600001000, 16, type='u32')` | `read 0x7ff600001000 16 --type u32` |
| `get` | `ue_get` `{object:"<ref>", paths:["Health","Controller.PlayerState"]}` | `c.ue.get(ref, ['Health', 'Controller.PlayerState'])` | `get <ref> Health Controller.PlayerState` |
| `sig-check` | `sig_check` `{pattern:"48 8b ?? 89", module:"game.exe"}` | `c.sig_check('48 8b ?? 89', module='game.exe')` | `sig-check 48 8b ?? 89 --module game.exe` |
| `census-diff` | `ue_census_diff` `{cls:"Actor", limit:0}` | `c.ue.census_diff(cls='Actor', limit=0)` | `census-diff --cls Actor --limit 0` |
| `game-thread` | `ue_game_thread` `{action:"on"}` | `c.ue.game_thread('on')` | `game-thread on` |
| `batch` | `batch` `{calls:[{verb:"read",args:{addr:"0x10"}},{verb:"base"}]}` | `s.batch_results([('read', {'addr': 0x10}), ('base', {})])` | `batch { read 0x10; base }` |
| `watch` | none: `watch_start`, `watch_read`, `watch_stop` | `c.watch(2, ['0x...:4'])` (blocks) | `watch 2 0x...:4` |
| host verb `world` | `host_world` | `c.host.world()` | `world` |

## MCP tools

- **Literal refs only.** Pass the `ref` a result carries; `$N` is refused.
- **Marks drive the client's prompts:** `writes` rows are destructive,
  the rest read-only. Marked `writes` though the skill treats some uses as
  reads: `each` (field paths are reads; a getter column calls) and
  `game-thread` (even `status` prompts). `call-pure` is read-only, but its first call on a new host or in a new game still needs approval (SKILL.md rules).
- **`batch` refuses writing verbs inside it** (`write`, `pin`, `trial`,
  `call`, `each`, `calls-start`, `game-thread`, ...): send each as its own
  tool so it is approved. Use `batch` for reads at one moment.
- **Blocking verbs are left out** (`watch`, `record`, a host's countdown
  capture): use `watch-start` / `watch-read` / `watch-stop`; for `record`
  use Python or the console.
- **Each call is its own console run.** Nothing holds state between calls
  but literal refs, pin, trial and capture ids, and the console's cache
  file. The console can be rebuilt between calls; reconnect only if the
  client ignores the tool-list notice and a tool's arguments changed.
- Output past the budget comes back as a `digest`: efficiency reference.

## Python

- Generate the module once per console build; setup recipe in the
  console-driving reference, API in [clients](https://github.com/Krixx1337/ReAgentLive/blob/main/docs/client.md#generated-python-functions).
- JSON calls refuse `$N`: take `rows[0]['ref']` and pass it on.
- Results are strict records: a wrong key raises at once. Never wrap result
  access in `try`/`except` or a default.
- Blocking verbs (`watch`, `record`) are callable and block the script for
  their duration. `Session` has no timeout: a call waits until the console
  answers or exits.
- `caller='tool'` when you read every result yourself; the default
  `script` keeps a script's output apart in `stats`.

## Console

- Typed lines or JSON calls; `$N` works in typed lines only
  ([addresses and refs](https://github.com/Krixx1337/ReAgentLive/blob/main/docs/commands/console.md#addresses-and-refs)).
- Quoting, parsing and one-shot use: console-driving reference.
