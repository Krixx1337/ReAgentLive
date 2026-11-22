# Building and reinjecting

## Building

- **Use the host's build script** when it has one (host notes). A good one
  loads the compiler environment, refuses to build without it, and prints
  one PASS/FAIL line per target, with the exit code.
- Without a script: build from a Visual Studio developer shell
  (`Launch-VsDevShell.ps1 -Arch amd64`). A build without that environment
  can exit 0 when nothing recompiles, then fail on the next header. A
  configure without it writes placeholder tools into the cache (`ml`,
  `CMAKE_LINKER-NOTFOUND`); fix that with `cmake --fresh`.
- Filtering build output hides failures; check the exit code of every
  build, or tests run on stale binaries.

## Reinjecting

- Agent and host changes need the new DLL; console-only changes don't.
- A protocol layout change makes a mismatched console refuse to attach
  ("agent layout differs").
- A console that finds the agent taken by another client is refused as
  `busy`: end the other client (a script, a second MCP server).
- Re-injecting into the same process stacks host copies; prefer a fresh
  game start for behaviour checks.
- `status` shows the answering agent's `build` time and `replaced_agents`.
  The build time stamps the agent core only: after a host-only change,
  check that `replaced_agents` went up.
- A reinjection or game restart empties the console's cache: the first
  class lookup scans again.
