# Native code

## Fast lane first

Answer one-address questions live, at once, with no database
([commands](../../../../docs/commands/fast-lane.md)):

- What is here: `disasm <addr>` (`--function` for the whole entry from the
  exception table, `--follow` through a jump stub).
- What points here: `xrefs <addr>` (kinds `call`, `jmp`, `jcc`, `lea`,
  `load`, `store`, `rip`, `pointer`; `--module` for a DLL calling into the
  exe).
- Does this signature hold: `sig --check`.
- What code uses these member offsets: `touches <addr> <offset>...` from a
  native function of the same class (a `func` exec); functions using most
  of the offsets, and writing them, rank first.

Go to the static tool for whole functions, decompiling, types, call graphs
and anything that needs the whole program.

## ReAgentLive or the static tool

- **Static tool** (a disassembler database of the same build): whole
  functions, decompiling, types, call graphs, names.
- **ReAgentLive:** live values, which object, which function a live object
  dispatches to (vtables, `func` on an interface), what a global holds at run
  time (the image file has zeros where the engine fills names at start-up),
  and the fast lane's one-address answers.
- **Crossing over:** every address ReAgentLive prints inside a module carries
  `module` and `rva`. In the static tool, go to its image base plus the RVA
  (in IDA: `idaapi.get_imagebase() + rva`). Back the other way, pass
  `<module>+<rva>` (`game.exe+0x1a2b3c`) to any ReAgentLive address argument.
- **Globals the static tool shows as `qword_…`:** `peek <module>+<rva>`. The
  engine fills tag and name globals at start-up, so the file has zeros where
  the live image has an FName index; `peek` shows it as `name_hint`. Accept
  the hint only when the code uses it where a tag is expected, then
  `peek <addr> --label <file>` records it for the static tool.
- **Names for the static tool:** where the host has SDK-generator IDA
  mappings, import those first, then `export-labels <file> --kinds
  interface,global`; without them, `export-labels <file>` (every kind: exec
  thunks, vtables, interface implementations, your `peek --label` globals).
  Never run a full export after mappings. Import with ReAgentLive's
  `src/reagentlive/frontends/ida/import_labels.py`; it refuses another build and never
  replaces a name you set.
- After a game patch: a new database (the RVAs change), then the label
  export and import again. The first
  auto-analysis of a large Unreal image is slow; start it early.
  Decompiling needs the tool's decompiler; driving it from a session needs
  the tool running with its bridge plugin.

## Following a native getter

- To see where a native getter reads from, `func` gives its exec address;
  `disasm <exec> --function` lists it, and each `call`'s `target` is the
  next hop. Follow the calls one level at a time; open the static tool once
  a function needs decompiling.
- `disasm` refuses `--function` where the exception table has no entry (a
  leaf function); list from the address itself instead.

## Naming an unreflected system

With no reflection, walk references one address at a time. Each hop is one
command; check each answer before the next.

