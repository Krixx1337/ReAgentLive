# Hidden state and unreflected objects

Verb behaviour: native objects (`docs/commands/native-objects.md`),
memory (`docs/commands/memory.md`).

## An unreflected object

`peek` the address (a `module` value in slot 0 is usually the vtable),
`vtable` with `rtti` for its class and slot targets, and `read` through a
chain: `{"verb": "read", "args": {"addr": {"base": "0x1f2e3d4c50", "chain": ["0x30", "0x10"]}, "len": 8, "type": "ptr"}}`.
The result's `chain` names a null or unreadable hop. A secondary interface
part: `offset` (`func` reports it). What a slot does: `disasm` of its
target with `function`.

## Hidden state

- To find where a native getter keeps its value, sample the getter and the
  owner's raw bytes in one session during the action, and keep fields that
  match it within a small error. Then one approved write mid-action tells
  integrated state (it holds) from time-driven (it jumps back).
- Values a Blueprint reads from obfuscated native fields are often written
  by native code from hidden state in unreflected padding: capture the
  whole native class range across the action and look for transient fields
  (zero before, move, back to zero).
- Host globals: saved settings can lag the game-thread copy. Use the host
  DLL's linker map and its logged base, verify the live image against that
  DLL, then read the runtime copy.
- **A block of native state inside a reflected class's padding** (`Pad_N`
  between reflected members): `read` at `{"base": "<ref>", "offset": <pad>}`
  with `type` `f64`, then `f32`. World positions show as doubles near live locations, sizes
  as round numbers that match a reflected extent, timers as small floats
  that grow between two reads. Then find the code: `func` on the object
  with `function` `<Class>:<any native function>` gives an exec in the
  class's own code, and `touches` from that exec with the block's `offsets`
  ranks the functions using the block; the
  writers are setup and tick. Name the fields from the decompiled writers,
  not from the values alone, then decode the whole block in one `read`
  with a layout type (`"type": "u8 phase, pad 3, f32 duration, f32 elapsed,
  f64[3] start"`). No hand slicing of hex.
- **Name what you decoded, once:**
  `{"verb": "member-name", "args": {"object": "0x1f2e3d4c50#123:4", "offset": "0x538", "type": "f64[3]", "name": "Start"}}`;
  `get` (every `mode`, and `paths` `[["Start"]]`) then shows it in every later run. Re-check
  labels marked `stale` after a patch. Readable names (labels, the host's
  names file) are hints: trust offsets and types.
- **Prove a decoded motion model live.** Sample the moving thing and the
  block in one `batch` (a `get` of its location and a `read`) and test the
  model on every row (on the line between two stored points, progress equal
  to elapsed over duration). Then predict the end state and keep sampling
  until it happens.

## Structs and arrays

- For an instanced struct array, resolve the script struct by name and
  match its type pointer. Confirm the header, the matching entry and the
  pointed-to value together in one `batch` before relying
  on a saved data address.
- Containers reflection does not describe (native-only sets and maps,
  table storage): read the layout and stride from `get` with `mode` `types` or a static
  tool, `read` the allocation (one `batch` for several ranges), then `name` the FName keys.

## Narrowing and searching

- Which bytes react to an action: `mem-snap`, then `mem-diff` with
  `compare` `unchanged` while nothing happens (drops noise), the user acts,
  then `decreased`, `changed` or `increased`, with `keep`; repeat until a
  few rows remain. Use the type the value really has and start the snap
  aligned. `mem-diff` is one read, not one tick: confirm a candidate with a
  capture before naming it.
- `search` matches exact bytes: search while a moving value rests at a
  known value, or the exact bytes a getter returned in the same `batch`.
  Scope long searches; a long one answers `state: running`: poll
  `{"verb": "search-job", "args": {"job": 3}}`.
