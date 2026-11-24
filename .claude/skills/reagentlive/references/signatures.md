# Signatures

Verb behaviour: fast lane (`docs/commands/fast-lane.md#signatures`); labels
files: Unreal pack (`docs/commands/labels.md#names-that-survive-a-patch`).

## Two ways, never mixed up

- **A labels file's names:** `labels-sign`, the mass signer, after naming
  (one fixed loop; it never gets smarter). A name it lists `unsigned`, or
  one `labels-port` lists `missing` once found again, gets a signature made
  as below, written into its row's `sig` (`{pattern, offset}` or
  `{pattern, resolve}`) and re-verified with `labels-check` (every
  signed name of this build, one call; act on `moved`, `missing`,
  `ambiguous`, `invalid`). Never extend the signer for a failure. A failed row is marked and later calls skip it:
  rerun `labels-sign` freely; `retry` retries the marked rows; delete
  the mark when you write a hand-made `sig`.
- **Everything else** (a host's runtime patterns): you make it as below,
  and ReAgentLive checks it.

## Making one

1. Read the function in the static tool with bytes shown, at the RVA
   (`func` gives a reflected function's native entry).
2. Wildcard what moves with every build: relative call and jump targets,
   RIP-relative displacements, absolute addresses. Keep ordinary constants.
   Wildcard struct offsets and stack slots only when the signature should
   survive a recompile that moves fields.
3. Whole instructions only, within the checker's length and fixed-byte
   limits (signatures page above).
4. Function openings repeat. If the start is not unique, begin at a later
   instruction and record the offset (`expect` the entry, `offset` N).
5. Still not unique: sign an instruction that references the target (a
   caller, or code that loads the global) and record the resolve formula,
   `expect` set to the target. A call instruction (`E8 rel32`) at match
   offset C is `resolve` `"C+5,C+1"` (C = 0: `"5,1"`); `lea rcx, [rip+d]`
   at C is `"C+7,C+3"`.
6. `{"verb": "sig-check", "args": {"pattern": "48 8b 05 ?? ?? ?? ?? 48 85 c0 74", "expect": {"module": "game.exe", "rva": "0x1234"}}}`
   after every attempt; act on what it reports: mismatches, where it
   diverges or turns unique (the extra bytes must be ones you would keep
   fixed), warnings (a fixed field holds an address: wildcard).
7. Prefer the shortest unique pattern, then the most fixed bytes. Try
   several candidates in one `sig-check` with `file`. Then hand it to the host's checks.

Without a static database: `disasm` of the entry with `function` for
instruction boundaries, `{"verb": "sig-bytes", "args": {"addr": "0x7ff600001000", "len": 96}}`
at a boundary for `suggested` masks, and `xrefs` of the entry for callers
to build a reference signature from. Check
the decoded length covers the pattern.
