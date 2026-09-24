# Pole Target and Swivel Control - Phase 2 Requirements

Status: in progress

Implemented; awaiting live-editor testing (`interactive_test_pass.md`).

This document specifies the pole target slice of Phase 2 of the rigging
roadmap in `rigging_tools.md`. It builds on `fabrik_ik.md` (Phase 1,
implemented: chain discovery, FABRIK, rotation-only write-back) and on
`ik_settings.md` (Phase 2 slice 1, implemented: the bone node's `Ik.*`
properties, channel locks, the `Ik_solver` / `Ik_chain` interface, the
constrained backward pass).

Terms used throughout:

- **Chain**: the joint list `Ik_drag::begin` discovers for one drag,
  `joints[0]` the fixed root and `joints[n]` the effector
  (`fabrik_ik.md` section 1).
- **Intermediate joint**: a chain joint that is neither the root nor the
  effector, i.e. indices `1 .. n-1`.
- **Unpoled solve**: the solve of `fabrik_ik.md` / `ik_settings.md` with no
  pole step, which is what the solver performs whenever no admissible pole
  governs the drag (R5) or the pole has no effect (R10, R11 steps 1, 3
  and 4).

## Motivation

A chain with more than one segment has a one-parameter family of solutions
for any reachable target: the whole bend can swivel about the line from the
chain root to the effector. FABRIK picks whichever member of that family the
previous pose happens to be nearest, so an elbow or a knee drifts sideways
during a drag and ends up bending backwards. A pole target names the
direction the bend should face: a node the chain's bend plane is made to
contain, plus a scalar offset angle about the root-to-effector line. This is
the control Blender's IK constraint exposes as Pole Target + Pole Angle, and
it is what makes dragging a hand or a foot produce a believable limb.

## Functional requirements

### 1. Data model

**R1.** Two of the `Ik.*` attached properties of a joint node
(`ik_settings.md` section 1, `doc/erhe/property_system.md` section 4.19)
carry the pole, in the same UI group "IK":

- `Ik.pole_target` - `erhe::property::Property<erhe::property::Weak_object_reference>`,
  label "Pole Target", `reference_item_types = erhe::Item_type::xformable`,
  `inherits = false`. The weak reference kind (D28) is what keeps a pole
  from being an ownership edge: the property stores a
  `std::weak_ptr<Dependency_object>`, a target that has gone away reads as
  an empty reference, and its `validate` is
  `Member_value_traits<std::weak_ptr<erhe::scene::Node>>::validate`, so a
  non-`Node` item is refused with the property's own error. It is read and
  written through `get_ik_pole_target` / `set_ik_pole_target`
  (`scene/ik_properties.hpp`).
- `Ik.pole_angle` - `erhe::property::Property<float>`, label "Pole Angle",
  entry stored, `inherits = false`, default `0.0f`, stored in radians.

**R2.** Being weak (R1), a pole is a reference and never a cycle. Entry
storage copies with the node (D10), so a cloned or prefab-instantiated bone
keeps naming the same pole node.

**R3.** `Ik_settings_data` carries `float pole_angle{0.0f}`, filled by
`read_ik_settings` like every other value. The pole node itself is **not**
in that record: the record is plain values, and the reference is read
through `get_ik_pole_target()`.

**R4.** `set_ik_pole_target(erhe::scene::Node&, const
std::shared_ptr<erhe::scene::Node>&)` accepts any node, including the
joint's own node and nodes of other scenes. All admissibility rules live at
solve time (R8), stated once, so no edit is silently discarded and the
Properties row always shows what was authored.

### 2. Which pole governs a drag

