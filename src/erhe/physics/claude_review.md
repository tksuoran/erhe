# erhe_physics -- Code Review

## Summary
A physics abstraction layer with interface classes (`IWorld`, `IRigid_body`, `ICollision_shape`, `IConstraint`) and two backends: Jolt Physics (full implementation) and null (no-op stubs). The design cleanly separates the physics API from the backend, allowing the engine to function without physics. The Jolt integration is comprehensive, covering rigid bodies, constraints, collision shapes, debug rendering, and body activation callbacks.

## Strengths
- Clean interface/implementation separation: `IWorld`, `IRigid_body`, `ICollision_shape` define the contract; `Jolt_*` classes implement it
- Null backend provides complete no-op stubs, enabling physics-free builds
- Factory pattern (`IWorld::create()`, `ICollision_shape::create_*_shared()`) hides backend selection from callers
- Good use of `std::shared_ptr` for collision shape ownership, enabling shape sharing across rigid bodies
- Comprehensive collision shape support: box, sphere, capsule, cylinder, convex hull, compound, uniform scaling
- Jolt integration handles body activation/deactivation callbacks with configurable user callbacks
- `Transform` type provides a clean glm-based transform representation for physics

## Issues
- **[moderate]** `IRigid_body` uses `void* owner` (irigid_body.hpp:94-95) for back-pointer to the scene node. This is type-unsafe; a typed pointer or `std::any` would be safer.
- **[moderate]** `ICollision_shape::~ICollision_shape()` (icollision_shape.hpp:44) has an empty body `{}` instead of `= default`. This prevents the compiler from generating a trivial destructor and may inhibit optimizations. More importantly, the implementation in the source file may cause ODR issues.
- **[moderate]** `Jolt_world` stores raw pointers to `Jolt_rigid_body*` and `Jolt_constraint*` (jolt_world.hpp:116-117). If a body or constraint is deleted externally without calling `remove_rigid_body()`, the world holds a dangling pointer. This is a documentation issue at minimum.
- **[minor]** `c_motion_mode_strings` (irigid_body.hpp:26-32) is `static constexpr` in a header, creating a copy in each translation unit. Should be `inline constexpr`.
- **[minor]** The `c_str(Motion_mode)` function (irigid_body.hpp:34-37) does not bounds-check the enum value. An invalid `Motion_mode` value would cause undefined behavior via out-of-bounds array access.
- **[minor]** `Jolt_world::m_on_body_activated_callback` and `m_on_body_deactivated_callback` (jolt_world.hpp:113-114) store `std::function<void(Jolt_rigid_body*)>` but the `IWorld` interface uses `std::function<void(IRigid_body*)>`. This type mismatch is resolved internally but creates coupling.
- **[minor]** `TraceImpl` in jolt_world.cpp uses `va_list` (C-style varargs). This is required by the Jolt callback signature but should be documented.

## Suggestions
- Replace `void* owner` with a typed mechanism, such as `erhe::scene::Node*` or a `std::function<void()>` callback.
- Change `ICollision_shape::~ICollision_shape()` from `{}` to `= default`.
- Add `inline` to `c_motion_mode_strings` and add bounds checking to `c_str(Motion_mode)`.
- Document the ownership model: who is responsible for calling `remove_rigid_body()` before deleting a body.
- Consider using `std::unordered_set` instead of `std::vector` for `m_rigid_bodies` and `m_constraints` to make add/remove O(1).
