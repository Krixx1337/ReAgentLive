# Engine calls, writes and pins

Every `write`, `pin` and state-changing `call` needs the user's approval
first (SKILL.md rules). Call semantics and named inputs:
named calls (`docs/commands/unreal.md#named-calls`).

## Calling functions

- `call`, `call-pure` and `release` need `host_thread: engine` (T3);
  otherwise the game-thread reference first.
- Approval: pure getters through `call-pure` need none (say what you
  call), except the first call on a new host or in a new game, the
  known-answer call (SKILL.md rules).
- Find the function with `funcs`, copy exact input names from `params`,
  then `call --dry` to see the block before anything runs.
- Getters: `call-pure` first. A refusal means the function is not const or
  pure, or keeps an owned result: it needs approval and `call`.
- Before asking for a `call`, put its `--dry` output in the request: the
  function, its `exec` address, `pure`, and the exact block.
- A call refused as faulted was caught inside the engine; the
  game may now be unstable. Tell the user, and confirm later readings with
  a second read.
- A `funcs` row declared only on a native interface (`help funcs`): never
  `call` it; follow the native-interface method in the native-code reference.
- Static library functions: call them on the library's `Default__` object.
- To cross-check a typed getter against a raw call, compare the complete
  result blocks on a stable target before relying on decoded output.
- **Owned results** (the result says it kept them; `help call`):
  `release <object> <Fn>` once when done.
  Verify on the handle's control block with `read` (+8 shared, +12 weak
  count): the count stays flat across repeats and drops after `release`.
- Getters that return a large engine struct by value (a stats struct of a
  few KB with arrays inside) copy owned arrays into the console's block:
  read the asset fields instead.

## Writing memory

- `write --expect` with the bytes you just read: heap memory can be reused
  between a read and a write.
- `search` twice a second apart and keep the intersection: heap addresses
  that churn are per-frame temporaries. Re-read the vtable right before
  every write; a block can be reused for another type in between.
- Never write through a pointer chain: resolve it, read, then write to that
  address with `--expect`.
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

## Trial writes

- **Use a trial to settle which way a lead goes** (does a multiplier
  shorten an action?). It writes once and the agent restores the original.
  Whether a trial needs approval follows the host's rules. A trial result
  is evidence for a lead's direction, not validation of a shipped feature.
- For an attribute behind a lookup handle, the owner is the effect
  component. Look the handle up with `call` (the result is kept), read its
  two pointers (object, control block), then `release` it. The attribute
  object has no vtable at `+0`; use the control block as `--object` (16
  bytes before the object, vtable in the game exe; base and current values
  at control block `+0x38`).
- **To time an action**, capture its Blueprint action object's state fields
  (frame counters, flags) during one untouched run and one trial run, then
  compare the times between matching state changes. A reload's notify
  sequence gave exact ratios.
- Interactive timing: start the capture, end the turn telling the user to
  act now, read the result when it completes.
- **A/B on an AI character.**
  1. Confirm the value sits still at rest: two reads a second apart.
  2. Capture the value and the action's progress over several untouched
     actions.
  3. Trial-write the value and capture the same ranges again.
  4. Compare durations on that one character, with an untouched one in the
     same capture as a control when one is in view.
- Read the end state with `trial-list`. Clear trials (`trial-clear`) before
  the host stops or you reinject: a stopped agent no longer ticks, so its
  trials never restore.

## Pins

- `pin` writes the same bytes every tick; `<ref>+off` targets stop when the
  object dies. A pinned value that holds is integrated state; one the game
  still overrides each frame is recomputed from scratch.
