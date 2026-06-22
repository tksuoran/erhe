# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**erhe** is a C++ graphics library and editor for Vulkan, OpenGL and Metal (Vulkan is the default backend). It features a render graph system, full 3D scene graph, physics (Jolt), geometry manipulation (Catmull-Clark, Conway operators via Geogram), and an ImGui-based editor application.

## Session handoff: `next_prompt.txt`

When an untracked `next_prompt.txt` exists in the repo root, it is a handoff written by an older Claude Code session so that work can continue with fresh context: read it first and continue the work it describes. Once it has been read and the work is done, simply delete the file - do not update it or keep it around. Notes about the work done must already be in the commit messages for that work, so no information is lost by deleting it. (Writing a new `next_prompt.txt` is only warranted when handing off still-unfinished work to a future session.)

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
| `ERHE_NAVIGATION_LIBRARY` | `recastnavigation` | `recastnavigation` or `none` |
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

### Code Generation

`src/erhe/gl/generate_sources.py` generates the OpenGL wrapper from `gl.xml`. Run this script when updating the GL API bindings.

### `erhe_codegen` (Python C++ Struct Code Generator)

`src/erhe/codegen/` - A Python-based code generator that produces C++ structs with versioned JSON serialization (simdjson), deserialization, rich reflection, and enum support from Python definitions. The implementation is complete.

### Backends / Abstraction Layers

Many systems have swappable backends selected at CMake configure time via `#ifdef ERHE_<SUBSYSTEM>_LIBRARY_<VALUE>` guards. This is especially true for physics, raytrace, window, and XR subsystems.

## Debugging on Windows / MSVC (Visual Studio MCP server)

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

