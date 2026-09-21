# erhe_physics

Stability: stable

## Purpose
Thin abstraction layer over physics engines (Jolt Physics and Box3D, plus a null
backend). Provides interfaces for worlds, rigid bodies, collision shapes, constraints,
and debug drawing, allowing the rest of erhe to use physics without depending on
a specific engine.

## Key Types
- `IWorld` -- physics world: manages rigid bodies and constraints, steps simulation, debug draws;
  trigger (sensor) overlap callbacks via `set_on_trigger_enter()` / `set_on_trigger_exit()`
  (`Trigger_event`, dispatched at the end of `update_fixed_step()`); per-pair collision
  enable/disable via `set_collision_enabled()` (joint enableCollision = false)
- `IRigid_body` -- rigid body with mass, velocity, damping, motion mode, transform, shared
  physics material / collision filter assignment
- `IRigid_body_create_info` -- parameters for creating a rigid body (shape, mass, physics material,
  initial velocities, gravity factor, sensor flag, shared material / filter, etc.); explicit mass 0
  means infinite mass (KHR_physics_rigid_bodies convention); without an explicit mass the body's
  mass is its shape mass scaled by the material density
- `ICollision_shape` -- factory for shapes: box, sphere, capsule, tapered capsule, cylinder,
  tapered cylinder, convex hull, triangle mesh (static/kinematic bodies only), compound,
  uniform scaling, non-uniform scaled and center-of-mass offset wrappers; introspection for
  serialization/export: `get_shape_type()`, primitive parameters (`get_half_extents()`,
  `get_radius()`, `get_axis()`, `get_length()`, ...), wrapper `get_inner_shape()` /
  `get_scale()` / `get_offset()`, compound `get_children()`
- `IConstraint` -- joint constraints: point-to-point and the generic six-DOF constraint
  (`Six_dof_constraint_settings`: per-axis limits incl. translation soft limits, position /
  velocity motors; frames in body node space, axes 0..2 translation, 3..5 rotation).
  `Point_to_point_constraint_settings` with `frequency` 0 is rigid (Jolt: zero-length distance
  constraint; Box3D: spherical joint); `frequency` > 0 is a spring pulling the pivots together
  with at most `max_force` (Jolt: six-DOF with free axes and translation position motors whose
  force limits clamp each axis; Box3D: motor joint linear spring with `maxSpringForce` clamping
  the magnitude). `solver_velocity_iterations` / `solver_position_iterations` raise Jolt's
  iteration counts for the constraint's whole island while it lives (Box3D ignores them)
- `Physics_material` -- shared material item, the carrier of how a kind of matter behaves:
  static/dynamic friction, restitution, the combine modes, linear/angular damping, wind
  receptivity and density are registered `erhe::property` properties (doc/erhe/property_system.md
  section 4.12), read through the typed accessors; a body without a material uses the
  `c_default_*` values; `IRigid_body::set_physics_material()` applies the damping to the body and
  re-derives a density-based mass; KHR_physics_rigid_bodies precedence in `combine()`
- `Collision_filter` -- shared collision-system filter item (allowlist / denylist of free-form
  system strings; Jolt backend interns at most 64 system names per world into uint64 bitsets).
  The three lists are registered `string[]` properties (`doc/erhe/property_system.md`
  section 4.21), read through `get_collision_systems()` and the two other getters; the
  editor's `Node_physics_system` observes the filter, so an edit from any writer recompiles the
  backend's snapshot
- `Physics_joint_settings` -- shared joint settings item, 1:1 with
  KHR_physics_rigid_bodies physicsJoints entries. It states one limit and one drive per degree
  of freedom as eleven registered properties per axis over the closed axis set `trans_x`,
  `trans_y`, `trans_z`, `rot_x`, `rot_y`, `rot_z` (66 in all,
  `doc/erhe/property_system.md` section 4.22), read through `get_axis_limits()` and
  `get_axis_drives()` as the `Constraint_axis_limit` / `Constraint_axis_drive` arrays
  `Six_dof_constraint_settings` is made of; the editor's `Joint_system` observes the item, so
  an edit from any writer rebuilds the live constraint
- `Physics_material`, `Collision_filter` and `Physics_joint_settings` are typed prims
  (`erhe::Typed`, `doc/erhe/item.md` "Prim classes"), each with its erhe class name as
  its fixed `typeName` token
- `Joint_reach` -- pure projection of a target position onto the positions a point of a jointed
  body can reach while one six-DOF joint to a fixed anchor frame holds (`Joint_side` names the
  moving body's side; shapes point / circle / sphere / box, or unprojected for combinations it does
  not handle; a limited hinge range is narrowed by a configured angular margin); `step_toward()`
  moves a reachable point along the reach (circle arc inside the range, sphere great circle,
  straight line otherwise); used by the editor's interactive physics drags; tests in
  `test/test_joint_reach.cpp`, and `scripts/physics_drag_joint_sweep.py` checks the drags end to end
  against a running editor
