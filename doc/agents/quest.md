# Quest / Android sessions

Stability: stable

What an agent session working on the Quest (or `mobile`-flavor Android) build
needs beyond `AGENTS.md`. Building, packaging and the flavors are described in
`doc/quest.md` and `doc/android.md`.

## Skills

- **`erhe-quest-launch`** for any Quest build / install / launch /
  log-capture work: canonical script invocations, the persistent logcat
  capture (`scripts/quest_logcat.sh`, started BEFORE every launch), erhe-only
  log filtering, readiness signals, Android SDK/JDK probing, and the mobile
  sideload-dialog workaround.
- **`erhe-quest-validation`** makes silent GPU hangs abort loudly.
- **`erhe-quest-shader-failure`** triages vkCreateShaderModule / spirv-val
  aborts at startup.
- RenderDoc captures on Quest: `doc/agents/quest_renderdoc_capture.md`.

## Launch protocol

- **Interactive launch** (the user will wear the headset): install the APK
  first, prompt the user to put the headset on and activate the
  controllers, and wait for explicit confirmation before EVERY one-off
  `adb shell am start`; a confirmation covers only that launch.
- **Automated run** (`scripts/quest_bench.py`, `scripts/soak_quest.py`, any
  non-interactive test): no user prompt. The proximity-sensor bypass is
  required before the launch, and normal sensor operation must be restored
  after each run, also on failure or interruption; the commands are in
  "Off-head runs" below.
- Pure builds and installs never need a prompt. Poll for a readiness signal
  (recipes in the skill) instead of sleeping.

## Vulkan validation on Quest

Without `VK_LAYER_KHRONOS_validation`, GPU-side errors are silent hangs. The
layer .so is bundled in every APK by default; the runtime knob is
`"vulkan_validation_layers"` in `config/editor/erhe_graphics.json`
(device-level errors then abort via `ERHE_FATAL`). Leave the knob off for
normal runs (per-frame overhead); the bundled .so alone costs nothing -- see
`erhe-quest-validation` for the full flow including the SPIR-V cache wipe.

## Reaching the MCP server on the device

On device the in-editor MCP server binds the same loopback address
(`127.0.0.1:3743`), unreachable from the host directly -- forward it over adb
(`adb forward tcp:3743 tcp:3743`, re-run after each device reconnect) and then
drive it exactly as on desktop (`doc/agents/editor_runs.md`). The APK declares
the `INTERNET` permission (`android-project/app/src/main/AndroidManifest.xml`):
Android gates all socket creation -- even loopback -- behind it, so without it
`bind()` returns `EACCES` on every port and the fallback loop logs
`failed to bind any port in [3743, 3763)` (a blanket denial, not a port
clash). Confirm the grant with
`adb shell dumpsys package org.libsdl.app.quest | grep INTERNET`. See
`doc/agents/mcp_server_usage.md` for the full recipe.

## Bistro scene data on the device

The Quest's Bistro file is prepared on the desktop with the headless editor
(never on the device) and pushed with adb:

1. Open the source `res/editor/assets/bistro_lights_compressed/bistro_lights_c.gltf`
   (foreign glTF: an import root node `bistro_lights_c.gltf`). The source
   is Z-up (the city lies on its side in erhe's Y-up world): set the import
   root's rotation to -90 degrees about X (`set_node_transform`,
   `rotation_xyzw` [-0.7071068, 0, 0, 0.7071068]). The save composes the
   import root's transform into its children.
2. `generate_lods` with `scope: "scene"` (default settings), wait for
   `get_async_status` idle, `save_scene` to a scratch file.
3. Open that file, run `bake_static_to_world_space` on it (or set the
   scene's `export_settings.bake_static_to_world_space` false for a
   node-space file; `set_scene_settings` needs `"_version": 2` on the
   settings object, else `export_settings` is dropped), bake the PVS there
   (`pvs_bake`, cell size 4 m, the street slab region: content bounds in x
   and z, y 0.6 - 4.6), then `save_scene_for_quest`. A bake made before the
   save reloads stale (the save folds the import root rotation into the
   children's TRS, so the reloaded bounds differ by rounding from the baked
   ones), and so does a PVS of meshes that Prepare world-bakes itself (the
   columns keep the unbaked transforms and bounds): bake to world space
   before the PVS bake.
4. Reload the result and check `get_pvs_status` state `ready`.
5. Push it into the app's external files directory
   (`/sdcard/Android/data/org.libsdl.app.quest/files/bistro/`). The editor
   creates that directory, owned by the app, at its first start
   (`SDL_GetAndroidExternalStoragePath()` in `main.cpp`); after a clean
   install, launch once before pushing (a directory adb creates first is
   owned by the shell user, and the editor cannot open the files in it).

