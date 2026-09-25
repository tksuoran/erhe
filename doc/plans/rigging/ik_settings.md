# Per-Bone IK Settings and Channel Locks - Phase 2 Requirements

Status: in progress

Implemented; awaiting live-editor testing (`interactive_test_pass.md`).
This document covers Phase 2 of the rigging roadmap in `rigging_tools.md`,
building directly on Phase 1 (`fabrik_ik.md`, implemented).

Implementation order within Phase 2: **(1) this document** - per-bone IK
settings data model + Properties UI + serialization, with general transform
channel locks riding along, and the solver refactor they force - then
(2) pole target / swivel (`pole_target.md`), then (3) effector orientation
option and chain visualization (`ik_drag_options.md`). Items 2-3 appear
below only as scope markers (section 7).

## Motivation

Phase 1 IK bends a chain freely: elbows hyperextend, knees bend sideways,
wrists spin. Real limbs have joints with limited freedom. This phase gives
bones per-axis degree-of-freedom locks and rotation limits that the FABRIK
solver enforces, plus a general per-component transform lock usable on any
node (Blender `protectflag` equivalent). Both are authored in the Properties
window and persist with the scene.

## Functional requirements

### 1. Per-bone IK settings - data model

- The per-bone IK values are attached properties of the bone node itself,
  registered by the holder class `Ik`
  (`src/editor/scene/ik_properties.{hpp,cpp}`) with owner type `Ik`, holder
  type `erhe::scene::Node` and UI group "IK"
  (`doc/erhe/property_system.md` section 4.19). Their qualified names are
  `Ik.lock_x`, `Ik.lock_y`, `Ik.lock_z`, `Ik.limit_x`, `Ik.limit_y`,
  `Ik.limit_z`, `Ik.limit_min`, `Ik.limit_max`, `Ik.stiffness`,
  `Ik.rest_rotation`, `Ik.pole_target` and `Ik.pole_angle`; each is
  entry-stored and registered with `inherits = false`. A bone holding no
  local `Ik.*` value is unconstrained. `Ik_settings_data` is the record of
  one node's effective values, filled by
  `read_ik_settings(const erhe::scene::Node&)`, which the solver reads.
- A limit set shared by several bones is a Style holding the `Ik.*` values:
  a Style holds every class's properties (D30 of
  `doc/erhe/property_system.md`) and the style layer supplies the value to
  every bone assigned that style.
- Fields, per rotation axis X/Y/Z (all following Blender's `bPoseChannel`
  `ikflag` / `limitmin` / `limitmax` / `ikstiffness` shape):
  - `lock[3]` (bool, default false) - the axis does not rotate under IK at
    all (DOF lock).
  - `limit[3]` (bool, default false) + `min[3]` / `max[3]` (radians)  - 
    rotation about the axis is limited to [min, max] under IK. As in
    Blender (`rna_pose.cc` clamps `ik_min_*` to [-pi, 0] and `ik_max_*` to
    [0, pi]), `min` is constrained to [-pi, 0] (default -pi) and `max` to
    [0, pi] (default pi), so the rest angle 0 is always inside the limits by
    construction - UI clamps, import validation, and the JSON schema all
    state this. `lock` wins over `limit` on the same axis.
  - Enforcement semantics: limits and
    locks are authored per local axis but **enforced via swing/twist
    decomposition**, following Blender's solver segments
    (`IK_QSphericalSegment` / `IK_QElbowSegment` and their `EllipseClamp`)
    rather than independent Euler clamps - see section 4 for the exact scheme
    (per-joint twist axis, swing ellipse from the two swing-axis limits,
    twist range from the twist-axis limits).
  - `stiffness[3]` (float 0..0.99, default 0) - resistance to rotation
    about the axis; 0 = free. Capped below 1 (as Blender caps it at 0.99)
    so stiffness can never alias a hard DOF lock. **Inert in this slice**:
    the value exists, serializes and has its property row, but the
    solver ignores it until the constrained solver is proven stable; enforcement (per-iteration
    scale-down, see section 4) is a later slice.
