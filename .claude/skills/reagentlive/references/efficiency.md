# Efficiency: fewer calls, smaller output

Budgets, digests and `stats` keys:
[console](https://github.com/Krixx1337/ReAgentLive/blob/main/docs/commands/console.md#usage-log-and-stats).

## Fewer calls

- Fewer tool calls beat faster ones: put everything the next decision
  needs in one `batch` or one `get` with several paths.
- `each` scans once: ask for every column in one call.
- A loop, sweep or comparison over many results: `each` when it is one
  class (one tick, one call); else one Python script that prints a
  summary, not N tool calls.
- A bare class name costs one table scan the first time per agent; a
  literal ref never scans. Keep refs from earlier results.

## Smaller output

- Filter `dump` (`--match`, `--nonzero`, `--changed`, `--own`) instead of
  reading all of it.
- Never `each`, `find` or `census` a whole class unfiltered: `--cls`,
  `--where`, `--outer` for one asset's subobjects, `--limit`.
- Read only the keys you need; `help <verb>` lists them.

## Digests

- **A `digest` means run its `next`.** If it names a `file` instead, read
  the file: that call may have changed state, so never repeat it.
- Raise `budget` only for what `next` cannot narrow.

## Measure the session

- `stats` (also with no target) ranks this session's verbs by output and
  marks `unfiltered`, `repeated` and `refused` calls. Check it before a long
  step and act on the marks.
- End the final report with its totals (context cost and what digests
  saved) and the two or three costliest habits.
- `stats --all` shows patterns across sessions and verbs nobody calls;
  `stats --sessions` compares this session with earlier ones.
