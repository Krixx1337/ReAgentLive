# Result shapes

Keys of `result` per command. Lists are arrays of objects with the keys in
parentheses. Optional keys are in brackets. `tests/cli/result_tests.cpp`
pins the rows marked ✓; change both together. The Python column names the
[session client](../client.md#python-session-client) method that returns it; an empty
cell means a script uses `send` on purpose. A `ref` can be passed verbatim to
another command, including a separate run.

| Command | `result` keys | Pinned | Python |
|---|---|---|---|
| `find` | `objects` (`ref`, `name`, `class`), `total` (null after the first page), `from`, `next_cursor` (null when done; pass as `--from`), `truncated` | ✓ | `find` |
| `describe` | `ref`, `name`, `class`, [`class_size`: bytes, past the last reflected field] | ✓ | `describe` |
| `fields` | `ref`, `fields` (`name`, `owner`, `type`, `offset`, `size`, `label`, [`struct`, `bool_mask`, `element_type`, `element_size`, `value_type`, `value_size`, `value_offset`, `value_struct`, `stride`], [`native`, `stale`]), `truncated`, [`native_refused` (`line`, `name`, `owner`, `offset`, `reason`)] | ✓ | `fields` |
| `dump` | `ref`, `fields` (as `fields` plus `value`, `truncated`, [`value_module`, `value_rva`], [`object` {`ref`, `name`, `class`}, `preview` {`total`, `elements` (`value`, [`object`]; maps: `key`, `value`, [`key_object`, `value_object`])}, `preview_error`, `before`], [`native`, `stale`]), `truncated`, `object_lookup_limited`, [`filtered` {`shown`, `total`}], [`native_refused`] | ✓ | `dump` |
| `funcs` | `ref`, `functions` (`owner`, `name`), `from`, `next_cursor`, `truncated` | ✓ | `funcs` |
| `func` | `ref`, `function` (`Owner:Name`), `address`, `flags`, `exec` (null for none), `executable`, [`module`, `rva` or `module_error`], [`interface` {`offset`, `slot`, `implementation`, [`implementation_module`, `implementation_rva`]}] | ✓ | `func` |
| `params` | `ref`, `function`, `address`, `block_size`, `parameters` (`name`, `owner`, `type`, `offset`, `size`, `direction`, `flags`, [field extras]) | ✓ | `params` |
| `name` | `names` (`index`, `name` or null) | ✓ | `names` |
| `name-find` | `names` (`index`, `name`), `from`, `next_cursor` | ✓ | `name_find` |
| `entries` | `ref`, `field`, `kind` (`array`, `set`, `map`), `total`, `from`, `entries` (`value` or `key` and `value`, [`value_object`, `key_object`]), `next_cursor`, `truncated` | | `entries` |
| `func --script` | `ref`, `function`, `size`, `names` (`offset`, `count`, `index`, `name`), `functions` (`offset`, `count`, `ref`, `function`), `properties` (`offset`, `count`, `address`, `property`) | ✓ | `script` |
| `refs` | `index`, `name`, `functions` (`ref`, `owner`, `name`, `offset`, `count`), `capped` | ✓ | `refs` |
| `census` | `filter`, `classes`, `instances`, `ms`, `rows` (`class`, `ref`, `count`, `samples`), `from`, `shown`, `total`, `next_cursor`, `truncated`, [`hint`]; class refs become `$1..$N` | ✓ | `census` (pages to the end) |
| `census --changed` | `filter`, `classes` {`before`, `after`}, `instances` {`before`, `after`}, `ms`, `rows` (`class`, `ref`, `before`, `after`, `samples`), `from`, `shown`, `total`, `next_cursor`, `truncated`, [`hint`]; class refs become `$1..$N` | ✓ | `census(changed=True)` |
| `each` | `class`, `class_ref`, `target`, `mode` (`field` or `call`), `count`, `capped`, `rows` (`ref`, `value`, [`hex` with `--meta`]); with `--seconds` also `polls`, `changes` (`ms`, `tick`, `ref`, `before`, `after`). A call's value is its return value, or `return` plus `outputs` | | `each` |
| `call` / `release` | `ref`, `function`, `mode` (`raw`, `typed` or `release`), `block_size`, `hex` (the whole block after the call), `kept`, [`kept_reason`]; typed: `outputs` (`name`, `direction`, `value`, `truncated`; `--meta`: the field metadata too), `truncated` | | `call` (bytes; with `args` the result), `release` |
| `read` | `address`, `size`, `hex` (space-separated bytes in memory order), [`module`, `rva`], [`views`], [`values`: a list, or `{name: value}` for a layout], [`text`, `length`, `terminated` with `--as cstr\|wstr`], [`chain` {`base`, `hops` (`hop`, `address`, `value`, [`module`, `rva`]), `address`, [`module`, `rva`]} after a chain, also on `peek`, `vtable`, `disasm`] | ✓ chain | `read`, `read_chain` (bytes, chain), `read_str` |
| `write` | `address`, `size`, [`module`, `rva`] | | `write` |
| `search` | `hits` (`address`, [`module`, `rva`]), `count`, `resume` | | `search` (addresses) |
| `peek` | `address`, [`module`, `rva`], `count`, `rows` (`address`, [`module`, `rva`], `label` (`object`, `module`, `heap`, a region kind such as `mapped`, `value`, `unreadable`), `value`, [`object`, `value_module`, `value_rva`, `f32`, `f64`, `name_hint`]), [`labelled`] | ✓ rows | `peek` |
| `write --trial` | `id`, `address`, `bytes`, `original`, `owner`, `object`, `state`, `changed_tick`, `remaining_ms`, [`guard`]; `list`: `trials`; `clear`: `cleared`, `trials` | | `trial`, `trials`, `end_trial` |
| `label` | `path`, `line`, `owner`, `offset`, `type`, `name`, `size`, `timestamp` | | `label` |
| `get` | `ref`, `values` (`path`, `ok`, `value`, `address`, `object`, `truncated`, [`native`, `stale`], [`module`, `rva`, `value_module`, `value_rva`], [`type`, `hex` with `--meta`] or `error`), `same_tick`, [`tick_accurate: false` off the engine's thread] | | `get` (values) |
| `batch` | `count`, `all_ok`, `same_tick`, `first_tick`, `last_tick`, `spans_ticks` (`index`, `command`), `results` (one envelope each, with [`tick`, `last_tick`]), [`tick_accurate: false` off the engine's thread] | ✓ | `batch` |
| `snap` / `record` | `ref` with `size`, `path` (null without a file) / `path`, `samples` | | `snap`, `record` |
| `modules` | `modules` (`name`, `base`, `size`), the exe first | | `modules` |
| `export-labels` | `path`, `module`, `timestamp`, `rows`, `globals`, `exec`, `vtables`, `interfaces`, `interface_lookups`, `capped` | | `export_labels` |
| `sig --check` | `pattern`, `list`, `module`, `unique`, `count`, `hits` (`address`, `module`, `rva`, [`reference` or `diverges_at`, `resolved`, `resolved_module`, `resolved_rva`]), `length`, `fixed_bytes`, `anchor` {`offset`, `length`, `hits`}, `warnings` (`offset`, `size`, `kind`, `target`, `target_module`, `target_rva`), [`decoded`, `unique_at_length`, `resolve` {`next`, `field`, `size`}, `expect`, `expect_module`, `expect_rva`, `offset`, `resolves`, `mismatches` (`offset`, `wanted`, `found`) or null] | ✓ | `sig_check` |
| `sig --check --file` | `file`, `count`, `winner` (line number or null), `searches`, `results` (`line`, `ok`, `result` or `error`) | | `sig_check_file` |
| `sig --bytes` | `address`, [`module`, `rva`], `length`, `pattern`, `list`, `suggested` {`pattern`, `list`, `fixed_bytes`, `fields` (as `warnings`), [`decoded`]} | ✓ | `sig_bytes` |
| `disasm` | `address`, [`module`, `rva`], `count`, `bytes`, `stop`, `instructions` (`address`, [`module`, `rva`], `bytes`, `text`, [`target`, `target_module`, `target_rva`, `string`, `wide`, `fields` (`offset`, `size`, `kind`), `ends`]), `function` ({`start`, `end`, `module`, `rva`, [`chunk` {`start`, `end`}]} or null), [`end` {`address`, `offset`, `reason`}, `followed` (`from`, `to`, [`to_module`, `to_rva`])] | ✓ | `disasm` (instructions) |
| `region` | `address`, [`module`, `rva`, `section`], `kind` (`code`, `rodata`, `data`, `mapped`, `private`, `reserved`, `free`), `base`, `size`, `allocation_base`, `state`, `type`, `protect`, `allocation_protect` | ✓ | `region` |
| `vtable` | `address`, [`module`, `rva`], `offset`, `table` (null when the qword points into no module), [`table_module`, `table_rva`, `stop` (`count`, `not code`, `end of read`), `stop_value`, `found`], `count`, `slots` (`slot`, `offset`, `target`, [`module`, `rva`, `body`, `body_module`, `body_rva`]) | ✓ | `vtable` |
| `xrefs` | `address`, [`module`, `rva`], `scanned`, `count` {per kind: `call`, `jmp`, `jcc`, `lea`, `load`, `store`, `rip`, `pointer`, `unknown`}, `shown`, `rows` (`kind`, `field`, [`instruction`], `function` (as `disasm`'s), [`module`, `rva`]), `truncated`, `scan_ms` | ✓ | `xrefs` |
| `touches` | `address`, [`module`, `rva`], `offsets`, `range`, `scanned` {`from`, `to`, `functions`, `bytes`}, `count`, `rows` (`function` {`start`, `end`, [`module`, `rva`]}, `distinct`, `writes`, `count`, `uses` (`offset`, `instruction`, [`module`, `rva`], `write`, `text`)), `truncated`, `scan_ms` | ✓ | `touches` |
| `status` | `pid`, `build` (the agent's compile time), `version` (the console's ReAgentLive version), `replaced_agents`, `ticks`, `ticks_per_second`, `interval_ms`, `ticking`, `host_thread` (`engine`, `borrowed`, `own`), `roots` (what `watch --follow` can follow), `packs` (the engine packs the host offers: `["ue"]` or `[]`), [`tick_note` off the engine's thread] | | `status` |
| `watch` | `rows`, `capacity`, `complete`, `stopped`, `ranges` (`address` or `offset`, `length`, `guarded`), `then`, `samples` (`tick`, `us`, `values`) | | `watch` (samples, result) |
| `watch --start` | `started`, `capacity`, `ms`, `interval_ms`, `root`, `ranges` | | `watch_start` |
| `watch --read` / `--stop` | `running`, `stopped`, `rows`, `capacity`, `ms`, `interval_ms`, `root`, `ranges`, `samples` (`tick`, `us`, `values`, [`lost`, `resumed`, `root`]), [`gaps` (`from_us`, `to_us`) when following] | | `watch_read`, `watch_stop` |

Every line is an envelope: `command`, `ok`, and `result` or `error`. Errors
carry `kind` and `message`; client errors (`agent`, `timeout`, `attach`,
`busy`) also carry `status`, `detail` and `reason` (pinned ✓). Any envelope
can carry `timing` when the line ended in `--timing`.

Dump values: addresses are hex strings. FNames carry `index`, `number` and
`name`. Structs expand to `{Name: value}` (`members` with `--meta`). Arrays and strings show their header,
and the top-level arrays add a `preview`. Unsupported layouts give a bounded
`hex` prefix. Check `truncated` on the result and its rows before treating
values as complete. A dump can span several ticks.
