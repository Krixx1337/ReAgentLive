# Native code behind reflection, and Blueprint logic (Unreal)

Verb behaviour: `docs/commands/unreal-calls.md#func-and-funcs` and
`docs/commands/unreal-code.md`. Native code in general:
the native-code reference.

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
  and properties. A native function has no bytecode.
- **Event graphs:** decode `ExecuteUbergraph_<Class>` once for its entry
  list, then `entry` set to each event's offset.
- If the decode reports it stopped, the statements before the stop are
  sound; read past it by hand only around the offset it names.
- **Which Blueprints read a value:** `name-refs` with `name` (the tag text) or
  `index`, then `script` with `decode` on each hit (its ref as `object`,
  no `function`).
- With an SDK dump: the parameter struct's `CallFunc_*` locals name every
  call the function makes (any math node in between shows); `grep` the
  SDK `*_parameters.hpp` for `CallFunc_FindAttribute_ReturnValue` to list
  every Blueprint that reads attributes.
