# Hidden state and unreflected objects

Verb behaviour: native objects (`docs/commands/native-objects.md`),
memory (`docs/commands/memory.md`).

## An unreflected object

No reflection, no engine pack, or a native member behind a reflected one:

1. **What is it?** `peek <addr>` (its `region` says what memory it is): a `module` value in
   slot 0 is usually the vtable.
2. **Its class:** `vtable <addr>` (slot targets carry `module` and `rva`
   for the static tool); `vtable <addr> --rtti` where the image carries MSVC type
   information (engine game code often does not). A secondary base or
   interface part: `--offset N` (`func` on an interface reports it).
3. **Read through it:** `read <addr>->0x30->0x10 8 --type ptr`; the result's
   `chain` names a null or unreadable hop.
4. **What a slot does:** `disasm <target> --function`.

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
  between reflected members): `read <obj>+<pad> <len> --type f64`, then
  `--type f32`. World positions show as doubles near live locations, sizes
  as round numbers that match a reflected extent, timers as small floats
  that grow between two reads. Then find the code: `func <obj>
  <Class>:<any native function>` gives an exec in the class's own code, and
  `touches <exec> <offset>...` ranks the functions using the block; the
  writers are setup and tick. Name the fields from the decompiled writers,
  not from the values alone, then decode the whole block in one `read`
  with a layout type (`--type "u8 phase, pad 3, f32 duration, f32 elapsed,
  f64[3] start"`). No hand slicing of hex.
- **Name what you decoded, once:** `label <obj> 0x538 f64[3] Start`; `get --types`,
  `get` and `get <ref> Start` then show it in every later run. Re-check
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
  table storage): read the layout and stride from `get --types` or a static
  tool, `read` the allocation (one `batch` for several ranges), then `name` the FName keys.

## Narrowing by change

- Which bytes of a range react to an action: `mem-snap <owner> <len>`,
  `mem-diff unchanged f32 --keep` while nothing happens (drops noise), the
  user acts, `mem-diff decreased f32 --keep` (or `changed`, `increased`);
  repeat until a few rows remain, then capture them for the timing. Use the
  type the value really has, and start the snap aligned: elements step by
  its size from the snap's start.
- `mem-diff` is one read of the range, not one tick: confirm a candidate
  with a capture before naming it.

## Searching memory

- Narrow long searches (`help search`): the heap for structs (search a
  vtable pointer to find every copy), one module for code and static data,
  in the main module by default. A long one answers `state: running`: do
  other reads and poll `search-job <id>`; stop it with `--stop`.
- `search` matches exact bytes. For a value that moves, search while it
  rests at a known value (full, zero), or search the exact bytes a getter
  returned in the same `batch`.
