# Labels

*Home of:* labels files (format, ranks, which file each command reads and writes), `peek` with `label`, `export-labels`, signatures on labels (`labels-sign`) and porting them after a patch (`labels-port`).

**TL;DR:** A labels file carries names for one or more builds of a module,
both ways between ReAgentLive and a static tool: `export-labels` derives
names from the engine, `peek` with `label` and the static tool's
`export_names` add the ones people confirm, and `disasm` saves name their
functions from it. `labels-sign` gives the confirmed names byte
signatures, and `labels-port` finds them again after a patch. Core
commands (T0), except `export-labels`, which needs the Unreal pack.

## Labels files

JSON lines `{module, module_size, timestamp, rva, name, kind}`; one file
holds several builds (image size and PE timestamp tell them apart, and
each command uses only the running build's rows). Per address the
highest rank wins: `user` (a name a person gave in the static tool),
`global` (`peek` with `label`), `exec`, `interface`, then the rest.

- **Derived names:** `<console>.labels.jsonl` beside the console, where
  `export-labels` writes by default: tens of thousands of rows, rebuilt in
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
| `export-labels` | the console's file | the console's file |
| `disasm` with `save` | the console's file and the host's names file | |
| `labels-sign`, `labels-port` | the host's names file, else the console's | the same |
| the IDA loader's `export_names` | | the path it is given ([IDA loader](ida-loader.md#names-both-ways)) |

## peek with label

`peek` with `label` appends the one qword at `addr` to the labels file as
a confirmed global (`kind` `global`), named `name` (default
`FName_<hint>`). Use it once `peek` and the code agree on what a global
holds.

## export-labels

`export-labels` (`file`, `module`, `kinds`: default the console's file,
the exe and every kind; a kind not asked for costs no requests) merges
into the file:

- `exec`: every native function's exec thunk (`Class::execName`), from
  the [exec entries](unreal-code.md#exec-entries) scan (game thread);
- `vtable`: each class default object's vtable (`vtbl_Class`, named after
  a native class where one shares it with Blueprint classes);
- `interface`: native interface implementations found through `func` on
  native class default objects (`Class::Interface_Function`, at most 4096
  lookups, `capped` past that).

`global` derives nothing: globals come from `peek` with `label`. The
merge keeps `user` and `global` rows, this build's rows of kinds not
derived now and every other build's lines (`other_builds`). No blind scan
of globals. Another tool's IDA mappings (Dumper-7's) are optional:
imported first, they name exec thunks, vtables and types, and `kinds`
`["interface"]` adds only what needs the running process. The result
counts what was written: `total` rows of this build, of which `globals`,
`user`, `exec`, `vtables` and `interfaces`.

## Names that survive a patch

A row may carry `sig`, a byte signature in `sig-check`'s terms:
`{pattern, offset}` for a function (it lies `offset` bytes before the
match, usually 0) or `{pattern, resolve}` for a global (a reference
signature at code that loads it). With the row's `rva` as the expected
address it is a line of a `sig-check` `file`. `export-labels` and
`export_names` keep it, and a row's `sig` and `ported_from` stay when it
is renamed: they follow the address.

`labels-sign` (`file`, `module`, `again`) signs this build's `user` and
`global` rows that have none (`again`: all of them). A function's pattern
starts at it, a global's at the first code that loads it; each is the live
bytes with address fields masked (as `sig-bytes` suggests), cut on whole
instructions and grown from 16 bytes (to `unique_at_length` when
`sig-check` knows it) until exactly one match lands on the row, up to 96.
Derived rows are not signed: `export-labels` remakes them. A row it cannot
sign gets `sig_failed` (`timestamp`: the build tried, `reason`), and later
calls on that build skip it and count it in `failed_before`, so a repeated
call searches only for new rows; `again` tries it too. Signing the row
drops the mark; `labels-port` never copies it. The signer is one fixed
loop ([design](../design/native.md#labels-sign-is-one-fixed-loop)).

A row `labels-sign` lists `unsigned` (with its `reason`, kept in its
`sig_failed`), or a name `labels-port` lists `missing` once found again,
is signed by hand: make the signature as for any other
([signatures](fast-lane.md#signatures)), write it into the row's `sig`
(removing `sig_failed`) and check the file's signatures with `sig-check`
and its `file`.

`labels-port` (`file`, `module`), after a patch: per name the running
build lacks, the newest signed row of another build of the module is
matched in the image (one shared search per anchor). One match appends a
row for this build with the same `sig` and `ported_from` (the old
timestamp); `missing` (no match: the code changed) and `ambiguous`
(several) are listed, never written. Old rows stay, `present` counts names
this build already has and `unsigned` names with no signature to port.
Re-find the missing ones, name them, then `labels-sign` the new rows.
