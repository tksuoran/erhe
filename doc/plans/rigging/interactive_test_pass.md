# Rigging Phase 2 - Interactive Test Pass

Status: in progress

The windowed, hands-on verification of rigging Phase 1 and Phase 2
(`fabrik_ik.md`, `ik_settings.md`, `pole_target.md`, `ik_drag_options.md`).
Everything listed here is already verified headlessly over MCP or by unit
tests; this pass covers what only a live mouse drag and the real ImGui
widgets reach.

Progress: sections 0-2 pass. Section 3 is re-run on the test rig below
(twist-axis locks are enforced now), then testing continues at section 4.

## Setup

- Editor: `build_ninja_win_vulkan\bin\editor.exe`, working directory the
  repo root.
- Asset: `res/editor/assets/skin_test/skin_test_3_boxes.glb`, imported into
  a scene, bone visualization on. Three boxes skinned to one chain,
  `bone_0` -> `bone_1` -> `bone_2`, each bone 1 unit long along its local
  +Y, `bone_0` at the origin, all rotations identity. So for every bone the
  twist axis is Y (turning about its own length) and the swing axes are X
  and Z.
- Right-click the rig root in the Hierarchy and run Add Bone Tip Nodes: this
  adds `bone_2 tip` under `bone_2`. `bone_2 tip` is the node to drag - the
  chain then aims all three bones. Dragging `bone_2` itself also works, but
  then only `bone_0` and `bone_1` rotate (a dragged bone is never aimed).
- With nothing locked the chain runs to `bone_0`; tick IK Lock on `bone_1`
  to confine a drag to `bone_1` and `bone_2`.
- Reading a result: select the bone and read the "Rotation" row of
  Properties (local Euler angles in degrees, X Y Z), or the Transform window
  in Local mode.

## Where things are in the UI

- **Move tool parameters** (Snap Enable, Bone IK, Effector Orientation):
  the Transform window, below the translation / rotation / scale / skew
  fields, in a "Move tool" group. The group appears once a gizmo handle has
  been hovered or dragged (finding F1).
- **Lock / Limit / Stiffness / Rest Rotation / Pole Target / Pole Angle
  rows**: Properties, group "IK" of the bone node itself. They appear on any
  node the rig marks as a bone; nothing has to be added first.
- **Set rest from current pose**: Properties, in the bone node's own framed
  section (headed by its type and name), row "Rest", above the node's
  property groups and so above the "IK" group whose Rest Rotation row it
  writes. Greyed out while the bone has `lock_edit`.
- **Channel locks**: Properties of a node, group "Channel Locks".
- **ik_lock**: Properties of a bone, the "IK Lock" row of the "IK" group
  (ends the chain at that bone; not an axis lock).
- **Add Bone Tip Nodes**: Hierarchy, right-click the rig root.

## 0. Setup - PASS

Import the asset, select the Move tool, confirm Bone IK is on and Effector
Orientation reads "Keep World".

## 1. Plain IK drag - PASS

1. Drag the effector with the translate gizmo: the chain follows, bone
   lengths stay fixed, the effector keeps its world orientation.
2. Drag far out of reach: the chain straightens toward the target with no
   jitter.
3. Release, Ctrl+Z once: the whole chain returns in one step; Ctrl+Y
   restores it.
4. Bone IK off: only the dragged bone moves. Turn it back on.
5. IK Lock on `bone_1`: `bone_0` stops moving.
6. Add Bone Tip Nodes, drag a tip node: its parent bone aims at it.

## 2. Chain visualization - PASS

During every IK drag: a cyan polyline along the chain, an orange cross at
the fixed root, a cyan cross at the effector, visible through the mesh, gone
at release. Colors and widths are the `ik_*` fields of the debug
visualization style settings.

## 3. Per-bone IK settings - NEXT

Start each step from the straight rest pose (Ctrl+Z back to it) with no IK
values set, unless the step says otherwise. "Drag" means: select
`bone_2 tip`, Move tool, Bone IK on, drag the gizmo.

1. **The group is there.** Select `bone_1`. Properties shows a group "IK"
   with IK Lock, Lock X/Y/Z, Limit X/Y/Z, Limit Min, Limit Max, Stiffness,
   Rest Rotation, Pole Target, Pole Angle - with nothing added first. Select
   the non-bone node `skin_test_3_boxes`: no "IK" group.
2. **One swing lock.** On `bone_1` tick Lock Z. Drag the tip sideways along
   world X (the direction a Z rotation would serve): `bone_1`'s Rotation Z
   stays 0 (a fraction of a degree at most, when the bone also picks up
   twist) during and after the drag while `bone_0` and `bone_2` still bend
   about Z. Drag along world Z: `bone_1` bends about X as before. Untick.
3. **Twist lock.** On `bone_1` tick Lock Y. Drag the tip in a circle around
   the chain, out of any single plane: `bone_1`'s Rotation Y stays 0; without
   the lock the same gesture leaves a few degrees of Y on it. Untick.
4. **Hinge.** On all three bones tick Lock Y and Lock Z. Drag the tip along
   world Z: the chain curls in the YZ plane, every bone's Rotation reads
   (x, 0, 0). Drag the tip along world X, out of that plane: the target is
   unreachable for a hinge chain, so the tip stays in the YZ plane at a
   best-effort position, does not follow the mouse sideways, and does not
   shake; Rotation Y and Z still read 0 on all three bones.
5. **All three locked.** On `bone_1` tick Lock X as well: the segments of
   `bone_0` and `bone_1` move as one rigid link, `bone_0` and `bone_2` still
   solve. Untick all locks on all bones.
