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
- A getter returning a large engine struct by value (KBs, arrays inside)
  copies owned arrays into the console's block: read the asset fields.

## Writing memory

- `write` with `expect` set to the bytes you just read: heap memory can be reused
  between a read and a write.
- Heap addresses that churn between two `search`es a second apart are
  per-frame temporaries. Re-read the vtable right before every write; a
  block can be reused for another type.
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
- **Writes can be blocked.** The session's permission check may refuse a
  state-changing `call` despite the user's go-ahead: plan read-only
  evidence that stands without it, and leave the effect check to the user.

## Timed writes (`write` with `undo`)

- Use it to settle which way a lead goes (does a multiplier shorten an
  action?): it writes once and the agent restores the original; a value
  the game rewrites meanwhile ends `conflict`. Evidence of direction, not
  validation of a shipped feature. Compare against an untouched run, or an
  untouched character in the same capture (the capture-and-timing
  reference).
- An attribute behind a lookup handle: `references/unreal/gameplay-systems.md#attributes-and-effects`.
- Read the end state with `timed-writes`. `clear` them
  (`{"verb": "write", "args": {"clear": "all"}}`, approval as for a write)
  before the host stops or you reinject: a stopped agent no longer ticks,
  so its `undo` writes never restore.

## Held writes (`write` with `hold`)

- `hold` writes the same bytes every tick and, when it runs out, leaves
  them as they are (it restores nothing); an `addr` of
  `{"base": "0x1f2e3d4c50#123:4", "offset": 16}` stops when the object
  dies. A held value that holds is integrated state; one the game still
  overrides each frame is recomputed from scratch.
