# Quest 3 performance

Status: in progress

The next focus after the culling plan ([culling.md](culling.md), phases 1
to 6 done): the editor's frame time on a Meta Quest 3. This document holds
the task's state; `prompt_queue.txt` only points here.

## Starting points

- [../reference/quest_profiling_2026_05_01.md](../reference/quest_profiling_2026_05_01.md):
  the last Quest 3 GPU profile (`ovrgpuprofiler` real-time metrics, the
  reproduction commands in its "Reproducing" section) and its ranked list of
  investigations: mips not sampled, nearest-vs-linear filtering, FFR level,
  texture compression, fp16 rollout. Re-measure first: the renderer has
  changed since (dedicated render thread and frame pipelining, draw-list
  culling, shared shadow records, GPU classification and command generation).
- [../agents/quest.md](../agents/quest.md) (launch protocol, validation,
  MCP over adb, logcat capture) and the `erhe-quest-launch` skill; the
  headset-prompt protocol is inviolable: install first, ask the user to put
  the headset on, launch only on acknowledged readiness.
- [../agents/quest_renderdoc_capture.md](../agents/quest_renderdoc_capture.md)
  for GPU state behind a frame.
- [../editor/render_thread.md](../editor/render_thread.md) section 3.3: the
  phase-timing log (M1 / M2 / R terms every 60 frames) is the CPU-side
  measurement that needs no profiler on the device; `logs/log.txt` reaches
  logcat under the `erhe` tag.
- [xr.md](xr.md) (multiview, prewarm, controller models) and
  [android.md](android.md).

## What the culling work means on Quest

- The headset renders multiview on Vulkan, and multiview passes draw
  unculled ([culling.md](culling.md) "Open"); per-eye passes are also
  unculled until the eye views are located in the pre-draw hook. Culling the
  multiview color pass needs one view whose planes are the union of the two
  eye frusta (a conservative keep), added in `Headset_view`'s
  `pre_execute_rendergraph_node`; that is the first culling item for Quest.
- Shadow passes are culled and, on the GPU path, their commands are
  generated on the GPU. The GPU path needs native multi-draw indirect,
  `drawIndirectFirstInstance` and `VK_KHR_draw_indirect_count`
  (`Device_info::use_draw_indirect_count`); check the device's startup log
  lines "Record indexing" and the classifier capability message before
  assuming either path. The `culling_classification` setting defaults to
  cpu.
- `Shadow_renderer::fit` is the largest render-phase CPU item on the desktop
  after culling (Release heavy about 1 ms; Debug 27 ms); on the Quest CPU it
  will weigh more. The phase-timing log shows it inside R.

## Plan

1. Baseline on the device: the phase-timing log terms and `ovrgpuprofiler`
   metrics on the default scene and on a heavier scene the headset can load,
   before any change, with the commit id in the log's first line. Record the
   numbers in `memory-bank/local/quest.md` (machine-specific) and the method
   here.
2. Rank the CPU side (R phase composition: fit, shadow draws, color draws,
   ImGui, the XR waits) against the GPU side (the 2026-05-01 report's items,
   re-measured), and pick the first item by measured share.
3. Work the items as separate verified commits, each with a before / after
   measurement on the device and a desktop headless image gate where the
   change touches rendering.

## Measurement rule

One editor at a time, nothing building on the host while a timed run is
taken on the device; the desktop culling numbers were taken that way
(`memory-bank/local/culling_perf_2026_10_17.md`) and Quest numbers follow
the same rule.

## Baseline 2026-10-24 (default OpenXR scene, release APK, 42c05810b)

Phase timing at 72 Hz, averaged per 60 frames (us): M1 12000 (the
`xrWaitFrame` wait; `frame_start_wait` 12), M2 780, M3 1000, R 5760
(`draw_data_wait` 450), `join_wait` 0.1, full tick 13880. The main thread
never blocks on R, so `xrWaitFrame(N+1)` runs inside R(N) as designed
(`doc/editor/render_thread.md` section 2.2). `ovrgpuprofiler` over 30 s:
GPU utilization 47 % at 456 MHz, 97 % of GPU time shading fragments,
texture fetch stall 26 %, nearest filtered 85 %, non-base-level textures
0 % (no mips sampled; unchanged since 2026-05-01), LRZ busy 84 %. Raw
samples and the full logcat capture are in
`memory-bank/local/quest_2026_10_24/`. The run was taken with the user
wearing the headset, so the view and the controller rays were not
reproducible; every later number comes from the benchmark mode below.

## Benchmark mode

Purpose: a timed run on the device that does not depend on where the user
looks or what the controllers do, so two runs of two builds compare. The
headset may lie on the desk. The run is a timedemo: the simulation clock
advances a fixed step per frame, so every run renders the same frames from
the same inputs however fast the device is, and performance is the wall
time those frames take. How to run one: [../agents/quest.md](../agents/quest.md)
"Benchmark runs".

