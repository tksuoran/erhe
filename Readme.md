# erhe

> A C++ 3D graphics library and sandbox editor, with a Vulkan/Metal-style rendering abstraction over OpenGL, Vulkan, and Metal.

[![build](https://github.com/tksuoran/erhe/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/tksuoran/erhe/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)

![screenshot](https://github.com/tksuoran/erhe/wiki/images/13.png)

erhe is an actively-developed personal research project for real-time rendering and geometry. Its editor is a sandbox for 3D scene creation, manipulation, and procedural mesh generation. erhe is the evolution of [RenderStack](https://github.com/tksuoran/RenderStack).

[![erhe summer 2024](https://img.youtube.com/vi/8hnKr348qt8/0.jpg)](https://www.youtube.com/watch?v=8hnKr348qt8)

## Quick start

```sh
git clone https://github.com/tksuoran/erhe
cd erhe

# Windows (from an x64 Native Tools Command Prompt)
scripts\configure_vs2026_opengl.bat

# Linux
scripts/configure_ninja_linux.sh && cmake --build build --target editor

# macOS
scripts/configure_xcode_metal.sh && cmake --build build_xcode_metal --target editor --config Debug
```

See [doc/building.md](doc/building.md) for all backends, CMake options, and IDE setup.

## Platforms

| Platform | Graphics backends | Status |
| :--- | :--- | :--- |
| Windows | OpenGL, Vulkan | Primary |
| Linux | OpenGL, Vulkan | Supported |
| macOS | OpenGL (4.1), Metal | Supported |
| Meta Quest / Android | Vulkan + OpenXR | Experimental |

The OpenGL backend requires OpenGL 4.1 at minimum (the maximum available on macOS); 4.6 is recommended. See [doc/opengl41_compatibility.md](doc/opengl41_compatibility.md) for the compatibility layer.

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

erhe targets OpenGL 4.6 with DSA but includes a runtime compatibility layer for OpenGL 4.1 (the maximum version on macOS). Features like DSA, SSBOs, compute shaders, persistent mapping, and texture views are emulated or gracefully degraded at runtime. See [doc/opengl41_compatibility.md](doc/opengl41_compatibility.md) for details.

## License

erhe's own source is MIT-licensed -- see [src/erhe/LICENSE](src/erhe/LICENSE). Bundled and fetched third-party dependencies retain their own licenses; see [License.txt](License.txt) for the overview.

## Acknowledgements

erhe stands on many excellent open-source projects, including [Dear ImGui](https://github.com/ocornut/imgui), [Jolt Physics](https://github.com/jrouwe/JoltPhysics), [Geogram](https://github.com/BrunoLevy/geogram), [fastgltf](https://github.com/spnda/fastgltf), [simdjson](https://github.com/simdjson/simdjson), [SDL](https://github.com/libsdl-org/SDL), [OpenXR](https://github.com/KhronosGroup/OpenXR-SDK), [FreeType](https://www.freetype.org/), [HarfBuzz](https://github.com/harfbuzz/harfbuzz), [Tracy](https://github.com/wolfpld/tracy), [GLM](https://github.com/g-truc/glm), [fmt](https://github.com/fmtlib/fmt), and [spdlog](https://github.com/gabime/spdlog). Dependencies are fetched at configure time via [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake).

Static analysis provided by [PVS-Studio](https://pvs-studio.com/en/pvs-studio/?utm_source=website&utm_medium=github&utm_campaign=open_source) - static analyzer for C, C++, C#, and Java code, free for open-source projects.
