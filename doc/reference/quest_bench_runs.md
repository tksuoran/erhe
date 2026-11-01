# Quest benchmark runs

The record of Quest 3 benchmark runs, the Bistro frame above all
(`config/editor/benchmark_bistro.json`), newest last. Each entry is one
`scripts/quest_bench.py run` invocation (its `--repeat` runs): the
configuration the editor had in effect (light shading, lights shaded and
shadow-mapped, light limits, shadows, culling, draw lists), the results and
the Quest's state per run (thermal status and temperatures, CPU and GPU
clocks with their caps, GPU busy and CPU usage). How to run and record:
`doc/agents/quest.md` "Benchmark runs".

Entries are appended by `quest_bench.py run --record` (or
`quest_bench.py record <run.json>...` for stored results), then amended by
hand where a value was not captured. The per-run result files they name
are stored per machine under `memory-bank/local/quest_runs/` (gitignored);
the numbers needed to compare runs are in the entry itself.

Comparing entries: the frame rate is comparable only between entries with
the same `input_hash` (same camera poses and `frame_dt`) and the same
display rate; the Bistro frame is GPU-bound at every configuration
recorded so far, so the XR counter's app GPU frame time and the
`XR multiview` pass are the terms that move with the lighting.

## 2026-10-27 21:31 - bistro, no configuration file

The Bistro's own 43 lights (33 point, 10 spot) with the OpenXR preset, clustered shading. Recorded before configuration and Quest state capture existed: the configuration rows are the defaults at that commit (no MCP configuration calls were made), the Quest state columns were not sampled.

- Runs: 3 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash c4294da4554d34d5; results `20261027_213154_bistro_0.json`, `20261027_213154_bistro_1.json`, `20261027_213154_bistro_2.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (296a0a3fd-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro.json --no-scene`; scene `bistro_quest.glb` (listed 4.7 s, idle 5.0 s).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | automatic / clustered (43 active lights > 16; the cluster binning pass ran) |
| Lights | point 33, spot 10, all shaded (clustered); shadow-mapped not captured |
| Light limits shadow+unshadowed | directional 1+4, spot 2+4, point 1+4 |
| Shadows | on; 1024 px, point 512 px, 16 bits, depth, hard, receiver_plane, cull_back (no shadow pass timer ran) |
| Light cluster binning | gpu |
| Culling classification | cpu |
| Occlusion culling | off |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 8.30 [8.27..8.30] |
| Frame wall time (ms) | 120.47 [120.36..120.80] |
| M2 / R / M3 (ms) | 116.14 [115.94..116.50] / 4.27 [4.24..4.37] / 0.70 [0.68..0.71] |
| App GPU frame time, XR counter (ms) | 113.30 [113.26..113.79] |
| GPU utilization, XR counter (%) | 99.7 [99.6..99.7] |
| CPU utilization average / worst, XR counter (%) | 6.0 [5.9..6.0] / 7.1 [7.0..7.1] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 [0.040..0.041] |
| GPU pass Light cluster binning Headset (ms) | 0.054 [0.054..0.054] |
| GPU pass XR multiview (ms) | 119.639 [119.545..120.029] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 119.735 [119.643..120.124] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 8.30 | - -> -; 0 -> 0, step-downs 0 | - | - | - | - | - | - | - | - |
| 1 | 8.27 | - -> -; 0 -> 0, step-downs 0 | - | - | - | - | - | - | - | - |
| 2 | 8.30 | - -> -; 0 -> 0, step-downs 0 | - | - | - | - | - | - | - | - |

## 2026-10-29 08:31 - bistro, single_directional_loops

One shadow-mapped directional light (added over MCP); Bistro's point and spot lights unshaded; light_shading loops (clustering off).

Against the 2026-10-27 entry (same APK, same input_hash, 43 lights clustered): app GPU frame time 113.3 -> 62.7 ms, 8.3 -> 14.6 fps; still GPU-bound at 99 % utilization, so about 62 ms of the frame is not light shading. The GPU ran at its 456 MHz cap (performance levels unset), the CPU clusters at their 1920 / 1651 MHz caps; thermal status 0 throughout.

