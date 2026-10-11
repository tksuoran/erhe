# Render thread frame-time measurements (2026-10-10 and 2026-10-11)

The frame-time measurements taken while the editor's render thread
(`doc/editor/render_thread.md`) was built, kept to interpret a future re-run
of `scripts/render_thread_bench.py` and `scripts/tracy_zone_summary.py`
(`doc/editor/render_thread.md` section 3.4 describes the tooling). All
numbers are microseconds per frame. The configurations are `inline`
(`ERHE_RENDER_THREAD=0`), `dedicated` (`=1`, `ERHE_IMGUI_OVERLAP=0`),
`overlap` (`=1`, `=1`), the three of them with `ERHE_FRAME_PIPELINING=0`,
and `pipelined` (the overlap plus `ERHE_FRAME_PIPELINING=1`). The scenes
are `config/editor/commands_bench_heavy.json` (heavy), `commands_bench_draws.json`
(draws) and the default scene. Per-machine values and the run directories
are in the local memory bank, not here.

## 1. Display-paced runs (Debug, windowed, NVIDIA GPU, default scene, MCP polling)

Averages of 60 frames from the `editor.frame` phase log of the verification
scripts' runs, before frame pipelining existed. The scripts poll the MCP
server, which inflates M2's `imgui` term.

| Term | dedicated, overlap off | dedicated, overlap on |
|---|---|---|
| M3 (the ImGui windows) | 397 | 444 |
| R | 1481 | 1588 |
| `join_wait` (main blocked on R) | 1557 | 1315 |
| `draw_data_wait` (R blocked on M3) | 0.6 | 0.6 |
| handoff kick / join latency | - | 115 / 27 |
| `slot_acquire` (display pacing) | ~6200 | ~6400 |
| full tick | ~9200 | ~9400 |

Reading: the overlap works as designed (the main thread blocks 250-350 us
less per frame, which is M3 minus the handoff; R never waits for the draw
data), and the frame time says nothing because every run is paced by the
display (`slot_acquire` about 6 ms).

## 2. Unpaced runs, serial schedules (Release, GCC, 2026-10-10)

Vsync off, no MCP polling, 540 steady-state frames per run.

Two defects fixed before these numbers meant anything: `ERHE_PROFILE_FUNCTION()`
recorded a 32-deep callstack with each of the ~2600 zones of a frame (about
1.6 ms per frame; the depth is the CMake option `ERHE_TRACY_CALLSTACK_DEPTH`,
0 in the trees measured), and the ImGui hosts took the ImGui renderer mutex
before waiting for M3's draw data, which M3 needs to draw (a deadlock one
startup in ten; the wait now comes first).

Integrated Intel GPU:

| Scene, window | Term | inline | dedicated | overlap |
|---|---|---|---|---|
| heavy, 2304x1200 | full tick | 27657 | 27663 | 27643 |
| heavy, 2304x1200 | `slot_acquire` (GPU wait) | 17913 | 20209 | 19207 |
| draws, 2304x1200 | full tick | 9383 | 10762 | 9356 |
| default, 2304x1200 | full tick | 7982 | 7967 | 7970 |
| draws, 960x540, viewport 320x200 | full tick | 2634-2671 | 2637-2645 | 2634-2650 |
| draws, 960x540, viewport 320x200 | M2 | 820-922 | 642-737 | 738-810 |
| draws, 960x540, viewport 320x200 | R | 1521-1611 | 1708-1747 | 1738-1769 |
| draws, 960x540, viewport 320x200 | `join_wait` | 0 | 1775 | 1634 |
| draws, 960x540, viewport 320x200 | handoff kick / join | - | 102 / 127 | 100 / 131 |
| Debug, draws, small | full tick | 21745 | 21247 | 20560 |

Discrete NVIDIA GPU (RTX 5070 Laptop, driver 595.99), the same binary, full
window; the GPU times are the Tracy GPU zones of the inline run (main pass +
shadows + post-processing):

| Scene, window | Term | inline | dedicated | overlap |
|---|---|---|---|---|
| heavy, 2304x1200 | full tick | 7339 | 7335 | 7375 |
| heavy, 2304x1200 | `slot_acquire` | 4233 | 3987 | 4160 |
| heavy, 2304x1200 | R | 2667 | 2858 | 2854 |
| heavy, 2304x1200 | GPU zones | 3900 + 2040 + 310 = 6250 | | |
| draws, 2304x1200 | full tick | 2465 | 2470 | 2488 |
| draws, 2304x1200 | `slot_acquire` | 1178 | 1032 | 1309 |
| draws, 2304x1200 | R | 860 | 941 | 934 |
| draws, 2304x1200 | GPU zones | 1070 + 215 + 330 = 1610 | | |
| default, 2304x1200 | full tick | 1915 | 1921 | 1922 |
| default, 2304x1200 | `slot_acquire` | 1472 | 1508 | 1540 |
| default, 2304x1200 | R | 237 | 239 | 260 |
| default, 2304x1200 | GPU zones | 580 + 80 + 270 = 930 | | |

