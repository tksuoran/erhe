# Raster occlusion culling

Status: proposed

This plan extends `doc/editor/rendering.md` (the composer passes and the
forward renderer) with depth-buffer-based occlusion culling.

Sketch:

1. Render the list of visible objects.
2. Rasterize the bounding volumes of all objects against the depth buffer from
   step 1.
3. Declare `layout(early_fragment_tests) in;` in the fragment shader so early-z
   happens; the scheme depends on it.
4. In the fragment shader, write a bit to an SSBO to mark the current object
   visible, so one visible fragment of a bounding volume marks the whole
   object. Objects that were not visible and just turned visible get a second
   bit saying they should be rendered this frame.
5. Render the objects that just became visible, to avoid one frame of occlusion
   lag. Optional where minor pop-in is acceptable.
6. Repeat for frame N+1, which consumes the visibility this frame generated.
7. Read the list of visible objects with MDI and generate the draws from it.
   This step is what makes the scheme worthwhile; without it the result is a
   worse form of hardware occlusion queries.

## Starting point

This plan is phase 7 of [culling.md](culling.md); phases 1 to 6 are done and
are described there. What the occlusion pass builds on:

- Classification: `Draw_list_scene` keeps SoA bounds per entry and one mask
  bit per cull view (`uint64_t` per 64-view group); the views of a frame are
  added in the rendergraph pre-execute hook (`Composer::add_cull_views()`
  for color views, `Shadow_renderer::fit()` for shadow views) and classified
  once per scene by `Cull_frame_registry::classify_frame()`
  (`src/erhe/scene_renderer/erhe_scene_renderer/cull_*`,
  `draw_list_scene*.cpp`). Occlusion enters as one more test into the color
  views' masks (culling.md phase 7).
- GPU path (editor setting `culling_classification` = `gpu`):
  `Gpu_cull_classifier` and `res/shaders/cull_classify.comp` write the masks;
  `res/shaders/cull_compact.comp` turns them into indirect commands plus a
  count for the shadow passes, drawn with
  `multi_draw_indexed_primitives_indirect_count()`. It needs
  `Device_info::use_draw_indirect_count` (Vulkan with
  `VK_KHR_draw_indirect_count`, GL 4.6); Metal and the null device stay on
  the CPU path. Check what the session's device reports before choosing
  where to develop.
- The color passes still draw CPU-written commands under `gpu`: their
  primitive records are patched per pass, so a GPU-written command cannot
  name a shared record (culling.md "Open", the color passes item). Step 7
  of the sketch (draw the visible list through MDI) needs that solved
  first, or a CPU readback of the visibility bits, which adds frames of lag.
- Gates and tools: MCP `get_cull_classification_check` (GPU vs CPU masks
  and commands), `render_scene_image` byte comparisons between settings,
  per-pass drawn/culled counts in `Shadow_draw_statistics` and
  `Composition_pass_stats`, `scripts/render_thread_bench.py --tracy` with
  the heavy and draws scenes (culling.md "Measurement"). GPU time per pass
  comes from `Gpu_timer` results and Tracy GPU zones (`Scoped_gpu_zone`;
  on Metal at encoder granularity).
- Render thread rules apply (`doc/editor/render_thread.md`): the hook runs
  in R before the first pass; visibility bits produced in frame N and read
  in N+1 are per frame slot state, written in R and read in R.

Decisions for the user before building: which views get occlusion (main
viewport color views only, or also previews and four-view), whether the
one-frame lag is accepted or the "just became visible" second draw (step
5) is built, and whether the color passes' GPU commands (the culling.md
open item) are done first as their own step.
