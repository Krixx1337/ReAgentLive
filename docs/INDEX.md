# ReAgentLive docs index

ReAgentLive is an in-process agent for live game memory and runtime
inspection on Windows x64. A host inside the target calls its `Tick`; a
client outside (console, Python, C++) sends it requests over shared memory.
Start here: find your task, read that page, follow its links. Code paths
in the docs (`core/`, `ue/`, `frontends/`) are under `src/reagentlive/`.

## By task

| I want to | Read |
|---|---|
| Use the console or drive it from a script | [console](commands/console.md), then the command's page below; scripts: [generated Python functions](client.md#generated-python-functions); AI agents: [MCP tools](client.md#mcp-tools) |
| Know what a command takes and returns | `help <verb>` in the console, or [result shapes](commands/results.md) |
| Try it in any process, no mod or SDK | [own-thread example](hosting.md#own-thread-example) |
| Put the agent into a process | [hosting](hosting.md) |
| Write a client or change the wire format | [clients](client.md), [protocol](protocol.md) |
| Keep a mod's logic or a tool outside the game, with a lean DLL inside | [outside programs](outside-programs.md) |
| Add a command or change the code | [contributing](contributing.md) |
| Know why something is built this way, or was rejected | [design](design.md) |
| Pick the next feature, or check it is not ruled out | [ideas](ideas.md) |
| Describe the project | [positioning](positioning.md) |
| Let an AI agent investigate a live game | the [`reagentlive` skill](../.claude/skills/reagentlive/SKILL.md) (method, pitfalls, references by question) |

## Pages

| Page | Home of |
|---|---|
| [../README.md](../README.md) | the pitch: what it is, why it is different, a demo, try it in five minutes |
| [commands/console.md](commands/console.md) | command lines, `help` and the verb table, addresses and refs, round trips (`batch`, `get`, hold, cache), one-shot runs, JSON Lines |
| [commands/memory.md](commands/memory.md) | core memory commands: `read`, `read-batch`, `peek`, `write`, trials, `search`, `pin`, `watch`, typed layouts |
| [commands/fast-lane.md](commands/fast-lane.md) | one-address code questions: `disasm`, `xrefs`, `touches`, `search text`, `sig` |
| [commands/native-objects.md](commands/native-objects.md) | without an engine pack: `vtable`, pointer chains, `region`, strings |
| [commands/unreal.md](commands/unreal.md) | the Unreal pack: lookups, reflection, calls, `each`, `census`, `dump`, labels, readable names, native members |
| [commands/capture.md](commands/capture.md) | which command for which question about change; `snap`, `record`, replay |
| [commands/results.md](commands/results.md) | every command's arguments and result keys, generated from the console's verb table; notation, conventions, envelopes |
| [hosting.md](hosting.md) | own-thread example, using it in your project (options, dependencies, version), host checklist, CMake targets, the tick, host thread, logging |
| [client.md](client.md) | C++ client library, generated Python functions, Python session, MCP tools |
| [outside-programs.md](outside-programs.md) | the core as a lean in-process proxy for an outside program: what it gives, limits, what fits |
| [protocol.md](protocol.md) | shared section, exchange and hold, handshake, ops, statuses, limits |
| [design.md](design.md) | principles, decisions (the self-describing console among them), rejected alternatives, not now |
| [contributing.md](contributing.md) | layers, adding a command, source layout, tests, code rules, where each doc fact goes |
| [ideas.md](ideas.md) | backlog with triggers; what is out |
| [positioning.md](positioning.md) | the pitch, role among other tools, what it is not |

## Commands

Every console verb and its page. **Core** commands work in any process,
the own-thread example included. **Unreal pack** commands need a host built
with the pack; on any other host they are refused with a message saying so,
and `status` lists the packs a host offers (`packs`).

| Command | Needs | Page |
|---|---|---|
| `read`, `read-batch`, `peek`, `write`, `modules` | core | [memory](commands/memory.md) |
| `pin`, `pin-list`, `pin-clear` | core | [memory](commands/memory.md#pin) |
| `watch`, `watch-start`, `watch-read`, `watch-stop` | core | [memory](commands/memory.md#watch) |
| `trial`, `trial-list`, `trial-clear` | core; the owner check needs the Unreal pack | [memory](commands/memory.md) |
| `search` | core | [memory](commands/memory.md#search) (`search text`: [fast lane](commands/fast-lane.md#search-text)) |
| `read --as "<layout>"` | core | [memory](commands/memory.md#typed-layouts) |
| `read --as cstr\|wstr`, `<addr>->off`, `vtable`, `region` | core | [native objects](commands/native-objects.md) |
| `disasm`, `xrefs`, `touches` | core | [fast lane](commands/fast-lane.md) |
| `sig-check`, `sig-file`, `sig-bytes` | core | [fast lane](commands/fast-lane.md#signatures) |
| `find`, `describe`, `name`, `name-find`, `fields`, `entries`, `refs` | Unreal pack | [Unreal pack](commands/unreal.md) |
| `call`, `release`, `func`, `funcs`, `params`, `script` | Unreal pack | [Unreal pack](commands/unreal.md) |
| `each` | Unreal pack | [Unreal pack](commands/unreal.md#each) |
| `census`, `census-diff` | Unreal pack | [Unreal pack](commands/unreal.md#census) |
| `dump` | Unreal pack | [Unreal pack](commands/unreal.md#dump), filters: [console](commands/console.md#fewer-round-trips) |
| `export-labels` | Unreal pack | [Unreal pack](commands/unreal.md#labels-for-a-static-tool) |
| `peek --label` | core | [Unreal pack](commands/unreal.md#labels-for-a-static-tool) |
| `label` | Unreal pack | [Unreal pack](commands/unreal.md#native-members) |
| `snap`, `record`, `--replay` | Unreal pack | [capture](commands/capture.md) |
| `get`, `--meta` | Unreal pack | [console](commands/console.md#fewer-round-trips) |
| `batch`, `--timing` | core | [console](commands/console.md#fewer-round-trips) |
| `status`, `base`, `help`, `quit`, `-c` | core | [console](commands/console.md) |

## Glossary

| Term | Meaning |
|---|---|
| Host | the code inside the target that calls `Tick` and states its thread; a mod or a small DLL ([hosting](hosting.md)) |
| Agent | the library code the host runs: dispatch and handlers |
| Pack | engine-specific agent handlers and client calls; today Unreal ([Unreal pack](commands/unreal.md)) |
| Console | the command-line client; JSON Lines out ([console](commands/console.md)) |
| Tick | one host call of `Tick`; on the engine's thread, one moment of the game |
| Held request | a request that says more follow, so the agent runs the next in the same tick ([protocol](protocol.md#exchange)) |
| `same_tick` | whether a `batch` or `get` ran in one tick ([console](commands/console.md#fewer-round-trips)) |
| Ref, `$N` | an object address with its table slot and serial, so a replaced object is detected; `$N` is the Nth ref of the last `find` ([console](commands/console.md#addresses-and-refs)) |
| `module` + `rva` | an address as module and offset, stable across launches; the bridge to static tools |
| Fast lane | one-address code questions answered from live bytes, no database ([fast lane](commands/fast-lane.md)) |
| Pin | bytes rewritten every tick until it expires ([memory](commands/memory.md#pin)) |
| Trial | one guarded write that restores itself on expiry or clear ([memory](commands/memory.md)) |
| Root | an object the host can always find again, for `watch-start --follow` |
| Verb table | one row per console verb: its arguments (key, type, default) and result type; `help`, parsing, JSON calls, the results page, strict Python results, the generated functions and the MCP tools read it ([design](design.md#self-describing-console)) |
| JSON call | a console input line `{verb, args}` with arguments named by the row's keys; the typed line is shorthand for it ([console](commands/console.md#json-calls)) |
| Cache file | `%TEMP%\reagentlive-<pid>.cache`: state kept between console runs ([console](commands/console.md#fewer-round-trips)) |
