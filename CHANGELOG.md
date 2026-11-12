# Changelog

ReAgentLive follows semantic versioning. Before 1.0 the API and the wire
layout can change in any minor release; agent and client are always built
from the same revision, and the handshake's layout check refuses a
mismatched pair. The version lives in `src/reagentlive/version.h`.

## Unreleased

- **The console describes itself; every caller breaks once.** One verb
  table says what each command takes and returns
  ([design](docs/design.md#self-describing-console)):
  - `help` lists the verbs, `help <verb>` shows flags, arguments and the
    result's keys, `help --json` prints the table (no target needed);
    `status` carries `table_hash`. A usage error ends with the verb's
    usage line; unknown flags are refused.
  - Modes are verbs: `sig-check`, `sig-file`, `sig-bytes`, `watch-start`,
    `watch-read`, `watch-stop`, `pin-list`, `pin-clear`, `trial`,
    `trial-list`, `trial-clear`, `census-diff`, `script`. `peek --label`
    names with `--name`.
  - Result keys: a result's list is `rows`; pages carry `from` and
    `next_cursor`; `capped` (rows cut) and `truncated` (a value
    incomplete) each mean one thing; keys that do not apply are omitted
    (no nulls); counts that repeated `rows` are gone, `total` counts all
    matches. `read --as "<layout>"` fills `layout`.
  - Results are typed structs written from one field list, checked
    against their row before printing; `docs/commands/results.md` is
    generated from the table.
  - The Python client's results are strict (`ResultKeyError` on a key the
    type lacks) with one method per verb; reshaping helpers are named for
    what they return.
  - `ConsoleConfig::hostHelp` is gone: a host gives rows
    (`ConsoleConfig::verbs`) for its own verbs.

- The console keeps the core and the Unreal pack apart: each `frontends/cli`
  layer has a `ue/` folder for the pack's part, the core reaches the pack
  only through `console/pack.h`, and the layout test enforces both.
- `status` lists the packs the host offers (`packs`); a pack's verbs on a
  host without it are refused with a message that says so; `help` groups
  the verbs into core and Unreal pack.
- `Ref` moved to the core protocol. The cache file keeps each pack's state
  in its own block (format 8; older files are dropped).
- `get` resolves every path, then reads the values back to back, so they
  land in one host tick (`same_tick`) instead of splitting over the agent's
  hold wait; warm field tables are shared instead of copied.
- Fixed: an address-only `$N` (a pointer `get` followed) made the next run
  drop the whole cache file; it is now stored as its address.

## 0.1.0

First versioned release.

- In-process agent with a shared-memory transport: reads, same-tick
  batches, guarded writes, pins, trial writes, `watch` captures (blocking,
  detached, following a root), `search` and `region`.
- Unreal pack: object lookup, reflected fields, containers, calls with kept
  results, `census`, `each`, `name-find`, bytecode `refs`, labels for a
  static tool.
- Fast lane: `disasm`, `xrefs`, `touches`, `sig --check` and `sig --bytes`
  from live bytes, with function bounds from the exception directory.
- Native objects: `vtable`, pointer chains, strings, `region`.
- Console with JSON Lines output, `batch`, `get`, a cache file between runs,
  and quoted `search text`; Python session client; IDA label importer.
- Own-thread example host and generic console: memory, the fast lane and
  native objects in any x64 process.
- The `reagentlive` agent skill.
