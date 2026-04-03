[![Codacy Badge](https://app.codacy.com/project/badge/Grade/49fade7c78954f3a99a2d6ce84a9bc1a)](https://www.codacy.com/gh/tksuoran/erhe/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=tksuoran/erhe&amp;utm_campaign=Badge_Grade)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)

# erhe

erhe is a C++ library and editor for 3D graphics with a graphics API abstraction loosely modeled after Vulkan and Metal, with backends for OpenGL and Metal. It runs on Windows, Linux, and macOS. The OpenGL backend requires a minimum of OpenGL 4.1; OpenGL 4.6 is recommended.

![screenshot](https://github.com/tksuoran/erhe/wiki/images/13.png)

[![erhe summer 2024](https://img.youtube.com/vi/8hnKr348qt8/0.jpg)](https://www.youtube.com/watch?v=8hnKr348qt8)

erhe is the evolution of [RenderStack](https://github.com/tksuoran/RenderStack).


## Editor

The editor is a sandbox application for 3D scene creation and manipulation.

### Scene and Viewport

-   Multiple 3D viewports with independent camera and rendering settings
-   Hierarchical scene graph with node parenting
-   glTF import and export
-   Scene serialization to JSON with companion binary glTF
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

### File Format Support

-   glTF 2.0 import and export (via fastgltf)
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

## Libraries

erhe is organized as a set of independent libraries under `src/erhe/`. Each has a `notes.md` with details on purpose, API, and design.

| Library | Description |
| :--- | :--- |
| `erhe::graphics` | Vulkan/Metal-style graphics abstraction: pipelines, buffers, textures, shaders, ring buffers, shader monitor |
| `erhe::rendergraph` | DAG of render nodes with typed inputs/outputs, executed in dependency order |
| `erhe::scene` | glTF-like scene graph: nodes, meshes, cameras, lights, animations, skins |
| `erhe::scene_renderer` | Forward renderer, shadow renderer, ID picking, camera/light/material/joint GPU buffers |
| `erhe::renderer` | Debug line renderer (compute, geometry shader, or GL_LINES), text renderer, texture blit |
| `erhe::geometry` | Polygon mesh manipulation via Geogram: subdivision, Conway operators, CSG, shape generators |
| `erhe::primitive` | Converts geometry meshes to GPU vertex/index buffers; PBR material definitions |
| `erhe::physics` | Thin abstraction over Jolt physics: rigid bodies, collision shapes, constraints |
| `erhe::imgui` | Custom ImGui backend with per-host ImGui contexts and window management |
| `erhe::commands` | Input command system with state machine, priority dispatch, and bindings for all input types |
| `erhe::window` | SDL / GLFW windowing abstraction with input event handling |
| `erhe::item` | Base `Item` (name, id, flags) and `Hierarchy` (parent/child tree) classes |
| `erhe::gl` | Generated type-safe OpenGL wrappers with call logging and extension queries |
| `erhe::log` | spdlog wrappers |
| `erhe::verify` | `VERIFY(condition)` and `FATAL(format, ...)` macros |
| `erhe::codegen` | Python code generator for C++ structs with versioned JSON serialization via simdjson |

### OpenGL Compatibility

erhe targets OpenGL 4.5 with DSA but includes a runtime compatibility layer for OpenGL 4.1 (the maximum version on macOS). Features like DSA, SSBOs, compute shaders, persistent mapping, and texture views are emulated or gracefully degraded at runtime. See [doc/opengl41_compatibility.md](doc/opengl41_compatibility.md) for details.

## Building

See [doc/building.md](doc/building.md) for build instructions, requirements, and CMake configuration options.

## License

MIT -- see [LICENSE](src/erhe/LICENSE).

## SAST Tools

[PVS-Studio](https://pvs-studio.com/en/pvs-studio/?utm_source=website&utm_medium=github&utm_campaign=open_source) - static analyzer for C, C++, C#, and Java code.