- `rest_rotation` (quaternion): the reference orientation that defines the
  zero of the limits. The limited quantity is
  `rel = inverse(rest_rotation) * parent_from_node_rotation`, decomposed
  as swing and twist (section 4) - limits live in the joint's rest-local frame,
  matching Blender's `basis = rest_basis^-1 * (parent_pose^-1 * pose_mat)`
  (`iksolver_plugin.cc`).
  - Axis meaning is rig-dependent: erhe joints are arbitrary glTF nodes
    with no guaranteed bone-axis convention (unlike Blender's bone = local
    Y), so *which* rest-local axis is the bend axis of an elbow depends on
    how the rig was authored. The twist axis is derived per joint (section 4);
    the UI and docs must not pretend a universal convention exists.
  - Twist: each solver step is a twist-free shortest arc in WORLD space,
    but composed onto a bent parent chain it turns the joint about its own
    twist axis relative to the rest frame (measured: a three-bone chain
    locked on Y and Z and dragged out of its plane ended with a 0.25
    quaternion Y component on the middle bone, which also leaked into Z
    through swing o twist). A lock or limit on the twist axis is therefore
    enforced in the solve (section 4); pre-existing twist is kept at drag
    start by the no-teleport rule. Twist is a distinct solver DOF, as in
    Blender, rather than an Euler component.
  - `Ik.rest_rotation` has a per-object default (D31 `compute_default`, no
    creation-time capture): the orthonormalized rotation of
    `inverse(world_from_bind(parent)) * world_from_bind(joint)` when the
    node and its parent node are joints of the same `erhe::scene::Skin`,
    and identity otherwise. The first skin in `Scene::get_skins()` order
    that lists both is used, so a node several skins list has one
    deterministic answer. The lookup is
    `erhe::scene::get_bind_pose_local_rotation(const Node&) ->
    std::optional<glm::quat>` (`erhe_scene/skin.{hpp,cpp}`). A local value
    overrides the default, so the bone tracks its bind pose until a rest
    orientation is authored.
  - Authored from Properties with the "Set rest from current pose" button
    (section 5), which writes the bone's current local rotation as the
    local value - needed because there is no rest-pose store on nodes yet
    (that is Phase 3's rest pose model, which can later supersede this
    value).
- Each `Ik.*` property's `visible_when` is "the object is a Node carrying
  `Item_flags::bone`", so the D12 listing rule offers the rows on bones. A
  non-bone node holding a local `Ik.*` value still lists it, and the value
  is inert there.
- Interaction with `Item_flags::ik_lock` (Phase 1): unchanged. `ik_lock`
  terminates the chain; the `Ik.*` values constrain a joint *inside* the
  chain.
  Locking all three axes is not the same as `ik_lock` - a fully DOF-locked
  joint still transmits the chain through itself rigidly rather than ending
  it.

### 2. General transform channel locks

- Nine new item flag bits: `lock_translation_x/y/z`, `lock_rotation_x/y/z`,
  `lock_scale_x/y/z` in `erhe::Item_flags` (`src/erhe/item/erhe_item/
  item.hpp`: bits, `c_bit_labels`, `count`), each registered in the
  persistent-flag allowlist (`src/erhe/gltf/erhe_gltf/gltf_item_flags.cpp`)
  so they ride the existing `ERHE_node.flags` serialization, and they work on
  **any** item, not just bones.
