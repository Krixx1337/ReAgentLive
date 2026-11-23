# ReAgentLive docs index

**TL;DR:** ReAgentLive is a DLL inside a running game that answers your
or your agent's questions about its memory, code and engine objects.
Start with [tiers](tiers.md) (what works where), then pick an
interface ([clients](client.md)), then the page for your task below. Every
command is listed with its page and the tier it needs; words you may not
know are in the [glossary](#glossary).

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
| Use the console or drive it from a script | [console](commands/console.md), then the command's page below; scripts: [generated Python functions](client.md#generated-python-functions); AI agents: [MCP tools](client.md#mcp-tools) |
| Decompile live functions in IDA, carry names both ways | [IDA loader](commands/ida-loader.md), [labels](commands/labels.md) |
| Know what a command takes and returns | `help` with `verb` in the console, the [verb reference](commands/reference.md), or [result shapes](commands/results.md) |
| Have an AI agent set it all up, or update it | [for AI agents](#for-ai-agents) |
| Try it in any process, no mod or SDK (T0) | [own-thread example](../examples/own_thread/README.md) |
| Any UE5 game, no dump (T1-T2; T3 once `game-thread` is on) | [Unreal example](../examples/unreal/README.md) |
| Put the agent into a process, or host it in a mod (T4) | [hosting](hosting.md) |
| Write a client or change the wire format | [clients](client.md), [protocol](protocol.md) |
| Keep a mod's logic or a tool outside the game, with a lean DLL inside | [outside programs](outside-programs.md) |
| Add a command or change the code | [contributing](contributing.md) |
| Change ReAgentLive with an AI agent | [AGENTS.md](../AGENTS.md) (commands, hard rules, large changes) |
| Know why something is built this way, or was rejected | [design](design.md): [console costs](design/console-costs.md), [architecture](design/architecture.md), [Unreal pack](design/unreal.md), [native code](design/native.md); for `help`, scripts and tools: [tool interface](tool-interface.md) |
| Pick the next feature, or check it is not ruled out | [ideas](ideas.md) |
| Describe the project | [positioning](positioning.md) |
| Let an AI agent investigate a live game | the [`reagentlive` skill](../.claude/skills/reagentlive/SKILL.md) (method, pitfalls, references by question) |

## For AI agents

Procedures an agent runs on the user's machine, in [agents/](agents/README.md);
the README's paste prompts send an agent there.

| Procedure | What |
|---|---|
| [which one](agents/README.md) | which procedure when, the rules they share |
| [setup](agents/setup.md) | first install: build, MCP tools, skill, check, setup record |
| [update](agents/update.md) | update a clone: pull, rebuild, skill, MCP server, re-inject, check `commit` |

## Commands

Every console verb, the [tier](tiers.md) it needs and its page. **T0**
commands work in any process, the own-thread example included. **T1-T3**
commands need a host built with the Unreal pack; on any other host they are
refused with a message saying so, and `status` lists the packs a host
offers (`packs`). **T3** commands also need the game thread
([which ones](commands/unreal.md#off-the-engines-thread)).

| Command | Needs | Page |
|---|---|---|
| `read`, `peek`, `write`, `modules` | T0 | [memory](commands/memory.md) |
| `write` with `hold` or `undo`, `timed-writes` | T0; `undo`'s owner check needs T1 | [memory](commands/memory.md#timed-writes) |
| `watch-start`, `watch-read`, `watch-stop` | T0 (`<ref>.<Field>` ranges: T2) | [memory](commands/memory.md#watch) |
| `search`, `search-job` | T0 | [memory](commands/memory.md#search) (text: [fast lane](commands/fast-lane.md#search-text)) |
| `read` with a layout `type` | T0 | [memory](commands/memory.md#typed-layouts) |
| `read` with `type` `cstr\|wstr`, pointer chains, `vtable`, its `rtti` | T0 | [native objects](commands/native-objects.md) |
| `mem-snap`, `mem-diff` | T0 | [memory](commands/memory.md#mem-snap-and-mem-diff) |
| `disasm`, `xrefs`, `touches` | T0 | [fast lane](commands/fast-lane.md) |
| `sig-check` (one `pattern` or a `file`), `sig-bytes` | T0 | [fast lane](commands/fast-lane.md#signatures) |
| `peek` with `label`, `labels-sign`, `labels-port` | T0 | [labels](commands/labels.md) |
| `labels-export` | T3 (exec thunks); other `kinds` T2 | [labels](commands/labels.md#labels-export) |
| `find`, `describe`, `name`, `name-find`, `census` (and its `diff`) | T1 | [Unreal objects](commands/unreal-objects.md) |
| `enum`, `assets-export` | T2 | [Unreal objects](commands/unreal-objects.md) |
| `each` | T3 | [Unreal objects](commands/unreal-objects.md#each) |
| `get` (every mode), `entries`, `rows`, `member-name`, `layout-export`, `layout-check` | T2 | [Unreal fields](commands/unreal-fields.md) (snapshots over time: [capture](commands/capture.md)) |
| `func`, `funcs` | T2 (`func`'s `params`: T3) | [Unreal calls](commands/unreal-calls.md#func-and-funcs) |
| `call`, `call-pure`, `call-each`, `release`, `line-trace` | T3 | [Unreal calls](commands/unreal-calls.md) |
| `calls-start`, `calls-read`, `calls-stop` | T3; the host lists the ops | [Unreal calls](commands/unreal-calls.md#call-recorder) |
| `game-thread` | Unreal pack, a host linking `reagentlive::ue-draw`; `on` gives T3 | [Unreal calls](commands/unreal-calls.md#game-thread) |
| `script` with `decode` | T2 (any thread) | [Unreal code](commands/unreal-code.md#decode) |
| `script`, `name-refs` | T3 | [Unreal code](commands/unreal-code.md) |
| `batch`, `timing` | T0 | [console](commands/console.md#fewer-round-trips) |
| `run` (plan files) | T0 | [console](commands/console.md#plans) |
| `status`, `help`, `quit`, `-c` | T0 | [console](commands/console.md) |
| `usage`, `--caller`, `--no-usage` | T0; no target needed | [console](commands/console.md#usage-log-and-usage) |

A host's own verbs (`help` lists them under its origin) come from the
host, not this library: see the host's docs.

What each verb takes and returns: [verb reference](commands/reference.md)
and [result shapes](commands/results.md). The Unreal pack's shared rules
(identity, layout groups, thread rules): [Unreal pack](commands/unreal.md).

## Glossary

| Term | Meaning |
|---|---|
| Host | the code inside the target that calls `Tick` and states its thread; a mod or a small DLL ([hosting](hosting.md)) |
| Agent | the library code the host runs: dispatch and handlers |
| Tier | a rung of the ladder T0 Core, T1 Unreal located, T2 Reflection, T3 Game thread, T4 SDK host: what works in a given host ([tiers](tiers.md)) |
| Game thread | the engine's own thread, the only one where its functions may be called; `host_thread: engine` in `status` ([tiers](tiers.md#two-axes)) |
| Off-thread reads | Unreal reads answered from a host that ticks elsewhere, under fault guards, when the host opts in ([Unreal pack](commands/unreal.md#off-the-engines-thread)) |
| `game-thread` | the verb that moves the Unreal example's tick onto the game thread through the game viewport's Draw (`reagentlive::ue-draw`) ([Unreal pack](commands/unreal.md)) |
| SDK, dump | C++ headers a dumper tool generates from a game's reflection data; ReAgentLive needs none below T4 |
| Engine layout, stock layout | the engine offsets the Unreal pack reads with (`packs/ue/engine_layout.h`): a host's SDK, else `StockUE5`, the stock UE5 layout ([without an SDK](commands/unreal.md#without-an-sdk)) |
| Layout group | one set of engine offsets (class, struct, field, property, typed, function, enum, call) checked together on objects every UE5 game has, before its commands run ([without an SDK](commands/unreal.md#without-an-sdk)) |
| Reflection | the engine's own description of its classes, fields and functions, which lets the pack work by name |
| UObject | the base of every Unreal engine object; the pack finds them in the engine's object table |
| DLL loader, injector | any tool that loads a DLL into a running process; ReAgentLive ships none ([agent setup](agents/setup.md) can fetch one) |
| MCP, MCP tools | the Model Context Protocol, how AI agents call tools; the server makes one tool per console verb ([MCP tools](client.md#mcp-tools)) |
| Interface | one of the four ways to send commands: MCP tools, console, Python, C++ client ([clients](client.md)) |
| Skill | the `reagentlive` folder of instructions an AI agent reads before investigating ([skill](../.claude/skills/reagentlive/SKILL.md)) |
| Signature | a byte pattern, with wildcards, that finds a piece of code again after a patch ([fast lane](commands/fast-lane.md#signatures)) |
| Pack | engine-specific agent handlers and client calls; today Unreal ([Unreal pack](commands/unreal.md)) |
| Console | the command-line client; JSON Lines out ([console](commands/console.md)) |
| Tick | one host call of `Tick`; on the engine's thread, one moment of the game |
| Held request | a request that says more follow, so the agent runs the next in the same tick ([protocol](protocol.md#exchange)) |
| `same_tick` | whether a `batch` or `get` ran in one tick ([console](commands/console.md#fewer-round-trips)) |
| Ref | an object address with its table slot and serial, `0x<address>#<index>:<serial>`, so a replaced object is detected; pass the literal ref a result printed ([console](commands/console.md#addresses-and-refs)) |
| `module` + `rva` | an address as module and offset, stable across launches; the bridge to static tools |
| Fast lane | one-address code questions answered from live bytes, no database ([fast lane](commands/fast-lane.md)) |
| Hold | bytes rewritten every tick until it expires, then left as they are: `write` with `hold` ([memory](commands/memory.md#timed-writes)) |
| Undo | one guarded write that restores itself on expiry or clear: `write` with `undo` ([memory](commands/memory.md#timed-writes)) |
| Root | an object the host can always find again, for `watch-start` with `follow` |
| Verb table | one row per console verb: its arguments (key, type, default) and result type; `help`, parsing, JSON calls, the results page, strict Python results, the generated functions and the MCP tools read it ([design](tool-interface.md#self-describing-console)) |
| JSON call | a console input line `{verb, args}` with arguments named by the row's keys; the console's only input form ([console](commands/console.md#json-calls)) |
| Cache file | `%TEMP%\reagentlive-<pid>.cache`: state kept between console runs ([console](commands/console.md#fewer-round-trips)) |
| Usage log | `<console>.usage.jsonl`: one line of facts per console call, read by `usage` ([console](commands/console.md#usage-log-and-usage)) |
