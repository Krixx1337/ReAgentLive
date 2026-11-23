# Capture

*Home of:* which command answers which question about change, and `get` with `mode` `snap` (its baseline across runs).

**TL;DR:** To see what changes when something happens in the game, pick
the command by the question: one object's fields across an action
(`get` with `mode` `snap`, then `changed`), named fields or raw bytes over
time (`watch-start` with field or address ranges), or every instance of a
class (`each`, `census` with `diff`). These read and change nothing;
`call-each` calls a pure getter on every instance.

Which command for which question about change:

| Question | Command |
|---|---|
| Which fields of this object changed across an action? | `get` with `mode` `snap`, then `get` with `mode` `changed` |
| How do these named fields move over time? | [`watch-start`](memory.md#watching-a-reflected-field) with `{"object": <ref>, "field": [...]}` ranges, then `watch-read` or `watch-stop` (`file` keeps every sample) |
| What do these raw bytes do every tick? | [`watch-start`](memory.md#watch) with `{"addr": <addr>, "len": N}` ranges |
| How does one field differ across all instances, or change in any of them? | [`each`](unreal-objects.md#each), `each` with `seconds` |
| Which classes gained or lost instances? | [`census` with `diff`](unreal-objects.md#census-changes) |
| What does a pure getter return on every instance? | [`call-each`](unreal-calls.md#call-each), with `seconds` to watch it |
| Which raw values in a block rose, fell or stayed across actions? | [`mem-snap`, then `mem-diff`](memory.md#mem-snap-and-mem-diff) with `keep` |
| What did the game pass to this reflected function? | the [call recorder](unreal-calls.md#call-recorder) (`calls-start`; a write the user approves) |

Take the baseline on a live instance, perform an action, then ask for the
changes:

```json
{"verb": "get", "args": {"object": "0x1f2e3d4c50#123:4", "mode": "snap"}}
{"verb": "get", "args": {"object": "0x1f2e3d4c50#123:4", "mode": "changed"}}
```

It compares reflected fields and prints the changed ones with `before` and
`value`; `view.match` narrows them. It checks that the ref and field layout
match the baseline. Reads can span ticks, and fields outside the 64 KB read
window are skipped. The last snapshot is kept in the cache file, so `mode`
`changed` works in a later run against the same agent instance (one-shot
runs, and the MCP adapter's console per call; a snapshot over 1 MB stays
in its run). A snapshot `file` (`"file": "before.snap"` on both calls)
keeps a baseline past the agent instance and beside a later snapshot.

To time an action, start a capture before it, tell the player to act, then
call `watch-stop`:

```json
{"verb": "watch-start", "args": {"duration": 10, "file": "burst.jsonl", "ranges": [{"object": "0x1f2e3d4c50#123:4", "field": ["Ammo"]}, {"object": "0x1f2e3d4c50#123:4", "field": ["State"]}]}}
```

Field ranges take their place and type from reflection at the start
(`get` with `mode` `types` shows the names; `Owner:Field` where one is
ambiguous) and are guarded on their object. The agent samples every tick,
so no change between ticks is missed, unlike polling from the console.
