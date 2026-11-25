# ReAgentLive docs index

**TL;DR:** ReAgentLive is a DLL inside a running game that answers your
or your agent's questions about its memory, code and engine objects.
Start with [tiers](tiers.md) (what works where), then pick an
interface ([clients](client.md)), then the page for your task below. Every
command, its tier and its page: [commands](commands/README.md); words you
may not know are in the [glossary](#glossary).

ReAgentLive is an in-process agent for live game memory and runtime
inspection on Windows x64. A host inside the target calls its `Tick`; a
client outside (MCP tools, console, Python, C++) sends it requests over
shared memory. Code paths in the docs (`core/`, `packs/ue/`, `frontends/`)
are under `src/reagentlive/`.

## By task

| I want to | Read |
|---|---|
| Know what works where, and my tier | [tiers](tiers.md) |
| Pick how to drive it (MCP tools, console, Python, C++ client) | [clients](client.md) |
| Use the console or script it | [console](commands/console.md); scripts: [generated Python functions](client.md#generated-python-functions); AI agents: [MCP tools](client.md#mcp-tools) |
| Find a command, its tier and page | [commands](commands/README.md) |
| Know what a command takes and returns | `help` with `verb`, the [verb reference](commands/reference.md), [result shapes](commands/results.md) |
| Decompile live functions in IDA, carry names both ways | [IDA loader](commands/ida-loader.md), [labels](commands/labels.md) |
| Have an AI agent set it up or update it | [agents](agents/README.md) ([setup](agents/setup.md), [update](agents/update.md)) |
| Start a project of my own from a kit | [starting from a kit](hosting.md#starting-from-a-kit) |
| Try it in any process, no mod or SDK (T0) | [own-thread example](../examples/own_thread/README.md) |
| Any UE5 game, no dump (T1-T2; T3 once `game-thread` is on) | [Unreal example](../examples/unreal/README.md) |
| Put the agent into a process, or host it in a mod (T4) | [hosting](hosting.md) |
| Write a client or change the wire format | [protocol](protocol.md) |
| Keep a mod's logic outside the game, with a lean DLL inside | [outside programs](outside-programs.md) |
| Add a command or change the code | [contributing](contributing.md), [AGENTS.md](../AGENTS.md) |
| Find a repo script | [tools](../tools/README.md) |
| Know why something is built this way, or was rejected | [design](design/README.md), [tool interface](tool-interface.md) |
| Pick the next feature, or check it is not ruled out | [ideas](ideas.md) |
| Describe the project | [positioning](positioning.md) |
| Let an AI agent investigate a live game | the [`reagentlive` skill](../.claude/skills/reagentlive/SKILL.md) |

## Glossary

Terms the pages below define in place (ref, hold, undo, `same_tick`, verb
table, cache file, signature, fast lane) are not repeated here.

| Term | Meaning |
|---|---|
| Host | the code inside the target that calls `Tick` and states its thread; a mod or a small DLL ([hosting](hosting.md)) |
| Agent | the library code the host runs: dispatch and handlers |
| Tier | a rung of the ladder T0 Core to T4 SDK host: what works in a given host ([tiers](tiers.md)) |
| Game thread | the engine's own thread, the only one where its functions may be called; `host_thread: engine` in `status` ([tiers](tiers.md#two-axes)) |
| SDK, dump | C++ headers a dumper tool generates from a game's reflection data; ReAgentLive needs none below T4 |
| Engine layout | the engine offsets the Unreal pack reads with: a host's SDK, else the stock UE5 layout ([without an SDK](commands/unreal.md#without-an-sdk)) |
| Layout group | one set of engine offsets checked together on objects every UE5 game has, before its commands run |
| Pack | engine-specific agent handlers and client calls; today Unreal ([Unreal pack](commands/unreal.md)) |
| Interface | one of the four ways to send commands: MCP tools, console, Python, C++ client ([clients](client.md)) |
| `module` + `rva` | an address as module and offset, stable across launches; the bridge to static tools |
