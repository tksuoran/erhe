# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**erhe** is a C++ graphics library and editor for Vulkan, OpenGL and Metal (Vulkan is the default backend). It features a render graph system, full 3D scene graph, physics (Jolt), geometry manipulation (Catmull-Clark, Conway operators via Geogram), and an ImGui-based editor application.

## Session handoff: `prompt_queue.txt`

When an untracked `prompt_queue.txt` exists in the repo root, it is a handoff written by an older Claude Code session so that work can continue with fresh context: read it first and continue the work it describes. It may hold a queue of sequential handoffs (do the first item, and only write the next when the current one is done and verified). Once an item has been read and its work is done, remove that item; when the file holds nothing outstanding, delete it - do not keep a stale file around. Notes about the work done must already be in the commit messages for that work, so no information is lost by deleting it. (Writing a new `prompt_queue.txt` is only warranted when handing off still-unfinished work to a future session.)

## `src/rendering_test/` is rotten

`src/rendering_test/` (the standalone `rendering_test` executable, its own duplicated shaders under `res/rendering_test/shaders/`, and its `cell_*.cpp` files) is in a known-bad state and slated for re-implementation. Do not invest effort in keeping it consistent when refactoring shared code (e.g. `erhe::scene_renderer`). Acceptable failure modes when touching shared infrastructure:

- `rendering_test` no longer builds or runs.
- Its duplicated shaders fall out of sync with the editor's.
- Its `Rendering_test` class wiring breaks.

Focus refactors on the `editor` executable and the `erhe::*` libraries. If a change in shared infrastructure would otherwise require parallel updates to `src/rendering_test/`, skip the rendering_test side and leave it broken; it will be replaced wholesale, so coherent partial fixes there are wasted work.

## Building

### Windows (Visual Studio 2026)

Vulkan is the default backend. From an x64 Native Tools Command Prompt:

```bat
scripts\configure_vs2026_vulkan.bat
```

This generates a Visual Studio solution in `build_vs2026_vulkan/`. Open it and build the `editor` target.

Alternative configurations:
- `configure_vs2026_vulkan_asan.bat` - Vulkan with AddressSanitizer
- `configure_vs2026_opengl.bat` - OpenGL backend
- `configure_vs2026_opengl_asan.bat` - OpenGL with AddressSanitizer
- `configure_vs2026_opengl_no_tracy.bat` - OpenGL without Tracy profiler

