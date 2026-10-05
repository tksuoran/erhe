# Editor time

Stability: mostly stable

`editor::Time` (`src/editor/time.{hpp,cpp}`) owns the editor's clocks. Once
per frame `Editor::tick()` calls `Time::prepare_update()`, which samples the
wall clock once and derives everything else from that one reading.

## Clocks

| Clock | Read with | Advances | Used for |
|---|---|---|---|
| Wall clock | `steady_clock` directly | Real time | Frame pacing and its sleeps, retry sleeps, MCP transport timeouts and request expiry, asset load budgets and the 30 s asset wait, the frame-time average (`get_frame_time_average_ms()`), profiling timers, log timestamps |
| Editor clock | `get_editor_time_ns()`, `get_editor_frame_duration_ns()` | Once per frame, by the frame's duration (wall clock source) or by a fixed dt (fixed_dt source) | Everything that reasons about user-facing elapsed time: ImGui `io.DeltaTime` (double clicks, key repeat, ImGui animations), input event timestamps, `Commands::tick`, transform animations (`begin_transform_animation`), scene animation playback, the navigation gizmo snap, the selection highlight pulse, thumbnail hover animation, the HUD long press, the knife tool's double click, the theremin pinch hold, the fly camera's synthesized input and its recording graphs |
| Simulation clock | `get_simulation_time_ns()`, `for_each_fixed_step()` | In fixed 1/240 s steps from an accumulator | Physics, fly camera and headset fixed updates |

### Editor clock source

`Editor_clock_source` selects how the editor clock advances:

- `wall_clock` (default): the editor clock is the wall clock sampled at the
  start of the frame, so interactive behavior follows real time.
- `fixed_dt`: every frame advances the editor clock by exactly `dt`, however
  long the frame took. A test or script then means the same on a fast GPU and
  on a software rasterizer at several hundred milliseconds per frame: a
  scripted double click is two presses a few frames apart, so a few times
  `dt` apart, always inside ImGui's 0.30 s double-click time. Started in
  `fixed_dt` mode the clock starts at 0, so time-derived visuals are the same
  on every run.

Selecting it:

- `ERHE_FIXED_DT_MS=<milliseconds>` in the editor's environment, read when
  `Time` is constructed: `fixed_dt` from the first frame. Empty, unset, or not
  a number in (0, 1000]: `wall_clock`. The `mcp_server_tests` editor launcher
  (`src/editor/mcp/test/editor_launcher.cpp`, used by the ctest fixtures),
  `scripts/mesh_modeling_verify.py` and the headless run-books
  (`doc/agents/editor_runs.md`, `doc/agents/linux.md`) set 16.667.
- MCP `advance_time` with `clock` (`wall_clock` | `fixed_dt`) and
  `fixed_dt_ms`, taking effect from the next frame.
- There is no editor setting: the clock source is a property of a run (a test
  harness, a CI job), and a persisted value in `editor_settings.json` would
  silently change the interactive editor on its next start.

The editor clock is monotonic across switches. Switching to `fixed_dt`
continues from the current value. In `wall_clock` mode the editor clock is
the wall clock plus an offset, 0 for a run that never left `wall_clock`;
switching back from `fixed_dt` sets the offset so the switching frame advances
the clock by its wall duration from where `fixed_dt` left it.

### Simulation time

The simulation advances per frame by:

- `wall_clock` source: the wall-clock frame duration, or the predicted display
  delta while frame pacing runs (`simulation_advance_ns`, FR4), capped at
  25 ms (time dilation instead of a catch-up burst).
- `fixed_dt` source: `dt`, without the cap.

On top of either source, `Time_mode` gates the simulation: `paused` and
`manual` produce no fixed steps except for advances queued with
`request_simulation_advance()` (MCP `advance_time seconds`), which replace the
frame's delta at up to `max_step_ms` per frame. A hidden window
(`advance_simulation == false`) also produces no steps. The editor clock
keeps advancing in every case.

## Input event timestamps

Every input producer stamps its events on the wall clock (`steady_clock`
nanoseconds): the window system backends (the SDL backend maps
`SDL_GetTicksNS()` onto `steady_clock`, `doc/erhe/window.md`), MCP injection
(`mcp_server_ui.cpp`) and the fly camera's input synthesizer (whose schedule
runs on the editor clock). `Editor::run()` maps every event onto the editor
clock with `Time::map_input_timestamp_ns()` right before dispatching it - the
one place where the domains meet. In `wall_clock` mode the mapping adds the
wall-to-editor offset (0 unless the run switched from `fixed_dt`); in
`fixed_dt` mode it stamps the current editor frame time (the events
dispatched in a frame arrived during the previous one).

## ImGui delta time

`Editor::tick()` passes the editor frame duration and editor time to
`Imgui_windows::process_events()`; each host sums the durations since its
last ImGui frame into `io.DeltaTime`. Only the very first frame has a zero
duration; the hosts substitute 1/60 s there because Dear ImGui requires a
positive delta.

## MCP

- `get_time`: frame number, editor clock source (`clock`, the current frame's;
  `pending_clock`, the next frame's) and `fixed_dt_ms`, editor time
  and frame advance, simulation mode and time, pending manual advance, wall
  frame-time average.
- `advance_frames {frames}`: answers once that many more frames have run (one
  deferred request stepping once per frame). This is how a test or script
  lets frames pass; `advance_time seconds` queues simulation time instead.
- `advance_time`: simulation `mode`, queued `seconds`, and the editor `clock`
  switch.

A deferred multi-frame request (a gesture, `drag_selection`, `physics_drag`,
`advance_frames`) records a step on every frame it advances; MCP request
expiry (`Mcp_server::k_request_timeout`, 5 s of wall time) is measured from
the last step, so a request that keeps stepping is answered however long it
runs, and expiry detects a main thread that stopped serving the queue. The
HTTP thread and the main thread decide expiry on one mutex-guarded record,
and a pass the main thread has begun cannot expire. An input gesture that does
expire releases the mouse buttons and modifier keys it pressed and left held.

## Verification

`mcp_server_tests` (`doc/testing.md`) covers the clock:
`Mcp_test.advance_frames_answers_after_the_frames_and_fixed_dt_advances_the_clock_by_dt`,
`Mcp_test.editor_clock_is_monotonic_across_clock_source_switches`,
`Mcp_test.gesture_longer_than_the_request_expiry_completes_while_it_steps`
(over 6 s of wall time under the frame pacing window's simulated workload) and
`Mcp_test.imgui_click_double_is_a_double_click_at_slow_frames_with_the_fixed_dt_clock`.

## Future work

- [plans/deterministic_editor_clock.md](../plans/deterministic_editor_clock.md)
