# Changelog

ReAgentLive follows semantic versioning. Before 1.0 the API and the wire
layout can change in any minor release; agent and client are always built
from the same revision, and the handshake's layout check refuses a
mismatched pair. The version lives in `src/reagentlive/version.h`.

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
