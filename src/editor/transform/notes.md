# transform/

## Purpose

Transform gizmo system for interactive translate, rotate, and scale operations.

## Key Types

- **`Transform_tool`** -- Coordinates the three sub-tools (move, rotate, scale). Manages gizmo visualization meshes, handle hit detection, and drag state. Subscribes to hover and selection messages. Binds keyboard shortcuts for switching between transform modes and toggling coordinate space (local/world).

- **`Move_tool`** -- Subtool for translation. Renders axis arrows and plane handles. Computes translation from pointer ray intersection with the appropriate constraint plane/line.

- **`Rotate_tool`** -- Subtool for rotation. Renders rotation rings. Computes rotation angle from pointer position relative to the gizmo center.

- **`Scale_tool`** -- Subtool for scaling. Renders scale handles. Computes scale factor from drag distance.

- **`Subtool`** -- Base class for transform sub-tools with shared handle visualization logic.

- **`Handle_visualizations`** -- Draws the gizmo handles (arrows, plane quads, rings, scale cones/cube) with the debug primitive renderer (x-ray lines and filled triangles; no scene meshes) and hit tests them analytically (`pick()`). Rendering and picking share the same per-handle visibility rules, so a handle is pickable exactly when it is drawn. The translate presentation is view-dependent: without negative handles, each axis shows the one camera-facing arrow (placed outside the rotate-ring sphere so arrows and rings never contest the same radius) and each plane quad sits at the gizmo center extending into the camera-facing quadrant (min corner on the center, so the three quads share edges). Draw order is inside-out (quads, rings, arrows) because the x-ray debug lines layer by submission order. In visible-arcs mode only SHOWN rings occlude arcs, so a single shown ring (e.g. mid-drag) renders as a full circle, with tangent/bitangent guide lines fading in from the center toward the arc.

- **`Handle_enums`** -- Enumerations for handle types (axis, plane) and coordinate spaces.

- **`Transform_tool_settings`** -- Settings for gizmo behavior (snap, local/world space).

- **`Rotation_inspector`** -- Debug window showing rotation decomposition.

## Public API / Integration Points

- `Transform_tool` is registered as a tool and activated from the hotbar
- `Transform_tool::update_target_nodes()` resolves the selection to the nodes the gizmo actually drives, via `resolve_transform_target()`. This is identity for everything except a skinned mesh whose joints live outside its own subtree: glTF 2.0 requires skinning to ignore the mesh node's transform (and erhe's `Joint_buffer` / `standard.vert` do), so dragging the host node would move the gizmo and leave the mesh in place. Such a selection redirects to `erhe::scene::get_skin_transform_root()`, and the Transform window shows a note naming the driven node. Targets are de-duplicated, so several meshes sharing one skin produce a single entry (otherwise a drag would apply its delta once per mesh). Selection itself is untouched - the host node stays selected, so delete / duplicate / Properties keep acting on what the user picked.
- Creates `Node_transform_operation` entries on the operation stack for undo/redo
- Uses `Time::begin_transform_animation()` for animated transform transitions

## Dependencies

- erhe::scene, erhe::commands, erhe::imgui, erhe::renderer (debug lines)
- editor: App_context, Tools, Operation_stack, Selection, Mesh_memory
