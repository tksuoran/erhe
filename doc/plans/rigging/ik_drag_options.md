# IK Drag Options - Phase 2 Requirements

Status: in progress

This document specifies three slices of Phase 2 of the rigging roadmap in
`rigging_tools.md`: the **effector orientation option** (section 1), the
**chain visualization** (section 2) and the **drag behavior options**
(section 3: mid-chain drag, solve from, pole alignment). Sections 1 and 2 are
implemented (see Implementation status at the end); all three act on the
interactive IK drag of
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

**R5.** The effector's own `Ik.lock_*` and `Ik.limit_*` values take no part
under either mode. A joint's constraint is enforced through its child segment
(`ik_settings.md` section 4) and the effector has none in the chain, so
`Ik_drag::begin` pushes an unused constraint entry for it on both solver paths
and neither mode consults it. The effector node still governs the drag in the
one way `pole_target.md` R5 gives it: its `Ik.pole_target` is the first the
pole scan reads.

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
from the render context as `context.get(<config>)`, the form every call site in
`src/editor/tools/debug_visualizations.cpp` uses. The config is the Transform
tool's own `handle_line_config` - the x-ray line bucket its handles and drag
guides already use - so the chain stays readable inside the skinned mesh it
runs through, for the reason `Bone_visualization` gives for drawing bones
x-ray. `pole_target.md` section 7 names that same type and needs no
correction.

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
scene's units. That fraction is a constant of the line-building input (R16),
not a setting: it states the markers' proportion to the chain, which no rig
needs to change.

**R15.** Colors and line widths are fields of the existing
`Debug_visualizations_style` codegen struct
(`src/editor/config/definitions/debug_visualizations_style.py`), which is
where the editor's visualization appearance is edited in one place:
`ik_chain_color`, `ik_chain_width`, `ik_root_color`, `ik_pole_color` and
`ik_marker_width`. New fields carry `added_in=2` and bump the struct's
`version`, and a codegen definition change needs the build run twice (see the
code generation notes in doc/cmake_conventions.md).

**R16.** The line list is produced by a free function with no scene access,
declared beside the solver in `src/editor/transform/ik_solver.hpp`:

```
void build_ik_drag_lines(const Ik_drag_line_input& input, Ik_drag_line_buffer& buffer);
```

