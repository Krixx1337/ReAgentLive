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
| Know what a command takes and returns | `help <verb>` in the console, or [result shapes](commands/results.md) |
| Have an AI agent set it all up | [agent setup](agent-setup.md) (build, MCP tools, skill, check) |
| Try it in any process, no mod or SDK (T0) | [own-thread example](../examples/own_thread/README.md) |
| Any UE5 game, no dump (T1-T2; T3 after `game-thread on`) | [Unreal example](../examples/unreal/README.md) |
| Put the agent into a process, or host it in a mod (T4) | [hosting](hosting.md) |
| Write a client or change the wire format | [clients](client.md), [protocol](protocol.md) |
| Keep a mod's logic or a tool outside the game, with a lean DLL inside | [outside programs](outside-programs.md) |
| Add a command or change the code | [contributing](contributing.md) |
| Know why something is built this way, or was rejected | [design](design.md); for `help`, scripts and tools: [tool interface](tool-interface.md) |
| Pick the next feature, or check it is not ruled out | [ideas](ideas.md) |
| Describe the project | [positioning](positioning.md) |
| Let an AI agent investigate a live game | the [`reagentlive` skill](../.claude/skills/reagentlive/SKILL.md) (method, pitfalls, references by question) |

## Pages

| Page | What |
|---|---|
| [../README.md](../README.md) | the pitch: tiers at a glance, a demo, use cases, where to start, the setup prompt |
| [tiers.md](tiers.md) | the tier ladder T0-T4: what each gives, needs and ships in; reading your tier from `status` |
| [agent-setup.md](agent-setup.md) | instructions an AI agent follows to build, register the MCP tools, install the skill and check |
| [commands/console.md](commands/console.md) | command lines, `help` and the verb table, addresses and refs, round trips (`batch`, `get`, hold, cache), one-shot runs, JSON Lines |
| [commands/memory.md](commands/memory.md) | core memory commands: `read`, `read-batch`, `peek`, `write`, trials, `search`, `pin`, `watch`, typed layouts |
| [commands/fast-lane.md](commands/fast-lane.md) | one-address code questions: `disasm`, `xrefs`, `touches`, `search text`, `sig` |
| [commands/native-objects.md](commands/native-objects.md) | without an engine pack: `vtable`, pointer chains, `region`, strings |
| [commands/unreal.md](commands/unreal.md) | the Unreal pack: lookups, reflection, calls, the call recorder, `trace`, `each`, `census`, `dump`, labels, readable names, native members |
| [commands/capture.md](commands/capture.md) | which command for which question about change; `snap`, `record`, replay |
| [commands/results.md](commands/results.md) | every command's arguments and result keys, generated from the console's verb table; notation, conventions, envelopes |
| [hosting.md](hosting.md) | Unreal and own-thread examples, using it in your project (options, dependencies, version), host checklist, CMake targets, the tick, host thread, logging |
| [client.md](client.md) | C++ client library, generated Python functions, Python session, MCP tools |
| [outside-programs.md](outside-programs.md) | the core as a lean in-process proxy for an outside program: what it gives, limits, what fits |
| [protocol.md](protocol.md) | shared section, exchange and hold, handshake, ops, statuses, limits |
| [design.md](design.md) | principles, decisions, rejected alternatives, not now |
| [tool-interface.md](tool-interface.md) | the self-describing console and the tool interface: verb table, `help --json`, generated functions, MCP tools, their decisions |
| [contributing.md](contributing.md) | layers, adding a command, source layout, tests, code rules, where each doc fact goes |
| [ideas.md](ideas.md) | backlog with triggers; what is out |
| [positioning.md](positioning.md) | the pitch, role among other tools, low footprint, safety model, what it is not |

## Commands

