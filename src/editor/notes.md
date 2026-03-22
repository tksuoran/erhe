# Editor Application

## Purpose

The editor is the main application built on the erhe C++ graphics engine. It provides a 3D scene editor with ImGui-based UI, supporting mesh creation, geometry operations (Catmull-Clark, Conway operators, CSG), physics simulation (Jolt), brush-based mesh placement, transform tools, and optional OpenXR headset support.

## Entry Point and Initialization Flow

- **Entry**: `main.cpp` calls `editor::run_editor()`.
- **`run_editor()`** (in `editor.cpp`):
  1. Initializes log sinks and all library-level logging.
  2. Initializes sleep, physics, and Geogram systems.
  3. Optionally initializes RenderDoc frame capture.
  4. Constructs the `Editor` object (a local class in `editor.cpp`).
  5. Calls `editor.run()` to enter the main loop.

- **`Editor` constructor**:
  1. Loads `Editor_config` from `erhe.json`.
  2. Creates a `tf::Executor` for task parallelism.
  3. Creates no-dependency subsystems: `Commands`, `App_message_bus`, `App_settings`, `Input_state`, `Time`.
  4. Creates the OS window and `erhe::graphics::Device`.
  5. Optionally creates OpenXR headset.
  6. Creates GPU subsystems (potentially in parallel via Taskflow): `Programs`, `Imgui_renderer`, `Debug_renderer`, `Text_renderer`, `Forward_renderer`, `Shadow_renderer`, `Mesh_memory`, `Rendergraph`, `Thumbnails`, `Icon_set`, `Post_processing`, `Id_renderer`.
  7. Creates editor subsystems: `Selection`, `Operation_stack`, `Tools`, `Fly_camera_tool`, transform tools (`Move_tool`, `Rotate_tool`, `Scale_tool`, `Transform_tool`), `Brush_tool`, `Create`, `Physics_tool`, `Paint_tool`, `Material_paint_tool`, `Hover_tool`, `Grid_tool`, `Hud`, `Hotbar`.
  8. Creates all ImGui windows.
  9. Creates a default `Scene_root` and `Scene_builder` to populate it.
  10. Fills the `App_context` struct with pointers to all subsystems.

- **Main loop** (`Editor::run()`):
  1. Polls window events and dispatches input.
  2. Calls `tick()` each frame.

- **`Editor::tick()`** per-frame:
  1. Waits for GPU frame.
  2. Prepares time, updates transform animations.
  3. Updates pointer/hover state.
  4. Runs fixed-step physics simulation loop.
  5. Processes ImGui events and draws all ImGui windows.
  6. Updates operation stack (executes queued operations).
  7. Flushes queued messages via `App_message_bus::update()`.
  8. Flushes GPU buffer transfers.
  9. Executes the render graph.
  10. Presents the frame.

## Key Subsystems and Interactions

### App_context (Service Locator)

`App_context` is a plain struct of raw pointers to all major subsystems. It is filled after construction and passed by reference to subsystems that need cross-cutting access. This is the central wiring mechanism -- subsystems do not hold direct references to each other at construction time but look them up through `App_context`.

### App_message_bus (Event System)

`App_message_bus` holds typed `erhe::message_bus::Message_bus` instances for decoupled communication. Messages include:
- `Selection_message` -- selection changed
- `Hover_scene_view_message` -- pointer entered/left a scene view
- `Hover_mesh_message` -- pointer hovers over a mesh
- `Graphics_settings_message` -- graphics preset changed
- `Node_touched_message` -- a node's transform was modified
- `Open_scene_message`, `Load_scene_file_message` -- scene lifecycle
- `Tool_select_message` -- active tool changed
- `Render_scene_view_message` -- scene view rendering requested

Messages can be synchronous (`sync_only`), queued (`queue_only`), or both. Queued messages are flushed once per frame via `App_message_bus::update()`.

### Scene (`scene/`)

