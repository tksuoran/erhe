# IK Drag Options - Phase 2 Requirements

Status: in progress

This document specifies two slices of Phase 2 of the rigging roadmap in
`rigging_tools.md`: the **effector orientation option** (section 1,
implemented) and the **chain visualization** (section 2, specified here and
implemented by a later commit). Both act on the interactive IK drag of
`fabrik_ik.md` (Phase 1) as extended by `ik_settings.md` (per-bone locks and
limits) and `pole_target.md` (the pole).

Terms are those of `pole_target.md`: **chain**, **intermediate joint**,
**governing pole**. One more is used below:

- **Gesture**: the span from `Ik_drag::begin` to the matching
  `Ik_drag::reset`, i.e. one interactive drag of the Transform tool, or one
  `ik_drag` MCP call.

## 1. Effector orientation

The chain's write-back rotates every joint except the effector
(`fabrik_ik.md` section 4): the solver aims each joint's child segment, and
the effector has no child segment in the chain. What the effector's own
orientation does while the chain bends under it is therefore a free choice,
and this slice makes it one.

### 1.1 The option

**R1.** `enum class Ik_effector_orientation` in
`src/editor/transform/ik_drag.hpp` has exactly the values `keep_world` and
`follow_last_segment`, with `c_ik_effector_orientation_strings` giving the UI
labels in declaration order, in the shape of
`c_scale_gizmo_mode_strings` in `transform_tool_settings.hpp`.

**R2.** `Ik_drag::begin` takes the mode as its second argument and stores it
for the gesture, beside the chain, the drag-start pose and the pole;
`Ik_drag::apply` reads the stored mode. One gesture therefore solves under one
mode throughout, the way it solves under one governing pole throughout
(`pole_target.md` R12), and `apply` reads no setting and no UI state.
`Ik_drag::reset` returns the stored mode to `keep_world`.

**R3.** `keep_world` is the Phase 1 behavior, unchanged: after the write-back,
`apply` rotates the effector so that its **world** rotation is its drag-start
world rotation. Only the effector's position follows the chain.

**R4.** `follow_last_segment` leaves the effector's **local** rotation at its
drag-start value: `apply` refreshes the effector's world transform so that it
reflects its solved parent and writes nothing else to the effector. The
effector therefore rides rigidly on its parent joint, exactly as any other
child of that joint does, and its world rotation carries the parent joint's
solved rotation. This is what "follow the last segment" means at the transform
level: the last segment is the parent joint's child segment, the solver aimed
that segment at the effector's solved position, and the effector turns with
it.

**R5.** The effector's own `Ik_settings` locks and limits take no part under
either mode. A joint's constraint is enforced through its child segment
(`ik_settings.md` section 4) and the effector has none in the chain, so
`Ik_drag::begin` pushes an unused constraint entry for it on both solver paths
and neither mode consults it. An `Ik_settings` attachment on the effector
still governs the drag in the one way `pole_target.md` R5 gives it: it is the
first attachment the pole scan reads.

**R6.** A non-bone drag handle effector (`fabrik_ik.md` section 1: a node
parented under a bone) follows R3 and R4 with no special case. Under
`keep_world` the handle keeps its world orientation while the bone swings to
aim at it, which is the Phase 1 behavior; under `follow_last_segment` the
handle turns with that bone. Neither mode writes anything to the handle's
local translation, so the bone still aims at the same point.

### 1.2 Transform tool setting

**R7.** `Transform_tool_settings::effector_orientation` holds the mode, beside
`translate_ik_enable` ("Bone IK"), defaulting to
`Ik_effector_orientation::keep_world`. It is session state that is not
persisted, exactly as `translate_ik_enable` is not: a drag-behavior toggle
whose owner is the running Transform tool, not
`Editor_settings_config::transform_tool` (which holds the persisted
preferences, e.g. `translate_snap_absolute`).

**R8.** The Move tool's Properties panel (`Move_tool::imgui`) draws the
setting as a combo labelled "Effector Orientation" immediately after the
"Bone IK" checkbox, with a tooltip stating what each value does. It is the
gizmo drag's setting, so it sits with the gizmo drag's other setting.