**R5.** Chains stay discovered per drag (`fabrik_ik.md` section 1); a pole
does not create, extend or terminate a chain. The pole that governs one drag
is found by scanning the chain from the effector toward the root - indices
`n, n-1, ... 0` - and taking the first admissible pole (R8) a scanned joint
node's `Ik.pole_target` names. The scan reads every chain joint's value
whatever the joint's flags, so a pole authored on the effector (the
Blender-equivalent place, where the IK constraint itself lives) wins over a
pole authored higher up. The scan continues past a joint that names no pole,
whose `Ik.pole_target` no longer resolves, or whose pole is not admissible,
so a pole authored nearer the root governs the drag in each of those cases.
The solve is unpoled when the scan reaches the root having found no
admissible pole.

**R6.** `Ik.pole_angle` is the effective value of the same joint node R5
selected. Angles of the other joints on the chain take no part; there is no
summing.

**R7.** The governing pole applies to the whole chain: every intermediate
joint of the chain is swivelled by the single rotation of R11 step 7,
whatever the chain length.

**R8.** An `Ik.pole_target` **resolves** when its weak reference locks to a
live `erhe::scene::Node`; one that does not - expired, or never authored - counts
as not authored, so the scan of R5 passes it by in silence. A resolved
`pole_target` is **admissible** when all of the following hold, evaluated
once per drag in `Ik_drag::begin`:

- the node's `get_item_host()` equals the effector's `get_item_host()`. A
  same-host test is what the doc/editor/coding_rules.md rule "Scene-hosted references in
  editor parts" asks for here: the chain being dragged is by construction
  hosted by a live registered scene, so a pole sharing that host is live
  too, and every pole in a closed or foreign scene is excluded by the same
  test without `Ik_drag` needing `App_context`;
- the node is active - its own `active` opinion and its derived
  `erhe::Item_flags::active` bit are both set;
- the node is not itself one of the chain's joints (a joint cannot swivel
  about a direction it defines).

When the scan of R5 reaches the root having found no admissible pole while at
least one joint named a non-admissible one, the whole drag is solved unpoled
and one warning names the effector, the first rejected pole in scan order and
its failing condition, logged once at `Ik_drag::begin` (never per solver
update). A scan that finds an admissible pole is silent, whatever it rejected
on the way.

**R9.** A pole node that is a **descendant** of a chain joint is admissible.
Its world position is captured once at `Ik_drag::begin` (R12), so the chain
moving under it during the drag feeds nothing back into the solve and the
"absolute target, re-solve from the drag-start pose" property of
`fabrik_ik.md` section 5 is preserved.

**R10.** A chain of exactly two joints (root + effector, one segment) has no
intermediate joint: the pole has no effect and the solve is unpoled. This is
a legitimate short chain, so it produces no warning.

### 3. Solver - pole reprojection

**R11.** A free function in `src/editor/transform/ik_solver.{hpp,cpp}`,
beside `fabrik_solve`, performs the whole pole step on world positions:

```
void ik_apply_pole(std::vector<glm::vec3>& positions, glm::vec3 pole_position, float pole_angle);
```

With `p = positions`, `n + 1 = p.size()`, and
`eps = 1e-4 * (sum of segment lengths)` - a relative epsilon, so the rule is
scale independent:

1. `axis_raw = p[n] - p[0]`. If `length(axis_raw) < eps`, return unchanged
   (the chain is folded onto its root; the swivel line does not exist).
   Otherwise `a = normalize(axis_raw)`.
2. For a point `v`, its **perpendicular offset** is
   `perp(v) = (v - p[0]) - a * dot(v - p[0], a)`.
3. The chain's **bend direction** is
   `bend_raw = sum over i in [1, n-1] of perp(p[i])`, equally weighted. If
   `length(bend_raw) < eps`, return unchanged (a straight chain, or one
   whose intermediate joints cancel about the axis, has no bend to aim).
   Otherwise `b = normalize(bend_raw)`. For the three-joint case this is
   exactly the single elbow's perpendicular offset; for longer chains it is
   the mean bend, and R11 step 7 rotates the solved shape rigidly, so a
   longer chain's shape is preserved and only its mean bend is aimed.