The current files (2026-11-06, Linux headless Release on NVIDIA) are
`bistro_quest_lod_cell_4m_v4_ws.glb` (static meshes in world space, PVS
`bits_hash` `76620449951931a4`) and `bistro_quest_lod_cell_4m_v4_node.glb`
(the same in node space, `56f7c094d19a1f1b`), both 2068 cells and LOD
chains on 2900 primitives. A file's prepared mesh data is rejected when its
stored BVHs come from another build of the BVH library
(`ERHE_raytrace_bvh: no build for the running backend and library`); the
Quest then rebuilds every primitive and the load does not go idle within
`quest_bench.py`'s timeout: `bistro_quest_lod_cell_4m_v3.glb` (Mac, Nov 1)
does that with the current APK. The older
`pvs_bistro_*cell_*m.glb` and `bistro_quest_lod_cell_4m.glb` files were
baked with the city on its side, and `config/editor/benchmark_bistro*.json`
poses are in that frame: re-pose the benchmarks before comparing runs.

## Benchmark runs

The editor's benchmark mode (`--benchmark <file>`, a timedemo run: fixed
simulation step per frame, camera on a scripted path, controllers
ignored, result logged and written to
`/sdcard/Android/data/org.libsdl.app.quest/files/logs/benchmark_<name>.json`,
the app's external files directory, readable with plain `adb` on a release
APK; then exit) is specified in `doc/plans/quest_performance.md` "Benchmark
mode". `scripts/quest_bench.py` drives it on the device:

```
py -3 scripts/quest_bench.py run --bench config/editor/benchmark_orbit.json [--scene config/editor/commands_bench_heavy.json] [--worn] [--gpu-metrics] [--repeat N] [--push]
py -3 scripts/quest_bench.py compare <a.json> <b.json>
```