- `IDebug_draw` -- debug rendering interface (wireframe, contacts, AABBs)
- `Transform` -- basis (mat3) + origin (vec3) transform representation
- `Motion_mode` -- enum: `e_none`, static, kinematic (non-physical/physical), dynamic.
  `e_none` is the default of the editor's `Node_physics.motion_mode` key property and
  means the prim simulates nothing, so a node carries a rigid body exactly while its
  effective mode is another one (`doc/erhe/property_system.md` section 4.26)

## Public API
- Factory pattern: `IWorld::create()`, `ICollision_shape::create_box_shape_shared()`, etc.
- `IWorld::add_rigid_body()`, `remove_rigid_body()`, `update_fixed_step(dt)`
- `IRigid_body::set_world_transform()`, `teleport()`, `set_linear_velocity()`, `set_motion_mode()`
- `ICollision_shape` static factories for all primitive shapes plus convex hull and compound
- `initialize_physics_system()` -- one-time initialization

## Dependencies
- External: glm, Jolt Physics (when `ERHE_PHYSICS_LIBRARY=jolt`), Box3D (when
  `ERHE_PHYSICS_LIBRARY=box3d`)
- `erhe::renderer` -- for `Jolt_debug_renderer` (debug draw)

## Notes
- Backend selected at CMake time: `jolt/` directory has Jolt implementations, `box3d/` has
  Box3D implementations, `null/` has no-op stubs.
- All interfaces use virtual dispatch with static factory methods returning raw/shared/unique pointers.
- `Transform` uses `mat3 basis + vec3 origin` (not quaternion) to match Jolt's internal representation.
- `set_world_transform()` vs `teleport()`: `set_world_transform()` is motion-mode aware -- for
  `e_kinematic_physical` bodies it uses Jolt `MoveKinematic()`, which turns the position delta into a
  velocity (intended for interactive dragging that should push other bodies). `teleport()` always sets
  the pose directly (`SetPositionAndRotation`, `DontActivate`) with no induced velocity, regardless of
  motion mode. Use `teleport()` to snap a body to a newly authored pose (joint create/flip, editor
  move) so the simulation does not react with a corrective impulse or kinematic velocity injection.
