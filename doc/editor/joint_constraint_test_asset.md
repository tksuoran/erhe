# Joint constraint test asset

Stability: stable

A scene with one of each kind of constraint the editor authors, for checking
the joint constraint visualization (`doc/editor/tools.md`
"Debug_visualizations") by hand and by script. File:
`res/editor/assets/joint_constraints/joint_constraints.glb`, built by
`scripts/creations/creation_23_joint_constraint_test.py`, whose `STATIONS`
and `IK_CASES` tables are the definition of every case below.

## Opening it

1. File > Load Scene, pick the file. The scene opens with the simulation
   paused (Physics window, "Physics enabled" off).
2. In the viewport's Debug Visualizations popup set **Joint Constraints** to
   **All**; leave **Physics Joints** and **IK Limits** checked.

## Physics joints (back row, left to right)

Every station is a static grey post with an orange 1 kg box hanging 0.55 m
below it; the joint joins a `Hinge` anchor under the box to a `Pivot` anchor
under the post. Orange lines are enforced limits, grey thin lines free axes,
white spokes the current values (red when out of range), magenta limits the
backend enforces differently from how they were authored, and a grey
dimmed station has no live constraint.

| Key | Station | Settings | Look for |
|---|---|---|---|
| P01 | Hinge limited | rotation Z +-0.785, rest fixed | a 90 degree arc (Jolt: the swing of frame B's X axis; Box3D: a twist arc about Z) |
| P02 | Hinge free | rotation Z free, rest fixed | a full thin circle |
| P03 | Hinge asymmetric | rotation Z -0.3 .. 1.2 | an arc offset to one side |
| P04 | Ball limited | all rotations +-0.5 | Jolt: pyramid swing + twist arc; Box3D: cone + twist, magenta |
| P05 | Swing pyramid asymmetric | X fixed, Y -0.2 .. 0.8, Z -0.6 .. 0.3 | Jolt: an off-center pyramid; Box3D: magenta cone |
| P06 | Ball free | rotations free | frames only, no limit geometry |
| P07 | Slider Y limited | Y -0.3 .. 0.1, rest fixed | a vertical segment with end ticks |
| P08 | Slider X free | X free, rest fixed | a thin horizontal line |
| P09 | Weld | everything fixed | frames only; the box is welded to the static post and cannot be moved |
| P10 | Free six-DOF | no settings | three thin free translation lines |
| P11 | Fixed off zero | Y fixed at 0.1, rest fixed | magenta on both backends (a fixed axis is enforced at 0); a weld like P09, so the box cannot be moved |
| P12 | Double pendulum | upper hinge Z +-0.8, lower Z +-1.0 | two stacked joints, the lower one between two dynamic boxes |
| P13 | Inactive joint | as P01, joint `active` off | dimmed grey; no constraint, so the box falls (turn `active` on to get P01's hinge) |

## IK limits (front row, left to right)

Each is a three-bone chain (`<key> bone_0` .. `bone_2`) skinned to a column
of three blue boxes; the Ik.* values sit on the middle bone `<key> bone_1`,
which also carries IK Lock, so an IK drag of `bone_2` stops the chain there
and turns the limited bone alone.
Green is the swing boundary at bone length, yellow the twist range, white
the current bone direction and twist (red when outside the limits).

| Key | Case | Middle bone values | Look for |
|---|---|---|---|
| I01 | Twist limited | limit Y +-0.5 | a small yellow twist arc only |
| I02 | Swing symmetric | limit X, Z +-0.6 | a round cone |
| I03 | Swing asymmetric | limit X -0.2 .. 0.9, Z -0.7 .. 0.3 | a lopsided cone |
| I04 | One swing axis | limit X -0.3 .. 0.8 | a band: two boundary curves |
| I05 | Elbow hinge | lock Y, Z; limit X 0 .. 2.0 | one arc in the bending plane |
| I06 | Locked | lock X, Y, Z | the current spoke only |
| I07 | All axes limited | limit X +-0.5, Y +-0.3, Z +-0.4 | a cone plus a twist arc |
| I08 | Posed outside | as I02, bone posed 1 rad about X | the white spoke outside the cone, drawn red |

## Interactive checks

- **Filter.** Set Joint Constraints to **Hovered Mesh** and hover the
  boxes: an orange box shows its station's joint (P12's upper box shows
  both of its joints); a blue column shows its middle bone's limits. Set
  it to **Hovered Bone**: switch to bone selection mode and hover a middle
  bone, or hover `<key> bone_1` in the Scene Hierarchy. Hovering nothing
  draws nothing in either mode.
- **Physics.** Enable physics and drag the boxes with the Physics tool:
  the current-value spokes follow and stay inside the limits; P10 and P13
  (no live constraint) fall; P11 stays where it was built - both backends
  hold the fixed axis at zero, not at the authored 0.1, which is what its
  magenta marks.
- **IK.** With the Move tool and Bone IK on, drag a `bone_2`: only the
  middle bone turns (IK Lock makes it the chain root), it stops at its
  green / yellow boundary, I06 does not bend, and during the drag the
  visual uses the drag's own constraint.
- **Save / reload.** Save the scene elsewhere and load it again: P13 is
  still inactive and every joint keeps its name.

## Automated checks

`py -3 scripts/joint_constraint_assets_verify.py` against a running editor
loads the file and checks the reported state of every station and bone, the
filter, IK drags that bend each chain against its limits, and physics
stations holding under an impulse; it detects the physics backend from the
hinge's enforced shape. Rebuild the asset with
`py -3 scripts/creations/creation_23_joint_constraint_test.py --reuse
--scene-only` after changing the tables.
