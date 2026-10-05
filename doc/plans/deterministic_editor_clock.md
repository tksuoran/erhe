# Deterministic editor clock for headless, cloud and CI runs

Status: proposed

Extends `doc/testing.md` (headless verification, `mcp_server_tests`) and the
manual simulation clock of `src/editor/time.{hpp,cpp}` (MCP `advance_time`,
`Time_mode` wall_clock / paused / manual, `doc/agents/creations.md`).

## 1. Goal

A headless editor whose behavior does not depend on how fast frames render.
On software Vulkan (lavapipe) under ASan a frame takes about 330 ms; with
the editor's time read from the wall clock, ImGui double clicks are missed,
multi-frame MCP gestures expire, and physics runs dilated. In fake-clock mode
every editor frame advances editor time by a fixed `dt`, independent of the
wall clock, so a test means the same thing on a fast GPU and on lavapipe.

## 2. Facts (2026-10-05)

- `editor::Time::prepare_update()` (`time.cpp:26`, called from
  `editor.cpp:622`) is the one frame-delta clock read. Manual mode already
  replaces the simulation delta with queued `advance_time` seconds
  (`time.cpp:73-78`, up to 250 ms per frame, bypassing the 25 ms cap).
- Manual mode covers physics only. These still read wall time:
  - the animation advance (`editor.cpp:~620`,
    `get_host_system_last_frame_duration_ns()`) and
    `Time::begin_transform_animation` (`time.cpp:~140`);
  - ImGui `io.DeltaTime` (`window_imgui_host.cpp:167`,
    `rendertarget_imgui_host.cpp:469`), summed from wall `host_system_dt_s`
    (`editor.cpp:668-675`), which drives ImGui double-click
    (`MouseDoubleClickTime` 0.30 s) and key repeat;
  - injected input event timestamps (`mcp_server_ui.cpp:109` `now_ns()`,
    used at `:565`), the fly camera's timestamp clamp and recording start
    (`fly_camera_tool.cpp:517,634,1702`), the knife double click
    (`mesh_component_selection_tool.cpp:2647`), theremin hold
    (`theremin.cpp:292-294`).
- Multi-frame MCP tools (click, double click, `mouse_drag`, gesture steps,
  `drag_selection`, `physics_drag`) step per frame, but a deferred request
  expires after `k_request_timeout` = 5 s of wall time measured from
  enqueue (`mcp_server.hpp:602`, `mcp_server.cpp:466-474`), even while it is
  progressing. At 330 ms frames any gesture longer than about 15 frames
  expires, and an expired drag can leave the button held.
- Headless runs free-running: `null_window::poll_events` does not block,
  the emulated swapchain has no vsync, the pacer has no decision.
- Must stay wall clock: frame pacing and retry sleeps, MCP transport
  timeouts and test-client deadlines, asset load budgets and the 30 s asset
  wait, the frame-time average, profiling timers, log timestamps.

## 3. Design

- `editor::Time` gains a third source alongside wall clock and manual:
  `fixed_dt`. In that mode `prepare_update()` advances one monotonic editor
  clock (`get_editor_time_ns()`) by `dt` per frame and reports `dt` as the
  host frame duration; the 25 ms simulation cap does not apply (as in manual
  mode). `paused` and `manual` keep their meaning on top of it: simulation
  time is still gated by them, the editor clock (UI / input time) keeps
  advancing by `dt` per frame.
- Every consumer in the "still read wall time" list of section 2 reads the
  editor clock instead of `steady_clock` / the wall frame duration. In wall
  clock mode the editor clock equals the wall clock, so normal interactive
  behavior is unchanged. Real GLFW input timestamps stay wall time; in
  fixed-dt mode they are mapped onto the editor clock at the event pump so
  one domain reaches `Commands::tick` and the fly camera.
- Switch: `ERHE_FIXED_DT_MS=<ms>` (environment, like `ERHE_AI_DRIVER`) and
  an editor setting; the headless test fixtures (`editor_launcher.cpp`) and
  the headless run-books set it to 16.667. MCP `set_time_mode` (or the
  existing time tool) can switch at run time; `get_time` reports frame
  number, editor time and simulation time.
- Deferred-request expiry: a multi-frame request that advanced a step this
  frame is progressing, so its expiry measures wall time since its last
  step, not since enqueue. The 5 s limit then detects a stalled main thread,
  which is its purpose, instead of limiting gesture length by frame speed.
  This is a correctness fix independent of fake mode and is its own commit.
- `advance_frames {n}`: an MCP tool that answers after n editor frames
  (one deferred request), replacing the `advance_time{0.016}` "step one
  frame" idiom in tests and scripts where frames, not seconds, are meant.

## 4. Steps

| Step | Content | Size |
|---|---|---|
| T1 | Deferred-request expiry measured from the last step; test: a gesture longer than 5 s of wall time completes when every frame steps it | S |
| T2 | `fixed_dt` mode in `editor::Time`, editor clock, env / setting switch, consumers of section 2 moved to the editor clock; ImGui `io.DeltaTime` = `dt`; tests: double click and key repeat recognized at any wall frame time | M |
| T3 | `get_time`, `advance_frames`, time mode over MCP; headless fixtures and run-books set `ERHE_FIXED_DT_MS`; `doc/testing.md`, `doc/agents/editor_runs.md`, `doc/agents/linux.md` | S |
| T4 | Re-run `mcp_server_tests`, `mesh_modeling_verify.py`, `geometry_nodes_smoke_test.py` on the headless editor in the cloud container; record results in `doc/testing.md` | S |

## 5. Risks

- Two timestamp domains meeting (event timestamps vs. `Commands::tick`,
  the fly camera clamp) drop or clamp events; T2 maps them at one place.
- Behavior differs between wall and fixed-dt modes; interactive Windows /
  macOS runs keep exercising wall mode, CI runs fixed-dt.
- A test that measures real elapsed time must say so and read the wall
  clock explicitly.
