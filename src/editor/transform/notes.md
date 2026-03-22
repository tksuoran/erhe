# transform/

## Purpose

Transform gizmo system for interactive translate, rotate, and scale operations.

## Key Types

- **`Transform_tool`** -- Coordinates the three sub-tools (move, rotate, scale). Manages gizmo visualization meshes, handle hit detection, and drag state. Subscribes to hover and selection messages. Binds keyboard shortcuts for switching between transform modes and toggling coordinate space (local/world).

- **`Move_tool`** -- Subtool for translation. Renders axis arrows and plane handles. Computes translation from pointer ray intersection with the appropriate constraint plane/line.

- **`Rotate_tool`** -- Subtool for rotation. Renders rotation rings. Computes rotation angle from pointer position relative to the gizmo center.

- **`Scale_tool`** -- Subtool for scaling. Renders scale handles. Computes scale factor from drag distance.

- **`Subtool`** -- Base class for transform sub-tools with shared handle visualization logic.

- **`Handle_visualizations`** -- Creates and manages the gizmo mesh geometry (arrows, rings, boxes) in a dedicated tool scene root.

- **`Handle_enums`** -- Enumerations for handle types (axis, plane) and coordinate spaces.

- **`Transform_tool_settings`** -- Settings for gizmo behavior (snap, local/world space).

- **`Rotation_inspector`** -- Debug window showing rotation decomposition.

## Public API / Integration Points

- `Transform_tool` is registered as a tool and activated from the hotbar
- Creates `Node_transform_operation` entries on the operation stack for undo/redo
- Uses `Time::begin_transform_animation()` for animated transform transitions

## Dependencies

- erhe::scene, erhe::commands, erhe::imgui, erhe::renderer (debug lines)
- editor: App_context, Tools, Operation_stack, Selection, Mesh_memory
