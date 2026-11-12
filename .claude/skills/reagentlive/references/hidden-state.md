# Hidden state and unreflected objects

## An unreflected object

No reflection, no engine pack, or a native member behind a reflected one:

1. **What is it?** `region <addr>`: `private` (heap), `rodata` (a vtable or
   constant), `code`, `mapped`. `peek <addr>` labels each qword: a
   `module` value in slot 0 is usually the vtable.
2. **Its virtual table:** `vtable <addr>`; slot targets have `module` and
   `rva` for the static tool. An interface part or secondary base:
   `vtable <addr> --offset N` (`func` on an interface reports the offset).
   `--resolve` shows where jump stubs lead.
3. **Read through it:** `read <addr>->0x30->0x10 8 --type ptr` follows
   pointers in one command; the result's `chain` names a null or unreadable
   hop. Strings: `--type cstr` / `--type wstr`. Never write through a chain:
   resolve it, read, then write to that address with `--expect`.
4. **What a slot does:** `disasm <target> --function`.

## Hidden state

- To find where a native getter keeps its value, sample the getter and the
  owner's raw bytes in one session during the action, and keep fields that
  match it within a small error. Then one approved write mid-action tells
  integrated state (it holds) from time-driven (it jumps back).
- Values a Blueprint reads from obfuscated native fields are often written by
  native code from hidden state in unreflected padding: capture the whole
  native class range across the action and look for transient fields (zero
  before, move, back to zero).
- Host globals: saved settings can lag the game-thread copy. Use the host
  DLL's linker map and its logged base, verify the live image against that
  DLL, then read the runtime copy with ReAgentLive.
- **A block of native state inside a reflected class's padding** (`Pad_N`
  between reflected members): `read <obj>+<pad> <len> --type f64`, then
  `--type f32`. World positions show as doubles near live locations, sizes as
  round numbers that match a reflected extent, timers as small floats that
  grow between two reads. Then find the code: `func <obj> <Class>:<any
  native function>` gives an exec in the class's own code, and
  `touches <exec> <offset>...` ranks the functions using the block; the
  writers are setup and tick. Name the fields from the decompiled writers,
  not from the values alone, then decode the whole block in one command:
  `read <obj>+0x538 --type "u8 phase, pad 3, f32 duration, f32 elapsed,
  f64[3] start"` (Python: `read_layout`). No hand slicing of hex.
- **Name what you decoded, once.** `label <obj> 0x538 f64[3] Start` (same
  type syntax; `?` after an unconfirmed name) writes the console's native
  file; `fields`, `dump` and `get $1 Start` then show it as a `native`
  field in every later run. A member labelled before a game patch shows
  `stale: true` until you check it and label it again; a label that now
  overlaps a reflected field is listed in `native_refused` instead of
  shown. Obfuscated reflected fields get readable names from the host's
  names file; those are hints, as are native names: trust offsets and
  types.
- **Prove a decoded motion model live.** Sample the moving thing and the
  block in one `batch` (a `get` of its location and a `read`) and test the
  model on every row (on the line between two stored points, progress equal
  to elapsed over duration). Then predict the end state and keep sampling
  until it happens.

## Searching memory

- Long searches: `search ... --background` keeps the game smooth when the
  host allows it; `--heap` for structs (search a vtable pointer to find every
  copy), `--module` for code and static data.
- `search` matches exact bytes, with no tolerance. For a value that moves,
  search while it rests at a known value (full, zero), or search the exact
  bytes a getter returned in the same `batch`.
- "Does a write stick?": find the storage here, then follow the write
  checklist for calls and writes (approval, `--expect`, copies that snap
  back).
