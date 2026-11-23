# Commands by task

Which verb answers which question. Arguments, flags and result keys:
`help <verb>`. Behaviour and guards: the linked page. Which verbs need the
game thread: off the engine's thread (`docs/commands/unreal.md#off-the-engines-thread`).

## Session

| Question | Verb |
|---|---|
| Which packs, which thread, which build, tick rate | `status` |
| Exe and module ranges | `base`, `modules` |
| What this session cost | `stats` (usage log: `docs/commands/console.md#usage-log-and-stats`) |
| Many reads in one tick | `batch` (fewer round trips: `docs/commands/console.md#fewer-round-trips`) |

## Objects and reflection (Unreal pack: `docs/commands/unreal.md`)

| Question | Verb |
|---|---|
| Objects by name or class; one asset's subobjects | `find` (`--cls`, `--live`, `--outer`) |
| Live instances per class; what spawned during an action | `census`, then `census-diff` |
| Name, class and ref of an address | `describe` |
| FName index to text / text to index | `name` / `name-find` |
| An object's layout / its values | `fields` / `dump` (filter it) |
| Typed values along pointer and struct paths, one moment | `get` |
| One table over every instance of a class (one tick, one call) | `each` |
| Elements of an array, set or map | `entries` |
| A data table's rows | `rows` |
| What a byte or enum field's number means | `enum` |
| Functions on a class; one function's entry; its parameter block | `funcs`; `func`; `params` |
| Name an unreflected member for good | `label` |
| Name a native function or global for good | static tool name + `export_names`; `peek --label` |
| Did a patch move the fields code reads | `layout-export`, later `layout-check` |

## Calls, Blueprint, events

| Question | Verb |
|---|---|
| Run a getter / any reflected function | `call-pure` (approval only for the first call on a new host or game) / `call` (approval), `release` a kept result |
| What a Blueprint function reads and calls | `script` |
| Which Blueprints name a tag or name | `refs` |
| What a line from A to B hits | `trace` |
| What the game passed to a function | `calls-start`, `calls-read`, `calls-stop` (approval) |
| Get the game-thread tick | `game-thread` (approval for `on`) |

## Memory (`docs/commands/memory.md`, `docs/commands/native-objects.md`)

| Question | Verb |
|---|---|
| Bytes, typed values, a pointer chain, a mixed layout | `read` (`--type`) |
| Several ranges in one tick | `read-batch` |
| What qwords hold (object, module, heap, value) | `peek` |
| What memory an address is | `region` |
| Where a value or string lives | `search` |
| Which bytes of a range react to an action | `mem-snap`, then `mem-diff` |
| A native object's class, bases, virtual table | `rtti`, `vtable` |
| Change memory once / every tick / once with an undo | `write` / `pin` / `trial` (approval); `pin-list`, `pin-clear`, `trial-list`, `trial-clear` |

## Over time (`docs/commands/memory.md#watch`, `docs/commands/capture.md`)

| Question | Verb |
|---|---|
| Memory or a reflected field over seconds to minutes | `watch-start`, `watch-read`, `watch-stop` |
| An object's fields before and after an action | `snap`, then `dump --changed` |
| Every tick for a few seconds, holding the caller | `watch`, `record` (blocking; interfaces reference) |

## Native code (fast lane: `docs/commands/fast-lane.md`)

| Question | Verb |
|---|---|
| What code is here | `disasm` |
| What points here | `xrefs` |
| Which functions use these member offsets | `touches` |
| Where a string is | `search text` |
| Is a signature unique and does it land there | `sig-check`, `sig-file`, `sig-bytes` |
| Names for a static tool | `export-labels`, `peek --label` |

## Refs and addresses

- Pass the literal ref a result prints (`<ref>` in this skill).
- Addresses inside a module print `module` and `rva`; `<module>+<rva>`
  works as an address (addresses and refs: `docs/commands/console.md#addresses-and-refs`).
- `name` takes many indices on one line. `name-find` can return binary
  junk beside real names: skip rows with control characters, confirm with
  `name <index>`.
