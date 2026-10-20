# Render thread frame-time measurements (2026-10-10 to 2026-10-20)

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

## 4. Apple M4 Pro, macOS (2026-10-20)

Release and Debug Xcode trees, Tracy with `ERHE_TRACY_CALLSTACK_DEPTH=0`,
window 960x540, viewport 320x200, 720 frames, 3 runs per configuration,
medians. Three drivers: native Metal through the headless tree (emulated
swapchain, no display involved), and Vulkan through MoltenVK and KosmicKrisp
on the windowed tree.

Two bench defects were fixed first. Since the frame pacing tier defaults to
`auto`, a device without present timing runs tier S, whose slop servo
sleeps about 4 ms in the frame start even with vsync forced off (and the
present mode `IMMEDIATE`); that sleep is `frame_start_wait`, outside the
phase log's full tick, so the "unpaced" runs reported a 1.2 ms tick for a
5.3 ms loop period. The bench now also sets `frame_pacing_tier=off`. And
tracy-capture started before the editor dropped the connection in the
handshake on macOS; it now connects after the readiness marker.

Release, headless Metal:

| Scene | inline | dedicated | overlap | pipelined | R | `slot_acquire` inline / pipelined |
|---|---|---|---|---|---|---|
| default | 535 | 534 | 534 | 534 | 203-214 | 263 / 456 |
| draws | 1467 | 1469 | 1470 | 1468 | 654-689 | 620 / 1255 |
| heavy | 6392 | 6390 | 6388 | 6388 | 2524-2584 | 3539 / 6069 |
| draws, preset Low | 1309 | | | 1311 | 544-574 | 601 / 1130 |
| heavy, preset Low | 4139 | | | 4136 | 1731-1802 | 2156 / 3872 |

Every scene is GPU-bound, at the small viewport too. A Metal System Trace
(`xcrun xctrace record --template 'Metal System Trace'`, the
`metal-gpu-intervals` table) of draws, inline, shows the GPU busy 93% of
the time: the viewport render pass about 0.6 ms per frame, vertex-bound
(1500 instances, 4x MSAA, independent of the viewport size), the shadow
pass about 0.18 ms. The Low preset (1024 shadow maps, 2x MSAA) moves heavy
from 6.4 to 4.1 ms and draws only from 1.47 to 1.31 ms. With pipelining the
main thread's wait moves from `join_wait` to `slot_acquire`, the tick does
not change, and the Tracy phase picture is the designed one: R while main
busy 99.7% (draws) and 99.8% (heavy), kick 2-4 us, join wait 0.

Debug, headless Metal (CPU-bound: `slot_acquire` about 0.1 us), 480
frames, 2 runs:

| Scene | inline | dedicated | overlap | pipelined | M1 + M2 + M3 | R |
|---|---|---|---|---|---|---|
| default | 1379 | 1355 | 949 | 871 | 520 | 847-867 |
| draws | 10197 | 9862 | 9440 | 7852 | 2220 | 7719-8004 |

Reading: this is the first measurement in which the CPU bounds the frame,
and every configuration's tick is its predicted sum. Inline is M1 + M2 +
M3 + R; dedicated serial is the same minus M1; the ImGui overlap hides M3
(draws: 9862 - 9440 = 422 = M3); pipelined is max(M1 + M2 + M3, R) plus
the handoff (draws: max(2.2, 7.85) = 7.85 ms measured 7.85; default:
max(0.52, 0.86) measured 0.87). R overlapped with M2 is about 1.6% slower
than R alone; the kick costs 4 us. R3 holds on Metal.

Windowed Vulkan (MoltenVK, KosmicKrisp) does not measure the schedule:

| Scene, driver | inline | dedicated | overlap | pipelined |
|---|---|---|---|---|
| draws, MoltenVK, per run | 3073 / 2902 / 2978 | 1914 / 1677 / 3394 | 3146 / 1997 / 2578 | 3182 / 3211 / 3178 |
| heavy, MoltenVK | 6232 | 6249 | 6250 | 6250 |
| draws, KosmicKrisp | 5973 | 5815 | 6169 | 5493 |
| heavy, KosmicKrisp | 7789 | 7360 | 7924 | 8192 |

The draws tick on MoltenVK jumps between about 1.8 and 3.2 ms from run to
run in the serial configurations. R is dominated by
`submit_command_buffers`, bimodal per call (median 0.4-0.6 ms, p90
3.4-3.7 ms), with the timed GPU work at about 0.5 ms; the Tracy captures
of dedicated and pipelined have the same tick (3.28 and 3.32 ms). The long
submit is consistent with the presenting submit waiting for a
`CAMetalLayer` drawable (about three frames per 8.33 ms display refresh),
not verified. The KosmicKrisp spread between configurations is of the
same size as its run-to-run spread.

Also found on this machine:

- MoltenVK's Vulkan timestamp queries do not time render passes on this
  GPU (counters are sampled at stage boundaries only): Tracy reports the
  main pass of draws as 2.5 us, the Metal System Trace as about 0.6 ms.
  Read GPU cost on Apple GPUs from a Metal System Trace.
- The Metal backend emits no Tracy GPU zones.
- The windowed Metal build is paced by the display with
  `force_disable_vsync` (`displaySyncEnabled = NO` on the layer):
  `nextDrawable` waits about 8.3 ms per frame on a 120 Hz display.

