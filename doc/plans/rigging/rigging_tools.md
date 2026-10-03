# Rigging Tools - Master Plan

Status: in progress

This plan extends `doc/editor/tools.md` (the tools the editor has for posing
and selecting bones) and `doc/erhe/scene.md` (skins, joints and animation)
with a rigging tool set. Phases 1-3 are built; phases 4-7 are outstanding
(see "Suggested implementation order - summary"). Each built phase has its
own requirements document, which owns that phase's design, code locations
and verification status: `fabrik_ik.md` (Phase 1), `ik_settings.md`,
`pole_target.md` and `ik_drag_options.md` (Phase 2), `skeleton_editing.md`
(Phase 3); `interactive_test_pass.md` is the windowed verification pass of
Phases 1 and 2.

## Purpose

Define the long-term roadmap for rigging tools in erhe editor, informed by a
survey of Blender's rigging system (constraint types, armature/pose tools,
skinning, drivers, Rigify - surveyed from the Blender source checkout, main
branch, Aug 2026). The former "non-goals" of the FABRIK requirements document
are placed here as later phases.

## Where erhe stands today

Building blocks the outstanding phases lean on:

- **Skeleton data**: glTF skin import; `erhe::scene::Skin` with joints +
  inverse bind matrices; the persistent `Item_flags::bone` flag and
  `bone_proxy`; bone visualization (`src/editor/tools/bone_visualization.*`);
  GPU skinning via `Joint_buffer`. Rig data on bone nodes (`Rig.tail`,
  `Rig.connected`, `Rig.rest_translation` / `rest_rotation` / `rest_scale`)
  is owned by `skeleton_editing.md`.
- **Animation**: glTF animation playback (`erhe::scene::Animation`, samplers /
  channels, STEP / LINEAR / CUBICSPLINE) and per-path keyframing of node TRS
  (Create / Delete Key, Autokey; `doc/editor/editor.md`, the `animation/`
  part; outstanding keyframing work in `doc/plans/animation_keyframing.md`).
- **Interactive IK** (Phases 1-2): FABRIK IK on a translate drag of a bone
  behind the `Ik_solver` interface (`src/editor/transform/ik_solver.hpp`),
  constrained by the per-bone `Ik.*` node properties (DOF locks, swing/twist
  limits, stiffness; `src/editor/scene/ik_properties.cpp`), a pole target,
  and the Move tool's `Ik_drag_options` (`src/editor/transform/ik_drag.hpp`);
  per-component transform channel locks are `Item_flags::lock_translation_x`
  .. `lock_scale_z` (`src/erhe/item/erhe_item/item.hpp`).
- **Skeleton editing and posing** (Phase 3): bone create / extrude /
  subdivide / delete / dissolve, symmetrize, roll and align
  (`src/editor/rig/bone_structure.hpp`), side naming and selection helpers,
  clear / copy / paste (flipped) pose (`src/editor/rig/bone_pose.hpp`), a
  rigid skin-binding stub (`src/editor/rig/rigid_skin.hpp`), the MCP tools of
  `src/editor/mcp/mcp_server_rig.cpp` and `mcp_server_skinning.cpp`.
- **Editing infrastructure**: Transform tool with subtools and multi-node undo
  (`Node_transform_operation`, compound operations), message bus, selection,
  mesh component selection, vertex Paint tool, Lattice tool, Properties window
  editing of node properties and flags, per-name flag serialization.

Missing entirely: a persistent transform/rig constraint system in the
scene-graph domain (physics joint constraints exist; IK runs only during a
drag), weight editing, drivers.

## Survey summary: what Blender has

