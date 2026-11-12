# ReAgentLive

**The live, in-process probe for AI-driven game reversing.**

Static tools tell you what the code does.
ReAgentLive tells you what is true in the running game, right now, and lets
you test it without breaking anything.

> **Live.** It runs inside the game and answers from live memory, on the
> game's own tick.
> **Safe.** References that know when an object died, writes that check
> first, changes that undo themselves.
> **Immediate.** No database, no hours of analysis: questions about code
> are answered from live bytes in milliseconds.

**First-class Unreal Engine support.** Build the Unreal pack against the
SDK [Dumper-7](https://github.com/Encryqed/Dumper-7) generates for your
game, and work by name: find objects, read and follow reflected fields,
call functions, list every instance of a class, all on the game thread.
Everything else (memory, code, native structs) works in any game.

---

## The problem

Point an AI agent at a running game with the usual tools and it hits the
same walls:

- **Reads from outside are torn.** Two values read a moment apart never
  came from the same frame.
- **Pointers go stale.** An enemy respawns and yesterday's address is now
  someone else's memory.
- **Raw writes crash things.** One wrong byte, no undo.
- **Static analysis is slow.** Hours of whole-program analysis before the
  first answer, and again after every patch.
- **Huge tool surfaces eat context.** A hundred tools cost an agent its
  budget before it does anything.

ReAgentLive is built around those walls.

## 30 seconds of it

Load one DLL into any x64 process, here a running Python interpreter, and
ask. No game, no SDK, no database. Output condensed from the JSON the
console prints:

```text
> reagentlive -c "search text PYTHONPATH --module python313.dll --paged --timing"
  rows: python313.dll+0x2c1ca8                               4.9 ms

> reagentlive -c "xrefs python313.dll+0x2c1ca8 --timing"
  lea  python313.dll+0x12ff02  in function +0x12fd64..+0x1301ce
  scanned all of python313.dll                               29.6 ms

> reagentlive -c "disasm python313.dll+0x12ff02 3"
  +0x12ff02  lea rdx, [0x7FF827981CA8]   string: "PYTHONPATH"
  +0x12ff09  mov rcx, rbx
  +0x12ff0c  call python313.dll+0x1301d0
```

A string, every place the code uses it, and the code around it: three
commands, well under a second, straight from the bytes the process is
running. Every address comes back as `module` + `rva`, ready to paste into
your disassembler.

## Why it is different

No single one of these is new. The combination is.

| ReAgentLive | The usual way |
|---|---|
| **Inside the process**, on a thread the host trusts. No debugger attached, no hooks of its own | A debugger, a hooking framework, or reading from outside |
| **Tick-accurate:** on the engine's thread, everything in one answer comes from the same moment | Torn reads from outside, or polling |
| **Safe by construction:** references that refuse a replaced object, writes only over the bytes you expected, pins and trials that expire on their own | Raw writes handed to an agent |
| **Shaped for an AI's costs:** one console, few commands that each do a whole lookup, filtered output, state kept between runs | Large tool surfaces that fill the context first |
| **Immediate:** one-address code questions from live bytes, no database | Hours of analysis before the first answer |
| **Reflection and raw memory in one tool:** Unreal objects and native structs side by side | One or the other |

## Low footprint

ReAgentLive is quiet. It answers when asked and otherwise stays out of the
game's way:

- **No hooks of its own.** The core never patches code or swaps a pointer;
  where the tick comes from is the host's business.
- **No threads of its own.** It runs on a thread the host already has. The
  own-thread example adds exactly one, its tick; background search adds a
  worker only when the host allows it.
- **No process handle.** The client never opens the game process: no
  `OpenProcess`, no `ReadProcessMemory`, no debugger. Requests travel
  through one named shared-memory section.
- **Next to nothing while idle:** one atomic increment and one compare per
  tick. Fixed buffers, no allocation at load.

Low footprint means it does not disturb the game. It does not mean hidden:
see [what it is not](#what-it-is-not).

## What you can do

| If you want to | ReAgentLive gives you |
|---|---|
| See what the game is doing right now | find objects, dump them, follow field paths, read containers, sample any value every tick |
| Test an idea without rebuilding | guarded writes, pins that hold a value, trial writes that undo themselves, engine calls |
| Understand code without waiting | the fast lane: instructions, references, functions using an offset, signature checks |
| Name what you found | label unreflected members once; export labels to your static tool |
| Let an AI do all of the above | one console with JSON in and out, MCP tools and typed Python functions generated from its own verb table, and a skill that teaches the method |

## Try it in five minutes

The own-thread example is a DLL that works in **any** x64 process: no mod,
no SDK, no game knowledge.

```powershell
# From a Visual Studio x64 developer shell, in the ReAgentLive folder:
cmake -S examples/own_thread -B build/own-thread -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/own-thread
# Load reagentlive-own-thread.dll into the target with any DLL loader, then:
reagentlive -c "status; modules"
```

You get memory, the fast lane and native objects (`vtable`, pointer chains,
`region`, strings): the core, which needs no SDK. Unreal pack commands are
refused here, and `status` says which packs a host offers (`packs`). With
no pid the console finds the process on its own.
More: [own-thread example](docs/hosting.md#own-thread-example).

## Recommended setup

One console, three ways in. They are generated from the same verb table, so
they take the same arguments and return the same keys.

| Who | Use | Why |
|---|---|---|
| An AI agent, investigating | **MCP tools** (default) | picks a tool and fills named, checked arguments: no quoting, no JSON parsing, literal refs passed from one result to the next; read-only tools are marked, writes and calls ask first |
| An AI agent, sweeping or looping (more than about 5 related calls, any loop) | **a script on the generated Python functions** | one call, only the summary comes back; typed and as strict as the tools |
| You, in a shell; anyone, for `help` | **the console's text lines** | short to type; `help` needs no target running |

Set it up once per console build (no target needed for either step):

```powershell
# Typed functions for your console, its own verbs included:
python src/reagentlive/frontends/python/reagentlive_gen.py --console <console.exe> -o <dir>/console_api.py
# Register the MCP server with your AI client, e.g. a project .mcp.json:
#   "command": "python",
#   "args": ["<reagentlive>/src/reagentlive/frontends/mcp/reagentlive_mcp.py",
#            "--console", "<console.exe>", "--module", "<dir>/console_api.py"]
```

Regenerate the module after a build that changes any verb; the module and the
server both refuse a stale one. The MCP server needs the MCP Python SDK
(`pip install mcp`); the rest is standard library. Details: [clients](docs/client.md).

## Best on the game thread

The own-thread DLL is the fast way in. ReAgentLive is at its best when the
host ticks it **on the engine's game thread**, from a mod or a hook you
already own:

| Host ticks on | You get |
|---|---|
| **The game thread** | everything: object and field commands, engine calls, and a tick that is one moment of the game (`batch` and `get` say with `same_tick` whether they fit in one) |
| Another thread (its own, or the frame present) | memory, the fast lane and native objects; engine commands are refused, and reads in one tick need not line up |

`status` says which one you have (`host_thread`). How to get onto the game
thread is the host's business; what each choice gives is in
[hosting](docs/hosting.md#host-thread).

## Go further

- **Unreal games:** the Unreal pack over your Dumper-7 SDK adds objects,
  reflected fields, function calls, `census` (what exists, what just
  spawned) and `each` (one field across every instance, in one tick).
  [Unreal pack](docs/commands/unreal.md)
- **Your own mod as the host:** tick the agent from the game thread and get
  all of the above. [Hosting](docs/hosting.md)
- **Logic outside the game:** keep a tool or a mod's logic in its own
  program and use the core as a lean in-process proxy.
  [Outside programs](docs/outside-programs.md)
- **AI agents:** MCP tools and typed Python functions
  ([recommended setup](#recommended-setup)), and the `reagentlive` skill
  that teaches the investigation method: rules, workflow, pitfalls.
  [Skill](.claude/skills/reagentlive/SKILL.md)

## Where it can grow

The core knows no engine. Everything engine-specific lives in a **pack**:
today one, for Unreal. Memory, the fast lane and native objects already
work in any engine; a pack adds objects, fields and calls on top.

Natural next packs are **Unity** (Mono and IL2CPP) and **Source 2**. They
are candidates, not promises: one gets built when a real project needs it
([backlog](docs/ideas.md#21--other-engine-packs-unity-source-2)).

## Plays well with others

ReAgentLive is a friendly neighbour, not a replacement. Keep your IDA or
Ghidra MCP for the static side and your Cheat Engine MCP or Frida for
scanning and hooking; ReAgentLive is the extra tool for what only the
running process can answer, and it hands every address over as
`module` + `rva` so the agent can carry it between them.

| Neighbour | It does | ReAgentLive gives it | ReAgentLive takes from it |
|---|---|---|---|
| A static tool (the main pairing) | whole functions, types, decompiling | live values, `module` + `rva`, exported labels, signature checks | names, what a function means |
| An engine dumper | a full SDK and offsets | a live check that an offset is right | names for what the engine does not reflect |
| An instrumentation tool | call tracing, hooks | exact addresses, live state before and after | who calls what |
| A memory scanner | narrowing scans, pointer scans | a consistent snapshot to confirm a find | candidate addresses |

## The safety model

- **Writes check first.** A write can require the bytes you expect to still
  be there; otherwise it is refused and shows what it found.
- **Identity, not just addresses.** An object reference carries its table
  slot and serial, so a replaced object is refused instead of written into.
- **Changes that end on their own.** Pins expire; a trial write restores the
  original bytes when it expires or is cleared, and stops writing as soon as
  its owner is replaced.
- **Engine calls only on the engine's thread.** A host that ticks elsewhere
  gets memory commands, and engine commands are refused.
- **Approval stays yours.** Anything that can change game state is a
  separate, explicit command.

## What it is not

- **Not an injector.** Something inside the process has to call its tick:
  your mod, or the example DLL loaded however you like.
- **Not "every engine" out of the box.** Memory and code work in any
  process; objects, fields and calls need an engine pack, and today there
  is one, for Unreal (UE5 layouts, built against your SDK dump).
- **Not a debugger, decompiler or signature maker.** It checks and reports;
  analysis stays with your static tool and your agent.

## Status

**Version 0.1.0** ([changelog](CHANGELOG.md)): usable, and the API can still
change before 1.0. Windows x64, C++23. Shipped: the in-process agent, tick-accurate reads,
guarded writes, pins and trials, the Unreal pack, the fast lane, native
objects, the console, the generated Python functions and MCP tools, and the own-thread example.
Planned items wait for a real need ([backlog](docs/ideas.md)).

**Docs: start at [docs/INDEX.md](docs/INDEX.md)**: pages by task, every
command and its page, and a glossary.

## License

MIT ([LICENSE](LICENSE)). Dependencies, all MIT: nlohmann/json and Zydis
(with Zycore) for the console, fetched at configure; doctest for the tests,
in `third_party/`.