`Scene_root` owns an `erhe::scene::Scene`, physics world, raytrace scene, mesh layers, and a `Content_library`. Multiple `Scene_root` instances are managed by `App_scenes`. `Scene_view` is the abstract base for anything that looks at a scene (camera + hover state). `Viewport_scene_view` is the concrete implementation that renders into the render graph.

### Operations (`operations/`)

Undo/redo system. `Operation` is the abstract base with `execute()` and `undo()`. `Operation_stack` queues operations and executes them during `update()`. Concrete operations include geometry operations (Catmull-Clark, Conway operators, CSG), item insert/remove, transform changes, and material changes. `Mesh_operation` is the base for geometry-modifying operations.

### Tools (`tools/`)

`Tool` extends `Command_host` with priority-based activation, viewport rendering, and tool properties. `Tools` manages the collection and priority system. The currently active ("priority") tool gets highest input priority. Tools include:
- `Fly_camera_tool` -- camera navigation (WASD + mouse)
- `Selection_tool` -- click-to-select
- `Brush_tool` -- place brushes on surfaces
- `Physics_tool` -- drag physics objects
- `Transform_tool` -- gizmo-based translate/rotate/scale (delegates to `Move_tool`, `Rotate_tool`, `Scale_tool`)
- `Paint_tool`, `Material_paint_tool` -- vertex/material painting
- `Hover_tool` -- displays hover information
- `Create` -- create new primitives (sphere, box, torus, cone)
- `Grid_tool` -- configurable grid display

### Rendergraph (`rendergraph/`)

Editor-specific render graph nodes:
- `Viewport_scene_view` -- renders scene content (also a `Texture_rendergraph_node`)
- `Shadow_render_node` -- renders shadow maps
- `Post_processing_node` -- bloom/tonemap post-processing
- `Basic_scene_view_node` -- sink node connecting viewport to output

### Renderers (`renderers/`)

- `Programs` -- loads and manages all shader programs and debug visualization variants
- `Mesh_memory` -- GPU buffer allocation for vertex/index data
- `Id_renderer` -- GPU-based object picking via ID buffer
- `Composer` -- composites multiple render passes for final output

### Content Library (`content_library/`)

`Content_library` is a tree structure (`Content_library_node` extends `Hierarchy`) that organizes materials, brushes, animations, skins, and textures. Each scene has its own content library. Supports drag-and-drop in the UI.

### Windows (`windows/`)

ImGui window implementations:
- `Viewport_window` -- hosts a `Viewport_scene_view` in an ImGui window
- `Properties` -- inspects selected item properties (camera, light, mesh, material, etc.)
- `Settings_window` -- application settings
- `Item_tree_window` -- tree view of scene hierarchy or content library
- `Viewport_config_window`, `Scene_view_config_window` -- viewport settings

### Config (`config/`)

`Editor_config` aggregates all configuration structs (loaded from `erhe.json`). Configuration structs are code-generated (via `erhe_codegen`) into `config/generated/`.

### Additional Subsystems

- `brushes/` -- `Brush` (parametric shape template) and `Brush_tool` (places brushes)
- `physics/` -- `Physics_tool` (drag/push/pull), `Physics_window` (settings UI), collision shape generation
- `create/` -- `Create` tool and shape generators (box, cone, torus, UV sphere)
- `parsers/` -- glTF, Wavefront OBJ, Geogram, and JSON polyhedra importers
- `graphics/` -- `Icon_set` (icon atlas), `Thumbnails` (material/brush previews), gradients
- `transform/` -- Transform gizmo system (`Transform_tool` + `Move_tool`, `Rotate_tool`, `Scale_tool`)
- `xr/` -- OpenXR headset view, hand tracking, controller visualization
- `grid/` -- Grid display and snapping
- `preview/` -- Material and brush preview renderers
- `animation/` -- Timeline window
- `developer/` -- Developer-only windows (clipboard, commands, composer, rendergraph, etc.)
- `experiments/` -- Experimental features (gradient editor, network, sheet)

## Important Patterns

### Dependency Injection via App_context