**Always use the `scripts\` build scripts on Windows.** Do not invoke `cmake --preset` directly -- the wrappers encode the project's intended configure flow (CPM caching, MSVC env init, etc.).

### Windows CLI builds (the day-to-day verification loop)

For command-line build verification (no Visual Studio needed), use the ninja
wrappers -- they locate VS 2026's bundled cmake/ninja and set up the MSVC
environment themselves, so they work from any shell:

```bat
scripts\configure_ninja_win_vulkan.bat        REM once -> build_ninja_win_vulkan/ (MSVC cl)
scripts\build_ninja_win_vulkan.bat editor     REM build a target (this is the usual edit-build loop)
```

`configure_ninja_win_clang.bat` / `build_ninja_win_clang.bat` are the clang-cl
equivalents (their configure also regenerates `compile_commands.json` for
clangd). The **headless Vulkan build** (needed for MCP-driven verification and
`capture_screenshot`, see "In-editor MCP server" below) is a VS solution
configured with `scripts\configure_vs2026_vulkan_headless.bat`; build it from
the CLI with:

```bat
cmake --build build_vs2026_vulkan_headless --target editor --config Debug
```

### clangd diagnostics vs the real build

IDE diagnostics come from clangd via `build_ninja_win_clang/compile_commands.json`.
Newly created source files are not in the compilation database until the next
CMake configure, so clangd reports masses of false "file not found" / "unknown
type" errors for them (and for files that include them). Do not chase these --
the ninja/VS build is the ground truth. Re-run
`scripts\configure_ninja_win_clang.bat` to refresh the database when the noise
gets in the way.

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

### Linux

Vulkan is the default backend:

```bash
scripts/configure_ninja_linux_vulkan.sh
cmake --build build_ninja_linux_vulkan --target editor
```

For the OpenGL backend, use `scripts/configure_ninja_linux_opengl.sh` and build `build_ninja_linux`.

Required packages: `libwayland-dev libxkbcommon-dev xorg-dev` (Ubuntu) or equivalent.

### CMake presets exist but should not be used

`CMakePresets.json` defines presets like `OpenGL_Debug` and `Vulkan_Debug`. Do not invoke `cmake --preset ...` directly. The `scripts/` wrappers (`configure_vs2026_*.bat` on Windows, `configure_xcode_*.sh` on macOS, `configure_ninja_linux_*.sh` on Linux) wrap the configure step with the project's expected environment and should be used instead. Plans, scripts, and docs should not reference `cmake --preset`.

### Key CMake Options

| Option | Default | Notes |
|--------|---------|-------|
| `ERHE_GRAPHICS_API` | `vulkan` | `vulkan`, `opengl`, `metal` (macOS), or `none` (headless) |
| `ERHE_NAVIGATION_LIBRARY` | `none` | `recastnavigation` or `none` |
| `ERHE_PHYSICS_LIBRARY` | `jolt` | `jolt` or `none` |
| `ERHE_RAYTRACE_LIBRARY` | `bvh` | `bvh`, `tinybvh`, `embree`, or `none` (none uses GPU ID-buffer picking) |
| `ERHE_PROFILE_LIBRARY` | `none` | `tracy`, `nvtx`, `superluminal`, or `none`; the configure wrappers pass `tracy` |
| `ERHE_WINDOW_LIBRARY` | `sdl` | `sdl`, `glfw` (deprecated), or `none` (headless) |
| `ERHE_XR_LIBRARY` | `none` | `openxr` or `none`; the configure wrappers pass `openxr` |
| `ERHE_USE_ASAN` | `OFF` | AddressSanitizer |
| `ERHE_USE_PRECOMPILED_HEADERS` | `OFF` | Speeds up builds; the configure wrappers pass `ON` |
| `ERHE_BUILD_TESTS` | `OFF` | gtest unit test targets (see Testing below) |

## Testing

Several `erhe::*` libraries have gtest suites under `src/erhe/<name>/test/`
(circular_ring_buffer, codegen, dataformat, geometry, graphics, item, math,
raytrace), plus `mcp_server_tests` for the editor's MCP server. Each builds an
`erhe_<name>_tests` executable, gated behind `-DERHE_BUILD_TESTS=ON`
(default OFF).

- The macOS `configure_xcode_*.sh` scripts enable tests by default. On Windows,
  `scripts\configure_tests_asan.bat` produces a dedicated test configuration
  (`build_tests_asan/`, OpenGL + ASAN + tests ON); other Windows configure
  scripts leave tests off -- pass `-DERHE_BUILD_TESTS=ON` through the wrapper
  if you want tests in a regular build tree. For performance measurement,
  `scripts\configure_tests.bat` produces `build_tests/` (no ASAN, profiler
  none; VS generator, so `--config Release` and `--config Debug` build from
  the same tree) -- used by the geometry timing harness, see
  `doc/catmull_clark.md`.
- Run via `ctest` from the build directory, or invoke the
  `.../src/erhe/<name>/test/<config>/erhe_<name>_tests.exe` binary directly.
  Run suites serially and fix one failure at a time -- an abort hides the rest
  of the run.
- When adding pure-logic code to an `erhe::*` library that already has a
  `test/` directory (geometry operations, math, item/graph logic, dataformat),
  add or extend a test for it. Editor-level behavior is instead verified live
  through the in-editor MCP server (below).

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

#### Part construction and `App_context` (IMPORTANT)

The editor is composed of "parts" (tools, windows, renderers, scene managers, etc.). `App_context` holds pointers to all parts and shared resources (`graphics_device`, `imgui_renderer`, `rendergraph`, `mesh_memory`, `scene_builder`, `headset_view`, `editor_settings`, ...).

**A part constructor must NEVER access `App_context` members other than to store the `App_context&` itself into a member variable.** The `App_context` pointers are only assigned *after all parts have been constructed*, so reading e.g. `context.graphics_device` from inside a part constructor yields `nullptr` (or a stale value) and will crash. (Exception: a few fields populated earlier in init, such as `current_command_buffer`, but do not rely on this - prefer explicit arguments.)

Each part constructor must receive the other parts and resources it needs as explicit `Part&` / resource-reference constructor arguments, and use those (not `App_context`) during construction. `App_context` may be used freely *after* construction (in per-frame / runtime methods). Helper/owned objects created by a part (e.g. `Quad_view`) must follow the same rule: pass them the needed references explicitly rather than having them read from `App_context` at construction time.

### CMake Conventions

- Follow modern CMake best practices. Do not use deprecated CMake features.
- **Never use `file(GLOB)` or `file(GLOB_RECURSE)` to collect source files.** All source and definition files must be listed explicitly in `CMakeLists.txt`. This is the CMake-recommended practice - globbing does not detect added/removed files without a reconfigure.
- `erhe_target_sources_grouped()` helper macro (in `cmake/functions.cmake`) organizes sources into IDE source groups.
- Each component has its own `CMakeLists.txt`.
- External dependencies are fetched at configure time via CPM (`cmake/CPM.cmake`).
- **Dependency version pins are not sacred.** A `GIT_TAG` / `VERSION` pin in `CMakeLists.txt` exists only to prevent uncontrolled upstream changes from breaking the build while nothing in erhe changes - it is not a statement that the pinned version is required. If the currently pinned version has a bug (build break, runtime defect, spec non-compliance), bumping the dependency to the latest version that fixes it is the preferred fix over carrying a downstream patch or fork. Prefer the newest release that resolves the issue; only carry a downstream change when no released upstream version fixes it yet.
- **Do not use CPM's `PATCHES` keyword.** Applying patches at configure time relies on a host `patch` program, which is not portable (e.g. GitHub Windows CI runners have a broken Strawberry-Perl `patch.exe` 2.5.9 that asserts on valid diffs). When a dependency genuinely needs a downstream change that no upstream release provides, do NOT carry a `.patch` file applied via CPM. Instead, **ask the user to provide a fork repo we control** (e.g. `tksuoran/<dep>`), apply the change in a dedicated branch of that fork, push it, and point `CPMAddPackage(... GITHUB_REPOSITORY tksuoran/<dep> GIT_TAG <commit>)` at the patched commit. This keeps the build free of any host `patch` invocation. Record in a comment why the fork exists and when it can be dropped (i.e. which upstream version would obsolete it). Precedent: `tksuoran/fastgltf` branch `khr_physics_rigid_bodies`.
- **Do not do any development work inside `.cpm_cache/` checkouts.** The CPM source cache is a fetch artifact, not a workspace: commits made there are invisible to the fork's real clone, easily lost to a re-fetch, and leave builds silently depending on unpushed state. When a dependency fork needs changes, work in the dedicated clone the user has prepared (per-machine clone locations are recorded in `memory-bank/local/`; ask the user to prepare a clone when none exists for a dependency), commit there, then prompt the user to arrange branches and perform pushes - never push yourself. Bump the `CPMAddPackage` `GIT_TAG` only after the commit is on GitHub.

### Code Generation

`src/erhe/gl/generate_sources.py` generates the OpenGL wrapper from `gl.xml`. Run this script when updating the GL API bindings.

### `erhe_codegen` (Python C++ Struct Code Generator)

`src/erhe/codegen/` - A Python-based code generator that produces C++ structs with versioned JSON serialization (simdjson), deserialization, rich reflection, and enum support from Python definitions. The implementation is complete.

### Backends / Abstraction Layers

Many systems have swappable backends selected at CMake configure time via `#ifdef ERHE_<SUBSYSTEM>_LIBRARY_<VALUE>` guards. This is especially true for physics, raytrace, window, and XR subsystems.

