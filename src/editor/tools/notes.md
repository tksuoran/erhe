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

- **`Tool_window`** -- Helper class that creates an `Imgui_window` for a tool's properties.

## Public API / Integration Points

- `Tools::register_tool()` -- register a tool
- `Tools::set_priority_tool()` -- set the active tool
- `Tools::render_viewport_tools()` -- render all tool overlays
- `Tool::tool_render()` -- per-tool viewport rendering
- `Tool::get_priority()` -- combined base + boost priority

## Dependencies

- erhe::commands, erhe::imgui, erhe::scene
- editor: App_context, App_message_bus, Icon_set, Mesh_memory, Scene_view
