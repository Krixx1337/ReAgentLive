# Commands by task

Which verb answers which question. Arguments and result keys: `help`
with `verb` (`{"verb": "help", "args": {"verb": "find"}}`). Behaviour:
`docs/commands/README.md`. Game-thread needs:
`docs/commands/unreal.md#off-the-engines-thread`. Session: `status`,
`modules`, `usage`, `batch`.

## Objects and reflection (Unreal pack: `docs/commands/unreal.md`)

| Question | Verb |
|---|---|
| Objects by name or class; one asset's subobjects | `find` (`cls`, `live`, `outer`) |
| Live instances per class; what spawned during an action | `census`, then `census` with `diff` |
| Name, class and ref of an address | `describe` |
| Which object and field hold an address (`search` hits) | `owner` (heap hits: add `within`) |
| How the engine reaches an object: the field path from a root | `route` |
| FName index to text / text to index | `name` / `name-find` |
| An object's layout / its values | `get` with `mode` `types` / `get` (filter it with `view`, or name `paths`) |
| Typed values along pointer and struct paths, one moment | `get` |
| One table over every instance of a class (one tick, one call) | `each` (fields), `call-each` (a getter) |
| Elements of an array, set or map | `entries` |
| A data table's rows; a curve table's keys, or its value at a time | `rows`, `rows` with `at` |
| Many assets read again and again (data assets, tables): to files once | `assets-export` (`dir`, `classes`), then grep the files |
| What a byte or enum field's number means | `enum` |
| Functions on a class; one function's entry and parameter block | `funcs`; `func` |
| Name an unreflected member for good | `member-name` |
| Name a native function or global for good | static tool name + `export_names`; `peek` with `label`; then `labels-sign` |
| Bring confirmed names to a patched build | `labels-port`; re-find only `missing` and `ambiguous` |
| Do this build's signed names still hold | `labels-check` (one call, read-only) |
| Did a patch move the fields code reads | `layout-export`, later `layout-check` |
| Typed structs for a static tool's decompiles | they come with the save (its methods' classes); `layout-export` with `closure`, then `import_layouts`, only for classes no label names |

## Calls, Blueprint, events

| Question | Verb |
|---|---|
| Run a getter / any reflected function | `call-pure` or `call-each` (approval only for the first call on a new host or target) / `call` (approval), `release` a kept result |
| What a Blueprint function reads and calls | `script` |
| Which Blueprints name a tag or name | `name-refs` |
| What a line from A to B hits | `line-trace` |
| What the target passed to a function | `calls-start`, `calls-read` (`stop` ends it; approval) |
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
| Change memory once / every tick / once with an undo | `write` / `write` with `hold` / with `undo` (approval) |
| Which timed writes are active; end them | `timed-writes` (read-only, but in `modify`); `write` with `clear` (`modify`, approval) |

## Over time

Sampling and captures: the capture-and-timing reference.

## Native code

Disassembly, xrefs, `touches`, `disp-scan`, signatures: the native-code
and signatures references.

## Refs

- Pass the literal ref a result prints (`<ref>` in this skill). A module
  address works as `{"module": "target.exe", "rva": "0x1234"}`
  (`docs/commands/console.md#addresses-and-refs`).
- `name-find` can return binary junk beside real names: skip rows with
  control characters, confirm with `{"verb": "name", "args": {"indices": [12345]}}`.