**R9.** `Transform_tool::try_translate_ik` passes
`shared.settings.effector_orientation` to `Ik_drag::begin` where it discovers
the chain - once per gesture, on the drag's first update. Changing the setting
mid-drag therefore takes effect from the next drag, matching R2.

### 1.3 MCP surface

**R10.** The `ik_drag` tool (`pole_target.md` R22) gains one optional argument
`effector_orientation`, a string that is `"keep_world"` (the default when the
argument is absent) or `"follow_last_segment"`. Any other value is refused
with `isError` naming the two accepted values. The handler passes the value
straight to `Ik_drag::begin` and never reads
`Transform_tool_settings::effector_orientation`
(`doc/agents/mcp_api_guidelines.md`), so an `ik_drag` call means the same
thing whatever the UI last showed. The result echoes the effective value under
the key `effector_orientation`. The argument and its two values are listed in
the tool's `config/editor/mcp_tools.json` schema entry.

### 1.4 Acceptance criteria

Measured against a running headless editor by
`scripts/ik_effector_orientation_verify.py`, which imports the tracked
`res/editor/assets/RiggedFigure/RiggedFigure.glb` fixture, reads the
effector's world and local rotation with `get_node_transform` before and after
each drag, and undoes every measured drag before the next one (the rule of
`pole_target.md`'s acceptance section). Rotation distance is the quaternion
distance up to sign, `1 - |dot(q_before, q_after)|`.

1. A drag that moves the effector far enough to rotate its parent joint by
   **more than 10 degrees** is used for both criteria below; the run reports
   the measured parent rotation so the premise is visible.
2. `effector_orientation: "keep_world"`: the effector's **world** rotation
   after the drag equals its drag-start world rotation to within **1e-4**.
3. `effector_orientation: "follow_last_segment"`: the effector's **local**
   rotation after the drag equals its drag-start local rotation to within
   **1e-4**, and its **world** rotation differs from its drag-start world
   rotation by more than 1e-3 - the mode moved it.
4. An `ik_drag` with an unrecognized `effector_orientation` returns `isError`
   and changes no joint transform.
5. Every criterion above is one undo step per drag, as `pole_target.md` R19
   requires of every gesture.
6. `scripts/ik_pole_verify.py` still passes every criterion it covers: the
   default is `keep_world`, so the pole slice's measurements are unchanged.

## 2. Chain visualization

While an IK drag is running, the chain the solver is acting on is invisible:
the bones move, but which of them the solve owns, where its fixed root is and
which pole is aiming it are all guesses. This slice draws them.

**R11.** The drawing owner is `Transform_tool::tool_render` (already an
override, already the place the gizmo's own handles are drawn from through
`Handle_visualizations`). It holds the `Ik_drag`, so it needs no new
reference, and it is called once per view per frame with the `Render_context`
the lines need.

**R12.** The current line API is `erhe::renderer::Primitive_renderer`, taken
from the render context as
`context.get({erhe::graphics::Primitive_type::line, <stride>, true, true})`,
the form every call site in `src/editor/tools/debug_visualizations.cpp` uses.
`pole_target.md` section 7 names that same type and needs no correction.

**R13.** The visualization is drawn exactly while `Ik_drag::is_active()` is
true, which is the span of one gesture: `Transform_tool::try_translate_ik`
sets it through `Ik_drag::begin` on the drag's first update and
`Transform_tool::end_drag` clears it through `Ik_drag::reset`. `tool_render`
tests that one flag. Nothing is polled, recomputed or pushed per frame beyond
the frame's own drawing: the geometry is derived from state the drag already
holds.

**R14.** What is drawn, all in world space:

- the **chain polyline**, one line per segment through the joints' current
  world positions, root to effector;
- the **root marker**, a three-axis cross at `joints[0]`'s world position;
- the **effector marker**, a three-axis cross at the effector's world
  position, drawn in the chain color;
- when the drag has a governing pole (`Ik_drag::has_pole()`), the **pole
  line** from the captured pole position (`pole_target.md` R9, a drag-start
  capture, so the line does not chase a moving pole) to the chain root, and
  the **pole marker**, a three-axis cross at that position.

Marker arms are sized as a fraction of the chain's reach (the sum of its
segment lengths), so the markers scale with the rig rather than with the
scene's units.

