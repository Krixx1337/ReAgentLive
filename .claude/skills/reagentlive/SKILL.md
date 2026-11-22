---
name: reagentlive
description: Investigate a running game with ReAgentLive, an in-process agent driven by MCP tools, a console or Python - find objects and UObject instances, read reflected and native fields, decode FName and gameplay tag indices, call engine getters on the game thread, capture runtime behaviour, read live code (disasm, xrefs, signatures) and turn findings into evidence-backed changes. Use for any runtime question about game data, engine objects or a mod's behaviour, before guessing from an SDK dump; also to read which tier a target gives and to climb it.
---

# ReAgentLive investigations

An agent inside the target process, ticked by a host (a mod or a DLL), runs
the verbs every interface sends. Reference docs: [INDEX](https://github.com/Krixx1337/ReAgentLive/blob/main/docs/INDEX.md).
Read the project's host notes first (console name, host verbs, build and
approval rules); they win over this skill.

## First command: `status`, then your tier

`status` prints no tier ([full reading](https://github.com/Krixx1337/ReAgentLive/blob/main/docs/tiers.md#read-your-tier-from-status)):
`packs` empty: T0. `packs: ["ue"]`, `host_thread` `own`/`borrowed`: T1, T2
once `fields Vector` answers. `host_thread: engine`: T3. Real serials (not `:0`): T4.

- T0: Unreal verbs need a host with the pack ([tiers](https://github.com/Krixx1337/ReAgentLive/blob/main/docs/tiers.md#the-ladder)).
- T1 (`fields Vector` refused with a layout-group check): load `references/unreal/new-game.md`.
- T2: `game-thread on` (approval); load `references/unreal/game-thread.md`.
- T3: T4 is a host built with the SDK ([hosting](https://github.com/Krixx1337/ReAgentLive/blob/main/docs/hosting.md)).
- What answers below T3: [off the engine's thread](https://github.com/Krixx1337/ReAgentLive/blob/main/docs/commands/unreal.md#off-the-engines-thread).
  A refusal `not on the game thread` means: climb, or answer it by reads.
- Below T3 a tick is not a game moment: values from separate reads need not line up.

## Rules

- Reads need no approval: every verb `help` does not mark `writes`, `each`
  over field paths, and pure getters through `call-pure` or an `each`
  column (say what you call).
- Except the first call on a new host or in a new game (the known-answer
  call, `call-pure` included): ask first, since a wrong engine layout can
  crash the game.
- Ask the user first for any `write`, `pin`, `calls-start`, `game-thread on`
  (it writes one pointer in the viewport object) and any `call` that could
  change state or crash; `trial` as the host's rules say. Give the address,
  the bytes now, the bytes new and the expected effect; for a call, its
  `call --dry` output. One approval covers one write.
- Never guess a flag or a result key: `help <verb>` (no target needed).
  Behaviour and guards: the verb's page under [commands](https://github.com/Krixx1337/ReAgentLive/blob/main/docs/INDEX.md#commands).
- Rely on literal refs (`0x<address>#<index>:<serial>`). Values that must
  line up come from one `get`, `read-batch` or `batch`; check `spans_ticks`.
- Only the user's live session is evidence; synthetic tests are not.
  Record live results where the project keeps them, stating what is and is
  not established.

## Pick the interface

MCP tools are the default. Python when the next step is a loop or a
computation over results. The console when MCP is absent or for a human.
Mapping, gaps and what MCP leaves out: load `references/interfaces.md`.

## Load when

| When you... | Load |
|---|---|
| need the verb for a task, or one you have not used this session | [references/commands-by-task.md](references/commands-by-task.md) |
| start an investigation, or a capture shows no change | [references/workflow.md](references/workflow.md) |
| plan more than a few calls, see a `digest`, or end a session | [references/efficiency.md](references/efficiency.md) |
| choose MCP, Python or the console, loop over many objects (`each` first), or need a verb's tool or function name | [references/interfaces.md](references/interfaces.md) |
| drive the console or Python by hand: quoting, parsing JSON Lines, generating the module | [references/console-driving.md](references/console-driving.md) |
| sample a value over time, record a function's calls, or see what changes across an action (one that resets at once, like a jump, too) | [references/capture-and-timing.md](references/capture-and-timing.md) |
| send any `call` (pure getters too), `write`, `pin` or `trial` | [references/calls-and-writes.md](references/calls-and-writes.md) |
| read an object with no reflection, hunt padding or native state, or `search` memory | [references/hidden-state.md](references/hidden-state.md) |
| disassemble, follow a native getter, read Blueprint bytecode, cross to a static tool, or make or check a signature | [references/native-code.md](references/native-code.md) |
| rebuild, reinject, or see an attach, layout or `busy` error | [references/reinjecting.md](references/reinjecting.md) |
| ask about tags, attributes, effects, "why can't I do X", data tables, maps, traces or replication (Unreal) | [references/unreal/gameplay-systems.md](references/unreal/gameplay-systems.md) |
| ask about poses, anim layers or nodes, aiming, lean, camera or bones (Unreal) | [references/unreal/animation-and-camera.md](references/unreal/animation-and-camera.md) |
| work in an Unreal game with no dump, or a reflected verb is refused with a layout-group check | [references/unreal/new-game.md](references/unreal/new-game.md) |
| find one layout group's offsets (struct, field, property, typed, class, function, enum, interface, containers, call) | [references/unreal/layout-groups.md](references/unreal/layout-groups.md) |
| `host_thread` is not `engine`, or a verb is refused `not on the game thread` | [references/unreal/game-thread.md](references/unreal/game-thread.md) |
| end a session, hit a limit, or edit this skill | [references/maintaining.md](references/maintaining.md) |

## Limits and upkeep

- A missing verb or flag: add it instead of working around it twice
  ([references/maintaining.md](references/maintaining.md)).
- At the end of every session and before any edit to this skill: load
  `references/maintaining.md`.
