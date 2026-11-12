# ReAgentLive

**Let your AI agent look inside a running game.**

Your agent can read code. It can't see what the game is doing right now.
ReAgentLive runs inside the process and gives the agent live answers:
where a value lives, what the code at an address does, what happens if it
changes a byte. These answers take milliseconds, and every change undoes
itself.

## Set it up with your agent

Paste this into Claude Code, Codex, Cursor or any coding agent:

```text
Read https://raw.githubusercontent.com/Krixx1337/ReAgentLive/main/docs/agent-setup.md and follow it.
```

The agent builds ReAgentLive, registers its MCP tools, installs the
skill that teaches it the method, and checks the result. If you want, it
also fetches a command-line injector, so it can load ReAgentLive into the
process you name. You need
Windows x64, Visual Studio's C++ tools and Python.
Doing it by hand: [agent setup](docs/agent-setup.md).

## What it changes

| You ask your agent | Without it | With it |
|---|---|---|
| "Where's the player's health?" | guesses from a dump, or you scan by hand in a memory scanner | finds the object and reads the field in the running game |
| "What uses this string?" | waits hours for the disassembler's analysis to finish | every reference and the code around it, in milliseconds |
| "Does this offset still work after the patch?" | rebuild, inject, hope | checks the offset and signature against live bytes |
| "What if this flag were 1?" | a raw write, a crash, a restart | a trial write that restores itself |

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
names are made up. Addresses come back as `module` + `rva`, ready for IDA
or Ghidra.

## Where it works

- **Any x64 Windows process:** memory, code questions and native objects.
  Load one DLL and go.
- **Unreal Engine games:** work by name. Find objects, follow fields,
  call functions and list every instance of a class. This needs a
  [Dumper-7](https://github.com/Encryqed/Dumper-7) SDK and your own mod
  as the host ([hosting](docs/hosting.md)).

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

- **Docs:** [docs/INDEX.md](docs/INDEX.md), with pages by task, every
  command and a glossary.
- **Why it is built this way:** [positioning](docs/positioning.md),
  [design](docs/design.md).
- **Status:** version 0.2.0 ([changelog](CHANGELOG.md)). It is usable,
  and the API can change before 1.0. Windows x64, C++23.

## License

MIT ([LICENSE](LICENSE)). Dependencies, all MIT: nlohmann/json and Zydis
(with Zycore) for the console, fetched at configure; doctest for the tests,
in `third_party/`.
