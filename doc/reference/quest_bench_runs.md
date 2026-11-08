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


## 2026-11-01 14:22 - bistro_street, single_directional_loops_gpu_culling_lod_every_frame

As single_directional_loops_gpu_culling, PVS off, occlusion on, lod_enabled on, lod_cpu_selection every_frame, threshold 1.0, band 0.25.

- Runs: 2 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash 1fb5ef2ee72a4f1d; results `20261101_142208_bistro_street_0.json`, `20261101_142208_bistro_street_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (528047df3); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street.json --no-scene`; scene `bistro_quest_lod_cell_4m.glb` (listed 4.44 s, idle 4.44 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).
- LOD and occlusion (doc/plans/lod_gpu_selection.md step 6, occlusion on): lod_enabled on, threshold 1.0 px, band 0.25, lod_cpu_selection every_frame; occlusion culling on, PVS off. get_occlusion_check (headset view, per run): 3054 of 3397 and 3053 of 3397 in-frustum entries occluded. get_lod_stats: 344 entries, 732347 triangles drawn; per level entries / triangles 0: 252 / 690556, 1: 34 / 30768, 2: 42 / 9419, 3: 16 / 1604.
- GPU counters: `--gpu-metrics` (ovrgpuprofiler); no `--perfetto` (no binning numbers): the metavr CLI is not installed on the measuring Mac.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (0 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
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
| Frames per second | 30.69 [30.52..30.87] |
| Frame wall time (ms) | 32.31 [32.11..32.51] |
| M2 / R / M3 (ms) | 0.80 [0.79..0.81] / 5.60 [5.56..5.64] / 0.69 [0.69..0.70] |
| App GPU frame time, XR counter (ms) | 27.50 [27.37..27.63] |
| GPU utilization, XR counter (%) | 98.4 [98.4..98.4] |
| CPU utilization average / worst, XR counter (%) | 12.0 [11.9..12.2] / 16.1 [16.0..16.2] |
| GPU pass Cull classification (ms) | 0.055 [0.055..0.055] |
| GPU pass Cull command generation (ms) | 0.097 [0.097..0.098] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.039 [0.039..0.039] |
| GPU pass Shadow 0 (ms) | 2.766 [2.755..2.777] |
| GPU pass XR multiview (ms) | 27.996 [27.874..28.117] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 31.366 [31.144..31.589] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 30.87 | 0 -> 0; 0 -> 0, step-downs 0 | 55.7 -> 60.4 | 55.8 -> 59.6 | 53.1 -> 57.2 | 32.0, 97 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 [1651..2362] cap 1651/2362 | 456 cap 456 | 98 | 23.0 |
| 1 | 30.52 | 0 -> 0; 0 -> 0, step-downs 0 | 58.2 -> 58.2 | 58.1 -> 57.9 | 56.5 -> 56.5 | 34.0, 96 % | cpu 0 1: 1920 [1382..1920] cap 1382/2054; cpu 2 3 4 5: 1651 [1651..2362] cap 1651/2362 | 456 [456..492] cap 456 | 98 | 23.7 |


## 2026-11-01 14:24 - bistro_street, single_directional_loops_gpu_culling_lod_on_demand

As single_directional_loops_gpu_culling, PVS off, occlusion on, lod_enabled on, lod_cpu_selection on_demand, threshold 1.0, band 0.25.

- Runs: 2 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash 1fb5ef2ee72a4f1d; results `20261101_142420_bistro_street_0.json`, `20261101_142420_bistro_street_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (528047df3); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street.json --no-scene`; scene `bistro_quest_lod_cell_4m.glb` (listed 4.05 s, idle 4.05 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).
- LOD and occlusion (doc/plans/lod_gpu_selection.md step 6, occlusion on): lod_enabled on, threshold 1.0 px, band 0.25, lod_cpu_selection on_demand; occlusion culling on, PVS off. get_occlusion_check (headset view, per run): 3053 of 3397 and 3053 of 3397 in-frustum entries occluded. get_lod_stats: 344 entries, 732347 triangles drawn; per level entries / triangles 0: 252 / 690556, 1: 34 / 30768, 2: 42 / 9419, 3: 16 / 1604. Retired level lag (the log, every 600 frames, both runs): min 2 max 2 mean 2.00 frames.
- GPU counters: `--gpu-metrics` (ovrgpuprofiler); no `--perfetto` (no binning numbers): the metavr CLI is not installed on the measuring Mac.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (0 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
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
| Frames per second | 30.56 [30.45..30.67] |
| Frame wall time (ms) | 32.40 [32.32..32.47] |
| M2 / R / M3 (ms) | 0.80 [0.79..0.80] / 5.19 [5.18..5.20] / 0.70 [0.69..0.70] |
| App GPU frame time, XR counter (ms) | 27.21 [27.16..27.26] |
| GPU utilization, XR counter (%) | 98.6 [98.5..98.7] |
| CPU utilization average / worst, XR counter (%) | 12.5 [11.4..13.5] / 15.9 [15.7..16.2] |
| GPU pass Cull classification (ms) | 0.056 [0.056..0.057] |
| GPU pass Cull command generation (ms) | 0.094 [0.093..0.096] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.039 [0.039..0.039] |
| GPU pass Shadow 0 (ms) | 2.769 [2.759..2.779] |
| GPU pass XR multiview (ms) | 28.114 [28.019..28.209] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 31.511 [31.261..31.761] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 30.45 | 0 -> 0; 0 -> 0, step-downs 0 | 58.7 -> 59.0 | 59.0 -> 59.1 | 57.4 -> 57.6 | 35.0, 96 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 98 | 23.8 |
| 1 | 30.67 | 0 -> 0; 0 -> 0, step-downs 0 | 59.6 -> 59.7 | 59.8 -> 59.9 | 58.3 -> 58.4 | 36.0, 96 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 [1651..2362] cap 1651/2362 | 456 cap 456 | 98 | 23.0 |


## 2026-11-01 14:26 - bistro_street, flat_draw_color_gpu_culling_lod_every_frame

Flat draw color, no lights, no shadows, culling classification gpu, PVS off, occlusion on, lod_enabled on, lod_cpu_selection every_frame, threshold 1.0, band 0.25.

- Runs: 2 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash 1fb5ef2ee72a4f1d; results `20261101_142651_bistro_street_0.json`, `20261101_142651_bistro_street_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (528047df3); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street.json --no-scene`; scene `bistro_quest_lod_cell_4m.glb` (listed 4.38 s, idle 4.38 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset (arguments in the result files' `host` block).
- LOD and occlusion (doc/plans/lod_gpu_selection.md step 6, occlusion on): lod_enabled on, threshold 1.0 px, band 0.25, lod_cpu_selection every_frame; occlusion culling on, PVS off. get_occlusion_check (headset view, per run): 3038 of 3378 and 3035 of 3375 in-frustum entries occluded. get_lod_stats: 340 entries, 730242 triangles drawn; per level entries / triangles 0: 249 / 688565, 1: 34 / 30768, 2: 41 / 9305, 3: 16 / 1604.
- GPU counters: `--gpu-metrics` (ovrgpuprofiler); no `--perfetto` (no binning numbers): the metavr CLI is not installed on the measuring Mac.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 0+0, spot 0+0, point 0+0 |
| Shadows | off; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | on (gpu supported) |
| Content draws | all |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 59.00 [58.57..59.43] |
| Frame wall time (ms) | 16.84 [16.73..16.95] |
| M2 / R / M3 (ms) | 0.83 [0.83..0.83] / 3.72 [3.68..3.75] / 0.79 [0.79..0.80] |
| App GPU frame time, XR counter (ms) | 13.92 [13.85..13.98] |
| GPU utilization, XR counter (%) | 97.5 [97.4..97.5] |
| CPU utilization average / worst, XR counter (%) | 16.8 [16.5..17.2] / 20.4 [20.0..20.8] |
| GPU pass Cull classification (ms) | 0.048 [0.048..0.049] |
| GPU pass Cull command generation (ms) | 0.076 [0.071..0.081] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.039 [0.039..0.039] |
| GPU pass XR multiview (ms) | 15.713 [15.694..15.733] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 15.908 [15.908..15.909] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 59.43 | 0 -> 0; 0 -> 0, step-downs 0 | 56.4 -> 57.5 | 55.5 -> 55.9 | 55.0 -> 55.4 | 36.0, 96 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2362 [1920..2362] cap 2362/2362 | 456 cap 456 | 97 | 24.7 |
| 1 | 58.57 | 0 -> 0; 0 -> 0, step-downs 0 | 58.2 -> 59.0 | 57.5 -> 57.5 | 57.0 -> 57.0 | 36.0, 96 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2054 [2054..2362] cap 2362/2362 | 456 cap 456 | 97 | 26.4 |


## 2026-11-01 14:28 - bistro_street, flat_draw_color_gpu_culling_lod_on_demand

Flat draw color, no lights, no shadows, culling classification gpu, PVS off, occlusion on, lod_enabled on, lod_cpu_selection on_demand, threshold 1.0, band 0.25.

- Runs: 2 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash 1fb5ef2ee72a4f1d; results `20261101_142809_bistro_street_0.json`, `20261101_142809_bistro_street_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (528047df3); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street.json --no-scene`; scene `bistro_quest_lod_cell_4m.glb` (listed 4.07 s, idle 4.07 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset (arguments in the result files' `host` block).
- LOD and occlusion (doc/plans/lod_gpu_selection.md step 6, occlusion on): lod_enabled on, threshold 1.0 px, band 0.25, lod_cpu_selection on_demand; occlusion culling on, PVS off. get_occlusion_check (headset view, per run): 3035 of 3375 and 3035 of 3375 in-frustum entries occluded. get_lod_stats: 340 entries, 730242 triangles drawn; per level entries / triangles 0: 249 / 688565, 1: 34 / 30768, 2: 41 / 9305, 3: 16 / 1604. Retired level lag (the log, every 600 frames, both runs): min 2 max 2 mean 2.00 frames.
- GPU counters: `--gpu-metrics` (ovrgpuprofiler); no `--perfetto` (no binning numbers): the metavr CLI is not installed on the measuring Mac.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 0+0, spot 0+0, point 0+0 |
| Shadows | off; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | on (gpu supported) |
| Content draws | all |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 58.74 [58.61..58.88] |
| Frame wall time (ms) | 16.84 [16.73..16.96] |
| M2 / R / M3 (ms) | 0.84 [0.84..0.84] / 3.49 [3.46..3.52] / 0.77 [0.77..0.78] |
| App GPU frame time, XR counter (ms) | 13.96 [13.85..14.07] |
| GPU utilization, XR counter (%) | 97.4 [97.2..97.5] |
| CPU utilization average / worst, XR counter (%) | 16.3 [15.5..17.1] / 19.9 [19.0..20.8] |
| GPU pass Cull classification (ms) | 0.048 [0.048..0.048] |
| GPU pass Cull command generation (ms) | 0.074 [0.070..0.078] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.039 [0.039..0.039] |
| GPU pass XR multiview (ms) | 15.683 [15.624..15.741] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 15.869 [15.797..15.941] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 58.88 | 0 -> 0; 0 -> 0, step-downs 0 | 59.1 -> 59.3 | 58.3 -> 58.3 | 57.9 -> 57.8 | 37.0, 96 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2054 [1920..2362] cap 2362/2362 | 456 cap 456 | 97 | 25.7 |
| 1 | 58.61 | 0 -> 0; 0 -> 0, step-downs 0 | 59.6 -> 59.9 | 58.8 -> 58.7 | 58.4 -> 58.3 | 37.0, 96 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1920 [1920..2362] cap 2362/2362 | 456 cap 456 | 97 | 24.1 |


## 2026-11-01 14:29 - bistro_street, single_directional_loops_gpu_culling_lod_every_frame_t4

As single_directional_loops_gpu_culling, PVS off, occlusion on, lod_enabled on, lod_cpu_selection every_frame, threshold 4.0, band 0.25.

- Runs: 2 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash 1fb5ef2ee72a4f1d; results `20261101_142940_bistro_street_0.json`, `20261101_142940_bistro_street_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (528047df3); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street.json --no-scene`; scene `bistro_quest_lod_cell_4m.glb` (listed 4.38 s, idle 4.38 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).
- LOD and occlusion (doc/plans/lod_gpu_selection.md step 6, occlusion on): lod_enabled on, threshold 4.0 px, band 0.25, lod_cpu_selection every_frame; occlusion culling on, PVS off. get_occlusion_check (headset view, per run): 3055 of 3397 and 3055 of 3397 in-frustum entries occluded. get_lod_stats: 344 entries, 511567 triangles drawn; per level entries / triangles 0: 209 / 319120, 1: 54 / 163459, 2: 25 / 19750, 3: 27 / 8684, 4: 24 / 504, 5: 5 / 50.
- GPU counters: `--gpu-metrics` (ovrgpuprofiler); no `--perfetto` (no binning numbers): the metavr CLI is not installed on the measuring Mac.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (0 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
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
| Frames per second | 33.65 [33.55..33.75] |
| Frame wall time (ms) | 29.44 [29.34..29.55] |
| M2 / R / M3 (ms) | 0.80 [0.79..0.82] / 5.61 [5.56..5.66] / 0.70 [0.70..0.70] |
| App GPU frame time, XR counter (ms) | 26.26 [24.98..27.54] |
| GPU utilization, XR counter (%) | 98.2 [98.2..98.2] |
| CPU utilization average / worst, XR counter (%) | 14.0 [13.9..14.1] / 17.9 [17.2..18.6] |
| GPU pass Cull classification (ms) | 0.056 [0.055..0.056] |
| GPU pass Cull command generation (ms) | 0.094 [0.092..0.095] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.039 [0.039..0.039] |
| GPU pass Shadow 0 (ms) | 2.223 [2.222..2.223] |
| GPU pass XR multiview (ms) | 26.020 [25.954..26.086] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 28.558 [28.493..28.623] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 33.75 | 0 -> 0; 0 -> 0, step-downs 0 | 59.3 -> 60.3 | 59.7 -> 60.0 | 58.0 -> 58.5 | 37.0, 96 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 [1651..2362] cap 1651/2362 | 456 cap 456 | 98 | 22.3 |
| 1 | 33.55 | 0 -> 0; 0 -> 0, step-downs 0 | 60.8 -> 61.0 | 61.1 -> 61.1 | 59.5 -> 59.6 | 38.0, 96 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 [1651..2362] cap 1651/2362 | 456 cap 456 | 98 | 23.3 |


## 2026-11-01 14:31 - bistro_street, single_directional_loops_gpu_culling_lod_on_demand_t4

As single_directional_loops_gpu_culling, PVS off, occlusion on, lod_enabled on, lod_cpu_selection on_demand, threshold 4.0, band 0.25.

- Runs: 2 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash 1fb5ef2ee72a4f1d; results `20261101_143142_bistro_street_0.json`, `20261101_143142_bistro_street_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (528047df3); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street.json --no-scene`; scene `bistro_quest_lod_cell_4m.glb` (listed 4.04 s, idle 4.38 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).
- LOD and occlusion (doc/plans/lod_gpu_selection.md step 6, occlusion on): lod_enabled on, threshold 4.0 px, band 0.25, lod_cpu_selection on_demand; occlusion culling on, PVS off. get_occlusion_check (headset view, per run): 3055 of 3397 and 3055 of 3397 in-frustum entries occluded. get_lod_stats: 344 entries, 511567 triangles drawn; per level entries / triangles 0: 209 / 319120, 1: 54 / 163459, 2: 25 / 19750, 3: 27 / 8684, 4: 24 / 504, 5: 5 / 50. Retired level lag (the log, every 600 frames, both runs): min 2 max 2 mean 2.00 frames.
- GPU counters: `--gpu-metrics` (ovrgpuprofiler); no `--perfetto` (no binning numbers): the metavr CLI is not installed on the measuring Mac.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (0 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
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
| Frames per second | 33.49 [33.41..33.58] |
| Frame wall time (ms) | 29.61 [29.58..29.63] |
| M2 / R / M3 (ms) | 0.81 [0.80..0.81] / 5.17 [5.16..5.18] / 0.70 [0.70..0.70] |
| App GPU frame time, XR counter (ms) | 24.75 [24.66..24.83] |
| GPU utilization, XR counter (%) | 98.2 [98.2..98.3] |
| CPU utilization average / worst, XR counter (%) | 12.6 [12.4..12.8] / 17.6 [17.5..17.6] |
| GPU pass Cull classification (ms) | 0.056 [0.056..0.057] |
| GPU pass Cull command generation (ms) | 0.095 [0.094..0.096] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.039 |
| GPU pass Shadow 0 (ms) | 2.243 [2.239..2.247] |
| GPU pass XR multiview (ms) | 26.109 [26.008..26.211] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 28.681 [28.608..28.754] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 33.41 | 0 -> 0; 0 -> 0, step-downs 0 | 61.6 -> 62.0 | 61.9 -> 61.6 | 60.4 -> 60.3 | 38.0, 96 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 [1651..2208] cap 1651/2362 | 456 cap 456 | 98 | 22.6 |
| 1 | 33.58 | 0 -> 0; 0 -> 0, step-downs 0 | 62.0 -> 62.2 | 62.2 -> 62.0 | 60.8 -> 60.7 | 39.0, 96 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 [1651..2362] cap 1651/2362 | 456 cap 456 | 98 | 22.7 |


## 2026-11-01 14:33 - bistro_street, flat_draw_color_gpu_culling_lod_every_frame_t4

Flat draw color, no lights, no shadows, culling classification gpu, PVS off, occlusion on, lod_enabled on, lod_cpu_selection every_frame, threshold 4.0, band 0.25.

- Runs: 2 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash 1fb5ef2ee72a4f1d; results `20261101_143353_bistro_street_0.json`, `20261101_143353_bistro_street_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (528047df3); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street.json --no-scene`; scene `bistro_quest_lod_cell_4m.glb` (listed 4.4 s, idle 4.4 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset (arguments in the result files' `host` block).
- LOD and occlusion (doc/plans/lod_gpu_selection.md step 6, occlusion on): lod_enabled on, threshold 4.0 px, band 0.25, lod_cpu_selection every_frame; occlusion culling on, PVS off. get_occlusion_check (headset view, per run): 3032 of 3373 and 3032 of 3373 in-frustum entries occluded. get_lod_stats: 341 entries, 509664 triangles drawn; per level entries / triangles 0: 207 / 317273, 1: 54 / 163459, 2: 25 / 19750, 3: 26 / 8628, 4: 24 / 504, 5: 5 / 50.
- GPU counters: `--gpu-metrics` (ovrgpuprofiler); no `--perfetto` (no binning numbers): the metavr CLI is not installed on the measuring Mac.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 0+0, spot 0+0, point 0+0 |
| Shadows | off; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | on (gpu supported) |
| Content draws | all |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 66.04 [65.78..66.30] |
| Frame wall time (ms) | 14.94 [14.91..14.96] |
| M2 / R / M3 (ms) | 0.84 [0.83..0.85] / 3.68 [3.65..3.71] / 0.79 [0.79..0.79] |
| App GPU frame time, XR counter (ms) | 12.75 [12.37..13.13] |
| GPU utilization, XR counter (%) | 96.6 [96.5..96.8] |
| CPU utilization average / worst, XR counter (%) | 18.4 [18.2..18.6] / 22.1 [20.0..24.2] |
| GPU pass Cull classification (ms) | 0.048 [0.047..0.048] |
| GPU pass Cull command generation (ms) | 0.075 [0.074..0.075] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.039 [0.039..0.039] |
| GPU pass XR multiview (ms) | 14.001 [13.973..14.028] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 14.191 [14.150..14.231] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 65.78 | 0 -> 0; 0 -> 0, step-downs 0 | 59.9 -> 60.4 | 59.2 -> 59.3 | 58.7 -> 58.9 | 39.0, 96 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2362 [1517..2362] cap 2362/2362 | 456 cap 456 | 96 | 26.5 |
| 1 | 66.30 | 0 -> 0; 0 -> 0, step-downs 0 | 60.8 -> 61.1 | 60.0 -> 60.0 | 59.7 -> 59.6 | 39.0, 96 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2054 [1920..2362] cap 2362/2362 | 456 cap 456 | 96 | 26.2 |


## 2026-11-01 14:35 - bistro_street, flat_draw_color_gpu_culling_lod_on_demand_t4

Flat draw color, no lights, no shadows, culling classification gpu, PVS off, occlusion on, lod_enabled on, lod_cpu_selection on_demand, threshold 4.0, band 0.25.

- Runs: 2 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash 1fb5ef2ee72a4f1d; results `20261101_143508_bistro_street_0.json`, `20261101_143508_bistro_street_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (528047df3); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street.json --no-scene`; scene `bistro_quest_lod_cell_4m.glb` (listed 4.46 s, idle 4.46 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset (arguments in the result files' `host` block).
- LOD and occlusion (doc/plans/lod_gpu_selection.md step 6, occlusion on): lod_enabled on, threshold 4.0 px, band 0.25, lod_cpu_selection on_demand; occlusion culling on, PVS off. get_occlusion_check (headset view, per run): 3034 of 3374 and 3033 of 3374 in-frustum entries occluded. get_lod_stats: 340 entries, 509520 triangles drawn; per level entries / triangles 0: 206 / 317129, 1: 54 / 163459, 2: 25 / 19750, 3: 26 / 8628, 4: 24 / 504, 5: 5 / 50. Retired level lag (the log, every 600 frames, both runs): min 2 max 2 mean 2.00 frames.
- GPU counters: `--gpu-metrics` (ovrgpuprofiler); no `--perfetto` (no binning numbers): the metavr CLI is not installed on the measuring Mac.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 0+0, spot 0+0, point 0+0 |
| Shadows | off; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | on (gpu supported) |
| Content draws | all |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 66.12 [65.70..66.55] |
| Frame wall time (ms) | 14.93 [14.83..15.02] |
| M2 / R / M3 (ms) | 0.84 [0.84..0.85] / 3.52 [3.50..3.55] / 0.79 [0.78..0.79] |
| App GPU frame time, XR counter (ms) | 12.29 [12.22..12.36] |
| GPU utilization, XR counter (%) | 96.7 [96.2..97.2] |
| CPU utilization average / worst, XR counter (%) | 17.1 [16.4..17.8] / 22.0 [21.0..23.1] |
| GPU pass Cull classification (ms) | 0.049 [0.048..0.049] |
| GPU pass Cull command generation (ms) | 0.078 [0.077..0.078] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.039 [0.039..0.039] |
| GPU pass XR multiview (ms) | 13.982 [13.937..14.028] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 14.158 [14.111..14.205] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 66.55 | 0 -> 0; 0 -> 0, step-downs 0 | 61.3 -> 61.8 | 60.4 -> 60.3 | 60.1 -> 59.9 | 40.0, 95 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2054 [1382..2362] cap 2362/2362 | 456 cap 456 | 97 | 25.9 |
| 1 | 65.70 | 0 -> 0; 0 -> 0, step-downs 0 | 61.3 -> 62.0 | 60.7 -> 60.5 | 60.3 -> 60.2 | 40.0, 95 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2208 [1920..2362] cap 2362/2362 | 456 cap 456 | 96 | 26.6 |


## 2026-11-01 14:36 - bistro_street, single_directional_loops_gpu_culling_lod_off

As single_directional_loops_gpu_culling, PVS off, occlusion on, lod_enabled off (threshold 1.0, band 0.25, lod_cpu_selection every_frame set explicitly).

- Runs: 2 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash 1fb5ef2ee72a4f1d; results `20261101_143629_bistro_street_0.json`, `20261101_143629_bistro_street_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (528047df3); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street.json --no-scene`; scene `bistro_quest_lod_cell_4m.glb` (listed 4.01 s, idle 4.33 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).
- LOD and occlusion (doc/plans/lod_gpu_selection.md step 6, occlusion on): lod_enabled off (the same-APK baseline of the LOD rows), every entry at level 0; occlusion culling on, PVS off. get_occlusion_check (headset view, per run): 3053 of 3397 and 3053 of 3397 in-frustum entries occluded. get_lod_stats: 344 entries, 802671 triangles drawn; per level entries / triangles 0: 344 / 802671.
- GPU counters: `--gpu-metrics` (ovrgpuprofiler); no `--perfetto` (no binning numbers): the metavr CLI is not installed on the measuring Mac.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (0 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
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
| Frames per second | 29.43 [29.36..29.50] |
| Frame wall time (ms) | 33.63 [33.51..33.74] |
| M2 / R / M3 (ms) | 0.82 [0.82..0.82] / 5.23 [5.21..5.25] / 0.71 [0.71..0.71] |
| App GPU frame time, XR counter (ms) | 28.34 [28.27..28.41] |
| GPU utilization, XR counter (%) | 98.5 [98.5..98.6] |
| CPU utilization average / worst, XR counter (%) | 11.6 [11.4..11.9] / 16.5 [16.3..16.7] |
| GPU pass Cull classification (ms) | 0.032 [0.032..0.032] |
| GPU pass Cull command generation (ms) | 0.075 [0.073..0.076] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 [0.039..0.040] |
| GPU pass Shadow 0 (ms) | 3.203 [3.184..3.221] |
| GPU pass XR multiview (ms) | 28.857 [28.802..28.912] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 32.901 [32.847..32.956] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 29.36 | 0 -> 0; 0 -> 0, step-downs 0 | 60.9 -> 61.9 | 61.2 -> 61.1 | 59.9 -> 60.1 | 40.0, 95 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 98 | 23.1 |
| 1 | 29.50 | 0 -> 0; 0 -> 0, step-downs 0 | 61.8 -> 62.6 | 62.0 -> 61.8 | 60.8 -> 60.7 | 40.0, 95 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 cap 1651/2362 | 456 cap 456 | 98 | 23.1 |


## 2026-11-01 14:38 - bistro_street, flat_draw_color_gpu_culling_lod_off

Flat draw color, no lights, no shadows, culling classification gpu, PVS off, occlusion on, lod_enabled off (threshold 1.0, band 0.25, lod_cpu_selection every_frame set explicitly).

- Runs: 2 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash 1fb5ef2ee72a4f1d; results `20261101_143846_bistro_street_0.json`, `20261101_143846_bistro_street_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (528047df3); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street.json --no-scene`; scene `bistro_quest_lod_cell_4m.glb` (listed 4.0 s, idle 4.0 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset (arguments in the result files' `host` block).
- LOD and occlusion (doc/plans/lod_gpu_selection.md step 6, occlusion on): lod_enabled off (the same-APK baseline of the LOD rows), every entry at level 0; occlusion culling on, PVS off. get_occlusion_check (headset view, per run): 3048 of 3388 and 3048 of 3388 in-frustum entries occluded. get_lod_stats: 340 entries, 800224 triangles drawn; per level entries / triangles 0: 340 / 800224.
- GPU counters: `--gpu-metrics` (ovrgpuprofiler); no `--perfetto` (no binning numbers): the metavr CLI is not installed on the measuring Mac.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 0+0, spot 0+0, point 0+0 |
| Shadows | off; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | on (gpu supported) |
| Content draws | all |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 57.34 [57.16..57.52] |
| Frame wall time (ms) | 17.41 [17.37..17.44] |
| M2 / R / M3 (ms) | 0.84 [0.84..0.84] / 3.58 [3.57..3.58] / 0.80 [0.79..0.80] |
| App GPU frame time, XR counter (ms) | 14.27 [14.21..14.33] |
| GPU utilization, XR counter (%) | 97.6 [97.6..97.7] |
| CPU utilization average / worst, XR counter (%) | 15.4 [15.0..15.7] / 19.3 [18.6..20.0] |
| GPU pass Cull classification (ms) | 0.026 [0.026..0.026] |
| GPU pass Cull command generation (ms) | 0.054 [0.053..0.055] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.039 [0.039..0.039] |
| GPU pass XR multiview (ms) | 16.103 [16.076..16.130] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 16.240 [16.209..16.270] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 57.16 | 0 -> 0; 0 -> 0, step-downs 0 | 61.9 -> 62.2 | 61.1 -> 60.9 | 61.0 -> 60.7 | 40.0, 95 % | cpu 0 1: 1920 [1382..1920] cap 1382/2054; cpu 2 3 4 5: 1987 [1920..2362] cap 2362/2362 | 456 [456..492] cap 456 | 97 | 27.7 |
| 1 | 57.52 | 0 -> 0; 0 -> 0, step-downs 0 | 61.9 -> 62.1 | 61.1 -> 60.9 | 60.9 -> 60.6 | 40.0, 95 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2054 [2054..2208] cap 2362/2362 | 456 cap 456 | 97 | 24.5 |


## 2026-11-06 12:28 - bistro_street, single_directional_loops_gpu_culling_lod_on_demand_t4

As single_directional_loops_gpu_culling, PVS off, occlusion on, lod_enabled on, lod_cpu_selection on_demand, threshold 4.0, band 0.25.

- Runs: 2 x 600 measured frames (warmup 1500), display 72.0 Hz, input_hash 1fb5ef2ee72a4f1d; results `20261106_122827_bistro_street_0.json`, `20261106_122827_bistro_street_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (ee49055dd-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street.json --no-scene`; scene `bistro_quest_lod_cell_4m_v4_ws.glb` (listed 5.32 s, idle 5.32 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (0 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
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
| Frames per second | 72.00 [72.00..72.00] |
| Frame wall time (ms) | 13.89 [13.89..13.89] |
| M2 / R / M3 (ms) | 0.81 [0.79..0.84] / 5.00 [4.98..5.01] / 0.75 [0.74..0.75] |
| App GPU frame time, XR counter (ms) | 8.13 [8.13..8.14] |
| GPU utilization, XR counter (%) | 74.6 [74.5..74.7] |
| CPU utilization average / worst, XR counter (%) | 21.5 [20.9..22.2] / 28.4 [26.5..30.3] |
| GPU pass Cull classification (ms) | 0.054 [0.052..0.056] |
| GPU pass Cull command generation (ms) | 0.095 [0.094..0.096] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.044 [0.043..0.044] |
| GPU pass Shadow 0 (ms) | 2.682 [2.663..2.700] |
| GPU pass XR multiview (ms) | 7.243 [7.235..7.252] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 10.182 [10.158..10.205] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 72.00 | 0 -> 0; 0 -> 0, step-downs 0 | 54.1 -> 56.2 | 53.7 -> 55.6 | 50.9 -> 52.9 | 33.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2362 [1920..2362] cap 2362/2362 | 456 cap 456 | 74 | 29.9 |
| 1 | 72.00 | 0 -> 0; 0 -> 0, step-downs 0 | 56.8 -> 57.0 | 56.4 -> 56.3 | 54.7 -> 54.7 | 35.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2141 [1920..2362] cap 2362/2362 | 456 cap 456 | 74 | 29.5 |


## 2026-11-06 12:40 - bistro_street_120, single_directional_loops_gpu_culling_lod_on_demand_t4

As single_directional_loops_gpu_culling, PVS off, occlusion on, lod_enabled on, lod_cpu_selection on_demand, threshold 4.0, band 0.25.

- Runs: 2 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261106_124023_bistro_street_120_0.json`, `20261106_124023_bistro_street_120_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (ee49055dd-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street_120.json --no-scene`; scene `bistro_quest_lod_cell_4m_v4_ws.glb` (listed 4.7 s, idle 4.7 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (0 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
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
| Frames per second | 87.08 [87.03..87.13] |
| Frame wall time (ms) | 11.15 [11.09..11.20] |
| M2 / R / M3 (ms) | 0.80 [0.79..0.80] / 4.50 [4.46..4.54] / 0.71 [0.71..0.71] |
| App GPU frame time, XR counter (ms) | 8.14 [8.12..8.15] |
| GPU utilization, XR counter (%) | 96.0 [96.0..96.0] |
| CPU utilization average / worst, XR counter (%) | 24.2 [23.5..24.8] / 30.9 [29.7..32.0] |
| GPU pass Cull classification (ms) | 0.053 [0.053..0.053] |
| GPU pass Cull command generation (ms) | 0.093 [0.092..0.094] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 [0.040..0.040] |
| GPU pass Shadow 0 (ms) | 1.329 [1.327..1.331] |
| GPU pass XR multiview (ms) | 8.406 [8.404..8.407] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 10.230 [10.229..10.232] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 87.13 | 0 -> 0; 0 -> 0, step-downs 0 | 61.1 -> 62.2 | 61.1 -> 61.2 | 58.1 -> 58.6 | 39.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2208 [1920..2362] cap 2362/2362 | 456 cap 456 | 96 | 30.1 |
| 1 | 87.03 | 0 -> 0; 0 -> 0, step-downs 0 | 63.6 -> 64.1 | 63.6 -> 63.4 | 60.7 -> 60.8 | 39.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2141 [1920..2362] cap 2362/2362 | 456 cap 456 | 96 | 30.2 |


## 2026-11-06 12:41 - bistro_street_120, flat_draw_color_gpu_culling_lod_on_demand_t4

Flat draw color, no lights, no shadows, culling classification gpu, PVS off, occlusion on, lod_enabled on, lod_cpu_selection on_demand, threshold 4.0, band 0.25.

- Runs: 2 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261106_124120_bistro_street_120_0.json`, `20261106_124120_bistro_street_120_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (ee49055dd-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street_120.json --no-scene`; scene `bistro_quest_lod_cell_4m_v4_ws.glb` (listed 5.08 s, idle 5.08 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset (arguments in the result files' `host` block).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 0+0, spot 0+0, point 0+0 |
| Shadows | off; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | on (gpu supported) |
| Content draws | all |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 119.98 [119.98..119.99] |
| Frame wall time (ms) | 8.34 [8.34..8.34] |
| M2 / R / M3 (ms) | 0.81 [0.81..0.81] / 3.48 [3.44..3.51] / 0.79 [0.78..0.79] |
| App GPU frame time, XR counter (ms) | 3.70 [3.68..3.72] |
| GPU utilization, XR counter (%) | 69.4 [69.0..69.8] |
| CPU utilization average / worst, XR counter (%) | 28.3 [27.2..29.4] / 31.7 [30.6..32.7] |
| GPU pass Cull classification (ms) | 0.045 [0.045..0.046] |
| GPU pass Cull command generation (ms) | 0.048 [0.047..0.048] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.039 [0.039..0.039] |
| GPU pass XR multiview (ms) | 4.361 [4.357..4.365] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 4.496 [4.492..4.501] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 119.98 | 0 -> 0; 0 -> 0, step-downs 0 | 64.6 -> 64.0 | 63.2 -> 62.6 | 61.9 -> 61.5 | 40.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1920 [1382..2362] cap 2362/2362 | 456 cap 456 | 68 | 36.5 |
| 1 | 119.99 | 0 -> 0; 0 -> 0, step-downs 0 | 65.5 -> 64.5 | 63.8 -> 63.2 | 62.4 -> 62.0 | 40.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2362 cap 2362/2362 | 456 cap 456 | 69 | 31.6 |


## 2026-11-06 13:14 - bistro_street_120, single_directional_loops_gpu_culling_lod_on_demand_t4

As single_directional_loops_gpu_culling, PVS off, occlusion on, lod_enabled on, lod_cpu_selection on_demand, threshold 4.0, band 0.25.

- Runs: 2 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261106_131417_bistro_street_120_0.json`, `20261106_131417_bistro_street_120_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (ee49055dd-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street_120.json --no-scene`; scene `bistro_quest_lod_cell_4m_v4_node.glb` (listed 4.5 s, idle 4.5 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (0 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
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
| Frames per second | 85.83 [85.66..86.01] |
| Frame wall time (ms) | 11.30 [11.25..11.35] |
| M2 / R / M3 (ms) | 0.82 [0.82..0.83] / 4.50 [4.49..4.51] / 0.72 [0.71..0.72] |
| App GPU frame time, XR counter (ms) | 8.15 [8.14..8.16] |
| GPU utilization, XR counter (%) | 96.1 [96.0..96.2] |
| CPU utilization average / worst, XR counter (%) | 25.1 [23.6..26.5] / 29.9 [28.3..31.4] |
| GPU pass Cull classification (ms) | 0.053 [0.053..0.053] |
| GPU pass Cull command generation (ms) | 0.091 [0.090..0.091] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 [0.040..0.041] |
| GPU pass Shadow 0 (ms) | 1.456 [1.454..1.458] |
| GPU pass XR multiview (ms) | 8.419 [8.405..8.433] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 10.418 [10.371..10.466] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 85.66 | 0 -> 0; 0 -> 0, step-downs 0 | 55.5 -> 59.1 | 55.2 -> 57.6 | 51.0 -> 53.6 | 35.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2362 [1920..2362] cap 2362/2362 | 456 cap 456 | 96 | 37.9 |
| 1 | 86.01 | 0 -> 0; 0 -> 0, step-downs 0 | 60.8 -> 61.3 | 60.0 -> 59.7 | 56.9 -> 57.0 | 36.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2131 [1920..2362] cap 2362/2362 | 456 cap 456 | 96 | 36.1 |


## 2026-11-06 13:15 - bistro_street_120, single_directional_loops_gpu_culling_lod_on_demand_t4

As single_directional_loops_gpu_culling, PVS off, occlusion on, lod_enabled on, lod_cpu_selection on_demand, threshold 4.0, band 0.25.

- Runs: 2 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261106_131514_bistro_street_120_0.json`, `20261106_131514_bistro_street_120_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (ee49055dd-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street_120.json --no-scene`; scene `bistro_quest_lod_cell_4m_v4_ws.glb` (listed 4.81 s, idle 4.81 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (0 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
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
| Frames per second | 87.41 [87.24..87.58] |
| Frame wall time (ms) | 11.07 [11.01..11.13] |
| M2 / R / M3 (ms) | 0.85 [0.84..0.85] / 4.53 [4.49..4.56] / 0.71 [0.71..0.71] |
| App GPU frame time, XR counter (ms) | 8.11 [8.06..8.16] |
| GPU utilization, XR counter (%) | 95.9 [95.8..96.0] |
| CPU utilization average / worst, XR counter (%) | 24.7 [24.5..24.9] / 31.5 [30.4..32.6] |
| GPU pass Cull classification (ms) | 0.054 [0.053..0.056] |
| GPU pass Cull command generation (ms) | 0.093 [0.090..0.097] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 |
| GPU pass Shadow 0 (ms) | 1.330 [1.329..1.331] |
| GPU pass XR multiview (ms) | 8.393 [8.377..8.408] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 10.137 [10.129..10.145] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 87.58 | 0 -> 0; 0 -> 0, step-downs 0 | 62.4 -> 62.8 | 61.7 -> 61.6 | 58.7 -> 58.8 | 36.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2141 [1651..2362] cap 2362/2362 | 456 cap 456 | 96 | 33.2 |
| 1 | 87.24 | 0 -> 0; 0 -> 0, step-downs 0 | 63.0 -> 63.6 | 63.0 -> 62.7 | 60.1 -> 60.1 | 37.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2362 [2054..2362] cap 2362/2362 | 456 cap 456 | 96 | 35.2 |


## 2026-11-06 13:16 - bistro_street_120, flat_draw_color_gpu_culling_lod_on_demand_t4

Flat draw color, no lights, no shadows, culling classification gpu, PVS off, occlusion on, lod_enabled on, lod_cpu_selection on_demand, threshold 4.0, band 0.25.

- Runs: 2 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261106_131611_bistro_street_120_0.json`, `20261106_131611_bistro_street_120_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (ee49055dd-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street_120.json --no-scene`; scene `bistro_quest_lod_cell_4m_v4_ws.glb` (listed 4.6 s, idle 4.6 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset (arguments in the result files' `host` block).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 0+0, spot 0+0, point 0+0 |
| Shadows | off; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | on (gpu supported) |
| Content draws | all |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 119.79 [119.60..119.97] |
| Frame wall time (ms) | 8.34 [8.33..8.34] |
| M2 / R / M3 (ms) | 0.85 [0.83..0.86] / 3.59 [3.53..3.64] / 0.78 [0.76..0.80] |
| App GPU frame time, XR counter (ms) | 3.74 [3.64..3.84] |
| GPU utilization, XR counter (%) | 70.0 [68.4..71.5] |
| CPU utilization average / worst, XR counter (%) | 27.7 [27.7..27.7] / 31.0 [30.3..31.7] |
| GPU pass Cull classification (ms) | 0.046 [0.046..0.046] |
| GPU pass Cull command generation (ms) | 0.049 [0.049..0.049] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.039 [0.039..0.039] |
| GPU pass XR multiview (ms) | 4.377 [4.372..4.382] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 4.513 [4.508..4.517] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 119.60 | 0 -> 0; 0 -> 0, step-downs 0 | 64.1 -> 63.6 | 62.4 -> 61.7 | 60.8 -> 60.5 | 38.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1920 [1517..2362] cap 2362/2362 | 456 cap 456 | 67 | 36.2 |
| 1 | 119.97 | 0 -> 0; 0 -> 0, step-downs 0 | 67.4 -> 65.7 | 64.3 -> 63.1 | 62.0 -> 61.7 | 38.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2362 [1920..2362] cap 2362/2362 | 456 cap 456 | 70 | 40.5 |


## 2026-11-06 13:16 - bistro_street_120, flat_draw_color_gpu_culling_lod_on_demand_t4

Flat draw color, no lights, no shadows, culling classification gpu, PVS off, occlusion on, lod_enabled on, lod_cpu_selection on_demand, threshold 4.0, band 0.25.

- Runs: 2 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261106_131656_bistro_street_120_0.json`, `20261106_131656_bistro_street_120_1.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (ee49055dd-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street_120.json --no-scene`; scene `bistro_quest_lod_cell_4m_v4_node.glb` (listed 4.67 s, idle 4.67 s).
- Configuration calls: set_culling_classification, set_pvs_culling, set_occlusion_culling, set_graphics_preset (arguments in the result files' `host` block).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/10 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 0+0, spot 0+0, point 0+0 |
| Shadows | off; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | on (gpu supported) |
| Content draws | all |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 119.98 [119.97..119.99] |
| Frame wall time (ms) | 8.34 [8.33..8.35] |
| M2 / R / M3 (ms) | 0.85 [0.82..0.89] / 3.46 [3.45..3.47] / 0.77 [0.77..0.78] |
| App GPU frame time, XR counter (ms) | 3.78 [3.76..3.81] |
| GPU utilization, XR counter (%) | 70.3 [70.3..70.3] |
| CPU utilization average / worst, XR counter (%) | 26.7 [26.3..27.2] / 29.1 [29.0..29.3] |
| GPU pass Cull classification (ms) | 0.046 [0.046..0.046] |
| GPU pass Cull command generation (ms) | 0.050 [0.050..0.051] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.039 [0.039..0.039] |
| GPU pass XR multiview (ms) | 4.359 [4.340..4.379] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 4.497 [4.478..4.516] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 119.99 | 0 -> 0; 0 -> 0, step-downs 0 | 66.1 -> 65.2 | 63.9 -> 63.0 | 62.2 -> 61.8 | 38.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1920 [1920..2362] cap 2362/2362 | 456 cap 456 | 70 | 36.7 |
| 1 | 119.97 | 0 -> 0; 0 -> 0, step-downs 0 | 65.9 -> 65.4 | 64.2 -> 63.3 | 62.5 -> 62.2 | 38.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1920 [1920..2362] cap 2362/2362 | 456 cap 456 | 69 | 35.7 |


## 2026-11-06 23:16 - bistro_street_120, no configuration file

- Runs: 1 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261106_231647_bistro_street_120_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (3a3bc4e2a-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src.quest.erhe_prepared` (listed 7.91 s, idle 7.91 s).
- Scene: the store Export (target quest) of the erhe-saved Bistro source (import root rotated -90 X, LOD chains generated): 3272 mesh items, Scene item 420 MB with the 393 images as raw ASTC 4x4 inside it (no textures as items yet); the following entry is Prepare (Quest) of the same source as a .glb, node space, no PVS. Memory in the measured window (`get_memory_usage`): VmHWM 2017 MB, VmRSS 1200 MB, retained image source bytes 402 MB (the images parsed from the in-memory Scene item are held).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | automatic / clustered (Headset) |
| Lights | point 33/33 shaded, 1 shadow-mapped (0 cached); spot 10/10 shaded, 2 shadow-mapped (2 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+4, spot 2+4, point 1+4 |
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
| Frames per second | 105.68 |
| Frame wall time (ms) | 9.29 |
| M2 / R / M3 (ms) | 0.75 / 3.16 / 0.72 |
| App GPU frame time, XR counter (ms) | 6.75 |
| GPU utilization, XR counter (%) | 95.3 |
| CPU utilization average / worst, XR counter (%) | 24.3 / 28.0 |
| GPU pass Cull classification (ms) | 0.052 |
| GPU pass Cull command generation (ms) | 0.106 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 |
| GPU pass Light cluster binning Headset (ms) | 0.053 |
| GPU pass XR multiview (ms) | 8.286 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 8.556 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 105.68 | 0 -> 0; 0 -> 0, step-downs 0 | 55.5 -> 55.3 | 55.2 -> 55.3 | 51.8 -> 52.3 | 31.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2362 cap 2362/2362 | 456 cap 456 | 95 | 26.6 |


## 2026-11-06 23:17 - bistro_street_120, no configuration file

- Runs: 1 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261106_231714_bistro_street_120_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (3a3bc4e2a-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src_quest.glb` (listed 4.57 s, idle 4.57 s).
- Scene: Prepare (Quest) `.glb` of the same source as the previous entry (node space, no PVS). Memory in the measured window (`get_memory_usage`): VmHWM 833 MB, VmRSS 787 MB, retained image source bytes 0.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | automatic / clustered (Headset) |
| Lights | point 33/33 shaded, 1 shadow-mapped (0 cached); spot 10/10 shaded, 2 shadow-mapped (2 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+4, spot 2+4, point 1+4 |
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
| Frames per second | 104.99 |
| Frame wall time (ms) | 9.37 |
| M2 / R / M3 (ms) | 0.76 / 3.14 / 0.72 |
| App GPU frame time, XR counter (ms) | 6.74 |
| GPU utilization, XR counter (%) | 95.6 |
| CPU utilization average / worst, XR counter (%) | 21.0 / 24.2 |
| GPU pass Cull classification (ms) | 0.053 |
| GPU pass Cull command generation (ms) | 0.113 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 |
| GPU pass Light cluster binning Headset (ms) | 0.053 |
| GPU pass XR multiview (ms) | 8.344 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 8.620 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 104.99 | 0 -> 0; 0 -> 0, step-downs 0 | 57.1 -> 57.1 | 57.5 -> 57.3 | 54.5 -> 54.5 | 32.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2054 [1920..2362] cap 2362/2362 | 456 cap 456 | 95 | 27.5 |


## 2026-11-07 01:03 - bistro_street_120, no configuration file

- Runs: 1 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261107_010341_bistro_street_120_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (0fe5de98a-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src.quest.erhe_prepared` (listed 5.81 s, idle 5.81 s).
- Scene: the store Export (target quest, prepared assets D8 step 6b) of the erhe-saved Bistro source (import root rotated -90 X, LOD chains generated): 3272 mesh items, 3272 BVH items, 266 Texture items (ASTC 4x4, 293 MB; the 393 images name them), Scene item 18.5 MB with no image inside; compare with the step 2 entry of 2026-11-06 23:16 (Scene item 420 MB with the images inside) and with the `.glb` entries around it. Memory in the measured window (`get_memory_usage`): VmHWM 864 MB, VmRSS 793 MB, retained image source bytes 0 (step 2: 2017 MB, 1200 MB, 402 MB).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | automatic / clustered (Headset) |
| Lights | point 33/33 shaded, 1 shadow-mapped (0 cached); spot 10/10 shaded, 2 shadow-mapped (2 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+4, spot 2+4, point 1+4 |
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
| Frames per second | 106.73 |
| Frame wall time (ms) | 9.14 |
| M2 / R / M3 (ms) | 0.76 / 3.24 / 0.75 |
| App GPU frame time, XR counter (ms) | 6.67 |
| GPU utilization, XR counter (%) | 94.7 |
| CPU utilization average / worst, XR counter (%) | 24.0 / 26.0 |
| GPU pass Cull classification (ms) | 0.052 |
| GPU pass Cull command generation (ms) | 0.106 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 |
| GPU pass Light cluster binning Headset (ms) | 0.053 |
| GPU pass XR multiview (ms) | 8.160 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 8.441 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 106.73 | 0 -> 0; 0 -> 0, step-downs 0 | 47.4 -> 49.2 | 47.9 -> 49.5 | 43.5 -> 45.5 | 30.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1920 [1651..2362] cap 2362/2362 | 456 cap 456 | 94 | 28.7 |


## 2026-11-07 01:04 - bistro_street_120, no configuration file

- Runs: 1 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261107_010419_bistro_street_120_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (0fe5de98a-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src_quest.glb` (listed 4.25 s, idle 4.56 s).
- Scene: Prepare (Quest) `.glb` of the step 2 source (node space, no PVS), the same file as the 2026-11-06 23:17 entry, with the D8 step 6b APK. Memory in the measured window (`get_memory_usage`): VmHWM 826 MB, VmRSS 826 MB, retained image source bytes 0.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | automatic / clustered (Headset) |
| Lights | point 33/33 shaded, 1 shadow-mapped (0 cached); spot 10/10 shaded, 2 shadow-mapped (2 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+4, spot 2+4, point 1+4 |
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
| Frames per second | 104.87 |
| Frame wall time (ms) | 9.34 |
| M2 / R / M3 (ms) | 0.74 / 3.17 / 0.72 |
| App GPU frame time, XR counter (ms) | 6.70 |
| GPU utilization, XR counter (%) | 95.0 |
| CPU utilization average / worst, XR counter (%) | 23.1 / 26.8 |
| GPU pass Cull classification (ms) | 0.052 |
| GPU pass Cull command generation (ms) | 0.113 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 |
| GPU pass Light cluster binning Headset (ms) | 0.053 |
| GPU pass XR multiview (ms) | 8.282 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 8.569 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 104.87 | 0 -> 0; 0 -> 0, step-downs 0 | 55.1 -> 56.7 | 55.7 -> 56.7 | 51.6 -> 53.0 | 30.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2362 [1920..2362] cap 2362/2362 | 456 cap 456 | 94 | 27.7 |


## 2026-11-07 01:04 - bistro_street_120, no configuration file

- Runs: 1 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261107_010458_bistro_street_120_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (0fe5de98a-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src.quest.erhe_prepared` (listed 5.91 s, idle 5.91 s).
- Scene: the store Export (target quest, prepared assets D8 step 6b) of the erhe-saved Bistro source (import root rotated -90 X, LOD chains generated): 3272 mesh items, 3272 BVH items, 266 Texture items (ASTC 4x4, 293 MB; the 393 images name them), Scene item 18.5 MB with no image inside; compare with the step 2 entry of 2026-11-06 23:16 (Scene item 420 MB with the images inside) and with the `.glb` entries around it. Memory in the measured window (`get_memory_usage`): VmHWM 844 MB, VmRSS 794 MB, retained image source bytes 0 (step 2: 2017 MB, 1200 MB, 402 MB).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | automatic / clustered (Headset) |
| Lights | point 33/33 shaded, 1 shadow-mapped (0 cached); spot 10/10 shaded, 2 shadow-mapped (2 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+4, spot 2+4, point 1+4 |
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
| Frames per second | 105.43 |
| Frame wall time (ms) | 9.32 |
| M2 / R / M3 (ms) | 0.77 / 3.29 / 0.75 |
| App GPU frame time, XR counter (ms) | 6.76 |
| GPU utilization, XR counter (%) | 95.2 |
| CPU utilization average / worst, XR counter (%) | 22.7 / 31.6 |
| GPU pass Cull classification (ms) | 0.053 |
| GPU pass Cull command generation (ms) | 0.108 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 |
| GPU pass Light cluster binning Headset (ms) | 0.053 |
| GPU pass XR multiview (ms) | 8.286 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 8.559 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 105.43 | 0 -> 0; 0 -> 0, step-downs 0 | 57.3 -> 57.1 | 57.7 -> 57.2 | 54.8 -> 54.5 | 30.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2362 [1920..2362] cap 2362/2362 | 456 cap 456 | 95 | 29.5 |


## 2026-11-07 01:05 - bistro_street_120, no configuration file

- Runs: 1 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261107_010523_bistro_street_120_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (0fe5de98a-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src_quest.glb` (listed 4.3 s, idle 4.3 s).
- Scene: Prepare (Quest) `.glb` of the step 2 source (node space, no PVS), the same file as the 2026-11-06 23:17 entry, with the D8 step 6b APK. Memory in the measured window (`get_memory_usage`): VmHWM 825 MB, VmRSS 782 MB, retained image source bytes 0.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | automatic / clustered (Headset) |
| Lights | point 33/33 shaded, 1 shadow-mapped (0 cached); spot 10/10 shaded, 2 shadow-mapped (2 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+4, spot 2+4, point 1+4 |
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
| Frames per second | 105.47 |
| Frame wall time (ms) | 9.33 |
| M2 / R / M3 (ms) | 0.77 / 3.27 / 0.74 |
| App GPU frame time, XR counter (ms) | 6.66 |
| GPU utilization, XR counter (%) | 95.0 |
| CPU utilization average / worst, XR counter (%) | 25.0 / 27.3 |
| GPU pass Cull classification (ms) | 0.053 |
| GPU pass Cull command generation (ms) | 0.122 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 |
| GPU pass Light cluster binning Headset (ms) | 0.053 |
| GPU pass XR multiview (ms) | 8.256 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 8.543 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 105.47 | 0 -> 0; 0 -> 0, step-downs 0 | 57.8 -> 57.7 | 58.2 -> 58.0 | 55.4 -> 55.3 | 32.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1920 [1651..2362] cap 2362/2362 | 456 cap 456 | 94 | 30.6 |


## 2026-11-07 01:43 - bistro_street_120, no configuration file

- Runs: 1 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261107_014311_bistro_street_120_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (9778f56d9-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark config/editor/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src.quest.erhe_prepared` (listed 4.36 s, idle 4.36 s).
- Scene: the D8 step 6b store Export of the erhe-saved Bistro source (as the 2026-11-07 01:03 entry), converted to 64 KiB pages (`PRAGMA page_size = 65536; VACUUM`: the same rows in the page size a new container gets), with the payload-read APK of `doc/erhe/asset_store.md` "Reading payloads" (64 KiB pages, open payload handles in the load, 8 read connections, Scene item keys read with simdjson). build_imported_buffer_meshes 1807 ms (6b: 2389 / 2717 ms). Memory in the measured window (`get_memory_usage`): VmHWM 859 MB, VmRSS 800 MB, retained image source bytes 0.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | automatic / clustered (Headset) |
| Lights | point 33/33 shaded, 1 shadow-mapped (0 cached); spot 10/10 shaded, 2 shadow-mapped (2 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+4, spot 2+4, point 1+4 |
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
| Frames per second | 106.68 |
| Frame wall time (ms) | 9.27 |
| M2 / R / M3 (ms) | 0.78 / 3.30 / 0.74 |
| App GPU frame time, XR counter (ms) | 6.65 |
| GPU utilization, XR counter (%) | 94.6 |
| CPU utilization average / worst, XR counter (%) | 24.3 / 31.7 |
| GPU pass Cull classification (ms) | 0.053 |
| GPU pass Cull command generation (ms) | 0.113 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 |
| GPU pass Light cluster binning Headset (ms) | 0.053 |
| GPU pass XR multiview (ms) | 8.251 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 8.523 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 106.68 | 0 -> 0; 0 -> 0, step-downs 0 | 58.9 -> 59.1 | 59.4 -> 59.3 | 56.7 -> 56.8 | 36.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 [1382..2362] cap 2362/2362 | 456 cap 456 | 94 | 29.1 |


## 2026-11-07 01:43 - bistro_street_120, no configuration file

- Runs: 1 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261107_014335_bistro_street_120_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (9778f56d9-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark config/editor/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src_quest.glb` (listed 4.67 s, idle 4.67 s).
- Scene: Prepare (Quest) `.glb` of the step 2 source (node space, no PVS), the same file as the 2026-11-07 01:04 entry, with the same APK as the store runs beside it. build_imported_buffer_meshes 1816 ms. Memory in the measured window (`get_memory_usage`): VmHWM 815 MB, VmRSS 773 MB, retained image source bytes 0.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | automatic / clustered (Headset) |
| Lights | point 33/33 shaded, 1 shadow-mapped (0 cached); spot 10/10 shaded, 2 shadow-mapped (2 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+4, spot 2+4, point 1+4 |
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
| Frames per second | 105.31 |
| Frame wall time (ms) | 9.32 |
| M2 / R / M3 (ms) | 0.76 / 3.22 / 0.73 |
| App GPU frame time, XR counter (ms) | 6.71 |
| GPU utilization, XR counter (%) | 94.8 |
| CPU utilization average / worst, XR counter (%) | 24.6 / 31.2 |
| GPU pass Cull classification (ms) | 0.052 |
| GPU pass Cull command generation (ms) | 0.116 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 |
| GPU pass Light cluster binning Headset (ms) | 0.053 |
| GPU pass XR multiview (ms) | 8.274 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 8.561 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 105.31 | 0 -> 0; 0 -> 0, step-downs 0 | 60.6 -> 60.5 | 61.1 -> 60.8 | 58.5 -> 58.4 | 37.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1920 [1651..2208] cap 2362/2362 | 456 cap 456 | 94 | 29.6 |


## 2026-11-07 01:43 - bistro_street_120, no configuration file

- Runs: 1 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261107_014359_bistro_street_120_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (9778f56d9-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark config/editor/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src.quest.erhe_prepared` (listed 4.54 s, idle 4.54 s).
- Scene: the D8 step 6b store Export of the erhe-saved Bistro source (as the 2026-11-07 01:03 entry), converted to 64 KiB pages (`PRAGMA page_size = 65536; VACUUM`: the same rows in the page size a new container gets), with the payload-read APK of `doc/erhe/asset_store.md` "Reading payloads" (64 KiB pages, open payload handles in the load, 8 read connections, Scene item keys read with simdjson). build_imported_buffer_meshes 1871 ms (6b: 2389 / 2717 ms). Memory in the measured window (`get_memory_usage`): VmHWM 850 MB, VmRSS 793 MB, retained image source bytes 0.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | automatic / clustered (Headset) |
| Lights | point 33/33 shaded, 1 shadow-mapped (0 cached); spot 10/10 shaded, 2 shadow-mapped (2 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+4, spot 2+4, point 1+4 |
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
| Frames per second | 105.78 |
| Frame wall time (ms) | 9.28 |
| M2 / R / M3 (ms) | 0.78 / 3.33 / 0.75 |
| App GPU frame time, XR counter (ms) | 6.90 |
| GPU utilization, XR counter (%) | 95.4 |
| CPU utilization average / worst, XR counter (%) | 24.7 / 32.4 |
| GPU pass Cull classification (ms) | 0.052 |
| GPU pass Cull command generation (ms) | 0.116 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 |
| GPU pass Light cluster binning Headset (ms) | 0.053 |
| GPU pass XR multiview (ms) | 8.264 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 8.542 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 105.78 | 0 -> 0; 0 -> 0, step-downs 0 | 61.8 -> 62.3 | 62.3 -> 61.8 | 59.7 -> 59.6 | 37.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2362 [1920..2362] cap 2362/2362 | 456 cap 456 | 95 | 29.6 |


## 2026-11-07 01:44 - bistro_street_120, no configuration file

- Runs: 1 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261107_014424_bistro_street_120_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (9778f56d9-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark config/editor/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src_quest.glb` (listed 4.36 s, idle 4.36 s).
- Scene: Prepare (Quest) `.glb` of the step 2 source (node space, no PVS), the same file as the 2026-11-07 01:04 entry, with the same APK as the store runs beside it. build_imported_buffer_meshes 1828 ms. Memory in the measured window (`get_memory_usage`): VmHWM 814 MB, VmRSS 798 MB, retained image source bytes 0.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | automatic / clustered (Headset) |
| Lights | point 33/33 shaded, 1 shadow-mapped (0 cached); spot 10/10 shaded, 2 shadow-mapped (2 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+4, spot 2+4, point 1+4 |
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
| Frames per second | 105.36 |
| Frame wall time (ms) | 9.28 |
| M2 / R / M3 (ms) | 0.79 / 3.36 / 0.76 |
| App GPU frame time, XR counter (ms) | 6.61 |
| GPU utilization, XR counter (%) | 94.8 |
| CPU utilization average / worst, XR counter (%) | 25.0 / 26.7 |
| GPU pass Cull classification (ms) | 0.052 |
| GPU pass Cull command generation (ms) | 0.105 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 |
| GPU pass Light cluster binning Headset (ms) | 0.053 |
| GPU pass XR multiview (ms) | 8.254 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 8.520 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 105.36 | 0 -> 0; 0 -> 0, step-downs 0 | 62.6 -> 62.3 | 63.1 -> 62.7 | 60.5 -> 60.3 | 37.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2054 [1920..2362] cap 2362/2362 | 456 cap 456 | 94 | 27.2 |


## 2026-11-07 01:44 - bistro_street_120, no configuration file

- Runs: 1 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261107_014447_bistro_street_120_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (9778f56d9-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark config/editor/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src.quest.erhe_prepared` (listed 4.41 s, idle 4.41 s).
- Scene: the D8 step 6b store Export of the erhe-saved Bistro source (as the 2026-11-07 01:03 entry), converted to 64 KiB pages (`PRAGMA page_size = 65536; VACUUM`: the same rows in the page size a new container gets), with the payload-read APK of `doc/erhe/asset_store.md` "Reading payloads" (64 KiB pages, open payload handles in the load, 8 read connections, Scene item keys read with simdjson). build_imported_buffer_meshes 1809 ms (6b: 2389 / 2717 ms). Memory in the measured window (`get_memory_usage`): VmHWM 851 MB, VmRSS 785 MB, retained image source bytes 0.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | automatic / clustered (Headset) |
| Lights | point 33/33 shaded, 1 shadow-mapped (0 cached); spot 10/10 shaded, 2 shadow-mapped (2 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+4, spot 2+4, point 1+4 |
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
| Frames per second | 105.78 |
| Frame wall time (ms) | 9.29 |
| M2 / R / M3 (ms) | 0.77 / 3.38 / 0.76 |
| App GPU frame time, XR counter (ms) | 6.85 |
| GPU utilization, XR counter (%) | 95.0 |
| CPU utilization average / worst, XR counter (%) | 24.6 / 29.7 |
| GPU pass Cull classification (ms) | 0.052 |
| GPU pass Cull command generation (ms) | 0.117 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 |
| GPU pass Light cluster binning Headset (ms) | 0.053 |
| GPU pass XR multiview (ms) | 8.244 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 8.525 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 105.78 | 0 -> 0; 0 -> 0, step-downs 0 | 63.0 -> 63.4 | 63.6 -> 62.9 | 61.1 -> 60.8 | 38.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1920 [1651..2362] cap 2362/2362 | 456 cap 456 | 95 | 29.3 |


## 2026-11-07 01:45 - bistro_street_120, no configuration file

- Runs: 1 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261107_014511_bistro_street_120_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (9778f56d9-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark config/editor/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src_quest.glb` (listed 4.31 s, idle 4.62 s).
- Scene: Prepare (Quest) `.glb` of the step 2 source (node space, no PVS), the same file as the 2026-11-07 01:04 entry, with the same APK as the store runs beside it. build_imported_buffer_meshes 1791 ms. Memory in the measured window (`get_memory_usage`): VmHWM 808 MB, VmRSS 773 MB, retained image source bytes 0.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | automatic / clustered (Headset) |
| Lights | point 33/33 shaded, 1 shadow-mapped (0 cached); spot 10/10 shaded, 2 shadow-mapped (2 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+4, spot 2+4, point 1+4 |
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
| Frames per second | 105.17 |
| Frame wall time (ms) | 9.33 |
| M2 / R / M3 (ms) | 0.78 / 3.34 / 0.75 |
| App GPU frame time, XR counter (ms) | 7.03 |
| GPU utilization, XR counter (%) | 95.4 |
| CPU utilization average / worst, XR counter (%) | 22.6 / 27.6 |
| GPU pass Cull classification (ms) | 0.053 |
| GPU pass Cull command generation (ms) | 0.110 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 |
| GPU pass Light cluster binning Headset (ms) | 0.053 |
| GPU pass XR multiview (ms) | 8.312 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 8.584 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 105.17 | 0 -> 0; 0 -> 0, step-downs 0 | 63.6 -> 63.7 | 64.1 -> 63.4 | 61.7 -> 61.4 | 38.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2362 [2208..2362] cap 2362/2362 | 456 cap 456 | 95 | 21.3 |


## 2026-11-07 06:30 - bistro_street_120, no configuration file

- Runs: 3 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261107_063013_bistro_street_120_0.json`, `20261107_063013_bistro_street_120_1.json`, `20261107_063013_bistro_street_120_2.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (954b7fc44); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark config/editor/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src.quest.erhe_prepared` (listed 4.78 s, idle 4.78 s).
- Configuration calls: get_pvs_status (arguments in the result files' `host` block).
- Scene (prepared assets D8 step 11, R13.4): the quest prepared asset that `scripts/prepared_asset_bistro_verify.py --keep-quest` makes from the erhe-saved Bistro source (import root rotated -90 X, LOD chains generated, export settings with the bake on, `instancing` `duplicate`, `pvs_mode` `rebake` over 2068 cells of 4 m): 10 Local-space mesh items, 3795 World-space mesh items, 3805 BVH items, 266 Texture items (ASTC 4x4), one PVS item and the Scene item, 7878 items, 1047 MB, run as a prepared run with no source on the device. The following entry is the prepared glTF file (`save_scene_for_quest`) of the same open scene after the same Export. Per run, listed / idle s, VmHWM MB, fps: 4.78 / 4.78, 832, 105.85; 4.69 / 4.69, 860, 105.23; 4.58 / 4.58, 864, 104.59 (median listed 4.69 s, VmHWM 860 MB); retained image source bytes 0; PVS `ready` with `bits_hash` `fd5d5edadffa53f7` after the load in every run. Between the MCP `load_scene` call and the parse the store open checks the format stamp of every item twice (MCP `load_scene` on the main thread, then the load task): 7878 `Asset_store::get_info` calls each, 0.87 - 1.0 s cold and 0.37 - 0.63 s warm on the device (temporary timing lines, not in this build), the cost the prepared glTF file does not have; with the item table's `payload` column moved after `info` the warm check took 0.17 s and the cold one 1.0 s (each lookup reads the row's leaf page, which holds the first 8 - 64 KiB of its payload).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | automatic / clustered (Headset) |
| Lights | point 33/33 shaded, 1 shadow-mapped (0 cached); spot 10/10 shaded, 2 shadow-mapped (2 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+4, spot 2+4, point 1+4 |
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
| Frames per second | 105.23 [104.59..105.85] |
| Frame wall time (ms) | 9.36 [9.32..9.44] |
| M2 / R / M3 (ms) | 0.77 [0.77..0.78] / 3.33 [3.22..3.38] / 0.66 [0.65..0.67] |
| App GPU frame time, XR counter (ms) | 6.70 [6.70..6.75] |
| GPU utilization, XR counter (%) | 95.1 [95.1..95.4] |
| CPU utilization average / worst, XR counter (%) | 23.0 [22.9..23.6] / 29.6 [29.6..30.8] |
| GPU pass Cull classification (ms) | 0.053 [0.052..0.053] |
| GPU pass Cull command generation (ms) | 0.127 [0.124..0.131] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 [0.040..0.040] |
| GPU pass Light cluster binning Headset (ms) | 0.053 [0.053..0.053] |
| GPU pass XR multiview (ms) | 8.262 [8.228..8.288] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 8.555 [8.509..8.581] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 105.85 | 0 -> 0; 0 -> 0, step-downs 0 | 47.6 -> 49.6 | 48.2 -> 49.8 | 43.7 -> 45.6 | 29.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2054 [1920..2208] cap 2362/2362 | 456 cap 456 | 94 | 29.7 |
| 1 | 105.23 | 0 -> 0; 0 -> 0, step-downs 0 | 53.6 -> 53.5 | 54.2 -> 53.6 | 50.8 -> 50.7 | 30.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1517 [1382..2362] cap 2362/2362 | 456 cap 456 | 95 | 28.3 |
| 2 | 104.59 | 0 -> 0; 0 -> 0, step-downs 0 | 54.4 -> 54.9 | 54.9 -> 54.5 | 51.9 -> 51.9 | 30.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2054 [1920..2362] cap 2362/2362 | 456 cap 456 | 94 | 28.4 |


## 2026-11-07 06:31 - bistro_street_120, no configuration file

- Runs: 3 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261107_063150_bistro_street_120_0.json`, `20261107_063150_bistro_street_120_1.json`, `20261107_063150_bistro_street_120_2.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (954b7fc44); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark config/editor/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src_quest.glb` (listed 4.48 s, idle 4.48 s).
- Configuration calls: get_pvs_status (arguments in the result files' `host` block).
- Scene: the prepared glTF file (`save_scene_for_quest`, 1094 MB, world-space form, PVS inline) of the open scene the previous entry's prepared asset was exported from, written after its Export with the same settings. Per run, listed / idle s, VmHWM MB, fps: 4.48 / 4.48, 819, 105.07; 4.33 / 4.64, 824, 105.49; 4.28 / 4.60, 826, 103.95 (median listed 4.33 s, idle 4.60 s, VmHWM 824 MB); PVS `ready`, `bits_hash` `fd5d5edadffa53f7`. Against the prepared asset (R13.4): the frame rate and the GPU times are the same within the runs' spread, the load is 0.36 s slower listed (0.09 s idle) and the peak 36 MB higher for the prepared asset, so R13.4 does not hold yet. `render_scene_image` on the device crashes in both (SIGSEGV in `Render_pass::start_render_pass` from `Shadow_renderer::draw`, the capture chain; pre-existing), so the images were not compared on the device; on the host the quest prepared run's PVS is `ready` with the same `bits_hash`.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | automatic / clustered (Headset) |
| Lights | point 33/33 shaded, 1 shadow-mapped (0 cached); spot 10/10 shaded, 2 shadow-mapped (2 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+4, spot 2+4, point 1+4 |
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
| Frames per second | 105.07 [103.95..105.49] |
| Frame wall time (ms) | 9.33 [9.32..9.34] |
| M2 / R / M3 (ms) | 0.78 [0.77..0.79] / 3.23 [3.22..3.23] / 0.66 [0.66..0.66] |
| App GPU frame time, XR counter (ms) | 6.63 [6.61..6.86] |
| GPU utilization, XR counter (%) | 95.0 [94.6..95.0] |
| CPU utilization average / worst, XR counter (%) | 23.1 [21.9..23.6] / 29.3 [26.5..29.3] |
| GPU pass Cull classification (ms) | 0.053 [0.052..0.053] |
| GPU pass Cull command generation (ms) | 0.128 [0.120..0.130] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 [0.040..0.040] |
| GPU pass Light cluster binning Headset (ms) | 0.053 [0.053..0.053] |
| GPU pass XR multiview (ms) | 8.267 [8.235..8.333] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 8.555 [8.528..8.617] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 105.07 | 0 -> 0; 0 -> 0, step-downs 0 | 53.2 -> 53.9 | 53.5 -> 53.6 | 50.2 -> 50.8 | 30.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2054 [1920..2208] cap 2362/2362 | 456 cap 456 | 94 | 23.6 |
| 1 | 105.49 | 0 -> 0; 0 -> 0, step-downs 0 | 55.6 -> 55.7 | 56.1 -> 56.1 | 53.2 -> 53.2 | 31.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 [1651..2362] cap 2362/2362 | 456 cap 456 | 94 | 26.9 |
| 2 | 103.95 | 0 -> 0; 0 -> 0, step-downs 0 | 57.1 -> 57.0 | 57.6 -> 57.4 | 54.7 -> 54.6 | 32.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1651 [1651..2362] cap 2362/2362 | 456 cap 456 | 94 | 32.3 |


## 2026-11-07 07:47 - bistro_street_120, no configuration file

- Runs: 4 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261107_074720_bistro_street_120_0.json`, `20261107_074808_bistro_street_120_0.json`, `20261107_074855_bistro_street_120_0.json`, `20261107_074943_bistro_street_120_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (ef887336b-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark config/editor/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src.quest.erhe_prepared` (listed 4.48 s, idle 4.48 s).
- Configuration calls: get_pvs_status (arguments in the result files' `host` block).
- Scene (prepared assets D8 step 11 fix, R13.4): the quest prepared asset of the 2026-11-07 06:30 entry made again by `scripts/prepared_asset_bistro_verify.py --keep-quest` in container format version 2 (payloads in their own table, `doc/erhe/asset_store.md` "Container"), 7878 items, 1045 MB, with the APK of this tree: one format stamp check per open from one read of the item table (`get_item_headers()`; `resolve_prepared_open()` 150 - 250 ms on the device, was two checks of 0.87 - 1.0 s cold each), the Scene item parsed through a reader (JSON chunk, BIN chunk by range) in place of a whole copy in memory, read connections with a 1 MiB page cache freed after each read once the load closed its payload handles, and the covering index `item_location` for payload lookups. Runs alternated with the following entry's (store, glTF, store, ...). Per run, listed / idle s, VmHWM MB, build_imported_buffer_meshes ms, fps: 4.48 / 4.48, 799, 1722, 106.95; 4.54 / 4.85, 800, 2043, 104.88; 4.61 / 4.91, 799, 2087, 105.62; 4.53 / 4.83, 795, 2031, 104.55 (median listed 4.54 s, idle 4.84 s, VmHWM 799 MB, malloc in use 568 MB); retained image source bytes 0; PVS `ready`, `bits_hash` `fd5d5edadffa53f7` in every run.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | automatic / clustered (Headset) |
| Lights | point 33/33 shaded, 1 shadow-mapped (0 cached); spot 10/10 shaded, 2 shadow-mapped (2 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+4, spot 2+4, point 1+4 |
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
| Frames per second | 105.25 [104.55..106.95] |
| Frame wall time (ms) | 9.25 [9.10..9.47] |
| M2 / R / M3 (ms) | 0.83 [0.82..0.86] / 3.43 [3.38..3.46] / 0.68 [0.67..0.70] |
| App GPU frame time, XR counter (ms) | 6.63 [6.41..6.88] |
| GPU utilization, XR counter (%) | 94.9 [94.1..95.3] |
| CPU utilization average / worst, XR counter (%) | 26.8 [25.0..29.3] / 30.8 [29.7..32.0] |
| GPU pass Cull classification (ms) | 0.052 [0.052..0.053] |
| GPU pass Cull command generation (ms) | 0.131 [0.124..0.134] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 [0.040..0.040] |
| GPU pass Light cluster binning Headset (ms) | 0.053 [0.053..0.053] |
| GPU pass XR multiview (ms) | 8.268 [8.117..8.328] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 8.566 [8.419..8.625] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 106.95 | 0 -> 0; 0 -> 0, step-downs 0 | 55.6 -> 58.3 | 55.4 -> 57.2 | 51.6 -> 53.9 | 37.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1920 [1382..1920] cap 2362/2362 | 456 cap 456 | 94 | 30.5 |
| 1 | 104.88 | 0 -> 0; 0 -> 0, step-downs 0 | 61.4 -> 61.8 | 61.7 -> 61.3 | 59.0 -> 59.0 | 38.0, 99 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2362 [1920..2362] cap 2362/2362 | 456 cap 456 | 94 | 25.9 |
| 2 | 105.62 | 0 -> 0; 0 -> 0, step-downs 0 | 64.1 -> 64.5 | 64.2 -> 63.6 | 61.6 -> 61.5 | 39.0, 99 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2362 [1920..2362] cap 2362/2362 | 456 cap 456 | 95 | 33.5 |
| 3 | 104.55 | 0 -> 0; 0 -> 0, step-downs 0 | 65.1 -> 65.6 | 65.6 -> 64.9 | 63.2 -> 62.9 | 40.0, 99 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2362 [1920..2362] cap 2362/2362 | 456 cap 456 | 95 | 31.1 |


## 2026-11-07 07:47 - bistro_street_120, no configuration file

- Runs: 4 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261107_074744_bistro_street_120_0.json`, `20261107_074832_bistro_street_120_0.json`, `20261107_074920_bistro_street_120_0.json`, `20261107_075008_bistro_street_120_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (ef887336b-dirty); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark config/editor/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src_quest.glb` (listed 4.7 s, idle 4.7 s).
- Configuration calls: get_pvs_status (arguments in the result files' `host` block).
- Scene: the prepared glTF file of the 2026-11-07 06:31 entry (the same file on the device), alternated with the previous entry's runs, same APK. Per run, listed / idle s, VmHWM MB, build_imported_buffer_meshes ms, fps: 4.70 / 4.70, 788, 1821, 105.27; 4.66 / 4.66, 789, 1813, 105.12; 4.75 / 4.75, 782, 1821, 106.64; 4.33 / 4.64, 780, 1796, 107.20 (median listed 4.68 s, idle 4.68 s, VmHWM 785 MB, malloc in use 560 MB); PVS `ready`, `bits_hash` `fd5d5edadffa53f7`. Against the prepared asset (R13.4): the prepared asset is listed 0.14 s earlier and idle 0.16 s later; its peak is 14 MB higher (was 36 MB) and its steady heap 8 MB larger (the open store: eight read connections and one byte source per item buffer, 8099); its mesh build reads 0.22 s longer than the file's (2037 against 1817 ms median), the remaining load cost; frame rate equal within the spread.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | automatic / clustered (Headset) |
| Lights | point 33/33 shaded, 1 shadow-mapped (0 cached); spot 10/10 shaded, 2 shadow-mapped (2 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+4, spot 2+4, point 1+4 |
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
| Frames per second | 105.95 [105.12..107.20] |
| Frame wall time (ms) | 9.19 [9.09..9.35] |
| M2 / R / M3 (ms) | 0.83 [0.81..0.85] / 3.42 [3.32..3.50] / 0.69 [0.67..0.70] |
| App GPU frame time, XR counter (ms) | 6.75 [6.68..6.79] |
| GPU utilization, XR counter (%) | 95.3 [94.1..95.4] |
| CPU utilization average / worst, XR counter (%) | 25.8 [25.2..26.5] / 31.1 [27.3..32.0] |
| GPU pass Cull classification (ms) | 0.052 [0.052..0.053] |
| GPU pass Cull command generation (ms) | 0.126 [0.124..0.130] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.040 [0.040..0.040] |
| GPU pass Light cluster binning Headset (ms) | 0.053 [0.053..0.053] |
| GPU pass XR multiview (ms) | 8.261 [8.212..8.295] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 8.548 [8.504..8.584] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 105.27 | 0 -> 0; 0 -> 0, step-downs 0 | 59.7 -> 60.4 | 60.2 -> 59.8 | 57.3 -> 57.5 | 38.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2362 [1920..2362] cap 2362/2362 | 456 cap 456 | 95 | 29.6 |
| 1 | 105.12 | 0 -> 0; 0 -> 0, step-downs 0 | 62.3 -> 63.0 | 62.9 -> 62.5 | 60.3 -> 60.2 | 38.0, 99 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2362 [2208..2362] cap 2362/2362 | 456 cap 456 | 94 | 31.6 |
| 2 | 106.64 | 0 -> 0; 0 -> 0, step-downs 0 | 64.1 -> 64.1 | 64.6 -> 64.1 | 62.3 -> 62.0 | 40.0, 99 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2362 [1920..2362] cap 2362/2362 | 456 cap 456 | 94 | 30.1 |
| 3 | 107.20 | 0 -> 0; 0 -> 0, step-downs 0 | 65.4 -> 65.0 | 65.8 -> 65.3 | 63.5 -> 63.2 | 40.0, 99 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2208 [1920..2362] cap 2362/2362 | 456 cap 456 | 94 | 25.7 |


## 2026-11-07 22:15 - bistro_street_120, no configuration file

- Runs: 4 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261107_221511_bistro_street_120_0.json`, `20261107_221611_bistro_street_120_0.json`, `20261107_221702_bistro_street_120_0.json`, `20261107_221755_bistro_street_120_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (802820c69); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src.quest.erhe_prepared` (listed 4.81 s, idle 4.81 s).
- Scene (R13.4 re-measure after D8 steps 11b - 15): the quest prepared asset of `scripts/prepared_asset_bistro_verify.py --keep-quest` (made 2026-11-07 16:38, PVS `bits_hash` `fd5d5edadffa53f7`, `ready`), APK of this tree after a clean install (the app's settings reset: light shading loops, unlike the 07:47 entries), runs alternated with the following entry's after one uncounted warmup. Per run, listed / idle s, VmHWM MB, fps: 4.81 / 4.81, 826, 83.39; 4.95 / 4.95, 800, 83.13; 4.50 / 4.83, 800, 83.26; 4.46 / 4.46, 788, 83.06 (median listed 4.66 s, VmHWM 800 MB). Draws 393 ASTC 4x4 textures (1806 MB) through 44 draw lists.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | point 0/33 shaded, 0 shadow-mapped (0 cached); spot 1/11 shaded, 1 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 0+0, spot 1+0, point 0+0 |
| Shadows | on; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, receiver_plane, cull_back |
| Light cluster binning | gpu |
| Culling classification | - |
| Occlusion culling | on (gpu supported) |
| Content draws | all |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 83.20 [83.06..83.39] |
| Frame wall time (ms) | 11.68 [11.59..11.81] |
| M2 / R / M3 (ms) | 0.79 [0.79..0.79] / 3.53 [3.46..3.57] / 0.64 [0.64..0.65] |
| App GPU frame time, XR counter (ms) | 8.47 [8.28..8.55] |
| GPU utilization, XR counter (%) | 96.2 [96.2..96.6] |
| CPU utilization average / worst, XR counter (%) | 20.9 [20.4..21.8] / 25.0 [23.2..27.7] |
| GPU pass Cull classification (ms) | 0.078 [0.077..0.078] |
| GPU pass Cull command generation (ms) | 0.101 [0.098..0.105] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.041 [0.041..0.042] |
| GPU pass Shadow 0 (ms) | 1.537 [1.529..1.541] |
| GPU pass XR multiview (ms) | 8.576 [8.563..8.585] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 11.314 [11.293..11.327] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 83.39 | 0 -> 0; 0 -> 0, step-downs 0 | 57.2 -> 57.7 | 57.6 -> 57.6 | 54.8 -> 54.9 | 31.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1987 [1920..2362] cap 2362/2362 | 456 cap 456 | 96 | 29.7 |
| 1 | 83.13 | 0 -> 0; 0 -> 0, step-downs 0 | 59.5 -> 59.5 | 59.9 -> 59.6 | 57.3 -> 57.1 | 32.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2141 [1920..2362] cap 2362/2362 | 456 cap 456 | 96 | 27.9 |
| 2 | 83.26 | 0 -> 0; 0 -> 0, step-downs 0 | 60.4 -> 60.7 | 61.0 -> 60.5 | 58.3 -> 58.1 | 33.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2208 [1920..2362] cap 2362/2362 | 456 cap 456 | 96 | 26.4 |
| 3 | 83.06 | 0 -> 0; 0 -> 0, step-downs 0 | 61.5 -> 61.7 | 62.0 -> 61.4 | 59.4 -> 59.3 | 33.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2362 cap 2362/2362 | 456 cap 456 | 96 | 25.2 |


## 2026-11-07 22:15 - bistro_street_120, no configuration file

- Runs: 4 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261107_221545_bistro_street_120_0.json`, `20261107_221640_bistro_street_120_0.json`, `20261107_221730_bistro_street_120_0.json`, `20261107_221823_bistro_street_120_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (802820c69); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src_quest.glb` (listed 8.41 s, idle 8.41 s).
- Scene: the prepared glTF file of the 07:47 entries, same APK, alternated with the previous entry's runs. Per run, listed / idle s, VmHWM MB, fps: 8.41 / 8.41, 826, 100.93 (the first open on this install: it derives and stores the device-side editor asset); 5.09 / 5.09, 783, 101.76; 5.60 / 5.60, 782, 100.43; 5.20 / 5.20, 793, 100.56 (median of the last three listed 5.20 s, VmHWM 783 MB). Not a baseline any more: a glTF file parse reads no derived data (R12.2: `ERHE_texture_ktx2`, `ERHE_pvs`, ...), so every draw list samples no texture (28 lists) and the scene has no PVS; the fps gap to the previous entry is texturing, not the prepared asset.

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | point 0/33 shaded, 0 shadow-mapped (0 cached); spot 1/11 shaded, 1 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 0+0, spot 1+0, point 0+0 |
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
| Frames per second | 100.75 [100.43..101.76] |
| Frame wall time (ms) | 9.55 [9.24..9.66] |
| M2 / R / M3 (ms) | 0.76 [0.74..0.83] / 3.31 [3.24..3.82] / 0.64 [0.64..0.70] |
| App GPU frame time, XR counter (ms) | 7.06 [7.02..7.30] |
| GPU utilization, XR counter (%) | 95.7 [94.6..96.3] |
| CPU utilization average / worst, XR counter (%) | 23.0 [22.5..24.2] / 27.1 [25.8..30.6] |
| GPU pass Cull classification (ms) | 0.055 [0.055..0.056] |
| GPU pass Cull command generation (ms) | 0.093 [0.088..0.097] |
| GPU pass ImGui Hotbar imgui host (ms) | 0.041 [0.041..0.041] |
| GPU pass Shadow 0 (ms) | 1.508 [1.500..1.515] |
| GPU pass XR multiview (ms) | 7.074 [7.038..7.108] |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 8.893 [8.783..8.939] |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 100.93 | 0 -> 0; 0 -> 0, step-downs 0 | 58.5 -> 58.6 | 58.9 -> 58.4 | 56.0 -> 56.0 | 30.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2362 [1920..2362] cap 2362/2362 | 456 cap 456 | 95 | 27.5 |
| 1 | 101.76 | 0 -> 0; 0 -> 0, step-downs 0 | 60.3 -> 60.1 | 60.7 -> 59.9 | 58.0 -> 57.7 | 33.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2362 [1920..2362] cap 2362/2362 | 456 cap 456 | 94 | 34.0 |
| 2 | 100.43 | 0 -> 0; 0 -> 0, step-downs 0 | 62.0 -> 61.6 | 61.8 -> 61.2 | 59.1 -> 58.9 | 32.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1920 [1920..2362] cap 2362/2362 | 456 cap 456 | 96 | 28.8 |
| 3 | 100.56 | 0 -> 0; 0 -> 0, step-downs 0 | 62.6 -> 62.8 | 63.1 -> 62.5 | 60.6 -> 60.2 | 33.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1920 [1651..2362] cap 2362/2362 | 456 cap 456 | 95 | 29.7 |


## 2026-11-08 10:14 - bistro_street_120, single_directional_loops_gpu_occlusion

As single_directional_loops, with culling classification gpu and occlusion culling on.

- Runs: 1 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261108_101445_bistro_street_120_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (802820c69); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src.quest.erhe_prepared` (listed 5.13 s, idle 5.13 s).
- Configuration calls: set_culling_classification, set_occlusion_culling, set_graphics_preset, set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (0 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/11 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
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
| Frames per second | 58.89 |
| Frame wall time (ms) | 16.68 |
| M2 / R / M3 (ms) | 0.83 / 4.52 / 0.65 |
| App GPU frame time, XR counter (ms) | 12.41 |
| GPU utilization, XR counter (%) | 96.6 |
| CPU utilization average / worst, XR counter (%) | 19.0 / 22.8 |
| GPU pass Cull classification (ms) | 0.076 |
| GPU pass Cull command generation (ms) | 0.128 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.044 |
| GPU pass Shadow 0 (ms) | 7.598 |
| GPU pass XR multiview (ms) | 8.318 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 16.054 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 58.89 | 0 -> 0; 0 -> 0, step-downs 0 | 54.6 -> 55.0 | 54.5 -> 54.7 | 52.5 -> 52.8 | 32.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1920 [1920..2362] cap 2362/2362 | 456 cap 456 | 96 | 23.1 |


## 2026-11-08 10:15 - bistro_street_120, single_directional_loops_gpu_occlusion

As single_directional_loops, with culling classification gpu and occlusion culling on.

- Runs: 1 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261108_101531_bistro_street_120_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (802820c69); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src.quest.erhe_prepared` (listed 5.08 s, idle 5.08 s).
- Configuration calls: set_culling_classification, set_occlusion_culling, set_graphics_preset, set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (0 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/11 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
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
| Frames per second | 58.43 |
| Frame wall time (ms) | 16.55 |
| M2 / R / M3 (ms) | 0.83 / 4.51 / 0.64 |
| App GPU frame time, XR counter (ms) | 11.98 |
| GPU utilization, XR counter (%) | 96.5 |
| CPU utilization average / worst, XR counter (%) | 16.5 / 20.6 |
| GPU pass Cull classification (ms) | 0.077 |
| GPU pass Cull command generation (ms) | 0.129 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.043 |
| GPU pass Shadow 0 (ms) | 7.627 |
| GPU pass XR multiview (ms) | 8.418 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 16.250 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 58.43 | 0 -> 0; 0 -> 0, step-downs 0 | 56.3 -> 56.8 | 56.3 -> 56.1 | 54.4 -> 54.4 | 33.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1920 [1651..2362] cap 2362/2362 | 456 cap 456 | 96 | 24.2 |


## 2026-11-08 12:06 - bistro_street_120, single_directional_loops_gpu_occlusion

As single_directional_loops, with culling classification gpu and occlusion culling on.

- Runs: 1 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261108_120618_bistro_street_120_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (4be47d3c5); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src.quest.erhe_prepared` (listed 5.18 s, idle 5.18 s).
- Configuration calls: set_culling_classification, set_occlusion_culling, set_graphics_preset, set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (0 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/11 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
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
| Frames per second | 58.69 |
| Frame wall time (ms) | 16.61 |
| M2 / R / M3 (ms) | 0.82 / 4.52 / 0.64 |
| App GPU frame time, XR counter (ms) | 11.63 |
| GPU utilization, XR counter (%) | 96.9 |
| CPU utilization average / worst, XR counter (%) | 18.7 / 22.0 |
| GPU pass Cull classification (ms) | 0.077 |
| GPU pass Cull command generation (ms) | 0.136 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.043 |
| GPU pass Shadow 0 (ms) | 7.381 |
| GPU pass XR multiview (ms) | 8.454 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 16.054 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 58.69 | 0 -> 0; 0 -> 0, step-downs 0 | 57.4 -> 57.4 | 57.1 -> 57.0 | 55.3 -> 55.2 | 33.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1920 [1651..2362] cap 2362/2362 | 456 cap 456 | 96 | 26.6 |


## 2026-11-08 12:07 - bistro_street_120, single_directional_loops_gpu_occlusion

As single_directional_loops, with culling classification gpu and occlusion culling on.

- Runs: 1 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261108_120704_bistro_street_120_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (4be47d3c5); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src.quest.erhe_prepared` (listed 9.26 s, idle 9.26 s).
- Configuration calls: set_culling_classification, set_occlusion_culling, set_graphics_preset, set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (0 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/11 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+0, spot 0+0, point 0+0 |
| Shadows | on; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, normal_offset, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | on (gpu supported) |
| Content draws | all |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 107.97 |
| Frame wall time (ms) | 8.97 |
| M2 / R / M3 (ms) | 0.89 / 5.15 / 0.71 |
| App GPU frame time, XR counter (ms) | 6.35 |
| GPU utilization, XR counter (%) | 94.5 |
| CPU utilization average / worst, XR counter (%) | 30.9 / 35.7 |
| GPU pass Cull classification (ms) | 0.076 |
| GPU pass Cull command generation (ms) | 0.100 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.041 |
| GPU pass Shadow 0 (ms) | 1.036 |
| GPU pass XR multiview (ms) | 6.864 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 8.321 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 107.97 | 0 -> 0; 0 -> 0, step-downs 0 | 59.1 -> 59.4 | 59.3 -> 59.4 | 56.6 -> 56.8 | 34.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 2362 [2208..2362] cap 2362/2362 | 456 cap 456 | 94 | 36.3 |


## 2026-11-08 12:07 - bistro_street_120, single_directional_loops_gpu_occlusion

As single_directional_loops, with culling classification gpu and occlusion culling on.

- Runs: 1 x 600 measured frames (warmup 1500), display 120.0 Hz, input_hash fccc71be85e379fd; results `20261108_120741_bistro_street_120_0.json` (`memory-bank/local/quest_runs/`, per machine).
- Build: erhe 1.0 (4be47d3c5); Quest 3, OS UP1A.231005.007.A1; performance levels CPU e_unset / GPU e_unset.
- Launch: `--benchmark /sdcard/Android/data/org.libsdl.app.quest/files/bench/benchmark_bistro_street_120.json --no-scene`; scene `bistro_src.quest.erhe_prepared` (listed 4.61 s, idle 4.61 s).
- Configuration calls: set_culling_classification, set_occlusion_culling, set_graphics_preset, set_graphics_preset, create_light, set_node_transform (arguments in the result files' `host` block).

| Configuration | |
|---|---|
| Light shading (setting / in effect) | loops / loops (Headset) |
| Lights | directional 1/1 shaded, 1 shadow-mapped (0 cached); point 0/33 shaded, 0 shadow-mapped (0 cached); spot 0/11 shaded, 0 shadow-mapped (0 cached) (cached: the shadow map cache reused the map in the measured window, no shadow pass drawn) |
| Light limits shadow+unshadowed | directional 1+0, spot 0+0, point 0+0 |
| Shadows | on; 1024 px, point 512 px, format_d16_unorm (16 bits), depth, hard, normal_offset, cull_back |
| Light cluster binning | gpu |
| Culling classification | gpu (gpu supported) |
| Occlusion culling | on (gpu supported) |
| Content draws | all |
| Draw lists | on |
| Specular anti-aliasing | normal_kernel |
| Preset | OpenXR |

| Result (median [min..max] of the runs' medians) | |
|---|---|
| Frames per second | 107.01 |
| Frame wall time (ms) | 8.94 |
| M2 / R / M3 (ms) | 0.85 / 5.21 / 0.68 |
| App GPU frame time, XR counter (ms) | 6.46 |
| GPU utilization, XR counter (%) | 94.8 |
| CPU utilization average / worst, XR counter (%) | 30.7 / 37.6 |
| GPU pass Cull classification (ms) | 0.076 |
| GPU pass Cull command generation (ms) | 0.100 |
| GPU pass ImGui Hotbar imgui host (ms) | 0.041 |
| GPU pass Shadow 0 (ms) | 1.038 |
| GPU pass XR multiview (ms) | 7.003 |
| GPU pass XR multiview line compute (ms) | 0.001 |
| GPU pass (total) (ms) | 8.442 |

Quest state per run (temperatures: thermal HAL maximum per type at the start of the measured window -> at the result line, deg C; clocks: median [min..max] over the measured window, MHz, with the scaling cap; usage: /proc/stat over the window).

| Run | fps | Thermal status (service; editor) | CPU C | GPU C | Skin C | Battery C, level | CPU clocks | GPU clock | GPU busy % | CPU usage % |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 107.01 | 0 -> 0; 0 -> 0, step-downs 0 | 59.7 -> 60.3 | 59.8 -> 59.8 | 57.1 -> 57.4 | 35.0, 100 % | cpu 0 1: 1920 cap 1920/2054; cpu 2 3 4 5: 1920 [1651..2362] cap 2362/2362 | 456 cap 456 | 94 | 34.9 |

