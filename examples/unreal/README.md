# Unreal example

**TL;DR:** ReAgentLive in any UE5 game with no dump and no mod: a DLL that
ticks the agent with the whole Unreal pack on its own thread (`host.cpp`),
and the console `reagentlive-ue.exe` (`console.cpp`) that talks to it.
Objects and fields by name work at once; engine calls once `game-thread`
is on.

Tier: T1-T2 on its own thread, T3 once `game-thread` is on
([tiers](../../docs/tiers.md)). Engine types are read with the stock UE5
layout, each group checked on known objects first
([without an SDK](../../docs/commands/unreal.md#without-an-sdk)).

## Start a project from it

This folder is a kit: copy it into a project of your own and build on
`host.cpp` and `console.cpp`
([starting from a kit](../../docs/hosting.md#starting-from-a-kit);
[agent setup](../../docs/agents/setup.md) does it for you). The project's
DLL and console are then named after it; below they keep this example's
names.

To build it here, in ReAgentLive's own tree, from a Visual Studio x64
developer shell in the ReAgentLive folder:

```powershell
cmake -S examples/unreal -B build/unreal -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/unreal
```

## Use it

Load `reagentlive-unreal.dll` into the game with any DLL loader, then:

```powershell
build/unreal/reagentlive-ue.exe -c '[{"verb": "status"}, {"verb": "get", "args": {"object": "Vector", "mode": "types"}}, {"verb": "enum", "args": {"enum": "ENetRole"}}]'
```

- **The first Unreal command stalls the game once**, for up to seconds
  ([before the first command](../../docs/commands/unreal.md#before-the-first-command)).
- **Reads** answer from the DLL's own thread under fault guards; **engine
  calls** need the game thread
  ([which commands](../../docs/commands/unreal.md#off-the-engines-thread)).
- **Getting onto the game thread:**
  `{"verb": "game-thread", "args": {"action": "on"}}` ticks the agent from
  the game viewport's Draw, changing one pointer in the viewport object and
  nothing in the game's code; `off` undoes it
  ([`game-thread`](../../docs/commands/unreal.md); how an agent confirms
  the viewport first:
  [the skill's game-thread page](../../.claude/skills/reagentlive/references/unreal/game-thread.md)).
- A game whose engine layout is not stock refuses the affected commands
  with the check that failed; memory commands still work.
- **MCP tools for an AI agent:** [agent setup](../../docs/agents/setup.md)
  (it makes a project from this kit and registers the server).