- Rigid body ownership is managed externally; the world does not own bodies.
- Activation on entry to the world: a body at rest is added asleep, so opening a scene does not set
  its contents in motion; a body that already holds a non-zero linear or angular velocity is moving
  and is added active. Both cases are decided in `IWorld::add_rigid_body()` from the body's own
  velocity, so a velocity taken from a create info, from a `set_linear_velocity()` call made before
  the body joined the world, or from a body that left and re-entered the world is treated the same.
  Jolt requires this: a sleeping non-static body holding a velocity trips `Body::ValidateMotion()`
  in an asserts-enabled build, and a body added asleep is never integrated, so its velocity is
  silently dropped. Assigning a non-zero velocity to a sleeping body that is already in the world
  wakes it (Jolt's `BodyInterface` does this itself). The Box3D backend enables a body on
  `add_rigid_body()` (which wakes it) and puts a body at rest back to sleep; a disabled Box3D body
  has no velocity state, so `Box3d_rigid_body` holds the velocity of a body outside the world and
  applies it on entry. The null backend never simulates and has no activation state: its
  `is_active()` is always false.
- `erhe_physics/imotion_state.hpp` is an empty placeholder file; nothing includes it.
- Unit tests live in `test/` (`-DERHE_BUILD_TESTS=ON` -> `erhe_physics_tests`). The suite
  builds for the simulating backends (`jolt`, `box3d`). Every build runs the
  backend-neutral tests, which step a real `IWorld` through the interface (body
  activation, trial-placement overlap queries). A `box3d` build adds the Box3D-specific
  tests: pure logic (hull builder, shape descriptors, collision filter table, six-DOF
  classifier) and the activation / sensor events `Box3d_world` synthesizes.
- KHR_physics_rigid_bodies support, its design and its known limitations are described in
  `doc/erhe/khr_physics_rigid_bodies_support.md`. Jolt-imposed limits: triangle mesh shapes are
  static/kinematic only; sensors must be non-static to detect static bodies (callers create
  static triggers as kinematic non-physical); six-DOF angular soft limits fall back to hard
  limits; acceleration-mode drives run as force mode. Box3D's own limits are in the table
  below.

## Box3D backend

Selected with `ERHE_PHYSICS_LIBRARY=box3d`; sources in `box3d/`. The authoritative
deferred/unsupported list is the header comment block in `box3d_world.hpp` -- reproduced
here, and to be kept in sync with it.

`doc/erhe/box3d_physics.md` describes this backend: how it is built and verified, and the
Box3D behaviors the mapping has to respect (hull edge budgets, the baked shape transform,
the joint def cookie, filter change detection, the event model). Read it before doing
anything non-trivial here.

| Feature                              | Status                    | Why |
| ------------------------------------ | ------------------------- | --- |
| `IWorld::debug_draw`                 | no-op                     | the signature names `erhe::renderer::Jolt_debug_renderer`; neutralizing it is deferred (Box3D does have `b3World_Draw`, so this is a wiring gap, not a capability gap) |
| `save_state` / `restore_state`       | not implemented, warns    | Box3D exposes no world snapshot API |
| static friction                      | ignored, dynamic used     | `b3SurfaceMaterial` carries a single friction |
| independent 6-DOF joints             | approximated              | Box3D has no generic six-DOF joint |
| universal joints (2 rotational DOF)  | approximated by spherical | no equivalent; the third axis stays free |
| multi-axis translation limits        | unsupported, warns        | no equivalent |
| rotation limits beyond +/-0.99 pi    | clamped                   | Box3D range limit |
| nested offset-center-of-mass         | ignored, errors           | Box3D carries the center of mass on the body |
| rotated mesh inside a compound       | rotation ignored, warns   | `b3CreateMeshShape` takes a scale but no transform |
| mesh on a dynamic body               | forced static, errors     | Box3D has no `b3ComputeMeshMass` |
| penetration tolerance versus meshes  | ignored (boolean overlap) | no public hull-versus-mesh manifold |
| non-uniform scale on sphere/capsule  | inscribed hull            | an ellipsoid is not representable |
| non-uniform scale over a rotation    | approximated, warns       | the exact result is a shear; same as Jolt |
| collision systems per world          | 64                        | interned into a uint64 bitset |
| body woken without moving            | reported on first move    | synthesized from `b3BodyMoveEvent` |
| resting body added with joints       | enters awake              | sleeping a body sleeps its whole island |
| six-DOF joint softness               | stiffest the step allows  | `constraintHertz` asks for more than Box3D's clamp (a quarter of the substep rate); Box3D's soft joints are stiff relative to the effective mass at the joint, which is tiny across the swing plane of a body hanging far from its joint, so the 60 Hz default gave way ~2 mm per newton there |
| height fields                        | not exposed               | erhe has no height field shape type |

Backend-specific design notes:
- **Collision shapes are descriptors.** Box3D has no standalone shape object -- shapes are
  created onto a body -- while an erhe `ICollision_shape` exists before any body and is shared
  between bodies. So `Box3d_collision_shape` owns what Box3D resources it can (`b3HullData`,
  `b3MeshData`) and materializes shapes onto a body on demand via `attach_to_body()`. A
  compound attaches each child to the same body, which is what Box3D recommends for runtime
  compounds.
- **Events are synthesized per step.** Box3D buffers events in the world rather than calling
  listeners, so `update_fixed_step()` reads them back after the step. Activation has no
  listener at all and is diffed out of the `b3BodyMoveEvent` stream; sensor touches are
  reported per shape pair and are counted per body pair so a compound visitor produces one
  enter and one exit, as with Jolt.
- **Materials resolve per contact pair.** A combine mode belongs to the pair, so friction and
  restitution are mixed in the world's friction / restitution callbacks from immutable
  per-material snapshots (`Box3d_material_registry`, looked up by `userMaterialId`); a body
  without a material is stamped with the default-material snapshot. The material's density
  is the shape density, so Box3D's mass from shapes is the density-derived mass;
  `set_physics_material()` updates density and damping and, while the body has no explicit
  mass, re-derives the mass.
- **Collision filtering runs through the custom filter callback**, not Box3D category/mask
  bits, since erhe's collision-system allow/deny lists cannot be expressed as bits. Per-pair
  collision exclusion (joint `enableCollision = false`) uses Box3D filter joints, which exist
  for exactly that.
- **Trial-placement queries use the pairwise manifold functions.** Box3D's overlap queries are
  boolean and `b3ShapeDistance` reports 0 for any overlap, so neither can express a penetration
  tolerance; `b3ManifoldPoint::separation` can. See `box3d_overlap_query.hpp`.
- **Shape geometry is read back from the live shapes** (`b3Shape_GetHull` and friends) rather
  than kept alongside: Box3D bakes a shape's creation transform and scale into the shape, so
  the getters return geometry already in the body frame.
- KHR_physics_rigid_bodies six-DOF constraints are mapped onto Box3D's concrete joint types by
  `box3d_six_dof_classifier.{hpp,cpp}` (weld / revolute / prismatic / spherical / filter),
  which is pure logic and unit tested.
