# physics/

## Purpose

Physics-related tools, UI, and collision shape generation for the editor.

## Key Types

- **`Physics_tool`** -- A `Tool` for interacting with physics objects. Three modes:
  - **Drag** -- grab and drag rigid bodies using a physics constraint (point-to-point)
  - **Push** -- apply force in the pointing direction
  - **Pull** -- apply force toward the pointer

  Manages damping overrides and visual feedback (debug lines); the drag constraint itself is a `Physics_drag_constraint`. Supports both desktop mouse and XR controller input.

- **`Physics_drag_constraint`** -- The pull of an interactive physics drag, shared by the Physics tool's right-drag and the Transform tool's drag of a jointed dynamic body (`transform/physics_driven_drag.hpp`): a collisionless kinematic drag point body plus a point-to-point constraint from a pivot on the dragged dynamic body to it (`Physics_drag_constraint_settings`: frequency 0 = rigid, else a spring with damping ratio and max force; optional Jolt solver iteration overrides). `attach()` wakes the dragged body and keeps it awake (`begin_move`), `move_drag_point()` teleports or kinematically moves the drag point, `detach()` removes constraint then drag point body and lets the body sleep again; the dragged body keeps its velocity. The owner detaches on scene close / removal of the dragged node, before the world or body goes away.

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
