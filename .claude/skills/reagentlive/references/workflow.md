# Workflow and pitfalls

## Method

1. **Question first.** Write the runtime question ("which field drops when
   I fire?") and what would answer it. Read what the project's docs record.
2. **Locate.** `{"verb": "find", "args": {"cls": "Pawn"}}` lists instances; add `text` to
   narrow. Name text matches anywhere, so short text floods the page: use a
   distinctive suffix or a class.
3. **Navigate.** `get` an owner object and follow the printed refs instead
   of computing offsets. To find where instances of a class live, start
   from the engine, never a memory sweep: `describe` one instance and walk
   its outer chain, `get` with `view.nonzero` on each outer for container
   fields, then `{"verb": "route", "args": {"object": "0x1f2e3d4c50#123:4"}}`.
   `search` for its address only when that fails, and pass the hits to
   `owner` (with the instance's outer as `within`).
4. **Decode.** Names print as text; `name` resolves a leftover `#N`.
   Readable names for obfuscated fields are hints: trust offsets, sizes and
   types.
5. **Observe change.** `get` with `mode` `snap`, the user acts, `mode` `changed`; or a
   capture for sequences. Pick the object that owns the data: no changed
   rows usually means the wrong object (position lives on a scene
   component, not the pawn). An action that resets at once (a jump): the
   capture reference.
6. **Cross-check.** Confirm with a second, independent source (asset value
   vs measured behaviour, computed point vs bone transform). One source is
   a hypothesis.
7. **Ship and verify.** When a finding changes code: `layout-export` every
   class it reads (`layout-check` after a patch), pure logic with unit
   tests, then a live capture that proves the new code path ran (a case
   only the new logic explains), then the evidence record.
8. **Name it once.** A member: `member-name`. A native function: name it in the
   static tool, then `export_names` into the host's names file. A global:
   `peek` with `label`. Then `labels-sign`, so `labels-port` finds them after a
   patch. Later saves show those names: read a save's names before working
   out a function again.

## Pitfalls

- **Refs go stale.** Respawns and reloads replace objects: look refs up
  again. A raw `read` of an old address still returns bytes: it checks
  memory, not identity; a literal ref checks identity.
- **Separate calls read separate ticks** (SKILL.md rules).
- **`own` can hide everything.** `get` with `own` lists only the fields the
  object's own class declares; a small subclass (an effect, a trigger, a
  data sub-object) often declares none. Empty: drop `own`, keep
  `view.nonzero`.
- **Native members may be absent from reflection.** `get` can show a data
  table's row struct and omit its native `RowMap` (`rows` reads it).
- **UI names differ from internal names.** Map through item tags, not by
  guessing.
- **Settings confound captures.** Ask the user to turn off host features
  that touch what you measure, and to keep still. Before blaming the code,
  read the saved settings file: a test may have run with the feature off.
- **Interactive tests.** Give the user exact steps and wait for one
  "ready"; a timed capture started early expires before they read your
  message.
- **Empty session.** Behaviour that needs a second player cannot be
  observed alone; verify on the local character and record what stays
  unmeasured.
