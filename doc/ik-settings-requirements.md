# Per-Bone IK Settings and Channel Locks - Phase 2 Requirements

Status: reviewed (fact-check + quality review passed, 2026-08-23);
implemented 2026-08-25 (see Implementation status at the end); awaiting
live-editor testing.
This document covers Phase 2 of the rigging roadmap in `rigging-tools-plan.md`,
building directly on Phase 1 (`fabrik-ik-requirements.md`, implemented).

Decisions already made with the user (2026-08-23):

- Per-bone IK settings serialize through a **new `ERHE_rig` glTF extension**
  (not `extras`, not a widening of `ERHE_node`).
- Implementation order within Phase 2: **(1) this document** - per-bone IK
  settings data model + Properties UI + serialization, with general transform
  channel locks riding along, and the solver refactor they force - then
  (2) pole target / swivel, then (3) effector orientation option and chain
  visualization. Items 2-3 get their requirements appended here (or in a
  companion doc) when their turn comes; they appear below only as scope
  markers.

## Motivation

Phase 1 IK bends a chain freely: elbows hyperextend, knees bend sideways,
wrists spin. Real limbs have joints with limited freedom. This phase gives
bones per-axis degree-of-freedom locks and rotation limits that the FABRIK
solver enforces, plus a general per-component transform lock usable on any
node (Blender `protectflag` equivalent). Both are authored in the Properties
window and persist with the scene.

## Functional requirements

### 1. Per-bone IK settings - data model

