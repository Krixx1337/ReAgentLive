# Labels

*Home of:* labels files (format, ranks, which file each command reads and writes), `peek` with `label`, `labels-export`, signatures on labels (`labels-sign`), checking them (`labels-check`) and porting them after a patch (`labels-port`).

**TL;DR:** A labels file carries names for one or more builds of a module,
both ways between ReAgentLive and a static tool: `labels-export` derives
names from the engine, `peek` with `label` and the static tool's
`export_names` add the ones people confirm, and `disasm` saves name their
functions from it. `labels-sign` gives the confirmed names byte
signatures, and `labels-port` finds them again after a patch. Core
commands (T0), except `labels-export`, which needs the Unreal pack.

## Labels files

JSON lines `{module, module_size, timestamp, rva, name, kind}`; one file
holds several builds (image size and PE timestamp tell them apart, and
each command uses only the running build's rows). Per address the
highest rank wins: `user` (a name a person gave in the static tool),
`global` (`peek` with `label`), `exec`, `interface`, then the rest.

- **Derived names:** `<console>.labels.jsonl` beside the console, where
  `labels-export` writes by default: tens of thousands of rows, rebuilt in
  seconds, so it stays local.
- **Names people confirm:** the host's names file
  (`ConsoleConfig::labelsFile`, absolute or relative to the console): a
  host keeps it under version control, so names survive a clean build and
  are shared. Without one, the console's file holds both.

Which file each command uses by default (its `file` or `save.labels`
replaces it):

| Command | Reads | Writes |
|---|---|---|
| `peek` with `label` | | the host's names file, else the console's |
| `labels-export` | the console's file | the console's file |
| `disasm` with `save` | the console's file and the host's names file | |
| `labels-sign`, `labels-port` | the host's names file, else the console's | the same |
| `labels-check` | the host's names file, else the console's | nothing |
| the IDA loader's `export_names` | | the path it is given ([IDA loader](ida-loader.md#names-both-ways)) |

## peek with label

`peek` with `label` appends the one qword at `addr` to the labels file as
a confirmed global (`kind` `global`), named `name` (default
`FName_<hint>`). Use it once `peek` and the code agree on what a global
holds.

## labels-export

`labels-export` merges derived rows into the file; a kind not asked for
(`kinds`) costs no requests:

- `exec`: every native function's exec thunk (`Class::execName`), from
  the [exec entries](unreal-code.md#exec-entries) scan (game thread);
- `vtable`: each class default object's vtable (`vtbl_Class`);
- `interface`: native interface implementations found through `func` on
  native class default objects (`Class::Interface_Function`).

`global` derives nothing: globals come from `peek` with `label`; there is
no blind scan of globals. The merge keeps `user` and `global` rows, this
build's rows of kinds not derived now and every other build's lines.
Another tool's IDA mappings (Dumper-7's) are optional: imported first, they
name exec thunks, vtables and types, and `kinds` `["interface"]` adds only
what needs the running process.

## Names that survive a patch

A row may carry `sig`, a byte signature in `sig-check`'s terms:
`{pattern, offset}` for a function or `{pattern, resolve}` for a global (a
reference signature at code that loads it). `labels-export` and
`export_names` keep it, and a row's `sig` and `ported_from` follow the
address when it is renamed.

`labels-sign` signs this build's `user` and `global` rows that have none
(`retry`: all of them): the live bytes with address fields masked, cut on
whole instructions and grown until exactly one match lands on the row.
Derived rows are not signed: `labels-export` remakes them. A row it cannot
sign gets `sig_failed`, and later calls on that build skip it (counted in
`failed_before`), so a repeated call searches only for new rows. The signer
is one fixed loop ([design](../design/native.md#labels-sign-is-one-fixed-loop)).

A row `labels-sign` lists `unsigned`, or a name `labels-port` lists
`missing` once found again, is signed by hand: make the signature as for any
other ([signatures](fast-lane.md#signatures)), write it into the row's `sig`
(removing `sig_failed`) and re-verify every signed name with `labels-check`.

`labels-check` matches each signed row of the running build in the image
(no expected address: where it lands is the answer) and compares the match
with the row: `ok`, `moved` (one match elsewhere: the row's `rva` or its
`sig` is stale), `missing`, `ambiguous` or `invalid` ([results](results.md)).
It never writes the file: fix a row by hand, or `labels-sign` it with
`retry`. With the agent's [image window](../protocol.md#handshake) it reads
the image once and finds every anchor in that pass: seconds, where without
the window each anchor is its own search. The counts always cover every
row; `status` keeps only the rows that need work.

```json
{"verb": "labels-check", "args": {"status": ["moved", "missing", "ambiguous", "invalid"]}}
```

`labels-port`, after a patch: per name the running build lacks, the newest
signed row of another build is matched in the image. One match appends a row
for this build with the same `sig` and `ported_from`; `missing` (the code
changed) and `ambiguous` are listed, never written. Re-find the missing
ones, name them, then `labels-sign` the new rows.
