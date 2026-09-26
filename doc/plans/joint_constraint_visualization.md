# Joint constraint debug visualization

Status: proposed

This plan extends `doc/editor/tools.md` (`Debug_visualizations`) with
viewport visualizations of the two kinds of joint constraint the editor
authors and enforces:

- **Physics joints**: the `Joint` prim (`src/editor/scene/joint.hpp`) with its
  shared `erhe::physics::Physics_joint_settings` (six axes: translation X/Y/Z,
  rotation X/Y/Z, each free or limited to `[min, max]`), built into a live
  six-DOF constraint by `Joint_system` (`src/editor/scene/joint_system.hpp`).
- **IK joint limits**: the attached `Ik.*` properties of a bone node
  (`src/editor/scene/ik_properties.hpp`: per-axis lock and limit, relative to
  `Ik.rest_rotation`), enforced by the constrained FABRIK solver as a
  swing/twist clamp (`doc/plans/rigging/ik_settings.md` section 4).

Today neither is visible: a joint's frames, its free axes and its ranges are
only numbers in the Properties window, and the only IK visual is the chain
polyline drawn during an IK drag (`build_ik_drag_lines()`,
`doc/plans/rigging/ik_drag_options.md` section 2). Authoring a hinge range or a
shoulder cone without seeing it is guesswork, and a joint whose frames were
captured at an unexpected pose is only discovered when the simulation
misbehaves.

## Goals

- G1. See where a joint is: both anchor frames, the bodies they belong to,
  and whether the joint is live or pending.
- G2. See what a joint admits: every free axis, every limited range, every
  fixed axis, drawn in the frame the solver enforces it in.
- G3. See where the joint is now inside that range: the current joint
  coordinate on each movable axis, and a clear signal when it is outside
  its range (a physics joint violated by a strong pull; an IK bone posed
  outside its authored limits, which the no-teleport rule allows).
- G4. Same two things for IK bones: the twist range and the swing region
  (cone) about the bone, and the current pose inside it.
- G5. No cost when off, no steady-state allocations when on
  (AGENTS.md "Run-time Memory Allocation Discipline").

## Non-goals

- Editing limits by dragging the visual (a gizmo for ranges). The visual is
  read-only; a later plan can add handles once this geometry exists.
- Drives / motors (targets, stiffness, damping). A later extension can draw
  the drive position target as a tick on the same arc.
- Point-to-point drag constraints of the Physics tool (`Physics_drag_constraint`);
  those already draw their own drag line.

## Design

### D1. One pure geometry builder per constraint kind, one renderer

Following the precedent of `build_ik_drag_lines()` (pure function, caller-owned
buffer, unit-testable), the geometry is computed by pure functions that fill
a caller-owned, clear-and-reuse line buffer:

- `build_physics_joint_lines(const Physics_joint_line_input&, Joint_line_buffer&)`
- `build_ik_limit_lines(const Ik_limit_line_input&, Joint_line_buffer&)`

The inputs are plain values (world frames, the six `Constraint_axis_limit`s,
the current joint coordinates, sizes, colors), so the builders have no scene,
physics or renderer dependency and get tests in `editor_rig_tests` /
a physics-side test target. `Debug_visualizations` gathers the inputs from the
scene and submits the buffer to the line renderer. The buffer is a member of
`Debug_visualizations` (capacity kept across frames).

Location: `src/editor/tools/joint_visualization.{hpp,cpp}` for both builders
(they share the arc / cone / frame-triad primitives), next to
`bone_visualization.*`.

### D2. Physics joint visual

Frames. A live joint draws from `Joint_constraint_state` (the frames the
constraint was actually built with): `F_a = world(body A) * frame_in_a`,
`F_b = world(body B) * frame_in_b` (or `frame_in_b` itself when B is the
world). A pending joint (no constraint: inactive, no physics world, body not
built yet) draws from the `Joint` prim's frame nodes (`body_0` / `body_1`
world transforms) in a dimmed "pending" color, so authoring works with the
simulation off. This is the same two-node model `Joint_system` captures from,
so the pending and live visuals coincide at build time.

Elements, all sized by one world-space `joint_size` style value (or a
screen-size-derived value, see open question Q2):

- Anchor frames: a small axis triad at `F_a` and `F_b`; a line from each
  frame to its body's center of mass (identifies which body is which);
  world-anchored side drawn as a ground-hatch marker.
