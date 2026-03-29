# erhe Library Dependencies

> This document was mostly written by Claude and may contain inaccuracies.

This document describes how erhe libraries depend on each other and how they collaborate across library boundaries.

## Dependency Table

Each row lists a library's erhe dependencies. PUBLIC means the dependency is part of the library's public API (headers); PRIVATE means it is only used internally.

| Library | PUBLIC erhe deps | PRIVATE erhe deps |
| :--- | :--- | :--- |
| `buffer` | utility | profile, verify |
| `commands` | xr, log, window | profile, verify |
| `configuration` | profile | file |
| `dataformat` | log, verify | -- |
| `defer` | -- | -- |
| `file` | -- | defer, log, verify |
| `geometry` | -- | log, math, profile, verify |
| `geometry_renderer` | geometry, math, renderer, profile, verify | -- |
| `gl` | dataformat | log, verify |
| `gltf` | -- | file, profile, geometry, graphics, log, primitive, scene |
| `graph` | -- | defer, log, verify, item |
| `graphics` | configuration, dataformat, item, window | defer, file, log, profile, utility, verify |
| `graphics_buffer_sink` | buffer, graphics, primitive | profile, verify |
| `hash` | -- | -- |
| `imgui` | commands, configuration, defer, graphics, log, math, rendergraph, window | file, profile, renderer, time |
| `item` | profile, utility | log, message_bus, verify |
| `log` | hash, configuration, verify | -- |
| `math` | -- | dataformat, log, profile, verify |
| `message_bus` | profile | -- |
| `net` | log, verify | -- |
| `physics` | geometry, log, primitive, profile, renderer | -- |
| `primitive` | buffer, dataformat, geometry, item, math, raytrace | log, profile, verify |
| `profile` | -- | -- |
| `raytrace` | buffer, dataformat, file, hash, geometry, profile, verify | log, time |
| `renderer` | graphics, math, primitive, scene, ui | utility, defer, log, profile, verify |
| `rendergraph` | graph, graphics, log, math, ui | profile, verify |
| `scene` | item, math, primitive, profile | utility, log |
| `scene_renderer` | dataformat, graphics, math, primitive, renderer, scene | file, log, message_bus, profile |
| `time` | -- | log, profile |
| `ui` | graphics, primitive, window | log, profile |
| `utility` | -- | -- |
| `verify` | -- | -- |
| `window` | time | log, profile, verify |
| `xr` | dataformat, window | log, profile, verify |

## Library Layers

Libraries roughly form layers based on their dependency depth. Libraries only depend on their own layer or layers above.

### Layer 0 -- Foundation

No erhe dependencies. Used by nearly everything else.

- **`verify`** -- `ERHE_VERIFY()` and `ERHE_FATAL()` macros.
- **`profile`** -- Tracy/NVTX profiler macros and `erhe::vector` (profiled allocator).
- **`utility`** -- `Debug_label`, `String_pool`, alignment helpers, `pimpl_ptr`.
- **`defer`** -- Deferred function execution (RAII).
- **`hash`** -- FNV-1a and constexpr XXH32.

### Layer 1 -- Infrastructure

Depend only on Layer 0.

- **`log`** -- spdlog wrappers. Also depends on `configuration` and `hash`.
- **`configuration`** -- TOML/JSON config file loading. Depends on `profile`.
- **`file`** -- File I/O utilities. Depends on `defer`.
- **`time`** -- Timers and high-resolution sleep.
- **`message_bus`** -- Typed publish/subscribe with sync and queued dispatch.
- **`buffer`** -- `Free_list_allocator` and `Cpu_buffer` for memory management.
- **`math`** -- AABB, sphere, viewport, projection math.
- **`dataformat`** -- Pixel formats, vertex formats, data types.
- **`item`** -- Base `Item` (name, id, flags) and `Hierarchy` (parent/child tree).

### Layer 2 -- Domain

Build on infrastructure to provide domain-specific functionality.

