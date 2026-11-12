# Fast lane

Questions about one address, answered from live bytes in well under a
second with no database. Use it while the static tool has no analysed
database for this build, or for a quick check; switch to the static tool
for whole functions, types, call graphs and anything that needs the whole
program ([design](../design.md#fast-lane)). The lane: `peek` (what a global
holds), `func` (a reflected function's native entry), `disasm`, `xrefs`,
`touches` and `sig-check`.

```text
disasm <addr> [count] [--function] [--follow]
xrefs <addr> [--module M] [--all]
touches <addr> <offset> [offset ...] [--range N]
search text <text|"quoted text"> [--wide] [search options]
```

## Function bounds

`disasm` and `xrefs` report `function`: the entry of the
module's exception directory (a 64-bit image lists every function with
unwind data there, sorted) that holds the address, as `start`, `end`,
`module`, `rva`, and `chunk` {`start`, `end`} when the address lies in a
continuation (chained unwind data; `start` and `end` are then the
function's first entry). A lookup is a few held reads: up to seven sampled
entries per `read-batch` while the range is wide, then one window of 340.
Sampled entries are kept for the console run, and the directory's location
goes in the cache file. With no entry, `function` is absent: a leaf function,
data, or chance bytes. Bounds are never guessed from padding or prologues.

## disasm

`disasm` decodes up to `count` instructions (1..64, default 24) and 512
bytes of live memory from `addr`, which must be an instruction boundary
(the decoder is Zydis, in the console only). One read; when part of it is
unreadable, up to the page end and on from there.
- `--function` starts at the start of the function holding `addr` and stops
  at its end; refused when no entry holds it.
- `--follow`: at an unconditional direct jump the listing continues at the
  target, at most 3 times inside the same limits, listed in `followed`.
  With `--function` each target's own entry bounds it. Calls, conditional
  jumps and indirect jumps are never followed.
- Per instruction (`rows`): `address`, `module`, `rva`, `bytes`, `text`; `target`
  (with `target_module`, `target_rva`) for a relative call, jump or branch
  and for a `[rip + disp]` operand; `string` when a RIP-relative target in
  a loaded module holds at least 4 printable characters and a terminator
  (ASCII, or UTF-16 with `wide: true`), cut at 96, read in held
  `read-batch`es for the whole listing; `fields` (`offset`, `size`, `kind`:
  `relative`, `rip`, `displacement`, `immediate`), which bytes of the
  instruction hold each value; `ends` on a return or unconditional jump.
- `size` is the bytes the rows cover; `stop` says why it ended (`count`,
  `bytes`, `function end`, or `end`'s reason). Bytes that do not decode, an instruction running past the byte
  limit or the function end, or into unreadable memory end it with `end`
  {`address`, `offset` (bytes listed before it), `reason`}, never a guess.
- No summary of what a function does, no names beyond `string`.

## xrefs

`xrefs` lists what points at `addr` in one module: the one holding it,
or `--module` (a DLL that calls into the exe). Two scans, each on the
agent's worker thread when the host allows background search, else in
tick slices, resident pages only:
- **Relative:** the `xrefs` agent op finds every 4-byte field that, read as
  a signed offset from its own end, lands on `addr`, or 1, 2 or 4 bytes
  before it (an immediate follows the displacement). At most 1,024 hits.
- **Absolute:** the pointer search for `addr`, 8-aligned (vtable slots,
  tables, stored pointers): kind `pointer`. At most 512 hits.
- **Kinds** come from the 4 bytes in front of each field (held
  `read-batch`es): `call` (`E8`), `jmp` (`E9`), `jcc` (`0F 8x`); behind a
  ModRM byte selecting `[rip + disp32]`, by opcode: `lea` (`8D`), `load`
  (`8A`, `8B`, `movzx`/`movsx`, `movsxd`), `store` (`88`, `89`, `C6` with
  an 8-bit and `C7` with a 32-bit or, after `66`, a 16-bit immediate), or
  `rip` for any other opcode. A relative hit with none of those in front,
  or with the wrong immediate for its opcode, is `unknown`, most likely
  chance bytes: counted, listed only with `--all`.
- Each row: `kind`, `field` (where the offset sits), `instruction` for
  `call`, `jmp`, `jcc`, `lea`, `load` and `store`, whose length before the
  field is fixed (none for `rip`: a linear decoder cannot walk backwards;
  `disasm --function` shows it in place), `module` and `rva` of the
  instruction (else the field), and `function` for that address. A row
  other than `pointer` without `function` may be chance bytes.
- `kinds` counts every hit per kind, listed or not; `capped` when a cap
  cut a scan; `scan_ms` for both scans.
- An agent without the op answers `unsupported`.

## touches

`touches` finds the code behind a member that reflection does not
name: a block of state inside a `Pad_` array, a native field between
reflected ones. Give an address next to that code (a reflected function's
`exec` from `func`: native code of one class sits together) and up to 8
offsets that should appear together.
- Every exception-directory entry starting within `--range` bytes of the
  address (default 0x80000, at most 0x400000, each side) is decoded whole;
  the code is read once, 4 KB per held request (1 MB, 5,858 functions
  took 0.5 s live).
- A use is a memory operand `[reg + disp]` whose displacement is an asked
  offset; `[rsp + disp]` (stack slots) and `[rip + disp]` (globals) are
  never members. `write` is set when the operand is written.
- Uses group by function (a continuation joins its function). Rows rank by
  how many distinct asked offsets the function uses, then writers first,
  then by uses: the setup that writes most of a block comes first, its
  readers after. At most 24 rows of 12 uses (`total` counts all of each);
  `capped` when more functions matched.
- Unrelated classes share small offsets: ask for several that only one
  layout has together, and confirm the top row with `disasm --function` or
  a decompiler before naming anything.

## search text

`search text` turns the text into bytes for the exact `search`, so the
first hop of a chain is one command. Quote a string to take it exactly:
`search text "Fatal error: %s" --module game.exe`. Inside the quotes,
spaces, `;` and `--` are text, `\"` is a quote and `\\` a backslash;
any other backslash is literal, so paths need no doubling. Unquoted words
are joined by single spaces up to the first option.

## The chain

In an engine with no reflection, a system is named by
walking references one address at a time:

| Hop | Command |
|---|---|
| A source-path or assert string → the function using it | `search text`, then `xrefs` |
| The function → the table slot holding it | `xrefs` (`pointer`) |
| The table → the constructor or destructor installing it | `xrefs` |
| The constructor → the global it stores the object in | `disasm` |
| The global → the functions returning it | `xrefs` (`lea`, `load`) |
| A stub that only jumps → the real body | `disasm --follow` |

Running that chain over every table is a script over the Python client, or
the static tool's job, not a command.

## Signatures

Console only, on `read`, `search` and `read-batch`. The agent or user makes
a byte signature; ReAgentLive checks it against the live image, which is what a
host's scanner reads, and says what to change when it fails.

```text
sig-check <pattern> [--module M] [--expect <addr>] [--offset N | --resolve next,field[,size]]
sig-file <patterns>
sig-bytes <addr> [len]
```

- **Pattern**, up to 96 bytes, in either form so it pastes into or out of a
  host's table: text (`48 8b ?? ?? 89`, `?` or `??` per wildcard) or a
  source-style list (`0x48, 0x8B, -1, -1, 0x89`; a comma selects it).
- **Module:** `--module` names any loaded module (one loaded since the list
  was read is found by one re-read). Without it, the exe: a pattern carries
  no address to say which image it is for.
- **Matching:** the longest run of fixed bytes (at least 4; the first 64 of
  it) is the anchor. An exact `search` finds it in the module, paged pages
  included (on the worker thread when the host allows background search,
  else in tick slices), and the bytes around each hit are read, 8 per held
  `read-batch`, and compared under the mask. More than 4,096 anchor hits is
  refused: lengthen the pattern. A match lies wholly inside the module.
- **Unique** means one match in the whole module image, data included:
  stricter than "one match in code", and safe whichever sections a scanner
  reads. No match is a result, not an error. `total` counts the matches;
  `rows` shows the first of them.
- **`--expect`** (any address form: `game.exe+0x1a2b3c`) adds `resolves`:
  whether the only match is that address. `--offset N` is for a signature
  that starts N bytes inside the function: the match must be `expect + N`.

Feedback, each pointing at the next correction:

- **`mismatches`** (with `--expect`, when no match lies there): each fixed
  byte that differs from the live bytes at `expect + offset` (`offset`,
  `wanted`, `found`); `mismatch_error` in its place when those bytes are
  unreadable. A slipped byte count or a typo shows at once.
- **`diverges_at`** (several matches): per row, the first offset past the
  pattern where its bytes differ from the reference row (`reference: true`:
  the expected one, else the first); absent when they agree for the 64 bytes
  read after it. `unique_at_length` is the length that separates them all,
  absent when one never diverges.
- **`warnings`:** fixed fields that hold an address that moves with every
  build: a rel32 after `E8` (`call`), `E9` (`jump`) or `0F 8x` (`branch`), a
  disp32 after a ModRM byte selecting `[rip + disp32]` behind an opcode the
  heuristic knows (`rip`, immediate accounted for), or a fixed 8-byte value
  (`absolute`), each flagged only when, read that way at the match (or at
  `--expect` without one), it points inside a loaded module (`target`,
  `target_module`, `target_rva`). They match today and break on the next
  build. Warnings, never refusals. With `--expect` (and no `--resolve`)
  the match starts at an instruction boundary the reader gave, so the
  console decodes the live bytes there and reports exactly the address
  fields of whole instructions (a relative target, a `[rip + disp32]`, a
  64-bit immediate), with `decoded`: how many pattern bytes the decoder
  read. Past where decoding stops (bytes that do not decode, or the pattern
  ending inside an instruction) the byte patterns above apply, and they
  apply alone without `--expect`.
- **`--resolve next,field[,size]`** (size 1, 2 or 4, default 4) checks a
  reference signature: `target = match + next + signed value at match +
  field`; each row gets `resolved`, and `--expect` is the target, so
  `resolves` proves a signature at a caller or at code that loads a global
  end to end. The field may lie in the 64 bytes read past the pattern.
  `--offset` does not combine with it.

`sig-file <patterns>` checks a JSON-lines file, one signature per
line: `pattern` (text, or an array of bytes with `-1`), optional `module`,
`rva` (number or `0x` string: the expect inside that module), `offset`,
`resolve` (`"7,3"` or `[7, 3, 4]`); blank and `#` lines are skipped. Each
line gets its own result or error; anchor searches are shared between lines
with the same module and anchor. `winner` is the line number of the first
that is unique and, when it has an `rva`, resolves. So the agent tries
several candidates in one call, and a host checks every signature it ships.

`sig-bytes <addr> [len]` (default 64, up to 96) prints the live bytes in
both forms with `module` and `rva`, and `suggested`: the same bytes with
the address fields wildcarded, each listed with where it points. `addr` is
taken as an instruction boundary, so the fields come from the decoder
(`suggested.decoded` bytes; the byte patterns past that). A start for a
signature with no static tool: `disasm` shows where instructions end, so
end the pattern on a boundary. `func` gives a reflected function's native
entry to start from.

Python: `s.sig_check(pattern, module=None, expect=None, offset=0,
resolve=None)`, `s.sig_file(path)`, `s.sig_bytes(address, length=64)`.
