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
   thumbnails, scene image captures and the id pass get none; the
   headset's multiview pass gets one ("Headset" below). The classification
   dispatch writes the frame slot's start words (test bits: a bounded entry
   in the frustum whose grown box lies beyond the near plane; result bits
   preset visible for entries without bounds, boxes reaching the near
   plane - a camera inside the box among them - and entries outside the
   frustum), and the view's "Occlusion test" composition pass, after the
   opaque and alpha-tested fills (`doc/editor/rendering.md` pass 16),
   draws per entry the three faces of its box toward the camera (a +X +Y
   +Z template mirrored per axis, 7 corners and 18 indices from a static
   uint32 index buffer, no face culling) whose fragments (early fragment
   tests, no depth or color writes) OR the view's bit into the entry's
   result word (`doc/erhe/draw_list_renderer.md` Q6 "Occlusion test").
   Gate: `scripts/occlusion_wall_check.py` (a 30 x 12 m wall 10 m in front
   of the viewport camera, six boxes behind it, four in front, one box
   crossing the near plane; then the camera inside a 6 m box with a box
   beyond its far wall): the six reported occluded, nothing else, the
   near-plane box marked without a test, the box around the camera and the
   box beyond it not occluded (the room's walls are back faces from inside
   and write no depth), on Metal 4, Metal 3 and KosmicKrisp. MCP
   `get_occlusion_check` per view, viewport camera of the startup
   commands: heavy 1003 tested, 31 occluded; heavy in four view 1029 / 33
   (perspective), 1257 / 5, 496 and 399 (the three orthographic views);
   Bistro (opened, framed camera) 3852 tested, 2814 occluded (73 percent).
   The pass's cost (Metal 4, Apple M4 Pro, Release headless, pipelined,
   `force_disable_vsync`, Tracy GPU zones, 12 s per run, two runs each,
   GPU `Main` with the pass but without consumption against occlusion off;
   the pass runs inside the view's render pass, and this GPU times only
   stage boundaries, so the difference is the pass): first version (36
   non-indexed vertices per entry, both box faces of all six sides) heavy
   +193 us, Bistro framed camera +480 us; now (three faces toward the
   camera, indexed: 7 vertex invocations instead of 36 and 6 triangles
   instead of 12 per entry) heavy 2764 -> 2871 us (+107), Bistro framed
   3844 -> 4100 us (+256), Bistro bench camera (eye (-29.28, 9.68,
   -16.34), target (0, 2, 0)) 3384 -> 4393 us (+1009: large boxes close to
   that camera; fill, not vertices). An earlier version issued the atomic
   OR for every fragment and cost heavy 1390 us (the atomics of one large
   visible box on one word serialize); the fragment shader reads the word
   first.
2. 3b, the classification consumes the bits. Done. The classification of
   frame N + 1 clears an occlusion view's mask bit of an entry that the same
   view (matched by key: the scene view and pass, `Gpu_command_key`) tested
   in frame N without a visible fragment; frame N must be the frame right
   before, with the same entry layout (`entry_layout_serial`: no entry
   added, removed or moved), else nothing is culled that frame (also the
   frame the setting turns on, a new view). The culled entries are tested
   again, so an entry that becomes visible is drawn one frame later: a fast
   camera move or an object moving out from behind an occluder can pop in
   for one frame (accepted, decision above). Gates: images byte-identical
   with occlusion on and off after settling (default, heavy, draws, heavy in
   four view, Bistro, the wall and inside-the-box scene; Metal 4, Metal 3,
   KosmicKrisp); `get_cull_classification_check` 0 mismatches with the
   culled bits added back to the GPU masks and dropped from the CPU
   writer's commands (`occlusion_culled_count`); the wall scene culls the 6
   boxes behind the wall; a camera teleport behind the wall captured
   frame-exact by a check request: the first frame culls the 6 boxes the
   old position found occluded (the lag frame), the next culls only what
   the new position hides; adding or deleting a box: that frame culls 0.
   Culled per view and the color passes' drawn counts, occlusion off -> on:
   heavy 31 culled (opaque fill drawn 1003 -> 972), heavy in four view 33 /
   5 / 496 / 399, Bistro framed 2814 (opaque 3724 -> 946, alpha tested 123
   -> 89, translucent 5 -> 3), Bistro bench camera 2710 of 3086 tested.
   Release headless Metal 4 (as above, two runs each, off -> on):

   | Scene | GPU `Main` (us) | tick (us) | R (us) |
   |---|---|---|---|
   | heavy | 2764 / 2764 -> 2861 / 2861 | 3481 / 3482 -> 3580 / 3579 | 1127 / 1125 -> 1143 / 1142 |
   | Bistro framed | 3850 / 3838 -> 3152 / 3147 | 4845 / 4825 -> 4132 / 4125 | 1376 / 1371 -> 1328 / 1252 |
   | Bistro bench camera | 3386 / 3382 -> 2510 / 2677 | 4392 / 4398 -> 3516 / 3681 | 1398 / 1383 -> 1247 / 1243 |

   The saving exceeds the pass's cost on Bistro (net GPU -700 us framed,
   -790 us bench camera; the consumption alone saves about 950 and 1800
   us) and not on heavy (31 of 1003 entries occluded: net +97 us).
