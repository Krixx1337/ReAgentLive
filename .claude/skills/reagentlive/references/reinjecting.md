# Building and reinjecting

## Building

- **Use the host's build script** when it has one (see the host notes).
  A good one loads the compiler environment, refuses to build without it, and
  prints one PASS/FAIL line per target, with the exit code.
- Without a script: build from a Visual Studio developer shell
  (`Launch-VsDevShell.ps1 -Arch amd64`). A build without that environment can
  exit 0 when nothing recompiles, then fail on the next header. A configure
  without it writes placeholder tools into the cache (`ml`,
  `CMAKE_LINKER-NOTFOUND`); fix that with `cmake --fresh`.
- Filtering build output hides failures; check the exit code of every build,
  or tests run on stale binaries.
- `near` and `far` are Windows macros: bad variable names.
- CRLF files need byte-level edits.
- PowerShell `@(@(a,b))` flattens a single pair; write `@(,@(a,b))`.
- PowerShell names are case-insensitive: `$early` overwrites an
  `[int]$Early` parameter and fails on the next double; give every variable
  a distinct name.

## Reinjecting

- Agent and host changes need the new DLL; console-only changes don't.
- A protocol layout change makes a mismatched console refuse to attach
  ("agent layout differs").
- Re-injecting into the same process stacks host copies; prefer a fresh
  game start for behaviour checks.
- `status` shows the answering agent's build time and `replaced_agents`. The
  build time stamps the agent core only: a host-only change keeps the old
  time, so check `replaced_agents` went up.
- Older agents keep stale copies of earlier requests in memory, which can
  show up as `search` hits.