4. `pole_raw = perp(pole_position)`. If `length(pole_raw) < eps`, return
   unchanged: the pole lies on the root-to-effector line and names no
   direction. Otherwise `d = normalize(pole_raw)`.
5. `d_target = angleAxis(pole_angle, a) * d` - `pole_angle` is a
   right-handed rotation about `a`, so a positive angle turns the bend
   counter-clockwise seen from the effector looking back at the root.
6. `theta = atan2(dot(cross(b, d_target), a), dot(b, d_target))`, the signed
   angle about `a` from the current bend direction to the wanted one.
7. `q = angleAxis(theta, a)`; for `i` in `[1, n-1]`,
   `p[i] = p[0] + q * (p[i] - p[0])`.

Every branch returns a finite result: the three guarded lengths are the only
divisions, so the function never produces NaN.

**R12.** `Ik_chain` gains `bool has_pole{false}`, `glm::vec3
pole_position{0.0f}` (world space, the same space as `positions` and
`target`) and `float pole_angle{0.0f}`. `Ik_drag::begin` fills them from the
governing joint node (R5, R6) and the admissible pole's world position (R8,
R9); `Ik_drag::apply` copies them into the chain like the other drag-start
data. When `has_pole` is false the solver runs unchanged.

**R13.** Where the step runs inside `Fabrik_solver::solve`:

- **Unconstrained path** (`!chain.has_constraints()`): once, on the final
  positions, after `fabrik_solve` returns. One application is exact:
  R11 step 7 is a rigid rotation about a line through `p[0]` and `p[n]`, so
  it moves neither the root nor the effector and changes no segment length -
  a converged solution maps to another converged solution, and the iteration
  count is unaffected.
- **Constrained path**: inside every iteration, between the forward pass and
  the backward pass, on the forward pass's positions. The constraint-clamping
  backward pass therefore always runs last and always produces the returned
  `positions` and `local_rotations` together, so the returned pose satisfies
  the joint limits and locks of `ik_settings.md` section 4 and its positions
  and rotations stay consistent. No pole step runs after the loop on this
  path.

**R14.** Precedence: **limits and locks win over the pole.** The pole selects
among the poses a chain may take; a limit or lock states which poses are
authored as legal, and `ik_settings.md` section 4 requires the pose handed to
write-back to satisfy them. Under tight limits the pole is therefore honored
only partially - the solver aims the bend at the pole and the clamp pulls it
back to the legal region - and the result is best effort, not an error. The
pole step is a deterministic function of the positions it is given, so it
adds no oscillation and the existing stall detector of
`ik_settings.md` section 4 keeps terminating the iteration.

**R15.** `Ik_drag::apply` routes both paths through `Ik_solver::solve` and
reads `m_chain.positions` for the unconstrained rotation-only write-back,
instead of calling `fabrik_solve` itself. This gives the pole one place to
act. `Fabrik_solver`'s unconstrained branch is otherwise untouched, so a
chain with neither constraints nor a pole still takes the Phase 1 path and
produces Phase 1 results bit for bit (`ik_settings.md` section 3).

**R16.** Unit tests in `src/editor/transform/test/test_ik_solver.cpp`
(`editor_ik_solver_tests`, `ERHE_BUILD_TESTS=ON`), on `Ik_chain` values with
no scene involved, covering:

- a three-joint chain reaching a target with a pole on one side and then the
  opposite side: the elbow's perpendicular offset points at the pole in both
  cases, the effector reaches the same target, segment lengths unchanged;
- `pole_angle` of +90 and -90 degrees on that chain: the measured swivel
  angle matches within 1e-3 rad, and the sign follows R11 step 5;
- a five-joint chain: the mean bend aims at the pole, and the relative
  geometry of the intermediate joints is the pre-pole one to within 1e-5
  (rigid rotation);
- each degenerate return of R11 (folded chain, straight chain, pole on the
  axis) leaves positions untouched and finite;