3. Step 5 of the sketch (the second draw of the entries that just became
   visible), only if the one-frame pop-in shows in use.

### Headset

The headset's multiview color pass (both eyes in one render pass) is an
occlusion view. Done.

- The headset renders its passes where it prepares them
  (`Composition_pass::render_immediate()`), so its rendergraph node's
  pre-execute hook (`Headset_view::add_render_cull_view()`, before the
  frame's classification) registers per pass what `Composer::add_cull_views()`
  registers for a viewport: `Composer::add_immediate_cull_views()` runs each
  pass's gating (`Composition_pass::prepare()` with
  `Composition_prepare_scope::gating_only`), gives each draw-list pass of the
  headset's scene the union eye frustum view and a GPU command selection,
  and makes that view an occlusion view for the "Occlusion test" pass. The
  multiview render passes them in `Render_context::immediate_pass_views`.
  The per-eye (non-multiview) path registers none.
- Both eyes share the union view and so one bit. The occlusion draw
  (`Draw_occlusion_parameters`: per view `clip_from_world` and eye,
  `view_count`) uses the multiview variant of `occlusion_test.vert`
  (`ERHE_MULTIVIEW`, built when the device has multiview): each eye
  rasterizes the faces of each box toward it with its own matrix, and a
  fragment in either eye marks the entry visible.
- Near plane: an entry is tested only when its grown box lies wholly beyond
  a head-space plane `-z = d`, d = the positional margin of the union view
  (0.1 m, covers the eye offset from the head origin) + the farthest corner
  of the eyes' near planes (z_near times the padded union sides) + 0.02 m
  (`Headset_view::prepare_cull_planes()`, M2). Inside an eye's frustum every
  point before that eye's near plane lies before this plane, canted eyes
  included; a box the eye is inside reaches it.
- A frame's words are consumed only when its occlusion pass drew them
  (`Occlusion_view_record::drawn`): an XR frame that registered the view but
  did not render (the multiview callback skipped) culls nothing next frame.
- Under OpenXR the frame command buffer is submitted inside the headset node
  (`Xr_session` submits it before the views' command buffer begins), so the
  host-read barrier of the frame's occlusion words
  (`Cull_frame_registry::record_frame_end()`) is recorded at the end of the
  multiview callback, into the views' command buffer, the frame's last; the
  editor records it only into a command buffer still recording.

Gate: `scripts/quest_occlusion_wall_check.py` (Quest over adb, off-head,
head pose from a benchmark file): the wall case occludes and culls exactly
the 6 boxes behind the wall, the box crossing the eyes' near region stays
untested; the inside case keeps the room box and the box beyond its far wall
visible. `get_cull_classification_check` 0 command mismatches on Bistro.

Result (2026-10-26, Quest 3 release APK, Bistro `bistro_quest.glb`,
`config/editor/benchmark_bistro.json` pose = the desktop bench camera,
clustered light shading, gpu classification and light binning, single runs):
frame 122.8 -> 70.8 ms (8.1 -> 14.1 fps), XR multiview GPU pass
121.9 -> 69.7 ms; opaque fill drawn 3609 -> 484 of 3724, 3192 of 3730
bounded in-frustum entries culled as occluded. Repeat-3 medians not taken.

## Open

- The pass's fill cost grows with the screen area of the tested boxes
  (Bistro bench camera +1 ms). Candidates: test only entries above a
  screen-size threshold (small entries are cheap to draw anyway) and a
  hierarchical-Z compute test instead of rasterized boxes.
- A scene with little occlusion (heavy) pays the pass without a saving;
  the setting stays off by default until measured on more scenes.
- Headset: the per-eye (non-multiview) path has no occlusion view; the
  headset's images with occlusion on and off are not compared (the Quest
  compositor screenshot shows the passthrough camera, not the app).
