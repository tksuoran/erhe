# physics/

## Purpose

Physics-related tools, UI, and collision shape generation for the editor.

## Key Types

- **`Physics_tool`** -- A `Tool` for interacting with physics objects. Three modes:
  - **Drag** -- grab and drag rigid bodies using a physics constraint (point-to-point)
  - **Push** -- apply force in the pointing direction
  - **Pull** -- apply force toward the pointer

  Manages constraint lifecycle, damping overrides, and visual feedback (debug lines). Supports both desktop mouse and XR controller input.

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
