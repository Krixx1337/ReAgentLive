# Efficiency: fewer calls, smaller output

The result cap and digests: `docs/commands/console.md#fewer-round-trips`;
`usage`: `docs/commands/console.md#usage-log-and-usage`.

## Fewer calls

- Fewer tool calls beat faster ones: put everything the next decision
  needs in one `batch` or one `get` with several paths.
- `each` scans once: ask for every column in one call.
- A loop, sweep or comparison over many results: `each` (or
  `call-each` for a getter) when it is one class (one tick, one
  call); else one Python script that prints a summary, not N tool calls.
- The same calls over time, or a measurement to repeat later: one `run`
  plan with `repeat` (the capture-and-timing reference), not a loop of
  tool calls.
- Enable a pack once, when the task first needs it; leave the rest off.
- A bare class name costs one table scan the first time per agent; a
  literal ref never scans. Keep refs from earlier results.

## Smaller output

- Filter `get` instead of reading every field (`view.match`,
  `view.nonzero`), or name the `paths`
  (`{"verb": "get", "args": {"object": "0x1f2e3d4c50#123:4", "view": {"nonzero": true}}}`).
- Never `each`, `find` or `census` a whole class unfiltered: narrow by
  class, value, owner (one asset's subobjects) or `view.limit` (`help`
  with `verb`).
- Read only the keys you need; `help` with `verb` lists them.

## Digests

- **A `digest` means run its `next`.** If it names a `file` instead, read
  the file: that call may have changed state, so never repeat it.
- Read the digest's `status` before running anything: a `saved` file is
  already written (open it, do not repeat the call), and `next` there is
  where a cut listing resumes.
- Raise `max_chars` only for what `next` cannot narrow.
- A `context_warning` on a result: this session is past its context
  limit. Stop reading wide results: narrow every call, summarise in a
  script, or hand off to a fresh session.

## Measure the session

- `usage` (also with no target) ranks this session's verbs by output and
  marks wasteful calls (unfiltered, repeated, refused; `help` on `usage`;
  `same_args` is not waste: the state changed). Check
  it before a long step and act on the marks.
- End the final report with its totals (context cost and what digests
  saved) and the two or three costliest habits.
- `usage` also reads across sessions: patterns, verbs nobody calls, this
  session against earlier ones (`usage` with `report` `sessions` or
  `all`), or against a report kept with `save` (`baseline`: what got
  worse). A mid-session check needs one part: `only` (`["totals"]`,
  `["compare"]`) keeps the rest out of context.