- a poled chain whose intermediate joint carries a limit from
  `ik_settings.md`: the returned pose satisfies the limit (R14) and differs
  from the unlimited poled pose;
- a two-joint chain with a pole set: identical to the same chain without one
  (R10);
- a poled chain with an unreachable target: the straight layout is returned
  unchanged (R11 step 3) and is finite.

### 4. Properties UI and undo

**R17.** Both properties of R1 draw as generic registered-property rows in
group "IK" of the joint node: `Ik.pole_target` as the object-reference row
(the picker the `reference_item_types` mask drives, as `Joint`'s
Connected Node row does) and `Ik.pole_angle` as a float row edited in degrees
and stored in radians, the way the limit rows of `ik_settings.md` section 5
are, with a drag range of -180 to +180 degrees and no coercion - the angle is
periodic, so a value outside the range is legal and means the same pose.

**R18.** Every completed edit of either property - picking a pole, clearing
it, dragging the angle - records exactly one `Property_set_operation` through
the generic Properties path, like every other `Ik.*` value
(`ik_settings.md` section 5). This slice adds no operation type.

**R19.** One drag gesture that solves with a pole still produces exactly one
undo step covering the chain (`fabrik_ik.md` section 6); the pole node's own
transform is never written by a solve.

### 5. MCP surface

**R20.** `set_item_property` and `get_item_properties` already carry
object-reference properties and need no change for R1: `set_item_property`
takes `value` as the referenced item's reference path (or its bare name),
`value: ""` to store an explicit empty reference, `value: null` or an omitted
value to clear the local value, or `reference_id` as an item id;
`get_item_properties` reports the reference as its `get_reference_path()`
string plus `reference_id` / `reference_type` / `reference_item_types`.

**R21.** Authoring a pole over MCP is `set_item_property` of
`Ik.pole_target` addressed to the bone node, with no preparation step: the
values live on the node the drag already names (R1).

**R22.** A new MCP tool `ik_drag` performs one complete IK drag gesture, so
the solver, its constraints and the pole are exercisable headlessly (a gizmo
drag is not reachable over MCP). Arguments: `scene_name` (required),
`node_id` or `node_name` naming the effector, and `target` as three world
coordinates. It runs `Ik_drag::begin` on that node, `Ik_drag::apply(target)`,
and queues one compound of `Node_transform_operation` covering the joints
whose `parent_from_node` changed, so the gesture is one undo step (R19). Its
result names, in root-to-effector order, each chain joint's id, name and
solved world position, plus the governing pole's reference path and angle
(`null` and `0` when the solve was unpoled). It returns `isError` with the
reason when no chain is discovered (fewer than two joints, an IK-locked
effector, a non-bone effector without a bone parent). Its schema entry goes
into `config/editor/mcp_tools.json` beside the other scene actions.

### 6. Serialization

**R23.** `Ik.pole_angle` rides the joint node's `ERHE_node` `properties` map
by its qualified name, like every other local `Ik.*` value
(`ik_settings.md` section 6).

**R24.** `Ik.pole_target` rides that map as the pole's reference path and
`ERHE_node` `property_node_refs` as the pole's **glTF node index** - the form
`KHR_physics_rigid_bodies` uses for a joint's `connectedNode`, so the pole is
named by position in the file's own node table. The index is what the reader
resolves, so the pole binds to the copy of the pole node this file carries,
whatever the import wraps the file's nodes in and whatever the scene already
holds under that name; the path is the readable form and the fallback for a
target the file has no node for.
`doc/gltf_extensions/ERHE_node.md` owns the specification of both keys -
which references get an index, the order the reader applies them in, and what
an unresolvable index or path does.

**R25.** The pole therefore survives both reload forms: save + open of the
scene, and `import_gltf` of the saved file into another scene, which places
the file's nodes under an import root. `scripts/scene_roundtrip_verify.py`
checks both.

