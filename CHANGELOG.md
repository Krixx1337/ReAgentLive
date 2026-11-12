# Changelog

ReAgentLive follows semantic versioning. Before 1.0 the API and the wire
layout can change in any minor release; agent and client are always built
from the same revision, and the handshake's layout check refuses a
mismatched pair. The version lives in `src/reagentlive/version.h`.

## Unreleased

Cheaper agent sessions: fewer wrong-looking answers, less output, fewer
turns ([design](docs/design.md#output-weight-and-dead-ends)). Every caller
breaks once: three argument names, and the `dump`, `get`, `each` and
`watch` results changed.

- **No answer that points the wrong way:**
  - A pointer chain of one `->off` (it follows no pointer) is refused,
    from a typed line and a JSON call alike, with the form that follows
    one: `<addr>->0->0x30`.
  - No argument is a Python keyword: `--class` is `--cls`, `--from` is
    `--start`, `read --as` is `read --type`, and `each`'s class is `cls`;
    every form spells them the same. A table test refuses a keyword.
  - An unknown JSON key is checked first and named with the keys the verb
    takes; an unknown flag lists the verb's flags; `read-batch` and `each`
    state the limit they hit.
- **Compact rows:** a `dump` row is `name` (the recovered readable one,
  `raw` beside it), `offset` and `value`, with `meta` (the layout) on
  `--meta` and `truncated` only when true; `dump --own` keeps the fields the
  object's own class declares. A `get` row is `value` and `address`, with
  `object` only when the path crossed a pointer; `path` and `ok` are gone
  (rows keep the order asked; a failed path keeps its place as `error`).
- **`each` is a table:** up to 16 field paths (structs and object pointers,
  as `get` takes) or one function; `columns`, then `rows` of `{ref,
  values}`, `errors` for failed cells and `same_tick`. Spans inside the
  instance share passes of 64 bytes over one scan.
- **A console without the Unreal pack:** `REAGENTLIVE_CONSOLE_UE=OFF`
  builds the console from core files and an empty pack seam
  (`console/no_pack.cpp`). Its table, `help`, generated functions and MCP
  tools hold no pack verbs and no core verb marked `needsPack` (`trial`).
  The default console's table and hash are unchanged.
- **Typed watches:** a range may be `<addr>:f64` or `<addr>:i32[2]`; its
  samples are numbers and the result adds a `summary` per value (first,
  last, least, greatest, `per_second`). The type is kept in the agent's
  copy of the spec (`WatchRange::reserved` is now `note`, same layout), so
  `watch-read` in a later run decodes it.

## 0.2.0

AI agents get MCP tools: one tool per console verb, generated from the
console's own verb table, with checked arguments and typed results. Every
caller of the console breaks once: verbs, result keys and the Python client
changed (below).

- **Typed calls and MCP tools, generated from the verb table**
  ([design](docs/design.md#tool-interface), [clients](docs/client.md)):
  - JSON calls: a line starting with `{` is `{verb, args}` with named
    arguments, checked against the row like a typed line; literal refs
    only (`$N` is refused). `batch` takes a list of calls.
  - Every argument in the table has a key, a type and a default;
    `int` and enum values are checked before the parser runs. `help
    --json` adds `types` (named types such as `Chain`, `Object`,
    `ue.Name`), `origins` (each with its title) and `blocks` on `watch`
    and `record`. `Object` is a core type a pack fills (`peek`'s rows).
  - `reagentlive_gen.py` writes typed Python functions for one console,
    a host's verbs included, refusing to run against another table.
    The session's hand-written per-verb methods and its paging helpers
    (`find_all`, `search_addresses`, ...) are gone; `s.call(verb, args)`
    sends any verb.
  - `frontends/mcp/reagentlive_mcp.py` serves those functions as MCP tools
    (needs the MCP Python SDK), read-only or destructive per `writes`;
    blocking verbs are left out, and a tool error carries the console's
    whole error object (a refused `write --expect` keeps `found`).
    Checked live in an AI client: read-only lookups, guarded writes, pins,
    trials and engine calls.
  - Hosts: `ConsoleConfig::hostCommand` takes the verb's row and its
    checked `Args` in place of the line; rows give keys and types.

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
