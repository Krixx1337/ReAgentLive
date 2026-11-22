# Unreal example

ReAgentLive in any UE5 game with no dump and no mod: a DLL that ticks the
agent with the whole Unreal pack on its own thread, and the console that
talks to it. Engine types are read with the stock UE5 layout, each group
checked on known objects first
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

- **The first Unreal command stalls the game once**, for up to seconds:
  with no dump the pack scans the game's image for the object table and
  name pool.
- **Reads** (`find`, `fields`, `dump`, `get`, `entries`, `enum`, `func`,
  `funcs`, `census`) answer from the DLL's own thread under fault guards;
  values read across ticks need not line up.
- **Engine calls** (`call`, `call-pure`, `params`, `each` with functions,
  `script`, `refs`, `exec entries`, the recorder) need the game thread.
  Confirm the game viewport's Draw first (the skill's
  `references/unreal/new-game.md`, "Calls in a new game"), then
  `game-thread on`: the host gives the viewport's Draw interface a private
  copy of its vtable with Draw swapped (one pointer in the viewport object;
  the game's code and tables are untouched), and ticks the agent from Draw
  on the game thread. `game-thread` shows the state; `game-thread off` puts
  the viewport's table back.
- A game whose engine layout is not stock refuses the affected commands
  with the check that failed; memory commands still work.
- Never unloaded.

## Tests

`ctest -R unreal-example`: the real DLL loaded into the test process
(no engine there), checking that it answers, refuses Unreal commands
cleanly and reports `game-thread` as waiting for a viewport.
