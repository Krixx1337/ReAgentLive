# Changelog

ReAgentLive follows semantic versioning. Before 1.0 the API and the wire
layout can change in any minor release; agent and client are always built
from the same revision, and the handshake's layout check refuses a
mismatched pair. The version lives in `src/reagentlive/version.h`.

## Unreleased

- The console keeps the core and the Unreal pack apart: each `frontends/cli`
  layer has a `ue/` folder for the pack's part, the core reaches the pack
  only through `console/pack.h`, and the layout test enforces both.
- `status` lists the packs the host offers (`packs`); a pack's verbs on a
  host without it are refused with a message that says so; `help` groups
  the verbs into core and Unreal pack.
- `Ref` moved to the core protocol. The cache file keeps each pack's state
  in its own block (format 8; older files are dropped).

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
