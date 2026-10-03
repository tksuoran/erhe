# Animation Timeline / Curve Graph Editor (issue #243)

Status: in progress

This plan holds the outstanding work on the editor's Animation window
(`src/editor/animation/`), described in `doc/editor/editor.md` "Animation
window and playback"; the data model and playback semantics are in
`doc/erhe/scene.md` "Animation playback". The LightWave-style keyframing
workflow (timeline strip, DopeTrack, scene markers, autokey refinements) is
planned separately in `doc/plans/animation_keyframing.md`; this plan covers the
Blender-Graph-Editor-style curve and dope sheet work.

Reference: Blender's Graph Editor / Dope Sheet
(https://docs.blender.org/manual/en/latest/editors/graph_editor/introduction.html).
Borrow interaction conventions (channel tree with hide/lock, box select,
handle rendering, normalized display), not the data model: erhe keeps glTF's
sampler/channel form (STEP / LINEAR / CUBICSPLINE) so import and export stay
lossless.

## Starting point

- `Animation_player` (`animation/animation_player.{hpp,cpp}`) owns playback
  and is advanced from `Editor::tick()` (`editor.cpp`), independent of window
  visibility.
- `Animation_window` (`animation/animation_window.{hpp,cpp}`) is a single
  window with: animation combo, transport and keying toolbars, timeline strip,
  channel list (name filter, per-component visibility, All / None / Animated)
  and one curve canvas. The canvas pans / zooms, frames all (Home), click /
  Ctrl-click / box selects keys, drags keys in time and value, inserts keys
  (Ctrl+click near a curve) and deletes keys (Delete / X), each edit one undo
  entry (`operations/animation_edit_operation.*`). Curves are drawn per
  segment: STEP as stairs, LINEAR as polylines, CUBICSPLINE tessellated with
  a fixed `c_cubic_segment_steps` per key interval.
- Keyframe edit helpers live in the editor (`animation/animation_edit.hpp`:
  `insert_keyframe`, `delete_keyframe`, `set_keyframe_time`,
  `set_keyframe_value`, `ensure_channel`, `set_key_from_node`, sampler and
  whole-animation snapshots). They have no unit tests.
- MCP tools: `get_scene_animations` (per-channel summaries, key counts),
  `set_animation_target`, `animation_playback`, `animation_edit_keyframe`,
  `animation_create_key`, `animation_delete_key`.
- `erhe_scene_tests` (`src/erhe/scene/test/test_animation_sampler.cpp`,
  `test_animation_apply.cpp`) cover sampler evaluation and apply.

## Outstanding work

Each item is independently verifiable and commits separately.

### 1. Edit API in `erhe::scene` with unit tests

Move the pure sampler-edit helpers of `animation/animation_edit.{hpp,cpp}`
(everything that touches only `Animation` / `Animation_sampler` storage) into
`erhe::scene` as `Animation_sampler` / `Animation` member functions, so the
layout invariants (CUBICSPLINE triplets, component strides, strictly
increasing timestamps, channel cursor reset) live next to the data. Add
`erhe_scene_tests` cases: insert / delete / retime keeping sortedness and
triplet layout, value set per component, cursor reset after structural edits,
and the shared-sampler policy of item 2.

### 2. Shared-sampler policy

glTF allows several channels to reference one sampler; a value edit through
one channel then edits its siblings. Copy-on-first-edit: an edit that targets
a sampler referenced by more than one channel clones the sampler, points the
edited channel at the clone, then edits. The undo snapshot captures the
channel re-pointing (the whole-animation `Animation_state` already does).

### 3. Interpolation mode editing

- `set_interpolation_mode(mode)` converting the data layout: LINEAR / STEP to
  CUBICSPLINE grows each key to a triplet with tangents synthesized from
  finite differences; CUBICSPLINE to LINEAR / STEP drops the tangents.
- Per-sampler mode combo in the channel list row (the row already shows the
  mode and key count as text).
- MCP `animation_set_interpolation`; gtest conversion round-trips.

### 4. CUBICSPLINE tangent handles

- Curves tab: Blender-style handle lines + dots on selected CUBICSPLINE keys;
  dragging a handle edits the in / out tangent as one undo entry (same gesture
  pattern as key drags: snapshot on press, live edit, one operation on
  release).
- Tessellate cubic segments by screen-space length instead of the fixed step
  count.
- MCP `animation_set_tangents`; exit criterion: evaluation after a tangent
  edit matches the expected hexfloat value.

### 5. Dope Sheet tab

Split the canvas area into two tabs, Curves (the existing canvas) and Dope
Sheet: one row per visible channel plus per-node and per-animation summary
rows, keyframe diamonds, the shared time ruler and playhead, click / box
select, horizontal drag retimes selected keys (one undo entry), Delete
removes them. The selected-key set is shared between the two tabs. Coordinate
with item 5 of `doc/plans/animation_keyframing.md` (DopeTrack key
manipulation on the timeline strip) so both reuse one key-selection and
retime implementation.

### 6. Curve canvas conveniences

- Escape during a key drag restores the drag-start snapshot without pushing
  an operation.
- Normalized display (each curve scaled to [-1, 1], display only) for comparing
  channels of different magnitude.
- Display rate (default 30 fps) for ruler subticks, and snap-to-frame for key
  drags and scrubbing (toggle; Ctrl inverts while dragging). Time storage stays
  float seconds.
- Per-channel lock (edit protection) in the channel list.

### 7. Channel removal and full keyframe dumps

- Remove channel (channel list context menu), undoable through the
  `Animation_state` snapshot.
- MCP query returning every key of a channel (time, values, tangents) as
  hexfloat strings per the diagnostic float serialization rule, so scripted
  verification can compare keys bit-exactly.

### 8. Smoke test

`scripts/animation_smoke_test.py`, following the existing `*_smoke_test.py`
scripts: import a small animated test asset (one TRANSLATION / LINEAR, one
ROTATION / CUBICSPLINE and one STEP channel, authored with the MCP keying
tools or a tiny glTF writer committed next to the script), play / seek and
check node transforms, edit / insert / delete keys with undo / redo, save /
reload the scene and re-check, and capture a screenshot of the window.
Gesture ergonomics (dragging keys and handles, box select, snapping) are
checked interactively by the user on the windowed build.

## Out of scope

- Morph-target `WEIGHTS` channels (no morph-target rendering in erhe; the UI
  hides them).
- Several animations playing at once, blending, layers, NLA: the player plays
  one animation (`doc/erhe/scene_format_support.md`).
- Driver expressions, constraints, curve modifiers; IK is the rigging topic.
- Frame-integer time storage: glTF time is float seconds and stays canonical.
