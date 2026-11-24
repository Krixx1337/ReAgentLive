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
`sig-check`. Every arg, its type and default: the
[reference](reference.md#disasm).

```json
{"verb": "disasm", "args": {"addr": "0x140001000", "function": true}}
{"verb": "xrefs", "args": {"addr": "0x140001000", "all": true}}
{"verb": "touches", "args": {"addr": "0x140001000", "offsets": ["0x3e0", "0x3e4"], "reads": true}}
{"verb": "search", "args": {"type": "text", "value": "Fatal error: %s"}}
```

## Function bounds

`disasm` and `xrefs` report `function`: the entry of the module's exception
directory that holds the address. With no entry, `function` is absent: a
leaf function, code a packed image leaves out, data, or chance bytes.
Bounds are never guessed from padding or prologues; a code row of `xrefs`
or `touches` outside the directory carries `unlisted` instead, with the
bounds of the piece holding it (cut at returns and int3 padding), which may
start with junk bytes. Entries under 4 bytes bound nothing: some packed
images fill the directory with one-byte decoys that would cut code into
slivers.

## disasm

`disasm` decodes live memory from `addr`, which must be an instruction
boundary (the decoder, Zydis, is in the console only).

- `function` lists the whole function holding `addr`; a cap that cuts it
  gives `next`, and the same call with `next` as `addr` and `"resume": true`
  lists on. With no entry it walks from `addr` and stops at the first
  return or unconditional jump no earlier forward branch passes (`stop`
  `walk end`, `function` absent; an early return before a branch target not
  yet seen can end it short).
- `follow` continues through unconditional direct jumps only. Calls,
  conditional and indirect jumps are never followed.
- Rows carry targets, RIP-relative strings, import names and constants read
  from read-only sections; a writable target is a global that changes:
  `peek` it. Bytes that do not decode end the listing with `end`, never a
  guess. Row keys: [results](results.md). `sig-bytes` is the tool for masks.
- `strings` (with `function`) lists each distinct string the code points at
  directly; text built at run time or held in a name pool is not seen.
- No summary of what a function does.
- **Small answers, big listings in files.** `disasm` answers about one
  address, so the default `count` is small; a whole function or more goes to
  a save file for the static tool, not into a reply an agent reads.

### Saving a function for a static tool

`disasm` with `function` and `save` also writes the function for a static
tool, so it decompiles that one function without analysing the whole image:

```json
{"verb": "disasm", "args": {"addr": "0x140001000", "function": true, "save": {"file": "fn.jsonl", "callees": 1}}}
```

The console writes `file` as given, so pass an absolute path (a relative
one lands in the console's or MCP server's working folder). The result
names the file in `saved` and leaves out `rows` and `strings`, which the
file holds. The [IDA loader](ida-loader.md) opens it as a small database.

- **Chunks and switches.** A function's continuations (chained unwind data,
  switch targets) are saved with it, so the decompiler shows the whole
  function instead of a `JUMPOUT`. MSVC switch tables are recognised, read
  and saved as data.
- **`save.callees`** also saves direct callees and tail-jump targets,
  breadth-first, each once, so calls between saved functions read as names.
  Three levels is the most: past that a call graph is the static tool's job.
  Targets left out are listed in `skipped` with the reason: `depth`, count
  past 16 callees, bytes past 256 KB of code in the whole save, or
  unreadable. Each saved function carries its depth in `functions`.
- **Names** come from the console's labels file and the host's names file
  ([labels](labels.md#labels-files)); the highest-ranked row per address.
- **Types travel with the save.** With an engine pack, a saved function
  whose label names a reflected class brings that class's layout (the lines
  of `layout-export`), so the first decompile shows `this->Field`.
- **No partial saves.** A walk starting far past the last return in its
  unlisted piece would save only the rest of a function, so `save` refuses.
  Save from the function's start (an `xrefs` row marked `unlisted` gives the
  piece holding a call site), or pass `resume` to save from the address
  anyway. A listing with no rows is never saved.
- The file is JSON lines, one row kind per line (`function`, `chunk`, `code`,
  `data`, `name`, `layout`, and `console`, which holds the console's paths so
  a static tool finds the console and its labels files); the loader is the
  contract, not this page.

## xrefs

`xrefs` lists what points at `addr` in one module (the one holding it, or
`module`). Two scans: relative (a 4-byte field that lands on `addr`, a call,
jump, `lea`, load or store) and absolute (the 8-aligned pointer: vtable
slots, tables).

- Each row's kind comes from the bytes in front of the field. A relative hit
  that matches none is `unknown`, most likely chance bytes: counted in
  `kinds`, listed only with `all`. `capped` says a scan was cut.
- `rip` rows carry no `instruction`: a linear decoder cannot walk backwards;
  `disasm` with `function` shows it in place.