- Semantics: a locked component of the node's **local** (parent-from-node)
  transform does not change through interactive editing:
  - Transform tool: all four delta-application choke points
    (`Transform_tool::adjust`, `adjust_translation`, `adjust_rotation`,
    `adjust_scale` in `src/editor/transform/transform_tool.cpp`) mask the
    locked components of each entry's new local transform back to the
    drag-start value, next to the existing `lock_viewport_transform`
    checks. The generic matrix path (`adjust`) decomposes per entry to
    apply the mask.
  - Rotation locks mask local-rotation change per Euler XYZ component of
    the delta from the drag-start local rotation.
    Exactness caveat: masking one Euler component of an arbitrary
    3-D rotation is inherently approximate for large deltas; requirement
    is only that a locked axis's Euler angle (in the node's XYZ
    decomposition) stays at its drag-start value.
  - Numeric editing: the Transform window's TRS fields
    (`Transform_tool::transform_properties` edit lambdas and the
    `apply_translation_edit` / `apply_rotation_edit` / `apply_scale_edit` /
    `apply_skew_edit` commit paths) disable the locked components' widgets
    and refuse locked-component changes at commit time (the commit-side
    check also covers MCP callers).
  - IK: a `lock_rotation_*` bit on a chain joint acts as an IK DOF lock on
    that axis, exactly as `Ik.lock_x` .. `Ik.lock_z` do (the two OR together  - 
    matching Blender's Auto-IK, which turns `protectflag` rotation locks
    into temporary IK DOF locks across the chain). Frame note: inside the
    IK solve, OR-ed locks operate in the limits frame and map onto the
    joint's swing/twist axes per section 4 (a joint constrained by channel-lock flags alone takes its
    drag-start local rotation as the rest, per the rest-frame rule of
    section 3), while the Transform-tool masking
    above uses the plain local Euler XYZ frame by design.
    `lock_translation_*` needs no IK handling at all: IK never changes
    any chain joint's local translation, the effector's included (Phase 1
    invariant), so the lock is never violated by a solve.
- Not in scope: enforcing locks against animation playback, physics
  write-back, or direct programmatic `set_*` calls. Locks are an
  interactive-editing guard, like Blender's, not a hard invariant.
- UI: the nine bits are registered bridged boolean properties of `Node`
  (`Node::lock_translation_x_property` .. `lock_scale_z_property`,
  `src/erhe/scene/erhe_scene/node.cpp`, over
  `Item_base::register_flag_bit_property`), so the Properties window draws
  them on its registered path as a "Channel Locks" group ("Translation X"
  .. "Scale Z") for nodes, undoable through `Property_set_operation` and
  reachable from MCP `set_item_property`; the developer-mode flag list
  still picks the bits up from `c_bit_labels`.

### 3. Solver interface

Constrained solving forces `fabrik_solve` (today a pure positional free
function, `src/editor/transform/ik_drag.cpp`) to know joint orientations
and constraints. Rather than growing its signature ad hoc, factor per the
plan:

- `Ik_chain` (POD): per joint - world position, segment length to child,
  drag-start local rotation (parent space), drag-start parent world
  rotation, and resolved constraint data (locks OR-ed from both sources,
  limit min/max, rest rotation, and the derived twist-axis /
  swing-axes assignment per section 4); plus effector target and solver
  parameters (tolerance, max iterations).
- `Ik_solver` interface: `solve(Ik_chain&) -> bool` - chain in, posed
  chain out (solved world positions and, for constrained joints, solved
  local rotations). `Fabrik_solver` is the first implementation; a
  damped-least-squares Jacobian solver can be swapped in later if
  constrained FABRIK proves unstable (the plan's stated fallback,
  matching Blender's SDLS solver).
- `Ik_drag` keeps chain discovery, drag-start capture (now also resolving
  each joint's constraint and rest frame from the node's `Ik.*` values plus
  its channel-lock flags, in `Ik_drag::begin` ->
  `resolve_constraint`), write-back, and effector-orientation restore; it
  calls the solver through the interface.
- Rest-frame rule (`resolve_constraint`, `ik_drag.cpp`): a joint with any
  `Ik.lock_*` or `Ik.limit_*` on takes the effective `Ik.rest_rotation` as
  the zero of its limits - a fixed frame, so the limits do not drift with
  the pose. A joint constrained by channel-lock flags alone takes its
  drag-start local rotation instead, because a channel lock states only
  that an axis must not move.
- The unconstrained path must behave bit-for-bit as Phase 1 (a chain whose
  joints hold no lock or limit, from either source, takes the
  constraint-free code path - no behavior or performance regression).
- Unit tests: the solver factoring makes the constrained solve testable
  headlessly; add tests next to the code covering: unconstrained
  equivalence with Phase 1 expectations, the swing-twist decomposition
  itself (round-trip, twist-axis derivation on off-convention rigs), a
  hinge chain (one swing axis locked, bend axis limited), a limited elbow
  that stops at its limit, an unreachable target with limits, the
  extended-region no-teleport behavior from an out-of-limit start pose,
  and degenerate zero-length segments with constraints present.

### 4. Constrained FABRIK - enforcement

Per-iteration reprojection, after Aristidou & Lasenby's constrained FABRIK
formulation adapted to swing/twist limits:

- **Pass-frame policy** (this choice is deliberate and normative):
  constraints are enforced only during the **backward** (root->tip) pass,
  where parent world orientations are well-defined - they are propagated
  root->tip within the pass as each joint's clamped rotation is computed.
  The forward (tip->root) pass runs unconstrained, exactly as in Phase 1;
  during it a joint's parent has not been repositioned yet, so no
  consistent frame exists to clamp in. This is a known practical
  simplification of the paper's formulation; the backward pass ends every
  iteration, so the pose handed to write-back always satisfies the
  constraints.
- **Twist axis and swing axes, derived per joint** (at drag start, from
  rest data - stable for the whole gesture): the twist axis is the
  rest-local coordinate axis (+/-X, +/-Y, or +/-Z) closest to the joint's
  rest-pose direction toward its child **in the discovered chain** (the
  next chain joint - joints may have several children), expressed in the
  joint's rest-local frame (ties broken deterministically in X, Y, Z
  priority order); the remaining two local axes are the swing axes. For
  Blender-convention rigs this yields Y-twist automatically; for other
  rigs it yields whatever the rig's bone axis actually is. A zero-length
  child offset has no direction: such a joint is treated as
  unconstrained, consistent with the zero-length-segment skip rule.
