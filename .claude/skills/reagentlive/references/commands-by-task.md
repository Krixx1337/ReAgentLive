# Commands by task

Which verb answers which question. Arguments and result keys: `help`
with `verb` (`{"verb": "help", "args": {"verb": "find"}}`). Behaviour and guards: the linked page. Which verbs need the
game thread: off the engine's thread (`docs/commands/unreal.md#off-the-engines-thread`).

## Session

| Question | Verb |
|---|---|
| Which packs, which thread, which build, tick rate | `status` |
| Exe and module ranges | `modules` (exe first) |
| What this session cost | `usage` (usage log: `docs/commands/console.md#usage-log-and-usage`) |
| Many reads in one tick | `batch` (fewer round trips: `docs/commands/console.md#fewer-round-trips`) |

## Objects and reflection (Unreal pack: `docs/commands/unreal.md`)

| Question | Verb |
|---|---|
| Objects by name or class; one asset's subobjects | `find` (`cls`, `live`, `outer`) |
| Live instances per class; what spawned during an action | `census`, then `census` with `diff` |
| Name, class and ref of an address | `describe` |
| FName index to text / text to index | `name` / `name-find` |
| An object's layout / its values | `get` with `mode` `types` / `get` (filter it with `view`, or name `paths`) |
| Typed values along pointer and struct paths, one moment | `get` |
| One table over every instance of a class (one tick, one call) | `each` (fields), `call-each` (a getter) |
| Elements of an array, set or map | `entries` |
| A data table's rows; a curve table's keys, or its value at a time | `rows`, `rows` with `at` |
| Many assets read again and again (data assets, tables): to files once | `assets-export` (`dir`, `cls`), then grep the files |
| What a byte or enum field's number means | `enum` |
| Functions on a class; one function's entry and parameter block | `funcs`; `func` |
| Name an unreflected member for good | `member-name` |
| Name a native function or global for good | static tool name + `export_names`; `peek` with `label`; then `labels-sign` |
| Bring confirmed names to a patched build | `labels-port`; re-find only `missing` and `ambiguous` |
| Did a patch move the fields code reads | `layout-export`, later `layout-check` |
| Typed structs for a static tool's decompiles | they come with the save (its methods' classes); `layout-export` with `closure`, then `import_layouts`, only for classes no label names |

## Calls, Blueprint, events

| Question | Verb |
|---|---|
| Run a getter / any reflected function | `call-pure` or `call-each` (approval only for the first call on a new host or game) / `call` (approval), `release` a kept result |
| What a Blueprint function reads and calls | `script` |
| Which Blueprints name a tag or name | `name-refs` |
| What a line from A to B hits | `line-trace` |
| What the game passed to a function | `calls-start`, `calls-read`, `calls-stop` (approval) |
| Get the game-thread tick | `game-thread` (approval for `on`) |

## Memory (`docs/commands/memory.md`, `docs/commands/native-objects.md`)

| Question | Verb |
|---|---|
| Bytes, typed values, a pointer chain, a mixed layout | `read` (`type`; a chain as `addr`) |
| Several ranges in one tick | `batch` of `read`s, or `get` |
| What qwords hold (object, module, heap, value) | `peek` |
| What memory an address is | `read`/`peek` (`region`) |
| Where a value or string lives | `search` (long ones: `search-job` with its `job`) |
| Which bytes of a range react to an action | `mem-snap`, then `mem-diff` |
| A native object's class, bases, virtual table | `vtable` with `rtti` |
| Change memory once / every tick / once with an undo | `write` / `write` with `hold` / with `undo` (approval); `timed-writes`, `timed-writes` with `clear` |

## Over time (`docs/commands/memory.md#watch`, `docs/commands/capture.md`)

| Question | Verb |
|---|---|
| Memory or a reflected field over seconds to minutes | `watch-start`, `watch-read`, `watch-stop` |
| An object's fields before and after an action | `get` with `mode` `snap`, then `changed` |
| Every tick for a few seconds, holding the caller | Python `s.watch(seconds, ranges)` (start, wait, stop) |

## Native code (fast lane: `docs/commands/fast-lane.md`)

| Question | Verb |
|---|---|
| What code is here | `disasm` |
| What points here | `xrefs` |
| Which functions use these member offsets | `touches` |
| Where a string is | `search` with `type` `text` |
| Is a signature unique and does it land there | `sig-check` (`file` for many), `sig-bytes` |
| Names for a static tool | `labels-export`, `peek` with `label` |

## Refs and addresses

- Pass the literal ref a result prints (`<ref>` in this skill).
- Addresses inside a module print `module` and `rva`;
  `{"module": "game.exe", "rva": "0x1234"}` works as an address (addresses
  and refs: `docs/commands/console.md#addresses-and-refs`).
- `name` takes many `indices` in one call. `name-find` can return binary
  junk beside real names: skip rows with control characters, confirm with
  `{"verb": "name", "args": {"indices": [12345]}}`.
