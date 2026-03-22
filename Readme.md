[![Codacy Badge](https://app.codacy.com/project/badge/Grade/49fade7c78954f3a99a2d6ce84a9bc1a)](https://www.codacy.com/gh/tksuoran/erhe/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=tksuoran/erhe&amp;utm_campaign=Badge_Grade)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)

# erhe

erhe is a C++20 graphics library and 3D editor built on modern OpenGL (with Vulkan support work in progress).

-   Vulkan-like abstraction over OpenGL with pipeline state objects (see `erhe::graphics`)
-   Python-generated type-safe OpenGL API wrappers (see `erhe::gl`)
-   Render graph system for composing rendering passes
-   Full 3D scene graph with glTF-like structure
-   Geometry manipulation with Catmull-Clark subdivision and Conway operators (via Geogram)
-   Physics simulation (Jolt Physics)
-   CPU ray tracing for mouse picking
-   ImGui-based editor application with transform gizmos, brush tools, and geometry operations
-   OpenXR support for VR headsets
-   Supports Windows and Linux

erhe is the evolution of [RenderStack](https://github.com/tksuoran/RenderStack).

![screenshot](https://github.com/tksuoran/erhe/wiki/images/13.png)

[![erhe summer 2024](https://img.youtube.com/vi/8hnKr348qt8/0.jpg)](https://www.youtube.com/watch?v=8hnKr348qt8)

## Building

### Dependencies

All dependencies are either included directly in `src/` for small libraries,
or fetched from their repositories using CMake CPM during the configure step.

### Requirements

| Requirement | Windows                              | Linux                              |
| :---------- | :----------------------------------- | :--------------------------------- |
| C++         | Visual Studio 2022 or 2026           | Recent GCC or Clang                |
| Standard    | C++20                                | C++20                              |
| CMake       | 3.16.3+                              | 3.16.3+                            |
| Python      | Python 3                             | Python 3                           |
| Packages    | ---                                  | `libwayland-dev libxkbcommon-dev xorg-dev` (Ubuntu) |

Arch Linux packages: `gcc ninja cmake git python libx11 libxrandr libxi libxinerama libxcursor mesa wayland wayland-protocols`

### Build Steps for Visual Studio (Windows)

1.  `git clone https://github.com/tksuoran/erhe`
2.  In an *x64 Native Tools Command Prompt for VS 2026* (or 2022), cd to the *erhe* directory
3.  `scripts\configure_vs2026_opengl.bat`
4.  Open the solution from the build directory with Visual Studio
5.  Build the `editor` target

Alternative configure scripts:

| Script                                 | Description                        |
| :------------------------------------- | :--------------------------------- |
| `configure_vs2026_opengl.bat`          | VS 2026, OpenGL                    |
| `configure_vs2026_opengl_asan.bat`     | VS 2026, OpenGL, AddressSanitizer  |
| `configure_vs2026_opengl_no_tracy.bat` | VS 2026, OpenGL, without Tracy     |
| `configure_vs2026_vulkan.bat`          | VS 2026, Vulkan                    |
| `configure_vs2022.bat`                 | VS 2022, OpenGL                    |
| `configure_ninja.bat`                  | Ninja with Clang                   |

### Using CMake Presets

```bash
cmake --preset OpenGL_Debug       # Debug OpenGL build
cmake --preset OpenGL_Release     # Release OpenGL build
cmake --preset OpenGL_ASAN_Debug  # Debug OpenGL with AddressSanitizer
cmake --preset Vulkan_Debug       # Debug Vulkan build
cmake --preset Vulkan_Release     # Release Vulkan build
```

Build output goes to `build/<presetName>/`.

### Build Steps for Visual Studio Code (Windows and Linux)

1.  `git clone https://github.com/tksuoran/erhe`
2.  Open the erhe folder in Visual Studio Code
3.  Execute command: *CMake: Select Configure Preset*
4.  Execute command: *CMake: Configure*
5.  Execute command: *CMake: Build*

## CMake Configuration Options

| Option                          | Default    | Values                            |
| :------------------------------ | :--------- | :-------------------------------- |
| ERHE_GRAPHICS_LIBRARY           | `opengl`   | `opengl`, `vulkan`                |
| ERHE_PHYSICS_LIBRARY            | `jolt`     | `jolt`, `none`                    |
| ERHE_RAYTRACE_LIBRARY           | `bvh`      | `bvh`, `embree`, `none`           |
| ERHE_PROFILE_LIBRARY            | `none`     | `tracy`, `nvtx`, `superluminal`, `none` |
| ERHE_WINDOW_LIBRARY             | `sdl`      | `sdl`, `glfw`, `none`             |
| ERHE_XR_LIBRARY                 | `none`     | `openxr`, `none`                  |
| ERHE_GUI_LIBRARY                | `imgui`    | `imgui`, `none`                   |
| ERHE_GLTF_LIBRARY               | `fastgltf` | `fastgltf`, `none`                |
| ERHE_FONT_RASTERIZATION_LIBRARY | `freetype` | `freetype`, `none`                |
| ERHE_TEXT_LAYOUT_LIBRARY        | `harfbuzz`  | `harfbuzz`, `freetype`, `none`   |
| ERHE_SVG_LIBRARY                | `plutosvg` | `plutosvg`, `none`                |
| ERHE_AUDIO_LIBRARY              | `none`     | `miniaudio`, `none`               |
| ERHE_TERMINAL_LIBRARY           | `none`     | `cpp-terminal`, `none`            |
| ERHE_USE_PRECOMPILED_HEADERS    | `OFF`      | `ON`, `OFF`                       |
| ERHE_USE_ASAN                   | `OFF`      | `ON`, `OFF`                       |
| ERHE_USE_MIMALLOC               | `OFF`      | `ON`, `OFF`                       |
| ERHE_SPIRV                      | `OFF`      | `ON`, `OFF`                       |
| ERHE_BUILD_TESTS                | `OFF`      | `ON`, `OFF`                       |

Main purposes of these configuration options are:

-   Allow faster build times by disabling features not needed during development
-   Choose different backends for graphics, physics, raytrace, windowing, and profiling

### ERHE_GRAPHICS_LIBRARY

`opengl` is the primary and fully functional backend. `vulkan` support is work in progress
and not yet ready for use.

### ERHE_RAYTRACE_LIBRARY

The `bvh` backend is used by default for CPU ray tracing (mouse picking in 3D viewports).
The `embree` backend exists but has not been maintained recently. When set to `none`,
mouse picking uses GPU-based ID buffer rendering with readback instead.

### ERHE_PROFILE_LIBRARY

Tracy is the recommended profiling backend. Superluminal and NVTX have basic support
but are not actively maintained.

### ERHE_XR_LIBRARY

OpenXR enables VR headset rendering in the editor. Disabling removes headset support.

### ERHE_GLTF_LIBRARY

erhe uses fastgltf for importing and exporting glTF files. Disabling removes glTF support in the editor.

### ERHE_SVG_LIBRARY

plutosvg is used for scene node icons. Disabling removes icon display.

### ERHE_FONT_RASTERIZATION_LIBRARY / ERHE_TEXT_LAYOUT_LIBRARY

FreeType and HarfBuzz provide native text rendering in 3D viewports. Disabling either
removes native text rendering. ImGui content is not affected.

## Editor Features

The `editor` is the main application, a 3D editor and sandbox for scene composition,
geometry manipulation, and rendering experiments.

### Scene Management

-   Multi-scene support with hierarchical scene graph
-   Scene browser with drag-and-drop
-   glTF and OBJ file import
-   glTF export
-   Content library for brushes, materials, and animations
-   Undo/redo system with asynchronous geometry operations

### Transform Tools

-   Translation, rotation, and scale gizmos with animated transitions
-   Local and world space modes
-   Axis and plane constraints
-   Grid snapping

### Geometry Operations

-   Catmull-Clark subdivision
-   Sqrt3 subdivision
-   Conway operators: dual, ambo, truncate, kis, join, meta, gyro, chamfer, subdivide
-   Boolean operations (CSG): union, intersection, difference
-   Triangulate, reverse normals, normalize, repair, weld, generate tangents

### Brush Tool

-   Place parametric mesh shapes (sphere, cone, torus, box) onto surfaces
-   Preview ghost display during placement
-   Snap to grid or polygon faces
-   Configurable subdivision density

### Painting

-   Vertex color painting
-   Per-face material assignment

### Rendering

-   Configurable multi-pass render graph
-   Shadow mapping
-   Post-processing (bloom, tonemapping)
-   Debug visualization overlays for lights, physics shapes, joints
-   Edge lines and wireframe modes
-   Per-viewport rendering configuration
-   GPU-based object and triangle picking via ID buffer

### Physics

-   Jolt Physics integration
-   Interactive drag, push, and pull with constraints
-   Configurable gravity and simulation parameters
-   Debug visualization of collision shapes

### VR/XR Support

-   OpenXR headset rendering
-   VR controller visualization and interaction
-   Hand tracking
-   Head-up display (HUD) rendered to VR overlay
-   Hotbar toolbar for tool switching

### UI Windows

-   Scene hierarchy browser
-   Property inspector (cameras, lights, meshes, materials, skins, animations, textures)
-   Viewport configuration
-   Operations panel with geometry operation buttons
-   Selection display
-   Clipboard history
-   Tool properties
-   Render graph visualization
-   Post-processing controls
-   Application settings
-   Icon browser

## erhe Libraries

Each library is a separate CMake target under `src/erhe/`. Every library has a `notes.md`
file documenting its purpose, key types, public API, and dependencies.

### Core Graphics and Rendering

| Library                     | Description                                                      |
| :-------------------------- | :--------------------------------------------------------------- |
| `erhe::gl`                  | Python-generated type-safe OpenGL API wrappers with strongly typed enums, call logging, and extension queries |
| `erhe::graphics`            | Vulkan-style abstraction over OpenGL: pipeline state, framebuffers, textures, shaders, buffers, ring buffers |
| `erhe::dataformat`          | Graphics-API-agnostic pixel and vertex format definitions        |
| `erhe::rendergraph`         | DAG framework for composing rendering operations with typed input/output connectors |

### Scene Graph

| Library                     | Description                                                      |
| :-------------------------- | :--------------------------------------------------------------- |
| `erhe::scene`               | glTF-like 3D scene graph: Node, Mesh, Camera, Light, Animation, Skin, Scene |
| `erhe::scene_renderer`      | Forward and shadow rendering for scene content with GPU buffer management |
| `erhe::item`                | Base Item class (name, id, flags) and Hierarchy (parent/child tree) |
| `erhe::gltf`                | glTF import/export using fastgltf                                |

### Geometry

| Library                     | Description                                                      |
| :-------------------------- | :--------------------------------------------------------------- |
| `erhe::geometry`            | Polygon mesh manipulation via Geogram: Catmull-Clark, Sqrt3, Conway operators, CSG |
| `erhe::geometry_renderer`   | Debug visualization of geometry meshes with wireframe and labels |
| `erhe::primitive`           | Converts geometry to GPU-ready vertex/index buffers with materials |

### Rendering Utilities

| Library                     | Description                                                      |
| :-------------------------- | :--------------------------------------------------------------- |
| `erhe::renderer`            | Debug line renderer, text renderer, texture renderer, GPU ring buffers, Jolt debug renderer |
| `erhe::buffer`              | Buffer allocation primitives with free-list allocator and RAII handles |
| `erhe::graphics_buffer_sink`| Bridges geometric data to GPU buffers with memory reclamation    |

### Physics and Ray Tracing

| Library                     | Description                                                      |
| :-------------------------- | :--------------------------------------------------------------- |
| `erhe::physics`             | Abstraction over physics engines (Jolt Physics, null backend)    |
| `erhe::raytrace`            | CPU ray tracing abstraction (BVH, Embree, null backends)         |

### UI and Input

| Library                     | Description                                                      |
| :-------------------------- | :--------------------------------------------------------------- |
| `erhe::imgui`               | Custom ImGui backend with GPU rendering and multi-context support |
| `erhe::ui`                  | Font rasterization (FreeType) and text layout (HarfBuzz)         |
| `erhe::window`              | Platform windowing abstraction (SDL, GLFW)                       |
| `erhe::commands`            | Input command system with key/mouse/controller bindings and state machine |
| `erhe::xr`                  | OpenXR integration for VR/AR headsets                            |

### Infrastructure

| Library                     | Description                                                      |
| :-------------------------- | :--------------------------------------------------------------- |
| `erhe::graph`               | Generic directed acyclic graph framework                         |
| `erhe::math`                | Math utilities: AABB, bounding sphere, viewport, input smoothing |
| `erhe::log`                 | Logging infrastructure on spdlog with in-memory log sink         |
| `erhe::verify`              | `ERHE_VERIFY(condition)` and `ERHE_FATAL(format, ...)` assertion macros (always active) |
| `erhe::configuration`       | INI/TOML configuration file reading                              |
| `erhe::file`                | Filesystem utilities: read/write, path conversion, native file dialogs |
| `erhe::message_bus`         | Generic typed publish-subscribe message bus                      |
| `erhe::net`                 | Cross-platform TCP networking (BSD sockets, select-based)        |
| `erhe::hash`                | Hashing utilities (FNV-1a runtime, XXH32 compile-time)           |
| `erhe::profile`             | Profiling abstraction (Tracy, Superluminal, NVTX, none)          |
| `erhe::time`                | High-precision sleep and timer utilities                         |
| `erhe::defer`               | Scope guard for deferred execution                               |
| `erhe::utility`             | Alignment, bitwise tests, debug labels, string pool              |
| `erhe::pch`                 | Precompiled header aggregating common includes                   |

### Code Generation

| Library                     | Description                                                      |
| :-------------------------- | :--------------------------------------------------------------- |
| `erhe::codegen`             | Python-based C++ struct code generator producing versioned JSON serialization, deserialization, reflection, and enum support from Python definitions |

## Other Executables

| Executable    | Description                                                     |
| :------------ | :-------------------------------------------------------------- |
| `example`     | Minimal rendering example using erhe libraries                  |
| `hello_swap`  | Minimal window and OpenGL context creation example              |
| `hextiles`    | Hex-based tile map application (requires ImGui)                 |

## Tests

Tests are enabled with the CMake option `ERHE_BUILD_TESTS=ON` (off by default).

### erhe_item_tests

Unit tests for the `erhe::item` library using [Google Test](https://github.com/google/googletest) 1.16.0.
Tests cover:

-   `Unique_id` generation and uniqueness
-   `Item_flags` bit manipulation
-   `Item_filter` matching logic
-   `Item_base` construction and properties
-   `Item` CRTP template system
-   `Hierarchy` parent/child operations
-   `Item_host` hosting and locking

### erhe_codegen_test

Tests for the `erhe::codegen` code generator, verifying that generated C++ structs
correctly serialize, deserialize, and migrate between schema versions.

### erhe_smoke

A standalone stress test executable that exercises `erhe::item::Hierarchy` with
randomized attach, detach, and reparent operations, validating structural invariants.

## SAST Tools

[PVS-Studio](https://pvs-studio.com/en/pvs-studio/?utm_source=website&utm_medium=github&utm_campaign=open_source) - static analyzer for C, C++, C#, and Java code.
