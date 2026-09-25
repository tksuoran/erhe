# Rigging Phase 2 - Interactive Test Pass

Status: in progress

The windowed, hands-on verification of rigging Phase 1 and Phase 2
(`fabrik_ik.md`, `ik_settings.md`, `pole_target.md`, `ik_drag_options.md`).
Everything listed here is already verified headlessly over MCP or by unit
tests; this pass covers what only a live mouse drag and the real ImGui
widgets reach.

Progress: sections 0-2 pass by hand. Sections 0-8 are automated (below);
every check passes except the section 8.3 stability sweep, which finds the
jumps of finding F7. What is left for a person is the two behaviour
choices the script prints as DECISION lines (section 8).

## Automated run

`scripts/ik_interactive_pass_verify.py` runs sections 0-8 against the
headless editor and prints one PASS / FAIL line per check (68 checks, about
5 minutes), then the DECISION lines - behaviour the checks measure but a
person chooses:

    py -3 scripts/ik_interactive_pass_verify.py --launch
    py -3 scripts/ik_interactive_pass_verify.py --port N --section 3 --section 4

`--launch` starts `build_vs2026_vulkan_headless/bin/Debug/editor.exe` from
the repo root, reads its MCP port from `logs/log.txt` and asks it to exit
at the end; without it the script drives an editor already running on
`--port` (default `ERHE_MCP_PORT` or 3743). It sets up its own scene from
the asset of Setup below (Add Bone Tip Nodes through the Hierarchy context
menu), aims the scene camera, and restores the rig before each check.

- **The gestures a check is about are real input.** Checkboxes, the Set
  Rest button, the Limit Min slider and the Effector Orientation and Solve
  From combos are
  clicked in the Properties / Transform windows; Ctrl+Z / Ctrl+Y are key
  chords over the viewport; the plain drag of 1.1, the channel-lock masking
  of 4.1 and one long ring drag of 4.3 are mouse drags on the gizmo handles
  (`doc/agents/mcp_ui_driving.md`). Every other drag goes through the
  Transform tool's drag over MCP (`drag_selection`, held and retargeted with
  `action: "move"` per step): the same drag as a handle press - the Move
  tool active, so the bone chain solves IK - with exact world deltas, the
  rig read after every step.
- **Measures.** A bone angle is the solver's own: the rest-relative
  rotation split into swing and twist about the bone's Y, a swing angle
  2 asin of its quaternion component, so a 45 degree limit reads 45. A
  locked axis may move at most 0.5 degrees; "smooth" means the step onto a
  limit is at most three times the drag's median step; "steady" means the
  samples at the limit stay within 1 degree; "no shaking" means the
  per-step motion reverses direction at most once. A "jump" is a drag step
  that moves a joint more than 5 times as far as the target moved.
- **Screenshots** of the visualization checks are kept in
  `logs/ik_interactive_pass/`; they need Pillow (`py -3 -m pip install
  pillow`), without it those checks become MANUAL lines. The x-ray check
  finds the chain pixels the mesh covers by comparing the frame with the
  same frame with the mesh hidden, and measures the RGB distance between the
  drawn line and the mesh behind it there.
- **The Pole Target picker** is driven as a user does: the row's picker
  arrow (`Pole Target.pick`, doc/erhe/imgui.md "Item recorder"), then the
  node in the list it opens; the row's clear button is `Pole Target.clear`.
- **The stability sweep** (8.3) draws 12 scenarios from a fixed seed: per
  bone a lock, a hinge, a limit around the rest and the start angle, or
  nothing; a pole on the elbow in about a third; a 24-step random walk of
  the target. `make_sweep_scenarios()` / `run_sweep_scenario()` replay one
  scenario alone.
- **Session state.** Move tool parameters (Bone IK, Effector Orientation,
  Solve From) live as long as the editor runs; the script sets them instead
  of assuming the startup values.

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
  fields, in a "Move tool" group (next to the "Rotate Tool" group), shown
  from startup.
- **Lock / Limit / Stiffness / Rest Rotation / Pole Target / Pole Angle
  rows**: Properties, group "IK" of the bone node itself. They appear on any
  node the rig marks as a bone; nothing has to be added first.
- **Set rest from current pose**: Properties, group "IK", the row "Set
  Rest" directly below Rest Rotation. Greyed out while the bone has
  `lock_edit`.
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

## 2. Chain visualization - PASS (automated)

During every IK drag: a cyan polyline along the chain, an orange cross at
the fixed root, a cyan cross at the effector, visible through the mesh, gone
at release. Colors and widths are the `ik_*` fields of the debug
visualization style settings. Measured through the mesh: the line shows on
97% of the chain points the mesh covers, at a median RGB distance of 269
from the mesh colour behind it.

## 3. Per-bone IK settings - PASS (automated)

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
   "Set Rest" row below Rest Rotation in the IK group): Rest Rotation in the
   IK group now holds that rotation. Set Limit X to -10 .. 45 again and drag:
   `bone_1` now stops at 30 - 10 = 20 and 30 + 45 = 75 degrees, the limits
   being measured from the rest pose. One Ctrl+Z undoes the Set rest press.

