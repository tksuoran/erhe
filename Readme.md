# erhe

> A C++ 3D graphics library and sandbox editor, with a Vulkan/Metal-style rendering abstraction over OpenGL, Vulkan, and Metal.

[![build](https://github.com/tksuoran/erhe/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/tksuoran/erhe/actions/workflows/build.yml)
[![tests](https://github.com/tksuoran/erhe/actions/workflows/tests.yml/badge.svg?branch=main)](https://github.com/tksuoran/erhe/actions/workflows/tests.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20%7C%20macOS%20%7C%20Quest-lightgrey.svg)

![screenshot](https://github.com/tksuoran/erhe/wiki/images/13.png)

erhe is an actively-developed personal research project for real-time rendering and geometry. Its editor is a sandbox for 3D scene creation, manipulation, and procedural mesh generation. erhe is the evolution of [RenderStack](https://github.com/tksuoran/RenderStack).

[![erhe summer 2024](https://img.youtube.com/vi/8hnKr348qt8/0.jpg)](https://www.youtube.com/watch?v=8hnKr348qt8)

## Quick start

```sh
git clone https://github.com/tksuoran/erhe
cd erhe

# Windows (from an x64 Native Tools Command Prompt)
scripts\configure_vs2026_vulkan.bat

# Linux
scripts/configure_ninja_linux_vulkan.sh && cmake --build build_ninja_linux_vulkan --target editor

# macOS
scripts/configure_xcode_metal.sh && cmake --build build_xcode_metal --target editor --config Debug
```

Vulkan is the default backend; the OpenGL build uses the `*_opengl` scripts instead (e.g. `scripts\configure_vs2026_opengl.bat`). See [doc/building.md](doc/building.md) for all backends, CMake options, and IDE setup.

## Platforms

| Platform | Graphics backends | Status |
| :--- | :--- | :--- |
| Windows | OpenGL, Vulkan | Primary |
| Linux | OpenGL, Vulkan | Supported |
| macOS | Vulkan, Metal | Supported |
| Meta Quest / Android | Vulkan + OpenXR | Supported |

The OpenGL backend requires OpenGL 4.5 (with Direct State Access) at minimum; 4.6 is recommended. It is not available on Apple platforms.

## Editor

The editor is a sandbox application for 3D scene creation and manipulation.

### Scene and Viewport

-   Multiple 3D viewports with independent camera and rendering settings
-   Hierarchical scene graph with node parenting
-   glTF import and export
-   Partial USD support: open, edit and save `.usd` / `.usda` / `.usdc` / `.usdz` stages, with references and payloads shown as prefab instances, variant sets, class prims as styles, and UsdPhysics / UsdSkel content
-   Scenes save as a single glTF binary carrying `ERHE_*` extensions, or as USD; a scene stays in the format it was opened from
-   Multi-scene support
-   Post-processing pipeline (bloom, tonemapping)
-   Shadow mapping
-   PBR material system with metallic/roughness workflow
-   Material library with preview rendering

### Tools

-   **Fly camera** -- 6DOF camera navigation with WASD, orbit, tumble, track, zoom
-   **Selection** -- click-to-select with shift/ctrl multi-select, GPU ID buffer or raytrace picking
-   **Transform gizmo** -- translate, rotate, scale with axis/plane constraints and snapping
-   **Brush placement** -- place shapes on surfaces with snap-to-face, rotation, and scale-to-match
-   **Vertex painting** -- paint vertex colors directly on meshes
-   **Material painting** -- assign materials to individual mesh faces
-   **Physics interaction** -- drag, push, and pull rigid bodies in the scene
-   **Grid** -- configurable grid display and snap-to-grid

### Geometry Operations

-   **Subdivision**: Catmull-Clark, sqrt3
-   **Conway operators**: ambo, chamfer, dual, gyro, join, kis, meta, truncate, chamfer/bevel
-   **CSG / boolean**: union, intersection, difference (experimental, via Geogram)
-   **Utilities**: triangulate, normalize, reverse, weld, repair, generate tangents, bake transform
-   Full undo/redo for all operations

### Mesh Creation

-   Parametric shapes: sphere, box, cone, torus, disc, cylinder
-   Brush system with reusable shape templates
-   180+ built-in polyhedra (Platonic solids, Johnson solids, geodesic domes, etc.)
-   Procedural scene generation

### Node Graphs

-   **Texture graph** -- Material Maker-style procedural texture authoring with ~75 node types (noises, patterns, gradients, transforms, color operations); nodes compose into a single fragment shader evaluated on the GPU, with live per-node previews, and bake to textures and PBR material channels
-   **Geometry graph** -- node-based procedural modeling: parametric generators, subdivision, Conway operators, lattice deform, boolean CSG, point scattering and instancing, nested node groups; evaluated asynchronously on worker threads so editing stays interactive
-   Shared node-editor canvas with a dockable node palette, undo/redo, and JSON serialization; graphs are content-library assets

### Ray Tracing

-   CPU ray tracing library with swappable backends (madmann91 bvh, tinybvh, Embree 4) and BVH disk caching, used for mouse picking and spatial queries
-   GPU ray tracing renderer (Vulkan ray query compute): PBR material shading, real scene lights, ray-traced shadows, reflection and refraction for transmissive materials

### File Format Support

-   glTF 2.0 import and export (via fastgltf), including `KHR_physics_rigid_bodies`, `KHR_materials_variants` and the erhe-specific [`ERHE_*` extensions](doc/gltf_extensions/README.md)
-   Partial OpenUSD import and export (via LightUSD; optional, `ERHE_USD_LIBRARY=lightusd`): meshes, UsdPreviewSurface and OpenPBR materials, cameras, lights, xformOp stacks, time samples, skinning, physics, point instancers, composition arcs and variants; MaterialX documents are not read. See [doc/erhe/usd_compatibility.md](doc/erhe/usd_compatibility.md)
-   Partial support for Wavefront OBJ import

### VR / OpenXR

-   Headset rendering with OpenXR
-   Hand and controller tracking with visualization
-   Floating HUD and hotbar UI in VR
-   Passthrough support

### Input

-   Keyboard and mouse with configurable bindings
-   SpaceMouse 6DOF input
-   OpenXR controllers

### Debug and Development

-   Debug line and text rendering in 3D viewports
-   Physics shape visualization
-   Render graph visualization window
-   Shader hot-reload via shader monitor
-   GL state dump to clipboard
-   Tracy profiler integration

### MCP Server

The editor embeds an [MCP](https://modelcontextprotocol.io/) server (JSON-RPC over HTTP on `127.0.0.1:3743`), so AI agents and scripts can drive a running editor. It works in the windowed build, in the headless build (no display needed) and on Quest over `adb forward`. See [mcp_server_usage.md](mcp_server_usage.md) for the API reference.

-   **Scene queries** -- scenes, nodes, cameras, lights, materials, textures, brushes, selection, undo/redo stack, physics items, async load status
-   **Scene editing** -- create shapes and nodes, place brushes, select, transform, reparent, edit materials, geometry operations, mesh component (face / edge / vertex) editing, physics bodies and joints, node graphs; edits go through the undo stack
-   **Properties** -- generic read / write access to the registered properties of any item, including styles and expressions
-   **Files** -- open, save and close scenes; glTF and USD import and export
-   **Screenshots** -- the editor's own composited frame, in both windowed and headless builds, optionally annotated with ImGui item rectangles
-   **ImGui introspection** -- list ImGui hosts, windows and items with their rectangles, and click, hover or scroll an item addressed by its label
-   **Input event injection** -- mouse clicks, drags and wheel, key presses and text are injected as real window input events, so menus, docking, property rows, gizmo drags and viewport gestures are driven the way a user drives them; see the [UI driving run-book](doc/agents/mcp_ui_driving.md)
-   All registered editor commands (undo, redo, delete, ...) are callable as tools
-   The MCP test suite (`mcp_server_tests`) and the scene round-trip verification scripts run against this server
-   [AI creations](doc/agents/creations.md) catalogs agent-built showcase scenes and the editor features each exercises

## Libraries

erhe is organized as a set of independent libraries under `src/erhe/`. Each has a `doc/erhe/<name>.md` document with details on purpose, API, and design; [doc/README.md](doc/README.md) indexes them.

| Library | Description |
| :--- | :--- |
| `erhe::graphics` | Vulkan/Metal-style graphics abstraction: pipelines, buffers, textures, shaders, ring buffers, shader monitor |
| `erhe::rendergraph` | DAG of render nodes with typed inputs/outputs, executed in dependency order |
| `erhe::scene` | glTF-like scene graph: nodes, meshes, cameras, lights, animations, skins |
| `erhe::gltf` | glTF 2.0 import and export via fastgltf, including KHR physics extensions |
| `erhe::usd` | Partial OpenUSD import and export via LightUSD (optional) |
| `erhe::raytrace` | CPU ray tracing abstraction with swappable bvh / tinybvh / Embree backends and BVH disk caching |
| `erhe::texgen` | Procedural texture generation: data-driven node descriptors composed into GLSL fragment shaders (Material Maker port) |
| `erhe::graph` | Generic node graph: nodes, pins, links, topological-order evaluation; base for the geometry, texture, and shader graphs |
| `erhe::xr` | OpenXR integration: session and swapchain management, action-based input, hand tracking, stereo rendering |
| `erhe::scene_renderer` | Forward renderer, shadow renderer, ID picking, camera/light/material/joint GPU buffers |
| `erhe::renderer` | Debug line renderer (compute, geometry shader, or GL_LINES), text renderer, texture blit |
| `erhe::geometry` | Polygon mesh manipulation via Geogram: subdivision, Conway operators, CSG, shape generators |
| `erhe::primitive` | Converts geometry meshes to GPU vertex/index buffers; PBR material definitions |
| `erhe::physics` | Thin abstraction over Jolt and Box3D physics: rigid bodies, collision shapes, constraints |
| `erhe::imgui` | Custom ImGui backend with per-host ImGui contexts and window management |
| `erhe::commands` | Input command system with state machine, priority dispatch, and bindings for all input types |
| `erhe::window` | SDL / GLFW windowing abstraction with input event handling |
| `erhe::item` | Base `Item` (name, id, flags) and `Hierarchy` (parent/child tree) classes |
| `erhe::property` | Dependency-property system (modeled on WPF): registered typed properties with layered values (local, style, reference, inherited, default), expressions, computed and animated values, change notification; every scene item carries a property store |
| `erhe::gl` | Generated type-safe OpenGL wrappers with call logging and extension queries |
| `erhe::math` | Bounding volumes, viewport projection, input axis filtering, vector/matrix helpers |
| `erhe::dataformat` | Graphics-API-agnostic pixel and vertex format definitions |
| `erhe::message_bus` | Typed publish-subscribe bus used to decouple editor subsystems |
| `erhe::profile` | Unified profiling macros dispatching to Tracy, Superluminal, or NVTX |
| `erhe::ui` | FreeType glyph rasterization and HarfBuzz shaping into GPU font atlases |
| `erhe::log` | spdlog wrappers |
| `erhe::verify` | `VERIFY(condition)` and `FATAL(format, ...)` macros |
| `erhe::codegen` | Python code generator for C++ structs with versioned JSON serialization via simdjson |

### OpenGL Compatibility

erhe requires OpenGL 4.5 with DSA (Direct State Access); device creation fails on older versions. DSA, SSBOs, compute shaders and clip control are used unconditionally. The former OpenGL 4.1 (macOS) runtime compatibility layer has been removed.

## Documentation

All documentation lives under [doc/](doc/README.md); `doc/README.md` states the layout and indexes every document with its stability level.

| Where | What |
| :--- | :--- |
| [doc/building.md](doc/building.md) | Build instructions, platform requirements, CMake options; also [Quest](doc/quest.md) and [Android](doc/android.md) |
| [doc/erhe/](doc/README.md#libraries-and-library-level-subsystems-erhe) | One document per `erhe::*` library, plus library-level subsystems: [Vulkan backend](doc/erhe/vulkan_backend.md), [Metal backend](doc/erhe/metal_backend.md), [draw list renderer](doc/erhe/draw_list_renderer.md), [shadows](doc/erhe/shadows.md), [property system](doc/erhe/property_system.md), [USD compatibility](doc/erhe/usd_compatibility_design.md) |
| [doc/editor/](doc/README.md#editor-editor) | The editor: [application overview](doc/editor/editor.md), one document per source subdirectory, and one per feature, e.g. [scene serialization](doc/editor/scene_serialization.md), [texture graph](doc/editor/texture_graph.md), [geometry nodes](doc/editor/geometry_nodes.md), [ray tracing](doc/editor/raytrace.md), and experimental features such as [lightmap baking](doc/editor/lightmap_baking.md) and [DDGI](doc/editor/ddgi.md) |
| [doc/frame_pacing/](doc/README.md#frame-pacing-frame_pacing) | Frame pacer requirements, algorithm and behavior |
| [doc/gltf_extensions/](doc/gltf_extensions/README.md) | Specification and JSON schemas of the `ERHE_*` glTF extensions |
| [doc/agents/](doc/README.md#agents-agents) | Documentation for AI coding agents: MCP guidelines and UI driving, orchestration harness, RenderDoc run-books; [AGENTS.md](AGENTS.md) holds the project rules |
| [doc/plans/](doc/README.md#plans-plans) | Future work |
| [doc/reference/](doc/README.md#reference-reference) | Material erhe does not own: upstream bug reports, comparisons, transcribed specifications |

## License

erhe's own source is MIT-licensed -- see [src/erhe/LICENSE](src/erhe/LICENSE). Bundled and fetched third-party dependencies retain their own licenses; see [License.txt](License.txt) for the overview.

## Acknowledgements

erhe stands on many excellent open-source projects, including [Dear ImGui](https://github.com/ocornut/imgui), [Jolt Physics](https://github.com/jrouwe/JoltPhysics), [Geogram](https://github.com/BrunoLevy/geogram), [fastgltf](https://github.com/spnda/fastgltf), [simdjson](https://github.com/simdjson/simdjson), [SDL](https://github.com/libsdl-org/SDL), [OpenXR](https://github.com/KhronosGroup/OpenXR-SDK), [FreeType](https://www.freetype.org/), [HarfBuzz](https://github.com/harfbuzz/harfbuzz), [Tracy](https://github.com/wolfpld/tracy), [GLM](https://github.com/g-truc/glm), [fmt](https://github.com/fmtlib/fmt), and [spdlog](https://github.com/gabime/spdlog). Dependencies are fetched at configure time via [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake).

Static analysis provided by [PVS-Studio](https://pvs-studio.com/en/pvs-studio/?utm_source=website&utm_medium=github&utm_campaign=open_source) - static analyzer for C, C++, C#, and Java code, free for open-source projects.
