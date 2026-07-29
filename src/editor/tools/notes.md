# tools/

## Purpose

Defines the Tool abstraction and the Tools container, plus several concrete tools for interacting with the 3D scene.

## Key Types

- **`Tool`** -- Base class extending `erhe::commands::Command_host`. Adds:
  - Priority-based activation (base priority + boost)
  - `tool_render()` for drawing into the viewport
  - `tool_properties()` for the tool properties panel
  - `handle_priority_update()` callback when priority changes
  - Tool flags: `enabled`, `background`, `toolbox`, `secondary`, `allow_secondary`
  - Hover scene view tracking

- **`Tool_flags`** -- Bit flags controlling tool behavior:
  - `background` -- always active (e.g., hover tool)
  - `toolbox` -- appears in hotbar, subject to priority enable/disable
  - `secondary` -- can remain active alongside the priority tool

- **`Tools`** -- Container managing all registered tools. Maintains:
  - A list of all tools and background tools
  - The "priority tool" (the one with highest priority)
  - A tool scene root (for tool visualization meshes)
  - Pipeline render states for tool mesh rendering (stencil-based hidden/visible)
  - `update_transforms()` and `render_viewport_tools()` called each frame

- **`Fly_camera_tool`** -- Camera navigation (WASD + mouse turn/tumble/track/zoom). Has many command objects for each input axis. Uses `Frame_controller` for 6DOF control. Supports recording input samples for debugging.

- **`Selection_tool`** -- Click-to-select in viewport. Delegates to `Selection` (a `Command_host` that manages the selection set).

- **`Hover_tool`** -- Background tool that tracks what the pointer hovers over and displays information. Subscribes to hover messages.

- **`Hotbar`** -- VR/desktop toolbar showing tool icons for quick switching.

- **`Hud`** -- Head-up display for VR mode (rendertarget-based ImGui host).

- **`Paint_tool`** -- Vertex color painting.

- **`Material_paint_tool`** -- Assigns materials to faces.

- **`Clipboard`** -- Cut/copy/paste for scene items.

- **`Debug_visualizations`** -- Renders debug overlays (light visualization, skin joints, physics shapes).

- **`Bone_visualization`** -- Makes skeleton joints visible and pickable. Owns ONE unit-bone `Primitive` (head at the origin, tail at +Y, ring at y = 0.1; 6 vertices / 8 triangles) and instances it: for each joint of each skin, a proxy `Node` parented *under* the joint carrying a `Mesh` in the `bone` mesh layer. The entire bone shape lives in the instance transform (scale `half_width, length, half_width` after `orient_y_to`), which is what lets the raytrace side keep a single BVH and pose it per instance -- `Mesh::handle_node_transform_update` pushes world matrices into `IInstance`, so animation needs no BVH rebuild.
  - `bone_tail_in_joint_space()` is the shared head/tail rule, expressed in JOINT-LOCAL space so a rotation-only animation never dirties it. First child joint wins (its local translation *is* the tail offset); a leaf points along local +Y by its own offset length.
  - Flags: `Item_flags::bone` (bit 28) marks joint nodes, set by `erhe::scene::mark_skin_joints` from `Scene_root::register_skin`. `Item_flags::bone_proxy` (bit 29) marks the proxy meshes and keeps them out of the item tree, save, export and prefabs. Proxies deliberately carry no `content` bit. Note that Item_TYPE cannot serve here: `Item<>::get_type()` returns `Self::get_static_type()`, i.e. it is per-class, and a joint is an ordinary `Node`.
  - Picking is gated per frame: `Item_flags::id` and the raytrace mask are set only in bone mode. The rt mask is written DIRECTLY rather than derived from the flags, because `bone_proxy` must stay set as the proxy's identity while pickability toggles; mask 0 = unhittable, so in object mode a ray passes straight through to the mesh. In practice picking is served by raytrace: the ID path drops bone proxies anyway under the default `skinning_filter == skinned_only` (`id_renderer.cpp`).
  - Three unlit materials (normal / selected / hover) are swapped onto the proxy's primitive as the joint's `is_selected()` / `is_hovered()` change, and must be registered with each scene's content library (`register_materials()`) or they get no material buffer slot and the shader falls back to a default. Colors come from `Debug_visualizations_style` and are pushed in at the edit via `apply_style_colors()`. Hover wins over selection.
  - Two display styles, user-selectable: lines (default, via the debug renderer) or N.V shaded solid bones (see `renderers/notes.md` for the composition pass).
  - Selection resolves proxy -> joint via `get_joint_for_proxy()` and selects the JOINT through the ordinary `Selection`, so Properties / undo / gizmo need no special cases.
  - Bone selection is a value of `Mesh_component_mode`, not a separate mode holder, so exactly one selection granularity is active and a viewport click has one unambiguous owner. Guards must test `is_mesh_component_mode()` rather than comparing against `object` (see `mesh_component_selection.hpp`).
  - `Scene_view::get_pickable_slot_mask()` centralizes the rule "in bone mode content is not a hover candidate, the bone slot is" -- necessary because `get_nearest_hover`'s mask is a filter that resolves by DISTANCE, which would always pick the skin surface over a bone inside it. Routed through it: `Hover_tool::get_hover_node()` / `tool_render()`, and `Fly_camera_tool`'s tumble/track pivot. Deliberately NOT routed: the asset-drop anchor and the paint / brush / material tools, which target content by definition. Consequence: the grid hover candidate is dropped in bone mode, so "Show Snapped Grid Position" does nothing there.

- **`Tool_window`** -- Helper class that creates an `Imgui_window` for a tool's properties.

## Public API / Integration Points

- `Tools::register_tool()` -- register a tool
- `Tools::set_priority_tool()` -- set the active tool
- `Tools::render_viewport_tools()` -- render all tool overlays
- `Tool::tool_render()` -- per-tool viewport rendering
- `Tool::get_priority()` -- combined base + boost priority
- `Bone_visualization::get_joint_for_proxy()` -- proxy mesh -> joint node, used by selection and hover
- `bone_tail_in_joint_space()` -- the head/tail rule, shared by the line and solid bone styles

## Dependencies

- erhe::commands, erhe::imgui, erhe::scene
- editor: App_context, App_message_bus, Icon_set, Mesh_memory, Scene_view
