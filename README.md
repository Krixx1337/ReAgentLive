# ReAgentLive

**Let your AI agent look inside a running game.**

**TL;DR:** ReAgentLive is a small DLL you load into a running Windows game;
your AI agent (or you, at a console) asks it questions and gets answers from
the live process in milliseconds. What it can do depends on your tier: raw
memory and code in any process, objects by name in any UE5 (Unreal Engine 5)
game, engine calls on the game's main thread. Pins and trial writes undo
themselves, and every write asks you first.

Your agent can read code. It can't see what the game is doing right now.
ReAgentLive closes that gap: you load it into the game with any DLL loader,
and it waits for questions from outside: where a value lives, what the code
at an address does, what happens if a byte changes. Your agent asks through
its tools; you can ask from a command line.

## What you can do, by tier

```text
T4 SDK host        + layout from your mod's SDK dump, your mod's own commands  (your mod hosts it)
T3 Game thread     + call the game's own functions          (after one command moves it onto the game's main thread)
T2 Reflection      + read fields and values by name, from the engine's own type info  (any UE5 game, no dump)
T1 Unreal located  + find objects by name, count instances of a class    (any UE5 game, no dump)
T0 Core              memory, code, signatures, trial writes               (any x64 process)
```

Each rung adds to the one below. An *SDK dump* is a set of C++ headers a
dumper tool generates from a game; a *signature* is a byte pattern that
finds code; a *trial write* changes a value and restores it by itself.
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
| 5 | `trial` | your health held at 1000 for 30 s, then restored *(asks you first)* |

> **Agent:** Health is a float at `HealthComp+0x1A8`, written by one damage
> function at `Game.exe+0x9A3F10`. Every soldier uses it. I held yours at
> 1000 for 30 s, and the original came back on its own.

A few seconds, with no dump to guess from, no rebuild, and nothing left
changed. This is an example: the tools are real, and the game and its
names are made up. The tools are the agent's MCP tools (Model Context
Protocol, how AI agents call tools); this run needs T3. Addresses come back
as `module` + `rva` (module and offset), ready for IDA or Ghidra.

## Why it's faster

| You ask your agent | Without it | With it |
|---|---|---|
| "Where's the player's health?" | guesses from a dump (headers generated from the game), or you scan by hand in a memory scanner | finds the object and reads the field in the running game |
| "What uses this string?" | waits hours for the disassembler's analysis to finish | every reference and the code around it, in milliseconds |
| "Does this offset still work after the patch?" | rebuild, inject, hope | checks the offset and signature (a byte pattern that finds code) against live bytes |
| "What if this flag were 1?" | a raw write, a crash, a restart | a trial write that restores itself |

## Use cases

- **Find a value you can see on screen.** Ammo, health, a cooldown: ask
  where it lives and what code changes it, instead of scanning by hand.
- **Check your mod after a game update.** Test every offset and signature
  against the running game in one go, before you rebuild.
- **Learn how a feature works.** Start from a string or an address and see
  its references and code from live bytes, with no full disassembly.
- **Try a change safely.** Flip a flag or hold a value for 30 seconds; the
  original comes back by itself.
- **Explore an Unreal Engine game with no dump.** List every object of a
  class, read their fields by name and, on the game thread, call the
  game's own getters.

## Pick your start

| Your target | Start from | Tier |
|---|---|---|
| Any Windows x64 process | [`examples/own_thread`](examples/own_thread/README.md) | T0 |
| Any UE5 game, no dump, no mod | [`examples/unreal`](examples/unreal/README.md) | T1-T2; T3 after `game-thread on` |
| A UE5 game you mod, with its SDK | your mod as the host ([hosting](docs/hosting.md)) | T4 |

## Ways to drive it

The same commands through four interfaces ([clients](docs/client.md)):

- **MCP tools** (Model Context Protocol): your AI agent's default, one tool per command.
- **Console:** a command line for you, or quick checks ([console](docs/commands/console.md)).
- **Python:** generated functions for loops and comparisons over results.
- **C++ client:** for your own tools and outside programs.

## Set it up with your agent

Paste this into Claude Code, Codex, Cursor or any coding agent:

```text
Read https://raw.githubusercontent.com/Krixx1337/ReAgentLive/main/docs/agents/setup.md and follow it.
```

The agent builds the example for your target, registers its MCP tools,
installs the skill (instructions it reads before investigating) and checks
the result. If you want, it also fetches a command-line DLL loader, so it
can load ReAgentLive into the process you name. You need Windows x64,
Visual Studio's C++ tools and Python. By hand: each example's README above.

To update later (pull, rebuild, re-inject, check), paste:

```text
Read https://raw.githubusercontent.com/Krixx1337/ReAgentLive/main/docs/agents/update.md and follow it.
```

## Safe by default

- **Writes check first:** a write is refused if the bytes changed.
- **Changes expire:** pins and trial writes restore the original bytes.
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

- **Docs:** [docs/INDEX.md](docs/INDEX.md): tiers first, then pages by
  task, every command and a glossary.
- **Why it is built this way:** [positioning](docs/positioning.md),
  [design](docs/design.md).
- **Status:** version 0.4.0 ([changelog](CHANGELOG.md)). It is usable,
  and the API can change before 1.0. Windows x64, C++23.

## License

MIT ([LICENSE](LICENSE)). Dependencies, all MIT: nlohmann/json and Zydis
(with Zycore) for the console, fetched at configure; doctest for the tests,
in `third_party/`.
