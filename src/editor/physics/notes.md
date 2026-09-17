# physics/

## Purpose

Physics-related tools, UI, and collision shape generation for the editor.

## Key Types

- **`Physics_tool`** -- A `Tool` for interacting with physics objects. Three modes:
  - **Drag** -- grab and drag rigid bodies using a physics constraint (point-to-point)
  - **Push** -- apply force in the pointing direction
  - **Pull** -- apply force toward the pointer

  Manages damping overrides and visual feedback (debug lines); the drag constraint itself is a `Physics_drag_constraint`. Supports both desktop mouse and XR controller input.

  The drag start (`begin_drag()`, shared by the right-drag's `acquire_target()` and `begin_scripted_drag()`) picks the pull by the grabbed body: a dynamic body held by a live `Node_joint` (`Scene_root::is_jointed_rigid_body()`) gets the jointed-body spring (`make_jointed_body_drag_settings()`, the same pull as the Transform tool's drag) with its pivot at the center of mass, no damping / friction / gravity overrides, no grab-time velocity reset and no extra per-frame velocity damping; its drag target is the grab point's goal offset by the current grab point -> center of mass vector. Every other body gets the rigid (frequency 0, unbounded) grab at the grabbed point with the tool's overrides and extra damping, its drag point moved as kinematic.

  Scripted drags (`begin_scripted_drag()` / `step_scripted_drag()` / `release_target()`) run a Drag mode drag from code through the same drag start, per-frame goal (`apply_drag_goal()`) and release; the MCP `physics_drag` tool steps one goal per frame through its deferred request (grab_point, default the body's center of mass; target or translation; frames; release false holds the drag until `action: "release"`). A scripted drag refuses pointer drags and survives hover view changes. Scene close and removal of the dragged mesh release the drag.

  `tool_render()` casts a ray down from the dragged mesh with `project_ray()`, which steps over the mesh's own hits without editing the scene.

- **`Physics_drag_constraint`** -- The pull of an interactive physics drag, shared by the Physics tool's right-drag and the Transform tool's drag of a jointed dynamic body (`transform/physics_driven_drag.hpp`): a collisionless kinematic drag point body plus a point-to-point constraint from a pivot on the dragged dynamic body to it (`Physics_drag_constraint_settings`: frequency 0 = rigid, else a spring with damping ratio and max force; optional Jolt solver iteration overrides). `attach()` wakes the dragged body and keeps it awake (`begin_move`), `move_drag_point()` teleports or kinematically moves the drag point, `detach()` removes constraint then drag point body and lets the body sleep again; the dragged body keeps its velocity. The owner detaches on scene close / removal of the dragged node, before the world or body goes away.

  `make_jointed_body_drag_settings(mass)` is the one definition of the jointed-body pull: 10 Hz, critically damped, force bounded to 3x the body's weight, Jolt island iterations 40 velocity / 20 position, `Drag_point_speed::braking` with a limit margin of 0.1 rad.

  Braking drag point: the drag registers with its `Scene_root` at `attach()` (`register_physics_drag`), and `Scene_root::update_physics_simulation_fixed_step()` calls `on_fixed_step(dt)` before each world step, which advances the drag point body along the joint reach (`Joint_reach::step_toward`) toward the projected target by at most `v_max * dt`, `v_max = sqrt(2 * (max_force / mass - g) * d)`, `d` = reach radius * limit margin for a limited hinge, else 0.05 m. The projected target of a limited hinge stays the margin inside the range. So the bounded pull can always brake the body before a joint limit: a jumping drag target (fast cursor) otherwise accelerated cradle balls to 4-6 m/s into the +-80 deg hinge limit (12-25 mm hinge separation). A cradle ball (2 kg, radius 0.64 m) gets `v_max` 1.585 m/s: a 0.7 m cursor jump reaches the ball in about 0.5 s. An immediate drag (the unjointed Physics tool grab) places the drag point in `move_drag_point()`.

  Joint-space projection: `attach()` takes the scene's `Node_joint`s and builds an `erhe::physics::Joint_reach` when exactly one live joint holds the dragged body and its other side is the world or a non-dynamic body (anchor frame captured at attach). `move_drag_point()` then projects every requested position onto what the joint lets the pivot reach - circle (one free / ranged rotation axis, range applied), sphere (two or three, angular limits not applied), box (translation only, per-axis ranges), point (nothing moves the pivot) - so the pull never fights the joint; a target without a unique nearest position keeps the last projected one. Several joints on the body, a joint to a dynamic body, and limit combinations `Joint_reach` does not handle (translation and rotation both movable, a rotation fixed at a non-zero angle) leave the drag point unprojected. `get_projection_description()` names the choice; `get_requested_drag_point()` / `get_projected_drag_point()` report the last move, `get_drag_point()` where the drag point body is, `get_drag_point_speed_limit()` the braking speed (infinity when immediate). The force bound stays as the safety net.

- **`Physics_drag_monitor`** -- editor.physics_drag diagnostics of active drags: at drag start the body, constraint, tool settings, the projection choice (`drag target projected: circle (...)` / `drag target unprojected: ...`), the drag point speed limit and the dump of the joints holding the body; per rendered frame the requested and projected drag target, drag point, spring separation, body velocities and the worst joint error of the scene; at drag end the release velocity and the per-joint maxima during the drag.

- **`Physics_window`** -- ImGui window for physics simulation settings (gravity, time step, debug visualization toggles).

- **`Collision_volume_calculator`** / **`Collision_shape_generator`** -- Function types used by `Brush` to compute collision volumes and create `ICollision_shape` instances from geometry. Defined in `collision_generator.hpp`.

## Public API / Integration Points

- `Physics_tool::acquire_target()` / `release_target()` -- begin/end physics interaction
- `Physics_tool::set_mode()` -- switch between drag/push/pull
- Collision generators are passed to `Brush_data` for physics-enabled brush placement

## Dependencies

- erhe::physics (IWorld, IRigid_body, IConstraint, ICollision_shape)
- erhe::raytrace (for picking)
- editor: App_context, Tools, Scene_view, Node_physics