**R15.** Colors and line widths are fields of the existing
`Debug_visualizations_style` codegen struct
(`src/editor/config/definitions/debug_visualizations_style.py`), which is
where the editor's visualization appearance is edited in one place:
`ik_chain_color`, `ik_chain_width`, `ik_root_color`, `ik_pole_color` and
`ik_marker_width`. New fields carry `added_in=2` and bump the struct's
`version`, and a codegen definition change needs the build run twice (see the
`erhe_codegen` notes in AGENTS.md).

**R16.** The line list is produced by a free function with no scene access,
declared beside the solver in `src/editor/transform/ik_solver.hpp`:

```
void build_ik_drag_lines(const Ik_drag_line_input& input, Ik_drag_line_buffer& buffer);
```

`Ik_drag_line_input` carries the joint world positions, the pole position and
a `bool`-free `has_pole` discriminator (an `std::optional<glm::vec3>` pole
position), the marker scale and the four colors; `Ik_drag_line_buffer` is a
caller-owned record of `std::vector`s of coloured line endpoints. The buffer
is a member of the drawing owner, cleared at the point of use and again after
use, so a steady-state drag frame allocates nothing (AGENTS.md "Run-time
Memory Allocation Discipline"). `tool_render` calls the function and then
hands the buffer's contents to the `Primitive_renderer`.

**R17.** Verification:

- Unit tests in `src/editor/transform/test/test_ik_solver.cpp`
  (`editor_ik_solver_tests`) over `build_ik_drag_lines`: a three-joint chain
  with no pole produces the segment lines plus the two markers and no pole
  line; the same chain with a pole produces the pole line from the pole
  position to `joints[0]` and the pole marker; a second call into the same
  buffer produces the same line count (the clear-and-fill rule of R16) and
  performs no reallocation once the buffer has reached its high-water mark.
- Interactive, in a windowed editor: drag a bone of the `RiggedFigure`
  fixture with "Bone IK" on and observe the chain, root and pole visuals
  appear for the duration of the drag and vanish on release.

Headless verification of the drawing is not available and none is specified.
The MCP `ik_drag` tool is one complete gesture within one call
(`pole_target.md` R22): the drag has already been reset by the time any frame
renders, so `capture_screenshot` can never catch an active drag. R16 exists
so that the part of this slice that can be checked without a display - the
line list - is checked by a test rather than by eye.

## Out of scope

- An effector orientation that blends between the two modes, or one authored
  per bone rather than per tool: the choice is a drag-behavior preference, and
  a per-bone orientation rule belongs to the persistent constraint model of
  Phase 4.
- Drawing the chain outside a drag (a persistent "this bone is in an IK chain"
  indication): chains are discovered per drag (`fabrik_ik.md` section 1), so
  outside a drag there is no chain to draw. That indication arrives with
  Phase 4's persistent chains.
- Drawing the joint limits and locks of `ik_settings.md` as cones or arcs.

## Implementation status

Section 1 is implemented as specified:

- `Ik_effector_orientation` and the stored mode -
  `src/editor/transform/ik_drag.{hpp,cpp}`.
- Transform tool setting - `Transform_tool_settings::effector_orientation`
  (`transform_tool_settings.hpp`), drawn by `Move_tool::imgui`
  (`move_tool.cpp`), passed at `Transform_tool::try_translate_ik`
  (`transform_tool.cpp`).
- MCP - the `effector_orientation` argument of `ik_drag`
  (`mcp_server_scene_action.cpp`, schema in `config/editor/mcp_tools.json`),
  exercised by `Mcp_test.ik_drag_solves_a_bone_chain_and_records_one_undo_entry`.
- Acceptance verification - `scripts/ik_effector_orientation_verify.py`.

Section 2 is not implemented.

Outstanding: interactive (windowed) verification of the Move tool combo and
of a live gizmo drag under `follow_last_segment`.
