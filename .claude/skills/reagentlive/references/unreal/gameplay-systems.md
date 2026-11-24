# Gameplay systems: tags, attributes, data, collision, replication

## Finding what exists

- "What exists in this world?": `{"verb": "census", "args": {"cls": "Actor"}}`
  before guessing class names; zero live instances means a lead can't be tested now.
- "What spawns when I do X?": `{"verb": "census", "args": {"cls": "ActorComponent"}}`,
  the user acts, `{"verb": "census", "args": {"cls": "ActorComponent", "diff": true}}`.
  Pick a base class to keep engine churn out; changed rows' samples are
  only new for classes absent before (else `find` with `cls`). Objects
  living a tick or two fall between censuses: capture them, or `each` with
  `seconds`.
- "Which instance has state X?":
  `{"verb": "call-each", "args": {"cls": "Pawn", "function": "Pawn:IsControlled"}}`;
  add `seconds` while the user acts to see which rows flip.

## Gameplay tags

- A tag question ("does this actor have state X?") has three sources to
  cross-check: a native predicate on the actor
  (`{"verb": "funcs", "args": {"object": "0x1f2e3d4c50#123:4", "view": {"match": "<word>"}}}`), the
  tag count getter on its effect or ability component, and the geometry or
  data that grants the tag.
- Tag text to index: `{"verb": "name-find", "args": {"text": "<part>"}}`
  lists the whole family; `name` goes the other way. Tag names inside
  struct arrays: dump `Default__GameplayTagsSettings` and inspect its
  redirects or rows.
- Tag counts have layers (total, effects, local, replication): read each
  source with the count getter before trying to clear a tag. A direct
  count setter usually writes only the local layer.
- "Why can't I do X?": find the action's definition asset (`name-find` its
  name) and decode its response map; a blocking states container lists the
  tags that refuse it. Then find which effect grants that tag.
- Which native code sends a tagged event: a tag the game defines in C++
  leads there.
  `{"verb": "search", "args": {"type": "text", "value": "<Tag.Name>", "scope": {"module": "<exe>"}}}`,
  `xrefs` the string's start: one `lea` in a start-up function that
  registers it; `disasm` there: the `lea rcx, [rip+…]` before the
  registration call is the tag's global.
  `{"verb": "xrefs", "args": {"addr": "0x7ff6a1b2c3d0"}}`: its `load` rows
  are the code that uses the tag. Try this before `touches` when the
  system raises tagged events or cues.

## Attributes and effects

- "Why did my stat edit do nothing?": look for a gameplay-attribute system
  first. Decode a designer effect's modifier array (its default object) to
  see which attribute tags the game really multiplies;
  `{"verb": "name-find", "args": {"text": "Attribute."}}` lists the family.
- A tag-to-float map on an effect or attribute component can be the
  character's attribute list (read its layout via `get` with `mode`
  `types`). Read live base and current values with the attribute library's
  own getter on a lookup handle.
- **An attribute behind a lookup handle** (for `write` with `undo`): the
  owner is the effect component. Look the handle up with `call` (the
  result is kept), read its two pointers (object, control block), then
  `release` it. The attribute object has no vtable at `+0`; use the
  control block as `undo.object`, and find the base and current values by
  `read` against the library getter.
- Attributes live per owner: character attributes in the pawn's effect
  component; an item's own component may have an empty map.

## Data and maps

- A reflected map or set:
  `{"verb": "entries", "args": {"object": "0x1f2e3d4c50#123:4", "field": "<Field>"}}`.
  Cross-check keys against an engine getter and values against a second
  live instance. No `stride` in `get` with `mode` `types`: the layout
  failed its check; decode the header by hand.
- A data table's rows:
  `{"verb": "rows", "args": {"object": "0x1f2e3d4c50#123:4", "view": {"match": "<row>", "fields": [["a"], ["b", "c"]]}}}`.
  A curve table: `rows` with `view.match` and `at` set to a level for each
  curve's value; never decode `RowMap` or curve keys by hand.
- A byte or enum field prints a number:
  `{"verb": "enum", "args": {"enum": "EMovementMode"}}`. Obfuscated value
  names are missing from the name pool (`#index`); read the SDK headers if
  there is a dump.

## Collision and traces

- "What blocks this line?": `line-trace` before guessing from geometry:
  `{"verb": "line-trace", "args": {"start": "0x1f2e3d4c50#123:4", "end": [0, 0, 0], "ignore": "0x1f2e3d5d60#456:2"}}`.
  Ends are points, objects or a mesh component and socket; `ignore` your
  pawn to keep your own body out of a camera line. Special collision shells
  or materials (glass, foliage) show up at once.
- Probing: freeze one start/end pair, vary one input, and decode the hit
  bytes with the engine's hit decoder before comparing component, material
  and distance.
- A channel mismatch: decode the live collision profile and check the hit
  component's response to each channel with its getter. Object channels
  and trace-query indices are separate.

## Replication on a client

- "It exists on the server, so why not here?": on a client an actor
  arrives only while net relevant. Read the class default object:
  `bAlwaysRelevant`, `bOnlyRelevantToOwner`, `NetCullDistanceSquared`
  (cm²). A culled actor's slot in an owner's array reads null, not stale.
- Look for an always-relevant owner or proxy that carries position and side
  at any range. `Auth_...` lists and server-only state are empty on a
  client: not evidence of nothing.
- Replicated state of other players often sits in a component's array
  (`{"verb": "each", "args": {"cls": "StateComponent", "paths": [["Entries"]]}}`):
  one table, cheaper than a getter per actor.
- One asset's subobjects (a physics asset's bodies, one mesh's parts):
  `{"verb": "each", "args": {"cls": "BodySetup", "paths": [["AggGeom"]], "outer": "0x1f2e3d4c50#123:4"}}`,
  not the whole class.