- In the backward pass, after positioning joint *i*'s child, compute the
  local rotation joint *i* would need (shortest-arc from its drag-start
  child direction, exactly the Phase 1 write-back math, under the
  propagated parent world orientation), form
  `rel = inverse(rest_rotation) * local_rotation`, and decompose it as
  **swing  o  twist** (twist = the rotation component about the twist
  axis; swing = the remaining rotation moving the twist axis itself  - 
  the standard swing-twist decomposition). Then:
  - **Twist** is parameterized by its sin(theta/2) component about the
    twist axis (canonical w >= 0). A twist-axis lock pins it to the
    drag-start value; a twist-axis limit clamps it to the mapped interval,
    extended to contain the drag-start value (no teleport); otherwise it
    is left as solved. Twist turns about the child direction, so clamping
    it leaves this joint's child in place and changes only the frame the
    descendants are solved in.
  - **Swing** is parameterized in Blender's clamp space, adopted here
    normatively: the two swing-quaternion components along the swing
    axes - sin(theta/2)-scaled, per `SphericalRangeParameters` /
    `ComputeSwingMatrix` in Blender's `IK_Math.h` - with authored angle
    limits mapped into that space via sin(lambda/2) including the sign/swap
    mapping of `IK_QSegment::SetLimit` (`m_min = -sin(lambdamax/2)` etc.).
    This space is singularity-free below 180 deg swing; angle space is NOT
    used for clamping. Call the two components (s1, s2).
  - **Which clamp applies** (Blender's rule, adopted verbatim): both
    swing axes limited -> per-quadrant ellipse clamp of (s1, s2) with
    radii from the mapped limits, asymmetric limits handled per quadrant
    as Blender's `EllipseClamp` does; exactly one swing axis limited ->
    plain 1-D interval clamp of that component alone, the other free;
    only the twist axis limited -> the swing is left as solved.
  - **Locks**: a locked swing axis pins its component to the drag-start
    value, and the clamp NEVER modifies a pinned component. With one
    component pinned, the free component is clamped to the ellipse's
    cross-section at the pinned value (the 1-D interval where that line
    intersects the quadrant ellipse); if the cross-section is empty, the
    free component goes to the nearest boundary point of the extended
    region (next bullet). Both swing axes locked -> the swing is fully
    pinned; only twist remains. A swing-axis lock together with a
    twist-axis lock is a hinge about the remaining axis.
  - Recompose clamped-swing  o  twist, and reposition the child onto the
    direction the clamped rotation actually allows (at unchanged segment
    length) before the pass continues. Positions and orientations
    therefore stay consistent inside the solve, instead of constraints
    being slapped on at write-back only.
- **No-teleport rule made precise**: the constraint region used during a
  drag is the authored region **extended to include the drag-start
  state** - per locked swing axis, the pinned value is the drag-start
  component itself; per limited swing quadrant, compute the drag-start
  point's ellipse norm in its quadrant and, if it exceeds 1, scale
  **both** of that quadrant's radii uniformly by that factor (for the
  1-D single-axis clamp, extend the interval to include the drag-start
  component). Deterministic, and monotone in the ellipse norm. A pose
  that starts outside its authored limits (moved after authoring, or
  twist the solver cannot correct - see section 1) may move toward or into the
  legal region during the drag but never further outside it. This is
  deterministic, monotone, time-independent, and preserves Phase 1's
  "drag back to start restores the start pose" property; the drag-start
  pose is never teleported.