- A row with no entry holds `function` as an `unlisted` piece, cut as
  `touches` cuts it: up to 16 KB each side of the hit, so a function with
  several returns spans several pieces. Entries under 4 bytes (a packed
  image's decoys) count as unlisted.

## touches

`touches` finds the code behind a member reflection does not name: a block
of state inside a `Pad_` array, a native field between reflected ones. Give
an address next to that code (a reflected function's `exec` from `func`:
native code of one class sits together) and up to 8 offsets that should
appear together. It decodes every function within `range` of the address,
plus unlisted code in pieces.

- A use is a `[reg + disp]` operand whose displacement is an asked offset.
  `[rip + disp]` (globals) is never a member; stack bases are left out
  unless `bases` names them. `reads` looks for the code that consumes a
  block instead of the code that writes it.
- **rbp is not only a frame pointer.** Optimised code uses it as a plain
  register, struct pointers included: in one sweep of a large game image,
  36% of the byte writes to one field went through rbp. For "who writes
  field X", pass `bases` with rbp, or use `disp-scan`, whose default keeps
  rbp.
- Rows rank by how many distinct asked offsets a function uses, writers
  first: the setup that writes most of a block comes first.
- Unrelated classes share small offsets: ask for several that only one
  layout has together, and confirm the top row with `disasm` and `function`
  or a decompiler before naming anything.

## disp-scan

`disp-scan` answers "does anything write these fields" for the whole main
module, where `touches` looks near one function:

```json
{"verb": "disp-scan", "args": {"offsets": ["0xD0", "0xB8"], "access": "write"}}
```

- It sweeps every executable section straight through and answers in pages:
  `next_cursor` is the next call's `view.start`. The console decodes: no
  agent job, nothing left running between calls. With the agent's
  [image window](../protocol.md#handshake) a sweep takes seconds; without it
  reads go through host ticks and take minutes, so start near the code you
  suspect. A loop over pages belongs in a Python script or a `run` plan.
- `access` is `write` by default; `read` and `any` (also `lea`: the field's
  address taken, often passed to a function that writes it) widen it.
  `width` and `imm` narrow it (`imm` compares in the operand's width:
  0xffffffff is -1 as 32 bits). `same_base` keeps only functions that use
  every asked displacement on one base register.
- `bases` takes rbp by default (a field hunt over the whole image would miss
  writers that hold the struct in rbp); only stack slots are left out.
- Each call scans up to 8 s and stops at `view.limit` hits (default 128, at
  most 4096); `scanned` is the share of the code behind the cursor and
  `read_ms` the share of `scan_ms` spent reading. `same_base` answers whole
  functions, up to that many rows, and cuts a page at a function's start.
  The image window is copied by the agent's worker thread while the host
  tick only polls; without it a sweep reads about a megabyte a second.
- A straight sweep decodes whatever lies in the code sections, jump tables
  included: a hit in data is rare but possible. Confirm a hit with `disasm`
  at its `function`.

## search text

`search` with `type` `text` turns the text into bytes for the exact search,
so the first hop of a chain is one call. The `value` string is taken
exactly: spaces, `;` and `--` are text, and only JSON's own escapes apply.
Each row carries `text`, the printable string the hit lies in (UTF-16 with
`type` `wtext`); `xrefs` the string's start, not the hit, when the match is
inside a longer string.

```json
{"verb": "search", "args": {"type": "text", "value": "Fatal error: %s", "scope": {"module": "game.exe"}}}
```

## The chain

In an engine with no reflection, a system is named by walking references
one address at a time:

| Hop | Command |
|---|---|
| A source-path or assert string → the function using it | `search` (`type` `text`), then `xrefs` |
| The function → the table slot holding it | `xrefs` (`pointer`) |
| The table → the constructor or destructor installing it | `xrefs` |
| The constructor → the global it stores the object in | `disasm` |
| The global → the functions returning it | `xrefs` (`lea`, `load`) |
| A stub that only jumps → the real body | `disasm` with `follow` |

No verb learns a reference chain: running it over every table is a script
over the Python client, or the static tool's job.

## Signatures

Console only. The agent or user makes a byte signature; ReAgentLive checks
it against the live image, which is what a host's scanner reads, and says
what to change when it fails.

```json
{"verb": "sig-check", "args": {"pattern": "48 8b 05 ?? ?? ?? ?? 48 85 c0 74", "expect": {"module": "game.exe", "rva": "0x1a2b3c"}}}
{"verb": "sig-check", "args": {"file": "patterns.jsonl"}}
{"verb": "sig-bytes", "args": {"addr": "0x140001000", "len": 32}}
```

Every arg: the [reference](reference.md#sig-check). Method: the skill's
signatures reference.

- **Unique** means one match in the whole module image, data included:
  stricter than "one match in code", and safe whichever sections a scanner
  reads. No match is a result, not an error.
- **`expect`** adds `resolves`: whether the only match is that address.
  `offset` is for a signature that starts N bytes inside the function.
  `resolve` checks a reference signature end to end (target = match + next
  + the signed value at `field`).
- **Feedback points at the next correction:** `mismatches` (the bytes that
  differ from the live ones at `expect`), `diverges_at` and
  `unique_at_length` (where several matches separate).
- **`warnings` are not refusals.** Fixed bytes that hold an address that
  moves with every build (a rel32 after `call`, `jmp` or `jcc`, a
  `[rip + disp32]`, a fixed 8-byte pointer) match today and break on the
  next build. With `expect` the console decodes the live bytes there and
  reports exactly the address fields of whole instructions; without it, byte
  patterns alone.
- **`sig-check` with `file`** checks a JSON-lines file of bare signatures,
  one per line: `pattern`, optional `module`, `rva`, `offset`, `resolve`;
  blank and `#` lines are skipped. A labels file's signed rows are
  `labels-check`'s ([labels](labels.md#names-that-survive-a-patch)). The
  agent tries several candidates in one call, and a host checks every
  signature it ships; `winner` is the first line that is unique and, with an
  `rva`, resolves.
- **`sig-bytes`** prints the live bytes in both forms and `suggested`: the
  same bytes with address fields wildcarded, each listed with where it
  points. `disasm` shows where instructions end, so end the pattern on a
  boundary.

Python: `c.sig_check(pattern='48 8b ?? 89', module='game.exe')`,
`c.sig_check(file='patterns.jsonl')`, `c.sig_bytes(addr='0x140001000', len=64)`.