**R26.** USD save carries the pole exactly as far as it carries the rest of
the rest of the `Ik.*` values, which is not at all: the USD writer has a
form for `Node_physics` and `Joint` (through the physics description),
the composition arcs of a prim, `Draw_mode` (`GeomModelAPI`) and
`Geometry_graph_mesh` (the `erhe:scene` block), and none for rig data.
`save_scene_usd` (`parsers/usd.cpp`) counts the nodes holding a local `Ik.*`
value (`count_ik_value_holders` over `has_local_ik_value`) and logs one
warning per save naming that count, stating that USD has no form for IK
settings and that they are not written.
A USD form for rig data is Phase 4 work, with the persistent constraint
model.

### 7. What the remaining Phase 2 slices own

- **Chain visualization** owns every pole visual: the line from the governing
  pole to the chain, the pole marker, and the chain and root highlight during
  a drag, all through `erhe::renderer::Primitive_renderer`. This slice draws
  nothing. Implemented; requirements: `ik_drag_options.md` section 2.
- **Effector orientation option** owns the Transform tool setting choosing
  between the effector keeping its world orientation and following the last
  segment. Implemented; requirements: `ik_drag_options.md` section 1.
- **Stiffness** owns the solver enforcement and the UI of the
  `Ik.stiffness` value, which stays inert here
  (`ik_settings.md` section 1).
- **Phase 4** owns persistent IK constraints, where a pole becomes a field of
  a stored constraint on a stored chain, and owns the USD form of rig data
  (R26).

## Out of scope

- A screen-space swivel modifier key: the pole is a scene node, authored and
  persistent (R1).
- Poles for chains other than the one discovered by the current drag; a
  second effector or a sub-base stays out (`fabrik_ik.md` "Out of scope").
- Translation or scale effects of a pole: the pole only chooses among
  rotations, and IK still writes rotations alone
  (`fabrik_ik.md` section 4).
- Automatic pole creation or placement helpers ("add a pole for this chain").

## Acceptance criteria

Headless recipe, on the `build_vs2026_vulkan_headless` editor with
`ERHE_AI_DRIVER=1`, driven with `py -3 scripts/mcp_call.py` (ids reshuffle per
launch, so re-query them). `scripts/ik_pole_verify.py` runs criteria 1 to 9
and 12 against such an editor and prints one PASS / FAIL line each.

Every measured `ik_drag` is undone before the next one runs: a drag solves
from the pose the chain is in when it begins (`fabrik_ik.md` section 5), and
the undo restores each joint's drag-start `parent_from_node`, so each
criterion below measures a drag from the same pose as the one it compares
against.

1. `create_scene`, then `import_gltf` of
   `res/editor/assets/RiggedFigure/RiggedFigure.glb` - the tracked skinned
   fixture the MCP tests and smoke tests already use - and `get_scene_nodes`
   to learn the joint ids and names. Steps 2-7 drag `arm_joint_L_3`, the tip
   of its left arm; the chain `ik_drag` reports is the one the run measures,
   whatever its length, and the measured quantity is the chain's bend
   direction of R11 step 3, computed from the reported joint positions.
2. `get_item_properties` on the effector bone lists `Ik.pole_target`,
   `Ik.pole_angle` and `Ik.limit_x`, so a pole is authorable with no
   preparation step (proves R21).
3. `ik_drag` on the effector bone toward a reachable target returns a joint
   list whose consecutive distances equal the pre-drag segment lengths to
   within 1e-3, and reports `pole` as null. Record `b`, the bend direction of
   R11 step 3, and the effector's distance to the target.
4. `create_node` a pole node, place it clear of the chain's current bend
   direction, then `set_item_property` `Ik.pole_target` on the effector bone
   naming that node (its `reference_id` or its reference path) and repeat the
   `ik_drag` of step 3 with the same target. The signed angle about `a = normalize(p_effector - p_root)`
   from the new `b` to `perp(p_pole)` (R11 steps 2 and 6) is **0 degrees,
   tolerance 2 degrees**, and the effector's distance to the target is
   unchanged from step 3 to within 1e-3.