## Debugging on Windows / MSVC (Visual Studio MCP server)

**Skill:** for any C++ crash / abort / `VERIFY` / `ERHE_FATAL` / thrown exception / hang, invoke the **`erhe-cpp-debugging`** skill -- Windows VS-MCP (this section) plus the complete macOS/Linux lldb run-book. For GPU "renders wrong" bugs use **`erhe-renderdoc-gpu-debug`** instead.

On Windows, prefer the **`visualstudio` MCP server** ([CodingWithCalvin/VS-MCPServer](https://github.com/CodingWithCalvin/VS-MCPServer)) for interactive debugging and build/diagnostic queries over ad-hoc print debugging. It lets Claude Code drive a *live* Visual Studio instance: set breakpoints, launch under the debugger, step, and inspect program state, all without leaving the conversation.

**Prerequisites (one-time, done by the user):**
- Visual Studio 2022 or 2026 with the "MCP Server" extension installed (Extensions > Manage Extensions, search "MCP Server"; requires the .NET 10 SDK).
- The server started inside VS: Tools > MCP Server > Start Server (listens on `http://localhost:5050`; can be set to auto-start in Tools > Options > MCP Server).
- The erhe solution open in that VS instance (e.g. `build_vs2026_vulkan/erhe.slnx`). The tools act on whatever solution is currently loaded -- always call `mcp__visualstudio__solution_info` first to confirm which one.
- Permission: the whole server is allow-listed via `mcp__visualstudio` in `.claude/settings.local.json`, so these tools never prompt (and keep working even when the `auto`-mode safety classifier is temporarily unavailable).

**Tool groups** (all prefixed `mcp__visualstudio__`):
- Solution/project: `solution_info`, `solution_open/close`, `project_list`, `project_info`, `startup_project_get/set`.
- Build: `build_solution`, `build_project`, `build_status` (poll until `State == "Done"`, check `FailedProjects`), `build_cancel`, `clean_solution`.
- Diagnostics: `errors_list` (severity / code / file / line), `output_read`, `output_list_panes`.
- Debugger: `debugger_add_breakpoint` / `list_breakpoints` / `remove_breakpoint`, `debugger_launch` (with debugging) / `launch_without_debugging`, `debugger_status` (`Mode` is `Design` when not running), `debugger_break` / `continue` / `stop`, `debugger_step_into/over/out`, `debugger_get_callstack`, `debugger_get_locals`, `debugger_evaluate`, `debugger_set_variable`.
- Documents/editor/navigation: `document_open/read/write/save`, `editor_find/replace`, `editor_goto_line`, `goto_definition`, `find_references`, `selection_get/set`.

**Typical debug flow:** confirm `solution_info` -> `debugger_add_breakpoint` (file + line) -> `debugger_launch` -> poll `debugger_status` until it breaks -> `debugger_get_callstack` / `debugger_get_locals` / `debugger_evaluate` -> step or `continue` -> `debugger_stop` when done.

**ALWAYS use the Visual Studio MCP server to diagnose editor crashes.** When the editor aborts / throws (e.g. `editor.exe` exits with a non-zero code, `[crash] unhandled exception (code=0x...)`, a `logs/editor_crash_*.dmp` minidump, a `VERIFY` / `ERHE_FATAL`, or any C++ exception `0xe06d7363`), do NOT try to reason it out from `logs/log.txt` and a stripped console backtrace alone, and do NOT just stare at the minidump. Reproduce it **live under the VS debugger** so you get a real callstack plus `debugger_get_locals` / `debugger_evaluate` at the throw site:
  1. `mcp__visualstudio__solution_info` to see which solution is loaded. The crash is almost always in shared `editor` / `erhe::*` code, so it reproduces on **any** build's `editor` target -- prefer the solution already open. To debug the **headless** build specifically (e.g. a crash only seen while driving the in-editor MCP / `capture_screenshot`), `solution_open` `build_vs2026_vulkan_headless/erhe.slnx`; for windowed use `build_vs2026_vulkan/erhe.slnx` (needs a live display) or `build_vs2026_opengl/erhe.slnx`.
  2. If the loaded `editor` binary predates your change, `build_project` the `editor.vcxproj` and poll `build_status` until `State=="Done"` / `FailedProjects==0`.
  3. `debugger_add_breakpoint` at the suspected throw site(s) if you have a hypothesis; otherwise just `debugger_launch` and let the debugger break on the unhandled exception (the editor's crash handler runs at second chance, so the user-stack throw frame is still visible).
  4. Drive the repro (e.g. the in-editor MCP `tools/call` over HTTP, or the input that triggers it), poll `debugger_status` until `Mode != "Design"` stops at the break, then `debugger_get_callstack` + `debugger_get_locals` / `debugger_evaluate` to read the faulting state. `debugger_stop` when done.
The minidump and `logs/log.txt` are a starting hypothesis, not the diagnosis -- the live callstack + locals are.

**Notes / gotchas:**
- `symbol_workspace` does not index this C++ solution's symbols (it is C#-oriented and returns no matches here). For C++ symbol navigation use `goto_definition` / `find_references` from an open document, or the Grep tool over `src/`.
- The startup project is `editor`; `build_status` reports `NoBuildPerformed` until a build is triggered through VS or the MCP build tools.
- This complements -- does not replace -- the log-based workflow below (`logs/log.txt`) and the Quest/Android debugging flows, which run on-device and are out of VS's reach.

## Building / diagnostics on macOS (Xcode MCP server)

**Skill:** invoke **`erhe-macos-xcode`** for building and reading diagnostics on
macOS -- it is the full run-book for the `mcp__xcode__*` tools (always call
`XcodeListWindows` first for the `tabIdentifier`; `BuildProject` + `GetBuildLog`
for structured errors; Apple-docs search). Hard limits to remember even without
the skill: it is NOT a debugger (no breakpoints / launch / attach / callstacks --
use lldb via `erhe-cpp-debugging`), it cannot select schemes or run destinations,
and it cannot launch or drive the editor (use the in-editor MCP server). If no
Xcode window is open, fall back to `scripts/configure_xcode_*.sh` +
`cmake --build`.

## Debugging on macOS (lldb, for Claude Code)

**Skill:** invoke **`erhe-cpp-debugging`** -- it is the complete macOS/Linux lldb
run-book (Python SB API emitting JSON preferred; recipes for crash capture,
all-thread hang snapshots, post-mortem cores; interface ranking). There is no
VS-MCP equivalent on macOS and the Xcode MCP server has zero debugger control.
Two constraints that always apply: the windowed editor needs a live, awake
display (attaching to an already-running editor is unaffected), and building +
launching the editor to verify changes is self-serve -- do not ask first. For
GPU "renders wrong" bugs use `erhe-renderdoc-gpu-debug` instead.

## Runtime logs

The editor and other apps write their spdlog output to `logs/` relative to the working directory (typically the repo root): `logs/log.txt` is the main log, `logs/vulkan.txt` and `logs/openxr.txt` capture backend-specific traces. Redirecting `stdout` of `editor.exe` is not enough -- the file sink writes via spdlog and a redirected stdout will be empty even when the app is running fine. Always `grep` / `findstr` against `logs/log.txt` to verify init-time and runtime behavior. On Android / Quest, the same log lines flow through the `android_sink` and are visible via `adb logcat` (tag `erhe`).

**Always use erhe logging in the editor -- never `printf` / `fprintf` / `std::cout` / `std::cerr`, not even for temporary debug tracing.** Editor code logs through the `log_*` spdlog categories declared in `src/editor/editor_log.hpp` (e.g. `log_startup->info("...", ...)`); pick the closest existing category or add a new one in `editor_log.{hpp,cpp}` (declare `extern`, create it in `initialize_logging()` via `make_logger("editor.<name>")`). Library (`erhe::*`) code uses that library's own `erhe::log::make_logger(...)` category. Only erhe logging reaches `logs/log.txt` and the Android logcat `erhe` tag, honors per-category levels in `config/editor/logging.json`, and carries timestamps -- raw `printf`/`std::cout` bypasses the file sink (see the redirect note above) and is invisible to the standard `grep logs/log.txt` verification flow. This applies to throwaway diagnostics too: add a temporary `log_*->info/trace(...)` line, not a `printf`, and remove it (or keep a concise permanent line) when done.

## Editor runs dirty the ImGui ini file

Any editor run rewrites `config/editor/desktop_window_imgui_host_imgui.ini`
(window layout state) on exit. After a verification run, restore it with
`git checkout -- config/editor/desktop_window_imgui_host_imgui.ini` and never
commit changes to it (or to other erhe_imgui window/ini state files).

## Once the user starts testing, stop headless testing yourself

For any task or bug that has a user-interaction aspect: once you consider the
change complete and the user has started testing it, STOP running the headless
verify loop (build + launch + MCP + screenshot) yourself unless the user
explicitly asks for it. At that stage the useful signal is the user's
verification in a real interactive session, and continuing to headless-test in
parallel only slows the work down (and re-dirties `logs/` and the ini state).
Headless verification is valuable earlier -- while you are still iterating on
your own before handing off -- but it is not a substitute for user
verification, and it routinely cannot exercise the menu- and mouse-driven entry
points that only a live windowed run reaches (precedent: the #258 headless run
docked the MCP-created viewport correctly, but the menu-driven "Create Scene"
viewport was still floating -- the user found that interactively). So: build,
hand off, and let the user drive; re-engage headless testing only when they ask
or when you resume iterating on a fresh change.

## In-editor MCP server (live scene scripting + headless screenshots)

**Skill:** invoke **`erhe-headless-verify`** for the standard verification
loop (build headless -> launch -> wait for the MCP server -> drive tools ->
screenshot -> clean up); the prose below is the full reference.

The `editor` executable embeds its own MCP server (`src/editor/mcp/mcp_server.{hpp,cpp}`), auto-started at launch on `http://127.0.0.1:8080/mcp` (JSON-RPC 2.0 over `POST`; methods `initialize`, `tools/list`, `tools/call`). If `8080` is taken it falls back to the next free port, scanning `[8080, 8100)`; grep `logs/log.txt` (or logcat on Quest) for `MCP server: listening on 127.0.0.1:<port>` to learn the bound port. It runs on a background thread and dispatches each call to the main thread (`process_queued_requests()` once per frame), so it is safe to drive a *running* editor. Auth is **off** unless `~/.claude/erhe_mcp_token` exists (mode 0600); when present, every request needs `Authorization: Bearer <token>`. This server is usually NOT registered as native `mcp__*` tools in a Claude Code session (it only exists while the editor is running), so drive it over plain HTTP, e.g.:

```bash
py -3 scripts/mcp_call.py --list                  # list tool names (optionally: --list <substring>)
py -3 scripts/mcp_call.py get_scene_lights        # call with no arguments
py -3 scripts/mcp_call.py create_shape b64:eyJzaGFwZSI6ICJib3gifQ==   # args as base64 JSON
```

**Always use `scripts/mcp_call.py` to drive it** -- it handles the JSON-RPC
envelope, the bearer token, and (via the `b64:` argument form or `-` for
stdin) sidesteps PowerShell 5.1's quote mangling, which reliably corrupts
inline JSON containing spaces. Raw HTTP works too (`POST` the JSON-RPC body to
`http://127.0.0.1:8080/mcp`) but is only worth it from code.

**Use this to set up / inspect / mutate a scene for any debugging need**, rather than only poking at the UI by hand. Tools fall into:
- **Queries**: `list_scenes`, `get_scene_nodes`, `get_node_details`, `get_scene_cameras`, `get_scene_lights`, `get_scene_materials`, `get_material_details`, `get_scene_textures`, `get_scene_brushes`, `get_selection`, `get_undo_redo_stack`, `get_physics_items`, `get_shadow_fit_debug`, `get_async_status`.
- **Actions**: `create_shape`, `create_node`, `place_brush`, `select_items`, `transform_selection`, `reparent_node`, `edit_material`, `lock_items`/`unlock_items`, `add_tags`/`remove_tags`, `toggle_physics` + the `*_physics_*` family, mesh-component editing (`set_mesh_component_mode`, `select_mesh_components`, `remesh`/`decimate`/`smooth`, ...), `save_scene`, `export_gltf`/`import_gltf`, and `capture_screenshot`.

**Screenshots (`capture_screenshot`, default `logs/mcp_screenshot.png`) only work in the headless Vulkan build** -- `Device::capture_last_frame` reads back the *emulated* swapchain (`ERHE_GRAPHICS_API=vulkan` + `ERHE_WINDOW_LIBRARY=none`; build via `scripts/configure_vs2026_vulkan_headless.bat` -> `build_vs2026_vulkan_headless`). The normal windowed build returns "Frame capture not available" (real WSI swapchain readback is unimplemented). For the windowed build, capture frames with the RenderDoc fork instead ([`doc/renderdoc_fork.md`](doc/renderdoc_fork.md)) -- which is also strictly more diagnostic (save individual textures, pixel-debug shaders).

**Windowed editor needs a live display; switch to headless when it does not start.** The windowed build creates a real WSI swapchain, so it needs a present-capable GPU queue on the window surface. When the **display is powered off / asleep / disconnected** (common when an AI is driving the machine unattended), no present queue exists, `choose_physical_device()` finds no usable GPU, and the editor aborts at startup. The log now says exactly this and recommends the fix (look for "Switch to the HEADLESS build" in `logs/log.txt`); older builds printed a misleading `vkCreateInstance() failed with 0 VK_SUCCESS` -- that message is really `choose_physical_device()` failing, not `vkCreateInstance()`. **When this happens, run the headless build instead** (`build_vs2026_vulkan_headless/src/editor/Debug/editor.exe`, `ERHE_WINDOW_LIBRARY=none`): it is surfaceless (emulated swapchain), needs no display, still runs the full render pipeline + MCP server, and supports `capture_screenshot`. The same RenderDoc/GPU-capture flows do not work against a headless emulated swapchain (no real present), so for GPU captures the windowed build + a live display is still required.

**Reaching the server on Quest / Android.** On device the server binds the same loopback address (`127.0.0.1:8080`), unreachable from the host directly -- forward it over adb (`adb forward tcp:8080 tcp:8080`, re-run after each device reconnect) and then drive it exactly as on desktop. The APK declares the `INTERNET` permission (`android-project/app/src/main/AndroidManifest.xml`): Android gates **all** socket creation -- even loopback -- behind it, so without it `bind()` returns `EACCES` on every port and the fallback loop logs `failed to bind any port in [8080, 8100)` (a blanket denial, not a port clash). Confirm the grant with `adb shell dumpsys package org.libsdl.app.quest | grep INTERNET`. See [`mcp_server_usage.md`](mcp_server_usage.md) for the full recipe.

**Interactive (windowed) debugging needs the user at the controls -- prompt for readiness before launching.** When a bug can only be reproduced by live mouse/keyboard/controller input (e.g. physics right-drag, gizmo manipulation, box-select, anything the headless MCP cannot drive), the standard loop is: add `log_*` instrumentation on the suspected path, build the windowed editor, then have the *user* perform the gesture while the file sink records to `logs/log.txt`. Because the run depends on the user actually being present and ready to interact, **prompt the user to get ready and wait for their go-ahead before launching the windowed editor** -- do not launch it and assume they will notice. After launch, tell them the exact gesture to perform and the log prefix you will grep for, then read `logs/log.txt` once they confirm. (This mirrors the Quest headset-prompt protocol: install/build first, then prompt, then act on an acknowledged signal.)

**Scripted startup scene**: `config/editor/commands.json` is a startup script (`scene.add_cameras` / `add_lights` / `add_room` / `add_platonic_solids` / ... with arg blocks; see `editor.cpp` dispatch + `src/editor/config/definitions/*.py` for arg fields). Adjust it to stand up a reproducible test scene before the editor even finishes init -- this is the preferred knob for "I need scene X to debug Y".

**Augment the API when a capability is missing.** If a debugging task needs an editor action the MCP server does not expose, add a new tool (a `query_*`/`action_*` handler + the `req->tool_name == "..."` dispatch entry + its `tools/list` schema in `refresh_tool_list`) rather than working around it from outside. Growing this surface makes the live editor scriptable for the next investigation; treat it as first-class debugging infrastructure.

## GPU frame debugging (RenderDoc fork MCP server)

**Skill:** invoke the **`erhe-renderdoc-gpu-debug`** skill for any "renders nothing / renders wrong / wrong color / why does X look different from Y / I need to SEE the frame or the exact uniform values feeding a shader" bug. It is the condensed run-book for this section: capture a frame, `save_texture` a render target to PNG and `Read` it to see the frame, and read decoded constant-buffer values (`get_constant_buffers` -> `get_constant_buffer_contents`) at a specific draw. On macOS reproduce the bug on `build_xcode_vulkan` first (RenderDoc cannot capture Metal/OpenGL or the headless emulated swapchain).

For **windowed Vulkan GPU debugging** -- inspecting a captured frame's pipeline state, render targets, texture statistics (min/max/mean, NaN/Inf/zero counts), pixel history, constant-buffer contents, post-VS geometry, and saving textures -- use the **RenderDoc fork MCP server** ([`tksuoran/renderdoc`](https://github.com/tksuoran/renderdoc), branch `mcp-server`, cloned and built locally), an MCP server embedded in `qrenderdoc` that connects to the running editor (which loads the fork's in-app `renderdoc.dll`) and reads back the captured frame programmatically. This is the tool for "renders nothing / renders wrong" bugs that the headless `capture_screenshot` and the in-editor MCP cannot diagnose (they show *what* is wrong, not *why* at the GPU level). It needs a **live display** (real WSI swapchain) -- it does not work against the headless emulated swapchain. The full tested recipe, gotchas, and a worked example (the "black atmosphere sky" bug) are in [`doc/renderdoc_fork.md`](doc/renderdoc_fork.md); the fork's own tool reference is its [`docs/mcp.md`](https://github.com/tksuoran/renderdoc/blob/mcp-server/docs/mcp.md) (the `D:\renderdoc\...` paths in that doc are just this machine's clone/build location of the fork).

**On-demand launch via the stdio proxy.** Claude Code connects every MCP server at session start and the model cannot reconnect one mid-session, so a raw http registration pointing at `qrenderdoc` only works if it was already running at startup. Instead, register the **stdio proxy** `scripts/renderdoc_mcp_proxy.py` in `.mcp.json` (`"renderdoc": { "command": "py", "args": ["-3", "D:/alt/erhe/scripts/renderdoc_mcp_proxy.py"] }`). A stdio server is always spawned at startup, so its tools are always registered; the proxy serves the fork's tool schema from the committed `scripts/renderdoc_tools.json` and **launches `qrenderdoc --mcp-server` lazily on the first tool call** (or eagerly via its `renderdoc_launch` tool), proxying JSON-RPC over POST to `127.0.0.1:7398`. It **leaves `qrenderdoc` running** when Claude Code exits; the `renderdoc_shutdown` tool only tears down an instance the proxy itself launched (an externally started `qrenderdoc` is never touched). Other management tools: `renderdoc_status` (is the backend reachable, did the proxy launch it). This means Claude can launch RenderDoc itself on demand -- no manual pre-start or `/mcp` reconnect needed.

**One-shot setup:** `py -3 scripts/setup_renderdoc_mcp.py` clones+builds the fork, configures erhe, registers the proxy in `.mcp.json`, and generates the schema (re-run with `--skip-build` to just rewire an existing build; `--help` for options). The pieces it wires up:

`scripts/renderdoc_tools.json` is the proxy's baked-in tool schema, **regenerated with `py -3 scripts/capture_renderdoc_tools.py`** (which launches `qrenderdoc` on demand to dump its `tools/list`). Re-run and re-commit it whenever the fork's tool set changes. `.mcp.json` is machine-specific and stays untracked/local; the proxy honors `ERHE_RENDERDOC_QRENDERDOC` / `ERHE_RENDERDOC_MCP_HOST` / `ERHE_RENDERDOC_MCP_PORT` / `ERHE_RENDERDOC_LAUNCH_TIMEOUT` env overrides for the path/port. This complements the Visual Studio MCP server (build + debug-launch the editor) and the in-editor MCP server (set up the scene): the canonical windowed GPU-debug loop drives all three.

## Memory growth diagnostics (Linux)

Two zero-install tools. Both launch the editor in an isolated working directory (copy of `config/` with optional overrides applied, `res/` symlinked, shared spirv cache), so the user's `config/` is never touched and runs are reproducible:

- `python3 scripts/idle_memory_check.py --duration 300` -- launches the editor, samples VmRSS / smaps_rollup / per-process DRM fdinfo GPU counters (GTT/VRAM) once a second, fits Theil-Sen + least-squares slopes, prints per-mapping growth attribution and a leak verdict (exit code 1 = growth). `--set 'file.json:dotted.path=value'` overrides any value in the copied config (works for logger levels in logging.json too); `--build-dir` selects other builds, including the OpenGL one.
- `python3 scripts/vk_alloc_census.py` -- runs the editor under gdb (ptrace_scope=1 forbids attaching, so gdb must be the parent) and counts DRM `GEM_CREATE` ioctls with requested sizes and backtraces, Vulkan loader create/destroy entry points, and glibc brk/anonymous-mmap heap growth, all normalized per `vkQueuePresentKHR` frame. This sees GPU allocations the driver makes internally (command-stream / descriptor upload BOs) that never go through `vkAllocateMemory`/VMA.

History: the 2026-06 idle leak (~850 MiB/min host heap + ~5 GiB/min GTT at uncapped fps) was `Device_impl::get_command_buffer()` calling `vkAllocateCommandBuffers` every frame; `vkResetCommandPool` resets but never frees pool-owned handles, so each frame leaked a `VkCommandBuffer` whose RADV command-stream/upload BOs stayed resident. Fixed by reusing handles across frame-in-flight cycles (`Per_thread_command_pool::vk_command_buffers` / `next_handle_index`). Exit-time stats (VMA dump, LSAN) looked clean because teardown's `vkDestroyCommandPool` freed everything -- steady growth needs over-time attribution, not exit-time leak checks.

## Quest / Android device work

**Skill:** invoke **`erhe-quest-launch`** for ANY Quest (or `mobile`-flavor
Android) build / install / launch / log-capture work -- it is the full
run-book: canonical script invocations, the persistent logcat capture
(`scripts/quest_logcat.sh`, started BEFORE every launch), erhe-only log
filtering, readiness signals, Android SDK/JDK probing, and the mobile
sideload-dialog workaround. `erhe-quest-validation` (make silent GPU hangs
abort loudly) and `erhe-quest-shader-failure` (vkCreateShaderModule /
spirv-val aborts at startup) build on it.

The inviolable launch protocol, even when not using the skill: install the APK
FIRST, then prompt the user to put the headset on and activate the
controllers, and wait for explicit confirmation before EVERY one-off
`adb shell am start`; a confirmation applies only to that one launch. Batch /
soak runs are the exception: one confirmation at the start covers the whole
unattended sweep -- do not prompt between iterations, but stop and re-confirm
if results suggest the headset went off-head. Pure builds and installs need no
prompt. Never blind-sleep waiting for the editor to come up; poll for a
readiness signal (recipes in the skill).

Vulkan validation on Quest: without `VK_LAYER_KHRONOS_validation`, GPU-side
errors are silent hangs. The layer .so is bundled in every APK by default; the
runtime knob is `"vulkan_validation_layers"` in
`config/editor/erhe_graphics.json` (device-level errors then abort via
`ERHE_FATAL`). Leave the knob off for normal runs (per-frame overhead); the
bundled .so alone costs nothing -- see `erhe-quest-validation` for the full
flow including the SPIR-V cache wipe.

## Python

**IMPORTANT: On this Windows machine, always use the `py -3` launcher to run Python scripts. Never use `python` or `python3` - they resolve to the Microsoft Store stub and will fail.** This applies to all Python invocations: scripts, codegen, tools.

**Write repository tooling/scripts in Python, not PowerShell.** New helper scripts (under `scripts/` or elsewhere) must be Python (`py -3`) - do not add `.ps1` PowerShell scripts. This keeps tooling portable and consistent. The only exception is the platform build-configure wrappers that must set up the native toolchain environment: `scripts/configure_*.bat` (Windows) and `scripts/configure_*.sh` (macOS/Linux); do not rewrite those.

Example:

```bash
py -3 src/erhe/codegen/generate.py <definitions_dir> <output_dir>
```

## Shell process hygiene (find.exe)

On this Windows machine `find` resolves to Git Bash's `C:\Program Files\Git\usr\bin\find.exe`. A broad search such as `find / -name <file>` walks the entire Windows filesystem and effectively hangs, leaving an orphaned `find.exe` running in the background. Prefer the dedicated Glob/Grep tools, or scope `find` to a specific directory -- never `/`. Every now and then, and especially right after you have used `find`, verify that no leftover `find.exe` processes remain and kill any that do:

```powershell
Get-CimInstance Win32_Process -Filter "Name='find.exe'" | Select-Object ProcessId, CommandLine | Format-List
Get-CimInstance Win32_Process -Filter "Name='find.exe'" | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
```

## Editor Improvement Plan

See [`doc/editor_improvements.md`](doc/editor_improvements.md) for the prioritized list of architectural improvements to `src/editor/`.

## Machine-Neutral Committed Files

**Never write machine-dependent information into files that are committed / pushed to the repo** - this applies to ALL committed files (sources, docs, CLAUDE.md, memory-bank), not just the Memory Bank (whose README "Machine-Scope Rule" defines the same policy). Forbidden: absolute per-machine paths (`C:\Users\<name>\...`, `C:\git\...`, `D:\...`), usernames, hostnames, per-machine install state. Phrase facts as capabilities or with placeholders (`%USERPROFILE%`, `<your clone>`), and record the actual per-machine values (clone locations, tool install paths) in `memory-bank/local/*.md`, which is gitignored.

## Text Encoding

**Use only ASCII characters** in all source files (.cpp, .hpp) and documentation files (.md). Never use Unicode dashes, quotes, or other non-ASCII characters. Use `-` or `--` instead of em-dash, `'` instead of curly quotes, etc.

## Diagnostic float serialization

When (de)serializing `float` / `double` values for diagnostic purposes (capturing
live values to dump/replay, repro generation, bit-exact comparison across runs or
processes), always use hexadecimal floating-point literals (`%a` / `std::hexfloat`,
parsed back with `%a` / `strtod` / `std::from_chars`). Decimal formatting is lossy
and round-trips are not guaranteed bit-exact, which silently invalidates any
diagnostic that depends on reproducing the exact same bits.

## No Band-Aid Fixes

Every proposed solution must be evaluated with the question: "is this just a band-aid?" If the answer is yes, the solution must be rejected. A band-aid is any change that masks, works around, or tolerates the symptom of a bug without addressing the root cause - for example, a defensive null check that lets shutdown proceed when an object should not have been null in the first place, a try/catch that swallows an unexpected error, or a "tolerant" code path that accommodates state the system was not supposed to enter. Find and fix the actual cause; do not paper over it.

## Never offer "pause" as an option

Never prompt the user with a choice where one of the options is to pause, stop, or wrap up the current investigation/task (e.g. "continue digging, or pause here?"). When the work is mid-flight and the path forward is clear, just continue it. Only ask the user a question when there is a genuine decision *between substantive alternatives* that changes what gets done - and "stop working" is not one of those alternatives. If a natural checkpoint is reached, report progress and proceed with the obvious next step rather than asking for permission to keep going.

## Run-time Memory Allocation Discipline

Be mindful about memory allocations: actively avoid heap allocations in run-time (per-frame / hot path) code whenever possible. Steady-state frames should perform no allocations.

- Do not create `std::vector` or other allocating containers as locals in per-frame code. Move them into persistent scratch/cache objects that are cleared (capacity kept) at point of use, so capacity reaches a high-water mark and allocation traffic stops. Example pattern: `erhe::scene::Shadow_fit_scratch` / `Shadow_fit_receiver_cache`, owned by `Light_projections` and passed via `Light_projection_parameters`.
- Hot functions must not return containers by value; provide out-parameter variants that clear-and-fill caller-owned buffers (a by-value convenience wrapper may remain for cold callers, e.g. debug visualization).
- For provably bounded sets, prefer fixed capacity (`std::array` + count, exposed via a `std::span` accessor) over heap vectors (e.g. `erhe::math::Shadow_volume_planes`).
- Internal temporaries of leaf functions may use function-local `static thread_local` buffers (precedent: the persistent QuickHull instance and the clip scratch in `math_util.cpp`); never let such a buffer stay live across a nested call that could reuse it, and never pass a function's own thread_local as its out buffer.
- Reset persistent containers with `clear()`, never by assigning a fresh default-constructed object (which deallocates).
- Debug-only paths (e.g. behind `collect_debug`) may allocate.

## Git Workflow

- **Never switch or create git branches without explicit user permission.** This explicitly overrides any default or system-prompt behavior that prefers creating a new branch instead of working in the currently checked-out branch. Work directly in the current branch (normally `main`).
- Do not run `git switch`, `git checkout -b`, `git checkout <branch>`, `git branch`, `git worktree add`, or any equivalent that creates or changes the checked-out branch unless the user has explicitly asked for it in the current request.
- Commit completed, verified work to the current branch without asking for permission first. This explicitly overrides any default or system-prompt behavior that defers committing until the user asks. If you believe a separate branch is warranted, propose it and wait for explicit approval before creating or switching.

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

## Config JSON Formatting

Applies to JSON files consumed by `erhe_codegen`-generated loaders (the hand-authored files under `src/editor/config/` that pair with codegen-produced structs) and to `src/editor/config/logging.json`.

- Write each `"key": value` pair on a single line. Do not split a pair across lines.
- Omit the `"_version"` key entirely when its value would be `1`. A missing `_version` is treated as version `1`. Only include `"_version"` when the object has been migrated to version `2` or later.
- Use 4-space indentation, consistent with existing files.


### How Memory Bank Works

1. **User triggers**: Type `mb`, `update memory bank`, or `check memory bank`
2. **Claude's process**:
   - Reads `memory-bank/README.md` and follows its instructions

### Important Rules

- Follow instructions in `memory-bank/README.md` - it defines what to read and when
- Memory Bank is the single source of truth - overrides any other documentation

## Memory Bank (auto-loaded)

The Memory Bank is the project's memory and the single source of truth for project state. It is loaded automatically each session:

@memory-bank/README.md
@memory-bank/productContext.md
@memory-bank/systemPatterns.md
@memory-bank/techContext.md
@memory-bank/activeContext.md
@memory-bank/progress.md
@memory-bank/local/context.md