- **Activation.** `editor --benchmark <json>` (cxxopts, next to
  `--commands`). On Android `ErheActivity.getArguments()` returns the
  `args` string extra of the launch intent split on whitespace, so
  `adb shell am start ... --es args "--commands config/editor/commands_bench_heavy.json --benchmark config/editor/benchmark_orbit.json"`
  reaches `main()` unchanged. The `config/editor/benchmark_*.json` files
  are APK-bundled with the rest of `config/`. A file that does not load
  logs `Benchmark failed <path>: <reason>` and the editor exits.
- **Benchmark file.** `name` (`[A-Za-z0-9_-]+`; names the result file),
  `warmup_frames` (default 120), `measure_frames` (default 600),
  `frame_dt` (seconds of simulation per measured frame, default 1/72, at
  most 0.025: `Time::prepare_update()` caps a frame's advance at 25 ms),
  `settle_timeout_frames` (default 1800), optional
  `display_refresh_rate_hz`, and `camera_path`: either
  `{"type":"orbit","center":[x,y,z],"radius":r,"height":h,"period_frames":n,"look_at":[x,y,z]}`
  (position center + (r sin t, h, r cos t), t = 2 pi frame / n, looking at
  `look_at` with +Y up) or
  `{"type":"keyframes","frames":[{"frame":0,"position":[...],"yaw_pitch_roll_deg":[...]}, ...]}`
  (integer frames start at 0 and increase; position and the three angles are
  interpolated linearly, looped with the last keyframe's frame as the
  loop length, so a seamless loop repeats the first keyframe last;
  orientation = yaw about +Y * pitch about +X * roll about +Z, identity
  looking down -Z; a single keyframe is a fixed pose).
  `benchmark_orbit.json` orbits the default OpenXR room at 1.6 m height,
  radius 2 m, looking at (0, 1, 0), period 720 frames, at 120 Hz;
  `benchmark_static.json` holds one pose at the display's own rate.
- **Phases.** Warmup: `warmup_frames` frames with the simulation clock
  frozen and the camera path running from frame 0. Settle: the driver
  waits until `App_context::is_scene_load_in_flight()` is false (async
  operations, queued operations, pending scene commits and asset loads,
  the terms MCP `get_async_status` reports) and the brush geometry queue
  is idle (`Scene_builder::is_brush_geometry_idle()`, doc/editor/brushes.md
  G5), the frame renders (under OpenXR: waited, `should_render` and the
  actions updated; always without OpenXR) and, when the file sets
  `display_refresh_rate_hz`, the achieved refresh rate is within 0.5 Hz
  of it; after `settle_timeout_frames` frames it logs
  `Benchmark failed <name>: <reason>` naming the first condition that did
  not hold (`frames do not render ...`, `scene did not settle`, or
  `display refresh rate did not settle (requested X Hz, achieved Y Hz)`),
  writes no file and the editor exits. A refused refresh rate request
  fails at once (`display refresh rate request refused`). Measure:
  `measure_frames` frames, the camera path restarted at frame 0 and the
  simulation advancing `frame_dt` per frame; a measured frame that does
  not render fails the run at once (`frame N not rendered`, no file). The
  line `Benchmark <name>: scene settled after N frames ...` marks the
  start.
- **Determinism.**
  - Fixed simulation time per frame: in benchmark mode `start_frame()`
    replaces the frame's display advance (the FR4 value that is the one
    source of the frame's simulation advance, read by
    `Time::prepare_update()` and the animation player) with the driver's:
    0 during warmup and settle, exactly `frame_dt` per measured frame. The
    measured window therefore starts from the same simulation state at
    simulation time 0 however long loading and settling took.
  - The camera path is indexed by the frame number of the measured
    window, never by wall time.
  - Measurement starts only on a settled scene (Phases above).
  - No randomness in the measured path: the scene builders, the brush
    placement and the render path use no unseeded generator. The
    generators in the editor are seeded from constants (DDGI, radiance
    cascades, the frame pacing window) or from a node parameter (the
    instance graph nodes), or are off the measured path (the scene id of
    a new scene, glTF uid generation on export, the fly camera's jitter
    and synthesized input, both off by default).
  - Run identity in the result: the erhe version string, the benchmark
    file path and its FNV-1a hash, the startup commands file the editor
    ran, the achieved display refresh rate and the XR predicted display
    period, device model and OS build (Android system properties; the
    host script fills them from `adb getprop` when empty), the XR CPU /
    GPU performance levels from the settings and the levels actually
    applied at the start and end of the measured window with the thermal
    step-down count (`boost_on_thermal_warning` keeps stepping down in
    benchmark mode), the Android thermal status at the start and end of
    the measured window (`AThermal`; -1 when unavailable), and
    `input_hash`: FNV-1a seeded with the content hash of the startup
    commands file the editor ran (the `--scene` file when that path is
    used; also recorded as `commands_file_hash`), then over, per measured
    frame, the benchmark file hash, `frame_dt` and the measured frame
    index (the sources of the pose rather than the float pose, so builds
    with a different libm or code generation still match, and two results
    of different scenes cannot claim identical inputs).
  - `input_hash` proves identical inputs, not identical history. The
    editor's frame number (`Time`, the camera block, the render thread's
    draw data handoff) keeps counting through the run-dependent settle,
    and progressive and temporal GPU state (DDGI / radiance cascades
    budgets and histories, the shadow map cache) advances during settle;
    that state is excluded from the determinism guarantee.
