# Engine calls and writes

Approval: SKILL.md rules. Call semantics and named inputs:
`docs/commands/unreal-calls.md#named-calls`; timed writes:
`docs/commands/memory.md#timed-writes`.

## Calling functions

- `call`, `call-pure`, `call-each` and `release` need `host_thread: engine` (T3);
  otherwise the game-thread reference first.
- Find the function with `funcs`, copy exact input names from `func`'s `params`,
  then `call` with `dry` to see the block before anything runs:
  `{"verb": "call", "args": {"object": "0x1f2e3d4c50#123:4", "function": "GetHealth", "params": {}, "dry": true}}`.
- Getters: `call-pure` first; a refusal means it needs approval and
  `call`. Before asking for a `call`, put its `dry` output in the request
  (the function, its `exec` address, `pure`, the exact block).
- A call refused as faulted was caught inside the engine; the
  game may now be unstable. Tell the user, and confirm later readings with
  a second read.
- A `funcs` row declared only on a native interface (`help` on `funcs`): never
  `call` it; follow the native-and-blueprint reference.
- Static library functions: call them on the library's `Default__` object.
- **Owned results** (the result says it kept them; `help` on `call`):
  `release` with the same `object` and `function` once when done.
  Verify on the handle's control block with `read` (+8 shared, +12 weak
  count): the count stays flat across repeats and drops after `release`.
- Getters that return a large engine struct by value (a stats struct of a
  few KB with arrays inside) copy owned arrays into the console's block:
  read the asset fields instead.

## Writing memory

- `write` with `expect` set to the bytes you just read: heap memory can be reused
  between a read and a write.
- `search` twice a second apart and keep the intersection: heap addresses
  that churn are per-frame temporaries. Re-read the vtable right before
  every write; a block can be reused for another type in between.
- Never write through a pointer chain: resolve it, read, then write to that
  address with `expect`.
- When a game setter refuses (authority, validation), look at the object it
  writes: native value objects often keep plain fields the game reads
  directly. Verify the layout against the game's own getter right before
  each write.
- A value that snaps back within one tick is a copy (movement components
  can keep a copy of prediction state). Find the source before trying
  per-tick writes.
- Class or tier tables in data assets are often re-read only on an event
  (inventory change, equip): after a write, ask the user to trigger it
  before judging.
- **Writes can be blocked.** Even with the user's go-ahead, the session's
  permission check may refuse a state-changing `call`. Plan read-only
  evidence (getters, refcounts, designer data) that stands without the
  write, and leave the effect check to the user's session.

## Timed writes (`write` with `undo`)

- **Use `write` with `undo` to settle which way a lead goes** (does a multiplier
  shorten an action?). It writes once and the agent restores the original;
  a value the game rewrites meanwhile ends `conflict`. Its result is
  evidence for a lead's direction, not validation of a shipped feature.
- An attribute behind a lookup handle: `references/unreal/gameplay-systems.md#attributes-and-effects`.
- **To time an action**, capture its Blueprint action object's state fields
  (frame counters, flags) in one untouched and one trial run and compare
  the times between matching state changes
  (the capture-and-timing reference).
- **A/B on an AI character.**
  1. Confirm the value sits still at rest: two reads a second apart.
  2. Capture the value and the action's progress over several untouched
     actions.
  3. `write` the value with `undo` and capture the same ranges again.
  4. Compare durations on that one character, with an untouched one in the
     same capture as a control when one is in view.
- Read the end state with `writes`. Clear them
  (`{"verb": "timed-writes", "args": {"clear": true}}`) before the host stops or
  you reinject: a stopped agent no longer ticks, so its `undo` writes never
  restore.

## Held writes (`write` with `hold`)

- `hold` writes the same bytes every tick and, when it runs out, leaves
  them as they are (it restores nothing); an `addr` of
  `{"base": "0x1f2e3d4c50#123:4", "offset": 16}` stops when the object
  dies. A held value that holds is integrated state; one the game still
  overrides each frame is recomputed from scratch.
