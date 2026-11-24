# Native code

Verb behaviour: fast lane (`docs/commands/fast-lane.md`). Decompiling in
IDA: the static-tool reference. Interface getters and Blueprint
bytecode (Unreal): the native-and-blueprint reference.

## Fast lane first

Answer one-address questions live, with no database:

- What is here: `disasm` (`follow` through a jump stub). A whole
  function is one call:
  `{"verb": "disasm", "args": {"addr": "0x7ff600001000", "function": true}}` gives every
  instruction, the constants its loads read and the strings it uses; read
  formulas from it instead of `read`ing each constant. An `import` target
  names an API call.
- What points here: `xrefs` (`module` for a DLL calling into the exe).
- What code uses these member offsets:
  `{"verb": "touches", "args": {"addr": "0x7ff600001000", "offsets": ["0x538", "0x550"]}}`
  from a native function of the same class (a `func` exec). Copies and
  constructors rank first when the offsets are common: `touches` with
  `reads` for the code that consumes the block. Rows marked unlisted are
  pieces of code outside the exception table: confirm each with `disasm`
  with `function`.
- Does this signature hold: `sig-check` (making one:
  the signatures reference); every signed name of a labels file:
  `labels-check`.
- Does anything write this field, anywhere: `disp-scan` (whole image,
  page by page); near one function: `touches`. Both skip stack-frame
  bases; `bases` `["rcx"]` narrows to `this`. Confirm a hit with `disasm`.

## Crossing to and from a static tool

- **Static tool:** whole functions, decompiling, types, call graphs.
  **ReAgentLive:** live values, which object, which function a live object
  dispatches to (vtables, `func` on an interface), what a global holds at
  run time (the image file has zeros where the engine fills names at
  start-up), and one-address answers.
- Printed `module` + `rva`: in the static tool, its image base plus the
  RVA (IDA: `idaapi.get_imagebase() + rva`). Back the other way, pass
  `{"module": "game.exe", "rva": "0x1234"}` as any address.
- **Globals the static tool shows as `qword_…`:** `peek` at its module
  and RVA; an FName index shows as `name_hint`. Accept the hint only when
  the code uses it where a tag is expected, then `peek` with `label` and
  `name` records it (`docs/commands/labels.md#peek-with-label`).
- **Names for a full database:** after SDK-generator mappings,
  `{"verb": "labels-export", "args": {"kinds": ["interface"]}}`, else a
  full `labels-export`; then the loader's `import_labels`.

## Following a native getter

- `func` gives its exec; `disasm` of it with `function` lists it, and each
  `call`'s `target` is the next hop. Follow one level at a time; decompile
  once a function needs it (the static-tool reference).
- Where the exception table has no entry (a leaf function, or a packed
  image that leaves real functions out), `disasm` with `function` walks
  from the address you give: give it the function's start (the byte after
  the previous `ret`). A listing a cap cut gives `next`: continue with
  `disasm` at `next` with `function` and `resume`.

## Naming an unreflected system

Walk references one address at a time; check each answer before the next.

1. **A source-path or assert string → the function using it.**
   `{"verb": "search", "args": {"type": "text", "value": "Weapon.cpp", "scope": {"module": "game.exe"}}}`
   (`type` `wtext` for UTF-16), then `xrefs` of the hit: `lea` rows with a
   `function`. Goes wrong: a string shared by several classes (read each
   with `disasm` with `function`), or the hit is inside a longer string
   (`xrefs` the string's start).
2. **The function → the table slot holding it.** `xrefs` of the function
   start, kind `pointer`, no `function`. A stub in between: `xrefs` the
   stub (`disasm` with `follow` shows the hop).
3. **The table → the constructor or destructor installing it.**
   `xrefs` of the table start (usually `lea`, then a `store` into the
   object).
4. **The constructor → the global it stores the object in.**
   `disasm` with `function` on the constructor's caller: a `store` with a
   RIP-relative `target` after the call.
5. **The global → the functions returning it.** `xrefs` of the global:
   `load` and `lea` rows; `peek` it for the live value.

`unlisted` rows: code no entry lists, `function` its piece (junk may lead).
No `function`, not `pointer`: likely chance bytes. Every table: a script.
