# Capture

*Home of:* which command answers which question about change, and `get --snap`.

**TL;DR:** To see what changes when something happens in the game, pick
the command by the question: one object's fields across an action
(`get --snap`, then `get --changed`), named fields or raw bytes over time
(`watch-start` with `<ref>.<Field>` or address ranges), or every instance
of a class (`each`, `census --diff`). These read and change nothing,
except `each` with a function, which calls into the game.

Which command for which question about change:

| Question | Command |
|---|---|
| Which fields of this object changed across an action? | `get --snap`, then `get --changed` |
| How do these named fields move over time? | [`watch-start`](memory.md#watching-a-reflected-field) `<ref>.<Field>...`, then `watch-read` or `watch-stop` (`--file` keeps every sample) |
| What do these raw bytes do every tick? | [`watch-start`](memory.md#watch) `<addr>:<len>...` |
| How does one field differ across all instances, or change in any of them? | [`each`](unreal.md#each), `each --seconds` |
| Which classes gained or lost instances? | [`census --diff`](unreal.md#census-changes) |

Use `get $N --snap` on a live instance, perform an action, then run `get $N
--changed`. It compares reflected fields and prints the changed ones with
`before` and `value`; `--match` narrows them. It checks that the ref and
field layout match the baseline. Reads can span ticks, and fields outside the
64 KB read window are skipped. The last snapshot is kept in the cache file, so
`get --changed` works in a later run against the same agent instance
(one-shot runs, and the MCP adapter's console per call).

To time an action, start a capture before it: `watch-start 10s $1.Ammo
$1.State --file burst.jsonl`, tell the player to act, then `watch-stop`.
Field ranges take their place and type from reflection at the start
(`get $N --types` shows the names; `Owner:Field` where one is ambiguous)
and are guarded on their object. The agent samples every tick, so no
change between ticks is missed, unlike polling from the console.
