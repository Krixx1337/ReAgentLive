# Native objects

*Home of:* reading a native (non-reflected) object with no engine pack: `vtable` (and `rtti`, `parts`), pointer chains, the region `read` and `peek` report, C strings; recording calls through a vtable entry or a function pointer (`native-calls-start`), and `calls-read` for every recorder.

**TL;DR:** When a game has no engine pack, or an object is plain C++ that
reflection does not list, these commands still say what it is: its
vtable and the functions in it, its MSVC class name, what kind of memory
an address is, and the strings it points to. They only read, but for
`native-calls-start`, an approved write; names they report are hints to
confirm, not proof.

What the engine-agnostic side says about a native object, with no engine
pack ([design](../design/native.md#native-objects)).

```json
{"verb": "vtable", "args": {"addr": "0x1f2e3d4c50", "rtti": true, "resolve": true}}
{"verb": "read", "args": {"addr": {"base": "0x1f2e3d4c50", "chain": [16, 8]}}}
{"verb": "read", "args": {"addr": "0x1f2e3d4c50", "type": "cstr"}}
```

Every arg: [reference](reference.md#vtable).

## vtable

`vtable` reads the table pointer at `addr` (an object's first 8 bytes;
`offset` N: a secondary base or an interface part) and lists its slots with
module and RVA. When `addr`'s own first qword already points into code,
`addr` is the table itself. It stops at the first entry that does not point
into an executable section of a loaded module (`stop` `not code`). `resolve`
adds `body` where a slot's target is a stub of unconditional direct jumps:
the address `disasm` with `jumps` would end at. A qword that points into no
module is a result: no `table`, and `found` holding the qword.

### parts

A virtual call through an interface lands on a table inside the object, not
the first one. `parts` N reads the object's first N bytes and lists each
distinct table found there, with every offset holding it (embedded structs
repeat one table). One call answers which part's slot 15 an indirect call
through `[rax+0x78]` reaches:

```json
{"verb": "vtable", "args": {"addr": "0x1f2e3d4c50", "parts": 4096, "slots": [6, 8, 15]}}
```

A function-pointer array in read-only data looks like a table too: confirm a
part with `vtable` at its `offset`.

### rtti

`vtable` with `rtti` also reads the slot before the table: in an MSVC image
built with run-time type information it points to the class's complete
object locator, which gives the class `name` (undecorated for plain and
nested classes, templates stay decorated), its `decorated` name and its
`bases`. `complete` is the whole object when `addr` is a secondary base or
an interface part. Where the slot holds no locator (no type information,
another compiler, or not a virtual table) `rtti_error` says why. Engines
often build their game code without it; most native programs and libraries
keep it. **Names are hints, not proof:** confirm them against the code.

## Native calls

`native-calls-start` records what callers pass through one code pointer for
up to 60 s; `calls-read` returns the calls so far (`stop` ends it early). A
host offers it by listing the core's three recorder ops; the start is a
write the host approves.

- **Which pointer:** with `vtable` (`entry`, and `offset` for a vptr past
  the object's start), one object's vtable entry: its vptr is pointed at a
  private copy of the table and the entry swapped there, so the table and
  other instances are untouched. Without it, `addr` is a cell that holds a
  pointer into a module's code: a field, or a global in a writable section.
  Read-only pages and sections, code, the import table, a value that is not
  code and a pointer already recorded are refused; nothing changes a page's
  protection. `dry` checks and shows the swap without writing.
- **What is seen:** only calls through that pointer: virtual calls on that
  object, or calls through that cell. Direct, devirtualised and inlined
  calls, and the same function through another object, are not seen; the
  result's `note` says so. `this` keeps only calls whose rcx is that object.
- **Each row:** `args` (rcx, rdx, r8, r9) and `xmm` (the low 8 bytes of
  xmm0..3) as hex, in the order the x64 convention passes them (a struct
  returned through a hidden pointer shifts the arguments one register on);
  `ret`, where the call returns, with its module, rva and `ret_name` (the
  labelled function holding it, from the labels files); `bytes`, with
  `copy`, up to 64 bytes at an argument register, read under a fault guard
  (`copy_faulted` when unreadable). No return value.
- **Stopping:** at the end, or `calls-read` with `stop`, the pointer goes
  back only if it still holds the thunk; otherwise something else swapped it
  since and it is left alone (`state: replaced`) while the thunk keeps
  forwarding. Stopping the last entry recorded on an object puts its vptr
  back. 256 rows per recording, the newest kept; eight recordings of every
  kind at once.

Which recorder: a reflected function (Unreal) is `calls-start`, with its
parameters decoded by name; a virtual function or a function pointer is
`native-calls-start`, with raw registers.

## Pointer chains

An address may be a chain, `{"base": <addr>, "chain": [16, 8]}`, in `read`,
`peek`, `mem-snap`, `vtable`, `disasm`, `xrefs`, `touches` and `sig-bytes`
(not `owner`, whose addresses are search rows, nor `write`). Each offset but the last reads a
pointer: base X with chain `[a, b]` is `*(X + a) + b`. A chain of one
(`[a]`) follows no pointer, so it is refused rather than answered with the
bytes at `X + a`: to read through the pointer at `X`, start with `0` (`[0,
48]` is `*X + 0x30`); the refusal spells both fixes. A null or unreadable
hop fails with error kind `chain` naming the hop. `write` (timed too)
refuses chains: resolve the address first and write to what was read, never
through a chain that may have changed.

## Regions and strings

`read` and `peek` add `region`, what the memory at the address is (one
`VirtualQuery`): its protection and a one-word `kind` (`code`, `rodata`,
`data` inside an image; else `mapped`, `private`, `reserved`, `free`; heap
and stack are not told apart). `peek` labels a readable pointer `heap` only
when its memory is private.

`read` with `type` `cstr` (or `wstr` for UTF-16) adds `text`, with
unprintable characters escaped, never dropped.
