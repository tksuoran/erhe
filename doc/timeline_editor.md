# Animation Timeline / Curve Graph Editor (issue #243)

Plan for implementing an animation timeline (dope sheet) and curve (graph)
editor in the erhe editor, compatible with animations imported from glTF
assets. Modeled on Blender's Graph Editor / Dope Sheet
(https://docs.blender.org/manual/en/latest/editors/graph_editor/introduction.html).
A Blender clone is available at `D:\blender` for reference (see
"Blender reference material" below).

Status: PLANNED, not implemented. This document is the design + phase plan to
be executed later.

## Table of Contents

1. [Goal and scope](#goal-and-scope)
2. [Existing erhe infrastructure](#existing-erhe-infrastructure)
3. [Known defects in existing code](#known-defects-in-existing-code)
4. [Gap analysis](#gap-analysis)
5. [Design decisions](#design-decisions)
6. [Implementation plan (phases)](#implementation-plan-phases)
7. [Verification plan](#verification-plan)
8. [Blender reference material](#blender-reference-material)
9. [Key files reference](#key-files-reference)
10. [Out of scope / future work](#out-of-scope--future-work)

---

## Goal and scope

Deliver, inside the erhe editor:

- A **timeline / dope sheet view**: channel rows with keyframe diamonds,
  a time ruler, a playhead, transport controls, keyframe retiming.
- A **curve (graph) editor view**: per-component curves plotted
  value-over-time, pan/zoom canvas, keyframe selection and editing
  (move/insert/delete), tangent handles for cubic-spline keys,
  interpolation-mode editing.
- **Playback that actually works**: a per-frame animation player owned by the
  editor tick (not by the Properties window as today), driven by editor time,
  with play/pause/loop/speed/seek, applying sampled values to scene nodes.
- **glTF compatibility both ways**: animations imported from glTF assets are
  viewable/editable, and edited animations survive scene save/load and glTF
  export (both currently drop them).
- **Undo/redo** for every edit, **MCP tools** for scripted verification, and a
  smoke-test script following the geometry-nodes precedent.

Explicitly out of scope for the initial implementation (see
[Out of scope](#out-of-scope--future-work)): morph-target `WEIGHTS` channels,
animation blending/layering/NLA, IK/constraints, driver expressions.

## Existing erhe infrastructure

A surprising amount already exists. Inventory (verified 2026-07-04):

### Data model: `erhe::scene::Animation`

`src/erhe/scene/erhe_scene/animation.{hpp,cpp}` -- glTF-faithful:

- `Animation_sampler`: `interpolation_mode` (STEP / LINEAR / CUBICSPLINE),
  flat `std::vector<float> timestamps`, flat `std::vector<float> data`.
  CUBICSPLINE stores per-key triplets (in-tangent, value, out-tangent), each
  `component_count` floats wide, exactly as glTF does.
- `Animation_channel`: `path` (TRANSLATION / ROTATION / SCALE / WEIGHTS),
  `sampler_index` into the owning `Animation`, `target` node
  (`shared_ptr<erhe::scene::Node>`), `start_position` (per-channel seek
  cursor, amortizes sequential playback), `value_offset` (float offset into
  shared sampler data; nonzero only for CUBICSPLINE where a channel reads the
  value out of the triplet).
- `Animation` is an `erhe::Item` (`Item_type::animation`,
  `static_type_name "Animation"`): named, selectable, shows in the UI.
  Public API: `evaluate(time, channel_index, component)` (single float,
  used by the curve widget), `apply(time)` (writes
  `parent_from_node.set_translation/rotation/scale` on targets),
  `get_first_time()` / `get_last_time()`.

### glTF import / export

- Import (`src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp`, `parse_animation`):
  full support for all three interpolation modes and all four paths;
  animations land in `Gltf_data::animations`.
- Editor-side import (`src/editor/parsers/gltf.cpp`): each animation is
  attached to `Content_library::animations` via an undoable
  `Content_library_attach_operation`, carrying a `Gltf_source_reference`
  (path / name / index / type) for provenance.
- Export (`erhe::gltf::export_gltf`, same file, `Gltf_exporter`): exports
  nodes, meshes, materials, physics -- **but not animations**. This is a gap
  this plan closes (Phase 5).

### Scene save / load

(SINCE LANDED, differently than planned: the glTF scene roundtrip work made
the scene file a single erhe-authored `.glb` and `export_gltf` now exports
every content-library animation, so animations persist with the scene -
`doc/scene_serialization.md`. The paragraph below describes the state when
this plan was written.)

`src/editor/scene/scene_serialization.cpp` persisted meshes/materials through
a companion `.glb` (written with `export_gltf`) plus `Gltf_source_reference`
entries. Animations were not serialized at all -- an imported animation
existed only for the session.

### Playback plumbing (currently a placeholder hack)

- `Timeline_window` (`src/editor/animation/timeline_window.{hpp,cpp}`):
  a working transport bar -- play/pause, seek to start/end, loop toggle,
  logarithmic speed slider with direction toggle, position slider. Registered
  in `editor.cpp` and reachable via `App_context::timeline_window`. It clocks
  itself with `std::chrono::steady_clock` from inside `imgui()` (so playback
  advances only while the window is visible) and knows nothing about which
  animation is playing.
- The actual "apply animation to scene" call lives in
  `Properties::animation_properties()`
  (`src/editor/windows/properties.cpp`): only when an `Animation` item is
  selected AND the Properties window is rendering does the editor set the
  timeline length, poll the play position, call `animation.apply(...)`, send
  `Animation_update_message`, and call `scene->update_node_transforms()`.
  Deselect the animation and playback silently stops. This wiring is
  explicitly temporary and is removed in Phase 0.
- `Animation_update_message` exists on `App_message_bus`; `Transform_tool`
  subscribes (refreshes its visualization).

### Curve display widget (read-only prototype)

`editor::animation_curve(Animation&)`
(`src/editor/animation/animation_curve.{hpp,cpp}`), embedded as a Properties
row: channel + component combos, pan (RMB-drag) / zoom (wheel, MMB-drag)
canvas, adaptive grid, curve polyline (uniform 100-step resampling of
`Animation::evaluate`), keyframe squares, value tooltip. All view state is in
function-local `static`s (shared across every animation and window instance).
It is the seed of the curve editor canvas; Phase 1 promotes it into a class.

### Empty placeholders

`src/editor/animation/animation_window.{hpp,cpp}` exist, are empty (1 line),
and are already listed in `src/editor/CMakeLists.txt`. The new editor window
goes there.

### Editor services to build on

- `Time` (`src/editor/time.{hpp,cpp}`): frame timing, fixed-step updates,
  and the editor tick already has a per-frame animation-ish hook
  (`m_time->update_transform_animations(...)` in `editor.cpp` -- that one is
  the unrelated camera-tween system, but the new player update slots in right
  next to it).
- `Operation_stack` + `Compound_operation` for undo/redo; the geometry-graph
  parameter-gesture pattern (capture before/after state on widget
  activate/release) is the precedent for drag edits.
- In-editor MCP server (`src/editor/mcp/`): established pattern for adding
  query/action tools and driving headless verification;
  `scripts/geometry_nodes_smoke_test.py` is the smoke-test template.
- `Item_type::animation` filtering, content-library selection, and the
  Properties dispatch (`get<erhe::scene::Animation>(selected_items)`) already
  route an animation selection to UI code.

## Known defects in existing code

Fix these as part of Phase 0 (they are all in code the new editor builds on):

1. **STEP interpolation evaluates as LINEAR.**
   `Animation_sampler::evaluate` branches only on
   `interpolation_mode != CUBICSPLINE`, so STEP samplers lerp between keys
   instead of holding the previous key's value.
2. **CUBICSPLINE rotation result is not normalized.**
   `rotation_value_normalized` is computed and then the *unnormalized* quat is
   returned (`animation.cpp` ROTATION cubic branch).
3. **`Properties::animation_properties` dereferences
   `animation.channels.front()`** without an empty-channels guard, and
   `animation_curve()` indexes `channels.at(channel_index)` with a `static`
   index that can be stale from a previously viewed animation (out-of-range
   `at()` throws).
4. **Playback clock is wall-clock inside `imgui()`** -- advances only while
   the Timeline window is visible, ignores the editor `Time` pause semantics.
5. **Curve view state is global `static`s** -- pan/zoom/channel selection leak
   across animations and would break a second window instance.
6. **Curve resampling is uniform 100 steps across the visible range** --
   misses STEP discontinuities and undersamples dense key regions; replace
   with per-segment (between adjacent keys) sampling.

## Gap analysis

| Capability | Blender | erhe today | Plan |
|---|---|---|---|
| Keyframed value evaluation | F-curves (bezier, many easings) | glTF STEP/LINEAR/CUBICSPLINE (STEP broken) | Fix STEP; keep the glTF trio as the data model (round-trips exports) |
| Playback | Global scene clock, all editors sync | Only while an Animation is selected in Properties | `Animation_player` in the editor tick (Phase 0) |
| Dope sheet | Dedicated editor, channel rows + key diamonds | Nothing | Phase 2 |
| Curve editor | Graph Editor: pan/zoom, normalize, handles, box select | Read-only prototype widget | Phases 1, 3, 4 |
| Keyframe editing | Insert/delete/move/scale, handle types | Nothing (data is read-only) | Sampler edit API + operations (Phase 3) |
| Undo | Full | n/a | `Operation_stack` ops for every edit (Phase 3) |
| Persistence | .blend | Animations dropped by scene save AND glTF export | Animation export in `export_gltf` + scene round-trip (Phase 5) |
| Authoring new animations | Auto-key, I-key insert | Import-only | Phase 6 |
| Scripted verification | Python API | No MCP animation tools | Tools + smoke test (every phase) |

## Design decisions

### D1. Playback moves into an `Animation_player` part

New class `editor::Animation_player` (`src/editor/animation/animation_player.{hpp,cpp}`),
owned by `editor.cpp` like other parts, pointer in `App_context`.

- State: list of *active* animation entries
  (`shared_ptr<erhe::scene::Animation>` + enabled flag), playhead time
  (seconds, glTF time domain), play state, speed, loop mode, and the
  timeline range (union of active animations' first/last times, or a user
  override).
- `update(const Time_context&)` called once per frame from the editor tick
  (next to `update_transform_animations`): advances the playhead when
  playing, calls `Animation::apply(t)` for each enabled animation, sends one
  `Animation_update_message`, and calls `update_node_transforms()` once per
  affected scene (collect distinct scenes from channel targets; guard null
  targets / empty channels).
- Scrubbing (`set_time(t)`) applies immediately even when paused.
- Per-frame `update` must not allocate (CLAUDE.md allocation discipline):
  the active list and the distinct-scene scratch are persistent members
  cleared with `clear()`.
- `Timeline_window` becomes a pure view over `Animation_player` (its
  transport-control code is reused; its self-clocking `update()` is deleted).
- `Properties::animation_properties` drops all playback/apply logic and shows
  summary data plus an "Edit in Animation window" button; the embedded
  `animation_curve` row is removed once Phase 1 lands.

Choosing "player applies to live nodes" (as today) over a
non-destructive-preview design: animations already write
`parent_from_node`, physics/tools already tolerate that via
`Animation_update_message`, and Blender behaves the same way (playback
mutates the evaluated scene). A "Reset to rest pose" convenience (seek t =
first time, or restore pre-play snapshot of targeted nodes) is included so
users can back out of a scrub; the pre-play snapshot restores exactly the
transforms captured when playback/scrubbing began.

### D2. One `Animation_window`, two tabs (Dope Sheet / Curves)

Blender uses two separate editors; erhe gets one window
(`src/editor/animation/animation_window.{hpp,cpp}` -- the placeholders) with:

- A persistent left panel: **channel tree** -- Animation > target Node >
  path (Translation/Rotation/Scale) > component (X/Y/Z/W). Rows have
  visibility (curve shown) and lock (edit-protect) toggles, Blender-style.
  Selection here drives both tabs. The animation shown follows the content
  library selection but can be pinned in the window.
- A top **transport strip** (reusing `Timeline_window` widget code against
  `Animation_player`) plus a shared **time ruler** with adaptive tick labels
  and a draggable playhead.
- Tab 1 **Dope Sheet**: one row per visible channel plus per-node and
  per-animation summary rows; keyframe diamonds; click / box select;
  horizontal drag retimes selected keys; Delete removes them.
- Tab 2 **Curves**: the promoted curve canvas (see D3).

The existing standalone `Timeline_window` stays (thin transport strip is
useful while other windows are focused) but shares all state through
`Animation_player`. Keeping one dockable `Animation_window` avoids two new
ini-tracked windows and keeps channel-list state shared between views.

### D3. Curve canvas promoted to a class

`editor::Curve_canvas` (new files `animation/curve_canvas.{hpp,cpp}`),
per-instance state (no function statics):

- View transform: independent x (time) / y (value) zoom + pan; `F` /
  double-click = fit view to visible channels; optional Blender-style
  **normalized display** (each curve scaled to [-1,1] for comparing channels
  of wildly different magnitude -- display-only).
- Rendering: adaptive grid with numeric labels; per-channel colors following
  the Blender convention (X/red, Y/green, Z/blue, W/yellow -- reuse the
  geometry-graph pin-color helper pattern); curves drawn **per segment**
  between adjacent keys (correct STEP stairs, cubic segments tessellated by
  screen-space length); keyframe markers (squares, selected = highlighted);
  CUBICSPLINE tangent handles as Blender-style handle lines + dots on
  selected keys; playhead line; current-value dot per curve.
- Interaction: LMB click select (shift = toggle), LMB drag on empty = box
  select, LMB drag on key/handle = move (see D5 for undo), RMB-drag pan,
  wheel zoom (ctrl = y-only, shift = x-only), ctrl+click on curve = insert
  key at that time, Delete = delete selected keys, tooltip with
  (time, value).
- All coordinate mapping helpers (`map`/`scale` in `animation_curve.cpp`)
  move into the class.

### D4. Keyframe editing API lives in `erhe::scene`

Editing raw `timestamps` / `data` vectors from UI code would scatter the
layout invariants (CUBICSPLINE triplets, component strides, sortedness).
Add member functions to `Animation_sampler` (pure logic, no editor deps):

- `insert_keyframe(time, span<const float> value)` -- inserts sorted; for
  CUBICSPLINE synthesizes zero (or auto) tangents; if a key already exists at
  `time` (exact float compare), overwrite its value.
- `remove_keyframe(index)`
- `set_keyframe_time(index, new_time) -> new_index` -- moves the key and
  re-sorts (returns the post-move index; matching keys' data blocks move with
  their timestamps).
- `set_keyframe_value(index, component, value)` /
  `set_keyframe_values(index, span<const float>)`
- `set_keyframe_tangents(index, component, in, out)` (CUBICSPLINE only)
- `set_interpolation_mode(mode)` -- converts data layout (LINEAR/STEP <->
  CUBICSPLINE grows/shrinks to triplets; synthesized tangents use
  finite differences).
- Channel cursor safety: any structural edit resets affected channels'
  `start_position` to 0 (the owning `Animation` exposes
  `reset_channel_cursors(sampler_index)`).

**Sampler sharing**: glTF allows several channels to reference one sampler.
Editing through one channel would silently edit siblings. Policy:
copy-on-first-edit -- when an edit targets a sampler referenced by more than
one channel, clone the sampler, point the edited channel at the clone, then
edit. (Imports in practice are 1:1; the clone path still gets a test.)

**Testing**: `src/erhe/scene/` has no `test/` directory today. Add one
(`erhe_scene_tests`, gated by `ERHE_BUILD_TESTS` like the other suites) with
gtests for: STEP/LINEAR/CUBICSPLINE evaluation (including the Phase 0 STEP
fix and quat normalization), insert/remove/move keeping sortedness and
triplet layout, interpolation-mode conversion round-trip, cursor reset, and
copy-on-shared-edit. This is exactly the "pure-logic code in an `erhe::*`
library" case CLAUDE.md calls for.

### D5. Undo/redo model

Every mutation goes through `Operation_stack`:

- `Animation_keyframe_operation`: generic before/after snapshot of one
  sampler's `(interpolation_mode, timestamps, data)` plus the channel
  re-pointing done by copy-on-first-edit. Snapshot-based (samplers are small
  -- tens to thousands of floats) rather than per-edit inverse ops; this
  makes drag gestures and multi-key edits trivially correct.
- Drag gestures (key move, tangent drag, dope-sheet retime) use the
  geometry-graph gesture pattern: capture the "before" snapshot on drag
  start, apply live edits directly during the drag (no per-frame operations),
  push one operation with before/after on release. Escape during drag
  restores the "before" snapshot without pushing.
- Structural ops (create animation, add/remove channel, delete animation)
  get dedicated operations mirroring `Content_library_attach_operation`.

### D6. Persistence and glTF round-trip

Two coordinated changes (Phase 5):

1. **Teach `Gltf_exporter` animations**: emit `animations[]` with samplers
   (input/output accessors, interpolation) and channels (node target, path)
   for every `Animation` whose channel targets are inside the exported
   subtree. All three interpolation modes; CUBICSPLINE data is already
   glTF-layout so export is a straight copy.
2. **Scene save/load round-trip**: scene save already writes a companion
   `.glb`; include animations in it and record per-animation
   `Gltf_source_reference` entries in the scene file (same pattern as
   `mesh_reference.py` -- a new `animation_reference.py` codegen definition,
   scene file version bump). On load, re-attach animations from the
   companion glb and re-resolve channel targets by node mapping (the glTF
   loader already maps nodes; the scene-load path must connect exported
   node indices back to scene nodes).

This makes the companion glb the single storage for animation data --
edited keyframes round-trip without a native JSON keyframe format, and plain
`export_gltf` (MCP `export_gltf` tool, user exports) becomes animation-complete
for free, which is what issue #243's "usable with glTF assets" asks for.

### D7. Time domain and snapping

glTF animation time is float seconds; erhe keeps seconds as the storage and
playhead domain. The UI offers a configurable display rate (default 30 fps)
used for: ruler labeling (seconds with optional frame subticks), optional
snap-to-frame during key drags and playhead scrubs (toggle button; ctrl
temporarily inverts). No integer-frame storage -- Blender's frame-based model
is not worth grafting onto glTF's seconds.

## Implementation plan (phases)

Each phase is independently verifiable and commits separately
(build + smoke check green before moving on). Phases 0-2 make what exists
correct and usable; 3-4 add editing; 5-6 add persistence and authoring.

### Phase 0: playback foundation + defect fixes

1. Fix STEP evaluation, CUBICSPLINE quat normalization, empty-channel guards
   (defects 1-3). Add `erhe_scene_tests` with evaluation tests (D4 testing
   scope, evaluation part).
2. Add `Animation_player`, wire into `editor.cpp` tick and `App_context`.
3. Rewire `Timeline_window` onto `Animation_player` (delete self-clocking);
   playhead advances with the window hidden.
4. Strip playback/apply from `Properties::animation_properties`; selecting an
   animation in the content library now (de)activates it in the player
   (selection = active set for now; explicit per-animation enable comes with
   the Animation window).
5. MCP: `get_animations` (list with channel/sampler summaries, first/last
   time), `get_animation_state` (playhead, playing, speed, loop, active set),
   `animation_playback` (play/pause/seek/speed/loop/activate).

Exit criteria: import an animated glTF headlessly, activate + play via MCP,
`get_node_details` shows the target node transform changing between two
seek times with hexfloat-exact expected values; STEP asset holds values.

### Phase 1: Animation window MVP (read-only)

1. `Curve_canvas` class extracted from `animation_curve()` (per-instance
   state, per-segment curve rendering, channel colors, playhead, fit-view,
   x/y zoom). `animation_curve.{hpp,cpp}` retired; Properties row replaced
   with a button focusing the Animation window.
2. `Animation_window` (fills the empty placeholder files): channel tree +
   transport strip + ruler + Curves tab hosting `Curve_canvas` (read-only:
   no key editing yet). Follows content-library selection, pin toggle.
3. Ruler drag scrubs the player.

Exit criteria: headless screenshot shows curves + playhead for an imported
animation (imgui-ini pre-sizing trick from the geometry-graph verification);
scrub via MCP seek, screenshot playhead moved.

### Phase 2: Dope Sheet tab

1. Dope-sheet rows (per channel + node/animation summary rows), key
   diamonds, shared ruler/playhead with the Curves tab.
2. Selection model shared between tabs (selected key set = (sampler, index)
   pairs; per-channel visibility/lock in the tree).

Exit criteria: screenshot verification; MCP `get_animation` reports keys
matching diamond count.

### Phase 3: keyframe editing

1. `Animation_sampler` edit API + copy-on-shared-edit + cursor reset (D4)
   with full gtest coverage.
2. `Animation_keyframe_operation` + gesture pattern (D5).
3. Curves tab: move keys (value/time), ctrl+click insert, Delete, box
   select. Dope Sheet: retime drag, Delete.
4. MCP: `animation_insert_keyframe`, `animation_move_keyframe`,
   `animation_delete_keyframe`, `get_animation` gains full keyframe dumps
   (hexfloat values per the diagnostic float serialization rule).

Exit criteria: gtests green; MCP script edits keys, verifies evaluated node
transforms change accordingly, undo/redo round-trips
(`get_undo_redo_stack`), screenshot shows moved keys.

### Phase 4: interpolation modes and tangents

1. `set_interpolation_mode` conversions (D4) + per-sampler mode stepper in
   the channel tree (arrow-stepper widget, not a combo, if inside any
   canvas-like context; plain combo is fine in the side panel).
2. CUBICSPLINE tangent handles: render + drag-edit in Curves tab.
3. MCP: `animation_set_interpolation`, `animation_set_tangents`.

Exit criteria: gtest conversion round-trips; screenshot shows STEP stairs,
handle lines; MCP verifies evaluation after tangent edit.

### Phase 5: persistence and glTF export

(SUPERSEDED: animation export and scene persistence landed via the glTF
scene roundtrip - `export_gltf` writes animations and the scene saves as a
single `.glb`; no codegen struct / scene-file version was needed. See
`doc/scene_serialization.md`.)

1. `Gltf_exporter` animation output (D6.1).
2. Scene save/load round-trip via companion glb + `animation_reference.py`
   codegen struct + scene file version bump (D6.2).
3. Import remains source of truth for provenance; re-import vs companion
   precedence documented in `scene_serialization.cpp`.

Exit criteria: MCP script: import animated glb -> edit a key -> save scene ->
clear -> load scene -> `get_animation` shows the edited key (hexfloat-exact)
and playback moves the node; `export_gltf` output re-imported into a fresh
scene plays identically.

### Phase 6: authoring

1. "New Animation" (content library context menu + Animation window button).
2. "Add channel" for the selected node (T/R/S), seeded with one key at the
   playhead from the node's current local transform.
3. "Insert key at playhead" (per channel and per node: captures current
   transform component); auto-key toggle in the transport strip (any
   transform-tool edit while enabled inserts/overwrites keys at the
   playhead -- listen to the existing transform message flow).
4. Delete animation / remove channel (undoable).
5. MCP: `create_animation`, `animation_add_channel`,
   `animation_insert_key_from_node`.

Exit criteria: MCP script authors a 2-key translation animation on a created
node from scratch, plays it, saves/reloads, re-verifies.

## Verification plan

- **Unit tests**: new `erhe_scene_tests` suite (evaluation semantics, edit
  API invariants, mode conversion, shared-sampler cloning). Run plain and
  ASAN per the testing guidance.
- **Headless MCP smoke test**: `scripts/animation_smoke_test.py` patterned on
  `scripts/geometry_nodes_smoke_test.py`; grows with each phase (playback
  seek assertions, edit round-trips, undo/redo churn, save/load, authoring).
  Needs a small animated test asset checked into `res/editor/assets/`
  (e.g. a 2-node cube with one TRANSLATION/LINEAR, one ROTATION/CUBICSPLINE
  and one STEP channel -- author it via the Phase 6 tools once they exist;
  until then generate once with a tiny Python glTF writer committed next to
  the smoke test, or export a trivial file from Blender at `D:\blender`).
- **Screenshots**: `capture_screenshot` in the headless Vulkan build for
  every UI-visible milestone (curves, playhead motion, dope sheet, handles),
  with the imgui-ini window-sizing pre-launch trick.
- **Interactive sanity** (user, windowed build): dragging keys/handles feels
  right, box select, snapping -- gesture ergonomics cannot be MCP-verified.
- Restore `config/editor/desktop_window_imgui_host_imgui.ini` after every
  editor run; temporary trace logging (e.g. a new `editor.animation` log
  category) reverted before commits.

## Blender reference material

The issue points at Blender's Graph Editor docs; the clone at `D:\blender`
is available for consulting concrete behavior:

- `source/blender/editors/space_graph/` -- Graph Editor: drawing
  (`graph_draw.cc` -- curve/handle/key rendering), editing operators
  (`graph_edit.cc`), selection (`graph_select.cc` -- click/box/lasso rules).
- `source/blender/editors/space_action/` -- Dope Sheet drawing and editing.
- `source/blender/editors/animation/` -- shared keyframe machinery:
  `keyframes_edit.cc` (map/apply over selected keys), `keyframing.cc`
  (insert-key logic, auto-key), `anim_channels_*.cc` (channel tree).
- `source/blender/blenkernel/intern/fcurve.cc` -- F-curve evaluation
  (reference for handle math; erhe keeps glTF's simpler model).

Borrow interaction conventions (channel tree with hide/lock, box select,
handle rendering, normalized display, playhead behavior), not architecture:
erhe's data model stays glTF's sampler/channel form so that import/export is
lossless, whereas Blender F-curves (bezier with per-key handle types) would
require lossy conversion at both ends.

## Key files reference

| Area | Files |
|---|---|
| Data model + evaluation | `src/erhe/scene/erhe_scene/animation.{hpp,cpp}` |
| New unit tests | `src/erhe/scene/test/` (new), pattern: `src/erhe/item/test/` |
| glTF import/export | `src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp` (`parse_animation`, `Gltf_exporter`) |
| Editor import attach | `src/editor/parsers/gltf.cpp` |
| Scene save/load | `src/editor/parsers/gltf.cpp` (`save_scene_gltf` / `open_scene_gltf`; see `doc/scene_serialization.md`) |
| Player (new) | `src/editor/animation/animation_player.{hpp,cpp}` (new) |
| Transport UI | `src/editor/animation/timeline_window.{hpp,cpp}` |
| Editor window | `src/editor/animation/animation_window.{hpp,cpp}` (currently empty placeholders) |
| Curve canvas | `src/editor/animation/curve_canvas.{hpp,cpp}` (new, from `animation_curve.{hpp,cpp}`) |
| Properties hook | `src/editor/windows/properties.cpp` (`animation_properties`) |
| Editor tick / wiring | `src/editor/editor.cpp`, `src/editor/app_context.hpp`, `src/editor/time.{hpp,cpp}` |
| Messages | `src/editor/app_message.hpp`, `app_message_bus.hpp` (`Animation_update_message`) |
| Undo | `src/editor/operations/` (`Operation_stack`, new keyframe ops) |
| MCP tools | `src/editor/mcp/mcp_server*.{hpp,cpp}` |
| Smoke test | `scripts/animation_smoke_test.py` (new) |
| Test asset | `res/editor/assets/` (new small animated glb) |

## Out of scope / future work

- **Morph target `WEIGHTS` channels**: `get_component_count` returns 0 and
  `apply` is a TODO; erhe has no morph-target rendering. Import must not
  crash on such channels (skip with a log line); UI hides them. Revisit when
  morph targets land.
- **Animation blending / layers / NLA**: the player applies animations in
  active-set order, last writer wins per node component. Real blending is a
  separate design.
- **Skinned-mesh interaction**: skins exist (`erhe_scene/skin.hpp`) and
  joint nodes animate like any node, so skeletal playback should work via
  node transforms; verifying/profiling skinned playback is follow-up work.
- **Driver expressions, constraints, IK**: not planned.
- **Frame-integer time model**: rejected (D7); seconds stay canonical.
- **Curve modifiers (noise, cycles)**: Blender feature, not planned.
