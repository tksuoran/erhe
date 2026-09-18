# FABRIK Inverse Kinematics — Initial Requirements

Status: draft, awaiting review.
This document is Phase 1 of the rigging roadmap in `rigging-tools-plan.md`.

## Motivation

erhe editor can import skinned meshes with skeletons (glTF skins), identify
joint nodes (`Item_flags::bone`, bone pick/display proxies), and animate them,
but the only way to pose a skeleton by hand is forward kinematics: selecting a
joint and rotating/translating it directly. This is the first step toward basic
rigging tools: interactive IK posing, so that dragging a bone makes a whole
chain follow naturally.

## Goal

When the user drags a bone node with the Transform tool's translate handles,
the dragged bone acts as an IK end effector. The chain of its ancestor bones —
up to, but not past, the first bone marked as IK-locked — is solved with FABRIK
(Forward And Backward Reaching Inverse Kinematics) so that the effector reaches
the drag position, or the closest reachable point when the target is out of
reach, without changing any bone lengths.

## Functional requirements

### 1. IK chain definition

- The chain is discovered implicitly from the dragged node; no separate chain
  setup step is required for the first version.
- Chain discovery: starting from the dragged bone, walk up the node hierarchy
  collecting ancestors while
  - the ancestor is a bone (`Item_flags::bone` set), and
  - the previously collected bone was not IK-locked (see below).
- The last collected bone is the **chain root**. Its world position stays fixed
  during the solve (its orientation may change so its child follows).
- A chain needs at least two joints (root + effector, one segment). If the
  dragged bone's parent is not a bone, if the dragged bone itself is the root
  of its skeleton, or if the dragged bone itself is IK-locked, IK does not
  apply and the translate drag behaves exactly as today (plain FK
  translation).
- Multi-selection: IK applies only when the resolved transform target is
  exactly one bone node. When several nodes are selected (bones or not), the
  drag uses today's plain multi-node FK translation.
- Drag handles (added in implementation): a non-bone node parented directly
  under a bone (e.g. a node created by the Hierarchy window's Add Bone Tip
  Nodes) also starts an IK drag - it joins the chain as the effector point,
  so its parent bone rotates to aim at it, which a bone-effector drag never
  does (the effector bone keeps its own orientation). The ancestor walk is
  otherwise identical, starting from the handle's parent bone.

### 2. IK lock flag

- Add a dedicated item flag, `Item_flags::ik_lock`, that masks a bone from IK:
  chain discovery stops at (and includes, as the fixed root) the first
  IK-locked ancestor.
- The flag is per-instance, authored by the user, serialized with the scene
  by name — which requires registering it in the persistent-flag table in
  `erhe_gltf/gltf_item_flags.cpp` (flag persistence is an explicit allowlist,
  not automatic), in addition to the `Item_flags` bit, `c_bit_labels` entry,
  and `count` bump in `item.hpp` — and editable from the item Properties
  window flag list like existing flags.
- The flag has no effect on non-bone nodes in the first version.

### 3. Solver — FABRIK

- Algorithm: standard FABRIK (Aristidou & Lasenby 2011), operating on joint
  world positions:
  1. Record segment lengths from the current pose (rest lengths are whatever
     the pose is when the drag starts — bind pose is not consulted).
  2. If the target's distance from the chain root exceeds the total chain
     length, the chain straightens toward the target (the unreachable case
     yields the closest reachable point directly).
  3. Otherwise iterate a forward-reaching pass (effector snapped to the
     target, positions pulled toward it preserving lengths, working toward
     the root) and a backward-reaching pass (root snapped back to its fixed
     position, working toward the tip) — the paper's terminology — until the
     effector is within tolerance of the target or the iteration limit is
     reached.
- Bone lengths are exactly preserved (within floating point) in the resulting
  pose.
- Degenerate segments: a zero-length (or near-epsilon) segment between
  coincident joints — common for helper/leaf bones in imported rigs — must
  not produce NaNs; such segments are skipped (their joint rides on its
  neighbor) and their bones keep their local transform.
- Scale: the solve assumes chains without non-uniform scale. World joint
  positions are solved as-is, so uniformly scaled chains work; if any chain
  node carries non-uniform scale, the rotation-only write-back cannot exactly
  preserve world segment lengths and the result is best-effort (no attempt to
  counter-scale in v1).
- Termination parameters (initial defaults, tunable constants — no UI needed
  yet): tolerance ~1e-4 m, max ~16 iterations.
- No joint constraints (angle limits, hinge axes), no pole vector / swivel
  control, and a single chain with a single effector only. These are explicit
  non-goals for the first version (see Out of scope for Phase 1).

### 4. From joint positions back to node transforms

- FABRIK produces new world positions for each joint. These must be converted
  back into node transforms:
  - Each non-effector joint gets a rotation applied so that its child segment
    points at the child's new solved position (minimal rotation from the old
    child direction to the new one, i.e. shortest-arc; roll about the segment
    axis is preserved).
  - Conversion order matters: joints are processed sequentially from root to
    effector. For each joint, the child's current world position is
    recomputed under the already-updated ancestor transforms, the shortest-arc
    world rotation from that direction to the solved direction is computed,
    and it is re-expressed in the joint's parent frame and composed into the
    joint's local rotation. Computing all deltas against the pre-solve pose
    simultaneously is incorrect.
  - Antiparallel degenerate case: when the old and new child directions are
    (near-)opposite, the shortest-arc axis is undefined; pick the rotation
    axis most orthogonal to the segment from the joint's current basis so the
    180° flip is deterministic (roll preservation is forfeited in this case).
  - Joint translations relative to their parents are unchanged — only
    rotations change (this is what "bone lengths do not change" means at the
    transform level).
  - The effector bone keeps its own orientation from the start of the drag
    (world orientation preserved); only its position follows the chain. Follow-up
    option: a setting to make the effector inherit rotation from the chain.
