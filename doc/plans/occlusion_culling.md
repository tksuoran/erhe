# Raster occlusion culling

Status: in progress

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

## Prerequisites

Decision (user, 2026-10-23): this plan starts only after the Metal backend
has the GPU-read draw count (`doc/plans/metal_backend.md` section 6,
option A) and Metal 4 (section 7 there), so the occlusion pass is built
once for Vulkan, OpenGL 4.6 and Metal. Both are in place (2026-10-24):
`Indirect_count_draws` (`doc/erhe/graphics.md` "Indirect count draws")
gives Metal 3 and Metal 4 the GPU-read count through an indirect command
buffer, and the Metal 4 backend (`doc/erhe/metal4_backend.md`) orders its
work from the application's barrier declarations, so a new pass declares
its resources' usage_before / usage_after and memory barriers exactly as on
Vulkan (`doc/erhe/graphics.md` "Resource barriers").

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
  count for the shadow passes and the draw-list color passes (one job per
  view or color pass selection and list chunk, each job with its own
  filter), drawn with
  `multi_draw_indexed_primitives_indirect_count()` (Vulkan with
  `VK_KHR_draw_indirect_count`, GL 4.6) or, on Metal, prepared per pass by
  `Compute_command_encoder::prepare_indexed_indirect_count_draws()` and
  drawn with `multi_draw_indexed_primitives_indirect_count(set, job)`
  (`doc/erhe/draw_list_renderer.md`); the null device stays on the CPU
  path.
- The color passes draw GPU-written commands under `gpu` (culling.md phase
  6c): the prerequisite of step 7 of the sketch (draw the visible list
  through MDI) is met, without a CPU readback of the visibility bits.
- Devices without a GPU-read draw count (`Device_info::
  use_draw_indirect_count` false: MoltenVK, the emulated multi-draw path,
  the null device) have no GPU command generation, so occlusion culling is
  disabled there: the rule is the device capability, never the backend
  (`doc/erhe/draw_list_renderer.md` Q6).
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

Decisions (user, 2026-10-24):

- Views: the viewport scene views (including four view) and the headset's
  per-eye passes get occlusion; previews and thumbnails stay frustum-only.
- Lag: the one-frame lag is accepted; the "just became visible" second draw
  (step 5) is built later only if the pop-in is visible in use.
- Order: the color passes' GPU-written commands land first, as their own
  gated step, before any occlusion pass (done: culling.md phase 6c; the
  color passes share the frame's records like the shadow passes - no
  per-pass constant block, since no draw-list shader variant reads a
  record's color or size - and their commands come from the frame's job
  table, one more filter per job).

## Build order

1. 3a, the occlusion pass produces visibility bits. Done. Editor setting
   `occlusion_culling` (default off; Settings window, MCP
   `set_occlusion_culling`), recorded per published slot as
   `Occlusion_culling` and effective only with the `gpu` classification
   on a device with `Device_info::use_draw_indirect_count` (the classifier)
   and `Device_info::fragment_stores_and_atomics`
   (`Gpu_cull_classifier::is_occlusion_supported()`). The views are the
   viewport scene views' color views, four view included
   (`Composer::add_cull_views()` registers them with
   `Draw_list_renderer::add_color_occlusion_view()`); previews,
   thumbnails, scene image captures and the id pass get none, and neither
   does the headset: its per-eye passes draw unculled (`Cull_view::none`,
   culling.md "Open"), so they have no view to test. The classification
   dispatch writes the frame slot's start words (test bits: a bounded entry
   in the frustum whose grown box lies beyond the near plane; result bits
   preset visible for entries without bounds, boxes reaching the near
   plane and entries outside the frustum), and the view's "Occlusion test"
   composition pass, after the opaque and alpha-tested fills
   (`doc/editor/rendering.md` pass 16), draws 36 vertices per entry whose
   fragments (early fragment tests, no depth or color writes) OR the view's
   bit into the entry's result word (`doc/erhe/draw_list_renderer.md` Q6
   "Occlusion test"). Nothing consumes the bits: images are byte-identical
   with the test on and off (default, heavy, draws, heavy in four view,
   Bistro on Metal 4), and `get_cull_classification_check` stays at 0
   mismatches.
   Gate: `scripts/occlusion_wall_check.py` (a 30 x 12 m wall 10 m in front
   of the viewport camera, six boxes behind it, four in front, one box
   crossing the near plane): the six reported occluded, nothing else, the
   near-plane box marked without a test (`near_plane` 1,
   `near_plane_occluded` 0) on Metal 4, Metal 3 and KosmicKrisp; with the
   wall deleted 0 occluded. MCP `get_occlusion_check` per view, viewport
   camera of the startup commands: heavy 1003 tested, 31 occluded; heavy in
   four view 1029 / 33 (perspective), 1257 / 5, 496 and 399 (the three
   orthographic views); Bistro (opened, framed camera) 3852 tested, 2814
   occluded (73 percent).
   Cost before any saving (Metal 4, Apple M4 Pro, Release headless,
   pipelined, `force_disable_vsync`, Tracy GPU zones, 12 s per run, two
   runs each, off -> on): heavy GPU `Main` 2763 -> 2956 us, tick 3476 ->
   3673 us, R 1050 -> 1090 us, `classify_frame` 58 -> 64 us; Bistro GPU
   `Main` 3839 / 3856 -> 4335 / 4332 us, tick 4822 / 4849 -> 5322 / 5336 us,
   R 1332 / 1384 -> 1416 / 1406 us, `classify_frame` 265 / 281 -> 294 / 296
   us (the CPU decode of the retired frame's words). The pass runs inside
   the view's render pass, so its own GPU time is not separable from
   `Main` on this GPU (timestamps at stage boundaries only); the difference
   is the pass. A first version that issued the atomic OR for every
   fragment cost heavy 1390 us (the atomics of one large visible box on one
   word serialize); the fragment shader now reads the word first.
2. 3b, the classification consumes the bits: the masks of frame N + 1 AND
   the result bits of frame N (one AND in `cull_classify.comp`, with the
   barrier fragment write in frame N -> compute read in frame N + 1,
   `earlier_submissions`). Next. The view indices of two frames need not
   match (a view's index is its position in its frame's set), so the
   consumer maps frame N's occlusion views to frame N + 1's by their key.
   Open for it: back-face culling or a front-face-only box (three faces
   from the eye's octant) to halve the pass's fragments.
3. Step 5 of the sketch (the second draw of the entries that just became
   visible), only if the one-frame pop-in shows in use.
