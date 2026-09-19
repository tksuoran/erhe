# Rigging Phase 2 - Interactive Test Pass

Status: in progress

The windowed, hands-on verification of rigging Phase 1 and Phase 2
(`fabrik_ik.md`, `ik_settings.md`, `pole_target.md`, `ik_drag_options.md`).
Everything listed here is already verified headlessly over MCP or by unit
tests; this pass covers what only a live mouse drag and the real ImGui
widgets reach.

Progress: sections 0-3 pass. Testing continues at section 4.

## Setup

- Editor: `build_ninja_win_vulkan\bin\editor.exe`, working directory the
  repo root.
- Asset: `res/editor/assets/RiggedFigure/RiggedFigure.glb`, imported into a
  scene, bone visualization on.
- Left arm of the figure: `arm_joint_L_1` upper arm (rotates at the
  shoulder), `arm_joint_L_2` forearm (rotates at the elbow),
  `arm_joint_L_3` hand - the bone to drag. With nothing locked the chain
  runs on through `torso_joint_3` to the root; set `ik_lock` on
  `arm_joint_L_1` to confine a drag to the arm.

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
- **ik_lock**: Properties of a bone, among the flag / lock rows.
- **Add Bone Tip Nodes**: Hierarchy, right-click the rig root.

## 0. Setup - PASS

Import the asset, select the Move tool, confirm Bone IK is on and Effector
Orientation reads "Keep World".

## 1. Plain IK drag - PASS

1. Drag `arm_joint_L_3` with the translate gizmo: the chain follows, bone
   lengths stay fixed, the hand keeps its world orientation.
2. Drag far out of reach: the arm straightens toward the target with no
   jitter.
3. Release, Ctrl+Z once: the whole arm returns in one step; Ctrl+Y restores
   it.
4. Bone IK off: only the dragged bone moves. Turn it back on.
5. `ik_lock` on an upper bone: bones above it stop moving.
6. Add Bone Tip Nodes, drag a tip node: its parent bone aims at it.

## 2. Chain visualization - PASS

During every IK drag: a cyan polyline along the chain, an orange cross at
the fixed root, a cyan cross at the effector, visible through the mesh, gone
at release. Colors and widths are the `ik_*` fields of the debug
visualization style settings.

## 3. Per-bone IK settings - PASS

1. Select `arm_joint_L_2` and find its "IK" group in Properties.
2. One Lock axis on: the elbow stops rotating about that axis (a lock on
   the bone's own twist axis has no effect, by design).
3. One Limit axis on with a narrow range (for example -10 to +45 degrees):
   the elbow stops at the limit without snapping or oscillating.
4. Ctrl+Z after each edit: one step per edit, a slider drag is one step.
5. Pose the arm, press Set rest from current pose: limits are measured from
   that pose; one Ctrl+Z undoes it.

## 4. Channel locks - NEXT

Sections 0-3 above were run while these same values lived on an "IK
Settings" node attachment; they now live on the bone node itself, so the
solver behavior they recorded is unchanged but the place to edit them is
not. Worth one spot-check before continuing: that the "IK" group appears on
a bone with nothing added first, and that Set rest from current pose still
takes one Ctrl+Z (section 3 steps 1 and 5).


1. On any node, tick for example translation X and rotation Y in "Channel
   Locks". Move and rotate it with the gizmo: the locked components do not
   change, and the Transform window greys out the locked fields.
2. Put a rotation lock on a mid-chain bone and IK-drag the hand: it behaves
   like the IK Lock of section 3.
3. Rotate a node past 90 degrees with one rotation axis locked: no sudden
   flip to another orientation.
4. Ctrl+Z: one step per lock toggle.

## 5. Pole target

1. Create an empty node and place it clearly in front of or behind the
   elbow, off the shoulder-to-hand line.
2. On the elbow bone set Pole Target to that node with the picker: the
   picker offers nodes and the row shows the chosen one. With several bones
   of a chain naming a pole, the one nearest the hand governs.
3. Drag the hand: the elbow swings to point at the pole; the visualization
   adds a magenta line from the pole to the root and a magenta cross at the
   pole. On the first small movement the elbow may jump toward the pole -
   judge whether that feels acceptable.
4. Pole Angle 90 (the row is in degrees), drag again: the bend turns a
   quarter turn about the shoulder-to-hand line.
5. Move the pole node, drag again: the elbow follows the new side. The pole
   is read once at drag start, so moving it alone does not re-pose the arm.
6. Clear the pole; Ctrl+Z after each step: one step each.
7. With a limit on the elbow as well: the limit wins over the pole and the
   arm does not shake while they disagree.

## 6. Effector orientation

1. Effector Orientation "Follow Last Segment", drag the hand a long way: the
   hand turns with the forearm.
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
