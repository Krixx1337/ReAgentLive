# Fast lane

*Home of:* code questions from live bytes: `disasm` (and saves for a static tool), `xrefs`, `touches`, `disp-scan`, `search` with `type` `text` and the signature commands. Why the lane is built this way: [design](../design/native.md#fast-lane).

**TL;DR:** Ask about one address and get an answer from the code as it is
loaded right now, in well under a second, with no disassembler database:
its instructions, who references it, which code touches a field, whether
a byte signature still matches. It answers one address at a time; whole
functions, types and call graphs stay with a static tool.

Use it while the static tool has no analysed database for this build, or
for a quick check. The lane: `peek` (what a global holds), `func` (a
reflected function's native entry), `disasm`, `xrefs`, `touches` and
`sig-check`.

```json
{"verb": "disasm", "args": {"addr": "0x140001000", "function": true}}
{"verb": "xrefs", "args": {"addr": "0x140001000", "all": true}}
{"verb": "touches", "args": {"addr": "0x140001000", "offsets": ["0x3e0", "0x3e4"], "reads": true}}
{"verb": "search", "args": {"type": "text", "value": "Fatal error: %s"}}
```

Every arg, its type and default: the [reference](reference.md#disasm).

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
code a packed or protected image leaves out of its directory, data, or
chance bytes. Function bounds are never guessed from padding or prologues;
a code row of `xrefs` or `touches` outside the directory carries `unlisted`
instead, with the bounds of the piece holding it (code cut at returns and
int3 padding), which may start with junk bytes. Entries under 4 bytes
bound no unlisted code: a function with unwind data holds at least a stack
adjustment and a return, and some packed images fill the directory with
one-byte decoys at a fixed stride that would cut it into slivers.

## disasm

`disasm` decodes up to `count` instructions (1..1024, default 24) and
8 bytes per instruction asked (at least 512, at most 16 KB) of live memory
from `addr`, which must be an instruction boundary
(the decoder is Zydis, in the console only). Reads of up to 4 KB; when part
of one is unreadable, up to the page end and on from there.
- `function` lists the whole function holding `addr`, from its start to
  its end: up to 2,048 instructions (`count` lowers it) and 16 KB. A cap
  that cuts it gives `next`; the same call with `next` as `addr` and
  `"resume": true` lists on from there (`resume` starts at `addr`, not at
  the function's start).
  An entry whose start does not decode (a packed image may begin one a
  few bytes early) is listed from `addr` instead, still bounded by it.
  An entry (or chunk) shorter than its first instruction, a one-byte
  decoy over real code, counts as no entry: the listing walks.
  When no entry holds `addr`, it walks from `addr` instead: the
  listing stops (`stop: "walk end"`) at the first return or unconditional
  jump that no earlier forward branch passes, counting branches up to
  16 KB ahead (farther ones are tail calls). `function` is then absent and
  `addr` is the start you gave; an early return before a branch target
  the walk has not seen yet can end it short.
- `follow`: at an unconditional direct jump the listing continues at the
  target, at most 3 times inside the same limits, listed in `followed`.
  With `function` each target's own entry bounds it. Calls, conditional
  jumps and indirect jumps are never followed.
- Per instruction (`rows`): `address`, `rva`, `bytes`, `text`; `target`
  (with `target_rva`, and `target_module` when it lies in another module) for a relative call, jump or branch
  and for a `[rip + disp]` operand; `string` when a RIP-relative target in
  a loaded module holds at least 4 printable characters and a terminator
  (ASCII, or UTF-16 with `wide: true`), cut at 96, read in held
  `read-batch`es for the whole listing; `import` when a `[rip + disp]`
  target is a slot of its module's import address table: the DLL and
  function the import directory names for it (`KERNELBASE.dll!Sleep`, or
  `!#12` by ordinal), read from the live headers; `fields` (`offset`, `size`, `kind`:
  `relative`, `rip`, `displacement`, `immediate`), which bytes of the
  instruction hold each value; `ends` on a return or unconditional jump.
- `value` when an operand `[rip + disp]` is read (not `lea`, not a
  branch) from a section that is not writable: a scalar with `value_type`
  (`f32`, `f64`, `i32`, `i64`, by the operand's element type and size),
  anything else (a packed mask, a 16-byte vector) as `value_hex`. A
  writable target is a global that changes: `peek` it. Import slots get
  `import` instead.
- Rows are compact: the listing names its `module` once. `"meta": true`
  on the call adds each row's `module`, every `target_module` and
  `fields`; the generated tools do not pass it. Under `function` rows are
  more compact still: no `bytes`, and `rva` only on the first row (targets
  keep `target_rva`); `meta` brings back full rows. For masks to build a
  signature from, use `sig-bytes`: its fields are kinded by what they
  point at (`call`, `jump`, `branch`, `rip`, `absolute`), not by encoding.
- `strings` (with `function`): each distinct string the rows reference,
  once, with `at`: the rows using it, in order. Only strings the code
  points at directly: text built at run time, reached through a pointer
  table, or held as names in a name pool is not seen.
- `size` is the bytes the rows cover; `stop` says why it ended (`count`,
  `bytes`, `function end`, `walk end`, or `end`'s reason); `next` where
  `count` or `bytes` cut it. Bytes that do not decode, an instruction running past the byte
  limit or the function end, or into unreadable memory end it with `end`
  {`address`, `offset` (bytes listed before it), `reason`}, never a guess.
- No summary of what a function does, no names beyond `string` and `import`.
- **Small answers, big listings in files.** The default `count` stays
  small: `disasm` answers about one address. A whole function or more goes
  to a save file for the static tool (`save`), not into a reply an agent
  reads.

### Saving a function for a static tool

`disasm` with `function` and `save` also writes the listed function
for a static tool, so it decompiles that one function without analysing
the whole image:

```json
{"verb": "disasm", "args": {"addr": "0x140001000", "function": true, "save": {"file": "fn.jsonl", "callees": 1}}}
```

The result names the file in `saved` and counts its
instructions in `saved_rows`; it leaves out `rows` and `strings`, which
the file holds. A function's **chunks** (continuations the exception
directory lists apart, reached by a jump from the function or following
on where its entry ends mid-flow, and switch targets) are saved with it,
each to its own end, and listed in `chunks`; the loader attaches them as
function tails, so the decompiler shows the whole function instead of a
`JUMPOUT`. **Switches:** a jump through a register fed by a table of
32-bit entries (the MSVC form `cmp idx, N; ja; lea; mov r, [base +
idx*4 + table]; add r, base; jmp r`, with or without a byte index table,
and the table-relative form) is recognised; its table is read (bounded
by the `cmp` before it), saved as `data` (the function's code stops where
a table after the jump starts) and listed in `tables` (`jump`, `table`,
`targets`), and each target the saved parts do not hold becomes a chunk
(listed in no entry: walked from the target). Up to 256 chunks per file,
then `capped: true`; targets of the function still unsaved are listed in
`jumpouts_left`.

- **`save.callees`** (1..3) also saves the functions it calls directly or
  reaches by a tail jump (a direct `jmp` out of a function and its chunks),
  that many levels deep, breadth-first, each once, each listed from the
  call target (an entry that begins elsewhere only bounds its end): calls
  between saved functions then read as names, and every callee decompiles
  from the same database. The caps hold for the whole save: 16 functions
  besides the first and 256 KB of code (every function and chunk). The
  result lists each saved function in `functions` with its `depth` (0: the
  first), and each target left out in `skipped` with `why`: `depth` (a
  call of the deepest level), `count`, `bytes` or `unreadable`. Three
  levels is the most: past that a call graph is the static tool's job.
- **Names** come from the console's labels file and the host's names
  file, or `save.labels` alone ([labels](labels.md#labels-files)): names
  you gave in IDA (`user`), the globals `peek` with `label` confirmed, exec
  thunks and interface implementations; the highest-ranked row per address.
- **Types travel with the save.** With an engine pack, each saved function
  whose label names a reflected class (`Class::Method`, or `Class_Method`
  by the longest prefix that is a class or struct) brings that class's
  layout into the file: `layout-export`'s lines with `closure` 1 (its super
  chain and the types its fields name, one step), as `layout` rows. The
  result names the classes and counts the types in `layouts`. The loader
  makes them local types and types those functions `Class *this`, so the
  first decompile shows `this->Field`.
- **No partial saves.** A walk (no exception entry) from more than 32
  bytes past the last return, unconditional jump or padding in its
  unlisted piece adds `piece`, the piece holding the address, and `save`
  then refuses: the file would hold only the rest of a function. A start
  lies right after one of those or behind a few junk bytes (a function
  after a tail jump shares its piece with the code before it); a call
  site lies far past one. Save from the function's start (junk bytes may lead the
  piece: `disasm` its first bytes), or pass `resume` to save from the
  address anyway. A listing with no rows is never saved: `save` refuses
  it (`start`), naming why it stopped.

JSON lines:

| `row` | Keys |
|---|---|
| `function` | `module`, `module_size`, `timestamp` (the build), `base`, `start`, `end`: one row per saved function, the first first |
| `chunk` | `start`, `end`, `function`: a chunk (chained entry or switch target) of the function starting at `function` |
| `code` | `address`, `rva`, `bytes` (hex): the listed code of every saved part, one row per contiguous run; runs that overlap or touch are joined, so no two rows overlap |
| `data` | `address`, `rva`, `bytes`: each read-only constant the code reads (at least 8 bytes) and each string it references, up to 64 bytes each, merged where they touch, and each switch table whole; bytes a `code` row holds are left out |
| `name` | `address`, `rva`, `name`, `what`: one per address, the first kind winning: `function` (its label, else `fn_<rva>`), `import` (each slot called through), `call` (a direct call target not saved: its label, else `fn_<rva>`), `tail` (a tail jump target outside every saved part, named the same way), `global` (a labelled RIP-relative target) |
| `layout` | a `layout-export` line (`kind` `class`, `struct`, `field` or `enum`, with its keys): the layouts of the saved methods' classes, last in the file |

The [IDA loader](ida-loader.md) opens a save as a small database, grows
one workspace database from later saves (`add_saved`, `fetch`) and
decompiles them. Start a save at the function's start: when the address
is a call site in unlisted code, an `xrefs` row marked `unlisted` gives
the piece holding it, whose start may sit behind junk bytes.

## xrefs

`xrefs` lists what points at `addr` in one module: the one holding it,
or `module` (a DLL that calls into the exe). Two scans, each on the
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
  chance bytes: counted, listed only with `all`.
- Each row: `kind`, `field` (where the offset sits), `instruction` for
  `call`, `jmp`, `jcc`, `lea`, `load` and `store`, whose length before the
  field is fixed (none for `rip`: a linear decoder cannot walk backwards;
  `disasm` with `function` shows it in place), `module` and `rva` of the
  instruction (else the field), and `function` for that address. Where
  no entry holds a code row, `function` is the piece of unlisted code
  holding it and the row carries `unlisted`: cut as `touches` cuts such
  code (between returns and int3 padding, within 16 KB each side), so a
  function with several returns spans several pieces. A row other than
  `pointer` without `function` lies outside the executable sections or in
  no piece: likely chance bytes.
- `kinds` counts every hit per kind, listed or not; `capped` when a cap
  cut a scan; `scan_ms` for both scans.
- An agent without the op answers `unsupported`.

## touches

`touches` finds the code behind a member that reflection does not
name: a block of state inside a `Pad_` array, a native field between
reflected ones. Give an address next to that code (a reflected function's
`exec` from `func`: native code of one class sits together) and up to 8
offsets that should appear together.
- Every exception-directory entry starting within `range` bytes of the
  address (default 0x80000, at most 0x400000, each side) is decoded whole;
  the code is read once, through the agent's
  [image window](../protocol.md#handshake) (4 MB a request) when it has
  one, else 4 KB per held request.
- Executable code no entry lists (leaf functions, code a packed image
  leaves out of its directory) is decoded too, in pieces cut after each
  return and around int3 padding. Its rows carry `unlisted: true`, with the
  piece's bounds as `function`; `scanned.unlisted` counts the pieces. A
  piece can start mid-instruction and hold more than one function: confirm
  it with `disasm` and `function` at a use.
- A use is a memory operand `[reg + disp]` whose displacement is an asked
  offset; `[rip + disp]` (globals) is never a member, and the stack
  frame's bases (`[rsp + disp]`, `[rbp + disp]`: locals) are left out
  unless `bases` names them. `bases` (a list of 64-bit registers) keeps
  only operands on those: `["rcx"]` for `this` as a method's first
  argument. `write` is set when the operand is written.
- **rbp is not only a frame pointer.** Optimised code uses it as a plain
  register, struct pointers included: in one sweep of a large game image,
  36% of the byte writes to one field went through rbp. Near one function
  the default (rbp left out) keeps frame locals out of the rows; for "who
  writes field X", pass `bases` with rbp among the registers you take, or
  sweep the image with `disp-scan`, whose default keeps rbp.
- Uses group by function (a continuation joins its function). Rows rank by
  how many distinct asked offsets the function uses, then writers first,
  then by uses: the setup that writes most of a block comes first, its
  readers after. At most 24 rows of 12 uses (`total` counts all of each);
  `capped` when more functions matched.
- `reads` looks for the code that consumes a block: rows rank by the
  distinct offsets read (`distinct`), and a row that reads none, or writes
  every offset it reads (a struct copy, a constructor), is left out.
- Unrelated classes share small offsets: ask for several that only one
  layout has together, and confirm the top row with `disasm` and `function` or
  a decompiler before naming anything.

## disp-scan

`disp-scan` answers "does anything write these fields" for the whole main
module, where `touches` looks near one function:

```json
{"verb": "disp-scan", "args": {"disps": ["0xD0", "0xB8"], "access": "write"}}
```

- Every executable section is swept straight through from the first code
  byte or `view.start`. One call works for up to 8 s or `view.limit` hits
  (default 128, at most 4096), then answers its page: `rows` (each
  instruction with `rva`, `disp` (which asked displacement), `base`,
  `access`, `width`, its `text` and `function`, the exception-table entry
  holding it), `scanned` (the share of the code behind the cursor),
  `scan_ms` and `read_ms` (the part spent reading), and `next_cursor`,
  which the next call passes as `view.start`; it is absent once the end is
  reached. The console decodes: no agent job, nothing left running between
  calls.
- **Reading the code.** With the agent's
  [image window](../protocol.md#handshake) the console reads 4 MB a
  request, copied by the agent's worker thread when the host allows
  background search (else in 2 ms tick slices), so the read costs about
  two host ticks per 4 MB and the decoding (about 40 MB of code a second)
  dominates: a full sweep of a game's 160 MB of code is a few seconds of
  reading and about four of decoding, one or two pages. Without the window
  (an agent built before it) reads go 4 KB per held request through host
  ticks: under a megabyte a second on a 60-tick host, about 2 minutes and
  15 to 85 pages for the same image; start near the code you suspect then
  (a class's methods sit together). A loop over pages, or several sweeps,
  belongs in a Python script or a `run` plan, not in one tool call after
  another.
- `disps` takes up to 8 displacements at once; each row says which one
  matched. `same_base` keeps only functions that use every asked
  displacement on one base register (two disps or more): `functions` lists
  them (`function`, `base`, `hits`) and `rows` holds their hits. Hits
  outside every exception-table entry are left out there, and a page that
  would cut a function ends before it (`next_cursor` is its start).
- `access`: `write` (default; stores and read-modify-writes), `read`
  (loads and read-modify-writes) or `any` (also `lea`: the field's
  address taken, often passed to a function that writes it). `width`
  keeps operands of that many bytes, `imm` instructions whose immediate
  equals it in the operand's width (`mov dword [rcx+0x538], 1`). `bases`
  as for `touches`, but the default takes rbp: a field hunt over the whole
  image would miss the writers that hold the struct in rbp. Only `[rsp +
  disp]` (stack slots) is left out unless `bases` names rsp; a row's
  `base` shows when an rbp hit is a frame local instead.
- `access`: `write` (default; stores and read-modify-writes), `read`
  (loads and read-modify-writes) or `any` (also `lea`: the field's
  address taken, often passed to a function that writes it). `width`
  keeps operands of that many bytes, `imm` instructions whose immediate
  equals it in the operand's width (`mov dword [rcx+0x538], 1`). `bases`
  as for `touches`; the stack frame's are left out by default.
- A straight sweep decodes whatever lies in the code sections, jump
  tables included: a hit in data is rare but possible. Confirm a hit with
  `disasm` at its `function` before relying on it.

## search text

`search` with `type` `text` turns the text into bytes for the exact
search, so the first hop of a chain is one call. The `value` string is
taken exactly: spaces, `;` and `--` are text, and only JSON's own escapes
apply (a backslash in a path is written `\\`).

```json
{"verb": "search", "args": {"type": "text", "value": "Fatal error: %s", "scope": {"module": "game.exe"}}}
```

Each row carries `text`: the printable string the hit lies in (ASCII, or
UTF-16 with `type` `wtext`), up to 96 characters each side of the match with
`...` where it was cut, read back in batches after the search. A
`search-job` read of the same job carries it too, in a later console run
as well (the [cache file](console.md#fewer-round-trips) keeps the last
text search). A hit that is not inside text gets none. `xrefs` the string's start, not the hit,
when the match is inside a longer string.

## The chain

In an engine with no reflection, a system is named by
walking references one address at a time:

| Hop | Command |
|---|---|
| A source-path or assert string → the function using it | `search` (`type` `text`), then `xrefs` |
| The function → the table slot holding it | `xrefs` (`pointer`) |
| The table → the constructor or destructor installing it | `xrefs` |
| The constructor → the global it stores the object in | `disasm` |
| The global → the functions returning it | `xrefs` (`lea`, `load`) |
| A stub that only jumps → the real body | `disasm` with `follow` |

Running that chain over every table is a script over the Python client, or
the static tool's job, not a command. No verb learns a reference chain:
`run` executes a plan of calls and stays a generic runner, and `fetch`
in IDA takes the addresses a hop gave and returns their pseudocode, one hop
at a time.

## Signatures

Console only, on the `read`, `search` and `read-batch` ops. The agent or user makes
a byte signature; ReAgentLive checks it against the live image, which is what a
host's scanner reads, and says what to change when it fails.

```json
{"verb": "sig-check", "args": {"pattern": "48 8b 05 ?? ?? ?? ?? 48 85 c0 74", "expect": {"module": "game.exe", "rva": "0x1a2b3c"}}}
{"verb": "sig-check", "args": {"file": "patterns.jsonl"}}
{"verb": "sig-bytes", "args": {"addr": "0x140001000", "len": 32}}
```

Every arg: the [reference](reference.md#sig-check).

- **Pattern**, up to 96 bytes, in either form so it pastes into or out of a
  host's table: text (`48 8b ?? ?? 89`, `?` or `??` per wildcard) or a
  source-style list (`0x48, 0x8B, -1, -1, 0x89`; a comma selects it).
- **Module:** `module` names any loaded module (one loaded since the list
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
  `rows` shows the first 16.
- **`expect`** (any address form: `{"module": "game.exe", "rva": "0x1a2b3c"}`)
  adds `resolves`: whether the only match is that address. `offset` N is
  for a signature that starts N bytes inside the function: the match must
  be `expect + N`.

Feedback, each pointing at the next correction:

- **`mismatches`** (with `expect`, when no match lies there): each fixed
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
  `expect` without one), it points inside a loaded module (`target`,
  `target_module`, `target_rva`). They match today and break on the next
  build. Warnings, never refusals. With `expect` (and no `resolve`)
  the match starts at an instruction boundary the reader gave, so the
  console decodes the live bytes there and reports exactly the address
  fields of whole instructions (a relative target, a `[rip + disp32]`, a
  64-bit immediate), with `decoded`: how many pattern bytes the decoder
  read. Past where decoding stops (bytes that do not decode, or the pattern
  ending inside an instruction) the byte patterns above apply, and they
  apply alone without `expect`.
- **`resolve`** (`"next,field[,size]"`, size 1, 2 or 4, default 4) checks a
  reference signature: `target = match + next + signed value at match +
  field`; each row gets `resolved`, and `expect` is the target, so
  `resolves` proves a signature at a caller or at code that loads a global
  end to end. The field may lie in the 64 bytes read past the pattern.
  `offset` does not combine with it.

`sig-check` with `file` checks a JSON-lines file of bare signatures, one per
line (it takes no `pattern` or other key; a labels file's signed rows are
`labels-check`'s, [labels](labels.md#names-that-survive-a-patch)): `pattern` (text, or an array of bytes with `-1`), optional `module`,
`rva` (number or `0x` string: the expect inside that module), `offset`,
`resolve` (`"7,3"` or `[7, 3, 4]`); blank and `#` lines are skipped. Each
line gets its own row in `lines` (`line`, `ok`, then `result` or `error`);
anchor searches are shared between lines with the same module and anchor
(`searches` counts them). `winner` is the line number of the first
that is unique and, when it has an `rva`, resolves. So the agent tries
several candidates in one call, and a host checks every signature it ships.

`sig-bytes` (`len` default 64, up to 96: a longer `len` is cut to
96 and the result says `capped: true`) prints the live bytes in
both forms with `module` and `rva`, and `suggested`: the same bytes with
the address fields wildcarded, each listed with where it points. `addr` is
taken as an instruction boundary, so the fields come from the decoder
(`suggested.decoded` bytes; the byte patterns past that). A start for a
signature with no static tool: `disasm` shows where instructions end, so
end the pattern on a boundary. `func` gives a reflected function's native
entry to start from.

Python: `c.sig_check(pattern='48 8b ?? 89', module='game.exe')`,
`c.sig_check(file='patterns.jsonl')`, `c.sig_bytes(addr='0x140001000', len=64)`.
