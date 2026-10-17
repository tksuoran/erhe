# Quest 3 performance

Status: proposed

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