- Translation axes (0..2), in `F_a`'s basis at `F_a`'s origin:
  - free: a long thin line along the axis (both directions),
  - limited: a thick segment from `min` to `max` with end ticks,
  - fixed: nothing (the triad already shows the point).
  The current offset `t = F_a.basis^T * (F_b.origin - F_a.origin)` is a dot.
- Rotation axes (3..5): drawn in the convention the backend enforces, which
  is NOT the rotation vector of R that `Joint_reach` uses as a
  simplification:
  - Jolt (`jolt_constraint.cpp`): six-DOF with `ESwingType::Pyramid`, twist
    about X, independent Y / Z swing limits in swing-twist decomposition.
    Twist range: an arc (pie outline) about F_a's X axis from `min` to `max`.
    Swing: the pyramid-bounded region traced by F_b's X axis, drawn as the
    boundary curve on a sphere of radius `joint_size` plus its four corner
    rays; a single free-or-limited swing axis degenerates to an arc.
  - Box3D (`box3d_constraint.cpp`): the joint kind classification decides
    (revolute: one arc about the hinge axis; spherical: a circular cone of
    the widest swing limit plus the twist arc). The builder input carries
    the enforced shape, and the backend supplies it, so the visual shows
    what Box3D simulates rather than what was authored; when those differ
    (the widest-swing cone) the style's "approximated" color marks it.
  - A free rotation axis draws a full thin circle; a fixed one nothing.
  - The current angle on each movable rotation axis is a radius line / dot
    on its arc, computed with the same decomposition the backend uses.
- Out of range (G3): the current-value marker switches to the
  `joint_violation_color` when it lies outside `[min, max]` by more than a
  small tolerance.

Which convention to draw needs one new query, not a second copy of the
backend's classification: `IConstraint` (or `IWorld`) exposes
`get_limit_shape()` returning the enforced rotation-limit shape
(`pyramid_swing_twist` / `revolute(axis)` / `cone_twist(half_angle)` /
`none`). The editor never re-derives backend rules. For pending joints (no
constraint yet) the shape comes from the same backend function applied to the
settings, which the backend exposes as a free function so both paths share it.

### D3. IK limit visual

Per bone node with any IK constraint (a local or inherited `Ik.lock_*`,
`Ik.limit_*`, or a `lock_rotation_*` channel-lock flag - exactly the inputs of
`resolve_constraint()` in `ik_drag.cpp`):

- Reference frame: `parent_world * rest_rotation` at the bone's head (the zero
  of the limits, `ik_settings.md` section 1).
- Twist axis: derived with the same rule the solver uses (`derive_twist_axis()`,
  closest local axis to the child direction). Outside a drag there is no
  chain, so the child direction is the bone's `Rig.tail` (bone-local, the
  vector `Bone_visualization` draws). During an IK drag the visual uses the
  drag's resolved `Ik_joint_constraint` (its `twist_axis` came from the chain's
  next joint), so what is drawn is what is being enforced. `derive_twist_axis()`
  and `resolve_constraint()` move from the anonymous namespace of `ik_drag.cpp`
  into `ik_solver.hpp` so the visualization calls the single definition.
- Swing region: the boundary of the clamp region in Blender's sin(theta/2)
  swing space, mapped back to directions: for each sample on the quadrant
  ellipse (radii `sin(limit/2)` per quadrant, per `constrain_local_rotation()`),
  form the swing quaternion, rotate the twist axis, and emit the point at
  bone length. One limited swing axis draws an arc (a fan); a locked swing
  axis draws only the plane it is pinned to. The sampling function lives in
  `ik_solver.cpp` next to the clamp (`sample_swing_limit_boundary()`), so the
  drawn boundary and the enforced clamp are one piece of math; a unit test
  asserts that every sampled boundary direction survives
  `constrain_local_rotation()` unchanged and that a direction pushed 1 degree
  outward is clamped.
- Twist range: an arc about the twist axis at the bone's head, from
  `limit_min` to `limit_max` (sin-half-angle mapped the same way).
- Current pose: the bone direction (a dot on the cone surface) and a twist
  tick; violation color when outside the authored region (allowed by the
  no-teleport extension, and worth seeing).
- Locked-everything (rigid) bones draw a small padlock-like square at the
  head instead of a degenerate cone.

### D4. Settings, style, UI

- `Debug_visualizations_settings` (per scene view, codegen,
  `debug_visualizations_settings.py`): two new `Visualization_mode` fields,
  `physics_joints` and `ik_limits` (off / selected / hovered / all, the same
  combo as `physics` and `skins`). "Selected" for a physics joint means the
  `Joint` prim or either frame node or either body is selected; for IK, the
  bone node. Version bump with `added_in`.