This is the reference feature map the phases below draw from. Not everything
here is planned for erhe; the "erhe phase" column says where (- = not planned,
future = beyond this plan's horizon).

### Constraint types (Blender's UI grouping)

| Group | Constraint | What it does | erhe phase |
|---|---|---|---|
| Tracking | Inverse Kinematics | Chain IK to target, pole target, per-bone DOF locks/limits/stiffness, stretch | 1-2 (interactive), 4 (persistent) |
| Tracking | Damped Track | Minimal swing rotation aiming an axis at target | 4 |
| Tracking | Track To | Legacy aim with explicit up axis | 4 |
| Tracking | Locked Track | Aim by rotating about one locked axis | 4 |
| Tracking | Stretch To | Aim +Y at target and stretch to reach, volume squash | 6 |
| Tracking | Clamp To | Pin location onto a curve | - |
| Tracking | Spline IK | Fit a bone chain along a curve (direct geometric fit) | 6 |
| Transform | Copy Location / Rotation / Scale / Transforms | Copy target channels, per-axis, mix modes | 4 |
| Transform | Limit Location / Rotation / Scale | Clamp own channels to ranges | 4 |
| Transform | Limit Distance | Clamp inside/outside/on a sphere around target | 4 |
| Transform | Maintain Volume | Compensate one axis's scale on the other two | 6 |
| Transform | Transformation | Map a range of a target channel onto a range of an own channel | 7 |
| Transform | Transform Cache | Matrix from Alembic/USD cache | - |
| Relationship | Child Of | Parenting via constraint with bakeable inverse | 4 |
| Relationship | Armature | Weighted multi-bone parenting blend | 6 |
| Relationship | Floor | Keep owner above a plane at target | 4 |
| Relationship | Follow Path | Move along a curve | - (curves first) |
| Relationship | Action | Drive a pose from an action by a target channel | 7 |
| Relationship | Pivot | Alternate rotation pivot | - |
| Relationship | Shrinkwrap | Snap onto target mesh surface | - |
| Motion Tracking | Camera/Object Solver, Follow Track | Movie-clip tracking | - |

Common constraint infrastructure (Blender): ordered per-owner constraint
stack evaluated head-to-tail, each constraint seeing the previous result;
target + subtarget (bone) model; owner/target space conversion (world / local
/ pose / custom); influence 0..1 blended in world space; IK and Spline IK are
solved as whole chains outside the per-bone stack. This shape is the model
for Phase 4.

### Rigging tools beyond constraints

| Area | Blender feature | erhe phase |
|---|---|---|
| Interactive IK | Auto-IK: temporary IK constraint injected during a pose-mode grab, chain from connected parents, respects per-bone locks | 1-2 (FABRIK equivalent) |
| Bone data | Per-bone IK DOF locks, joint limits (min/max per axis), stiffness | 2 |
| Bone data | Transform channel locks (loc/rot/scale per component) | 2 |
| Bone data | Inherit rotation (hinge) / inherit scale modes, connected bones | 3 |
| Bone data | Custom bone shapes, bone colors | 3 (partial) |
| Bone data | Bone collections (grouping, visibility, selectability) | - (erhe item tree/tags may suffice) |
| Bone data | B-Bones (bendy bones, curved segments) | future |
| Bone data | Envelopes (capsule geometry for binding weights; falloff skinning) | 5 (binding), 6 (falloff) |
| Armature editing | Add/extrude/subdivide/fill/duplicate/delete bones, connect vs offset parenting | 3 |
| Armature editing | Symmetrize, .L/.R naming conventions, name flipping, autoside | 3 |
| Armature editing | Bone roll calculation/alignment | 3 |
| Armature editing | Chain/hierarchy/mirror selection | 3 |
| Skinning | Bind with empty groups / envelope weights / automatic (bone-heat Laplacian) weights | 5 |
| Skinning | Weight painting: draw/blur/average/smear brushes, gradient, sample, flood | 5 |
| Skinning | Weight ops: normalize (all), smooth, clean, quantize, limit total, mirror, invert | 5 |
| Skinning | Dual-quaternion skinning ("preserve volume") | 6 |
| Posing | Clear transforms (respecting locks), copy/paste pose (flipped) | 3 |
| Posing | Apply pose as rest pose, visual transform apply | 6 |
| Posing | Pose slide: push/relax/breakdown/blend-with-rest, pose propagate | future (needs keyframing) |
| Posing | Pose library (pose assets, blendable) | future |
| Posing | Bone motion paths | future |
| Animation glue | Keyframing / animation authoring | built per path (`doc/plans/animation_keyframing.md`); IK bake in 7 |
| Animation glue | Drivers (property driven by transform channel / property, expression) | 7 |
| Automation | Rigify-style meta-rig -> generated rig with IK/FK switching | future |
| Retargeting | BVH import, pose transfer | future |

Notable Blender facts that shaped this plan:

- **Auto-IK validates Phase 1's design**: Blender's drag-IK is exactly a
  temporary, targetless IK constraint created on grab and removed on release,
  with the chain discovered by walking connected parents and capped by a
  length setting; per-bone locks become temporary DOF locks. Our FABRIK-on-
  drag is the same UX with a simpler solver and a flag (`ik_lock`) instead of
  a chain-length number.
- **Blender ships two IK solvers** (legacy SDLS-damped Jacobian, and iTaSC).
  FABRIK is simpler than either and fine for interactive posing; if joint
  limits (Phase 2) prove unstable under constrained FABRIK, a damped-least-
  squares Jacobian solver is the known-good fallback - the plan keeps the
  solver behind an interface so this is swappable.
- **Spline IK is not an iterative solver** - it is a direct tip-to-root
  geometric fit of bones onto a curve. Cheap to implement once erhe has
  curves; scheduled with the deformation phase.
- **Automatic weights** are a cotangent-Laplacian "bone heat" solve with
  visibility ray casts - a well-understood algorithm, but it needs a sparse
  solver; scheduled late in the skinning phase.

## Cross-cutting concerns (all phases)

- **Serialization**: erhe scenes are saved as erhe-authored glTF files with
  `ERHE_*` extensions. Rig data beyond skins (constraints, IK settings,
  per-bone limits) has no core glTF home. Per-node rig data rides the node's
  `ERHE_node` `properties` map by qualified property name, with
  `property_node_refs` for a value naming another node
  (`doc/gltf_extensions/ERHE_node.md`); Phase 2's per-bone IK settings are
  the first user and Phase 4's constraints follow the same carriage. Item flags
  serialize by name through an explicit persistent-flag allowlist
  (`erhe_gltf/gltf_item_flags.cpp`); `ik_lock`, `bone` and the channel locks
  persist through it.
- **Evaluation order**: today node transforms flow parent->child only. From
  Phase 4 on, constraints introduce cross-hierarchy dependencies (owner
  depends on target) and whole-chain solves (IK), so scene update needs an
  explicit evaluation pass with dependency ordering and cycle detection -
  the single largest architectural change in this plan. erhe's transform
  setters refresh only the set node's
  cached world transform (descendants wait for the next
  `update_node_transforms()` pass), so every same-frame chain computation
  refreshes caches by hand - an evaluation pass owns that ordering instead
  (see `fabrik_ik.md`'s implementation notes).
- **Undo**: every interactive tool records one operation per gesture through
  the existing operation machinery; every new data type (constraint, weights)
  needs corresponding operations.
- **Testing**: solver math (FABRIK, constraint evaluation, heat weights) gets
  unit tests in the owning library's `test/` directory; editor gestures are
  exercised via the MCP interface where practical.
- **Performance**: chains and constraint stacks are small; correctness and
  stability first. Only skinning-weight tools touch per-vertex data at scale.

## Phases

Ordering rationale: start where value is immediate and no new data model is
needed (interactive IK on imported characters), then build the persistent
data model (constraints), then content-creation depth (skeleton authoring,
skinning), then deformation quality, then animation-system integration.
Phases 3 and 5 are independent of each other and can be reordered or
interleaved; phase boundaries are release points, not waterfalls.

### Phase 1 - Interactive FABRIK IK on translate drag - built

Requirements and status: `fabrik_ik.md`. Dragged bone = effector; chain up
to the first `Item_flags::ik_lock` bone; rotations-only write-back; one undo
operation per gesture; Move tool "Bone IK" toggle; drag handles (non-bone
children of a bone) and the Hierarchy window's Add Bone Tip Nodes and Reset
Bones to Bind Pose.

Outstanding: the user's hands-on pass (`interactive_test_pass.md`).

### Phase 2 - IK quality of life - built

Requirements and status: `pole_target.md` (pole target / swivel),
`ik_settings.md` (per-bone DOF locks, joint limits, stiffness, transform
channel locks, the `Ik_solver` interface), `ik_drag_options.md` (effector
orientation, chain visualization, and the Phase 1 feel questions as Move
tool options: mid-chain drag, solve-from, pole alignment).

Outstanding:

- The user's hands-on pass (`interactive_test_pass.md`): live gizmo drags
  with a pole and under `follow_last_segment`, the Properties pole target
  picker, the Move tool option combos and the chain visualization
  (`pole_target.md` and `ik_drag_options.md` "Outstanding" lines).
- Stiffness feel: `Ik.stiffness` is built as a per-iteration scale-down
  (`ik_settings.md` section 4); whether it gives the posing feel a rigger
  expects is decided by that hands-on use.
- A damped-least-squares Jacobian or other global solver behind
  `Ik_solver`, if constrained FABRIK proves insufficient (finding F8 of
  `interactive_test_pass.md`: constrained FABRIK is a local solver with
  best-effort reach).

### Phase 3 - Skeleton editing and posing basics - built

Requirements and status: `skeleton_editing.md` (every slice, the bone
display properties and the rigid-bind stub included). Structural editing of
a skeleton bound to a `Skin` is refused until Phase 6's rest-pose tooling.

Outstanding: the user's hands-on pass.

### Phase 4 - Constraint system foundation

The architectural core: a persistent, ordered constraint stack evaluated in
the scene update. Modeled on Blender's proven shape (see survey).

- **Data model**: `Constraint` as a value group of the node or a per-node ordered list;
  common fields: enabled, influence 0..1, target node (+ optional
  space-defining node), owner/target space (world / parent / local). Evaluated
  head-to-tail, each constraint seeing the previous result; influence blends
  the constrained matrix against the unconstrained one.
- **Evaluation pass**: scene-update stage that orders owners by dependency
  (constraint targets before owners), detects cycles (disable + report), and
  runs whole-chain IK solves as units. This subsumes Phase 1's drag-time
  solve: an interactive drag becomes a temporary IK constraint, exactly like
  Blender's Auto-IK - one code path for interactive and persistent IK.
- **Initial constraint set** (chosen for rigging value / simplicity ratio):
  - Copy Location / Copy Rotation / Copy Scale / Copy Transforms (per-axis,
    basic mix modes)
  - Limit Location / Rotation / Scale, Limit Distance
  - Damped Track (preferred aim), Track To, Locked Track
  - Child Of (with set-inverse), Floor
  - **IK constraint**: persistent chain (target node, chain length or
    ik_lock-terminated, pole target, iterations/tolerance) using the Phase 2
    solver.
- **UI**: constraint list per node in Properties (add/remove/reorder/enable/
  influence), constraint editing, target picking; item tree indication that a
  node is constrained.
- **Serialization**: extend the Phase 2 `ERHE_node` property carriage (see
  cross-cutting) to cover constraint data.
- **Undo**: add/remove/reorder/edit constraint operations.

Deliverable: persistent rigs - an IK leg with a floor constraint and a
tracked look-at survive save/load.

### Phase 5 - Skinning: weights authoring

Make erhe able to bind meshes to skeletons, not just import bindings.
Builds on the existing Paint tool and mesh component infrastructure.

- **Weight data model**: editable per-vertex joint weights on editor meshes
  (source-of-truth on geometry, synced to the GPU `JOINTS_0`/`WEIGHTS_0`
  attributes), influence count limit (4 to match GPU path), per-bone "deform"
  opt-out flag.
- **Bind operations**: bind mesh to skeleton with (a) empty groups, (b)
  distance/envelope-based automatic weights, (c) bone-heat automatic weights
  (cotangent Laplacian + visibility ray casts + sparse solve - the expensive
  item, last).
- **Weight painting**: extend/parallel the vertex Paint tool: draw / add /
  subtract / blur / smear brushes, weight value + strength, active-bone
  selection (paint the weights of the selected joint), auto-normalize toggle,
  X-mirror.
- **Weight visualization**: heat-map display of the active joint's weights,
  weightless-vertex warning color.
- **Weight ops**: normalize (vertex / all), limit total, clean (remove
  near-zero), smooth, mirror, transfer between joints.
- **Weight inspection**: per-vertex weight list UI for the selected vertex
  (mesh component selection already exists).

Deliverable: model a mesh, build a skeleton (Phase 3), bind and paint
weights, pose with IK - full character workflow inside erhe.

### Phase 6 - Deformation quality and advanced chain tools

- **Dual-quaternion skinning** option (fixes candy-wrapper twisting) in the
  GPU skinning path.
- **Stretch To constraint** and **stretchy IK** (optional per-chain stretch
  with volume compensation), **Maintain Volume**.
- **Spline IK**: fit a bone chain along a curve (direct geometric fit,
  tip->root, as Blender does). Depends on erhe growing an editable curve
  primitive - that dependency, not the solver, is the real cost; if curves
  are far off, substitute a "chain through node path" variant.
- **Armature constraint** (weighted multi-bone parenting) for advanced
  mechanisms.
- **Apply pose as rest pose** (rewrites inverse bind matrices + rest data -
  needs Phase 3's rest pose model and Phase 5's weights to stay valid) and
  **visual transform apply** (bake the constraint-evaluated result into the
  node's own transform). This also unlocks structural editing of bound
  skeletons, deferred from Phase 3.
- Envelope-capsule skinning falloff, if envelope binding in Phase 5 proved
  useful.

Deliverable: production-quality deformation: twisting limbs without collapse,
stretchy cartoon rigs, tails/spines on curves.

### Phase 7 - Rig logic: drivers and animation integration

The glue that turns posable skeletons into *rigs*, and IK into something
that can be recorded.

- **Keyframing prerequisite**: per-path TRS keyframing is built (see "Where
  erhe stands today"); its outstanding work is
  `doc/plans/animation_keyframing.md`. This phase adds recording IK-solved
  poses as FK keys ("bake pose").
- **Drivers**: a property driven by another property or by a transform
  channel of another node (Blender's most-used rigging glue after
  constraints). erhe's geometry-graph node system is a candidate substrate -
  drivers as a small dataflow graph evaluated in the constraint pass.
- **Transformation constraint** (range->range channel mapping) - with drivers,
  covers most "gadget" rigs (foot roll, finger curl sliders).
- **Action constraint** equivalent (pose driven by a channel through an
  animation clip) if erhe animation authoring reaches named clips.
- **Morph targets (shape keys)**: erhe currently has no morph-target support
  end-to-end (glTF weights animation channels are skipped on export with a
  warning). Rigging-grade corrective shapes need morph rendering + import
  first, then driving target weights from bone transforms via the driver
  system above. The rendering half may deserve its own plan; only the
  driver hookup is Phase 7 scope.
- **IK/FK switching**: per-chain blend between FK pose and IK result, with
  snapping (align FK to current IK result and vice versa).

Deliverable: animator-facing rigs: foot-roll controls, finger curls, IK/FK
switching, and IK poses bakeable to keyframes.

### Future / research (beyond this plan)

Collected from the survey; explicitly not scheduled:

- Pose slide tools (push/relax/breakdown), pose propagate, pose library -
  all keyframe-centric; revisit once Phase 7 keyframing matures.
- Bone motion paths visualization.
- B-Bones / bendy bones (curved bone segments with eased handles).
- Rigify-style rig generation from meta-rigs; IK/FK-switch generation.
- Mocap: BVH import, retargeting (Blender itself has no built-in retargeting).
- Full-body IK / multi-effector solves (FABRIK extends to multiple end
  effectors and sub-bases; revisit if there is demand).
- Muscle/jiggle simulation, cloth-driven bones.
- Sculpted corrective shape authoring (editing morph target geometry
  in-editor; Phase 7 only drives existing targets).
- Set Rest intermittently records no operation in the automated pass
  (finding F6 of `interactive_test_pass.md`): root cause (the button or the
  injected click) not yet found; deferred.

## Suggested implementation order - summary

| Phase | Title | Status | Depends on | Rough size |
|---|---|---|---|---|
| 1 | FABRIK IK on drag | built; hands-on pass open | - | S-M |
| 2 | IK quality of life (pole, limits, locks) | built; hands-on pass + stiffness feel open | 1 | M |
| 3 | Skeleton editing + posing basics | built; hands-on pass open | - (1 for testing) | M-L |
| 4 | Constraint system foundation | not started; needs a requirements document | 1-2 (solver), scene update rework | L |
| 5 | Skinning: weights authoring | not started (rigid-bind stub from 3) | 3 (for full value) | L |
| 6 | Deformation quality (DQ skinning, Spline IK, stretch) | not started | 3 (rest pose model), 4, 5 | M-L |
| 7 | Drivers + animation integration | not started | 4; keyframing plan | L |

Milestone framing: with Phase 2 erhe *poses* imported characters well;
after Phase 4 it can *rig* them persistently; after Phase 5 it can *create*
characters end-to-end; after Phase 7 it can *animate* rigs like a DCC tool.