`Ik_drag_line_input` carries the joint world positions, the pole position as a
`bool`-free discriminator (an `std::optional<glm::vec3>`, unset meaning an
unpoled drag), the marker scale (defaulted, R14) and the three colors of R15 -
the effector marker takes the chain color, so there are three, not four.
`Ik_drag_line_buffer` is a caller-owned record of two `std::vector`s of
coloured line endpoints, one per line width the owner draws with:
`path_lines` (the chain polyline and the pole line, `ik_chain_width`) and
`marker_lines` (the three crosses, `ik_marker_width`). The buffer is a member
of the drawing owner, cleared at the point of use and again after use, so a
steady-state drag frame allocates nothing (AGENTS.md "Run-time Memory
Allocation Discipline"); the joint positions the input spans are gathered into
a second scratch vector of the owner under the same rule, and neither scratch
holds a node reference. `tool_render` calls the function and then hands the
buffer's contents to the `Primitive_renderer`, one `add_line` per line with
that group's width.

**R17.** Verification:

- Unit tests in `src/editor/transform/test/test_ik_solver.cpp`
  (`editor_ik_solver_tests`) over `build_ik_drag_lines`: a three-joint chain
  with no pole produces the segment lines plus the two markers and no pole
  line; the same chain with a pole produces the pole line from the pole
  position to `joints[0]` and the pole marker; a second call into the same
  buffer produces the same line count (the clear-and-fill rule of R16) and
  performs no reallocation once the buffer has reached its high-water mark;
  and the degenerate inputs draw nothing spurious (fewer than two joints
  produces no lines at all, a chain of zero reach produces its segment lines
  and no markers).
- Interactive, in a windowed editor: drag a bone of the `RiggedFigure`
  fixture with "Bone IK" on and observe the chain, root and pole visuals
  appear for the duration of the drag and vanish on release.

Headless verification of the drawing is not available and none is specified.
The MCP `ik_drag` tool is one complete gesture within one call
(`pole_target.md` R22): the drag has already been reset by the time any frame
renders, so `capture_screenshot` can never catch an active drag. R16 exists
so that the part of this slice that can be checked without a display - the
line list - is checked by a test rather than by eye.

## 3. Drag behavior options

Three behaviors of the IK drag each come in two variants, and the user picks
the variant per drag through the Move tool, the way section 1 picks the
effector orientation. The first variant of each option is the behavior the
drag has always had and stays the default; the second is new.

| Option | Default variant | Other variant |
|---|---|---|
| Mid-chain drag (3.1) | Rigid Children | Pin Chain End |
| Solve from (3.2) | Drag Start | Previous Step |
| Pole alignment (3.3) | Snap | Ease In |

**R18.** Each option is an `enum class` in `src/editor/transform/ik_drag.hpp`
with a `c_..._strings` label array in declaration order, in the shape of
`Ik_effector_orientation` (R1): `Ik_mid_chain_drag { rigid_children,
pin_chain_end }`, `Ik_solve_from { drag_start, previous_step }`,
`Ik_pole_alignment { snap, ease_in }`. The ease-in variant has one parameter,
`pole_ease_distance` (R27).

**R19.** The options of one gesture travel together in one value class,
`Ik_drag_options`, which also holds the effector orientation of section 1:

```
class Ik_drag_options
{
public:
    Ik_effector_orientation effector_orientation{Ik_effector_orientation::keep_world};
    Ik_mid_chain_drag       mid_chain_drag      {Ik_mid_chain_drag::rigid_children};
    Ik_solve_from           solve_from          {Ik_solve_from::drag_start};
    Ik_pole_alignment       pole_alignment      {Ik_pole_alignment::snap};
    float                   pole_ease_distance  {0.5f};
};
```

`Ik_drag::begin` takes an `Ik_drag_options` as its second argument in place
of the bare effector orientation and stores it for the gesture (R2 extended
to every option): one gesture solves under one set of options, and `apply`
reads no setting and no UI state. `Ik_drag::reset` returns the stored options
to their defaults.

### 3.1 Mid-chain drag

A drag of a bone that has bone children in the middle of a chain moves that
bone (the effector) and aims its ancestors at it. What happens to the bones
below the effector is the option.

**R20.** `rigid_children` is the behavior of sections 1-2, unchanged: the
bones below the effector follow it rigidly - their local transforms do not
change - so the chain's end moves with the effector.

**R21.** `pin_chain_end` keeps the chain's end where it was at drag start.
The **lower chain** runs from the effector down: starting at the effector,
it follows the only bone child of each bone and stops at the first bone with
no bone child or with more than one (the **end joint**; a hand with fingers
is an end joint). The lower chain exists when it holds at least two joints,
so an effector with no bone child or with several bone children has none,
and a non-bone drag handle (R6) never has one; a drag without a lower chain
behaves as under `rigid_children`.

**R22.** Under `pin_chain_end` each `apply` first solves the chain of
sections 1-2 (the **upper chain**, root..effector) exactly as before, then
solves the lower chain as a chain of its own: its root is the effector at its
solved position, its target is the end joint's drag-start world position,
and its joints' constraints (`ik_settings.md`) and governing pole
(`pole_target.md` R5, scanned from the end joint toward the effector) are
resolved at `begin` by the same rules as the upper chain's. The end joint
keeps its drag-start world rotation, so everything below it stays in place in
the world; when the pinned position is out of the lower chain's reach the
lower chain reaches toward it (FABRIK's closest reachable pose) and the end
joint lies where that pose puts it.

**R23.** Under `pin_chain_end` with a lower chain, the effector is the lower
chain's root: the lower solve sets its rotation, and the effector orientation
(section 1) does not apply to it. The effector orientation still applies
under `rigid_children`, and to any drag without a lower chain.

**R24.** Every joint of the lower chain takes part in the gesture's undo step
(`Ik_drag::make_transform_operation`, and the joints
`Transform_tool::try_translate_ik` appends to the transform entries), and in
the chain visualization of section 2: the chain polyline continues from the
effector through the lower chain to the end joint, and the end joint gets a
marker in the root color - it is held fixed the way the root is.

### 3.2 Solve from

**R25.** `drag_start` is the behavior of sections 1-2, unchanged: every
`apply` restores the drag-start pose and solves from it, so a drag is path
independent - a target reached by two paths gives one pose, and dragging back
to the start restores the start pose exactly.

**R26.** `previous_step` solves each `apply` from the pose the previous
`apply` of the same gesture left, so the pose carries what the drag picked up
on the way: dragging back to the start does not in general restore the start
pose. The first `apply` of a gesture solves from the drag-start pose, as under
`drag_start`. The joints' constraints keep their drag-start resolution; the
no-teleport extension of a limit (`ik_settings.md` section 4) is taken from
the pose the step solves from, which lies inside the drag-start extension, so
over a gesture the admissible region only narrows toward the authored limit.
The undo step still records the drag-start pose as "before". Under
`pin_chain_end` the lower chain solves from its previous step too, with the
same drag-start target.

### 3.3 Pole alignment

**R27.** `snap` is the behavior of `pole_target.md`, unchanged: from the first
`apply` the bend is swiveled fully onto the pole, however far that is from the
drag-start bend.

**R28.** `ease_in` swivels the bend by a fraction `w` of the swivel `snap`
would make (`ik_apply_pole` gains the weight; the rotation about the
root-to-effector line is `w` times the full angle):
`w = clamp(d / (pole_ease_distance * reach), 0, 1)`, where `d` is the distance
from the effector's drag-start position to the current target and `reach` is
the chain's total segment length. `pole_ease_distance` is a fraction of the
reach in [0.05, 2], default 0.5. So the pole takes over gradually over the
first part of the drag and fully from there on; `w` depends on the target
only, so under `drag_start` a drag stays path independent and dragging back
to the start eases the pole back out. The weight applies wherever the pole
applies (both solver paths, and the lower chain of R22).

### 3.4 Transform tool settings

**R29.** `Transform_tool_settings::ik_drag_options` (an `Ik_drag_options`)
replaces `Transform_tool_settings::effector_orientation`; it is session state
that is not persisted, as R7 states for the effector orientation.

**R30.** `Move_tool::imgui` draws the options after "Effector Orientation",
in this order: "Mid-Chain Drag" (combo), "Solve From" (combo), "Pole
Alignment" (combo), and "Pole Ease Distance" (a slider in [0.05, 2], shown
only while Pole Alignment is Ease In). Each has a tooltip stating what its
variants do. `Transform_tool::try_translate_ik` passes the settings' options
to `Ik_drag::begin` (R9).

### 3.5 MCP surface

**R31.** The `ik_drag` tool gains the optional arguments `mid_chain_drag`
(`"rigid_children"` default, `"pin_chain_end"`), `solve_from`
(`"drag_start"` default, `"previous_step"`), `pole_alignment` (`"snap"`
default, `"ease_in"`) and `pole_ease_distance` (number, default 0.5, refused
outside [0.05, 2]). An unrecognized value is refused with `isError` naming the
accepted values, before any joint moves. As R10 states for
`effector_orientation`, the handler never reads the Transform tool settings,
and the result echoes every effective option.

**R32.** Because `previous_step` only differs from `drag_start` over several
steps, `ik_drag` takes either `target` (one world position, as before) or
`path` (an array of world positions, applied in order within the one gesture;
the last is the final target); giving both, or neither, is refused. A path is
one gesture and one undo step (`pole_target.md` R22).

### 3.6 Acceptance criteria

Measured by `scripts/ik_interactive_pass_verify.py` section 8
(`interactive_test_pass.md`), which sets the options through the Move tool's
combos in the Transform window, as a user does:

1. Pin Chain End: dragging `bone_1` of `skin_test_3_boxes` along the circle
   of positions that keep `bone_2` in reach (a swivel of `bone_1` about the
   `bone_0`-to-`bone_2` line) leaves `bone_2`'s world position and rotation
   unchanged (within 1e-3 / 0.05 degrees) while `bone_1` follows the drag; a
   drag off that circle keeps bone lengths and moves `bone_2` only as far as
   the lower chain cannot reach.
2. Previous Step: a drag that pulls the chain straight out of reach and back
   to its start ends in a pose that differs from the start pose (by more than
   1 degree on some bone), where Drag Start restores it exactly; no step of
   either moves a joint more than 5 times the target's step.
3. Ease In: over a drag moving away from the start, the bend's angle off the
   pole decreases from the drag-start angle to 0 as `w` grows and is 0 once
   `w` reaches 1; dragging back to the start restores the start pose (Drag
   Start).
4. Each option combination above is one undo step per drag.
5. The unit tests of `ik_apply_pole` cover the weight: 0 leaves the positions
   untouched, 1 is the full swivel, 0.5 half the angle.

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
- Transform tool setting - `Transform_tool_settings::ik_drag_options`
  `.effector_orientation` (`transform_tool_settings.hpp`), drawn by `Move_tool::imgui`
  (`move_tool.cpp`), passed at `Transform_tool::try_translate_ik`
  (`transform_tool.cpp`).
- MCP - the `effector_orientation` argument of `ik_drag`
  (`mcp_server_scene_action.cpp`, schema in `config/editor/mcp_tools.json`),
  exercised by `Mcp_test.ik_drag_solves_a_bone_chain_and_records_one_undo_entry`.
- Acceptance verification - `scripts/ik_effector_orientation_verify.py`.

Section 2 is implemented as specified:

- Line building - `build_ik_drag_lines` with `Ik_drag_line`,
  `Ik_drag_line_input` and `Ik_drag_line_buffer`, beside the solver in
  `src/editor/transform/ik_solver.{hpp,cpp}`; unit tests in
  `src/editor/transform/test/test_ik_solver.cpp` (`Ik_drag_lines.*`).
- Drawing - `Transform_tool::render_ik_drag`, called from
  `Transform_tool::tool_render` while `Ik_drag::is_active()`
  (`transform_tool.{hpp,cpp}`), with the buffer and the joint-position scratch
  as members; the pole's drag-start position is read through
  `Ik_drag::get_pole_position()`.
- Appearance - `Debug_visualizations_style::ik_chain_color` / `ik_chain_width`
  / `ik_root_color` / `ik_pole_color` / `ik_marker_width`
  (`src/editor/config/definitions/debug_visualizations_style.py`, struct
  version 2).

Section 3 is implemented as specified:

- Options - `Ik_mid_chain_drag`, `Ik_solve_from`, `Ik_pole_alignment` with
  their `c_..._strings` labels, and `Ik_drag_options`, in
  `src/editor/transform/ik_drag.hpp` (R18, R19). `Ik_drag::begin` takes an
  `Ik_drag_options` and stores it for the gesture (`Ik_drag::get_options()`);
  `Ik_drag::reset` restores the defaults.
- Solve From - `Ik_drag::apply` (`ik_drag.cpp`, R25, R26): under
  `previous_step` every apply after the gesture's first
  (`m_has_previous_step`) fills the chain's positions and local rotations from
  the joints' current pose instead of restoring the drag-start pose, so the
  constrained solver's no-teleport extension is taken from that pose; the
  effector's local transform returns to its drag-start value each step, so
  R3 / R4 hold under either effector orientation.
- Transform tool setting - `Transform_tool_settings::ik_drag_options`
  replaces `effector_orientation` (R29); `Move_tool::imgui` draws the "Solve
  From" combo after "Effector Orientation" (R30);
  `Transform_tool::try_translate_ik` passes the options to `Ik_drag::begin`.
- MCP - the `solve_from` argument of `ik_drag`, echoed in the result, and
  `path` as the alternative to `target` (R31, R32; schema in
  `config/editor/mcp_tools.json`), exercised by
  `Mcp_test.ik_drag_path_and_solve_from`.
- Pole Alignment - `ik_apply_pole` takes the weight and returns the full
  swivel angle, `Ik_chain::pole_weight` carries it (`ik_solver.{hpp,cpp}`,
  R28). The unconstrained path swivels its result by the weight; the
  constrained path swivels its first defined per-iteration application by the
  weight and then aims every later iteration at the residual angle that left,
  so iterating holds the partial swivel instead of compounding it into a
  snap. `Ik_drag::apply` computes the weight (`Ik_drag::pole_weight`: 1 under
  `snap`, the R28 ramp under `ease_in`) and reports the last one through
  `Ik_drag::get_pole_weight()`.
- Move tool - "Pole Alignment" combo after "Solve From", and "Pole Ease
  Distance" (slider in [0.05, 2]) while Ease In is chosen (R30).
- MCP - the `pole_alignment` and `pole_ease_distance` arguments of `ik_drag`,
  refused before any joint moves when unrecognized or out of range, echoed
  with the last step's `pole_weight` (R31), exercised by
  `Mcp_test.ik_drag_pole_alignment_ease_in`.
- Mid-Chain Drag - `Ik_drag_chain` (`ik_drag.{hpp,cpp}`) holds one chain's
  drag-start capture, constraints, pole and solver input, and solves and
  writes back through one path; `Ik_drag` holds the upper chain
  (root..effector) and, under `pin_chain_end`, the lower chain
  (effector..end joint) that `Ik_drag::begin` discovers from a bone
  effector (R21), with its constraints and its pole (scanned from the end
  joint) resolved by the upper chain's rules. `Ik_drag::apply` solves the
  upper chain, then the lower chain from the pose it rides in toward the
  end joint's drag-start world position, with the effector's solved parent
  as the lower root's parent frame and the gesture's pole weight (R22,
  R28), and puts the end joint back at its drag-start world rotation; the
  effector orientation applies only without a lower chain (R23). Under
  `previous_step` a drag with a lower chain keeps the effector's previous
  local rotation too, as a solved joint of the lower chain (R26).
- Joints - `Ik_drag::get_upper_joints()` and `get_lower_joints()`;
  `Ik_drag::make_transform_operation` and `Transform_tool::try_translate_ik`
  cover both chains (R24).
- Visualization - `Ik_drag_line_input::lower_joint_positions`:
  `build_ik_drag_lines` continues the polyline through the lower chain and
  marks the end joint in the root color (R24), unit test
  `Ik_drag_lines.lower_chain_continues_polyline_and_marks_pinned_end`;
  `Transform_tool::render_ik_drag` fills both spans from one scratch vector.
- Move tool - "Mid-Chain Drag" combo after "Effector Orientation", before
  "Solve From" (R30).
- MCP - the `mid_chain_drag` argument of `ik_drag`, refused before any joint
  moves when unrecognized, echoed, with the lower chain's joints in
  `lower_joints` (R31), exercised by
  `Mcp_test.ik_drag_mid_chain_drag_pin_chain_end`.
- Acceptance verification - criterion 1 of 3.6 is check 8.6, criterion 2 is
  check 8.4 and criterion 3 is check 8.5 of
  `scripts/ik_interactive_pass_verify.py`, each measuring one undo step per
  drag (criterion 4); criterion 5 is
  `Ik_solver.pole_weight_scales_the_swivel` and
  `Ik_solver.pole_weight_applies_on_both_solver_paths`
  (`src/editor/transform/test/test_ik_solver.cpp`).

Outstanding: interactive (windowed) verification of the Move tool combo, of a
live gizmo drag under `follow_last_segment`, and of the chain visualization
(R17's second bullet - a live drag is not reachable headlessly).
