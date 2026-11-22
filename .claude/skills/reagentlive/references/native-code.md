# Native code

Verb behaviour: [fast lane](https://github.com/Krixx1337/ReAgentLive/blob/main/docs/commands/fast-lane.md).

## Fast lane first

Answer one-address questions live, with no database:

- What is here: `disasm <addr>` (`--function` for the whole entry,
  `--follow` through a jump stub). An `import` target names an API call.
- What points here: `xrefs <addr>` (`--module` for a DLL calling into the
  exe).
- What code uses these member offsets: `touches <exec> <offset>...` from a
  native function of the same class (a `func` exec).
- Does this signature hold: `sig-check`.

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
  uses it where a tag is expected, then `peek <addr> --label <file>`
  records it for the static tool.
- **Names for the static tool:** with SDK-generator IDA mappings, import
  those first, then `export-labels <file> --kinds interface,global`;
  without them, a full `export-labels <file>`. Never a full export after
  mappings. Import with [`src/reagentlive/frontends/ida/import_labels.py`](https://github.com/Krixx1337/ReAgentLive/blob/main/src/reagentlive/frontends/ida/import_labels.py)
  ([labels](https://github.com/Krixx1337/ReAgentLive/blob/main/docs/commands/unreal.md#labels-for-a-static-tool)).
- After a game patch: a new database (the RVAs change), then export and
  import the labels again. The first auto-analysis of a large Unreal image
  is slow: start it early. Decompiling from a session needs the tool
  running with its bridge plugin.

## Following a native getter

- `func` gives its exec; `disasm <exec> --function` lists it, and each
  `call`'s `target` is the next hop. Follow one level at a time; open the
  static tool once a function needs decompiling.
- `disasm` refuses `--function` where the exception table has no entry (a
  leaf function): list from the address itself.

## Native interface getters

- **Never `call` a function declared on a native interface** (a `funcs` row
  with `via: "interface"`): its stub calls a vtable slot without adjusting
  `this`, so it runs the wrong function. Code that looks it up by name on
  the class gets null: the symptom to recognise.
- `func <object> <Interface>:<Fn>`, then open `implementation_rva` in the
  static tool. A trivial getter is a field read: `movss xmm0, [rcx + X];
  ret` means the value lives at `object + offset + X`; read or capture it.
- `slot` absent: open the stub (`rva`) and read the vtable at
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

Rows without `function` other than `pointer` may be chance bytes. Doing
this for every table is a Python script, not a command.

## Making a signature

You make the signature; ReAgentLive checks it against the live image
([signatures](https://github.com/Krixx1337/ReAgentLive/blob/main/docs/commands/fast-lane.md#signatures)).

1. Read the function in the static tool with bytes shown, at the RVA
   (`func` gives a reflected function's native entry).
2. Wildcard what moves with every build: relative call and jump targets,
   RIP-relative displacements, absolute addresses. Keep ordinary constants.
   Wildcard struct offsets and stack slots only when the signature should
   survive a recompile that moves fields.
3. Whole instructions only, within the checker's length and fixed-byte
   limits (signatures page above).
4. Function openings repeat. If the start is not unique, begin at a later
   instruction and record the offset (`--expect <entry> --offset N`).
5. Still not unique: sign an instruction that references the target (a
   caller, or code that loads the global) and record the resolve formula,
   `--expect` set to the target. A `call rel32` at match offset C is
   `--resolve C+5,C+1`; `lea rcx, [rip+d]` at C is `C+7,C+3`.
6. `sig-check <pattern> --expect <module>+<rva>` after every attempt; act on
   it: `mismatches` (a typo or slipped byte count), `diverges_at` /
   `unique_at_length` (how much longer; the extra bytes must be ones you
   would keep fixed), `warnings` (a fixed field holds an address: wildcard).
7. Prefer the shortest unique pattern, then the most fixed bytes. Try
   several candidates in one `sig-file`. Then hand it to the host's checks.

Without a static database: `disasm <entry> --function` for instruction
boundaries, `sig-bytes <boundary> 96` for `suggested` masks, and
`xrefs <entry>` for callers to build a reference signature from. Check
`decoded` covers the pattern.

## Reading Blueprint logic

- **What a Blueprint function really reads:** `script <obj|class> <Fn>`
  (names, calls and properties, confirmed live). A native function has no
  bytecode.
- **Which Blueprints read a value:** `refs <tag text>` or `refs #<index>`,
  then `script` on each hit.
- `script` is an anchored scan, not a disassembler. To prove a returned
  value is used unchanged, read the bytecode near the end by hand: the
  function's script array (`Script` in the engine layout,
  [`src/reagentlive/packs/ue/engine_layout.h`](https://github.com/Krixx1337/ReAgentLive/blob/main/src/reagentlive/packs/ue/engine_layout.h)); look for `EX_Let` 0x0F,
  `EX_LocalOutVariable` 0x48, `EX_Cast` 0x38.
- With an SDK dump: the parameter struct's `CallFunc_*` locals name every
  call the function makes (any math node in between shows); `grep` the
  SDK `*_parameters.hpp` for `CallFunc_FindAttribute_ReturnValue` to list
  every Blueprint that reads attributes.
