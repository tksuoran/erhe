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
- `configure_vs2026_opengl_asan.bat` - with AddressSanitizer
- `configure_vs2026_opengl_no_tracy.bat` - without Tracy profiler
- `configure_vs2026_vulkan.bat` - Vulkan backend

### macOS (Xcode)

Use the build scripts in `scripts/`:

```bash
scripts/configure_xcode_opengl.sh       # OpenGL backend -> build_xcode_opengl/
scripts/configure_xcode_metal.sh        # Metal backend  -> build_xcode_metal/
scripts/configure_xcode_opengl_asan.sh  # OpenGL + ASAN  -> build_xcode_opengl_asan/
```

Then build with:
```bash
cmake --build build_xcode_opengl --target editor --config Debug
cmake --build build_xcode_metal  --target editor --config Debug
```

**Always use the `scripts/` build scripts on macOS.** Do not use CMake presets directly on macOS.

### Using CMake Presets (Windows/Linux)

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
| `ERHE_GRAPHICS_LIBRARY` | - | `opengl`, `vulkan`, or `none` (headless) |
| `ERHE_PHYSICS_LIBRARY` | `jolt` | `jolt` or `none` |
| `ERHE_RAYTRACE_LIBRARY` | `bvh` | `bvh`, `tinybvh`, `embree`, or `none` (none uses GPU ID-buffer picking) |
| `ERHE_PROFILE_LIBRARY` | `tracy` | `tracy`, `nvtx`, `superluminal`, or `none` |
| `ERHE_WINDOW_LIBRARY` | `sdl` | `sdl`, `glfw`, or `none` (headless) |
| `ERHE_XR_LIBRARY` | `openxr` | `openxr` or `none` |
| `ERHE_USE_ASAN` | `OFF` | AddressSanitizer |
| `ERHE_USE_PRECOMPILED_HEADERS` | `OFF` | Speeds up builds |

## Architecture

### Library Structure (`src/erhe/`)

Each subdirectory is a separate CMake target (`erhe_<name>`). **Each library has a `notes.md`** file with details on purpose, key types, public API, dependencies, and implementation notes. Always check `src/erhe/<name>/notes.md` first when working with a library.

- **`erhe::gl`** - Python-generated type-safe OpenGL API wrappers (`generate_sources.py` parses `gl.xml`). Provides strongly-typed enums, call logging, and extension queries.
- **`erhe::graphics`** - Vulkan-style abstraction over OpenGL: `Pipeline`, framebuffers, textures, shaders, buffers. Core rendering primitives.
- **`erhe::rendergraph`** - DAG of `Rendergraph_node`s with typed inputs/outputs. Executed in dependency order each frame.
- **`erhe::scene`** - glTF-like scene graph: `Node` (transform + children), `Mesh`, `Camera`, `Light`, `Scene`.
- **`erhe::scene_renderer`** - Renders `erhe::scene` content using `erhe::renderer` utilities.
- **`erhe::geometry`** - Polygon mesh manipulation using Geogram as backend. Catmull-Clark, Conway operators, CSG (experimental).
- **`erhe::primitive`** - Converts `erhe::geometry::Geometry` to GPU vertex/index buffers.
- **`erhe::item`** - Base `Item` (name, id, flags) and `Hierarchy` (parent/child tree) classes.
- **`erhe::renderer`** - GPU ring buffer, `Line_renderer` (debug lines), `Text_renderer` (2D labels in 3D viewports).
- **`erhe::imgui`** - Custom ImGui backend and window management helpers.
- **`erhe::physics`** - Thin abstraction over Jolt physics.
- **`erhe::window`** - SDL/GLFW windowing abstraction.
- **`erhe::commands`** - Input command system.
- **`erhe::log`** - spdlog wrappers.
- **`erhe::verify`** - `VERIFY(condition)` and `FATAL(format, ...)` macros.

### Editor Application (`src/editor/`)

The `editor` executable is the main application. Entry point is `src/editor/main.cpp` → `editor::run_editor()`. Key subsystems:

- `rendergraph/` - Editor-specific render graph nodes (shadow maps, viewports, post-processing)
- `tools/` - Editor tools (translate, rotate, scale, brush painting, etc.)
- `operations/` - Geometry operations (Catmull-Clark, Conway, CSG)
- `windows/` - ImGui windows (scene browser, properties, console, etc.)
- `scene/` - Scene management and content library
- `res/` - Resources: TOML configuration files and GLSL shaders

### CMake Conventions

- Follow modern CMake best practices. Do not use deprecated CMake features.
- **Never use `file(GLOB)` or `file(GLOB_RECURSE)` to collect source files.** All source and definition files must be listed explicitly in `CMakeLists.txt`. This is the CMake-recommended practice - globbing does not detect added/removed files without a reconfigure.
- `erhe_target_sources_grouped()` helper macro (in `cmake/functions.cmake`) organizes sources into IDE source groups.
- Each component has its own `CMakeLists.txt`.
- External dependencies are fetched at configure time via CPM (`cmake/CPM.cmake`).

