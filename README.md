# ReAgentLive

**Let your AI agent look inside a running game.**

**TL;DR:** ReAgentLive is a small DLL you load into a running Windows game;
your AI agent (or you, at a console) asks it questions and gets answers from
the live process in milliseconds. What it can do depends on your tier: raw
memory and code in any process, objects by name in any UE5 (Unreal Engine 5)
game, engine calls on the game's main thread. A write with `undo` restores
itself, a `hold` stops by itself, and every write asks you first.

Your agent can read code. It can't see what the game is doing right now.
ReAgentLive closes that gap: you load it into the game with any DLL loader,
and it waits for questions from outside: where a value lives, what the code
at an address does, what happens if a byte changes. Your agent asks through
its tools; you can ask from a command line.

## Quick start

**Needs:** Windows x64, Visual Studio's C++ tools, Git and Python.

Paste this into Claude Code, Codex, Cursor or any coding agent:

```text
Read https://raw.githubusercontent.com/Krixx1337/ReAgentLive/main/docs/agents/setup.md and follow it.
```

The agent puts ReAgentLive into your project (a mod that hosts it) or a
new one started from an example, builds it, registers its MCP tools,
adds the skill and checks the result. Everything it writes is committed
with your project, so whoever clones it gets the same setup. It may offer a command-line DLL
loader; say no if you load DLLs your own way.

**By hand**, it is a C++ library. Add it to your project as a submodule
and link a target ([hosting](docs/hosting.md#using-it-in-your-project)):

```powershell
git submodule add https://github.com/Krixx1337/ReAgentLive.git reagentlive
```

```cmake
add_subdirectory(reagentlive)
target_link_libraries(my-host PRIVATE reagentlive::ue-base)     # the DLL inside the target
target_link_libraries(my-console PRIVATE reagentlive::console)  # the console your agent drives
```

No host yet? Copy an example's three files into your project and build on
them ([starting from an example](docs/hosting.md#starting-from-an-example)):

| Your target | Start from | Tier |
|---|---|---|
| Any Windows x64 process | [`examples/own_thread`](examples/own_thread/README.md) | T0 |
| Any UE5 game, no dump, no mod | [`examples/unreal`](examples/unreal/README.md) | T1-T2; T3 once `game-thread` is on |
| A UE5 game you mod, with its SDK | your mod as the host ([hosting](docs/hosting.md)) | T4 |

Then wire your agent to it: MCP server, skill and instructions, for any
host ([an agent in your project](docs/hosting.md#an-agent-in-your-project)):

```powershell
python -I reagentlive\tools\agent_link.py --console build\reagentlive-ue.exe --dll build\reagentlive-unreal.dll
```

Every project carries its own pinned ReAgentLive, so updating one never
touches another.

**To update later** (move the submodule, rebuild, re-inject, check), ask
your agent to "update ReAgentLive": the skill routes it. By hand:
`reagentlive\tools\update.ps1` shows what is coming and asks to apply it.
Without the skill, paste:

```text
Read https://raw.githubusercontent.com/Krixx1337/ReAgentLive/main/docs/agents/update.md and follow it.
```

## What you can do, by tier

| Tier | Adds | Works on |
|---|---|---|
| **T4** SDK host | Layout from your mod's SDK dump; your mod's own commands | Your mod hosts it |
| **T3** Game thread | Call the game's own functions | One command moves it onto the game's main thread |
| **T2** Reflection | Read fields and values by name, from the engine's own type info | Any UE5 game, no dump |
| **T1** Unreal located | Find objects by name; count instances of a class | Any UE5 game, no dump |
| **T0** Core | Memory, code, signatures, writes that undo themselves | Any x64 process |

Each rung adds to the one below. What each needs and what stops it:
[tiers](docs/tiers.md); terms: [glossary](docs/README.md#glossary).

## What it looks like

You're in a match. You ask your agent one question, and it works it out
in the running game:

> **You:** Find my health. Is it the same for everyone? Then make me tanky.

| | Tool | What the agent learns |
|---|---|---|
| 1 | `ue_find` | 12 soldiers in the match; you are `BP_Soldier_C_0` |
| 2 | `ue_get` | your health: **87.5 / 100** |
| 3 | `ue_each` | every soldier's health, read in the same tick |
| 4 | `touches` | the one damage function that writes it |
| 5 | `write` with `undo` | your health set to 1000, restored after 30 s *(asks you first)* |

> **Agent:** Health is a float at `HealthComp+0x1A8`, written by one damage
> function at `Game.exe+0x9A3F10`. Every soldier uses it. I set yours to
> 1000, and after 30 s the original came back on its own.

A few seconds, no dump, no rebuild, nothing left changed. The tools are
real MCP tools; the game and its names are made up. This run needs T3.

It checks offsets and signatures against live bytes, lists every reference to
a string in milliseconds and reverts its own writes. Use cases, other
interfaces and how it pairs with IDA, Ghidra or Cheat Engine:
[positioning](docs/positioning.md), [clients](docs/client.md).

## Safe by default

- **Writes check first:** a write is refused if the bytes changed.
- **Changes expire:** a write with `undo` restores the original bytes; a
  `hold` stops rewriting when it runs out.
- **Your approval:** tools that write are marked, so your agent asks
  first.
- **Stale objects:** a reference to an object that died is refused, not
  written into.

More: [safety model](docs/positioning.md#safety-model).

## What it is not

- **Not an injector.** Something has to load it: your mod, or any DLL
  loader. The setup offers [Injector](https://github.com/nefarius/Injector),
  a command-line one your agent can run itself.
- **Not a way around protection.** A game that blocks other tools blocks
  this too.
- **Not a debugger or decompiler.** It checks and reports; analysis stays
  with your static tool and your agent.

## Learn more

- **Docs:** [docs/README.md](docs/README.md): tiers first, then pages by
  task, every command and a glossary.
- **Why it is built this way:** [positioning](docs/positioning.md),
  [design](docs/design/README.md).
- **Status:** pre-1.0, the API can change ([changelog](CHANGELOG.md)).
  Windows x64, C++23.

## License

MIT ([LICENSE](LICENSE)). Dependencies, all MIT: nlohmann/json and Zydis
(with Zycore) for the console and doctest for the tests, each fetched at
configure at a pinned version through CPM.cmake, which is kept in `cmake/`
(its licence is in its header).
