# Nsight Graphics GPU Trace and Graphics Capture control

Stability: experimental

The editor can start an NVIDIA Nsight Graphics GPU Trace or Graphics Capture
itself, so the result covers exactly the frames of a scene state a script
prepared over MCP. A trace started by a timer (`ngfx --start-after-ms`) or by
attaching later does not work for this: the editor's startup and scene load
take a varying time, and `ngfx --attach-pid` ignores `--start-after-ms` (the
report records "Start After Elapsed Time 0 ms").

## Build

The configure option `ERHE_NSIGHT_GRAPHICS_SDK_DIR` names the `include`
directory of the Nsight Graphics SDK that ships with Nsight Graphics
(`<nsight graphics install>/SDKs/NsightGraphicsSDK/<version>/include`). When
it is non-empty and the graphics API is Vulkan, the editor compiles
`src/editor/developer/nsight_graphics.cpp` against the SDK headers (defining
`ERHE_NSIGHT_GRAPHICS_SDK`). The SDK is header-only: at run time it finds the
library that ngfx injected into the process, so a build with the option runs
normally outside Nsight. Without the option, the `start_nsight_*` tools
report that the editor was built without the SDK.

## Run time

ngfx launches the editor with one activity injected, waiting for the
application:

    GPU Trace:        ngfx --activity "GPU Trace Profiler" --start-with-ngfx-sdk --stop-with-ngfx-sdk --auto-export ...
    Graphics Capture: ngfx --activity "Graphics Capture" --hotkey-capture ...
    (both:            --exe <editor> --dir <repo root> --args --no-scene --env "ERHE_AI_DRIVER=1;" --output-dir <dir>)

- `start_nsight_gpu_trace {frame_count, skip_frames}` and
  `start_nsight_graphics_capture {frame_count, skip_frames}` (MCP) initialize
  the SDK's activity on the first call
  (`NGFX_GPUTrace_InitializeActivity_Vulkan` /
  `NGFX_GraphicsCapture_InitializeActivity_Vulkan`; it fails with
  `InvalidState` when that activity is not injected; one process runs one
  activity) and queue the request in `Nsight_graphics`, owned by
  `App_rendering`.
- `App_rendering::process_start_capture()`, at the start of each render phase
  frame before any submit, calls `Nsight_graphics::on_frame_start()`. After
  `skip_frames` frame starts:
  - GPU Trace: `NGFX_GPUTrace_StartTrace_Vulkan`, and at the start of the last
    traced frame `NGFX_GPUTrace_StopTrace_Vulkan` with `NextFrameBoundary`, so
    the trace ends at that frame's present. Neither call needs a Vulkan
    handle.
  - Graphics Capture: `NGFX_GraphicsCapture_RequestCapture_Vulkan` with the
    present delimiter (at most 60 frames): the capture starts at the next
    present and ends by itself after `frame_count` presents.
- `get_nsight_status` reports the activity, the request's state (`idle`,
  `pending`, `tracing`, `stopped`, `capturing`, `failed`), the frames started,
  for a GPU Trace the SDK's own status (`NGFX_GPUTrace_GetStatus`: `active`,
  `tracing`, `draining`, ...), and for a Graphics Capture the completed
  capture files of the run and the newest one's path.