All subsystems receive `App_context&` and look up sibling subsystems through it. This avoids circular constructor dependencies but means subsystems are loosely typed -- null checks are important when accessing context members.

### Command System

Input handling uses `erhe::commands::Commands`. Tools register `Command` objects (e.g., `Brush_insert_command`) and bind them to keys, mouse buttons, or XR controller inputs. Commands have `try_ready()` (check preconditions) and `try_call()` (execute). `Command_host` priority determines which commands are active when multiple tools compete for the same input.

### Async Operations

Geometry operations can run asynchronously via `tf::Executor`. `async_for_nodes_with_mesh()` in `items.cpp` manages a global map of per-item async tasks, chaining dependent operations. `App_context::pending_async_ops` and `running_async_ops` are atomic counters displayed in the status bar.

### Parallel Initialization

Controlled by `ERHE_SERIAL_INIT` / `ERHE_PARALLEL_INIT`. When parallel init is enabled, GPU subsystems are created in parallel Taskflow tasks with explicit dependency edges. Currently serial init is the default due to GL context sharing issues.

### Physics Integration

Each `Scene_root` owns a physics world. `Node_physics` is a `Node_attachment` wrapping a Jolt rigid body. Physics callbacks (`on_body_activated`/`on_body_deactivated`) set `no_transform_update` flags to let physics drive transforms for active bodies. The owner pointer on rigid bodies uses `reinterpret_cast<Node_physics*>`. Dynamic physics can be toggled at runtime via `App_settings::physics.dynamic_enable`.

### Brush Placement

`Brush` is a parametric shape template (geometry + collision shape + density). `Brush::make_instance(Instance_create_info)` creates a scene node with mesh, material, and optional physics body at a pre-scaled geometry. The `place_brush_in_scene()` free function (in `brushes/brush.hpp`) wraps `make_instance()` with undo support and `Brush_placement` attachment tracking. Both `Brush_tool` (interactive surface-aligned placement) and the MCP server (programmatic placement by position) call this shared function.

### Scene Serialization

Scenes serialize to JSON + companion `.glb` (meshes/materials) + `.geogram` (editable geometry). The codegen system (`erhe_codegen`) generates versioned C++ structs with simdjson-based serialization. `Scene_file` (v2) contains nodes, cameras, lights, mesh references, and node physics data. Collision shape types (box, sphere, cylinder, capsule, compound) are serialized and faithfully recreated on load instead of degrading to convex hulls.

### Content Library Drag-and-Drop

Content library items can be dragged between libraries (e.g., materials from one scene to another). Cross-library drops perform a copy via `Material` copy constructor + `Item_insert_remove_operation`. Same-library drags are ignored. Materials can also be dropped onto scene nodes to assign them to mesh primitives.

### MCP Server (`mcp/`)

The editor embeds an MCP (Model Context Protocol) server on `127.0.0.1:8080` for external tool integration. It exposes:

- **Query tools**: `list_scenes`, `get_scene_nodes`, `get_node_details`, `get_scene_cameras`, `get_scene_lights`, `get_scene_materials`, `get_material_details`, `get_scene_brushes`, `get_selection`
- **Action tools**: `select_items` (by ID), `place_brush` (by brush ID + position), `toggle_physics`
- **Editor commands**: All registered `Command` objects (undo, redo, delete, etc.)

The HTTP server (cpp-httplib) runs on a background thread. All requests are queued to the main thread via `std::promise`/`std::future` for thread safety. `process_queued_requests()` is called once per frame from `Editor::tick()`. See `mcp_server_usage.md` for full API reference with curl examples.

## Dependencies

- erhe libraries: commands, graphics, imgui, renderer, rendergraph, scene, scene_renderer, physics, raytrace, geometry, primitive, item, window, log, verify, configuration, file, gltf, math, time, profile, message_bus, graph, ui, xr
- External: Taskflow, ImGui, GLM, Geogram, spdlog/fmt, simdjson, Jolt (physics), SDL/GLFW, OpenXR, cpp-httplib, nlohmann/json
