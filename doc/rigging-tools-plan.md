# Rigging Tools — Master Plan

Status: draft, awaiting review.
Companion document: `fabrik-ik-requirements.md` (detailed requirements for Phase 1).

## Purpose

Define the long-term roadmap for rigging tools in erhe editor, informed by a
survey of Blender's rigging system (constraint types, armature/pose tools,
skinning, drivers, Rigify — surveyed from the Blender source checkout, main
branch, Aug 2026). The former "non-goals" of the FABRIK requirements document
are placed here as later phases.

## Where erhe stands today

Existing building blocks the plan leans on:

- **Skeleton data**: glTF skin import; `erhe::scene::Skin` with joints +
  inverse bind matrices; `Item_flags::bone` and `bone_proxy`; bone
  visualization (`src/editor/tools/bone_visualization.*`); GPU skinning via
  `Joint_buffer`.
- **Animation**: glTF animation playback (`erhe::scene::Animation`, samplers /
  channels, STEP / LINEAR / CUBICSPLINE) — playback only, no authoring.
- **Editing infrastructure**: Transform tool with subtools and multi-node undo
  (`Node_transform_operation`, compound operations), message bus, selection,
  mesh component selection, vertex Paint tool, Lattice tool, Properties window
  flag editing, per-name flag serialization.

Missing entirely: any IK, any transform/rig constraint system (physics joint
constraints exist, but nothing in the scene-graph domain), weight editing,
skeleton authoring, keyframing, drivers.

## Survey summary: what Blender has