5. The angle about `a` between step 3's `b` and step 4's `b` exceeds
   10 degrees, proving the pole, not the start pose, chose the bend. The pole
   node of step 4 is placed to make this so: at least 30 degrees about `a`
   away from step 3's `b`.
6. `set_item_property` `Ik.pole_angle` to `1.5707963` and repeat the drag: the
   angle measured in step 4 becomes **90 degrees, tolerance 2 degrees**, with
   the sign of R11 step 5.
7. `set_item_property` `Ik.pole_target` with `value: null` and repeat the
   drag: the result equals step 3's to within 1e-3 per joint.
8. Each `set_item_property` of steps 4, 6 and 7 is exactly one undo step:
   `get_undo_redo_stack` grows by one entry per call, and each `ik_drag` adds
   exactly one more (R18, R19, R22).
9. With the pole parented below another node, so that it is not a top-level
   name: `save_scene` and re-open. `get_item_properties` on the re-opened
   effector bone reports an `Ik.pole_target` at the same reference path whose
   `reference_id` is the re-opened pole node, and the same `Ik.pole_angle`
   (R23, R24). The same file imported into another scene with `import_gltf`,
   which places the file's nodes under an import root, binds the pole to the
   **imported copy** of the pole node (R25).
10. `editor_ik_solver_tests` passes, including every case of R16, and the
    pre-existing cases of `ik_settings.md` still pass unchanged.
11. A chain with neither constraints nor a pole produces the same solved
    positions as before this slice (R15), checked by the unconstrained
    equivalence test of `ik_settings.md` section 3.
12. Saving a scene whose bones hold local `Ik.*` values as USD logs the
    warning of R26 and completes.

## Key locations

- Solver step - `ik_apply_pole` in `src/editor/transform/ik_solver.{hpp,cpp}`,
  beside `fabrik_solve`; `Ik_chain::has_pole` / `pole_position` /
  `pole_angle` carry it, and `Fabrik_solver::solve` applies it on the
  unconstrained path once after `fabrik_solve` and on the constrained path
  between the forward and backward passes of every iteration. Unit tests:
  `src/editor/transform/test/test_ik_solver.cpp` (`editor_ik_solver_tests`
  target, `ERHE_BUILD_TESTS=ON` trees).
- Values - `Ik::pole_target_property` and `Ik::pole_angle_property`
  (`src/editor/scene/ik_properties.{hpp,cpp}`), read with
  `get_ik_pole_target` and through `Ik_settings_data::pole_angle`; both draw
  as generic registered-property rows in Properties group "IK".
- Drag integration - `Ik_drag::discover_pole` (`ik_drag.cpp`) performs the
  scan of R5 and the admissibility tests of R8 once per drag and captures
  the pole's world position there.
- MCP - the `ik_drag` tool (`mcp_server_scene_action.cpp`, schema in
  `config/editor/mcp_tools.json`).
- Serialization - `ERHE_node` `properties` and `property_node_refs`
  (`doc/gltf_extensions/ERHE_node.md`), written in
  `gltf_extensions_export.cpp` and `erhe::gltf`
  (`gltf_fastgltf.{hpp,cpp}`, `gltf_item_flags.{hpp,cpp}`) and read in
  `gltf_extensions_import.cpp`. `save_scene_usd` (`parsers/usd.cpp`) logs
  the count of nodes holding IK values, which USD has no form for.
- Acceptance verification - `scripts/ik_pole_verify.py`, criteria 1 to 9
  and 12; the reload legs also run in
  `scripts/scene_roundtrip_verify.py`.

Outstanding: interactive (windowed) verification of the Properties pole
target picker and of a live gizmo drag with a pole.
