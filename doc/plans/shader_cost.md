# Shader cost: fragment invocations per pipeline and per-pixel shader cost

Status: in progress

This plan adds two measurements on top of the shader variant browser and its
offline compiler analytics (`doc/erhe/graphics.md` "Shader variant browser",
"Mali analytics"):

- **A. Fragment invocations per pipeline.** For one rendered frame, the
  number of fragment shader invocations of every render pipeline, mapped to
  the shader program (name and defines) it runs; an MCP tool returns them and
  writes a sidecar the browser shows as a "Fragments" column, with the
  per-variant estimate of fragments times static cost (Mali cycles).
- **B. Per-pixel shader cost.** The editor loads the analytics of a browser
  directory and the viewport shows, per pixel, the sum over the fragments
  shaded there of their variant's static cost (a Mali G310
  metric) as a heat map: overdraw weighted by shader cost.

The motivating question is which shader variants spend a frame's fragment
work on the Quest 3 (`doc/plans/quest_performance.md`): A ranks variants by
fragments times cost, B shows where on screen that cost lands.

## Design

### A1. Capture with pipeline statistics queries

A frame's fragment invocations are counted with Vulkan pipeline statistics
queries (`VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT`), one
query per pipeline bind range inside a render pass: the bind opens a query,
the next bind or the end of the render pass closes it. Hardware counters, not
counting shader variants, because:

- the frame runs its real pipelines: no variant is compiled, no early depth
  test is changed, and every pipeline counts, not only the `standard`
  content draws (the grid, sky, tools, ImGui and post-processing too);
- the count is what the GPU ran, early depth rejection included.

What a count is follows the implementation's counter: the Vulkan spec lets
an implementation count helper invocations and leaves counts at
implementation-defined granularity, so the counts are exact for ranking and
approximate as absolute numbers (verified against the overdraw counts, A4).

`Device_info::pipeline_statistics_query` is set when the device has
`pipelineStatisticsQuery` and `hostQueryReset` (the pool is reset on the
host when a capture is requested); the feature is enabled at device
creation. OpenGL (`ARB_pipeline_statistics_query`), Metal (no draw-boundary
statistics sampling on Apple GPUs) and null leave it false: a request
returns unsupported. Quest 3 support is to be read from the device log.

API (`erhe::graphics::Device`; backend-neutral types in
`fragment_invocation_capture.hpp`):

- `request_fragment_invocation_capture()`: arms the next frame whose
  command buffers begin after the call; false when unsupported or a capture
  is in flight.
- `take_fragment_invocation_capture()`: the completed capture once that
  frame retires, else nullopt. A capture holds the frame id and, per
  pipeline bind range, the pipeline's debug label, the program name and
  defines (copied at bind time: pipelines may be destroyed before
  readback), the view count, and the invocations; aggregates per program
  are the caller's.

