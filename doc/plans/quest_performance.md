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
headset may lie on the desk.

- **Activation.** `editor --benchmark <json>` (cxxopts, next to
  `--commands`). On Android `ErheActivity.getArguments()` returns the
  `args` string extra of the launch intent split on spaces, so
  `adb shell am start ... --es args "--commands config/editor/commands_bench_heavy.json --benchmark config/editor/benchmark_orbit.json"`
  reaches `main()` unchanged. The `config/editor/benchmark_*.json` files
  are APK-bundled with the rest of `config/`.
- **Benchmark file.** `name`, `warmup_frames`, `measure_frames`,
  `camera_path`: either `{"type":"orbit","center":[x,y,z],"radius":r,"height":h,"period_frames":n,"look_at":[x,y,z]}`
  or `{"type":"keyframes","frames":[{"frame":0,"position":[...],"yaw_pitch_roll_deg":[...]}, ...]}`
  interpolated linearly and looped. The path is indexed by the frame
  number, never by wall time, so run A and run B render the same frames.
- **Pose independence.** `Headset_view` keeps a `Benchmark_pose` for the
  frame: the head pose on the path. The located eye views are used only
  for their offsets relative to the located head
  (`Xr_session::update_view_pose` / `get_headset_pose`): eye_i =
  path_head * inverse(located_head) * located_eye_i, so the IPD and the
  per-eye fovs stay the runtime's. `update_camera_node()` poses the root
  camera from the path head; `Headset_view_resources::update()` poses the
  eye cameras from the overridden views, and the projection layers submit
  the overridden poses (the compositor reprojects to the real head, which
  is correct per the OpenXR spec: the layer pose is the pose the image
  was rendered from).
- **Action independence.** In benchmark mode `Headset_view` skips
  `update_pointer_context_from_controller`, `update_quad_layer_hover`,
  `update_id_render` and `update_hover_with_id_render`, draws no
  controller ray and hides the controller visualization; `xrSyncActions`
  still runs (the session needs it). The Hud and the hotbar follow the
  settings as before (a benchmark file may want them off: use
  `openxr_editor_settings.json` overrides, not benchmark flags).
- **Measurement and report.** After `warmup_frames`, the driver records
  per frame: full tick, M1, M2, M3, R (the `Phase_timing` terms, per
  frame rather than per 60) and the `XR_META_performance_metrics`
  counters already queried once per frame (`Xr_session::get_perf_counters`,
  app CPU / GPU time, compositor GPU time, CPU / GPU utilization, dropped
  and stale frame counts). After `measure_frames` it logs one
  `Benchmark result <name>: ...` line (`editor.frame`, info) with mean,
  median and p99 of each term, writes `logs/benchmark_<name>.json` with
  the same summary plus the per-frame arrays and the commit id, and
  requests exit (the `quit_after_frames` path). The per-frame storage is
  reserved at start (`measure_frames` entries), so the measured frames
  allocate nothing.
- **Host script.** `scripts/quest_bench.py run --scene <commands.json> --bench <benchmark.json> [--off-head] [--gpu-metrics]`:
  force-stops the app, starts `scripts/quest_logcat.sh`, launches with
  the `args` extra, waits for the result line (bounded), pulls
  `files/logs/benchmark_<name>.json` through `run-as`, prints the summary
  table and stores the JSON under `memory-bank/local/quest_runs/`.
  `--off-head` sends `am broadcast -a com.oculus.vrpowermanager.prox_close`
  before the launch and `automation_disable` after the run so the session
  does not pause with the headset on the desk; the headset-prompt protocol
  still applies to the launch. `--gpu-metrics` runs `ovrgpuprofiler` for
  the measured window. `compare a.json b.json` prints the two summaries
  side by side with the deltas.
- **Out of scope for the first commit.** Driving a desktop viewport
  camera along the same path (the driver and the path type are
  platform-neutral so this can follow), and recording the counters on
  desktop.

