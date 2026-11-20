# Kit thumbnail burst frame times (2026-11-20)

The acceptance measurement of `doc/plans/worker_executors.md` W8: frame
times of the editor while the Level Kit window makes every kit shape
thumbnail, in the three ways of W8.1. "Today" is the pipeline before build
step 8: thumbnails built on the main thread, two shapes per frame. "Background
4" and "background = foreground" are the pipeline of build step 8
(`doc/editor/level_tools.md` "Shape thumbnails"): each shape is built by a
task of the background executor and the main thread renders the ready ones,
with the background executor at its default 4 threads and at the foreground
executor's thread count.

## How it is measured

`python3 scripts/worker_executors_bench.py --editor <tree>/bin/editor
[--cwd <dir>]` launches the editor with `ERHE_AI_DRIVER=1`,
`ERHE_DEFAULT_LAYOUT=1` and the wall clock (`ERHE_DETERMINISTIC_CLOCK=0`),
in the repo root or in the working directory `--cwd` (its own `config/`,
`res/` and `logs/`), with the startup scene of `config/editor/commands.json`
("Default Scene"). It reads the main loop's tick durations through MCP
`get_frame_ticks` (`doc/agents/mcp_server_usage.md`), one call per frame, so
the series has no gaps; a tick is `start_frame()` begin to `frame()` end, the
loop period. Phases:

- startup: the end of the first tick from which every tick reads
  `get_async_status` idle, in seconds since the editor process started
  (the "first interactive frame" of W8.4);
- rest: 300 ticks of the startup scene after `get_async_status` read idle in
  three consecutive frames;
- burst: the Level Kit window shown (`set_window_visibility`, focused, not
  docked) and every thumbnail requested (`level_get_kit_thumbnails
  request_all`), from the tick that served the request to the one that served
  the reply reporting no thumbnail `none`, `queued` or `building`; its
  duration is the sum of its tick durations;
- after: 300 ticks with the window open and every thumbnail made.

Each phase reports the median, the 99th percentile (nearest rank) and the
longest tick. The script also records the executors of the run
(`get_executor_status`: worker counts, requested and read-back priorities)
and the log's "Background apply took" warnings (W4.1) and late mesh upload
errors.

## Configuration

- Build: Release, Vulkan, no window library (the headless tree of
  `scripts/configure_ninja_linux_vulkan_headless.sh`; the emulated swapchain
  presents without a display, so the frame rate is not paced to a refresh).
- Machine: Linux desktop, Intel Core Ultra 9 290HX Plus (24 hardware
  threads), NVIDIA GeForce RTX 5070 Laptop GPU selected with
  `VK_DRIVER_FILES`, implicit Vulkan layers disabled.
- Editor settings: `threading.thread_count` 0 (one worker per hardware
  thread: 24 foreground workers), `render_thread` `dedicated_thread`,
  `imgui_overlap` on; `thumbnails.size_pixels` 256. The kit library has 104
  shapes; none fails.
- Today: the repo root's `config/` of that day, which carried local edits
  beside the committed settings. Background 4 and background = foreground:
  a working directory (`--cwd`) holding the committed `config/`, the second
  with `threading.background_thread_count` 0 (one worker per hardware
  thread). `get_executor_status` reads back 24 foreground workers at nice 0
  and 4, respectively 24, background workers at nice 10.

The resting tick of the two background columns is about 1.06 ms against
1.53 ms today: the configurations differ, so each run is read against its
own resting median (W8.2), not against another column's.

## Runs

Times in milliseconds unless marked; burst ticks in parentheses.

| Run | Startup (s) | Rest median / p99 / max | Burst median / p99 / max | Burst duration (s) | After median / p99 / max | Apply warnings |
|---|---|---|---|---|---|---|
| today 1 | 0.64 | 1.53 / 1.71 / 2.07 | 8.98 / 26.54 / 26.54 (55) | 0.54 | 1.52 / 1.73 / 8.36 | - |
| today 2 | 0.53 | 1.53 / 1.80 / 2.25 | 9.04 / 26.10 / 26.10 (55) | 0.54 | 1.52 / 1.77 / 2.53 | - |
| background 4, 1 | 0.61 | 1.04 / 1.36 / 2.29 | 1.10 / 1.77 / 1.77 (96) | 0.11 | 1.05 / 1.24 / 2.13 | 0 |
| background 4, 2 | 0.54 | 1.07 / 1.43 / 2.34 | 1.12 / 2.26 / 2.26 (90) | 0.10 | 1.07 / 1.39 / 2.40 | 0 |
| background 4, 3 | 0.55 | 1.06 / 1.44 / 2.39 | 1.12 / 2.01 / 2.01 (89) | 0.10 | 1.07 / 1.19 / 2.12 | 0 |
| background 24, 1 | 0.55 | 1.06 / 1.39 / 2.31 | 1.34 / 4.71 / 4.71 (34) | 0.06 | 1.07 / 1.62 / 2.11 | 1 (2.49 ms) |
| background 24, 2 | 0.59 | 1.07 / 1.37 / 2.49 | 1.07 / 6.08 / 6.08 (36) | 0.05 | 1.06 / 1.43 / 2.32 | 1 (2.43 ms) |
| background 24, 3 | 0.55 | 1.06 / 1.38 / 2.31 | 1.04 / 8.38 / 8.38 (31) | 0.05 | 1.06 / 1.36 / 2.47 | 1 (3.87 ms) |

No run logs a late mesh upload, and no thumbnail fails.

## Results

The run per column is the one with the median longest burst tick (today:
run 2 of its two).

| | Today (main thread) | Background 4 | Background = foreground (24) |
|---|---|---|---|
| Rest median | 1.53 | 1.06 | 1.07 |
| Burst median | 9.04 | 1.12 | 1.07 |
| Burst p99 | 26.10 | 2.01 | 6.08 |
| Burst longest | 26.10 | 2.01 | 6.08 |
| Burst duration (s) | 0.54 | 0.10 | 0.05 |
| Startup (s) | 0.53 | 0.55 | 0.59 |

A burst has 31 to 96 ticks, so its 99th percentile (nearest rank) is its
longest tick in every run: the p99 row rests on one tick.

## Read against W8.2

With the background executor at 4 threads the longest burst tick of the
median run is 2.01 ms against twice its resting median, 2.12 ms: the goal is
met in that run and in run 1 (1.77 ms against 2.08 ms) and missed by
0.12 ms in run 2 (2.26 ms against 2.14 ms). The burst median is 1.06 times
the resting median; today's longest burst tick was 17 times it.

## Read against W8.3

The wide setting finishes the 104 thumbnails in half the time (0.05 s
against 0.10 s), and its longest burst tick is 3 to 4 times that of the
default (4.7 to 8.4 ms against 1.8 to 2.3 ms). The default costs about
0.05 s of completion time and saves about 4 ms on the worst frame.

## Read against W8.4

Startup to the first interactive frame is 0.54 to 0.61 s with the background
executor, against 0.53 and 0.64 s today: no longer.

## What the burst's long ticks are

The burst's main-thread cost is the apply of ready thumbnails, in the update
phase: the "Phase timing" line over the burst's 60 frames shows `update`
134 us/frame with 4 background threads and 368 us/frame with 24, against
about 5 us/frame at rest, while `upload` stays at its resting value. With 24
threads many shapes finish within one frame and that frame renders them all:
the one "Background apply took" warning of each wide run names
`kit_thumbnails` alone at 2.4 to 3.9 ms, the frame that is the burst's
longest tick. With 4 threads the apply stays under the 2.0 ms soft limit
(`threading.background_apply_soft_limit_ms`) in every frame.
