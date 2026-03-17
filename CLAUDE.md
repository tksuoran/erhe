# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**erhe** is a C++ graphics library and editor built on modern OpenGL (with Vulkan support work in progress, not yet reedy for use). It features a Vulkan-like abstraction over OpenGL, a render graph system, full 3D scene graph, physics (Jolt), geometry manipulation (Catmull-Clark, Conway operators via Geogram), and an ImGui-based editor application.

## Building

### Windows (Visual Studio 2026)

From an x64 Native Tools Command Prompt:

```bat
scripts\configure_vs2026_opengl.bat
```

This generates a Visual Studio solution in `build_vs2026_opengl/`. Open it and build the `editor` target.

Alternative configurations:
- `configure_vs2026_opengl_asan.bat` — with AddressSanitizer
- `configure_vs2026_opengl_no_tracy.bat` — without Tracy profiler
- `configure_vs2026_vulkan.bat` — Vulkan backend

### Using CMake Presets

```bash
cmake --preset OpenGL_Debug    # Debug OpenGL build
cmake --preset OpenGL_Release  # Release OpenGL build
cmake --preset Vulkan_Debug    # Debug Vulkan build
```

Build output goes to `build/<presetName>/`.

### Linux

```bash
cmake -B build -DERHE_GRAPHICS_LIBRARY=opengl -DERHE_WINDOW_LIBRARY=sdl ...
cmake --build build --target editor
```

Required packages: `libwayland-dev libxkbcommon-dev xorg-dev` (Ubuntu) or equivalent.

### Key CMake Options

| Option | Default | Notes |
|--------|---------|-------|
| `ERHE_GRAPHICS_LIBRARY` | — | `opengl` or `vulkan` |
| `ERHE_PHYSICS_LIBRARY` | `jolt` | `jolt` or `none` |
| `ERHE_RAYTRACE_LIBRARY` | `bvh` | `bvh`, `embree`, or `none` (none uses GPU ID-buffer picking) |
| `ERHE_PROFILE_LIBRARY` | `tracy` | `tracy`, `nvtx`, `superluminal`, or `none` |
| `ERHE_WINDOW_LIBRARY` | `sdl` | `sdl` or `glfw` |
| `ERHE_XR_LIBRARY` | `openxr` | `openxr` or `none` |
| `ERHE_USE_ASAN` | `OFF` | AddressSanitizer |
| `ERHE_USE_PRECOMPILED_HEADERS` | `OFF` | Speeds up builds |

## Architecture

### Library Structure (`src/erhe/`)

Each subdirectory is a separate CMake target (`erhe_<name>`):

- **`erhe::gl`** — Python-generated type-safe OpenGL API wrappers (`generate_sources.py` parses `gl.xml`). Provides strongly-typed enums, call logging, and extension queries.
- **`erhe::graphics`** — Vulkan-style abstraction over OpenGL: `Pipeline`, framebuffers, textures, shaders, buffers. Core rendering primitives.
- **`erhe::rendergraph`** — DAG of `Rendergraph_node`s with typed inputs/outputs. Executed in dependency order each frame.
- **`erhe::scene`** — glTF-like scene graph: `Node` (transform + children), `Mesh`, `Camera`, `Light`, `Scene`.
- **`erhe::scene_renderer`** — Renders `erhe::scene` content using `erhe::renderer` utilities.
- **`erhe::geometry`** — Polygon mesh manipulation using Geogram as backend. Catmull-Clark, Conway operators, CSG (experimental).
- **`erhe::primitive`** — Converts `erhe::geometry::Geometry` to GPU vertex/index buffers.
- **`erhe::item`** — Base `Item` (name, id, flags) and `Hierarchy` (parent/child tree) classes.
- **`erhe::renderer`** — GPU ring buffer, `Line_renderer` (debug lines), `Text_renderer` (2D labels in 3D viewports).
- **`erhe::imgui`** — Custom ImGui backend and window management helpers.
- **`erhe::physics`** — Thin abstraction over Jolt physics.
- **`erhe::window`** — SDL/GLFW windowing abstraction.
- **`erhe::commands`** — Input command system.
- **`erhe::log`** — spdlog wrappers.
- **`erhe::verify`** — `VERIFY(condition)` and `FATAL(format, ...)` macros.

### Editor Application (`src/editor/`)

The `editor` executable is the main application. Entry point is `src/editor/main.cpp` → `editor::run_editor()`. Key subsystems:

- `rendergraph/` — Editor-specific render graph nodes (shadow maps, viewports, post-processing)
- `tools/` — Editor tools (translate, rotate, scale, brush painting, etc.)
- `operations/` — Geometry operations (Catmull-Clark, Conway, CSG)
- `windows/` — ImGui windows (scene browser, properties, console, etc.)
- `scene/` — Scene management and content library
- `res/` — Resources: TOML configuration files and GLSL shaders

### CMake Conventions

- `erhe_target_sources_grouped()` helper macro (in `cmake/functions.cmake`) organizes sources into IDE source groups.
- Each component has its own `CMakeLists.txt`.
- External dependencies are fetched at configure time via CPM (`cmake/CPM.cmake`).

### Code Generation

`src/erhe/gl/generate_sources.py` generates the OpenGL wrapper from `gl.xml`. Run this script when updating the GL API bindings.

### Active Work: `erhe_codegen` (Python C++ Struct Code Generator)

`src/erhe/codegen/` — A new Python-based code generator that produces C++ structs with versioned JSON serialization (simdjson), deserialization, rich reflection, and enum support from Python definitions. See `src/erhe/codegen/PLAN.md` for the full design and implementation milestones (M1–M9). Implementation has not yet started beyond the plan and empty package skeleton.

### Backends / Abstraction Layers

Many systems have swappable backends selected at CMake configure time via `#ifdef ERHE_<SUBSYSTEM>_LIBRARY_<VALUE>` guards. This is especially true for physics, raytrace, window, and XR subsystems.