1. **A source-path or assert string → the function using it.**
   `search text Weapon.cpp --module <exe>` (`--wide` for UTF-16), then
   `xrefs <hit>`: rows of kind `lea` with a `function`. Goes wrong: a
   string shared by several classes (several functions; read each with
   `disasm --function`), or the hit is inside a longer string (`xrefs` on
   the string's start, which `read` shows).
2. **The function → the table slot holding it.** `xrefs <function start>`,
   kind `pointer`, `function: null` (data). Goes wrong: a stub sits between
   the table and the body (`xrefs` the stub instead; `disasm --follow` shows
   the hop).
3. **The table → the constructor or destructor installing it.**
   `xrefs <table start>` (usually `lea`, then a `store` into the object).
4. **The constructor → the global it stores the object in.**
   `disasm --function` on the constructor's caller: a `store` with a
   RIP-relative `target` after the call.
5. **The global → the functions returning it.** `xrefs <global>`: `load`
   (and `lea`) rows, each with its `function`; `peek` it for the live value.

Rows with `function: null` other than `pointer` may be chance bytes, as are
the `unknown` ones `--all` adds. Doing this for every table is a script
over the Python client (`s.xrefs`, `s.disasm`), not a command.

## Making a signature

You make the signature; ReAgentLive checks it against the live image
([commands](../../../../docs/commands/fast-lane.md#signatures)).

1. Read the function in the static tool with bytes shown, at the RVA from
   ReAgentLive (`func` gives a reflected function's native entry).
2. Wildcard what moves with every build: relative call and jump targets,
   RIP-relative displacements, any absolute address. Keep ordinary
   constants. Wildcard struct offsets and stack slots only when the
   signature should survive a recompile that moves fields.
3. Whole instructions only. 8 to 96 bytes, at least 6 fixed, no leading
   wildcard, trailing wildcards dropped; avoid a start whose first 4 bytes
   are mostly wildcards (the checker needs 4 fixed in a row somewhere).
4. Function openings repeat (`48 89 5c 24 08 57 ...`). If the start is not
   unique, begin at a later instruction and record the offset
   (`--expect <entry> --offset N`).
5. Still not unique: sign an instruction that references the target (a
   caller, or code that loads the global; the static tool lists them) and
   record the resolve formula: `--resolve next,field[,size]` with
   `--expect` set to the target. A `call rel32` at match offset C is
   `C+5,C+1`; `lea rcx, [rip+d]` at C is `C+7,C+3`.
6. `sig --check <pattern> --expect <module>+<rva>` after every attempt, and
   act on what it says:
   - `mismatches`: a typo or a slipped byte count at those offsets;
   - `diverges_at` / `unique_at_length`: how much longer to make it (the
     extra bytes must be ones you would keep fixed);
   - `warnings`: a fixed field that holds an address; wildcard it.
7. Prefer the shortest unique pattern, then the one with the most fixed
   bytes. Try several candidates in one call with `sig --check --file`
   (one JSON line each; `winner` names the first that holds). Then hand it
   to the host's own checks.

Without a static database (a DLL with none, or analysis not finished):
`disasm <entry> --function` for the instructions and their boundaries,
`sig --bytes <boundary> 96` for `suggested` masks (decoded exactly from that
boundary), and `xrefs <entry>` for callers to build a reference signature
from (`call` rows give `instruction`, so `--resolve` is `C+5,C+1` from
there). With `--expect`, `warnings` come from the decoder; check `decoded`
covers the pattern.

## Native interface getters

- **Never `call` a function declared on a native interface**
  (`SomeInterface:GetValue` on an object that implements it). Its exec stub
  calls a slot of the object's vtable without adjusting `this`, so it runs the
  wrong function.
- If `func` refuses with "interfaces array implausible", the engine's
  `UClass` layout differs from the one the pack expects. Find the array by
  hand: look in the class for a `TArray` whose entries are `{UClass*, int32}`,
  where each class derives from `Interface`.
- Read the implementation instead: `func <object> <Interface>:<Fn>` reports
  `interface` with the interface part's `offset`, the stub's vtable `slot`,
  and the `implementation` address and RVA. `call` refuses these functions.
- Open `implementation_rva` in the static tool. A trivial getter is a field read:
  `movss xmm0, [rcx + X]; ret` means the value lives at
  `object + offset + X`; read or `watch` it directly.
- If `slot` is null, the stub had no plain `mov rax, [reg]; call [rax + disp]`.
  Open the stub (`rva`) in the static tool and read the vtable at
  `object + offset` yourself.

## Reading Blueprint logic

- **To find what a Blueprint function really reads:**
  `func <obj|class> <Fn> --script`. It lists the function's bytecode names
  (gameplay tags, rows), the functions it calls and the properties it uses,
  each confirmed live, with offset and count. A native function has no
  bytecode and returns `not found`.
- **To find which Blueprints read a value:** `refs <tag text>` or
  `refs #<index>` lists every Blueprint function whose bytecode names it. Then
  run `--script` on each hit.
- `--script` is an anchored scan, not a disassembler. To prove a returned
  value is used unchanged, still read the bytes near the end by hand
  (`read <function address>+0x60 16` gives the script array; look for
  `EX_Let` 0x0F, `EX_LocalOutVariable` 0x48, `EX_Cast` 0x38).
- The SDK parameter struct's `CallFunc_*` locals name every call the function
  makes. Read them first: they show whether any math node sits in between.
- `grep` the SDK `*_parameters.hpp` for `CallFunc_FindAttribute_ReturnValue`
  to list every Blueprint that reads attributes.