6. **Limit.** On `bone_1` tick Limit X and set Limit Min X to -10 and Limit
   Max X to 45 (the rows are in degrees; Min accepts -180..0, Max 0..180).
   Drag the tip along +Z and then -Z: `bone_1`'s Rotation X stops at 45 one
   way and -10 the other, reached smoothly - no snap when the limit engages,
   no oscillation while the mouse keeps pulling past it. The other bones take
   up the rest of the motion.
7. **Lock wins over limit.** With the limit of step 6 still on, tick Lock X
   on `bone_1`: Rotation X no longer changes at all.
8. **Undo granularity.** Ctrl+Z repeatedly: each checkbox tick is one step,
   each Limit Min / Max slider drag is one step (not one per mouse move),
   each IK drag is one step for the whole chain.
9. **Rest.** Clear everything. Rotate `bone_1` to about 30 degrees about X
   with the Rotate tool. Press "Set rest from current pose" (Properties, the
   "Rest" row in the bone's framed section at the top): Rest Rotation in the
   IK group now holds that rotation. Set Limit X to -10 .. 45 again and drag:
   `bone_1` now stops at 30 - 10 = 20 and 30 + 45 = 75 degrees, the limits
   being measured from the rest pose. One Ctrl+Z undoes the Set rest press.

## 4. Channel locks

Channel locks are the "Channel Locks" group of Properties (Translation /
Rotation / Scale X Y Z), offered on every node. They stop the Transform
tools from changing a component, and their rotation entries also act as IK
axis locks. They are a different thing from the IK group's Lock X/Y/Z, which
only IK reads.

1. **Tool masking on a plain node.** Select a non-bone node (for example a
   shape from the Operations window). In "Channel Locks" tick Translation X
   and Rotation Y.
   - Move tool, drag the X arrow: the node does not move. Drag the XY plane
     handle: it moves in Y only. The Translation X value in Properties and
     in the Transform window is unchanged.
   - Rotate tool, drag the Y ring: nothing. Drag the X ring: Rotation X
     changes and Rotation Y stays at its value.
   - Transform window: the Translation X and Rotation Y fields are greyed
     out and cannot be typed into; the others still work.
   - Scale is untouched by either lock.
2. **Rotation channel lock as an IK lock.** Clear the IK group's own locks
   on all bones. On `bone_1` tick Channel Locks > Rotation Z, then IK-drag
   the tip along world X: `bone_1`'s Rotation Z stays put exactly as with
   the IK group's Lock Z in section 3 step 2. Tick Rotation Y as well and
   repeat the out-of-plane drag of section 3 step 4: Rotation Y and Z of
   `bone_1` stay put. (With channel locks alone the constraint is measured
   from the pose at drag start, so a bone already rotated about a locked
   axis keeps that rotation rather than returning to zero.)
3. **No flip past 90 degrees.** On a plain node tick Channel Locks >
   Rotation X only. With the Rotate tool turn it about Y in one long drag
   through 90 degrees and on to about 170: the orientation changes
   continuously - no sudden jump to a different orientation near 90, and
   Rotation X still reads its starting value at the end. Repeat about Z.
4. **Undo.** Ctrl+Z steps back one lock toggle at a time, and each gizmo
   drag made under a lock is one step.
5. Untick every channel lock before section 5.

## 5. Pole target

Here the root is `bone_0`, the elbow is `bone_1` and the hand is
`bone_2 tip`. Bend the chain a little first - a straight chain has no bend
for a pole to aim.

1. Create an empty node and place it clearly in front of or behind the
   elbow, off the root-to-hand line.
2. On the elbow bone set Pole Target to that node with the picker: the
   picker offers nodes and the row shows the chosen one. With several bones
   of a chain naming a pole, the one nearest the hand governs.
3. Drag the hand: the elbow swings to point at the pole; the visualization
   adds a magenta line from the pole to the root and a magenta cross at the
   pole. On the first small movement the elbow may jump toward the pole -
   judge whether that feels acceptable.
4. Pole Angle 90 (the row is in degrees), drag again: the bend turns a
   quarter turn about the root-to-hand line.
5. Move the pole node, drag again: the elbow follows the new side. The pole
   is read once at drag start, so moving it alone does not re-pose the arm.
6. Clear the pole; Ctrl+Z after each step: one step each.
7. With a limit on the elbow as well: the limit wins over the pole and the
   arm does not shake while they disagree.

## 6. Effector orientation

1. Effector Orientation "Follow Last Segment", drag the hand a long way: the
   tip turns with `bone_2`.
2. Back to "Keep World": the hand holds its world orientation again.

## 7. Persistence

1. Save the scene with a pole, a limit and a channel lock set; close and
   re-open: all three intact, the pole names the same node.
2. Import the saved `.glb` into another scene: the pole binds to the
   imported copy of the pole node.
3. Save as USD: `logs/log.txt` holds a warning that IK settings are not
   written (expected until Phase 4).
4. After closing a scene wait a few seconds: `logs/log.txt` holds no
   `scene-close leak` line.

## 8. Verdicts wanted

- Mid-chain drag (for example the elbow): the dragged bone is the effector
  and its children follow rigidly - acceptable?
- Each drag re-solves from the drag-start pose, so dragging back restores
  the start pose exactly - right feel, or should the solve be incremental?
- Is the constrained solver stable enough that stiffness is worth adding
  next (`ik_settings.md` section 1)?

## Findings

- **F1.** The Transform window shows no tool parameter group until a gizmo
  handle has been used once. Queued in `prompt_queue.txt`, to fix after
  this pass.
- **F2.** "Set rest from current pose" sits above the bone node's property
  groups, apart from the "IK" group that holds the Rest Rotation row it
  writes - hard to find. Candidate: move it next to that row.

## Reporting a problem

Name the gesture and the bone. A misbehaving path gets `log_*`
instrumentation and a joint reproduction; a crash is reproduced under the
Visual Studio debugger.