## 4. Channel locks - PASS (automated)

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
   - Transform window in Local mode: the Translation X field is greyed
     out and cannot be typed into; the other translation fields still work.
     The rotation fields stay editable (the Euler / quaternion / axis-angle
     views mix axes), and a rotation typed there keeps Rotation Y at its
     value.
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

## 5. Pole target - PASS (automated)

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
   pole. A start pose whose bend is off the pole snaps onto the pole in
   the first drag step, and only then (measured: the bend is on the pole
   from step 1 on, later steps small); whether to ease that snap in is a
   section 8 decision.
4. Pole Angle 90 (the row is in degrees), drag again: the bend turns a
   quarter turn about the root-to-hand line.
5. Move the pole node, drag again: the elbow follows the new side. The pole
   is read once at drag start, so moving it alone does not re-pose the arm.
6. Clear the pole; Ctrl+Z after each step: one step each.
7. With a limit on the elbow as well: the limit wins over the pole and the
   arm does not shake while they disagree.

## 6. Effector orientation - PASS (automated)

1. Effector Orientation "Follow Last Segment", drag the hand a long way: the
   tip turns with `bone_2`.
2. Back to "Keep World": the hand holds its world orientation again.

## 7. Persistence - PASS (automated)

1. Save the scene with a pole, a limit and a channel lock set; close and
   re-open: all three intact, the pole names the same node.
2. Import the saved `.glb` into another scene: the pole binds to the
   imported copy of the pole node.
3. Save as USD: `logs/log.txt` holds a warning that IK settings are not
   written (expected until Phase 4).
4. After closing a scene wait a few seconds: `logs/log.txt` holds no
   `scene-close leak` line.

## 8. Behaviour and decisions - automated, decisions wanted

Measured (checks 8.1 - 8.4):

1. Mid-chain drag: dragging `bone_1` makes it the effector - `bone_0` aims
   at the target, `bone_1` keeps its world orientation and everything below
   it follows rigidly.
2. Solve From "Drag Start" (the default): each drag step solves from the
   drag-start pose, so a target reached by two paths gives the same pose,
   and back at the start the start pose.
3. Stability sweep: locks, limits and bone lengths hold in every scenario,
   and a replayed drag gives the same poses. Jumps: only with a pole and a
   hinge at the chain root (finding F7); so the constrained solver is not
   yet stable enough to add stiffness on top.
4. Solve From "Previous Step" (`ik_drag_options.md` 3.6 criterion 2), chosen
   in the Move tool's combo: a drag pulling the chain straight out of reach
   and back to its start ends in a pose more than 1 degree off the start
   pose on some bone, where "Drag Start" restores the start pose; no step of
   either drag moves a joint more than 5 times the target's step, and each
   drag is one undo step. The check leaves Solve From at "Drag Start".

Decisions for the user (the script prints them as DECISION lines):

- Mid-chain drag keeps the children rigid (1.) - or keep the chain's end in
  place (a two-target solve)?
- The pole snap on the first step (5.3) - or ease the swivel in?

## Findings

- **F1.** Fixed: the Transform window showed no tool parameter group until a
  gizmo handle had been dragged once. The Move and Rotate tool groups are now
  drawn from startup.
- **F2.** Fixed: the default Rest Rotation of a bone (its bind pose) followed
  the current pose, so limits without an explicit rest were measured from
  the pose (a bone at 20 degrees stopped at 20 + 45). The bind pose is now
  read from the inverse bind matrices alone.
- **F3.** Fixed: editing a parent bone (Properties, MCP, undo) while its
  child is selected left the child's drag baseline and the gizmo at the
  child's old position; the next drag started from there.
- **F4.** Fixed: closing a scene could crash the next hover through the ID
  renderer's id-range table, which still named the closed scene's meshes.
- **F5.** Stiffness is a normal IK row now (it was developer-only); the
  solver still ignores its value.
- **F7.** Open: with a pole on the elbow and a hinge (Lock Y + Z) on the
  chain root, a drag jumps - joints move up to 13.5 times as far as the
  target in one step - and the hand misses targets the unpoled solve
  reaches (sweep scenario 0, seed 20260925; also seen with only the hinge
  and the pole). The pole's swivel about the root-to-hand line, applied
  between the forward and the backward pass, is a rotation the hinge root
  cannot make; the backward pass clamps it away and the solve stalls at a
  different best effort from one step to the next.
- **F6.** Future work (deferred; not queued): in about one full automated run of five, the
  Set Rest click of check 3.9 records no operation, and a second click right
  after does not either; three diagnostic runs of sections 1-3 did not
  reproduce it. Not yet known whether the button or the injected click is at
  fault. The script reports it as a 3.9 FAIL.

## Reporting a problem

Name the gesture and the bone. A misbehaving path gets `log_*`
instrumentation and a joint reproduction; a crash is reproduced under the
Visual Studio debugger.
