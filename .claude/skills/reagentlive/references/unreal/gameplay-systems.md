# Gameplay systems: tags, attributes, data, collision, replication

## Finding what exists

- "What exists in this world?": `{"verb": "census", "args": {"cls": "Actor"}}`
  before guessing class names; zero live instances means a lead can't be tested now.
- "What spawns when I do X?": `{"verb": "census", "args": {"cls": "<Base>"}}`,
  the user acts, `{"verb": "census", "args": {"cls": "<Base>", "diff": true}}`.
  Pick a base class (`Actor`, `ActorComponent`, a game base) to keep engine
  churn out; the changed rows' samples are only new for classes absent
  before (else `find` with `cls`). Objects living a tick or two fall between
  censuses: capture them, or `each` with `seconds`.
- "Which instance has state X?":
  `{"verb": "call-each", "args": {"cls": "<Class>", "function": "<Owner:PureGetter>"}}`;
  add `seconds` while the user acts to see which rows flip.

## Gameplay tags

- A tag question ("does this actor have state X?") has three sources to
  cross-check: a native predicate on the actor
  (`{"verb": "funcs", "args": {"object": "0x1f2e3d4c50#123:4", "view": {"match": "<word>"}}}`), the
  tag count getter on its effect or ability component (tag FName index in
  the first 4 bytes of the block), and the geometry or data that grants the
  tag.
- Tag text to index: `{"verb": "name-find", "args": {"text": "<part>"}}`
  lists the whole family
  (`...Zone.Alpha/Bravo/Hostile`); `name` goes the other way.
- Tag names inside struct arrays (a nested array shows only its header):
  dump `Default__GameplayTagsSettings` and inspect its tag redirects or tag
  rows.
- Tag counts have layers (total, effects, local, replication): read each
  source with the tag-count getter before trying to clear a tag. A direct
  count setter usually writes only the local layer.
- "Why can't I do X?" (sprint, aim): find the action's definition asset
  (`name-find` the action's name) and decode its response map; a blocking
  states container lists the state tags that refuse it. Then find which
  effect grants that tag.
- Which native code sends a tagged event (an impact or a cue): a tag the
  game defines in C++ leads there.
  `{"verb": "search", "args": {"type": "text", "value": "<Tag.Name>", "scope": {"module": "<exe>"}}}`,
  `xrefs` the string's start: one `lea` in a start-up function that
  registers it; `disasm` there: the `lea rcx, [rip+…]` before the
  registration call is the tag's global.
  `{"verb": "xrefs", "args": {"addr": "<global>"}}`: its `load` rows
  are the code that uses the tag. Try this before `touches` when the
  system raises tagged events or cues.

## Attributes and effects

- "Why did my stat edit do nothing?": look for a gameplay-attribute system
  first. Decode a designer effect's modifier array (its default object) to
  see which attribute tags the game really multiplies;
  `{"verb": "name-find", "args": {"text": "Attribute."}}` lists the family.
- A tag-to-float map on an effect or attribute component can be the
  character's attribute list: raw stride 20 (8-byte tag, 4-byte value, 8
  hash bytes). Read live base and current values with the attribute
  library's own getter on a lookup handle.
- Attributes live per owner. Character attributes sit in the pawn's effect
  component. An item can have its own effect component, but its
  tag-to-float map can be empty, so a raw `read` of the map data fails.

## Data and maps

- A reflected map or set:
  `{"verb": "entries", "args": {"object": "0x1f2e3d4c50#123:4", "field": "<Field>"}}`.
  Cross-check keys against an engine getter and values against a second
  live instance. If `get` with `mode` `types` shows no `stride` for it, the layout failed its check: decode the header
  by hand (sparse-array allocation bits and occupied count).
- A data table's rows:
  `{"verb": "rows", "args": {"object": "0x1f2e3d4c50#123:4", "view": {"match": "<row>", "fields": [["a"], ["b", "c"]]}}}`.
- A curve table (`CT_...` balance curves): `rows` with `view.match` for the
  keys, `at` set to a level for each curve's value there; never decode
  `RowMap` or curve keys by hand.
- A byte or enum field prints a number:
  `{"verb": "enum", "args": {"enum": "<EnumName>"}}`. Obfuscated enum
  value names are missing from the name pool (`#index`); read the enum in
  the SDK headers if there is a dump.

## Collision and traces

- "What blocks this line?": `trace` before guessing from geometry or
  rebuilding a host:
  `{"verb": "trace", "args": {"start": "<camera manager ref>", "end": [0, 0, 0], "ignore": "<pawn ref>"}}`.
  Ends are points, objects or a mesh component and socket; `ignore` with
  your pawn keeps your own body out of a camera line. A
  collision shell or a material the game treats specially (glass, foliage)
  shows up at once.
- Collision probes: freeze one start/end pair, vary one trace input, and
  pass the returned hit bytes through the engine's hit decoder before
  comparing component identity, material, surface and distance.
- A collision-channel mismatch: decode the live collision profile and check
  the hit component's response to each channel with its getter. Object
  channels and trace-query indices are separate. A profile on a projectile
  actor only establishes that actor's configuration.

## Replication on a client

- "It exists on the server, so why not here?": on a client (`Role` 1 on the
  actor, simulated proxy) an actor arrives only while it is net relevant.
  Read the class default object: `bAlwaysRelevant`, `bOnlyRelevantToOwner`
  and `NetCullDistanceSquared` (cm²; 1e10 is 1 km). A culled actor's slot in
  an owner's array reads null, not stale.
- Look for an always-relevant owner or proxy (a placement, plot or marker
  actor) that carries position and side at any range.
- Lists named `Auth_...` and server-only subsystem state are empty on a
  client; an empty list there is not evidence of nothing.
- Replicated state of other players often sits in a component's array
  (`{"verb": "each", "args": {"cls": "<Component>", "paths": [["<Array>"]]}}`):
  one table, cheaper than a getter per actor.
- Before trusting a design doc's formula, run `script` with `decode` on the
  Blueprint function that applies it and read the formula from its statements.
- One asset's subobjects (the bodies of one physics asset, the parts of one
  mesh): `{"verb": "each", "args": {"cls": "<Class>", "paths": [["<Field>"]], "outer": "<asset>"}}`,
  not the whole class.
