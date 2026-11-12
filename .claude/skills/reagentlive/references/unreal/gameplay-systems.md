# Gameplay systems: tags, attributes, data, collision

## Finding what exists

- "What exists in this world?": `census --cls Actor` before guessing class
  names; zero live instances means a lead can't be tested now.
- "What spawns when I do X?": `census --cls <Base>`, the user acts,
  `census-diff --cls <Base>`. Pick a base class (`Actor`,
  `ActorComponent`, a game base) to keep engine churn out; the changed rows'
  samples are only new for classes absent before (else `find --cls`).
  Objects living a tick or two fall between censuses: use `watch` or
  `each --seconds` for those.
- "Which instance has state X?": `each <Class> <Owner:PureGetter>`; add
  `--seconds N` while the user acts to see which rows flip.

## Gameplay tags

- A tag question ("does this actor have state X?") has three sources to
  cross-check: a native predicate on the actor (`funcs <ref> <word>`), the
  tag count getter on its effect or ability component (tag FName index in the
  first 4 bytes of the block), and the geometry or data that grants the tag.
- Tag text to index: `name-find <part>` lists the whole family
  (`...Zone.Alpha/Bravo/Hostile`); `name` goes the other way.
- Tag counts have layers (total, effects, local, replication): read each
  source with the tag-count getter before trying to clear a tag. A direct
  count setter usually writes only the local layer.
- "Why can't I do X?" (sprint, aim): find the action's definition asset
  (`name-find` the action's name) and decode its response map; a blocking
  states container lists the state tags that refuse it. Then trace which
  effect grants that tag.

## Attributes and effects

- "Why did my stat edit do nothing?": look for a gameplay-attribute system
  first. Decode a designer effect's modifier array (its default object) to
  see which attribute tags the game really multiplies; `name-find
  Attribute.` lists the family.
- A tag-to-float map on an effect or attribute component can be the
  character's attribute list: raw stride 20 (8-byte tag, 4-byte value, 8 hash
  bytes). Read live base and current values with the attribute library's own
  getter on a lookup handle.
- Attributes live per owner. Character attributes sit in the pawn's effect
  component. An item can have its own effect component in its components
  array, but its tag-to-float map can be empty, so a raw `read` of the map
  data fails.
- Functions declared on a native interface are not on the implementing class
  chain. Use `func <object> <Interface>:<Fn>` to reach their implementation;
  `call` refuses them (see the native code reference).
- Getters that return a large engine struct by value (a full stats struct of
  a few KB with arrays inside) copy owned arrays into the console's block.
  Avoid calling them; read the asset fields instead.

## Data and maps

- Read a reflected map or set with `entries <obj> <Field>` (`dump` previews
  the first eight). Cross-check keys against an engine getter and values
  against a second live instance when available. If `fields` shows no
  `stride` for it, the layout failed its check: decode the header by hand
  (sparse-array allocation bits and occupied count).
- Obfuscated enum value names are missing from the name pool; read the enum
  in the SDK headers instead.

## Collision and traces

- Collision probes: freeze one start/end pair, vary one trace input, and pass
  the returned hit bytes through the engine's hit decoder before comparing
  component identity, material, surface and distance.
- A collision-channel mismatch: decode the live collision profile and check
  the hit component's response to each channel with its getter. Object
  channels and trace-query indices are separate. A profile on a projectile
  actor only establishes that actor's configuration.