On macOS, prefer the **`xcode` MCP server** (Apple's official Xcode MCP, prefix `mcp__xcode__`) for build, diagnostics, and in-project file navigation over ad-hoc shell `xcodebuild` invocations. It drives a *live* Xcode instance that has the erhe `.xcodeproj` open, so it builds the exact scheme/config the user is working in and returns structured errors. Verified working against `build_xcode_vulkan/erhe.xcodeproj` (the Xcode-generated project is one logical `erhe` tree; C++ sources are reachable, e.g. `erhe/erhe-executables/editor/main.cpp`).

**Always call `mcp__xcode__XcodeListWindows` first** to get the `tabIdentifier` (e.g. `windowtab1`) and confirm which workspace is loaded -- every other tool requires that `tabIdentifier`. If no Xcode window is open, fall back to the `scripts/configure_xcode_*.sh` + `cmake --build` flow.

**What it CAN do (use these):**
- **Build + diagnostics**: `BuildProject` (builds the active scheme, waits for completion), `GetBuildLog` (errors/warnings/remarks, filterable by `severity` / `pattern` regex / `glob`; reports `buildIsRunning` and `buildResult`), `XcodeListNavigatorIssues` (live Issue-Navigator issues incl. package/config problems -- can be huge, filter by `severity`/`glob`/`pattern`), `XcodeRefreshCodeIssuesInFile` (per-file compiler diagnostics).
- **Project navigation / editing within the Xcode project organization** (NOT filesystem paths): `XcodeLS`, `XcodeGlob`, `XcodeGrep` (regex; remember double-escaping in the JSON arg, e.g. `\\d`), `XcodeRead`, `XcodeWrite`, `XcodeUpdate`, `XcodeMV`, `XcodeRM`, `XcodeMakeDir`, `XcodeGetCurrentFile`. For ordinary editing of erhe sources the regular Read/Edit/Write/Grep/Glob tools on real filesystem paths are usually simpler; reach for the `Xcode*` file tools when you specifically need project-organization semantics (e.g. adding a file to the project).
- **Apple docs**: `DocumentationSearch` (semantic search of Apple Developer Documentation; useful for Metal API questions).

**What it CANNOT do (do NOT expect VS-MCP-style live debugging):**
- **No debugger control whatsoever** -- there is no launch-under-debugger, breakpoints, step, continue, attach, callstack, or locals/variable inspection. This is the key difference from the Windows Visual Studio MCP server. To debug an erhe crash on macOS you still need lldb directly (or Xcode's UI), plus `logs/log.txt`.
- **No scheme / run-destination / launch-target selection** -- all build/test/run tools act on whatever scheme is *currently active* in the Xcode UI (here: `editor`). You cannot switch scheme or run target via MCP; ask the user to set it in Xcode.
- **No way to launch and drive the running editor app.** The only "run" tools are `RunAllTests` / `RunSomeTests` (active scheme's XCTest test plan -- erhe's `editor` scheme has 0 XCTest tests, so not useful here), `ExecuteSnippet` (builds & runs a *Swift* snippet and returns its `print` output -- N/A to a C++ project), and `RenderPreview` (SwiftUI preview snapshots -- N/A). To actually run/inspect the editor, use the build scripts + the in-editor MCP server / headless screenshots described below.

**Bottom line:** on macOS the Xcode MCP server is the preferred tool for *building and reading diagnostics* (and Apple-docs lookup), but it is NOT a debugger and cannot launch or drive the editor. For runtime behavior use `logs/log.txt` and the in-editor MCP server; for live debugging use lldb as described next.

## Debugging on macOS (lldb, for Claude Code)

There is no Visual-Studio-MCP equivalent on macOS (the Xcode MCP server has zero debugger control). The debugger is `lldb`, already installed at `/usr/bin/lldb` (ships with Xcode 26 / Command Line Tools - no install or plugin needed). Claude Code drives it through the **Bash tool**. All three Debug builds expose full symbols (`build_xcode_{metal,opengl,vulkan}/src/editor/Debug/editor`), verified down to source lines (`editor::run_editor()` -> `editor.cpp`).

### Which interface to use (most efficient first)

lldb offers several machine interfaces. Ranked by efficiency *for an automated agent*:

1. **Python SB API emitting JSON - PREFERRED, default for almost everything.** lldb embeds a Python interpreter with the full scripting API (`SBDebugger`, `SBTarget`, `SBProcess`, `SBThread`, `SBFrame`, `SBValue`, ...). Write a script that drives the debugger and `print`s a single JSON blob in a schema *you* control, then run it in one batch call and parse stdout. This beats every other option because you fetch exactly the fields you need (minimal tokens), there is no protocol handshake or event loop, and you never scrape human-formatted `bt` / `frame variable` text. Invocation (verified):

   ```bash
   lldb --batch -o "command script import /tmp/probe.py" -o "quit"
   ```
   where `probe.py` defines `def __lldb_init_module(debugger, internal_dict):` and does the work on the passed-in `debugger` (set `debugger.SetAsync(False)` for synchronous control). Delimit the JSON with sentinels (e.g. `print("JSON_BEGIN"+json.dumps(out)+"JSON_END")`) and extract it, since lldb interleaves its own chatter on stdout. (`lldb -P` prints the SB-API module path if you ever want to `import lldb` from a matching python instead.)

2. **lldb-dap (Debug Adapter Protocol) - only when you need a *persistent interactive* session across multiple tool calls.** `xcrun -f lldb-dap` -> `/Applications/Xcode.app/Contents/Developer/usr/bin/lldb-dap`. DAP is a standard JSON-RPC protocol (`launch`, `setBreakpoints`, `stackTrace`, `scopes`, `variables`, `evaluate`, plus async `stopped` events). Run it in the background on a socket (`lldb-dap --connection listen://localhost:PORT`) and send DAP requests across tool calls so the debuggee stays stopped between your inspections. More plumbing than the SB API (Content-Length framing, seq correlation, event vs. response handling) - use it only for genuine step-debugging, not one-shot captures.

3. **lldb's built-in MCP server - generally NOT worth it for Claude Code.** Inside an lldb session, `protocol-server start MCP listen://localhost:PORT` starts an `lldb-mcp` server (the only supported protocol is `MCP`). It exposes exactly one tool, `lldb_command` (run an lldb command, text back), and a resource per debugger instance (`lldb://debugger/N`). The payload is unstructured text and the transport is raw TCP (needs a stdio bridge to register as a `mcp__*` server), so it adds plumbing for no structured-data gain over just running lldb commands via Bash. Skip it unless a GUI MCP client is sharing the session.

4. **Plain CLI text** (`lldb -b -o "..." -o "..."`) - fine for a quick one-line peek; otherwise prefer the SB API so output is structured.

5. **GDB/MI** is a dead end here: Apple does not ship `lldb-mi`.

### Canonical recipes (all via the SB API / Bash)

- **Reproduce-and-capture a crash / VERIFY / FATAL / C++ exception** (the macOS analog of the Windows VS-MCP "launch -> break on unhandled exception -> dump callstack + locals" flow): SB-API script that creates the target, sets breakpoints (or relies on the stop on the fatal signal/throw), `Launch`es, and on stop walks `SBProcess` -> threads -> frames -> `SBValue` variables, emitting JSON. One call, structured result in your schema.
- **Snapshot a hung / deadlocked editor** (get *all* threads' callstacks, per the multithreading-debug guidance): attach by pid, iterate every `SBThread`, dump each frame's function/file/line to JSON, then detach - e.g. `lldb -b -o "attach -p <pid>" -o "thread backtrace all" -o "detach" -o "quit"` for a quick text snapshot, or the SB-API form for JSON.
- **Post-mortem**: `ulimit -c unlimited`, reproduce, then load the core: `lldb <editor> -c /cores/core.<pid>` (or `target create` + `target.LoadCore` in the SB API).

### Constraints

- **The windowed editor needs a live display.** Metal/OpenGL builds have no headless variant (unlike the Vulkan headless build on Windows), so a *windowed* launch under lldb still aborts at startup if the display is off/asleep. Attaching to an editor that is already running is unaffected.
- **Per the user's setup, do not launch/build the editor unprompted** (the user runs the Metal builds). Static/batch SB-API inspection that does not start the process (target/symbol/breakpoint queries, core-dump post-mortems) is fine; *launching* the GUI editor under lldb should be confirmed first. Attaching to an editor the user is already running is the friction-free path.

## Runtime logs

The editor and other apps write their spdlog output to `logs/` relative to the working directory (typically the repo root): `logs/log.txt` is the main log, `logs/vulkan.txt` and `logs/openxr.txt` capture backend-specific traces. Redirecting `stdout` of `editor.exe` is not enough -- the file sink writes via spdlog and a redirected stdout will be empty even when the app is running fine. Always `grep` / `findstr` against `logs/log.txt` to verify init-time and runtime behavior. On Android / Quest, the same log lines flow through the `android_sink` and are visible via `adb logcat` (tag `erhe`).

## In-editor MCP server (live scene scripting + headless screenshots)

The `editor` executable embeds its own MCP server (`src/editor/mcp/mcp_server.{hpp,cpp}`), auto-started at launch on `http://127.0.0.1:8080/mcp` (JSON-RPC 2.0 over `POST`; methods `initialize`, `tools/list`, `tools/call`). It runs on a background thread and dispatches each call to the main thread (`process_queued_requests()` once per frame), so it is safe to drive a *running* editor. Auth is **off** unless `~/.claude/erhe_mcp_token` exists (mode 0600); when present, every request needs `Authorization: Bearer <token>`. This server is usually NOT registered as native `mcp__*` tools in a Claude Code session (it only exists while the editor is running), so drive it over plain HTTP, e.g.:

```powershell
$body = '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_scene_lights","arguments":{}}}'
Invoke-RestMethod http://127.0.0.1:8080/mcp -Method POST -Body $body -ContentType application/json
```

**Use this to set up / inspect / mutate a scene for any debugging need**, rather than only poking at the UI by hand. Tools fall into:
- **Queries**: `list_scenes`, `get_scene_nodes`, `get_node_details`, `get_scene_cameras`, `get_scene_lights`, `get_scene_materials`, `get_material_details`, `get_scene_textures`, `get_scene_brushes`, `get_selection`, `get_undo_redo_stack`, `get_physics_items`, `get_shadow_fit_debug`, `get_async_status`.
- **Actions**: `create_shape`, `create_node`, `place_brush`, `select_items`, `transform_selection`, `reparent_node`, `edit_material`, `lock_items`/`unlock_items`, `add_tags`/`remove_tags`, `toggle_physics` + the `*_physics_*` family, mesh-component editing (`set_mesh_component_mode`, `select_mesh_components`, `remesh`/`decimate`/`smooth`, ...), `save_scene`, `export_gltf`/`import_gltf`, and `capture_screenshot`.

**Screenshots (`capture_screenshot`, default `logs/mcp_screenshot.png`) only work in the headless Vulkan build** -- `Device::capture_last_frame` reads back the *emulated* swapchain (`ERHE_GRAPHICS_API=vulkan` + `ERHE_WINDOW_LIBRARY=none`; build via `scripts/configure_vs2026_vulkan_headless.bat` -> `build_vs2026_vulkan_headless`). The normal windowed build returns "Frame capture not available" (real WSI swapchain readback is unimplemented). For the windowed build, capture frames with the RenderDoc fork instead ([`doc/renderdoc_fork.md`](doc/renderdoc_fork.md)) -- which is also strictly more diagnostic (save individual textures, pixel-debug shaders).

**Windowed editor needs a live display; switch to headless when it does not start.** The windowed build creates a real WSI swapchain, so it needs a present-capable GPU queue on the window surface. When the **display is powered off / asleep / disconnected** (common when an AI is driving the machine unattended), no present queue exists, `choose_physical_device()` finds no usable GPU, and the editor aborts at startup. The log now says exactly this and recommends the fix (look for "Switch to the HEADLESS build" in `logs/log.txt`); older builds printed a misleading `vkCreateInstance() failed with 0 VK_SUCCESS` -- that message is really `choose_physical_device()` failing, not `vkCreateInstance()`. **When this happens, run the headless build instead** (`build_vs2026_vulkan_headless/src/editor/Debug/editor.exe`, `ERHE_WINDOW_LIBRARY=none`): it is surfaceless (emulated swapchain), needs no display, still runs the full render pipeline + MCP server, and supports `capture_screenshot`. The same RenderDoc/GPU-capture flows do not work against a headless emulated swapchain (no real present), so for GPU captures the windowed build + a live display is still required.

**Scripted startup scene**: `config/editor/commands.json` is a startup script (`scene.add_cameras` / `add_lights` / `add_room` / `add_platonic_solids` / ... with arg blocks; see `editor.cpp` dispatch + `src/editor/config/definitions/*.py` for arg fields). Adjust it to stand up a reproducible test scene before the editor even finishes init -- this is the preferred knob for "I need scene X to debug Y".

**Augment the API when a capability is missing.** If a debugging task needs an editor action the MCP server does not expose, add a new tool (a `query_*`/`action_*` handler + the `req->tool_name == "..."` dispatch entry + its `tools/list` schema in `refresh_tool_list`) rather than working around it from outside. Growing this surface makes the live editor scriptable for the next investigation; treat it as first-class debugging infrastructure.

## GPU frame debugging (RenderDoc fork MCP server)

For **windowed Vulkan GPU debugging** -- inspecting a captured frame's pipeline state, render targets, texture statistics (min/max/mean, NaN/Inf/zero counts), pixel history, constant-buffer contents, post-VS geometry, and saving textures -- use the **RenderDoc fork MCP server** ([`tksuoran/renderdoc`](https://github.com/tksuoran/renderdoc), branch `mcp-server`, cloned and built locally), an MCP server embedded in `qrenderdoc` that connects to the running editor (which loads the fork's in-app `renderdoc.dll`) and reads back the captured frame programmatically. This is the tool for "renders nothing / renders wrong" bugs that the headless `capture_screenshot` and the in-editor MCP cannot diagnose (they show *what* is wrong, not *why* at the GPU level). It needs a **live display** (real WSI swapchain) -- it does not work against the headless emulated swapchain. The full tested recipe, gotchas, and a worked example (the "black atmosphere sky" bug) are in [`doc/renderdoc_fork.md`](doc/renderdoc_fork.md); the fork's own tool reference is its [`docs/mcp.md`](https://github.com/tksuoran/renderdoc/blob/mcp-server/docs/mcp.md) (the `D:\renderdoc\...` paths in that doc are just this machine's clone/build location of the fork).

**On-demand launch via the stdio proxy.** Claude Code connects every MCP server at session start and the model cannot reconnect one mid-session, so a raw http registration pointing at `qrenderdoc` only works if it was already running at startup. Instead, register the **stdio proxy** `scripts/renderdoc_mcp_proxy.py` in `.mcp.json` (`"renderdoc": { "command": "py", "args": ["-3", "D:/alt/erhe/scripts/renderdoc_mcp_proxy.py"] }`). A stdio server is always spawned at startup, so its tools are always registered; the proxy serves the fork's tool schema from the committed `scripts/renderdoc_tools.json` and **launches `qrenderdoc --mcp-server` lazily on the first tool call** (or eagerly via its `renderdoc_launch` tool), proxying JSON-RPC over POST to `127.0.0.1:7398`. It **leaves `qrenderdoc` running** when Claude Code exits; the `renderdoc_shutdown` tool only tears down an instance the proxy itself launched (an externally started `qrenderdoc` is never touched). Other management tools: `renderdoc_status` (is the backend reachable, did the proxy launch it). This means Claude can launch RenderDoc itself on demand -- no manual pre-start or `/mcp` reconnect needed.

`scripts/renderdoc_tools.json` is the proxy's baked-in tool schema, **regenerated with `py -3 scripts/capture_renderdoc_tools.py`** (which launches `qrenderdoc` on demand to dump its `tools/list`). Re-run and re-commit it whenever the fork's tool set changes. `.mcp.json` is machine-specific and stays untracked/local; the proxy honors `ERHE_RENDERDOC_QRENDERDOC` / `ERHE_RENDERDOC_MCP_HOST` / `ERHE_RENDERDOC_MCP_PORT` / `ERHE_RENDERDOC_LAUNCH_TIMEOUT` env overrides for the path/port. This complements the Visual Studio MCP server (build + debug-launch the editor) and the in-editor MCP server (set up the scene): the canonical windowed GPU-debug loop drives all three.

## Memory growth diagnostics (Linux)

Two zero-install tools. Both launch the editor in an isolated working directory (copy of `config/` with optional overrides applied, `res/` symlinked, shared spirv cache), so the user's `config/` is never touched and runs are reproducible:

- `python3 scripts/idle_memory_check.py --duration 300` -- launches the editor, samples VmRSS / smaps_rollup / per-process DRM fdinfo GPU counters (GTT/VRAM) once a second, fits Theil-Sen + least-squares slopes, prints per-mapping growth attribution and a leak verdict (exit code 1 = growth). `--set 'file.json:dotted.path=value'` overrides any value in the copied config (works for logger levels in logging.json too); `--build-dir` selects other builds, including the OpenGL one.
- `python3 scripts/vk_alloc_census.py` -- runs the editor under gdb (ptrace_scope=1 forbids attaching, so gdb must be the parent) and counts DRM `GEM_CREATE` ioctls with requested sizes and backtraces, Vulkan loader create/destroy entry points, and glibc brk/anonymous-mmap heap growth, all normalized per `vkQueuePresentKHR` frame. This sees GPU allocations the driver makes internally (command-stream / descriptor upload BOs) that never go through `vkAllocateMemory`/VMA.

History: the 2026-06 idle leak (~850 MiB/min host heap + ~5 GiB/min GTT at uncapped fps) was `Device_impl::get_command_buffer()` calling `vkAllocateCommandBuffers` every frame; `vkResetCommandPool` resets but never frees pool-owned handles, so each frame leaked a `VkCommandBuffer` whose RADV command-stream/upload BOs stayed resident. Fixed by reusing handles across frame-in-flight cycles (`Per_thread_command_pool::vk_command_buffers` / `next_handle_index`). Exit-time stats (VMA dump, LSAN) looked clean because teardown's `vkDestroyCommandPool` freed everything -- steady growth needs over-time attribution, not exit-time leak checks.

## Quest device launches

**For an individual / one-off launch, ALWAYS prompt the user to put the headset on and activate the controllers immediately before that launch.** Never assume the user is still wearing the headset for a one-off launch - the user may remove it after any single test, and an earlier "I'm ready / proceed" confirmation applies ONLY to that one launch, never to a later one. Each one-off `adb shell am start` (or `... quest run`) must be preceded by its own fresh prompt and explicit confirmation.

**Batch / soak runs are the exception to the per-launch prompt:** a single headset-readiness confirmation at the START of the batch is sufficient for the whole sweep - do NOT prompt between iterations. The user keeps the headset on for the duration while an automated loop (clean reinstall + launch + watch per iteration) runs unattended. If a batch detects that the headset has gone off-head mid-run (e.g. repeated INCONCLUSIVE results / no ticks), stop and re-confirm rather than silently burning iterations.

Do the install (`scripts\install_android.bat quest`) FIRST, while the user can keep their hands free. **Only after the APK is on the device**, prompt the user to put the headset on and pick up / activate the Touch controllers, and wait for explicit confirmation before running the launch (`adb shell am start -n org.libsdl.app.quest/...` or `scripts\install_android.bat quest run`). Quest's `RequiresControllersLaunchInterceptor` shows a system "Controllers Required" dialog that blocks the immersive app from coming to the foreground until controllers are detected as in-hand; launching while the headset is off the user's head wastes the attempt and we have to retry. Pure builds and installs (no app start) do not need the prompt.

### Capturing logcat to a file on every Quest launch

Always start `scripts/quest_logcat.sh` BEFORE the `adb shell am start ...` line. The in-memory logcat ring rolls over within seconds once the editor enters a per-frame error loop (e.g. the `XR_FRAME_DISCARDED` storm), so the only way to keep the full startup trace is to stream it to disk while it happens.

```bash
bash scripts/quest_logcat.sh             # prints the new capture path
"$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe" shell am start -n org.libsdl.app.quest/org.libsdl.app.ErheActivity
```

The script enforces these invariants -- understand them before changing it:

- **One extra process, ever.** The single backgrounded `adb logcat` is the only process owned by this workflow. `scripts/quest_logcat.sh` re-running kills the previous streamer before starting a new one; `scripts/quest_logcat.sh stop` kills it without starting a replacement. The shared `adb` server daemon is not "ours" -- it persists across sessions regardless.
- **No leaks across Claude Code sessions.** The PID is tracked in `logs/.logcat.pid` (a real file in the repo, gitignored), so even a fresh shell finds and stops the prior capture.
- **No PID-recycle hazard.** Before killing, the script confirms the recorded PID still points at an `adb` process via `ps -p PID`; if the PID was recycled to something unrelated, it leaves the new owner alone and just removes the stale PID file. Git Bash's `ps` reports the executable path only (no argv), so we match on `adb` -- this is fine because every other adb call in this workflow is short-lived.

Capture files land in `logs/quest_<YYYYMMDD>_<HHMMSS>.log` (gitignored). To stop a capture explicitly (e.g. when wrapping up a session and you do not plan to launch again): `bash scripts/quest_logcat.sh stop`. If you forget, the next launch's start call cleans it up automatically.

To grep the running capture: `tail -F logs/quest_<...>.log` or `grep <pattern> logs/quest_<...>.log`. The log file is appended to live by the streamer, so `tail -F` shows new entries as they land.

### Filtering a capture down to erhe-only lines

Raw logcat captures interleave hundreds of system tags (`MRSS`, `[CT]`, `wlan`, ...) with the editor's own output. A typical 30 s capture is 30k+ lines, which is too much to scan. Filter to just the editor's `erhe` spdlog tag:

```bash
bash scripts/quest_logcat.sh filter logs/quest_<YYYYMMDD>_<HHMMSS>.log
# writes logs/quest_<YYYYMMDD>_<HHMMSS>_erhe.log
```

The filter is a single grep on ` erhe +:` (space, literal tag, optional padding, colon) anchored to logcat's threadtime tag column, so it does not match the substring "erhe" inside other tags or message bodies. Always work from the filtered file for analysis; reach for the unfiltered capture only when you need to correlate against OS-level events (e.g. `OpenXR`, `VrApi`, `MRSS`, `[CT]` tags from the Horizon runtime, or `WLAN`/`battery` for thermal-throttle context).

### Knowing when the editor is ready (NEVER blind-sleep before capturing)

The editor takes ~15-20 s to initialize on Quest and the exact time varies per run. `sleep N` before looking once wastes runs: too short misses the window, too long and the user may have removed the headset or exited. ALWAYS poll/stream for a definite readiness signal and act the instant it appears.

Pick the signal that matches the state you need:

- **XR session is up / projection depth is being submitted**: poll the live disk capture (from `scripts/quest_logcat.sh`) for a stable session line, e.g. `OpenXR submitted projection depth` or `OpenXR multiview depth swapchain`. Use the disk file, not the in-memory ring, so the line is not evicted before you grep it.
- **Editor is actively rendering AND a specific compositor layer is present** (e.g. the HUD quad is open): enable the compositor layer dump and stream it through a watcher that exits the instant the editor's own block contains the layer you need. This is the robust trigger for any compositor-layer / depth-submission diagnostic, and the captured block itself carries the data (per-layer `Depth SwapChain` valid vs `BAD`, and `Depth Info min/max/near/far`):

```bash
ADB="$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe"
"$ADB" shell setprop debug.oculus.logLayers 1   # enable per-frame layer dump (CompositorClient tag)
# Stream the dump; exit the moment OUR app's block contains a QUAD (HUD open).
# Replace "Type QUAD" with the condition you need; raise timeout for slow init.
timeout 150 "$ADB" logcat -s CompositorClient | awk '
  /LogLayers: Client/ {
    if (cap && hit) { printf "%s", buf; found=1; exit }
    cap = (index($0, "org.libsdl.app.quest") > 0); buf=""; hit=0
  }
  cap { buf = buf $0 "\n"; if (index($0,"Type QUAD")>0) hit=1 }
  END { if (!found) print "[watcher] target block not seen (editor not rendering / HUD not open / headset off-head)" }
'
"$ADB" shell setprop debug.oculus.logLayers 0   # ALWAYS turn the flood back off when done
```

Two gotchas, both learned the hard way:

- **`logLayers` floods logcat** at compositor framerate, so the editor's `erhe`-tagged startup lines are evicted from the in-memory ring within seconds. While `logLayers` is on, do NOT use `adb logcat -d -s erhe` to detect readiness -- use the streaming watcher above (event-driven) or the persistent disk capture.
- **An off-head headset pauses the OpenXR session**: the app stops submitting and only `com.oculus.vrshell` (`FocusPlaceholderActivity`) appears in the dump / as the resumed activity. So "only vrshell in the dump" means "headset off-head or app backgrounded," NOT "editor broken." The editor being `topResumedActivity` is necessary but not sufficient -- it must also be submitting frames (its block present in the dump).

### Locating Android tools in shell commands

The scripts in `scripts\` (`install_android.bat`, `run_android.bat`, `build_android.bat`) all probe for the Android SDK and JDK in the same way; mirror this when running adb / gradle / java directly from a shell:

- **`ANDROID_HOME`**: use `$ANDROID_HOME` if set, else fall back to `%LOCALAPPDATA%\Android\Sdk\` on Windows. Adb lives at `<ANDROID_HOME>\platform-tools\adb.exe`. In bash on Windows: `"$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe"`.
- **`JAVA_HOME`** (only needed for direct gradle / gradlew calls; the scripts above set it for you): use `$JAVA_HOME` if set, else fall back to `C:\Program Files\Android\Android Studio\jbr` (Android Studio's bundled JDK 21). Gradle 8.12 + AGP do not support JDK 25.

Prefer the wrapper scripts when possible -- they handle both probes plus device-state checks. Drop to direct invocations only when a script does not cover the case (e.g. passing a `-P` gradle property the script's bat-arg parser mangles; see "Vulkan validation layer on Quest / Android" below).

### Disabling the sideload security-check dialog on mobile (non-Quest) Android

When running on a regular Android phone via the `mobile` flavor (e.g. soaking the editor with `scripts/soak_quest.py --flavor mobile`, or any sideloaded APK), Play Protect / Samsung Auto Blocker pops a "Send app for a security check?" dialog on each `adb install`. It blocks the install until dismissed, which stalls and can crash an unattended clean-reinstall soak. **Always disable it before a mobile Android run**, and restore it afterward (these are global settings):

```bash
ADB="$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe"
# disable for the soak:
"$ADB" -s <serial> shell settings put global verifier_verify_adb_installs 0
"$ADB" -s <serial> shell settings put global upload_apk_enable 0
"$ADB" -s <serial> shell settings put global package_verifier_user_consent -1
# restore afterward:
"$ADB" -s <serial> shell settings put global verifier_verify_adb_installs 1
"$ADB" -s <serial> shell settings put global upload_apk_enable 1
"$ADB" -s <serial> shell settings put global package_verifier_user_consent 1
```

If the dialog still appears, it is Samsung Auto Blocker rather than Play Protect: toggle it off in Settings > Security and privacy > Auto Blocker. (The Quest never shows this dialog, but the `mobile` editor cannot run on the Quest anyway -- the Quest sleeps flat 2D activities and the editor then SIGSEGVs at Vulkan surface creation; soak the `mobile` editor on a phone instead. Alternatively, `scripts/soak_quest.py batch --no-reinstall` relaunches the already-installed app each iteration, dodging the install dialog entirely.)

### Vulkan validation layer on Quest / Android

Without validation layers, GPU-side errors (descriptor set mismatches, image-layout violations, multiview misuse) are silent on Quest -- the editor hangs on the first bad frame with no log line, and abort hooks never fire.

The Khronos `VK_LAYER_KHRONOS_validation` `.so` is bundled into every Quest / Android APK by default. The Gradle task `fetchVulkanValidationLayer` downloads the .so for `arm64-v8a` from the Vulkan SDK release matching `validationLayerVersion` in `android-project/app/build.gradle` and stages it under `android-project/app/libs/arm64-v8a/`; from there Gradle's normal `jniLibs` packaging puts it in the APK. The download is cached after the first build. To drop the .so (saves ~10 MB but loses the diagnostic capability), pass `-Pvulkan_validation_skip` to gradle / `build_android.bat`.

To actually USE the layer at runtime:

1. Set `"vulkan_validation_layers": true` in `config/editor/erhe_graphics.json`. The C++ device init enables `VK_LAYER_KHRONOS_validation` only when both this config knob is on AND the layer is loadable from the APK; bundling the .so alone does NOT slow normal runs.
2. Validation errors flow through `Device::device_message` -> the editor's callback (in `editor.cpp` line ~698) which calls `ERHE_FATAL` on `Message_severity::error`, so device-level Vulkan errors become loud aborts.

When the layer is enabled there is noticeable per-frame overhead -- leave the config knob off for normal runs and flip it on when chasing a hang or visual corruption.

## Python

**IMPORTANT: On this Windows machine, always use the `py -3` launcher to run Python scripts. Never use `python` or `python3` - they resolve to the Microsoft Store stub and will fail.** This applies to all Python invocations: scripts, codegen, tools.

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
