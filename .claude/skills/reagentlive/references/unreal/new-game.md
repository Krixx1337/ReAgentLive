# A new Unreal game with no dump

Engine offsets: `EngineLayout` and `StockUE5` in
`src/reagentlive/packs/ue/engine_layout.h` (one member per offset, grouped
by profile group). What each group checks and which verbs it gates:
without an SDK (`docs/commands/unreal.md#without-an-sdk`).

## Try the verbs first

- Run `fields Vector`, `enum ENetRole` and `funcs` on any object. If they
  answer, the stock layout holds: nothing below is needed.
- A refusal names the failed group check ("field: Vector members are not
  X, Y, Z"): start at that group (step 2) and find its offsets live.
- Record them as a profile (`ProfileJson`: `{"groups": {group: {member:
  "0x.."}}}`).
- **The agent cannot load a profile yet.** A group that differs from
  `StockUE5` stays off; the profile is the evidence, and the input for a
  host that builds its own layout. Tell the user instead of working around
  a refused group.
- Reads answering is not the end: calls need the game thread (the
  game-thread reference).

## A static tool is optional

Live reads settle every group except `call`'s frame offsets (`FFrame`
`Node` and `Locals`), which only code shows without making a call. The
ProcessEvent slot and the engine version come from live `vtable`,
`disasm` and the exe file's strings.

- **Not needed** for a stock game, or for any group short of `call`.
- **Worth it** for a modified engine, and to confirm the `FFrame` offsets
  before calls: decompile the `FFrame` constructor and `UFunction::Invoke`.
- **Never wait for it.** A large exe takes hours to analyse; start it in
  the background and do the live groups meanwhile. Opening a database while
  the user's tool is still analysing it gets in their way: ask first.

## Rules

- **A candidate counts only when every sample agrees.** At least two known
  objects per member, from different classes. One that disagrees: the
  candidate is wrong, or that sample is.
- **More than one offset fits: stop.** Add samples until one is left. Don't
  pick the first.
- **Names may be hashed.** Many shipping games hash cooked `FProperty`
  names. Hash-like member names: confirm by structure instead (counts,
  sizes, offsets, cast flags, pointer targets).
- **Read, don't call.** Everything before the `call` group is reads. The
  first call is the known-answer call; it needs the user's approval
  (SKILL.md rules) even through `call-pure`.
- **A group you can't settle stays out of the profile.** A missing group
  turns its verbs off. A wrong one returns garbage, or crashes for `call`.

## 0. Prepare

- Inject an SDK-free host: `examples/unreal`, or any host linking
  `reagentlive::ue-base`. Run `status`. The first Unreal verb locates the
  object table and name pool (before the first command: `docs/commands/unreal.md#before-the-first-command`).
- Note the module and both globals' RVAs (label them in the static tool if
  one is in use).
- `find Class`, `find Struct`, `find Field`, `find Object`: the core
  classes every recipe starts from. `describe` each.

## 1. Engine version and variants

- Strings in the exe, no static tool needed: `Unreal Engine 5.x.y`,
  `++UE5+Release-5.x`, engine source paths in asserts
  (`Runtime/CoreUObject/...`), the build string. Often UTF-16: search both.
- What the version tells you:
  - 4.25 and later use `FField`/`FProperty`. Earlier: `UProperty`, not
    covered here.
  - Before 5.6 enum names are a `{FName, int64}` pair array; from 5.6 two
    arrays and a count.
  - `FName` is 8 bytes unless built with case-preserving names (16): the
    `nameNumber` offset is 4 with 8-byte names.
  - Hashed names: a pool entry with an 8-byte hash prefix. The pack reads
    both pool layouts; `name` decoding shows which one this game uses.

## 2. Groups

Per group, in dependency order: the layout-groups reference.

## Reading names while probing

Query each comparison index with its own `name` command. A garbage index
(a pointer or flag word read as an index) can misalign the other names in
the same `name` reply.

## 3. Finish

- Write the profile beside the host (`<game>.layout.json`), even when every
  group matched `StockUE5`: it records what was checked.
- Compare every group again on a second set of objects (after a level
  change). A value that moved is not an engine offset.
- Record which groups are in, which are out, and why.