- `Debug_visualizations_style` (editor-global, codegen): `joint_size`,
  `joint_line_width`, `joint_frame_a_color`, `joint_frame_b_color`,
  `joint_free_color`, `joint_limit_color`, `joint_value_color`,
  `joint_violation_color`, `joint_pending_color`, `joint_approximated_color`,
  and the IK equivalents `ik_limit_swing_color`, `ik_limit_twist_color`
  (the existing `ik_*` drag colors stay separate). Version bump.
- Depth: drawn in the x-ray bucket like the IK drag chain, so a joint inside
  a mesh stays visible.
- The Debug Visualizations window gets the two combos; the style editor gets
  the new rows (`style_imgui`).

### D5. Update model and allocation

The visual is drawn in `Debug_visualizations::render()` from current state
each frame it is enabled, like every other debug visualization - it draws the
current pose, which changes every simulation step, so this is rendering, not
derived state kept in sync (AGENTS.md "No update each frame" does not apply).
No per-frame caching of settings is added: `Physics_joint_settings` mirrors
(`get_axis_limits()`) and `read_ik_settings()` are read at draw time. Scratch:
the `Joint_line_buffer` member and a member vector of sampled boundary points,
both cleared-not-reassigned. `read_ik_settings()` returns a small value type
and does not allocate; verify with Tracy's allocation zones on a scene with
the rigged figure and creation 21 (Newton's cradle).

## Slices

Each slice is one commit, built on `build_ninja_win_vulkan` (and the headless
tree for verification), with its tests and doc update.

1. **Builder primitives + physics translation/frames.** `joint_visualization.*`
   with triad / segment / arc / fan primitives, `build_physics_joint_lines()`
   for frames, body links and translation axes; settings + style fields;
   window combo. Unit tests on the builder output (counts, endpoints).
2. **Physics rotation limits.** `get_limit_shape()` on both backends plus the
   rotation arcs / pyramid / cone; current-value and violation markers. Tests:
   builder geometry per shape; backend shape classification for hinge, ball,
   cone, pyramid and fully fixed settings on Jolt and Box3D.
3. **IK limits.** Move `derive_twist_axis()` / `resolve_constraint()` to
   `ik_solver.hpp`; `sample_swing_limit_boundary()` with the clamp-agreement
   test; `build_ik_limit_lines()`; drag-time path using the drag's resolved
   constraints.
4. **Docs.** `doc/editor/tools.md` (`Debug_visualizations`) describes both
   visuals; `doc/editor/physics.md` and `doc/plans/rigging/rigging_tools.md`
   point to it; this plan is deleted and its README index row removed.

## Verification

- Unit tests as listed per slice (builder geometry; boundary-vs-clamp
  agreement; backend shape classification).
- Headless MCP (`doc/agents/editor_runs.md`): build creation 21 (Newton's
  cradle, unlimited hinges) and a scene with one limited hinge (+-80 deg) and
  one cone joint; enable `physics_joints = all`; `capture_screenshot` at rest,
  mid-swing (`physics_drag`) and after pulling a ball past its limit; READ each
  PNG. Expected: hinge arc spans exactly the authored range, current-angle
  marker tracks the ball, violation color appears only while pulled past.
- Same for the RiggedFigure arm (`arm_joint_L_1/2/3`) with an elbow hinge
  (swing-locked, twist-limited) and a shoulder cone; screenshot at rest and
  during `ik_drag`, with the boundary visibly containing the solved pose.
- Both backends: repeat the physics captures on
  `build_vs2026_vulkan_headless_box3d`.
- A committed `scripts/joint_visualization_verify.py` drives the above and
  checks MCP-reported joint coordinates against the authored ranges, so the
  check is repeatable (the acceptance numbers go in the script, not in a
  reviewer's eye).
- Tracy: zero allocations in `Debug_visualizations::render` steady-state with
  both modes on "all".

## Open questions

- Q1. Pending physics joints: draw them at all when the simulation has never
  been enabled? Recommendation: yes, dimmed - it is the authoring case.
- Q2. Fixed world-space `joint_size` vs. screen-constant size. Recommendation:
  world-space, defaulting to a fraction of the smaller body's bounding radius
  when unset, because the ranges must read against the bodies they move;
  revisit after the first screenshots.
- Q3. MCP: expose the drawn joint coordinates (`get_joint_state` returning
  `t`, the swing/twist angles and an in-range flag per axis) so
  verification does not depend on pixels. Recommendation: yes, in slice 2;
  it reuses the same decomposition the builder calls.