This is the reference feature map the phases below draw from. Not everything
here is planned for erhe; the "erhe phase" column says where (— = not planned,
future = beyond this plan's horizon).

### Constraint types (Blender's UI grouping)

| Group | Constraint | What it does | erhe phase |
|---|---|---|---|
| Tracking | Inverse Kinematics | Chain IK to target, pole target, per-bone DOF locks/limits/stiffness, stretch | 1-2 (interactive), 4 (persistent) |
| Tracking | Damped Track | Minimal swing rotation aiming an axis at target | 4 |
| Tracking | Track To | Legacy aim with explicit up axis | 4 |
| Tracking | Locked Track | Aim by rotating about one locked axis | 4 |
| Tracking | Stretch To | Aim +Y at target and stretch to reach, volume squash | 6 |
| Tracking | Clamp To | Pin location onto a curve | — |
| Tracking | Spline IK | Fit a bone chain along a curve (direct geometric fit) | 6 |
| Transform | Copy Location / Rotation / Scale / Transforms | Copy target channels, per-axis, mix modes | 4 |
| Transform | Limit Location / Rotation / Scale | Clamp own channels to ranges | 4 |
| Transform | Limit Distance | Clamp inside/outside/on a sphere around target | 4 |
| Transform | Maintain Volume | Compensate one axis's scale on the other two | 6 |
| Transform | Transformation | Map a range of a target channel onto a range of an own channel | 7 |
| Transform | Transform Cache | Matrix from Alembic/USD cache | — |
| Relationship | Child Of | Parenting via constraint with bakeable inverse | 4 |
| Relationship | Armature | Weighted multi-bone parenting blend | 6 |
| Relationship | Floor | Keep owner above a plane at target | 4 |
| Relationship | Follow Path | Move along a curve | — (curves first) |
| Relationship | Action | Drive a pose from an action by a target channel | 7 |
| Relationship | Pivot | Alternate rotation pivot | — |
| Relationship | Shrinkwrap | Snap onto target mesh surface | — |
| Motion Tracking | Camera/Object Solver, Follow Track | Movie-clip tracking | — |

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
| Bone data | Bone collections (grouping, visibility, selectability) | — (erhe item tree/tags may suffice) |
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
| Animation glue | Keyframing / animation authoring | 7 (prerequisite work) |
| Animation glue | Drivers (property driven by transform channel / property, expression) | 7 |
| Automation | Rigify-style meta-rig → generated rig with IK/FK switching | future |
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
  squares Jacobian solver is the known-good fallback — the plan keeps the
  solver behind an interface so this is swappable.
- **Spline IK is not an iterative solver** — it is a direct tip-to-root
  geometric fit of bones onto a curve. Cheap to implement once erhe has
  curves; scheduled with the deformation phase.
- **Automatic weights** are a cotangent-Laplacian "bone heat" solve with
  visibility ray casts — a well-understood algorithm, but it needs a sparse
  solver; scheduled late in the skinning phase.

## Cross-cutting concerns (all phases)

- **Serialization**: erhe scenes are saved as erhe-authored glTF files with
  `ERHE_*` extensions. Rig data beyond skins (constraints, IK settings,
  per-bone limits) has no core glTF home. Decide once — extension (e.g.
  `ERHE_constraints`) vs `extras` — by the first phase that adds non-flag rig
  data, which is Phase 2 (per-bone IK limits/locks), not Phase 4. Item flags
  serialize by name through an explicit persistent-flag allowlist
  (`erhe_gltf/gltf_item_flags.cpp`); `ik_lock` needs one new table entry
  there (plus the flag bit, `c_bit_labels` entry, and `count` bump in
  `item.hpp`), and nothing beyond that.
- **Evaluation order**: today node transforms flow parent→child only. From
  Phase 4 on, constraints introduce cross-hierarchy dependencies (owner
  depends on target) and whole-chain solves (IK), so scene update needs an
  explicit evaluation pass with dependency ordering and cycle detection —
  the single largest architectural change in this plan.
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

### Phase 1 — Interactive FABRIK IK on translate drag

Scope: exactly `fabrik-ik-requirements.md`. Dragged bone = effector; chain up
to first `Item_flags::ik_lock` bone; unconstrained FABRIK; rotations-only
write-back; one undo op per gesture; Transform tool toggle.

Deliverable: pose an imported glTF character by dragging bones.

### Phase 2 — IK quality of life

Builds directly on Phase 1's solver and drag UX.

- **Pole target / swivel control**: designate a pole node (or screen-space
  swivel angle modifier key) to control chain bend direction (elbow/knee).
- **Per-bone IK settings**: DOF locks per axis and joint rotation limits
  (min/max per axis), stored per node (new small POD on bone nodes or a node
  attachment), edited in Properties. Solver enforces them via constrained
  FABRIK (per-iteration reprojection). Stiffness if it falls out naturally.
  This is the first non-flag rig data that must persist, so the serialization
  decision (see cross-cutting) lands in this phase.
- **Solver interface**: factor the solver behind an interface (chain in /
  posed chain out) so a damped-least-squares Jacobian solver can replace or
  complement FABRIK if constrained FABRIK proves unstable.
- **Transform channel locks**: general per-component lock of loc/rot/scale on
  nodes (Blender `protectflag` equivalent), respected by IK, by the Transform
  tool, and by Properties editing. Useful well beyond rigging.
- **Effector orientation option**: keep world orientation (Phase 1 default) vs
  follow last segment, as a Transform tool setting.
- **Chain visualization**: highlight active chain, root, pole during drag.
- Resolve Phase 1 open questions that were deferred (mid-chain drag feel,
  incremental vs from-start solve) with the added experience.

Deliverable: believable limb posing with controlled elbows/knees and locked
joints.

### Phase 3 — Skeleton editing and posing basics

Authoring skeletons in-editor rather than only importing them, plus the
non-IK posing verbs. Mostly editor UX over existing Node machinery.

- **Bone creation**: create bone (child of selection or at cursor), extrude
  bone from selected tip, subdivide, delete/dissolve; connected vs offset
  parenting semantics (a "connected" child keeps its head on the parent's
  tail; store as a flag).
- **Skeleton conventions**: `.L`/`.R` (or `_L`/`_R`) side naming, name
  flipping, symmetrize across X (mirror bones, remap parents).
- **Bone roll / orientation tools**: recalculate roll from view / world axis /
  cursor; align selected to active.
- **Selection helpers**: select whole chain (linked), select parent/children,
  select mirror.
- **Posing verbs**: clear location/rotation/scale/all to rest pose
  (respecting Phase 2 locks); copy pose / paste pose / paste flipped
  (clipboard already exists in editor tools).
- **Rest pose model**: define what "rest pose" means for erhe skeletons
  (currently implicit in inverse bind matrices) — prerequisite for "clear to
  rest" and later "apply pose as rest".
- **Bone head/tail model**: erhe joints are glTF nodes — a position, no
  tail. Extrude-from-tip, connected parenting, and roll all presuppose a
  head/tail bone; decide how tail is represented (inferred from the single
  child, stored length + direction, or proxy-only) before the editing verbs.
- **Bound-skeleton editing semantics**: Phase 3 verbs (subdivide, delete,
  symmetrize, roll changes) applied to a skeleton that already has a `Skin`
  invalidate inverse bind matrices and orphan weights. v1 stance: structural
  editing of a bound skeleton is unsupported (blocked with a message) until
  Phase 6's rest-pose tooling; posing a bound skeleton is of course fine.
- **Bone display**: custom bone display shapes and per-bone colors on the
  existing bone proxy system (nice-to-have within this phase).
- **Skin binding stub**: plumbing for Phase 5 — create a `Skin` from a bone
  selection with derived inverse binds and trivial rigid weights (each vertex
  fully weighted to its nearest bone), so an authored skeleton can be
  smoke-tested against a mesh before real weighting exists.

Deliverable: build a simple skeleton from scratch in the editor and pose it.

### Phase 4 — Constraint system foundation

The architectural core: a persistent, ordered constraint stack evaluated in
the scene update. Modeled on Blender's proven shape (see survey).

- **Data model**: `Constraint` as a node attachment or per-node ordered list;
  common fields: enabled, influence 0..1, target node (+ optional
  space-defining node), owner/target space (world / parent / local). Evaluated
  head-to-tail, each constraint seeing the previous result; influence blends
  the constrained matrix against the unconstrained one.
- **Evaluation pass**: scene-update stage that orders owners by dependency
  (constraint targets before owners), detects cycles (disable + report), and
  runs whole-chain IK solves as units. This subsumes Phase 1's drag-time
  solve: an interactive drag becomes a temporary IK constraint, exactly like
  Blender's Auto-IK — one code path for interactive and persistent IK.
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
- **Serialization**: extend the Phase 2 extension/extras scheme (see
  cross-cutting) to cover constraint data.
- **Undo**: add/remove/reorder/edit constraint operations.

Deliverable: persistent rigs — an IK leg with a floor constraint and a
tracked look-at survive save/load.

### Phase 5 — Skinning: weights authoring

Make erhe able to bind meshes to skeletons, not just import bindings.
Builds on the existing Paint tool and mesh component infrastructure.

- **Weight data model**: editable per-vertex joint weights on editor meshes
  (source-of-truth on geometry, synced to the GPU `JOINTS_0`/`WEIGHTS_0`
  attributes), influence count limit (4 to match GPU path), per-bone "deform"
  opt-out flag.
- **Bind operations**: bind mesh to skeleton with (a) empty groups, (b)
  distance/envelope-based automatic weights, (c) bone-heat automatic weights
  (cotangent Laplacian + visibility ray casts + sparse solve — the expensive
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
weights, pose with IK — full character workflow inside erhe.

### Phase 6 — Deformation quality and advanced chain tools

- **Dual-quaternion skinning** option (fixes candy-wrapper twisting) in the
  GPU skinning path.
- **Stretch To constraint** and **stretchy IK** (optional per-chain stretch
  with volume compensation), **Maintain Volume**.
- **Spline IK**: fit a bone chain along a curve (direct geometric fit,
  tip→root, as Blender does). Depends on erhe growing an editable curve
  primitive — that dependency, not the solver, is the real cost; if curves
  are far off, substitute a "chain through node path" variant.
- **Armature constraint** (weighted multi-bone parenting) for advanced
  mechanisms.
- **Apply pose as rest pose** (rewrites inverse bind matrices + rest data —
  needs Phase 3's rest pose model and Phase 5's weights to stay valid) and
  **visual transform apply** (bake the constraint-evaluated result into the
  node's own transform). This also unlocks structural editing of bound
  skeletons, deferred from Phase 3.
- Envelope-capsule skinning falloff, if envelope binding in Phase 5 proved
  useful.

Deliverable: production-quality deformation: twisting limbs without collapse,
stretchy cartoon rigs, tails/spines on curves.

### Phase 7 — Rig logic: drivers and animation integration

The glue that turns posable skeletons into *rigs*, and IK into something
that can be recorded.

- **Keyframing prerequisite**: animation authoring — create/edit keyframes on
  node TRS channels (erhe animation is playback-only today; this is its own
  plan, likely `animation-keyframing-plan.md` — this phase depends on it, and
  on recording IK-solved poses as FK keys ("bake pose")).
- **Drivers**: a property driven by another property or by a transform
  channel of another node (Blender's most-used rigging glue after
  constraints). erhe's geometry-graph node system is a candidate substrate —
  drivers as a small dataflow graph evaluated in the constraint pass.
- **Transformation constraint** (range→range channel mapping) — with drivers,
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

- Pose slide tools (push/relax/breakdown), pose propagate, pose library —
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

## Suggested implementation order — summary

| Phase | Title | Depends on | Rough size |
|---|---|---|---|
| 1 | FABRIK IK on drag | — | S-M |
| 2 | IK quality of life (pole, limits, locks) | 1; serialization decision | M |
| 3 | Skeleton editing + posing basics | — (1 for testing) | M-L |
| 4 | Constraint system foundation | 1-2 (solver), scene update rework | L |
| 5 | Skinning: weights authoring | 3 (for full value) | L |
| 6 | Deformation quality (DQ skinning, Spline IK, stretch) | 3 (rest pose model), 4, 5 | M-L |
| 7 | Drivers + animation integration | 4; keyframing plan | L |

Milestone framing: after Phase 2 erhe can *pose* imported characters well;
after Phase 4 it can *rig* them persistently; after Phase 5 it can *create*
characters end-to-end; after Phase 7 it can *animate* rigs like a DCC tool.
