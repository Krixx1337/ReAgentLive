# Native code

Verb behaviour: fast lane (`docs/commands/fast-lane.md`). Decompiling in
IDA: the static-tool reference. Interface getters and Blueprint
bytecode (Unreal): the native-and-blueprint reference.

## Fast lane tips

Verbs: `disasm`, `xrefs`, `touches`, `disp-scan`, `sig-check`,
`labels-check`. Non-obvious parts:

- `disasm` with `whole` gives every instruction, the constants its
  loads read and its strings: read formulas from it instead of `read`ing
  each constant. `jumps` goes through a jump stub; an `import` target
  names an API call.
- `touches` needs an address in a native function of the same class (a
  `func` exec). Copies and constructors rank first when the offsets are
  common: add `access` `read` for the code that consumes the block. Rows marked
  unlisted are code outside the exception table: confirm each with
  `disasm` with `whole`.
- "Who writes field X": name rbp in `bases` too, or sweep with `disp-scan`
  (which keeps rbp): optimised code holds structs in rbp, and the default
  hides those writers. `bases` `["rcx"]` narrows to `this`.
- More than one or two `disp-scan` pages: a Python loop or a `run` plan
  over `next_cursor`, never call after call. A page's `read_ms` near its
  `scan_ms` means the agent has no image window: reinject.
- After a patch: `{"verb": "labels-check", "args": {"status": ["moved", "missing", "ambiguous", "invalid"]}}`.

## Crossing to and from a static tool

- **Static tool:** whole functions, decompiling, types, call graphs.
  **ReAgentLive:** live values, which function a live object dispatches to
  (vtables, `func` on an interface), what a global holds at run time (the
  image file has zeros where the engine fills names at start-up).
- Printed `module` + `rva`: in the static tool, image base plus RVA. Back,
  pass `{"module": "game.exe", "rva": "0x1234"}` as any address.
- A global shown as `qword_…`: `peek` it; an FName index shows as
  `name_hint`. Accept the hint only when the code uses it where a tag is
  expected, then `peek` with `label` records it
  (`docs/commands/labels.md#peek-with-label`). Names for a full database:
  `labels-export`, then the loader's `import_labels`.

## Following a native getter

- `func` gives its exec; `disasm` of it with `whole` lists it, and each
  `call`'s `target` is the next hop. Follow one level at a time; decompile
  once a function needs it (the static-tool reference).
- Where the exception table has no entry (a leaf function, or a packed
  image that leaves real functions out), `disasm` with `whole` walks
  from the address you give: give it the function's start (the byte after
  the previous `ret`). A listing a cap cut gives `next_cursor`: the same
  call with `view.cursor` set to it continues.

## Where a virtual call lands

- An indirect call through `[rax+N]` after `mov rax, [rcx]`: the receiver is often an
  interface part inside the object, not its first table. One call lists
  every part with the slot that matters:
  `{"verb": "vtable", "args": {"addr": "0x1f2e3d4c50", "parts": 4096, "slots": [15]}}`
  (slot = N / 8). A slot that is `mov rax, [rcx]; jmp [rax+k]` forwards
  to another slot of the same part: follow `k`.
- What a subclass changes: `vtable` an instance and its base class's
  default object, in one script; slots whose target differs are its
  overrides, slots past the base's last are the virtuals it adds.
- Only a slot or two of a long table: `slots`, never the whole table.

## Naming an unreflected system

Walk references one address at a time; check each answer before the next.

1. **A source-path or assert string → the function using it.**
   `{"verb": "search", "args": {"type": "text", "value": "Weapon.cpp", "scope": {"module": "game.exe"}}}`
   (`type` `wtext` for UTF-16), then `xrefs` of the hit: `lea` rows with a
   `function`. Goes wrong: a string shared by several classes (read each
   with `disasm` with `whole`), or the hit is inside a longer string
   (`xrefs` the string's start).
2. **The function → the table slot holding it.** `xrefs` of the function
   start, kind `pointer`, no `function`. A stub in between: `xrefs` the
   stub (`disasm` with `jumps` shows the hop).
3. **The table → the constructor or destructor installing it.**
   `xrefs` of the table start (usually `lea`, then a `store` into the
   object).
4. **The constructor → the global it stores the object in.**
   `disasm` with `whole` on the constructor's caller: a `store` with a
   RIP-relative `target` after the call.
5. **The global → the functions returning it.** `xrefs` of the global:
   `load` and `lea` rows; `peek` it for the live value.

`unlisted` rows: code no entry lists, `function` its piece (junk may lead).
No `function`, not `pointer`: likely chance bytes. Every table: a script.