Vulkan: one query pool of 16384 queries; a range in a multiview render pass
takes one query per view (the spec's consecutive indices) and sums them; a
range past the capacity is not recorded and counted as dropped. The open
range lives on the command buffer (`Command_buffer_impl`): the render
command encoder's `set_render_pipeline()` / `set_render_pipeline_state()`
close and open it, `Render_pass_impl::end_render_pass()` closes it. The
results are read in `acquire_frame_slot()` after the captured frame's slot
wait, as the GPU timers are.

`Shader_stages` keeps the variant defines of the create info it was built
from (`Shader_stages::get_variant_defines()`: the defines and those the
vertex format contributes, `ERHE_ATTRIBUTE_<name>` and the vertex
encodings), so a bind knows the program's identity; the shader variant
browser records the same list, so one shader key drawn with two vertex
formats is two programs on both sides.

### A2. MCP tool and browser sidecar

`capture_fragment_invocations` (deferred over frames, as
`get_occlusion_check`): requests a capture, waits for it, and returns the
ranges aggregated per program (name, defines, invocations, pipelines, share
of the frame) and per pipeline. With `browser_directory` (default: the
session's `--shader-variant-browser` directory, if any) it also writes
`<browser>/fragments.js` (`window.erhe_shader_fragments`, schema
`erhe_shader_fragments` 1): per program key (the name and the defines as the
manifest writes them), the invocations.

### A3. Browser columns

`index.html` loads `fragments.js` when present and adds a "Fragments"
column (and its share of the frame); with `analytics.js` present too it adds
per compiler the estimate of the frame's work of the variant - fragments
times the fragment (`Main`) static cost: Mali `Main` cycles - and that estimate's share, the column to sort by. The
program page shows the program's fragments. Programs are matched by key,
not by id, so a capture of one session matches a browser recorded by
another session with the same variants.

### B1. Analytics in the editor

`Shader_analytics` (editor; `src/editor/renderers/shader_analytics.*`)
loads `variants/manifest.json` and `analytics.js` of a browser directory and
indexes, per program key, per core, per stage, the "after" summaries (the
module the device runs when SPIR-V optimization is on; "before" when the
run had it off - the manifest's `tools.spirv_optimization`). Loaded by the
MCP tool `load_shader_analytics` and the editor option
`--shader-analytics <directory>`, and filled by the editor's background
analysis of every program it builds (doc/editor/shader_analytics.md; desktop
default, `--no-shader-analytics` turns it off, never on Android).

The metrics a cost view offers (`Shader_cost_metric`): Mali G310 fragment
`Main` total cycles (the bound pipeline's), arithmetic cycles, texture
cycles.

### B2. The cost overdraw mode

A third counting mode, `Overdraw_count::cost` (`ERHE_OVERDRAW_COST`), counts
the shaded invocations as `shaded` does but adds the variant's weight
instead of 1. The weight is a define of the counting variant,
`ERHE_OVERDRAW_WEIGHT`, carried by a new `Shader_int` axis `OVERDRAW_WEIGHT`
so variants of different weights are different keys; the axis is emitted
only in the cost mode (other keys keep their defines). The weight of a key
is the metric of its `OVERDRAW` off twin, looked up by
`Shader_variant_cache` through a weight provider the editor installs
(`set_overdraw_weight_provider()`); a twin the analytics lack weighs 0 and
is counted in the view's "variants without analytics" (MCP and the legend).
Choosing another metric bumps the provider's serial, which drops the
draw-list resolutions and bucket keys of the cost mode.

The heat map of the cost mode is linear from 0 to the view's max value
(default: four times the largest weight of the metric), the legend names the
metric. The reduction's total is a 32-bit sum, which the weights overflow:
the cost mode reports max and covered pixels and leaves total and the
histogram out. The frame's total cost is A's estimate.

UI: the viewport's shader debug combo gets "Shader cost: <metric>" entries,
enabled when analytics are loaded and overdraw is supported. MCP:
`set_overdraw_visualization` takes mode `cost` with a `metric`.

## Remaining

1. Bistro: capture and cost view of the Bistro bench camera (desktop), the
   ranking of the Fragments column against the Quest Bistro eye pass
   numbers of `doc/plans/quest_performance.md`.
2. Quest 3: whether the Adreno driver exposes `pipelineStatisticsQuery`
   (the startup log names it); the cost mode in the headset view (built
   with the shared ramp, not run in a headset).
3. The browser gap of "Open" below.

## Built

- A1 and A2. On Intel (headless Release, default scene, 2304x1120 at 4x
  MSAA): 13 programs, 28 ranges; `standard` 932980 invocations against the
  overdraw shaded total of 918541 (+1.5 %: helper invocations, which the
  overdraw counter does not count); the `standard` counts are identical with
  the overdraw counting variants and with the real ones.
- A3. The same session recorded with the browser: 12 of the 13 captured
  programs are recorded (99.9 % of the frame's invocations); the post-processing
  `downsample` program is not recorded by the browser (open below).
- B1 and B2. The twin identity is built as `Shader_variant_cache::get()`
  and `Program_interface::make_prototype()` build a create info (the
  program defines of `Program_interface::add_program_defines()` and the
  vertex format's defines). Default scene, analytics of a recorded browser:
  the two `standard` cost variants compiled with `ERHE_OVERDRAW_WEIGHT`
  from their twins' Mali G310 cycles 2150 and 2480 (21.5 and 24.8 cycles); no variant missing;
  cost and shaded modes cover the same 781439 pixels; `scripts/overdraw_verify.py`
  passes unchanged.

## Verification

- A: the GPU test's exact areas; the default scene's `standard` invocations
  against the overdraw shaded total of the same frame setup.
- B: rectangles of known variants and weights read back through
  `get_overdraw_stats` max and pixel probes.

## Open

- The shader variant browser does not record the post-processing
  `downsample` program (`post_processing.cpp`), with an empty SPIR-V cache
  too: find which compile path skips `Shader_variant_report::add()`.

- Quest 3: does the Adreno driver expose `pipelineStatisticsQuery`?
