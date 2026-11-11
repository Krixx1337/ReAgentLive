# Animation, poses and camera

## Finding the right object

- First-person pose questions: the arms mesh's anim instance links
  per-weapon layers; blueprint anim variables keep readable names, so `dump`
  the layer and search its rows (`dump --match`).
  `GetLinkedAnimLayerInstanceByClass(baseClass, true)` finds the held
  weapon's layer.
- To find which sequence node plays a clip, read each player's `Sequence`
  pointer and `describe` it; clip names (`..._In_..._Settle`) say what they
  do.

## Measuring what the user sees

- "The animation is still slow": sample a bone (`GetSocketTransform`,
  component space) against the camera bone. It separates the main move from
  a settle and gives the exact duration to hunt for.
- A slow aim-in can be two separate things: the gameplay progress value and
  the visual pose catching up. Sample the progress getter first; if it alone
  takes about the whole time, the cause is the progress, not the pose.
- Lean: sample camera translation as well as rotation, and compare 10-90%
  crossings with the character modifier's progress in the same tick. A
  visible lean can show no roll at all.

## Anim graph nodes

- Before snapping a few anim nodes, scan them all read-only: list every
  node's alpha source, bool blend times and interp speeds from one raw
  snapshot plus the SDK offsets, and cross with a hip-vs-aim diff. A node
  that differs between two states may smooth something else too (look sway):
  snapping it can make the gun shake. Ship such snaps behind a session-only
  testing switch first.
- Node alphas that ease have `bInterpResult` set in their
  `AlphaScaleBiasClamp`; the interpolated value hides in the struct's
  trailing padding.
- "Where does this anim-graph input come from?": `__CustomProperty_<Pin>_<GUID>`
  fields feed a node (often a Control Rig). `find <GUID>` gives its
  `EvaluateGraphExposedInputs` function. Its bytecode (`UStruct` script array
  at +0x60) calls the ubergraph at an entry offset (`0x46 <func> 0x1d
  <int32>`). Read the ubergraph bytecode there and resolve the pointer
  operands: `FField` name at +0x20, `FProperty` offset at +0x44; `01` =
  instance variable, `00` = local, `0f` = assign.
