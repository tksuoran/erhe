# erhe_physics

## Purpose
Thin abstraction layer over physics engines (currently Jolt Physics, with a null
backend). Provides interfaces for worlds, rigid bodies, collision shapes, constraints,
and debug drawing, allowing the rest of erhe to use physics without depending on
a specific engine.

## Key Types
- `IWorld` -- physics world: manages rigid bodies and constraints, steps simulation, debug draws
- `IRigid_body` -- rigid body with mass, velocity, damping, motion mode, transform
- `IRigid_body_create_info` -- parameters for creating a rigid body (shape, mass/density, friction, etc.)
- `ICollision_shape` -- factory for shapes: box, sphere, capsule, cylinder, convex hull, compound, uniform scaling
- `IConstraint` -- joint constraints (currently point-to-point)
- `IDebug_draw` -- debug rendering interface (wireframe, contacts, AABBs)
- `Transform` -- basis (mat3) + origin (vec3) transform representation
- `Motion_mode` -- enum: static, kinematic (non-physical/physical), dynamic

## Public API
- Factory pattern: `IWorld::create()`, `ICollision_shape::create_box_shape_shared()`, etc.
- `IWorld::add_rigid_body()`, `remove_rigid_body()`, `update_fixed_step(dt)`
- `IRigid_body::set_world_transform()`, `set_linear_velocity()`, `set_motion_mode()`
- `ICollision_shape` static factories for all primitive shapes plus convex hull and compound
- `initialize_physics_system()` -- one-time initialization

## Dependencies
- External: glm, Jolt Physics (when `ERHE_PHYSICS_LIBRARY=jolt`)
- `erhe::renderer` -- for `Jolt_debug_renderer` (debug draw)

## Notes
- Backend selected at CMake time: `jolt/` directory has Jolt implementations, `null/` has no-op stubs.
- All interfaces use virtual dispatch with static factory methods returning raw/shared/unique pointers.
- `Transform` uses `mat3 basis + vec3 origin` (not quaternion) to match Jolt's internal representation.
- Rigid body ownership is managed externally; the world does not own bodies.
- The `IMotion_state` header appears to be an empty/placeholder file.
