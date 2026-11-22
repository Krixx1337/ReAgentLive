# Unreal example

**TL;DR:** ReAgentLive in any UE5 game with no dump and no mod: a DLL that
ticks the agent with the whole Unreal pack on its own thread, and the
console that talks to it. Objects and fields by name work at once; engine
calls after `game-thread on`.

**Which tier this gives you:** T1-T2 on its own thread, T3 after
`game-thread on` ([tiers](../../docs/tiers.md)). Engine types are read with
the stock UE5 layout, each group checked on known objects first
([without an SDK](../../docs/commands/unreal.md#without-an-sdk)).

| File | What |
|---|---|
| `host.cpp` | `reagentlive-unreal.dll`: on load, starts one thread that ticks the agent every 2 ms, with every core and Unreal op and `game-thread` |
| `console.cpp` | `reagentlive-ue.exe`: the console with the Unreal pack; with no pid it finds the one process that has this agent |
| `CMakeLists.txt` | builds both on their own, consuming the library as any host project would |

## Build

From a Visual Studio x64 developer shell, in the ReAgentLive folder:

```powershell
cmake -S examples/unreal -B build/unreal -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/unreal
```

## Use it

Load `reagentlive-unreal.dll` into the game with any DLL loader, then:

```powershell
build/unreal/reagentlive-ue.exe -c "status; fields Vector; enum ENetRole"
```

- **The first Unreal command stalls the game once**, for up to seconds
  ([before the first command](../../docs/commands/unreal.md#before-the-first-command)).
- **Reads** (objects, fields, `dump`, `get`, `entries`, `enum`, ...)
  answer from the DLL's own thread under fault guards; values read across
  ticks need not line up. **Engine calls** (`call`, `each`, `script`, ...)
  need the game thread. The exact lists:
  [off the engine's thread](../../docs/commands/unreal.md#off-the-engines-thread).
- **Getting onto the game thread:** `game-thread on` (how an agent
  confirms the game viewport's Draw first:
  [the skill's game-thread page](../../.claude/skills/reagentlive/references/unreal/game-thread.md)):
  the host ticks the agent from that Draw, changing one pointer in the
  viewport object and nothing in the game's code. `game-thread` shows the
  state, `game-thread off` undoes it; how it works:
  [`game-thread`](../../docs/commands/unreal.md).
- A game whose engine layout is not stock refuses the affected commands
  with the check that failed; memory commands still work.
- Never unloaded.
- **MCP tools for an AI agent:** [agent setup](../../docs/agent-setup.md)
  (it builds this example and registers the server).

## Tests

`ctest -R "Unreal example"`: the real DLL loaded into the test process
(no engine there), checking that it answers, refuses Unreal commands
cleanly and reports `game-thread` as waiting for a viewport.
