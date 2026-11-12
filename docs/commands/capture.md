# Capture and replay

Which command for which question about change:

| Question | Command |
|---|---|
| Which fields of this object changed across an action? | `snap`, then `dump --changed` |
| How do these named fields move over time? | `record` |
| What do these raw bytes do every tick? | [`watch`](memory.md#watch) |
| How does one field differ across all instances, or change in any of them? | [`each`](unreal.md#each), `each --seconds` |
| Which classes gained or lost instances? | [`census-diff`](unreal.md#census-changes) |

Use `snap $N` on a live instance, perform an action, then run `dump $N
--changed`. It compares reflected fields and prints the changed ones with
`before` and `value`; `--match` narrows them. It checks that the ref and
field layout match the baseline. Reads can span ticks, and fields outside the
64 KB dump window are skipped.

Use `fields $N` to choose exact field names for a recording. Qualify duplicate
names with `Owner:Field`. Select up to eight fields of up to 64 bytes each
within a single 4 KB span. Use a new file path (quote one with spaces).
The recorder samples a baseline, gives a three-second countdown, then writes
a start marker and polls for 1–10 seconds. Return focus to the game during the
countdown and perform the action after the marker.

The tab-separated capture keeps every sample, elapsed client milliseconds and
agent heartbeat values bracketing the read. Those values bound the sample
tick. Polling can miss ticks and short-lived changes. A refused read or timeout
ends the capture and leaves the file marked incomplete.

Run the console with `--replay <file>` to show the baseline, marker and
subsequent changes without attaching to a game. Replay returns a failure
status for incomplete captures after printing the available records.