### Code Generation

`src/erhe/gl/generate_sources.py` generates the OpenGL wrapper from `gl.xml`. Run this script when updating the GL API bindings.

### `erhe_codegen` (Python C++ Struct Code Generator)

`src/erhe/codegen/` - A Python-based code generator that produces C++ structs with versioned JSON serialization (simdjson), deserialization, rich reflection, and enum support from Python definitions. The implementation is complete.

### Backends / Abstraction Layers

Many systems have swappable backends selected at CMake configure time via `#ifdef ERHE_<SUBSYSTEM>_LIBRARY_<VALUE>` guards. This is especially true for physics, raytrace, window, and XR subsystems.

## Python

**IMPORTANT: On this Windows machine, always use the `py -3` launcher to run Python scripts. Never use `python` or `python3` - they resolve to the Microsoft Store stub and will fail.** This applies to all Python invocations: scripts, codegen, tools.

Example:

```bash
py -3 src/erhe/codegen/generate.py <definitions_dir> <output_dir>
```

## Editor Improvement Plan

See [`doc/editor_improvements.md`](doc/editor_improvements.md) for the prioritized list of architectural improvements to `src/editor/`.

## Text Encoding

**Use only ASCII characters** in all source files (.cpp, .hpp) and documentation files (.md). Never use Unicode dashes, quotes, or other non-ASCII characters. Use `-` or `--` instead of em-dash, `'` instead of curly quotes, etc.

## C++ Coding Style

- **Always use `class`, never `struct`** - this makes forward declarations trivial (always `class Foo;`).
- **Prefer explicit types over `auto`** - spell out the actual type for readability. Reviewers should not need to trace through code to determine types.
- **Use sufficient parentheses** - do not rely on C++ operator precedence. Add parentheses so the intent is unambiguous to readers (e.g., `(a & b) != 0` not `a & b != 0`).
- **Never use lock-free / atomic techniques** without explicitly asking the user for permission first. Prefer simple mutex-based synchronization.
- **Multithreading debugging**: When diagnosing deadlocks or contention, ask the user for callstacks of all threads - not just the stuck thread. The root cause is usually on another thread.

## Shader Interface Block Layout (UBO/SSBO)

When creating or modifying shader interface blocks (uniform blocks or shader storage blocks), always respect std140/std430 alignment rules explicitly:

- `vec4` and `vec3` fields must start at an offset that is a multiple of 16 bytes (vec4 size).
- `vec2` fields must start at an offset that is a multiple of 8 bytes (vec2 size).
- `float`, `int`, `uint` fields must start at an offset that is a multiple of 4 bytes.
- Struct total size must be a multiple of 16 bytes (vec4). Add explicit padding fields to ensure this.
- After a `vec3`, add a `float` padding to reach the next 16-byte boundary.
- When a block has an odd number of `vec2`/`float` pairs, add padding `vec2` or two `float` fields to round the struct size to a multiple of 16 bytes.

Example: a block with `uvec2 texture_handle`, `vec2 uv_min`, `vec2 uv_max` needs a `vec2 padding` appended to make the total size 32 bytes (2 x vec4).

## C++ Naming Conventions

Follow these naming conventions consistently:

| Element | Convention | Examples |
|---------|-----------|----------|
| **Class names** | `Snake_case` (capital first letter, underscores between words) | `Rendergraph_node`, `Buffer_create_info`, `Geometry_operation` |
| **Member variables** | `m_` prefix + `snake_case` | `m_projection`, `m_line_color`, `m_dst_vertex_sources` |
| **Methods/functions** | `snake_case` | `get_exposure()`, `make_edge_midpoints()`, `post_processing()` |
| **Local variables** | `snake_case` | `src_vertex`, `corner_count`, `new_dst_facet` |
| **Enum class names** | `Snake_case` (same as classes) | `Light_type`, `Buffer_target`, `Primitive_type` |
| **Enum values** | `snake_case` | `directional`, `point`, `triangle_strip` |
| **Namespaces** | `lowercase` (no underscores between words) | `erhe::geometry`, `erhe::scene` |
| **Files** | `snake_case.hpp` / `snake_case.cpp` | `geometry_operation.hpp`, `imgui_host.cpp` |
| **Constants** | `snake_case` for `constexpr`; `c_` prefix for `const char*` arrays | `shadow_cast`, `c_type_strings[]` |
| **Template params** | Single uppercase letter or descriptive name | `T`, `Base`, `Self` |
| **Interface classes** | `I` prefix + `Snake_case` | `ICollision_shape`, `IMotion_state` |
| **Getters/setters** | `get_`/`set_` prefix | `get_mesh()`, `set_exposure()` |
| **Boolean getters** | `is_`/`has_` prefix | `is_enabled()`, `has_cursor()` |

## C++ Standard

This project uses **C++20**. Prefer modern C++20 features over older alternatives (e.g. concepts over SFINAE, `std::span` over pointer+size, `std::format`/`fmt` over `sprintf`, `constexpr` where possible, designated initializers, `requires` clauses).