- Stiffness (deferred to a later slice; the field is inert in this one  - 
  section 1): when implemented, scales down the per-iteration angular change of
  the joint by (1 - stiffness) before clamping, biasing the solve toward
  moving less-stiff joints first. Purely a solve-quality knob; no
  correctness requirement beyond stability.
- Termination and fallbacks: same tolerance / max-iteration scheme as
  Phase 1. With constraints, the target is often unreachable; the solver
  must converge to a stable best-effort pose (no oscillation between
  iterations - if the error stops decreasing, stop).
- Fully locked joints (all axes) transmit rigidly: their local rotation
  never changes; the chain effectively has a rigid multi-segment link.
  Chains whose every non-effector joint is fully locked leave the pose
  unchanged (the drag does nothing IK-wise; the gizmo still moves as in
  the Phase 1 out-of-reach case).
- Write-back: unchanged sequential root-to-effector scheme from Phase 1,
  except constrained joints take their solved local rotation from the
  solver output (already clamped) rather than recomputing shortest-arc
  from positions; the same cache-refresh discipline applies
  (`Node::update_world_from_node` along the chain - see the Phase 1
  implementation notes).
- Numerical safety: same NaN/degeneracy rules as Phase 1 (zero-length
  segments skipped; antiparallel case deterministic). The swing-twist
  decomposition is singular only at 180 deg swing (the antiparallel case)  - 
  resolve it with the same deterministic axis pick as Phase 1's
  antiparallel rule; no gimbal issues exist elsewhere in swing/twist.
  Out-of-limit start poses are handled by the extended constraint region
  above - never by snapping the pose.

### 5. Properties UI

- The rows are the generic registered-property rows of the bone node
  itself, in group "IK" (`doc/erhe/property_system.md` section 4.19) -
  Lock X/Y/Z and Limit X/Y/Z checkboxes, Limit Min / Limit Max as vec3 rows
  edited in degrees and stored in radians (coerced per component to
  [-180 deg, 0 deg] and [0 deg, 180 deg] per section 1), Stiffness
  (shown; the value is inert - section 1), Rest Rotation as Euler
  degrees, and the pole rows of `pole_target.md` R17. The `visible_when` of
  section 1 decides which nodes show them, so there is nothing to add and
  nothing to gate.
- The "Set rest from current pose" button (section 1) is a
  `Property_row_action` on `Ik.rest_rotation`, registered by the
  `Properties` constructor (`src/editor/windows/properties.cpp`): the row
  "Set Rest" directly below Rest Rotation in the "IK" group. With several
  bones selected one press writes every bone's rest as one undo step.
- Undo: every row edit, the "Set rest from current pose" button and the MCP
  `set_item_property` tool record one `Property_set_operation` (the local
  state before and after), so one completed edit = one undo step.
  Channel-lock toggles are registered `Node` properties and record the same
  operation (section 2).
- A limit set shared by several bones is a Style holding the `Ik.*` values
  (section 1); the values themselves do not inherit down the node chain, so
  a bone's limits are its own unless a style supplies them.

### 6. Serialization

- The `Ik.*` local values ride the bone node's `ERHE_node` `properties` map
  by their qualified names (D14 of `doc/erhe/property_system.md`), the
  carriage every attached property uses. A node holding no local `Ik.*`
  value writes nothing.