Every console verb, the [tier](tiers.md) it needs and its page. **T0**
commands work in any process, the own-thread example included. **T1-T3**
commands need a host built with the Unreal pack; on any other host they are
refused with a message saying so, and `status` lists the packs a host
offers (`packs`). **T3** commands also need the game thread
([which ones](commands/unreal.md#off-the-engines-thread)).

| Command | Needs | Page |
|---|---|---|
| `read`, `read-batch`, `peek`, `write`, `modules` | T0 | [memory](commands/memory.md) |
| `pin`, `pin-list`, `pin-clear` | T0 | [memory](commands/memory.md#pin) |
| `watch`, `watch-start`, `watch-read`, `watch-stop` | T0 | [memory](commands/memory.md#watch) |
| `trial`, `trial-list`, `trial-clear` | T0; the owner check needs T1 | [memory](commands/memory.md) |
| `search` | T0 | [memory](commands/memory.md#search) (`search text`: [fast lane](commands/fast-lane.md#search-text)) |
| `read --type "<layout>"` | T0 | [memory](commands/memory.md#typed-layouts) |
| `read --type cstr\|wstr`, `<addr>->off`, `vtable`, `region` | T0 | [native objects](commands/native-objects.md) |
| `rtti` | T0 | [native objects](commands/native-objects.md#rtti) |
| `mem-snap`, `mem-diff` | T0 | [memory](commands/memory.md#mem-snap-and-mem-diff) |
| `disasm`, `xrefs`, `touches` | T0 | [fast lane](commands/fast-lane.md) |
| `sig-check`, `sig-file`, `sig-bytes` | T0 | [fast lane](commands/fast-lane.md#signatures) |
| `find`, `describe`, `name`, `name-find` | T1 | [Unreal pack](commands/unreal.md) |
| `fields`, `entries`, `func`, `funcs`, `enum` | T2 | [Unreal pack](commands/unreal.md) |
| `rows` | T2 | [Unreal pack](commands/unreal.md#rows) |
| `call`, `call-pure`, `release`, `params`, `script`, `refs`, `trace` | T3 | [Unreal pack](commands/unreal.md) |
| `game-thread` | Unreal pack, a host linking `reagentlive::ue-draw`; `on` gives T3 | [Unreal pack](commands/unreal.md) |
| `calls-start`, `calls-read`, `calls-stop` | T3; the host lists the ops | [Unreal pack](commands/unreal.md#call-recorder) |
| `layout-export`, `layout-check` | T2 | [Unreal pack](commands/unreal.md#layouts-in-bulk) |
| `each` | T3 | [Unreal pack](commands/unreal.md#each) |
| `census`, `census-diff` | T1 | [Unreal pack](commands/unreal.md#census) |
| `dump` | T2 | [Unreal pack](commands/unreal.md#dump), filters: [console](commands/console.md#fewer-round-trips) |
| `export-labels` | T3 (native exec thunks); other `--kinds` T2 | [Unreal pack](commands/unreal.md#labels-for-a-static-tool) |
| `peek --label` | T0 | [Unreal pack](commands/unreal.md#labels-for-a-static-tool) |
| `label` | T2 | [Unreal pack](commands/unreal.md#native-members) |
| `snap`, `record`, `--replay` | T2 | [capture](commands/capture.md) |
| `get`, `--meta` | T2 | [console](commands/console.md#fewer-round-trips) |
| `batch`, `--timing` | T0 | [console](commands/console.md#fewer-round-trips) |
| `status`, `base`, `help`, `quit`, `-c` | T0 | [console](commands/console.md) |
| `stats`, `--caller`, `--no-usage` | T0; no target needed | [console](commands/console.md#usage-log-and-stats) |

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
| DLL loader, injector | any tool that loads a DLL into a running process; ReAgentLive ships none ([agent setup](agent-setup.md) can fetch one) |
| MCP, MCP tools | the Model Context Protocol, how AI agents call tools; the server makes one tool per console verb ([MCP tools](client.md#mcp-tools)) |
| Interface | one of the four ways to send commands: MCP tools, console, Python, C++ client ([clients](client.md)) |
| Skill | the `reagentlive` folder of instructions an AI agent reads before investigating ([skill](../.claude/skills/reagentlive/SKILL.md)) |
| Signature | a byte pattern, with wildcards, that finds a piece of code again after a patch ([fast lane](commands/fast-lane.md#signatures)) |
| Pack | engine-specific agent handlers and client calls; today Unreal ([Unreal pack](commands/unreal.md)) |
| Console | the command-line client; JSON Lines out ([console](commands/console.md)) |
| Tick | one host call of `Tick`; on the engine's thread, one moment of the game |
| Held request | a request that says more follow, so the agent runs the next in the same tick ([protocol](protocol.md#exchange)) |
| `same_tick` | whether a `batch` or `get` ran in one tick ([console](commands/console.md#fewer-round-trips)) |
| Ref, `$N` | an object address with its table slot and serial, so a replaced object is detected; `$N` is result N of the last command that lists refs ([console](commands/console.md#addresses-and-refs)) |
| `module` + `rva` | an address as module and offset, stable across launches; the bridge to static tools |
| Fast lane | one-address code questions answered from live bytes, no database ([fast lane](commands/fast-lane.md)) |
| Pin | bytes rewritten every tick until it expires ([memory](commands/memory.md#pin)) |
| Trial | one guarded write that restores itself on expiry or clear ([memory](commands/memory.md)) |
| Root | an object the host can always find again, for `watch-start --follow` |
| Verb table | one row per console verb: its arguments (key, type, default) and result type; `help`, parsing, JSON calls, the results page, strict Python results, the generated functions and the MCP tools read it ([design](tool-interface.md#self-describing-console)) |
| JSON call | a console input line `{verb, args}` with arguments named by the row's keys; the typed line is shorthand for it ([console](commands/console.md#json-calls)) |
| Cache file | `%TEMP%\reagentlive-<pid>.cache`: state kept between console runs ([console](commands/console.md#fewer-round-trips)) |
| Usage log | `<console>.usage.jsonl`: one line of facts per console call, read by `stats` ([console](commands/console.md#usage-log-and-stats)) |
