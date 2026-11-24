# Native objects

*Home of:* reading a native (non-reflected) object with no engine pack: `vtable` (and `rtti`, `parts`), pointer chains, the region `read` and `peek` report, C strings.

**TL;DR:** When a game has no engine pack, or an object is plain C++ that
reflection does not list, these commands still say what it is: its
vtable and the functions in it, its MSVC class name, what kind of memory
an address is, and the strings it points to. They only read; names they
report are hints to confirm, not proof.

What the engine-agnostic side says about a native object, with no engine
pack ([design](../design/native.md#native-objects)).

```json
{"verb": "vtable", "args": {"addr": "0x1f2e3d4c50", "rtti": true, "resolve": true}}
{"verb": "read", "args": {"addr": {"base": "0x1f2e3d4c50", "chain": [16, 8]}}}
{"verb": "read", "args": {"addr": "0x1f2e3d4c50", "type": "cstr"}}
```

A pointer chain works in `read`, `peek`, `vtable` and `disasm`; `type`
`cstr` reads a C string (`wstr`: UTF-16). Every arg:
[reference](reference.md#vtable).

## vtable

`vtable` reads the table pointer at `addr` (an object's first 8 bytes;
`offset` N: at `addr + N`, a secondary base or an interface part) and lists
its slots (`rows`): `slot`, `offset` (slot × 8), `target`, `module`, `rva`.
When `addr`'s own first qword already points into code, `addr` is the table
itself (an object's first qword points into read-only data instead). It
stops at the first entry that does not point into an executable section of
a loaded module (`stop` `not code`, with `stop_value`), at `view.limit`, or
where the read ended; one read covers up to 512 slots. `slots` reads up to
the highest slot asked and keeps only those rows. Sections come from each
module's section table, read once with its exception directory and kept in
the cache file. `resolve` adds `body` (with `body_module`, `body_rva`) where
a slot's target is a stub of unconditional direct jumps: the address
`disasm` with `follow` would end at, one read per hop. A qword that points
into no module is a result: no `table`, and `found` holding the qword.

### parts

A virtual call through an interface lands on a table inside the object,
not the first one. `parts` N reads the object's first N bytes and lists, in
`parts`, each distinct table found there (a qword that points into a module
but not into code, whose target's first entry is code): `offsets` (every
offset holding it, in order: embedded structs repeat one table), `table`
(with `table_module`, `table_rva`), and with `slots` those slots' `rows`. One
call answers which part's slot 15 an indirect call through `[rax+0x78]` reaches:

```json
{"verb": "vtable", "args": {"addr": "0x1f2e3d4c50", "parts": 4096, "slots": [6, 8, 15]}}
```

A pointer into a module's read-only data that happens to start with a code
address (a function-pointer array) shows up too: confirm a part with
`vtable` at its `offset`.

### rtti

`vtable` with `rtti` also reads the slot before the table and adds `rtti`: in an MSVC image built with run-time type information it points to
the class's complete object locator. The locator must say signature 1 and
name its own RVA (which gives the module's base); then its type descriptor
gives `name` (undecorated for plain and nested classes, `Game::Weapon`;
templates stay decorated) and `decorated` (`.?AVWeapon@Game@@`), and its
class hierarchy gives `bases`, every base it lists without the class
itself, up to 32 (`capped`). `complete_offset` is the part's offset inside the
complete object, so `complete` is the whole object when `addr` is a
secondary base or an interface part. Where the slot holds no locator (no
type information, another compiler, or not a virtual table) the slots stand
and `rtti_error` says why in place of `rtti`. Engines often build their game code without it; most native
programs and libraries keep it. Everything is read from live bytes in the
console; the agent only reads.

## Pointer chains

An address may be a chain, `{"base": <addr>, "chain": [16, 8]}` (up to 16
offsets), in `read`, `peek`, `mem-snap`, `vtable` and `disasm`. Each offset but the last
reads a pointer: base X with chain `[a, b]` is `*(X + a) + b`. A chain of
one (`[a]`) follows no pointer, so it is refused rather than answered with
the bytes at `X + a`: to read through the pointer at `X`, start with `0`
(`[0, 48]` is `*X + 0x30`); the refusal spells both fixes with the call's
own address and offset. The client library resolves
it (`Client::ResolveChain`, one held read per hop) and the command runs at
the end; its result adds `chain`: `base`, `hops` (`hop`, `address` where
the pointer was read, `value` it held, [`module`, `rva`]) and `address`. A
null or unreadable hop fails the command with error kind `chain`, the
message naming the hop, and the `chain` with `broken` and `reason` in place
of `address`. `write` (timed too) refuses chains: resolve the address
first and write to what was read, never through a chain that may have
changed.

## Regions

`read` and `peek` add `region`, what the memory at the address is, from
the agent's `region` op (one `VirtualQuery`): `base`, `size`, `protect`
(`r`, `rw`, `rc` copy-on-write, `x`, `rx`, `rwx`, `rcx` or `none`, with
`+guard`, `+nocache`, `+writecombine`) and a one-word `kind`: inside an
image `code`, `rodata` or `data` by its section (by protection where no
section holds it), else `mapped`, `private` (heap or stack: they are not
told apart), `reserved` or `free`. `region` is absent when the agent has no
`region` op or the query fails. `peek` also uses it per value: a readable
pointer is `heap` only when its memory is private, and otherwise labelled
with its kind (`mapped`); with an agent lacking the op every readable
pointer stays `heap`.

## Strings

`read` with `type` `cstr` (or `wstr` for UTF-16) reads
`len` bytes (default 256, up to the read limit; up to the page end when the
rest is unreadable) and adds `text`, `length` (characters before the
terminator, or every one read) and `terminated`. Unprintable characters are
escaped (`\n`, `\t`, `\r`, `\\`, `\xNN`; wide: `\uNNNN`, other
characters as UTF-8), never dropped.
