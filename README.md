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

**Needs:** Windows x64, Visual Studio's C++ tools and Python.

Paste this into Claude Code, Codex, Cursor or any coding agent:

```text
Read https://raw.githubusercontent.com/Krixx1337/ReAgentLive/main/docs/agents/setup.md and follow it.
```

The agent builds the example for your target, registers its MCP tools,
installs the skill (instructions it reads before investigating) and checks
the result.

**Optional:** it asks whether to fetch a command-line DLL loader, so it can
load ReAgentLive into the process you name. Say no if you load DLLs your
own way (your mod, your own loader).

By hand, start from the example for your target:

| Your target | Start from | Tier |
|---|---|---|
| Any Windows x64 process | [`examples/own_thread`](examples/own_thread/README.md) | T0 |
| Any UE5 game, no dump, no mod | [`examples/unreal`](examples/unreal/README.md) | T1-T2; T3 once `game-thread` is on |
| A UE5 game you mod, with its SDK | your mod as the host ([hosting](docs/hosting.md)) | T4 |

**To update later** (pull, rebuild, re-inject, check), ask your agent to
"update ReAgentLive": the skill routes it. Without the skill, paste:

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

Each rung adds to the one below. An *SDK dump* is a set of C++ headers a
dumper tool generates from a game; a *signature* is a byte pattern that
finds code.
What each rung needs and what stops it: [tiers](docs/tiers.md).

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

A few seconds, with no dump to guess from, no rebuild, and nothing left
changed (a value the game rewrites in those 30 s ends `conflict` and is
left as the game wrote it). This is an example: the tools are real, and the game and its
names are made up. The tools are the agent's MCP tools (Model Context
Protocol, how AI agents call tools); this run needs T3. Addresses come back
as `module` + `rva` (module and offset), ready for IDA or Ghidra.

## Why it's faster

| You ask your agent | Without it | With it |
|---|---|---|
| "Where's the player's health?" | guesses from a dump (headers generated from the game), or you scan by hand in a memory scanner | finds the object and reads the field in the running game |
| "What uses this string?" | waits hours for the disassembler's analysis to finish | every reference and the code around it, in milliseconds |
| "Does this offset still work after the patch?" | rebuild, inject, hope | checks the offset and signature (a byte pattern that finds code) against live bytes |
| "What if this flag were 1?" | a raw write, a crash, a restart | a `write` with `undo` that restores itself |

## Use cases

- **Find a value you can see on screen.** Ammo, health, a cooldown: ask
  where it lives and what code changes it, instead of scanning by hand.
- **Check your mod after a game update.** Test every offset and signature
  against the running game in one go, before you rebuild.
- **Learn how a feature works.** Start from a string or an address and see
  its references and code from live bytes, with no full disassembly.
- **Try a change safely.** Flip a flag for 30 seconds with `undo`; the
  original comes back by itself.
- **Explore an Unreal Engine game with no dump.** List every object of a
  class, read their fields by name and, on the game thread, call the
  game's own getters.

## Ways to drive it

The same commands through four interfaces ([clients](docs/client.md)):

- **MCP tools** (Model Context Protocol): your AI agent's default, one tool per command.
- **Console:** a command line for you, or quick checks ([console](docs/commands/console.md)).
- **Python:** generated functions for loops and comparisons over results.
- **C++ client:** for your own tools and outside programs.

## Safe by default

- **Writes check first:** a write is refused if the bytes changed.
- **Changes expire:** a write with `undo` restores the original bytes; a
  `hold` stops rewriting when it runs out.
- **Your approval:** tools that write are marked, so your agent asks
  first.
- **Stale objects:** a reference to an object that died is refused, not
  written into.

More: [safety model](docs/positioning.md#safety-model).

## Works with your tools

It doesn't replace IDA, Ghidra, Cheat Engine or Frida. It adds the one
thing they can't give you: what is true in the running process right now.
How it pairs with each: [positioning](docs/positioning.md#role-among-other-tools).

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
- **Status:** version 0.6.0 ([changelog](CHANGELOG.md)). It is usable,
  and the API can change before 1.0. Windows x64, C++23.

## License

MIT ([LICENSE](LICENSE)). Dependencies, all MIT: nlohmann/json and Zydis
(with Zycore) for the console and doctest for the tests, each fetched at
configure at a pinned version.