- `Ik.pole_target` is an object reference, so it additionally rides
  `property_node_refs` beside the map: the glTF node index of the pole in
  this file's own node table, which is what the reader resolves. The
  reference's path form in `properties` stays the readable form and the
  fallback for a target the file has no node for.
  `doc/gltf_extensions/ERHE_node.md` owns both keys' specification -
  written form, read order, and what an unresolvable index or path does.
- On import the `properties` map is the node's complete local set, so a
  value the map does not name is cleared and a bone whose values come from
  a style keeps reading the style after a reload.
- Channel-lock flags serialize as flag names through the existing
  `ERHE_node.flags` allowlist (section 2).
- Prefabs / clone: entry-stored values copy with the node (D10 of
  `doc/erhe/property_system.md`), so a cloned or prefab-instantiated bone
  keeps its `Ik.*` values and names the same pole node.
- USD has no form for rig data: `save_scene_usd` counts the nodes holding a
  local `Ik.*` value and logs one warning per save naming that count
  (`pole_target.md` R26). A USD form is Phase 4 work.

### 7. Scope markers for the rest of Phase 2 (not in this slice)

- **Pole target / swivel control** - implemented, specified in
  `pole_target.md` (the `Ik.pole_target` reference and the `Ik.pole_angle`
  value of a joint node, carried as in section 6).
- **Effector orientation option** (keep world orientation vs follow last
  segment) - Transform tool setting; implemented, specified in
  `ik_drag_options.md` section 1.
- **Chain visualization** during drag (chain, root, effector and pole) via
  `erhe::renderer::Primitive_renderer` - implemented, specified in
  `ik_drag_options.md` section 2.
- Phase 1 deferred open questions (mid-chain drag feel, incremental vs
  from-start solve) - revisit with constrained-solver experience.

## Out of scope

- Persistent IK constraints (target node stored in the scene; chains here
  are still discovered per drag) - Phase 4.
- A real rest-pose model on nodes (Phase 3); `Ik.rest_rotation` is the
  stopgap and migrates there later.
- Enforcing channel locks against animation, physics, or programmatic
  transform writes.
- Stiffness UI and solver enforcement (deferred wholesale to a later
  slice - section 1); weighting schemes beyond the simple scale-down.
