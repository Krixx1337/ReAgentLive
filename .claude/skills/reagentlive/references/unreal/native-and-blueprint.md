# Native code behind reflection, and Blueprint logic (Unreal)

Verb behaviour: `docs/commands/unreal-calls.md#func-and-funcs` and
`docs/commands/unreal-code.md`. Native code in general: the native-code
reference.

## Native interface getters

- **Never `call` a function declared on a native interface** (`funcs`
  marks the row; `help` on `funcs`): its stub calls a vtable slot without adjusting
  `this`, so it runs the wrong function. Code that looks it up by name on
  the class gets null: the symptom to recognise.
- `func` with `function` `<Interface>:<Fn>`, then open its
  implementation's RVA in the static tool (keys: `help` on `func`). A trivial getter is a field read: `movss xmm0, [rcx + X];
  ret` means the value lives at `object + offset + X`; read or capture it.
- No slot in the result: open the stub (`rva`) and read the vtable at
  `object + offset` yourself.
- `func` refused with "interfaces array implausible": the engine's `UClass`
  layout differs. Find the array by hand: a `TArray` in the class whose
  entries are `{UClass*, int32}`, each class deriving from `Interface`.

## Reading Blueprint logic

- **What a Blueprint function computes:**
  `{"verb": "script", "args": {"object": "0x1f2e3d4c50#123:4", "function": "CalcDamage", "decode": true}}`
  gives its statements with offsets; read the formula there, never by hand
  from the raw bytes. Without `decode`, `script` lists only names, calls
  and properties. A native function has no bytecode. Read a design doc's
  formula the same way before trusting it.
- **Event graphs:** decode `ExecuteUbergraph_<Class>` once for its entry
  list, then `entry` set to each event's offset.
- If the decode reports it stopped, the statements before the stop are
  sound; read past it by hand only around the offset it names.
- **Which Blueprints read a value:** `name-refs` with `name` (the tag text) or
  `name_index`, then `script` with `decode` on each hit (its ref as `object`,
  no `function`).
- With an SDK dump: the parameter struct's `CallFunc_*` locals name every
  call the function makes; `grep` the SDK `*_parameters.hpp` for a
  `CallFunc_*_ReturnValue` to list every Blueprint that makes it.

## Poses, anim graphs and camera

- A first-person arms mesh's anim instance links per-weapon layers; anim
  variables keep readable names: `get` the layer with `view.match`
  (`{"verb": "get", "args": {"object": "0x1f2e3d4c50#123:4", "view": {"match": "Aim"}}}`).
  `describe` a player's `Sequence` pointer to see which clip a node plays.
- "Still slow": sample a bone (`GetSocketTransform`, component space)
  against the camera bone, and the gameplay progress getter beside it. A
  slow visible move is either the progress value or the pose catching up;
  the getter tells which. For lean, sample camera translation too.
- Scan all anim nodes read-only before snapping a few (alpha sources, blend
  times, interp speeds): a node that differs between two states may smooth
  something else too, and snapping it can shake the gun. Easing alphas set
  `bInterpResult`; the interpolated value hides in the struct's padding.
- "Where does this graph input come from?": `__CustomProperty_<Pin>_<GUID>`
  fields feed a node (often a Control Rig). `find` with `text` the GUID
  gives its `EvaluateGraphExposedInputs` function; `script` with `decode`
  on it calls the uber graph at an entry offset: `decode` there with
  `entry` set to that offset.
