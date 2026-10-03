# Parallel editor initialization

Status: proposed

Extends `doc/editor/editor.md` "Initialization Order". Editor part
construction runs serially today: 25 `ERHE_INIT_STEP_BEGIN("<name>")` /
`ERHE_INIT_STEP_END` blocks in `Editor::Editor()` (`src/editor/editor.cpp`),
each a profile scope and a line on the init status display. The goal is to
construct independent parts concurrently again, as a Taskflow graph with
explicit dependency edges, so startup time is bounded by the critical path
of the graph instead of the sum of the steps.

## 1. Measure first

Record the per-step wall time of a serial startup with
`scripts/tracy_startup_profile.py` (each step is already an
`ERHE_PROFILE_SCOPE` named after it) on Vulkan and OpenGL. Parallelize only
the steps whose cost is large enough to matter; the rest stay serial on the
main thread before or after the parallel section. The measured table goes in
this plan.

## 2. The dependency graph

The steps below carry the dependency edges the former taskflow annotations
declared (`.succeed(...)`). Those annotations were never enforced, so each
edge is a starting point to verify against the code, not a proven fact: an
edge is needed when the later step dereferences a part the earlier step
creates. "GPU" marks steps that create graphics objects and, on OpenGL, need
a current GL context.

| Step | GPU | After |
|---|---|---|
| Programs (load) | yes | |
| Imgui_renderer | yes | |
| Debug_renderer | yes | |
| Thumbnails | yes | |
| Rendergraph | yes | |
| Forward_renderer | yes | |
| Shadow_renderer | yes | |
| Texel_renderer | yes | |
| Content_wide_line_renderer | yes | |
| Imgui_windows | | Imgui_renderer, Rendergraph |
| Icon_set | yes | Imgui_renderer |
| Post_processing | yes | |
| Id_renderer | yes | |
| App_rendering | yes | |
| Some windows | | Imgui_renderer, Imgui_windows |
| Tools | | Imgui_renderer, Imgui_windows, App_rendering |
| Default content library | yes | Imgui_renderer, Imgui_windows |
| Scene_builder | yes | Default content library, Imgui_renderer, Imgui_windows, Rendergraph, App_rendering, Post_processing, Tools |
| Headset (init) | yes | Imgui_renderer, Imgui_windows, Rendergraph, App_rendering |
| Headset (attach) | | Default content library, Headset (init), Scene_builder |
| Transform tools | yes | Imgui_renderer, Imgui_windows, Headset (init), Icon_set, Tools |
| Group 1 | yes | Imgui_renderer, Imgui_windows, App_rendering, Rendergraph, Forward_renderer, Tools, Scene_builder, Headset (init) |
| Material_preview | yes | |
| Brush_tool | yes | Headset (init), Icon_set, Tools |
| Group 2 | yes | Imgui_renderer, Imgui_windows, Icon_set, Tools, Headset (init) |

"Programs (load)" precedes every renderer that looks up a program; the table
does not list that edge per row.

## 3. Thread-safe construction

Each piece of shared state a part constructor touches is made safe for
concurrent constructors, with a mutex (AGENTS.md: no lock-free techniques
without the user's approval):

- GPU object creation on OpenGL: each GPU step takes a worker context through
  the `Gl_context_provider` API (`doc/erhe/gl_worker_thread_contexts.md`) for
  its duration. Vulkan and Metal device-level creation calls are already
  callable from any thread; the erhe-side caches they fill (pipeline cache,
  shader compilation, descriptor / bind group layout caches, the texture
  heap, `Mesh_memory` allocators) are audited and locked where they are not.
- `Editor_settings_store`: registration already takes `m_callbacks_mutex`;
  `touch()` and `m_dirty` move under the same mutex if any constructor calls
  `touch()`.
- `App_message_bus` subscriptions, `erhe::commands::Commands` registration,
  `Tools` registration, ImGui window registration (`Imgui_windows`) and
  `App_context` member assignment are serialized with a mutex, or moved to a
  serial registration pass after the parallel section.
- The ImGui context and the init status display stay main-thread only: the
  main thread waits on the graph while pumping
  `Init_status_display` (`doc/plans/init_status_display.md`).

## 4. Build the graph

Replace the init step macros of the parallel section with Taskflow tasks
(`m_executor` already exists and is used at runtime), one per step, named
after the step, with the verified edges of section 2. Keep the profile scope
and the status line per task. A build option or command-line switch selects
serial execution of the same graph for debugging and for comparison.

## 5. Verification

- Startup reaches `Main loop: completed frame 12` on Vulkan, OpenGL and
  Metal, windowed and headless, repeatedly (startup races are intermittent:
  at least 20 launches per backend).
- A ThreadSanitizer build of the editor starts cleanly (the Clang / GCC
  sanitizer flags are commented out in `cmake/*.cmake` today; wiring an
  option for them is part of this work).
- `mcp_server_tests` and the deviceless ctest set pass.
- Startup time is measured against the serial baseline of section 1 and
  recorded in `memory-bank/local/` (machine-specific numbers).