- Translation/scale IK limits (IK only rotates; loc/scale limits would be
  constraint-stack material, Phase 4's Limit Location/Scale).

## Acceptance criteria

1. On a test rig with a known bend axis (e.g. the checked-in test asset;
   "X" below means that rig's bend axis), set an elbow bone's bend-axis
   limit to [-150 deg, 0 deg]: dragging the hand can no longer hyperextend
   the elbow past straight, and bending stops at 150 deg; clearing the
   bone's local `Ik.*` values restores Phase 1 behavior exactly.
2. Set a knee to hinge on the same rig (lock the non-bend swing axis and
   the twist axis, limit the bend axis):
   dragging the foot bends the knee only about the bend axis within its
   limits, reaching targets in the hinge plane on the current bend side;
   targets the heuristic cannot reach (including bend-sign flips  - 
   constrained FABRIK is best-effort, not globally complete) settle
   stably at a best-effort pose with no visible oscillation.
3. Lock all three rotation axes on a mid-chain bone: the two segments it
   joins move as one rigid link; the rest of the chain still solves.
4. `lock_rotation_x` (channel lock, no local `Ik.*` value) on a chain
   bone whose derived twist axis is NOT X (i.e. X is a swing axis on the
   test rig) behaves
   in IK like an X DOF lock; the same flag also prevents the Rotate tool
   and the Transform window's X rotation field from changing that axis,
   while Y/Z still work.
5. `lock_translation_*` on a node: Move tool drags and numeric edits leave
   the locked component unchanged, on bones and non-bones alike.
6. One drag gesture with constraints active still produces exactly one
   undo step restoring the pre-drag pose; one completed Properties edit of
   IK settings is one undo step; toggling a Locks-row checkbox is one undo
   step.
7. IK settings and channel locks survive a glTF save/load round trip
   (`ERHE_node` `properties` / `property_node_refs` and `ERHE_node.flags`
   respectively); a file saved without them loads unchanged in behavior; a
   third-party glTF viewer ignores `ERHE_node` without error (extension,
   not required).
8. Solver unit tests (section 3) pass; a chain with no constraints follows the
   identical code path and produces identical results to Phase 1.
9. Zero-length segments, limits excluding the current pose, and
   180 deg-swing (antiparallel) configurations produce no NaNs, crashes, or
   frame-to-frame pose jumps.

## Resolved design decisions

1. **Where the data lives**: attached properties of the bone node itself,
   group "IK" (section 1). Pole and chain data of later slices goes with
   the chain or the effector, not into a per-joint blob.
2. **Limit frame default**: the bind pose when the bone and its parent are
   joints of one skin, identity otherwise, as a computed default
   (section 1), authorable from Properties.
3. **Channel locks**: 9 `Item_flags` bits (bits 42-50), chosen for
   zero-cost persistence through `ERHE_node.flags` and any-item
   applicability.
4. **Stiffness**: serialized but inert in this slice (section 1); solver
   enforcement and UI wait until the constrained solver is proven.
5. **Limit parameterization**: swing/twist (Blender-solver style -
   per-joint derived twist axis, swing ellipse with per-quadrant radii
   from the per-axis limits, twist interval from the twist-axis limit), NOT
   independent Euler clamps. See section 1 and section 4. The authored data
   stays per-axis min/max, so this choice affects enforcement only.
6. **Sharing a limit set**: a Style holding the `Ik.*` values (section 1),
   rather than inheritance down the node chain - a bone's limits belong to
   that bone, and a chain of bones has no shared limit by construction.

## Key locations

- Values - `editor::Ik` (`src/editor/scene/ik_properties.{hpp,cpp}`): the
  registrations, `read_ik_settings`, `get_ik_pole_target` /
  `set_ik_pole_target`, and `has_local_ik_value`, which a writer with no
  form for IK data counts. The bind-pose lookup behind the rest default is
  `erhe::scene::get_bind_pose_local_rotation` (`erhe_scene/skin.{hpp,cpp}`).
- Channel locks - `Item_flags::lock_translation_x` .. `lock_scale_z`
  (bits 42-50, `item.hpp`, with `lock_*_mask` composites), persisted via
  `gltf_item_flags.cpp`; enforced by `enforce_channel_locks`
  (`transform_tool.cpp`) at every Transform tool delta path, the
  `apply_*_edit` commit paths, and the MCP direct set-transform action;
  per-component toggles are the registered `Node` properties
  `lock_translation_x` .. `lock_scale_z` (bridged flag bits, Properties
  group "Channel Locks"); locked widgets are greyed out in the Transform
  window (local single-node mode; rotation relies on commit-side masking).
- Solver - `src/editor/transform/ik_solver.{hpp,cpp}`: `Ik_chain` /
  `Ik_solver` / `Fabrik_solver`; constrained enforcement in
  `constrain_local_rotation` (swing/twist decomposition, sin(half-angle)
  clamp space, per-quadrant ellipse, pinned locks, no-teleport extension).
  Unconstrained chains take the untouched Phase 1 `fabrik_solve` path.
  Unit tests: `src/editor/transform/test/test_ik_solver.cpp`
  (`editor_ik_solver_tests` target, `ERHE_BUILD_TESTS=ON` trees).
- IK integration - `Ik_drag::begin` resolves per-joint constraints
  (`resolve_constraint`: the node's `Ik.*` values OR its channel-lock
  flags, the rest-frame rule of section 3, twist axis via
  `derive_twist_axis`; any lock or limit, the twist axis included, routes
  the chain into the constrained solver); constrained write-back sets
  solver-produced local rotations directly.
- Properties UI - the generic rows of group "IK" plus the "Set Rest" row
  action (`properties.cpp`). Property tests:
  `src/editor/transform/test/test_ik_properties.cpp`.

## Standing traps

- Euler-component channel-lock masking must pick the Euler branch of the
  new rotation nearest the reference decomposition before masking -
  `glm::eulerAngles` jumps branches past ~90 degrees and cross-branch
  masking snaps the node to a wrong orientation.
- The constrained solver models chains as pure rotations with world
  segment lengths: correct under uniform scale, best-effort under
  non-uniform scale (same policy as Phase 1's write-back).
- Programmatic transform writes outside the covered paths (animation,
  physics) bypass channel locks by design (section 2).