- Runs: 3 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash c4294da4554d34d5; results `20261029_083137_bistro_0.json`, `20261029_083137_bistro_1.json`, `20261029_083137_bistro_2.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (296a0a3fd-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro.json --no-scene`; scene `bistro_quest.glb` (listed 4.77 s, idle 5.82 s).
- Configuration calls: set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (1 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+0, spot 0+0, point 0+0 |
| Shadows | on; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | cpu |
| Occlusion culling | off |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 14.60 [14.57..14.61] |
| Frame wall time (ms) | 68.43 [68.30..68.51] |
| M2 / R / M3 (ms) | 0.65 [0.65..0.66] / 4.39 [4.36..4.42] / 0.60 [0.60..0.60] |
| App GPU frame time, XR counter (ms) | 62.65 [62.49..62.76] |
| GPU utilization, XR counter (%) | 99.2 [99.1..99.2] |
| CPU utilization average / worst, XR counter (%) | 8.2 [8.2..9.0] / 10.8 [10.1..10.9] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 [0.040..0.040] |
| GPU pass Shadow 0 (ms) | 0.000 |
| GPU pass XR multiview (ms) | 67.307 [67.269..67.486] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 67.352 [67.316..67.548] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 14.60 | 0 -> 0; 0 -> 0, step-downs 0 | 58.1 -> 58.4 | 57.4 -> 57.6 | 57.5 -> 57.6 | 40.0, 97 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 99 | 19.4 |
| 1 | 14.61 | 0 -> 0; 0 -> 0, step-downs 0 | 58.3 -> 58.4 | 57.6 -> 57.4 | 57.8 -> 57.6 | 41.0, 96 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 99 | 18.9 |
| 2 | 14.57 | 0 -> 0; 0 -> 0, step-downs 0 | 58.1 -> 58.2 | 57.5 -> 57.4 | 57.7 -> 57.5 | 41.0, 96 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 99 | 19.9 |


## 2026-10-29 08:44 - bistro, single_directional_loops_gpu_culling

As single_directional_loops, with culling classification gpu (GPU-generated draw commands where the device supports them).

Against single_directional_loops (08:31): no change, XR multiview 67.5 vs 67.3 ms (runs 1-2); the two compute passes add about 0.12 ms. Run 0 was slower (XR multiview 77.5 ms, flat over the whole window, GPU clock unchanged at 456 MHz): unexplained per-launch outlier, the first launch after a configuration change; the medians exclude it.

- Runs: 3 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash c4294da4554d34d5; results `20261029_084408_bistro_0.json`, `20261029_084408_bistro_1.json`, `20261029_084408_bistro_2.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (296a0a3fd-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro.json --no-scene`; scene `bistro_quest.glb` (listed 5.24 s, idle 5.24 s).
- Configuration calls: set_culling_classification, set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (1 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+0, spot 0+0, point 0+0 |
| Shadows | on; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | off (gpu supported) |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 14.52 [12.83..14.54] |
| Frame wall time (ms) | 68.64 [68.54..77.82] |
| M2 / R / M3 (ms) | 0.66 [0.66..0.67] / 4.47 [4.45..4.56] / 0.60 [0.59..0.60] |
| App GPU frame time, XR counter (ms) | 62.74 [62.52..62.94] |
| GPU utilization, XR counter (%) | 99.1 [99.1..99.2] |
| CPU utilization average / worst, XR counter (%) | 8.4 [8.0..9.1] / 10.8 [10.1..10.9] |
| GPU pass Cull classification (ms) | 0.021 [0.021..0.021] |
| GPU pass Cull command generation (ms) | 0.099 [0.085..0.101] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.039 [0.039..0.039] |
| GPU pass Shadow 0 (ms) | 0.000 |
| GPU pass XR multiview (ms) | 67.508 [67.486..77.547] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 67.691 [67.686..77.726] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 12.83 | 0 -> 0; 0 -> 0, step-downs 0 | 58.2 -> 58.6 | 58.3 -> 58.3 | 57.6 -> 57.6 | 40.0, 97 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 99 | 19.1 |
| 1 | 14.52 | 0 -> 0; 0 -> 0, step-downs 0 | 57.2 -> 57.6 | 56.7 -> 56.5 | 56.8 -> 56.6 | 40.0, 96 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 99 | 18.5 |
| 2 | 14.54 | 0 -> 0; 0 -> 0, step-downs 0 | 57.1 -> 57.5 | 56.6 -> 56.6 | 56.8 -> 56.6 | 40.0, 96 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 99 | 18.3 |


## 2026-10-29 08:51 - bistro, single_directional_loops_gpu_occlusion

As single_directional_loops, with culling classification gpu and occlusion culling on.

Against the gpu_culling entry (08:44): XR multiview 67.5 -> 45.4 ms (-33 %), 14.5 -> 21.5 fps (runs 1-2). Run 0 was slower (XR multiview 54.5 ms, flat over the window, GPU clock 456 MHz): the same first-launch outlier as the gpu_culling entry. The XR counter's app GPU frame time disagrees with the XR multiview timer in this configuration (50.2 vs 45.4 ms; run 0 41.6 vs 54.5 ms); compare the timer.

- Runs: 3 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash c4294da4554d34d5; results `20261029_085106_bistro_0.json`, `20261029_085106_bistro_1.json`, `20261029_085106_bistro_2.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (296a0a3fd-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro.json --no-scene`; scene `bistro_quest.glb` (listed 4.76 s, idle 4.76 s).
- Configuration calls: set_culling_classification, set_occlusion_culling, set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (1 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+0, spot 0+0, point 0+0 |
| Shadows | on; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | on (gpu supported) |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 21.48 [17.69..21.50] |
| Frame wall time (ms) | 46.52 [46.43..55.58] |
| M2 / R / M3 (ms) | 0.68 [0.67..0.68] / 4.54 [4.49..4.57] / 0.60 [0.60..0.60] |
| App GPU frame time, XR counter (ms) | 50.23 [41.65..50.50] |
| GPU utilization, XR counter (%) | 99.1 [98.4..99.2] |
| CPU utilization average / worst, XR counter (%) | 9.6 [9.4..10.0] / 12.2 [12.0..12.6] |
| GPU pass Cull classification (ms) | 0.024 [0.024..0.025] |
| GPU pass Cull command generation (ms) | 0.071 [0.070..0.072] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.039 [0.039..0.040] |
| GPU pass Shadow 0 (ms) | 0.000 |
| GPU pass XR multiview (ms) | 45.532 [45.374..54.479] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 45.685 [45.558..54.703] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 17.69 | 0 -> 0; 0 -> 0, step-downs 0 | 60.1 -> 61.0 | 61.0 -> 61.0 | 59.4 -> 59.6 | 41.0, 95 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 98 | 19.4 |
| 1 | 21.50 | 0 -> 0; 0 -> 0, step-downs 0 | 60.1 -> 60.4 | 59.7 -> 59.5 | 59.3 -> 59.2 | 41.0, 95 % | cpu 0 1: 1920 [1382..1920] cap 1382/2054; cpu 2 3 4 5: 1651 [1651..1920] cap 1651/2362 | 456 [456..492] cap 456 | 99 | 20.0 |
| 2 | 21.48 | 0 -> 0; 0 -> 0, step-downs 0 | 60.2 -> 60.6 | 59.9 -> 59.7 | 59.5 -> 59.3 | 41.0, 95 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 [1651..1920] cap 1651/2362 | 456 [456..492] cap 456 | 99 | 20.0 |


## 2026-10-29 09:11 - bistro, bistro_lights_clustered_gpu_occlusion

The Bistro's own lights, clustered light shading at the preset's limits, culling classification gpu and occlusion culling on.

- Runs: 1 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash c4294da4554d34d5; results `20261029_091127_bistro_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (296a0a3fd-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro.json --no-scene`; scene `bistro_quest.glb` (listed 4.25 s, idle 4.25 s).
- Configuration calls: set_culling_classification, set_occlusion_culling, set_graphics_preset (arguments in the result files' `host` block).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | clustered / clustered (Headset) |
| Lights | point 33/33 shaded, 1 shadow-mapped (0 cached); spot 10/10 shaded, 2 shadow-mapped (2 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+4, spot 2+4, point 1+4 |
| Shadows | on; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | on (gpu supported) |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 6.16 |
| Frame wall time (ms) | 165.14 |
| M2 / R / M3 (ms) | 160.53 / 4.42 / 0.66 |
| App GPU frame time, XR counter (ms) | 103.75 |
| GPU utilization, XR counter (%) | 99.8 |
| CPU utilization average / worst, XR counter (%) | 7.1 / 8.8 |
| GPU pass Cull classification (ms) | 0.039 |
| GPU pass Cull command generation (ms) | 0.134 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 |
| GPU pass Light cluster binning Headset (ms) | 0.054 |
| GPU pass XR multiview (ms) | 164.190 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 164.462 |

Perfetto capture (metavr perf capture, GPU render stages + Adreno counters, in the measured window); the surface with the most GPU time, medians per surface (ms):

| Run | Surface | Surfaces | Duration | Binning | Bin rendering | Preempted | Rendered bins | Trace |
|---|---|---|---|---|---|---|---|---|
| 0 | 1680x1760 color 64 bpp depth 24 bpp MSAA 1 HwVizBinning bins 24 | 21 | 164.97 | 10.62 | 84.30 | 68.02 | 17 | `20261029_091127_bistro_0` |

| Counter (mean over the capture) | Run 0 |
|---|---|
| % Time Shading Fragments | 95.41 |
| % Time Shading Vertices | 4.514 |
| % Shaders Busy | 83.64 |
| % Shader ALU Capacity Utilized | 45 |
| % Vertex Fetch Stall | 44.21 |
| % Texture Fetch Stall | 18.22 |
| % Stalled on System Memory | 0.1298 |
| ALU / Fragment | 119.4 |
| ALU / Vertex | 176.1 |
| Fragments Shaded / Second | 2.103e+09 |
| Vertices Shaded / Second | 2.703e+07 |
| Pre-clipped Polygons/Second | 3.235e+07 |
| Average Polygon Area | 261.3 |
| % Prims Trivially Rejected | 16.12 |
| Textures / Fragment | 2.185 |
| Textures / Vertex | 9.641 |
| GPU Frequency | 4.56e+08 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 6.16 | 0 -> 0; 0 -> 0, step-downs 0 | 60.9 -> 61.7 | 63.1 -> 63.3 | 60.1 -> 60.7 | 41.0, 94 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 [1382..1651] cap 1382/2362 | 456 cap 456 | 99 | 16.9 |


## 2026-10-29 09:16 - bistro, bistro_lights_loops_gpu_occlusion

The Bistro's own lights, per-type light loops at the preset's limits, culling classification gpu and occlusion culling on.

- Runs: 1 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash c4294da4554d34d5; results `20261029_091650_bistro_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (296a0a3fd-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro.json --no-scene`; scene `bistro_quest.glb` (listed 4.75 s, idle 4.75 s).
- Configuration calls: set_culling_classification, set_occlusion_culling, set_graphics_preset (arguments in the result files' `host` block).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | point 5/33 shaded, 1 shadow-mapped (0 cached); spot 6/10 shaded, 2 shadow-mapped (2 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+4, spot 2+4, point 1+4 |
| Shadows | on; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | - |
| Occlusion culling | on (gpu supported) |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 13.69 |
| Frame wall time (ms) | 72.90 |
| M2 / R / M3 (ms) | 0.80 / 4.27 / 0.61 |
| App GPU frame time, XR counter (ms) | 67.05 |
| GPU utilization, XR counter (%) | 99.3 |
| CPU utilization average / worst, XR counter (%) | 8.5 / 10.3 |
| GPU pass Cull classification (ms) | 0.041 |
| GPU pass Cull command generation (ms) | 0.123 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 |
| GPU pass XR multiview (ms) | 71.655 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 71.873 |

Perfetto capture (metavr perf capture, GPU render stages + Adreno counters, in the measured window); the surface with the most GPU time, medians per surface (ms):

| Run | Surface | Surfaces | Duration | Binning | Bin rendering | Preempted | Rendered bins | Trace |
|---|---|---|---|---|---|---|---|---|
| 0 | 1680x1760 color 64 bpp depth 24 bpp MSAA 1 HwVizBinning bins 24 | 53 | 72.13 | 10.61 | 55.22 | 4.83 | 17 | `20261029_091650_bistro_0` |

| Counter (mean over the capture) | Run 0 |
|---|---|
| % Time Shading Fragments | 88.12 |
| % Time Shading Vertices | 11.67 |
| % Shaders Busy | 70.7 |
| % Shader ALU Capacity Utilized | 15.47 |
| % Vertex Fetch Stall | 63.67 |
| % Texture Fetch Stall | 2.111 |
| % Stalled on System Memory | 0.2418 |
| ALU / Fragment | 129.1 |
| ALU / Vertex | 152 |
| Fragments Shaded / Second | 4.233e+08 |
| Vertices Shaded / Second | 5.899e+07 |
| Pre-clipped Polygons/Second | 7.07e+07 |
| Average Polygon Area | 18.86 |
| % Prims Trivially Rejected | 13.94 |
| Textures / Fragment | 6.425 |
| Textures / Vertex | 9.353 |
| GPU Frequency | 4.56e+08 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 13.69 | 0 -> 0; 0 -> 0, step-downs 0 | 57.6 -> 58.2 | 57.5 -> 57.6 | 57.0 -> 57.1 | 40.0, 94 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 99 | 18.4 |


## 2026-10-29 13:54 - bistro, single_directional_loops_gpu_occlusion

As single_directional_loops, with culling classification gpu and occlusion culling on.

- Runs: 1 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash c4294da4554d34d5; results `20261029_135401_bistro_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (e47cfd98c-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro.json --no-scene`; scene `bistro_quest.glb` (listed 4.17 s, idle 4.56 s).
- Configuration calls: set_culling_classification, set_occlusion_culling, set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (1 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+0, spot 0+0, point 0+0 |
| Shadows | on; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | on (gpu supported) |
| Content draws | all |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 21.33 |
| Frame wall time (ms) | 46.81 |
| M2 / R / M3 (ms) | 0.69 / 4.85 / 0.60 |
| App GPU frame time, XR counter (ms) | 42.33 |
| GPU utilization, XR counter (%) | 99.0 |
| CPU utilization average / worst, XR counter (%) | 9.9 / 12.0 |
| GPU pass Cull classification (ms) | 0.024 |
| GPU pass Cull command generation (ms) | 0.069 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 |
| GPU pass Shadow 0 (ms) | 0.000 |
| GPU pass XR multiview (ms) | 45.703 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 45.888 |

Perfetto capture (metavr perf capture, GPU render stages + Adreno counters, in the measured window); the surface with the most GPU time, medians per surface (ms):

| Run | Surface | Surfaces | Duration | Binning | Bin rendering | Preempted | Rendered bins | Trace |
|---|---|---|---|---|---|---|---|---|
| 0 | 1680x1760 color 64 bpp depth 24 bpp MSAA 1 HwVizBinning bins 24 | 83 | 46.12 | 10.62 | 30.26 | 3.74 | 17 | `20261029_135401_bistro_0` |

| Counter (mean over the capture) | Run 0 |
|---|---|
| % Time Shading Fragments | 79.13 |
| % Time Shading Vertices | 20.7 |
| % Shaders Busy | 63.11 |
| % Shader ALU Capacity Utilized | 20.69 |
| % Vertex Fetch Stall | 52.47 |
| % Texture Fetch Stall | 3.739 |
| % Stalled on System Memory | 0.436 |
| ALU / Fragment | 96.93 |
| ALU / Vertex | 145.1 |
| Fragments Shaded / Second | 6.454e+08 |
| Vertices Shaded / Second | 9.053e+07 |
| Pre-clipped Polygons/Second | 1.087e+08 |
| Average Polygon Area | 18.25 |
| % Prims Trivially Rejected | 13.2 |
| Textures / Fragment | 2.799 |
| Textures / Vertex | 9.288 |
| GPU Frequency | 4.56e+08 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 21.33 | 0 -> 0; 0 -> 0, step-downs 0 | 57.4 -> 59.3 | 56.9 -> 58.7 | 55.1 -> 57.8 | 31.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 98 | 21.7 |


## 2026-10-29 13:55 - bistro, single_directional_loops_gpu_occlusion_opaque_only

As single_directional_loops_gpu_occlusion, with content draws opaque_only (the alpha-tested and translucent passes draw nothing).

- Runs: 1 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash c4294da4554d34d5; results `20261029_135553_bistro_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (e47cfd98c-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro.json --no-scene`; scene `bistro_quest.glb` (listed 4.69 s, idle 4.69 s).
- Configuration calls: set_culling_classification, set_occlusion_culling, set_content_draws, set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (1 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+0, spot 0+0, point 0+0 |
| Shadows | on; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | on (gpu supported) |
| Content draws | opaque_only |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 28.36 |
| Frame wall time (ms) | 35.08 |
| M2 / R / M3 (ms) | 0.69 / 4.64 / 0.60 |
| App GPU frame time, XR counter (ms) | 31.36 |
| GPU utilization, XR counter (%) | 98.8 |
| CPU utilization average / worst, XR counter (%) | 13.0 / 14.7 |
| GPU pass Cull classification (ms) | 0.024 |
| GPU pass Cull command generation (ms) | 0.065 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 |
| GPU pass Shadow 0 (ms) | 0.000 |
| GPU pass XR multiview (ms) | 34.121 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 34.267 |

Perfetto capture (metavr perf capture, GPU render stages + Adreno counters, in the measured window); the surface with the most GPU time, medians per surface (ms):

| Run | Surface | Surfaces | Duration | Binning | Bin rendering | Preempted | Rendered bins | Trace |
|---|---|---|---|---|---|---|---|---|
| 0 | 1680x1760 color 64 bpp depth 24 bpp MSAA 1 HwVizBinning bins 24 | 110 | 34.52 | 8.72 | 21.22 | 3.03 | 17 | `20261029_135553_bistro_0` |

| Counter (mean over the capture) | Run 0 |
|---|---|
| % Time Shading Fragments | 79.47 |
| % Time Shading Vertices | 20.3 |
| % Shaders Busy | 59.31 |
| % Shader ALU Capacity Utilized | 21.53 |
| % Vertex Fetch Stall | 45.73 |
| % Texture Fetch Stall | 3.784 |
| % Stalled on System Memory | 0.5378 |
| ALU / Fragment | 80.3 |
| ALU / Vertex | 137.5 |
| Fragments Shaded / Second | 8.065e+08 |
| Vertices Shaded / Second | 8.546e+07 |
| Pre-clipped Polygons/Second | 1.157e+08 |
| Average Polygon Area | 24.61 |
| % Prims Trivially Rejected | 14.53 |
| Textures / Fragment | 2.089 |
| Textures / Vertex | 8.959 |
| GPU Frequency | 4.56e+08 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 28.36 | 0 -> 0; 0 -> 0, step-downs 0 | 55.3 -> 55.7 | 54.8 -> 54.6 | 54.1 -> 53.9 | 33.0, 100 % | cpu 0 1: 1920 [1382..1920] cap 1382/2054; cpu 2 3 4 5: 1651 [1651..2362] cap 1651/2362 | 456 [456..492] cap 456 | 98 | 23.3 |


## 2026-11-01 11:36 - bistro, single_directional_loops_gpu_culling

As single_directional_loops, with culling classification gpu (GPU-generated draw commands where the device supports them).

- Runs: 1 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash c4294da4554d34d5; results `20261101_113626_bistro_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (9d1b1ef0f-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro.json --no-scene`; scene `pvs_bistro_quest_cell_4m.glb` (listed 4.08 s, idle 4.89 s).
- Configuration calls: set_culling_classification, set_graphics_preset, create_light, set_node_transform, set_graphics_preset (arguments in the result files' `host` block).
- LOD (doc/plans/lod_gpu_selection.md decisions 5 and 6): the last set_graphics_preset sets `lod_enabled` on and `lod_cpu_selection` on_demand; the file stores no LOD chains, so the selection is not active.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (1 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+0, spot 0+0, point 0+0 |
| Shadows | on; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | off (gpu supported) |
| Content draws | all |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 12.85 |
| Frame wall time (ms) | 77.75 |
| M2 / R / M3 (ms) | 0.79 / 3.38 / 0.70 |
| App GPU frame time, XR counter (ms) | 71.13 |
| GPU utilization, XR counter (%) | 99.3 |
| CPU utilization average / worst, XR counter (%) | 7.9 / 9.9 |
| GPU pass Cull classification (ms) | 0.027 |
| GPU pass Cull command generation (ms) | 0.117 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.039 |
| GPU pass XR multiview (ms) | 76.743 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 76.943 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 12.85 | 0 -> 0; 0 -> 0, step-downs 0 | 55.9 -> 55.2 | 55.1 -> 54.1 | 55.4 -> 54.2 | 35.0, 98 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 99 | 20.1 |


## 2026-11-01 11:33 - bistro, single_directional_loops_gpu_culling

As single_directional_loops, with culling classification gpu (GPU-generated draw commands where the device supports them).

- Runs: 1 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash c4294da4554d34d5; results `20261101_113303_bistro_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (9d1b1ef0f-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro.json --no-scene`; scene `pvs_bistro_quest_cell_4m.glb` (listed 4.8 s, idle 5.33 s).
- Configuration calls: set_culling_classification, set_graphics_preset, create_light, set_node_transform, set_graphics_preset (arguments in the result files' `host` block).
- LOD (doc/plans/lod_gpu_selection.md decisions 5 and 6): the last set_graphics_preset sets `lod_enabled` on and `lod_cpu_selection` every_frame; the file stores no LOD chains, so the selection is not active.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (1 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+0, spot 0+0, point 0+0 |
| Shadows | on; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | off (gpu supported) |
| Content draws | all |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 12.77 |
| Frame wall time (ms) | 77.97 |
| M2 / R / M3 (ms) | 0.80 / 3.41 / 0.71 |
| App GPU frame time, XR counter (ms) | 71.63 |
| GPU utilization, XR counter (%) | 99.2 |
| CPU utilization average / worst, XR counter (%) | 7.4 / 9.8 |
| GPU pass Cull classification (ms) | 0.028 |
| GPU pass Cull command generation (ms) | 0.099 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.039 |
| GPU pass Shadow 0 (ms) | 0.000 |
| GPU pass XR multiview (ms) | 76.945 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 77.126 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 12.77 | 0 -> 0; 0 -> 0, step-downs 0 | 59.9 -> 54.8 | 59.0 -> 53.8 | 58.3 -> 54.1 | 33.0, 98 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 99 | 19.8 |


## 2026-11-01 13:15 - bistro_street, single_directional_loops_gpu_culling_lod_off

As single_directional_loops_gpu_culling, PVS and occlusion off, lod_enabled off (threshold 1.0, band 0.25, lod_cpu_selection every_frame set explicitly).

- Runs: 2 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash 1fb5ef2ee72a4f1d; results `20261101_131519_bistro_street_0.json`, `20261101_131519_bistro_street_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (42cb4188d); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street.json --no-scene`; scene `bistro_quest_lod_cell_4m.glb` (listed 4.47 s, idle 4.47 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).
- LOD (doc/plans/lod_gpu_selection.md step 6): the file stores LOD chains (2900 primitives); lod_enabled off, so every entry draws level 0. get_lod_stats in the measured window: 3396 entries, 3120440 triangles drawn, all at level 0 (Content fill opaque 3304 entries 2827665 triangles, alpha tested 91 / 292771, translucent 1 / 4).
- GPU counters: `--gpu-metrics` (ovrgpuprofiler: utilization, pre-clipped polygons / s, % prims trivially rejected, reused vertices / s, vertices shaded / s, % time shading vertices / fragments); no `--perfetto` (no binning numbers): the metavr CLI is not installed on the measuring Mac.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (0 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+0, spot 0+0, point 0+0 |
| Shadows | on; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | off (gpu supported) |
| Content draws | all |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 16.50 [16.20..16.79] |
| Frame wall time (ms) | 60.29 [59.22..61.36] |
| M2 / R / M3 (ms) | 0.81 [0.80..0.81] / 5.21 [5.20..5.21] / 0.71 [0.70..0.71] |
| App GPU frame time, XR counter (ms) | 46.68 [40.78..52.57] |
| GPU utilization, XR counter (%) | 99.0 [99.0..99.0] |
| CPU utilization average / worst, XR counter (%) | 9.5 [8.8..10.2] / 12.2 [11.8..12.6] |
| GPU pass Cull classification (ms) | 0.028 [0.027..0.028] |
| GPU pass Cull command generation (ms) | 0.089 [0.087..0.091] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.041 [0.040..0.042] |
| GPU pass Shadow 0 (ms) | 3.618 [3.200..4.036] |
| GPU pass XR multiview (ms) | 55.659 [54.709..56.608] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 59.480 [58.332..60.627] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 16.20 | 0 -> 0; 0 -> 0, step-downs 0 | 58.7 -> 57.3 | 58.3 -> 56.4 | 57.0 -> 56.1 | 34.0, 98 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 98 | 19.8 |
| 1 | 16.79 | 0 -> 0; 0 -> 0, step-downs 0 | 55.8 -> 55.9 | 55.2 -> 55.1 | 55.0 -> 54.8 | 35.0, 98 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 98 | 19.8 |


## 2026-11-01 13:20 - bistro_street, single_directional_loops_gpu_culling_lod_every_frame

As single_directional_loops_gpu_culling, PVS and occlusion off, lod_enabled on, lod_cpu_selection every_frame, threshold 1.0, band 0.25.

- Runs: 2 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash 1fb5ef2ee72a4f1d; results `20261101_132054_bistro_street_0.json`, `20261101_132054_bistro_street_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (42cb4188d); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street.json --no-scene`; scene `bistro_quest_lod_cell_4m.glb` (listed 4.41 s, idle 4.41 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).
- LOD (doc/plans/lod_gpu_selection.md step 6): lod_enabled on, threshold 1.0 px, band 0.25, lod_cpu_selection every_frame. get_lod_stats in the measured window: 3396 entries, 1905768 triangles drawn; per level entries / triangles 0: 1319 / 1342276, 1: 557 / 375530, 2: 747 / 141289, 3: 454 / 40385, 4: 97 / 3142, 5: 222 / 3146 (Content fill opaque 3304 entries 1617703 triangles, alpha tested 91 / 288061); get_lod_selection: one color column (the headset union view) and one shadow column.
- GPU counters: `--gpu-metrics` (ovrgpuprofiler: utilization, pre-clipped polygons / s, % prims trivially rejected, reused vertices / s, vertices shaded / s, % time shading vertices / fragments); no `--perfetto` (no binning numbers): the metavr CLI is not installed on the measuring Mac.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (0 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+0, spot 0+0, point 0+0 |
| Shadows | on; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | off (gpu supported) |
| Content draws | all |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 18.33 [17.65..19.01] |
| Frame wall time (ms) | 54.43 [52.55..56.31] |
| M2 / R / M3 (ms) | 0.81 [0.80..0.81] / 5.64 [5.62..5.66] / 0.70 [0.70..0.71] |
| App GPU frame time, XR counter (ms) | 42.33 [37.95..46.72] |
| GPU utilization, XR counter (%) | 98.7 [98.7..98.8] |
| CPU utilization average / worst, XR counter (%) | 10.2 [10.0..10.5] / 13.4 [12.9..13.9] |
| GPU pass Cull classification (ms) | 0.053 [0.052..0.053] |
| GPU pass Cull command generation (ms) | 0.119 [0.117..0.121] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.039 |
| GPU pass Shadow 0 (ms) | 2.801 [2.800..2.802] |
| GPU pass XR multiview (ms) | 49.986 [48.131..51.841] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 53.471 [51.585..55.356] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 17.65 | 0 -> 0; 0 -> 0, step-downs 0 | 58.2 -> 58.2 | 57.8 -> 57.1 | 57.5 -> 56.8 | 37.0, 98 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 98 | 21.4 |
| 1 | 19.01 | 0 -> 0; 0 -> 0, step-downs 0 | 57.0 -> 57.6 | 56.3 -> 56.3 | 56.3 -> 56.1 | 37.0, 98 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 98 | 21.4 |


## 2026-11-01 13:24 - bistro_street, single_directional_loops_gpu_culling_lod_on_demand

As single_directional_loops_gpu_culling, PVS and occlusion off, lod_enabled on, lod_cpu_selection on_demand, threshold 1.0, band 0.25.

- Runs: 2 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash 1fb5ef2ee72a4f1d; results `20261101_132449_bistro_street_0.json`, `20261101_132449_bistro_street_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (42cb4188d); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street.json --no-scene`; scene `bistro_quest_lod_cell_4m.glb` (listed 4.01 s, idle 4.34 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).
- LOD (doc/plans/lod_gpu_selection.md step 6): lod_enabled on, threshold 1.0 px, band 0.25, lod_cpu_selection on_demand (its log line confirms the GPU classification selects). get_lod_stats in the measured window: 3396 entries, 1905768 triangles drawn; per level entries / triangles 0: 1319 / 1342276, 1: 557 / 375530, 2: 747 / 141289, 3: 454 / 40385, 4: 97 / 3142, 5: 222 / 3146 (Content fill opaque 3304 entries 1617703 triangles, alpha tested 91 / 288061); get_lod_selection: one color column (the headset union view) and one shadow column. Retired level lag (the log, every 600 frames, both runs): min 2 max 2 mean 2.00 frames.
- GPU counters: `--gpu-metrics` (ovrgpuprofiler: utilization, pre-clipped polygons / s, % prims trivially rejected, reused vertices / s, vertices shaded / s, % time shading vertices / fragments); no `--perfetto` (no binning numbers): the metavr CLI is not installed on the measuring Mac.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (0 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+0, spot 0+0, point 0+0 |
| Shadows | on; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | off (gpu supported) |
| Content draws | all |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 17.75 [17.67..17.83] |
| Frame wall time (ms) | 55.92 [55.78..56.06] |
| M2 / R / M3 (ms) | 0.81 [0.81..0.82] / 5.23 [5.19..5.27] / 0.71 [0.71..0.71] |
| App GPU frame time, XR counter (ms) | 46.93 [36.62..57.25] |
| GPU utilization, XR counter (%) | 98.8 [98.7..98.8] |
| CPU utilization average / worst, XR counter (%) | 9.6 [9.2..10.0] / 12.4 [12.1..12.7] |
| GPU pass Cull classification (ms) | 0.054 [0.053..0.054] |
| GPU pass Cull command generation (ms) | 0.120 [0.115..0.124] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.041 [0.040..0.042] |
| GPU pass Shadow 0 (ms) | 3.332 [2.841..3.823] |
| GPU pass XR multiview (ms) | 51.385 [51.057..51.714] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 55.153 [54.956..55.350] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 17.83 | 0 -> 0; 0 -> 0, step-downs 0 | 56.0 -> 56.8 | 55.7 -> 56.3 | 55.1 -> 55.6 | 37.0, 97 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 98 | 20.6 |
| 1 | 17.67 | 0 -> 0; 0 -> 0, step-downs 0 | 57.3 -> 57.4 | 56.8 -> 56.9 | 56.4 -> 56.5 | 38.0, 97 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 98 | 21.2 |


## 2026-11-01 13:28 - bistro_street, single_directional_loops_gpu_culling_lod_off

As single_directional_loops_gpu_culling, PVS and occlusion off, lod_enabled off (threshold 1.0, band 0.25, lod_cpu_selection every_frame set explicitly).

- Runs: 2 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash 1fb5ef2ee72a4f1d; results `20261101_132837_bistro_street_0.json`, `20261101_132837_bistro_street_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (42cb4188d); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street.json --no-scene`; scene `pvs_bistro_quest_cell_4m.glb` (listed 4.06 s, idle 4.71 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).
- LOD (doc/plans/lod_gpu_selection.md step 6): the reference file without LOD chains (`pvs_bistro_quest_cell_4m.glb`), lod_enabled off. get_lod_stats in the measured window: 3396 entries, 3120440 triangles drawn, all at level 0 (Content fill opaque 3304 entries 2827665 triangles, alpha tested 91 / 292771, translucent 1 / 4).
- GPU counters: `--gpu-metrics` (ovrgpuprofiler: utilization, pre-clipped polygons / s, % prims trivially rejected, reused vertices / s, vertices shaded / s, % time shading vertices / fragments); no `--perfetto` (no binning numbers): the metavr CLI is not installed on the measuring Mac.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (0 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+0, spot 0+0, point 0+0 |
| Shadows | on; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | off (gpu supported) |
| Content draws | all |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 16.06 [16.02..16.10] |
| Frame wall time (ms) | 61.95 [61.79..62.11] |
| M2 / R / M3 (ms) | 0.81 [0.81..0.81] / 5.10 [5.08..5.11] / 0.71 [0.70..0.71] |
| App GPU frame time, XR counter (ms) | 52.64 [52.39..52.88] |
| GPU utilization, XR counter (%) | 99.0 [99.0..99.0] |
| CPU utilization average / worst, XR counter (%) | 9.6 [9.5..9.6] / 11.5 [10.9..12.1] |
| GPU pass Cull classification (ms) | 0.027 [0.027..0.027] |
| GPU pass Cull command generation (ms) | 0.119 [0.116..0.123] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.042 |
| GPU pass Shadow 0 (ms) | 4.126 [4.079..4.173] |
| GPU pass XR multiview (ms) | 57.072 [56.929..57.216] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 61.239 [61.114..61.364] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 16.10 | 0 -> 0; 0 -> 0, step-downs 0 | 56.7 -> 57.7 | 56.3 -> 56.6 | 55.9 -> 56.2 | 38.0, 97 % | cpu 0 1: 1920 cap 1382/2054; cpu 2 3 4 5: 1651 [1651..1920] cap 1651/2362 | 456 [456..492] cap 456 | 98 | 20.0 |
| 1 | 16.02 | 0 -> 0; 0 -> 0, step-downs 0 | 57.3 -> 57.6 | 57.0 -> 57.0 | 56.6 -> 56.7 | 38.0, 97 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 98 | 21.2 |


## 2026-11-01 13:32 - bistro_street, flat_draw_color_gpu_culling_lod_off

Flat draw color, no lights, no shadows, culling classification gpu, PVS and occlusion off, lod_enabled off (threshold 1.0, band 0.25, lod_cpu_selection every_frame set explicitly).

- Runs: 2 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash 1fb5ef2ee72a4f1d; results `20261101_133251_bistro_street_0.json`, `20261101_133251_bistro_street_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (42cb4188d); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street.json --no-scene`; scene `bistro_quest_lod_cell_4m.glb` (listed 4.43 s, idle 4.43 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset (arguments in the result files' `host` block).
- LOD (doc/plans/lod_gpu_selection.md step 6): the geometry-only frame (flat draw color, no lights, no shadow pass) on the file with LOD chains, lod_enabled off. get_lod_stats in the measured window: 3396 entries, 3120440 triangles drawn, all at level 0 (Content fill opaque 3304 entries 2827665 triangles, alpha tested 91 / 292771, translucent 1 / 4).
- GPU counters: `--gpu-metrics` (ovrgpuprofiler: utilization, pre-clipped polygons / s, % prims trivially rejected, reused vertices / s, vertices shaded / s, % time shading vertices / fragments); no `--perfetto` (no binning numbers): the metavr CLI is not installed on the measuring Mac.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 0+0, spot 0+0, point 0+0 |
| Shadows | off; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | off (gpu supported) |
| Content draws | all |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 21.39 [21.37..21.41] |
| Frame wall time (ms) | 46.67 [46.63..46.71] |
| M2 / R / M3 (ms) | 0.82 [0.81..0.83] / 3.33 [3.30..3.36] / 0.71 [0.71..0.72] |
| App GPU frame time, XR counter (ms) | 39.52 [39.19..39.84] |
| GPU utilization, XR counter (%) | 99.1 [99.0..99.1] |
| CPU utilization average / worst, XR counter (%) | 9.0 [8.6..9.3] / 10.7 [10.3..11.0] |
| GPU pass Cull classification (ms) | 0.023 [0.023..0.023] |
| GPU pass Cull command generation (ms) | 0.077 [0.077..0.078] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.039 [0.039..0.039] |
| GPU pass XR multiview (ms) | 45.297 [45.282..45.311] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 45.453 [45.435..45.471] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 21.41 | 0 -> 0; 0 -> 0, step-downs 0 | 56.1 -> 56.6 | 55.3 -> 55.6 | 55.4 -> 55.6 | 38.0, 97 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 99 | 21.3 |
| 1 | 21.37 | 0 -> 0; 0 -> 0, step-downs 0 | 56.9 -> 57.7 | 56.0 -> 56.1 | 56.2 -> 56.2 | 38.0, 97 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 99 | 20.2 |


## 2026-11-01 13:35 - bistro_street, flat_draw_color_gpu_culling_lod_every_frame

Flat draw color, no lights, no shadows, culling classification gpu, PVS and occlusion off, lod_enabled on, lod_cpu_selection every_frame, threshold 1.0, band 0.25.

- Runs: 2 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash 1fb5ef2ee72a4f1d; results `20261101_133555_bistro_street_0.json`, `20261101_133555_bistro_street_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (42cb4188d); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street.json --no-scene`; scene `bistro_quest_lod_cell_4m.glb` (listed 4.05 s, idle 4.05 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset (arguments in the result files' `host` block).
- LOD (doc/plans/lod_gpu_selection.md step 6): the geometry-only frame (flat draw color, no lights, no shadow pass), lod_enabled on, threshold 1.0 px, band 0.25, lod_cpu_selection every_frame. get_lod_stats in the measured window: 3396 entries, 1905768 triangles drawn; per level entries / triangles 0: 1319 / 1342276, 1: 557 / 375530, 2: 747 / 141289, 3: 454 / 40385, 4: 97 / 3142, 5: 222 / 3146 (Content fill opaque 3304 entries 1617703 triangles, alpha tested 91 / 288061).
- GPU counters: `--gpu-metrics` (ovrgpuprofiler: utilization, pre-clipped polygons / s, % prims trivially rejected, reused vertices / s, vertices shaded / s, % time shading vertices / fragments); no `--perfetto` (no binning numbers): the metavr CLI is not installed on the measuring Mac.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 0+0, spot 0+0, point 0+0 |
| Shadows | off; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | off (gpu supported) |
| Content draws | all |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 23.96 [23.87..24.05] |
| Frame wall time (ms) | 41.53 [41.35..41.70] |
| M2 / R / M3 (ms) | 0.79 [0.78..0.80] / 3.38 [3.38..3.38] / 0.71 [0.71..0.71] |
| App GPU frame time, XR counter (ms) | 37.88 [35.38..40.38] |
| GPU utilization, XR counter (%) | 99.0 [99.0..99.0] |
| CPU utilization average / worst, XR counter (%) | 9.6 [9.5..9.6] / 12.0 [11.3..12.6] |
| GPU pass Cull classification (ms) | 0.047 [0.046..0.047] |
| GPU pass Cull command generation (ms) | 0.107 [0.106..0.109] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.039 [0.039..0.040] |
| GPU pass XR multiview (ms) | 40.597 [40.415..40.779] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 40.839 [40.652..41.026] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 24.05 | 0 -> 0; 0 -> 0, step-downs 0 | 57.6 -> 58.0 | 56.7 -> 56.6 | 56.8 -> 56.7 | 39.0, 97 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 98 | 21.4 |
| 1 | 23.87 | 0 -> 0; 0 -> 0, step-downs 0 | 57.7 -> 57.7 | 56.7 -> 56.6 | 56.9 -> 56.8 | 39.0, 97 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 99 | 20.1 |

