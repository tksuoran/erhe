# erhe_physics

## Purpose
Thin abstraction layer over physics engines (currently Jolt Physics, with a null
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
- `IRigid_body_create_info` -- parameters for creating a rigid body (shape, mass/density, friction,
  initial velocities, gravity factor, sensor flag, shared material / filter, etc.); explicit mass 0
  means infinite mass (KHR_physics_rigid_bodies convention)
- `ICollision_shape` -- factory for shapes: box, sphere, capsule, tapered capsule, cylinder,
  tapered cylinder, convex hull, triangle mesh (static/kinematic bodies only), compound,
  uniform scaling, non-uniform scaled and center-of-mass offset wrappers; introspection for
  serialization/export: `get_shape_type()`, primitive parameters (`get_half_extents()`,
  `get_radius()`, `get_axis()`, `get_length()`, ...), wrapper `get_inner_shape()` /
  `get_scale()` / `get_offset()`, compound `get_children()`
- `IConstraint` -- joint constraints: point-to-point and the generic six-DOF constraint
  (`Six_dof_constraint_settings`: per-axis limits incl. translation soft limits, position /
  velocity motors; frames in body node space, axes 0..2 translation, 3..5 rotation)
- `Physics_material` -- shared material item (static/dynamic friction, restitution, combine
  modes with KHR_physics_rigid_bodies precedence in `combine()`)
- `Collision_filter` -- shared collision-system filter item (allowlist / denylist of free-form
  system strings; Jolt backend interns at most 64 system names per world into uint64 bitsets)
- `Physics_joint_settings` -- shared joint settings item (`Joint_limit` / `Joint_drive` arrays,
  1:1 with KHR_physics_rigid_bodies physicsJoints entries)
- `IDebug_draw` -- debug rendering interface (wireframe, contacts, AABBs)
- `Transform` -- basis (mat3) + origin (vec3) transform representation
- `Motion_mode` -- enum: static, kinematic (non-physical/physical), dynamic

## Public API
- Factory pattern: `IWorld::create()`, `ICollision_shape::create_box_shape_shared()`, etc.
- `IWorld::add_rigid_body()`, `remove_rigid_body()`, `update_fixed_step(dt)`
- `IRigid_body::set_world_transform()`, `teleport()`, `set_linear_velocity()`, `set_motion_mode()`
- `ICollision_shape` static factories for all primitive shapes plus convex hull and compound
- `initialize_physics_system()` -- one-time initialization

## Dependencies
- External: glm, Jolt Physics (when `ERHE_PHYSICS_LIBRARY=jolt`)
- `erhe::renderer` -- for `Jolt_debug_renderer` (debug draw)

## Notes
- Backend selected at CMake time: `jolt/` directory has Jolt implementations, `null/` has no-op stubs.
- All interfaces use virtual dispatch with static factory methods returning raw/shared/unique pointers.
- `Transform` uses `mat3 basis + vec3 origin` (not quaternion) to match Jolt's internal representation.
- `set_world_transform()` vs `teleport()`: `set_world_transform()` is motion-mode aware -- for
  `e_kinematic_physical` bodies it uses Jolt `MoveKinematic()`, which turns the position delta into a
  velocity (intended for interactive dragging that should push other bodies). `teleport()` always sets
  the pose directly (`SetPositionAndRotation`, `DontActivate`) with no induced velocity, regardless of
  motion mode. Use `teleport()` to snap a body to a newly authored pose (joint create/flip, editor
  move) so the simulation does not react with a corrective impulse or kinematic velocity injection.
- Rigid body ownership is managed externally; the world does not own bodies.
- The `IMotion_state` header appears to be an empty/placeholder file.
- KHR_physics_rigid_bodies support status, design and known limitations are tracked in
  `doc/khr_physics_rigid_bodies_support.md`. Jolt-imposed limits: triangle mesh shapes are
  static/kinematic only; sensors must be non-static to detect static bodies (callers create
  static triggers as kinematic non-physical); six-DOF angular soft limits fall back to hard
  limits; acceleration-mode drives run as force mode.