A GPU Trace is collected by ngfx, which saves the `.ngfx-gputrace` report into
the output directory, exports its tables (`BASE/*.xls`, tab-separated text;
the per-debug-label GPU times are `BASE/D3DPERF_EVENTS.xls`) and terminates
the editor. GPU Trace forces vsync off and locks the GPU clocks to base
clocks, so its times are longer than an unlocked run's. A Graphics Capture
(`.ngfx-capture`, the frame debugger's capture, about 2 GB for a Bistro frame)
is written by the injected library inside the editor process into Nsight's
documents folder (`GraphicsCaptures/` under `CorePlugin.DefaultDocumentsFolder`
of Nsight's settings), not into `--output-dir`.

While the Graphics Capture layer (`VK_LAYER_NV_ngfx_capture_<release>`) is
loaded, the Vulkan device does not enable `VK_EXT_present_timing`
(`Device_impl::Present_timing_use`): the layer does not support it and stops
the process at device creation with a blocking "incompatibility" dialog.
Frame pacing then resolves to tier S (plain FIFO).

## Script

`scripts/nsight_graphics.py --activity trace|capture` runs the whole sequence:
it launches the editor under ngfx, waits for the editor's ready line, opens
the scene and sets the camera like `scripts/bistro_frame_bench.py`, isolates
the scene's viewport, makes the `--mcp` setup calls (e.g.
`set_graphics_preset={"light_shading": "clustered"}`), waits for
`get_async_status` idle, starts the activity, and waits for the result: a
trace's state `stopped` and ngfx's exit (then prints the GPU time per debug
label), or a capture file reported by `get_nsight_status` (then prints its
path). Every wait is on a condition, so a slow configuration costs only its
frames. ngfx is found from `--ngfx`, `ERHE_NGFX` or `PATH`. The settings the
script does not set come from the working copy's `config/editor/` files.

The editor runs with optimized SPIR-V (`ERHE_SPIRV_OPTIMIZATION=1`,
doc/erhe/graphics.md "SPIR-V optimization"); `--spirv-optimization off`
traces the unoptimized shaders.

- `--default-scene` traces or captures the editor's startup scene instead:
  the editor starts without `--no-scene`, and no scene is opened or moved.
- `--editor-setting KEY.PATH=JSON` sets a `config/editor/editor_settings.json`
  value for the run (e.g. `'load.base_variant="on_demand"'`); the file is
  restored byte for byte at exit.
- `--ngfx-arg ARG` passes an extra activity option to ngfx (e.g.
  `--ngfx-arg=--time-every-action` for per-draw GPU times).

After a GPU Trace or a Graphics Capture is saved, ngfx terminates the editor
itself ("Terminating process..."); the script identifies the editor by its
executable (`/proc/<pid>/exe`), since ngfx's own command line names the editor
too.

## Reading a GPU Trace

- The automated analysis and the shader profiler need two activity options:
  `--ngfx-arg=--multi-pass-metrics` (counters collected over several frames;
  the "Trace Analysis" view's advice) and
  `--ngfx-arg=--real-time-shader-profiler` (the SM's sampling profiler; the
  "Top-Down Calls" and shader source views). The traced frames should render
  the same thing (fixed camera, no animation or physics). Shader source lines
  resolve because the SPIR-V carries non-semantic debug info, which the
  SPIR-V optimizer keeps.
- With the real-time shader profiler on, the top-level "PCIe Throughput"
  (about 11% on an RTX 5070 laptop) is the profiler's own sample stream:
  `pcie__write_bytes` (GPU to host) at a constant rate per second, whatever
  the frame length. The editor's own PCIe reads are about 0.1%.
- The top-level throughputs average over every traced frame, idle time
  included. Under the default frame pacing a Bistro trace of 3 frames shows
  the same ~7 ms of GPU work per frame but GPU frame times of ~8, ~55 and
  ~58 ms, so the summary reads ~6% SM. Compare the per-frame columns of
  `BASE/GPUTRACE_FRAME.xls` and the ranges of `BASE/D3DPERF_EVENTS.xls`
  (e.g. `Viewport window`), not the summary. The script sets editor settings
  only, not `erhe_graphics.json` (`frame_pacing_tier`, `force_disable_vsync`).

## Requirements

- The NVIDIA driver must allow GPU performance counters for the user (GPU
  Trace). On Linux `/proc/driver/nvidia/params` shows
  `RmProfilingAdminOnly: 0` once the module option
  `NVreg_RestrictProfilingToAdminUsers=0` is set (modprobe configuration,
  initramfs update, reboot); otherwise the trace fails with "GPU Performance
  Counters unavailable".
- Graphics Capture wants at least 20 GB free at its capture location; with
  less, the injected library stops the process at startup with a blocking
  warning dialog (`zenity` on Linux) until it is dismissed.
- `ngfx` is a Qt application: `--platform` is taken by Qt as its platform
  plugin name, so it is not passed.
