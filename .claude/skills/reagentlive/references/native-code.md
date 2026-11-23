# Native code

Verb behaviour: fast lane (`docs/commands/fast-lane.md`).

## Fast lane first

Answer one-address questions live, with no database:

- What is here: `disasm <addr>` (`--follow` through a jump stub). A whole
  function is one call: `disasm <addr> --function` gives every
  instruction, the constants its loads read and the strings it uses; read
  formulas from it instead of `read`ing each constant. An `import` target
  names an API call.
- What points here: `xrefs <addr>` (`--module` for a DLL calling into the
  exe).
- What code uses these member offsets: `touches <exec> <offset>...` from a
  native function of the same class (a `func` exec). Copies and
  constructors rank first when the offsets are common: `touches --reads`
  for the code that consumes the block. Rows marked unlisted are pieces of
  code outside the exception table: confirm each with `disasm --function`.
- Does this signature hold: `sig-check` (making one: the signatures reference).

## ReAgentLive or the static tool

- **Static tool** (a disassembler database of the same build): whole
  functions, decompiling, types, call graphs, names.
- **ReAgentLive:** live values, which object, which function a live object
  dispatches to (vtables, `func` on an interface), what a global holds at
  run time (the image file has zeros where the engine fills names at
  start-up), and one-address answers.
- **Crossing over:** printed `module` + `rva`; in the static tool go to its
  image base plus the RVA (IDA: `idaapi.get_imagebase() + rva`). Back the
  other way, pass `<module>+<rva>` as any address.
- **Globals the static tool shows as `qword_…`:** `peek <module>+<rva>`;
  an FName index shows as `name_hint`. Accept the hint only when the code
  uses it where a tag is expected, then `peek <addr> --label --name <N>`
  records it in the host's names file.
- **Names for the static tool:** after SDK-generator mappings,
  `export-labels --kinds interface,global`, else a full `export-labels`.
  `import_labels` it; `export_names` what you named before closing, so later
  saves show it (`docs/commands/unreal.md#labels-for-a-static-tool`).
- After a game patch: a new database (the RVAs change), then export and
  import the labels again. The first auto-analysis of a large Unreal image
  is slow: start it early. Decompiling from a session needs the tool
  running with its bridge plugin.
- **No database of this build, or not worth one:** `disasm <addr> --function
  --save <file> --callees` (named from the console's and host's labels) and the IDA
  loader (`src/reagentlive/frontends/ida/reagentlive_ida.py`) give the function,
  its chunks and callees in seconds; decompile all in one Python call,
  `add_saved` the next save
  (`docs/commands/fast-lane.md#saving-a-function-for-a-static-tool`). Save
  from the function's start.
  A `JUMPOUT(<addr>)` or cast `fn_<rva>(...)` left is code not saved (a jump
  table, a target past the cap): save it, `add_saved` it. A typed-pointer index
  (`p[27]`) is offset = index × size.

## One workspace database per investigation

- Open the first save; `add_saved` every later save into the same database;
  close it when the investigation ends. Never one database per function.
- `add_saved` returns a summary of what it added and joined, and refuses
  when a saved function's start has no loaded bytes: read the refusal,
  save what it names, add again.
- Each work cycle starts with `list_databases`: close every headless
  (idalib) database that is not the workspace or a reference database.
  Leaked workers pile up and die.
- One-time setup, done by the user (outside the repo; ask, never edit their
  install): put the loader's folder on IDAPython's path (one `sys.path`
  line in `idapythonrc.py`) so scripts `import reagentlive_ida`, and
  disable any plugin that fails to load under idalib.

## Following a native getter

- `func` gives its exec; `disasm <exec> --function` lists it, and each
  `call`'s `target` is the next hop. Follow one level at a time; open the
  static tool once a function needs decompiling.
- Where the exception table has no entry (a leaf function, or a packed
  image that leaves real functions out), `disasm --function` walks from
  the address you give: give it the function's start (the byte after the
  previous `ret`). A listing a cap cut gives `next`: continue with
  `disasm <next> --function --resume`.

## Native interface getters

- **Never `call` a function declared on a native interface** (`funcs`
  marks the row; `help funcs`): its stub calls a vtable slot without adjusting
  `this`, so it runs the wrong function. Code that looks it up by name on
  the class gets null: the symptom to recognise.
- `func <object> <Interface>:<Fn>`, then open its implementation's RVA in
  the static tool (keys: `help func`). A trivial getter is a field read: `movss xmm0, [rcx + X];
  ret` means the value lives at `object + offset + X`; read or capture it.
- No slot in the result: open the stub (`rva`) and read the vtable at
  `object + offset` yourself.
- `func` refused with "interfaces array implausible": the engine's `UClass`
  layout differs. Find the array by hand: a `TArray` in the class whose
  entries are `{UClass*, int32}`, each class deriving from `Interface`.

## Naming an unreflected system

Walk references one address at a time; check each answer before the next.

1. **A source-path or assert string → the function using it.**
   `search text Weapon.cpp --module <exe>` (`--wide` for UTF-16), then
   `xrefs <hit>`: `lea` rows with a `function`. Goes wrong: a string shared
   by several classes (read each with `disasm --function`), or the hit is
   inside a longer string (`xrefs` the string's start).
2. **The function → the table slot holding it.** `xrefs <function start>`,
   kind `pointer`, no `function`. A stub in between: `xrefs` the stub
   (`disasm --follow` shows the hop).
3. **The table → the constructor or destructor installing it.**
   `xrefs <table start>` (usually `lea`, then a `store` into the object).
4. **The constructor → the global it stores the object in.**
   `disasm --function` on the constructor's caller: a `store` with a
   RIP-relative `target` after the call.
5. **The global → the functions returning it.** `xrefs <global>`: `load`
   and `lea` rows; `peek` it for the live value.

`unlisted` rows: code no entry lists, `function` its piece (junk may lead).
No `function`, not `pointer`: likely chance bytes. Every table: a script.

## Reading Blueprint logic

- **What a Blueprint function really reads:** `script <obj|class> <Fn>`
  (names, calls and properties, confirmed live). A native function has no
  bytecode.
- **Which Blueprints read a value:** `refs <tag text>` or `refs #<index>`,
  then `script` on each hit.
- `script` is an anchored scan, not a disassembler. To prove a returned
  value is used unchanged, read the bytecode near the end by hand: the
  function's script array (`Script` in the engine layout,
  `src/reagentlive/packs/ue/engine_layout.h`); look for `EX_Let` 0x0F,
  `EX_LocalOutVariable` 0x48, `EX_Cast` 0x38.
- With an SDK dump: the parameter struct's `CallFunc_*` locals name every
  call the function makes (any math node in between shows); `grep` the
  SDK `*_parameters.hpp` for `CallFunc_FindAttribute_ReturnValue` to list
  every Blueprint that reads attributes.
