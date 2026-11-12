# Native objects

What the engine-agnostic side says about a native object, with no engine
pack ([design](../design.md#native-objects)).

```text
vtable <addr> [count] [--offset N] [--table] [--resolve]
<addr>->0x10->0x8                 a pointer chain, in read, peek, vtable, disasm and region
region <addr>                     what the memory there is
read <addr> [max] --as cstr|wstr  a C string (UTF-16 with wstr)
```

## vtable

`vtable` reads the table pointer at `addr` (an object's first 8 bytes;
`--offset N`: at `addr + N`, a secondary base or an interface part), or
takes `addr` as the table with `--table`, and lists its slots (`rows`): `slot`,
`offset` (slot × 8), `target`, `module`, `rva`. It stops at the first entry
that does not point into an executable section of a loaded module (`stop`
`not code`, with `stop_value`), at `count`, or where the read ended; one
read covers up to 512 slots. Sections come from each module's section
table, read once with its exception directory and kept in the cache file.
`--resolve` adds `body` (with `body_module`, `body_rva`) where a slot's
target is a stub of unconditional direct jumps: the address `disasm
--follow` would end at, one read per hop. A qword that points into no
module is a result: no `table`, and `found` holding the qword.

## Pointer chains

An address may be written `<addr>->0x10->0x8` (up to 16
offsets) in `read`, `peek`, `vtable`, `disasm` and `region`. Each `->off` but the last
reads a pointer: `X->a->b` is `*(X + a) + b`. The client library resolves
it (`Client::ResolveChain`, one held read per hop) and the command runs at
the end; its result adds `chain`: `base`, `hops` (`hop`, `address` where
the pointer was read, `value` it held, [`module`, `rva`]) and `address`. A
null or unreadable hop fails the command with error kind `chain`, the
message naming the hop, and the `chain` with `broken` and `reason` in place
of `address`. `write`, `pin` and trials refuse chains: resolve the address
first and write to what was read, never through a chain that may have
changed.

## region

`region` asks the agent's `region` op (one `VirtualQuery`) what the
memory at an address is: `base`, `size`, `allocation_base`, `state`
(`commit`, `reserve`, `free`), `type` (`image`, `mapped`, `private`,
`none`), `protect` and `allocation_protect` (`r`, `rw`, `rc` copy-on-write,
`x`, `rx`, `rwx`, `rcx` or `none`, with `+guard`, `+nocache`,
`+writecombine`), and a one-word `kind`: inside an image `code`, `rodata` or
`data` by its section (`section`, with `module` and `rva`; by protection
where no section holds it), else `mapped`, `private` (heap or stack: they
are not told apart), `reserved` or `free`. An address outside the address
space is `not found`. `peek` uses it: a readable pointer is `heap` only
when its memory is private, and otherwise labelled with its kind
(`mapped`); with an older agent every readable pointer stays `heap`.

## Strings

`read <addr> [max] --as cstr` (or `wstr` for UTF-16) reads
`max` bytes (default 256, up to the read limit; up to the page end when the
rest is unreadable) and adds `text`, `length` (characters before the
terminator, or every one read) and `terminated`. Unprintable characters are
escaped (`\n`, `\t`, `\r`, `\\`, `\xNN`; wide: `\uNNNN`, other
characters as UTF-8), never dropped.