`run` installs nothing: install the APK first (`scripts\install_android.bat
quest release`). Each run force-stops the app, starts
`scripts/quest_logcat.sh`, launches with the `args` intent extra, waits
(bounded by `--timeout`, default 600 s) for the `Benchmark result <name>:`
or `Benchmark failed <name>:` line in the capture, pulls the result JSON
with `adb exec-out cat` (no `run-as`), and stores it under `memory-bank/local/quest_runs/`
(gitignored). `--repeat N` launches N runs and stores a repeat summary
(per-term median across runs, min / max spread). `--gpu-metrics` runs
`ovrgpuprofiler` for the measured window; its metric ids come from
`--gpu-metric-ids`, else an `ovrgpuprofiler_ids::<id,id,...>` line in
`memory-bank/local/quest.md`, else the names listed in the script's
`DEFAULT_GPU_METRIC_NAMES` looked up in `ovrgpuprofiler -m`, always with the
GPU frequency metrics. The APK's `config/` files are copied to the
writable directory only when missing there, so an edited benchmark or
commands file reaches the device through a rebuild and a clean reinstall,
or through `--push` (copies the local file over the device's copy), which
needs a debuggable APK (`run-as`) and fails on a release APK. `compare` prints two results
or repeat summaries side by side with the deltas and whether their
`input_hash` values match (same camera poses and `frame_dt`). Measure one
editor at a time with nothing building on the host
(`doc/plans/quest_performance.md` "Measurement rule").

### MCP during a run

`quest_bench.py run` drives the editor over MCP while it runs: with `--mcp
TOOL[=JSON]` (repeatable), `--load-scene <device path>` or `--mcp-measure
TOOL[=JSON]` it reads the editor's `MCP server: listening on
127.0.0.1:<port>` line, waits for frame 12, forwards host port `--mcp-port`
(default 3743) to it with `adb forward` (removed after the run), makes the
`--mcp` calls and the scene load (`load_scene`, wait idle,
`set_headset_scene`) during the warmup and the `--mcp-measure` calls in the
measured window, storing their replies in the result's `host` block. The
warmup must outlast the calls (the run fails when the measured window
starts first). `--bench-to-device` pushes the benchmark file into the
app's external files directory with plain adb, so a release APK takes a
new benchmark file without a rebuild; `--editor-args=...` appends editor
arguments (`--no-scene`). From Git Bash set `MSYS_NO_PATHCONV=1`, or
device paths such as `/sdcard/...` are rewritten. Example, the Bistro
frame with a scene file pushed to the device before:

```
py -3 scripts/quest_bench.py run --bench config/editor/benchmark_bistro.json --bench-to-device --editor-args=--no-scene --mcp "set_culling_classification={\"mode\": \"gpu\"}" --load-scene /sdcard/Android/data/org.libsdl.app.quest/files/<dir>/<scene>.glb --mcp-measure get_composition_passes
```

`--mcp-after-load TOOL[=JSON]` (repeatable) calls are made after the scene
load; `$SCENE` in their arguments is replaced by the loaded scene's name
(`create_light` and `set_node_transform` need it).

### Bench configurations and the run record

A configuration under test (lighting, culling, draw lists) is a file in
`scripts/quest_bench_configs/` passed with `--config`: `name`,
`description` and the lists `mcp`, `mcp_after_load` and `mcp_measure` of
`TOOL[=JSON]` strings, made before the command line's calls of the same
kind. `bistro_single_directional.json` adds one shadow-mapped directional
light to the Bistro, sets the preset's spot and point limits to 0 and
`light_shading` to `loops` (no clustering). A new light configuration is a
new file; `set_graphics_preset` (light limits, `light_shading`, shadows,
`use_draw_lists`), `set_culling_classification`, `set_occlusion_culling`,
`set_light_cluster_binning`, `set_content_draws` (`opaque_only` leaves out
the alpha-tested and translucent draws), `create_light` and `edit_light`
cover the variations.

Every run that uses MCP stores the configuration in effect in the result's
`host.config_state` (the preset, the culling, occlusion, binning and content draws
settings during the warmup; the light shading in effect and the shaded /
shadow-mapped lights from `get_light_cluster_stats` and `get_scene_lights`
in the measured window), and every run stores the Quest's state in
`host.device_state`: adb snapshots before the launch, at the start of the
measured window and at the result line (cpufreq clocks and caps per
policy, the kgsl GPU clock, cap and busy %, thermal zones, the thermal
service's status and HAL temperatures, battery) and samples every
`--sample-interval` seconds (default 2) through the measured window.

`--record` appends the invocation's entry to
`doc/reference/quest_bench_runs.md`, the record of past runs; record every
Bistro run there and commit it. `quest_bench.py record <run.json>...`
appends the entry of stored per-run results. The Bistro, single
directional light, clustering off:

```
py -3 scripts/quest_bench.py run --bench config/editor/benchmark_bistro.json --bench-to-device --editor-args=--no-scene --load-scene /sdcard/Android/data/org.libsdl.app.quest/files/bistro/<scene>.glb --config scripts/quest_bench_configs/bistro_single_directional.json --record
```

`--perfetto` adds a Perfetto trace of the measured window, taken with the
Meta VR CLI (`metavr`, installed with the Meta VR Claude Code plugin or
from npm; the script finds it on PATH or in `~/.metavr/bin/`): GPU render
stages and the Adreno counters, 4 s starting 3 s after the "scene settled"
line. The result's `host.perfetto` holds the trace path (under
`~/.config/odh/traces/`) and its summary, and the record adds the eye
surface's line and a counter table. The render stages split the eye pass
(the 1680x1760 two-view surface) into binning, per-bin rendering and
preemption: the compositor preempts a long app pass at every display
refresh, and its time is inside the `XR multiview` timer but not in the XR
counter's app GPU frame time. Binning plus bin rendering is the app's own
GPU work. The counters are device-wide and count the compositor's work as
well, which dominates when preemption is large. A trace opens in
ui.perfetto.dev; the metavr MCP tools (`perf_trace_query_sql`,
`perf_trace_gpu_counters`) and `metavr perf query <session> "<SQL>"` query
it. `--perfetto` and `--gpu-metrics` are exclusive; a 4 s capture did not
change the `XR multiview` timer (45.6 ms with, 45.8 ms without, Bistro
single light with occlusion culling; a hand-taken capture before
`--perfetto` existed, runs not in the record).

The app's external files directory must be created by the app itself:
Android creates it, owned by the app's uid, the first time the editor asks
for it, which is when a benchmark writes its result. After a clean install
it does not exist; a directory that `adb shell mkdir` or `adb push` creates
first is owned by the shell user, and the editor then fails with
`Benchmark failed <path>: file not readable`. After a clean install, run
the APK-bundled benchmark once
(`quest_bench.py run --bench config/editor/benchmark_orbit.json`) before
pushing anything there; if adb already created the directory,
`adb shell rm -rf /sdcard/Android/data/org.libsdl.app.quest` first.

### Off-head runs

An automated run (Launch protocol above) wraps each launch in this
sequence; `quest_bench.py` does so for every run unless `--worn` (an
interactive run the user confirmed) skips it. Verified with the headset
lying on the desk on a Quest 3 (2026-10-24): the session
reaches FOCUSED, frame 12 is logged after about 4 s and the phase log runs
at the full 72 Hz, as when worn.

Before the launch:

```
adb shell am force-stop org.libsdl.app.quest
adb shell input keyevent KEYCODE_WAKEUP
adb shell am broadcast -a com.oculus.vrpowermanager.prox_close    # the runtime treats the headset as worn
adb shell setprop debug.oculus.guardian_pause 1                     # no boundary interruptions
adb shell setprop debug.oculus.refreshRate 120                      # only when the benchmark file sets display_refresh_rate_hz (its value)
```

The refresh rate property is what switches the display: on the current OS
build the runtime offers 72, 80, 90 and 120 Hz and accepts
`xrRequestDisplayRefreshRateFB(120)`, but the display stays at 72 Hz until
the property is set (verified 2026-10-24). The editor still requests the
rate and its settle gate waits for the achieved rate.

then the normal launch
(`am start -W -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -c com.oculus.intent.category.VR -n org.libsdl.app.quest/org.libsdl.app.ErheActivity ...`).
After the run, always (also on failure or Ctrl-C; the script does this in
a `finally`):

```
adb shell am force-stop org.libsdl.app.quest
adb shell am broadcast -a com.oculus.vrpowermanager.automation_disable
adb shell setprop debug.oculus.guardian_pause 0
adb shell setprop debug.oculus.refreshRate '""'                    # when it was set
```

Readiness comes from the logcat capture (the session FOCUSED line, frame
12, the benchmark's own lines), never from a blind sleep.