Reading:

- At the full window every scene is GPU-bound on both GPUs: the tick is the
  GPU time and the main thread spends it in `slot_acquire`, the wait for the
  GPU to retire frame F-2. On the draws and default scenes on the NVIDIA GPU
  the tick is about twice the timed GPU zones, yet `slot_acquire` holds the
  main thread for half the tick: a wait outside the timed render passes (the
  ImGui host pass and the present path are not GPU-timed), the same in every
  configuration.
- With the GPU taken out (small viewport, iGPU) the serial configurations
  are within run-to-run noise (about 0.2 ms; the laptop's thermal state
  moves a whole table by that much). The dedicated thread removes M1 + M3
  (about 0.25 ms) from the critical path and adds the handoff (two
  condition variable wakes, about 0.23 ms) and a slower R (+0.1-0.2 ms, the
  render thread reading what the main thread just wrote). Net zero.
- The CPU cost that matters is inside R: `Shadow_renderer::render` is 1.4 of
  R's 1.7 ms on the draws scene, most of it the tight directional light fit
  (`tight_directional_light_projection_transforms`: 0.63 ms each on 1500
  caster bounds, calipers 0.49 ms, convex hull 0.24 ms x 2). The fit is now
  skipped for directional lights that cast no shadow (`Light_shadow_use`),
  which took `Light_projections::apply` from 1.3 to 0.9 ms on that scene.
- Thread placement is not the problem: pinning both threads to P-cores or
  to E-cores of the hybrid CPU does not change the tick.
- A 200 us spin before both condition variable waits changed nothing: both
  waits are longer than that, and the latency is the wake of a thread whose
  core has gone idle.
- Debug is 8x slower across the board (R 18 ms) with the same shape.

## 3. The pipelined schedule (Release, Intel iGPU, 2026-10-11)

Window 960x540, viewport 320x200, 720 frames, 3 batches, medians; `M2`
carries its GPU wait (`slot_acquire`) in brackets.

| Scene | Config | full tick | M1 | M2 (`slot_acquire`) | M3 | R | `join_wait` |
|---|---|---|---|---|---|---|---|
| draws | inline | 2753 | 175 | 1314 (1145) | 124 | 1141 | 0 |
| draws | dedicated | 2753 | 176 | 996 (792) | 125 | 1477 | 1369 |
| draws | overlap | 2748 | 220 | 1090 (894) | 140 | 1413 | 1320 |
| draws | pipelined | 3204 (batches 2805-3224) | 195 | 2914 (2736) | 107 | 1223 | 0 |
| default | inline / dedicated / overlap / pipelined | 1596 / 1584 / 1559 / 1541 | | | | | |
| heavy | inline / dedicated / overlap / pipelined | 9113 / 9125 / 9110 / 9111 | | | | | |

Tracy phase picture (draws, small viewport, `scripts/tracy_zone_summary.py`):

| Config | R while main busy | R while main in M2 | R while main joins | R idle |
|---|---|---|---|---|
| overlap | 19% | 0% | 80% | 49% of tick |
| pipelined | 99% | 78% | 0.4% | 42% of tick |

Reading: every scene is GPU-bound on the iGPU, the draws scene at the small
viewport included (its timed GPU zones sum to about 2.1 ms plus the untimed
ImGui host pass and the present; a 640x360 window with a 160x100 viewport is
still GPU-bound at about 2.25 ms). With pipelining the main thread's wait
moves from `join_wait` to `slot_acquire` (the timeline wait for frame N-1)
and the tick does not drop; the kick handoff costs 70-100 us; the pipelined
draws tick was about 0.45 ms slower in two of three batches (noise not
established). The CPU-side bound, max(M1 + M2 work + M3 about 0.48 ms, R
about 1.2 ms) plus the handoff, is well under the GPU floor, so these runs
cannot show the pipelining gain in the tick; the Tracy picture shows the
schedule itself: R runs while M2 runs. A GPU with a lower floor is needed
to measure the gain (`doc/editor/render_thread.md` section 4).