- **Pose independence.** `erhe::xr::Headset::set_head_pose_override()`
  (main thread, before `wait_frame()`) poses the head from the path: the
  views are then located in the view reference space (each eye relative
  to the tracked head) and composed with the path head, eye_i =
  path_head * inverse(located_head) * located_eye_i, so the IPD and the
  per-eye fovs stay the runtime's. The wait side (`update_view_pose()`,
  `get_headset_pose()`, `locate_views()`) uses the value set; the frame
  carries it to R in `Frame_timing::head_pose_override`, and
  `render_frame()` / `render_frame_multiview()` apply it to the views they
  render and submit as the projection layer (the compositor reprojects to
  the real head, which is correct per the OpenXR spec: the layer pose is
  the pose the image was rendered from). `Headset_view` sets the path pose
  in M1 before the wait.
- **Action independence.** In benchmark mode `Headset_view` skips
  `update_pointer_context_from_controller` (with it the ray trace, ID
  pick and quad layer hover merges), `update_id_render`, the controller
  ray, the XR input event injection (so no button or thumbstick reaches
  the commands, e.g. the camera offset) and the controller visualization
  update, and hides the controller visualization; `xrSyncActions` still
  runs (the session needs it). The Hud and the hotbar follow the settings
  as before; their ImGui hosts keep the wall-clock `io.DeltaTime`, so
  with those quad layers on their animations are wall-time driven inside
  the window. Benchmark settings should turn the Hud and the hotbar off
  (`openxr_editor_settings.json` overrides, not benchmark flags).
- **Display refresh rate.** On OpenXR `xrWaitFrame` paces the app to the
  display: when a frame fits the refresh period the wall time is pinned
  at frames * period and only the breakdown terms show the headroom. The
  result records the achieved refresh rate (`xrGetDisplayRefreshRateFB`
  on OpenXR, the window's display otherwise) and
  `frames_at_refresh_ceiling`, the measured frames whose wall time is
  within 5 % of the refresh period, so a reader sees when the headline is
  vsync-bound; the rate is read again on the last measured frame and
  stored as `display_refresh_hz_end`, so a mid-run switch is visible. `display_refresh_rate_hz` in the benchmark file requests
  that rate through `xrRequestDisplayRefreshRateFB` once the session
  runs (nothing restores it; the run exits) to raise the ceiling; on the
  Quest the request is accepted but the display switches only with the
  developer property the host script sets around the run
  ([../agents/quest.md](../agents/quest.md) "Off-head runs").
- **Measurement and report.** Per measured frame, at the end of `frame()`
  (the same point of every frame): the steady-clock wall time since the
  previous frame's end, and the phase timing terms full tick, M1, M2, M3
  and R (deltas of `Phase_timing` per frame; R is the frame joined during
  that tick), plus the `XR_META_performance_metrics` counters, read in M2
  after the XR actions (they are queried in R by `Headset::begin_frame()`;
  OpenXR runs the serial schedule, so R is idle in M2). After
  `measure_frames` it logs one `Benchmark result <name>: wall_time_s ...
  frames_per_second ...` line (`editor.frame`, info): the headline wall
  time from the end of the last unmeasured frame to the end of the last
  measured one and the derived frame rate first, then the frame count,
  refresh rate, `frames_at_refresh_ceiling`, mean / median / p99 / max of
  the per-frame wall time and the phase terms, mean / median / p99 of each
  XR counter, and `input_hash`. `logs/benchmark_<name>.json` (relative to
  the working directory; on Android under the app's external files
  directory, `SDL_GetAndroidExternalStoragePath()`, so `adb` reads it
  without `run-as` on a release APK; the log names the path) holds
  `wall_time_s` and `frames_per_second` first, then the refresh block, the
  identity, the summary (mean, median, p99, min, max per term and
  counter) and the per-frame arrays. The editor then exits (the
  `quit_after_frames` path). Every per-frame container is reserved before
  the measured window, so the measured frames allocate nothing.
- **Host script.** `scripts/quest_bench.py run --bench <benchmark.json> [--scene <commands.json>] [--worn] [--gpu-metrics] [--repeat N] [--push]`
  and `compare a.json b.json`; usage in [../agents/quest.md](../agents/quest.md)
  "Benchmark runs". `--repeat N` launches N runs and reports each term's
  median across runs with the min / max spread, so thermal and DVFS noise
  shows apart from code changes; with `--gpu-metrics` the GPU frequency
  metrics are always captured, so clock changes between runs are
  recorded.
- **Out of scope for the first commit.** Driving a desktop viewport
  camera along the same path (the driver and the path type are
  platform-neutral so this can follow), and recording the counters on
  desktop. On the desktop the driver runs with the same phases and
  report, without a camera path consumer.
