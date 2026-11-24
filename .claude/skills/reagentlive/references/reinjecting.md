# Building and reinjecting

## Building

Use the host's build script when it has one (host notes). Otherwise build
from a Visual Studio developer shell: without it a build can exit 0 when
nothing recompiles and a configure caches placeholder tools (`cmake
--fresh` fixes it). Check every build's exit code; filtered output hides
failures.

## Reinjecting

- Agent and host changes need the new DLL; console-only changes don't.
- A protocol layout change makes a mismatched console refuse to attach
  ("agent layout differs"); after an update, `docs/agents/update.md#7-running-targets`.
  Restart the target, then inject: never load a DLL with a changed
  protocol into a process that runs the old agent.
- Re-injecting into the same process stacks host copies; prefer a fresh
  game start for behaviour checks.
- `status` shows the answering agent's `build` time and `replaced_agents`.
  The build time stamps the agent core only: after a host-only change,
  check that `replaced_agents` went up.
- A reinjection or game restart empties the console's cache: the first
  class lookup scans again.