- New node attachment `Ik_settings` (working name; editor domain,
  `src/editor/scene/node_ik_settings.{hpp,cpp}` next to `Node_physics`),
  a pure data attachment in the mold of `erhe::scene::Layout_item`: public
  fields, no runtime behavior, custom clone constructor only.
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
  - Enforcement semantics (decided with the user, 2026-08-23): limits and
    locks are authored per local axis but **enforced via swing/twist
    decomposition**, following Blender's solver segments
    (`IK_QSphericalSegment` / `IK_QElbowSegment` and their `EllipseClamp`)
    rather than independent Euler clamps - see section 4 for the exact scheme
    (per-joint twist axis, swing ellipse from the two swing-axis limits,
    twist range from the twist-axis limits).
  - `stiffness[3]` (float 0..0.99, default 0) - resistance to rotation
    about the axis; 0 = free. Capped below 1 (as Blender caps it at 0.99)
    so stiffness can never alias a hard DOF lock. **Inert in this slice**
    (decided with the user): the field exists and serializes so the
    `ERHE_rig` schema is stable from day one, but the UI hides it and the
    solver ignores it until the constrained solver is proven stable;
    enforcement (per-iteration scale-down, see section 4) is a later slice.
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
  - Twist caveat: the solver produces shortest-arc rotations with roll
    preserved (Phase 1 section 4), so IK never *generates* twist about the
    segment axis. A lock or limit on the twist axis is therefore a no-op
    during the solve - and it must NOT cause pre-existing twist to be
    clamped at drag start (see section 4's no-teleport rule). This mirrors
    Blender, where twist is a distinct solver DOF rather than an Euler
    component.
  - Captured automatically when the attachment is created: from the bind
    pose when the node is a joint of an `erhe::scene::Skin` whose parent
    node is a joint of the same skin - the local bind rotation is the
    rotation of `world_from_bind(parent)^-1 * world_from_bind(joint)`;
    if the node belongs to several skins, the first skin found is used,
    deterministically. Otherwise (parent not a joint of the same skin, or
    no skin), the node's current `parent_from_node` rotation is captured.
  - Re-capturable from the Properties section ("Set rest from current
    pose" button) - needed because there is no rest-pose store on nodes
    yet (that is Phase 3's rest pose model, which can later supersede this
    field).
- The attachment is meaningful only on bone nodes (`Item_flags::bone`); the
  add-attachment gate (see section 5) restricts creation accordingly. If one ends
  up on a non-bone node (e.g. after flag changes), it is inert but harmless.
- Interaction with `Item_flags::ik_lock` (Phase 1): unchanged. `ik_lock`
  terminates the chain; `Ik_settings` constrains a joint *inside* the chain.
  Locking all three axes is not the same as `ik_lock` - a fully DOF-locked
  joint still transmits the chain through itself rigidly rather than ending
  it.

### 2. General transform channel locks

- Nine new item flag bits: `lock_translation_x/y/z`, `lock_rotation_x/y/z`,
  `lock_scale_x/y/z` in `erhe::Item_flags` (`src/erhe/item/erhe_item/
  item.hpp`: bits, `c_bit_labels`, `count`), each registered in the
  persistent-flag allowlist (`src/erhe/gltf/erhe_gltf/gltf_item_flags.cpp`)
  so they ride the existing `ERHE_node.flags` serialization - no `ERHE_rig`
  involvement, and they work on **any** item, not just bones.
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
    that axis, exactly as `Ik_settings::lock` does (the two OR together  - 
    matching Blender's Auto-IK, which turns `protectflag` rotation locks
    into temporary IK DOF locks across the chain). Frame note: inside the
    IK solve, OR-ed locks operate in the limits frame and map onto the
    joint's swing/twist axes per section 4 (a lock on the twist axis is a solve
    no-op; when the joint has no `Ik_settings` attachment, its drag-start
    local rotation serves as the rest), while the Transform-tool masking
    above uses the plain local Euler XYZ frame by design.
    `lock_translation_*` needs no IK handling at all: IK never changes
    any chain joint's local translation, the effector's included (Phase 1
    invariant), so the lock is never violated by a solve.
- Not in scope: enforcing locks against animation playback, physics
  write-back, or direct programmatic `set_*` calls. Locks are an
  interactive-editing guard, like Blender's, not a hard invariant.
- UI: the existing "Locks" row in the Properties window's generic
  item-details block (`properties.cpp`, `add_entry("Locks", ...)`,
  shown for any item passing `show_item_details`) grows per-component
  toggles (a 3x3 grid of small checkboxes under Location / Rotation /
  Scale), in addition to the developer-mode flag list which picks the new
  bits up automatically from `c_bit_labels`.

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
- `Ik_drag` keeps chain discovery, drag-start capture (now also capturing
  constraint data and rest frames from `Ik_settings` + channel-lock flags,
  in `Ik_drag::begin`), write-back, and effector-orientation restore; it
  calls the solver through the interface.
- The unconstrained path must behave bit-for-bit as Phase 1 (a chain with
  no settings attachment and no lock flags takes the constraint-free code
  path - no behavior or performance regression).
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
  - **Twist** is left untouched: the solver never generates it (section 1), so
    twist locks/limits do not participate in the solve.
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
    only the twist axis limited -> solve no-op (section 1).
  - **Locks**: a locked swing axis pins its component to the drag-start
    value, and the clamp NEVER modifies a pinned component. With one
    component pinned, the free component is clamped to the ellipse's
    cross-section at the pinned value (the 1-D interval where that line
    intersects the quadrant ellipse); if the cross-section is empty, the
    free component goes to the nearest boundary point of the extended
    region (next bullet). Both swing axes locked -> the swing is fully
    pinned; only twist would remain, and the solver never generates it.
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

- `Ik_settings` registered in the attachment-type catalog
  (`src/editor/scene/attachment_types.cpp`): key `ik_settings`, display
  name "IK Settings", `can_add` gate = node has `Item_flags::bone`. This
  makes it appear in the Properties "Add Attachment" popup, the Hierarchy
  window, and the MCP scene actions for free.
- Properties section (`Properties::ik_settings_properties` in
  `src/editor/windows/properties.cpp`, dispatched from `item_properties`
  like `layout_item_properties`): per axis - Lock checkbox; Limit checkbox
  enabling a min/max degree pair (the `optional_float_editor` /
  `physics_joint_settings_properties` widget pattern; edited in degrees,
  stored in radians; min clamped to [-180 deg, 0 deg], max to [0 deg, 180 deg] per
  section 1); no Stiffness widget in this slice (the field is inert - section 1); plus the "Set rest from current pose" button (section 1).
- Undo: attachment add/remove already routes through
  `Node_attach_operation`. Field edits get an `Ik_settings_change_operation`
  (before/after copy of the POD, modeled on `Material_change_operation`
  with the same `Editor_state` latch pattern), so one completed edit = one
  undo step. Channel-lock toggles in the Locks row go through
  `Item_set_flag_bits_operation` so they are undoable too (the
  developer-mode flag list may keep its current direct-set behavior).

### 6. Serialization - `ERHE_rig`

- New editor-domain per-node glTF extension `ERHE_rig`, written and read in
  `src/editor/parsers/gltf_extensions_export.cpp` /
  `gltf_extensions_import.cpp` exactly on the `ERHE_layout` pattern
  (payload string via `append_members` into
  `extension_payloads.nodes[node]`, manual `extensions_used` declaration;
  import via `find_extension` / `parse_extension_object` in a new
  `import_rigs()` called from `import_gltf_editor_state()`). The generic
  `ERHE_*` capture callback in `erhe::gltf` needs no changes.
- Emitted for every node that carries an `Ik_settings` attachment  - 
  including an all-default one, since the attachment's presence is itself
  user intent; a node without the attachment writes nothing.
- Payload shape (one `ik` object now; room for future rig data - pole
  targets, per-chain settings - as sibling keys in later slices/phases):

  ```json
  "ERHE_rig": {
      "ik": {
          "lock":          [false, false, true],
          "limit":         [true,  false, false],
          "min":           [-2.62, -3.14159274, -3.14159274],
          "max":           [0.0,    3.14159274,  3.14159274],
          "stiffness":     [0.0, 0.0, 0.0],
          "rest_rotation": [0.0, 0.0, 0.0, 1.0]
      }
  }
  ```

  Angles in radians; `rest_rotation` as glTF-order quaternion [x, y, z, w].
  Absent fields take defaults on import (forward compatibility); unknown
  fields are ignored with a log warning.
- Documentation set, mirroring `ERHE_layout`: spec page
  `doc/gltf_extensions/ERHE_rig.md`, schema
  `doc/gltf_extensions/schema/ERHE_rig.schema.json`, a table row in
  `doc/gltf_extensions/README.md`, and the extension inventory tables in
  `doc/scene_serialization.md`.
- Channel-lock flags serialize as flag names through the existing
  `ERHE_node.flags` allowlist (section 2), not through `ERHE_rig`.
- Prefabs / clone: the attachment clones with the node like other
  attachments (custom clone constructor), so instantiated prefabs keep
  their IK settings.

### 7. Scope markers for the rest of Phase 2 (not in this slice)

- **Pole target / swivel control** - next slice; requirements to be
  written after this slice lands (will add pole data to `ERHE_rig` if
  persistent).
- **Effector orientation option** (keep world orientation vs follow last
  segment) - Transform tool setting; small, after pole.
- **Chain visualization** during drag (highlight chain, root, later pole)
  via `erhe::renderer::Primitive_renderer` - after pole.
- Phase 1 deferred open questions (mid-chain drag feel, incremental vs
  from-start solve) - revisit with constrained-solver experience.

## Out of scope

- Persistent IK constraints (target node stored in the scene; chains here
  are still discovered per drag) - Phase 4.
- A real rest-pose model on nodes (Phase 3); `rest_rotation` in the
  attachment is the stopgap and migrates there later.
- Enforcing channel locks against animation, physics, or programmatic
  transform writes.
- Stiffness UI and solver enforcement (deferred wholesale to a later
  slice - section 1); weighting schemes beyond the simple scale-down.
- Translation/scale IK limits (IK only rotates; loc/scale limits would be
  constraint-stack material, Phase 4's Limit Location/Scale).

## Acceptance criteria

1. On a test rig with a known bend axis (e.g. the checked-in test asset;
   "X" below means that rig's bend axis), add an IK Settings attachment to
   an elbow bone and set the bend-axis limit to [-150 deg, 0 deg]: dragging the
   hand can no longer hyperextend the elbow past straight, and bending
   stops at 150 deg; removing the attachment restores Phase 1 behavior
   exactly.
2. Set a knee to hinge on the same rig (lock the non-bend swing axis and
   limit the bend axis - the twist axis needs no lock, IK never twists):
   dragging the foot bends the knee only about the bend axis within its
   limits, reaching targets in the hinge plane on the current bend side;
   targets the heuristic cannot reach (including bend-sign flips  - 
   constrained FABRIK is best-effort, not globally complete) settle
   stably at a best-effort pose with no visible oscillation.
3. Lock all three rotation axes on a mid-chain bone: the two segments it
   joins move as one rigid link; the rest of the chain still solves.
4. `lock_rotation_x` (channel lock, no IK Settings attachment) on a chain
   bone whose derived twist axis is NOT X (i.e. X is a swing axis on the
   test rig - a lock on the twist axis is a solve no-op per section 4) behaves
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
   (`ERHE_rig` and `ERHE_node.flags` respectively); a file saved without
   them loads unchanged in behavior; a third-party glTF viewer ignores
   `ERHE_rig` without error (extension, not required).
8. Solver unit tests (section 3) pass; a chain with no constraints follows the
   identical code path and produces identical results to Phase 1.
9. Zero-length segments, limits excluding the current pose, and
   180 deg-swing (antiparallel) configurations produce no NaNs, crashes, or
   frame-to-frame pose jumps.

## Resolved design decisions (with the user, 2026-08-23)

1. **Attachment name**: `Ik_settings` / "IK Settings". Pole/chain data in
   later slices goes elsewhere (chain/effector), not this per-joint blob.
2. **Limit frame default**: bind pose when available, else current local
   rotation at attachment creation (section 1), re-capturable from Properties.
3. **Channel locks**: 9 new `Item_flags` bits (bits 38-46), chosen for
   zero-cost persistence through `ERHE_node.flags` and any-item
   applicability; 17 bits remain free.
4. **Stiffness**: field serialized but inert in this slice (section 1); solver
   enforcement and UI deferred until the constrained solver is proven.
5. **Limit parameterization**: swing/twist (Blender-solver style  - 
   per-joint derived twist axis, swing ellipse with per-quadrant radii
   from the per-axis limits, twist untouched by the solve), NOT
   independent Euler clamps. See section 1 and section 4. The authored data stays
   per-axis min/max, so this choice affects enforcement only, not the
   `ERHE_rig` schema.

## Implementation status (2026-08-25)

Implemented as specified. Key locations:

- `Ik_settings` attachment - `src/editor/scene/node_ik_settings.{hpp,cpp}`
  (`Ik_settings_data` value struct for before/after undo copies); created
  via `Scene_commands::attach_new_ik_settings` (bind-pose rest capture in
  `capture_ik_rest_rotation`, `scene_commands.cpp`); registered in the
  attachment catalog (`attachment_types.cpp`, bone-gated).
- Channel locks - `Item_flags::lock_translation_x` .. `lock_scale_z`
  (bits 38-46, `item.hpp`, with `lock_*_mask` composites), persisted via
  `gltf_item_flags.cpp`; enforced by `enforce_channel_locks`
  (`transform_tool.cpp`, declared in `transform_tool.hpp`) at every
  Transform tool delta path, the `apply_*_edit` commit paths, and the MCP
  direct set-transform action; per-component toggles in the Properties
  "Channel Locks" row (undoable via `Item_set_flag_bits_operation`);
  locked widgets greyed out in the Transform window (local single-node
  mode; rotation relies on commit-side masking).
- Solver - `src/editor/transform/ik_solver.{hpp,cpp}`: `Ik_chain` /
  `Ik_solver` / `Fabrik_solver`; constrained enforcement in
  `constrain_local_rotation` (swing/twist decomposition, sin(half-angle)
  clamp space, per-quadrant ellipse, pinned locks, no-teleport extension).
  Unconstrained chains take the untouched Phase 1 `fabrik_solve` path.
  Unit tests: `src/editor/transform/test/test_ik_solver.cpp`
  (`editor_ik_solver_tests` target, `ERHE_BUILD_TESTS=ON` trees).
- IK integration - `Ik_drag::begin` resolves per-joint constraints
  (`resolve_constraint`: attachment fields OR channel-lock flags; twist
  axis via `derive_twist_axis`; a twist-axis-only constraint does NOT
  route the chain into the constrained solver - it would be a no-op that
  changed unreachable-target behavior); constrained write-back sets
  solver-produced local rotations directly.
- Serialization - `ERHE_rig` (spec `doc/gltf_extensions/ERHE_rig.md`,
  schema `doc/gltf_extensions/schema/ERHE_rig.schema.json`); export in
  `gltf_extensions_export.cpp`, import in `gltf_extensions_import.cpp`
  (`import_rigs`, per-element JSON type guards, range clamps).
- Properties UI - `Properties::ik_settings_properties`
  (`properties.cpp`): per-axis Lock / Limit (degrees) / Set-rest rows.
  Undo deliberately does NOT use the material inspect-latch pattern: a
  single-slot latch flaps and loses records when several Ik_settings
  render at once, and its retained initial state goes stale across undo.
  Instead every completed edit immediately queues one
  `Ik_settings_change_operation` with a before-copy captured at
  interaction start (`queue_ik_settings_change`).

Notes from the implementation review (all confirmed findings fixed):

- Euler-component channel-lock masking must pick the Euler branch of the
  new rotation nearest the reference decomposition before masking -
  glm::eulerAngles jumps branches past ~90 degrees and cross-branch
  masking snaps the node to a wrong orientation.
- The constrained solver models chains as pure rotations with world
  segment lengths: correct under uniform scale, best-effort under
  non-uniform scale (same policy as Phase 1's write-back).
- Programmatic transform writes outside the covered paths (animation,
  physics) still bypass channel locks by design (section 2).