- Nodes outside the chain are not modified; children of chain bones (including
  other branches of the skeleton) follow their parents through the normal
  transform hierarchy.

### 5. Transform tool integration

- Trigger: the translate drag (`Move_tool` axis/plane handles) when the
  resolved transform target is a bone node with at least one bone parent, as
  defined in §1. Rotation and scale handles are unaffected and keep their
  current FK behavior.
- During the drag, each gizmo update solves the chain against the current gizmo
  translation target and applies the resulting transforms immediately, so the
  chain follows the cursor live at interactive rates (the chains in question
  are short — a handful of joints — so per-update solving is expected to be
  cheap).
- The solve is re-run from the drag-start pose each update (target changes are
  absolute, not incremental), so dragging back to the start position restores
  the starting pose.
- When the target is out of reach, the gizmo follows the drag target (the
  cursor's constrained position, as today), while the effector bone rests at
  the closest reachable point — the gizmo and the bone separate visibly
  rather than the gizmo sticking to the clamped effector.
- Mode control: IK-on-drag is the new default behavior for bones with a valid
  chain. A Transform tool setting (checkbox in the tool's settings, similar to
  existing translate settings) allows disabling IK to get today's plain FK
  translation.
- Physics motion-mode handling, node-touched messages, and dependency updates
  follow the same pattern as the existing multi-node translate path
  (`Transform_entry`).

### 6. Undo / redo

- One drag gesture produces one undoable operation covering every node whose
  transform the solver changed (chain root through effector), using the
  existing node transform operation machinery (`Node_transform_operation` /
  compound operation), consistent with how multi-node translate drags are
  recorded today.
- Undo restores the exact pre-drag pose of all affected bones.

### 7. Visual feedback

- Minimum for the first version: the existing bone visualization (bone
  proxies) simply follows the solved pose — no new rendering is strictly
  required.
- Desirable (small, may ship with v1 if cheap): highlight the bones
  participating in the active chain during the drag, and mark the chain root
  (e.g. via `erhe::renderer::Primitive_renderer`, which draws the translate
  drag guides today).
- The Properties window flag list showing `ik_lock` doubles as the way to see
  and edit lock state; a distinct item tree icon or badge for locked bones is
  a follow-up, not required.

## Out of scope for Phase 1 — scheduled in later phases

These are not v1 requirements, but they are planned; see
`rigging-tools-plan.md` for the phase definitions.

- Joint constraints (rotation limits, DOF locks, hinge/ball types) — Phase 2.
- Pole vectors / swivel angle control for elbow-knee direction — Phase 2.
- Persistent IK setups (stored IK constraints/handles as scene items; chains
  here are discovered per-drag) — Phase 4 (constraint system).
- Animation keyframing of IK results (v1 works only on the live pose) —
  Phase 7 (drivers + animation integration).
- Multiple effectors, sub-bases, closed loops (full FABRIK generality) —
  future/research.
- IK on non-bone node chains (may fall out naturally later, but not required
  and not exposed in v1) — unscheduled.
- XR controller drag path (desktop viewport drag first; XR should not break,
  it just keeps FK behavior if not trivially supported) — unscheduled.

## Acceptance criteria

1. Import a glTF file with a skinned character. Drag a hand bone with the
   translate gizmo: the arm chain bends to follow the cursor; bone lengths are
   unchanged; the shoulder (first IK-locked bone, once marked) does not
   translate (it may rotate so its child follows).
2. With no bone marked `ik_lock`, the chain extends to the topmost bone of the
   skeleton, whose position stays fixed.
3. Dragging a bone that is itself marked `ik_lock` performs a plain FK
   translation (no IK), as does dragging a multi-node selection.
4. Dragging the target beyond reach straightens the chain toward the target;
   the effector rests at the closest reachable point; nothing stretches. A rig
   containing zero-length bones does not produce NaNs or crashes.
5. Toggling the Transform tool's IK setting off restores today's plain
   translate behavior on bones.
6. A single undo after an IK drag restores the exact pre-drag pose.
7. `ik_lock` survives a save/load round trip of the scene.
8. Non-bone nodes and rotation/scale drags behave exactly as before.

## Open questions for review

1. Flag name: `ik_lock` vs `ik_root` vs `ik_pin` — "lock" chosen here since it
   masks the bone from IK, but naming is open.
2. Should the effector keep its world orientation during the drag (current
   proposal), or align with the last segment?
3. Should IK-on-drag be default-on for bones (current proposal), or opt-in via
   the Transform tool setting?
4. Is re-solving from the drag-start pose each update the desired feel, or
   should the solve be incremental from the previous frame's pose (converges
   with hysteresis, feels "springier")?
5. When the dragged bone has bone children (mid-chain drag, e.g. dragging an
   elbow), v1 still treats it as the effector and its subtree follows rigidly —
   confirm that is acceptable.
