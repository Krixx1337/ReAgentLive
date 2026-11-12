# Engine calls, writes and pins

Every `write`, `pin` and state-changing `call` needs the user's approval
first (see the skill's rules).

## Calling functions

- Engine calls: `func` for the UFunction, `fields <its address>` (or
  `params`) for the parameter layout, then a hex block of exactly that size;
  outputs come back in the same block. A getter with only outputs needs no
  block: `call <obj> <Fn>` sends zeros of the right size.
- Named getter calls: discover with `funcs` and copy exact input names from
  `params`. To pass an FName whose text appears in a dump, read its
  reflected eight bytes to recover the index and instance number.
- Static library functions: `call` them on the library's `Default__` object.
- To cross-check a typed getter against a raw call, compare the complete
  `result.hex` blocks on a stable target before relying on decoded output.
- A getter that returns a container leaves heap pointers in the result block;
  read them in the next command and decode with `name`.
- **Owned results (weak or shared handles, strings).** When a call's output
  owns memory, the result shows `kept: true` and `kept_reason` names the
  output. The agent reuses that block, so repeated calls of the same
  function no longer leak a reference each.
  - When finished, `release <object> <Fn>` once; it calls with zeroed inputs
    and drops the block.
  - At most 8 functions can be kept at once. A ninth is refused until you
    `release` one.
  - Verify on the handle's control block with `read` (+8 shared, +12 weak
    count): the count stays flat across repeats and drops back after
    `release`.
  - An older agent without this support returns `kept: false` on every call;
    pass the previous result hex back as the return slot by hand there.

## Writing memory

- Use `write --expect` with the bytes you just read: heap memory can be
  reused between a read and a write.
- `search` twice a second apart and keep the intersection: heap addresses
  that churn are per-frame temporaries. Re-read the vtable right before every
  write; a block can be reused for another type between the search and the
  write.
- When a game setter refuses (authority, validation), look at the object it
  writes: native value objects often keep plain fields that the game reads
  directly. Verify the layout against the game's own getter right before
  each write.
- A value that snaps back within one tick after a write is a copy (Unreal
  Mover components keep a copy of Network Prediction state). Find the source
  before trying per-tick writes.
- Class or tier tables in data assets (thresholds, ranges) are often re-read
  only on an event (inventory change, equip): after a write, ask the user to
  trigger that event before judging it.
- **Writes can be blocked.** Even with the user's go-ahead, the session's
  permission check may refuse a state-changing `call`. Plan read-only
  evidence (getters, refcounts, designer data) that stands without the write,
  and leave the effect check to the user's session.

## Trial writes

- **Use a trial to settle which way a lead goes**, for example whether a
  multiplier shortens an action:
  `trial <addr> <hex> --owner <ref> --expect <hex now> [--object <native base>] --seconds N`.
  - It writes once and the agent restores the original by itself.
  - Python: `s.trial(...)`, `s.trial_list()`, `s.trial_clear()`.
  - Whether a trial needs approval follows the host's rules. A trial result
    is evidence for a lead's direction, not validation of a shipped feature.
- For an attribute behind a lookup handle, the owner is the effect component.
  Look the handle up with `call` (the result is kept), read its two pointers
  (object, control block), then `release` it.
  - The attribute object has no vtable at `+0`.
  - Use the control block as `--object`: 16 bytes before the object, with a
    vtable in the game exe. The base and current values sit at control block
    `+0x38`.
- **To time an action**, `watch` its Blueprint action object's state fields
  (frame counters, flags) during one untouched run and one trial run, then
  compare the times between matching state changes. A reload's notify
  sequence (`LastAnimFrame`, `bMagOutExecuted`, `bRechamberComplete`) gave
  exact ratios.
- Interactive timing: start the capture in the background, end the turn
  telling the user to act now, and read the result when it completes.
- **A/B on an AI character.**
  1. Confirm the value sits still at rest: two reads a second apart.
  2. `watch` the value and the action's progress over several untouched
     actions.
  3. Trial-write the value and `watch` the same ranges again.
  4. Compare durations on that one character. AI characters act when they
     choose, so compare a character with itself, adding an untouched one to
     the same `watch` as a control when one is in view.
- Read the end state with `trial-list`:
  - `restored`: put back;
  - `conflict`: the game changed the value, so it was left alone;
  - `abandoned`: the owner or vtable changed, so nothing was written.

  Clear trials (`trial-clear`) before the host stops its overlay or
  you reinject: a stopped agent no longer ticks, so its trials never restore.

## Pins

- `pin` writes the same bytes every tick; `$N+off` targets stop when the
  object dies. A pinned value that holds is integrated state; one the game
  still overrides each frame is recomputed from scratch.