- **`geometry`** -- Polygon mesh manipulation via Geogram. No public erhe deps -- uses math, log, profile privately.
- **`gl`** -- Generated OpenGL wrappers. Depends on `dataformat`.
- **`raytrace`** -- CPU ray intersection (BVH/embree). Depends on `buffer`, `geometry`, `dataformat`.
- **`window`** -- SDL/GLFW windowing and input. Depends on `time`.
- **`xr`** -- OpenXR VR/AR integration. Depends on `window`, `dataformat`.
- **`net`** -- Basic networking. Depends on `log`.
- **`graph`** -- Generic DAG with topological sort. Uses `item` privately.

### Layer 3 -- Graphics and Scene

Core rendering and scene representation.

- **`graphics`** -- Vulkan-style GPU abstraction. Depends on `dataformat`, `item`, `window`, `configuration`.
- **`primitive`** -- Converts geometry to GPU buffers; PBR materials. Depends on `buffer`, `geometry`, `item`, `math`, `raytrace`, `dataformat`.
- **`scene`** -- 3D scene graph (nodes, meshes, cameras, lights). Depends on `item`, `math`, `primitive`.
- **`commands`** -- Input command system with bindings. Depends on `window`, `xr`.
- **`ui`** -- Font rendering, color picker. Depends on `graphics`, `primitive`, `window`.

### Layer 4 -- Rendering and Integration

High-level rendering systems that combine multiple Layer 3 libraries.

- **`renderer`** -- Debug lines, text overlay, draw indirect. Depends on `graphics`, `scene`, `primitive`, `math`, `ui`.
- **`rendergraph`** -- Render pass DAG. Depends on `graph`, `graphics`, `math`, `ui`.
- **`scene_renderer`** -- Forward renderer, shadow maps, GPU buffer management. Depends on `graphics`, `scene`, `primitive`, `renderer`, `math`, `dataformat`.
- **`imgui`** -- Custom ImGui backend. Depends on `graphics`, `rendergraph`, `commands`, `window`, `math`, `configuration`.
- **`graphics_buffer_sink`** -- Bridges `buffer` allocation with `graphics` GPU buffers. Depends on `buffer`, `graphics`, `primitive`.
- **`geometry_renderer`** -- Debug geometry visualization. Depends on `geometry`, `renderer`, `math`.
- **`gltf`** -- glTF import/export. Privately depends on `geometry`, `graphics`, `primitive`, `scene`.
- **`physics`** -- Jolt physics abstraction. Depends on `geometry`, `primitive`, `renderer`.

## Cross-Library Patterns

### Item hierarchy

`erhe::item` defines the base `Item` class (name, unique ID, flags, type info) and `Hierarchy` (parent/child tree). Multiple libraries extend this:

- `erhe::scene::Node` extends `Hierarchy` with 3D transforms
- `erhe::graphics::Texture` extends `Item`
- `erhe::primitive::Material` extends `Item`
- `erhe::scene::Mesh`, `Camera`, `Light` extend `Node_attachment` which extends `Item`
- `erhe::graph::Node` extends `Item`

Item flags (`erhe::Item_flags`) are used across scene, renderer, and editor for filtering (visible, selected, opaque, translucent, etc.). The `Item_filter` struct tests flag combinations to select which items to render.

### Geometry to GPU pipeline

Mesh data flows through several libraries on its way to the GPU:

1. **`geometry`** -- `Geometry` wraps `GEO::Mesh` with typed attributes (positions, normals, tex coords, etc.). Shape generators and operations (subdivision, Conway, CSG) produce geometry.

2. **`primitive`** -- `Primitive_builder` converts a `Geometry` into a `Buffer_mesh` containing vertex and index data organized by primitive mode (fill, edge lines, corner points, centroids). A `Buffer_sink` abstraction controls where the data goes.

3. **`graphics_buffer_sink`** -- `Graphics_buffer_sink` implements `Buffer_sink`, allocating space from GPU `Buffer` objects via `Free_list_allocator` (from `erhe::buffer`). The resulting `Buffer_allocation` RAII handles manage the GPU memory lifetime.

4. **`graphics`** -- The GPU `Buffer` objects hold the actual vertex/index data. `Render_command_encoder` binds them for drawing.

### Scene to rendering pipeline

Scene data is uploaded to the GPU and rendered through a chain of libraries:

