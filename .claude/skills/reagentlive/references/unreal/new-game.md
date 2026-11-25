# A new Unreal game with no dump

Engine offsets: `EngineLayout` and `StockUE5` in
`src/reagentlive/packs/ue/engine_layout.h` (one member per offset, grouped
by profile group). What each group checks and which verbs it gates:
without an SDK (`docs/commands/unreal.md#without-an-sdk`).

## Try the verbs first

- Run `{"verb": "get", "args": {"object": "Vector", "mode": "types"}}`,
  `{"verb": "enum", "args": {"enum": "ENetRole"}}` and `funcs` on any object.
  If they answer, the stock layout holds: nothing below is needed.
- A refusal names the failed group check ("field: Vector members are not
  X, Y, Z"): start at that group (step 2) and find its offsets live.
- Record them as a profile (`ProfileJson`: `{"groups": {group: {member:
  "0x.."}}}`). The agent cannot load one yet
  (`docs/tiers.md#the-gap`): a group that differs from `StockUE5` stays
  off. Tell the user instead of working around a refused group.
- Reads answering is not the end: calls need the game thread (the
  game-thread reference).

## A static tool is optional

Live reads settle every group except `call`'s frame offsets, which only
code shows. Use one for a modified engine, or to confirm the `FFrame`
offsets (decompile its constructor and `UFunction::Invoke`). Never wait
for it: a large exe takes hours to analyse, and opening a database the
user's tool is still analysing gets in their way; ask first.

## Rules

- **A candidate counts only when every sample agrees:** at least two known
  objects per member, from different classes.
- **More than one offset fits: stop.** Add samples until one is left.
- **Names may be hashed.** Confirm by structure instead (counts, sizes,
  offsets, cast flags, pointer targets).
- **Read, don't call.** Everything before the `call` group is reads. The
  first call is the known-answer call; it needs the user's approval
  (SKILL.md, Approval) even through `call-pure`.
- **A group you can't settle stays out of the profile.** A missing group
  turns its verbs off; a wrong one returns garbage, or crashes for `call`.
- Query each comparison index with its own `name` command: a garbage index
  can misalign the other names in the same reply.

## 0. Prepare

- Inject an SDK-free host: `examples/unreal`, or any host linking
  `reagentlive::ue-base`. Run `status`. The first Unreal verb locates the
  object table and name pool (`docs/commands/unreal.md#before-the-first-command`).
- Note the module and both globals' RVAs.
- `find` with `text` `Class`, `Struct`, `Field` and `Object`
  (`{"verb": "find", "args": {"text": "Class"}}`): the core classes every
  recipe starts from. `describe` each.

## 1. Engine version and variants

- Strings in the exe: `Unreal Engine 5.x.y`, `++UE5+Release-5.x`, engine
  source paths in asserts. Often UTF-16: search both.
- What the version tells you:
  - 4.25 and later use `FField`/`FProperty`. Earlier: `UProperty`, not
    covered here.
  - Before 5.6 enum names are a `{FName, int64}` pair array; from 5.6 two
    arrays and a count.
  - `FName` is 8 bytes unless built with case-preserving names (16).
  - Hashed names: a pool entry with an 8-byte hash prefix. The pack reads
    both pool layouts; `name` decoding shows which one this game uses.

## 2. Groups

Per group, in dependency order: the layout-groups reference.

## 3. Finish

- Write the profile beside the host (`<game>.layout.json`), even when every
  group matched `StockUE5`.
- Compare every group again on a second set of objects (after a level
  change): a value that moved is not an engine offset.
- Record which groups are in, which are out, and why.
