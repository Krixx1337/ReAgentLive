# Commands

**TL;DR:** Every console verb, the tier it needs and the page that explains it.
Find the verb for your task here, then read its page; what a verb takes and
returns is in `help` with `verb` and the [verb reference](reference.md).

Every console verb, the [tier](../tiers.md) it needs and its page. **T0**
commands work in any process, the own-thread example included. **T1-T3**
commands need a host built with the Unreal pack; on any other host they are
refused with a message saying so, and `status` lists the packs a host
offers (`packs`). **T3** commands also need the game thread
([which ones](unreal.md#off-the-engines-thread)).

| Command | Needs | Page |
|---|---|---|
| `read`, `peek`, `write`, `modules` | T0 | [memory](memory.md) |
| `write` with `hold`, `undo` or `clear`, `timed-writes` | T0; `undo`'s owner check needs T1 | [memory](memory.md#timed-writes) |
| `watch-start`, `watch-read` | T0 (`<ref>.<Field>` ranges: T2) | [memory](memory.md#watch) |
| `search`, `search-job` | T0 | [memory](memory.md#search) (text: [fast lane](fast-lane.md#search-text)) |
| `read` with a layout `type` | T0 | [memory](memory.md#typed-layouts) |
| `read` with `type` `cstr\|wstr`, pointer chains, `vtable`, its `rtti` | T0 | [native objects](native-objects.md) |
| `mem-snap`, `mem-diff` | T0 | [memory](memory.md#mem-snap-and-mem-diff) |
| `disasm`, `xrefs`, `touches`, `disp-scan` | T0 | [fast lane](fast-lane.md) |
| `sig-check` (one `pattern` or a `file`), `sig-bytes` | T0 | [fast lane](fast-lane.md#signatures) |
| `peek` with `label`, `labels-sign`, `labels-check`, `labels-port` | T0 | [labels](labels.md) |
| `labels-export` | T3 (exec thunks); other `kinds` T2 | [labels](labels.md#labels-export) |
| `find`, `describe`, `name`, `name-find`, `census` (and its `diff`) | T1 | [Unreal objects](unreal-objects.md) |
| `enum`, `assets-export` | T2 | [Unreal objects](unreal-objects.md) |
| `each` | T3 | [Unreal objects](unreal-objects.md#each) |
| `owner`, `route` | T3 | [Unreal objects](unreal-objects.md#owner-and-route) |
| `get` (every mode), `entries`, `rows`, `member-name`, `layout-export`, `layout-check` | T2 | [Unreal fields](unreal-fields.md#every-field-and-its-filters) (`snap`, `changed`) |
| `func`, `funcs` | T2 (`func`'s `params`: T3) | [Unreal calls](unreal-calls.md#func-and-funcs) |
| `call`, `call-pure`, `call-each`, `release`, `line-trace` | T3 | [Unreal calls](unreal-calls.md) |
| `calls-start`, `calls-read` | T3; the host lists the ops | [Unreal calls](unreal-calls.md#call-recorder) |
| `game-thread` | Unreal pack, a host linking `reagentlive::ue-draw`; `on` gives T3 | [Unreal calls](unreal-calls.md#game-thread) |
| `script` with `decode` | T2 (any thread) | [Unreal code](unreal-code.md#decode) |
| `script`, `name-refs` | T3 | [Unreal code](unreal-code.md) |
| `batch`, `timing` | T0 | [console](console.md#fewer-round-trips) |
| `run` (plan files) | T0 | [console](console.md#plans) |
| `status`, `help`, `quit`, `-c` | T0 | [console](console.md) |
| `usage`, `--caller`, `--no-usage` | T0; no target needed | [console](console.md#usage-log-and-usage) |

A host's own verbs (`help` lists them under its origin) come from the
host, not this library: see the host's docs.

What each verb takes and returns: [verb reference](reference.md)
and [result shapes](results.md). The Unreal pack's shared rules
(identity, layout groups, thread rules): [Unreal pack](unreal.md).