1. **`scene`** -- `Scene` holds the node tree, mesh layers, light layers. `Node::update_transforms()` propagates world transforms. Meshes reference `Primitive` and `Material` objects.

2. **`scene_renderer`** -- Ring buffer clients (`Camera_buffer`, `Light_buffer`, `Material_buffer`, `Primitive_buffer`, `Joint_buffer`) upload scene data to the GPU each frame. `Forward_renderer` sets pipeline state and issues multi-draw-indirect calls. `Shadow_renderer` generates depth maps. `Program_interface` defines the shared C++/GLSL shader resource layout.

3. **`renderer`** -- `Draw_indirect_buffer` builds GPU draw commands from mesh spans filtered by `Item_filter`. `Debug_renderer` and `Text_renderer` provide overlay rendering.

4. **`graphics`** -- `Ring_buffer` provides fence-synchronized circular GPU memory. `Render_pipeline_state` encapsulates all fixed-function state. `Render_command_encoder` records draw calls. `Shader_resource` generates GLSL declarations that match the C++ buffer layouts.

### Input event pipeline

Input flows from the OS through several layers:

1. **`window`** -- `Context_window` collects raw input events (key, mouse, controller) into a double-buffered queue. SDL or GLFW backend translates platform events.

2. **`commands`** -- `Commands` implements `Input_event_handler` and routes events to `Command` objects via `Command_binding` (key, mouse button, drag, motion, wheel, controller, XR). A state machine (disabled/inactive/ready/active) and priority system ensures only one command handles each event.

3. **`xr`** -- `Xr_action` types represent OpenXR controller inputs. The `Headset` polls OpenXR actions and feeds them into the command system as `Xr_boolean_event`, `Xr_float_event`, etc.

### Render graph

The render graph organizes rendering into a dependency-ordered DAG:

1. **`graph`** -- Generic `Graph<N>` and `Node` with topological sort. Not graphics-specific.

2. **`rendergraph`** -- `Rendergraph` extends the generic graph for rendering. `Rendergraph_node` adds typed input/output connections that carry textures between nodes. `Texture_rendergraph_node` manages render targets (MSAA, resolve).

3. **`graphics`** -- `Render_pass` defines framebuffer attachments. `Texture` objects flow between rendergraph nodes as producer outputs and consumer inputs.

4. **`imgui`** -- `Imgui_host` is a `Rendergraph_node` that consumes a viewport texture and renders it with ImGui. `Window_imgui_host` presents to the OS window swapchain.

### Shader resource system

`erhe::graphics::Shader_resource` is a central cross-library mechanism. It programmatically builds GLSL interface block declarations from C++, ensuring the shader and C++ buffer layouts are always in sync without runtime reflection:

- `scene_renderer` defines uniform/storage blocks for camera, light, material, primitive, and joint data using `Shader_resource`.
- `renderer` defines blocks for debug line data and text rendering.
- `imgui` defines blocks for ImGui draw parameters.
- `Shader_stages_create_info` references these blocks; the shader compiler injects the generated GLSL into shader source preambles.

### Ring buffer pattern

Multiple libraries use `erhe::graphics::Ring_buffer` for streaming per-frame data to the GPU:

- `scene_renderer`: Camera, light, material, primitive, joint buffers
- `renderer`: Debug line vertex data, text vertex data, draw indirect commands
- `imgui`: ImGui vertex, index, and draw parameter buffers

Each consumer allocates a range from the ring buffer, writes CPU data, and the ring buffer handles fence synchronization to prevent overwriting data the GPU is still reading.

### Profile and logging

`erhe::profile` and `erhe::log` are near-universal dependencies:

- **`profile`** is depended on by 20+ libraries (usually PRIVATE). It provides `ERHE_PROFILE_FUNCTION()`, `ERHE_PROFILE_SCOPE()`, `ERHE_PROFILE_MUTEX()`, and `erhe::vector` (allocator-tracked vector). When the profiler is disabled, all macros become no-ops.

- **`log`** is depended on by 20+ libraries (usually PRIVATE). Each library defines its own log categories (e.g., `log_scene`, `log_graphics`) using `spdlog` wrappers.
