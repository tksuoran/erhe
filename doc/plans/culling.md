# Culling: one classification for every pass, then on the GPU

Status: in progress

Extends [../erhe/draw_list_renderer.md](../erhe/draw_list_renderer.md) (Q6, the
per-pass entry cull), [../erhe/shadows.md](../erhe/shadows.md) and
[../erhe/point_light_shadows.md](../erhe/point_light_shadows.md) (the passes
that cull), and takes over the "Point-light cube shadow budget" items of
[shadows.md](shadows.md). [occlusion_culling.md](occlusion_culling.md) is the
occlusion half of the same subject and becomes a phase of this plan.

Reference design: the Esoterica engine (tksuoran/Esoterica,
`Docs/Rendering/Material Rendering Pipeline.md` and `Point Light Shadows.md`).

## What exists

Every pass that draws the draw lists runs `Draw_list_scene::draw_list_chunks`
over the lists published for the frame; per entry it applies the pass's
`Item_filter`, tests `Draw_list_entry::world_aabb` against the pass's planes
with `erhe::math::aabb_in_convex_volume` (center + extents against each plane,
scalar, early out on the first rejecting plane), writes one pass byte, and then
writes the primitive records (a memcpy per passing entry from the list's cached
`primitive_records`) and one indirect command per passing entry into ring
buffers. Skinned lists are never culled.

The heavy bench scene has 1257 entries and 18 shadow passes (4 directional, 2
spot, 12 point cube faces) plus the color passes. Per frame the shadow passes
alone visit 18 x 1257 entries and run up to 100 plane tests per entry; on the
Intel iGPU that is 18 x 22 us (Release) and 18 x 365 us (Debug) in
`draw_shadow`, roughly half of it the cull and half the record and command
writes. The data is AoS: the AABB sits in the 64-byte entry among the draw
parameters.

The graphics layer has `Compute_pipeline` / `Compute_command_encoder` on
Vulkan, multi-draw indirect either native (`multiDrawIndirect` +
`shaderDrawParameters`, `ERHE_DRAW_ID` = `gl_DrawID`) or emulated with one draw
and a push constant per command (MoltenVK, devices without the features),
`drawIndirectFirstInstance` recorded as `Device_info::use_base_instance`, and no
`drawIndirectCount` query yet.

## The reference

Esoterica runs no per-instance CPU work. Each render view (the camera, every
shadow cascade, every environment map face) is one bit of a 64-bit view mask.
A compute pass tests every instance against every view at once and writes the
mask; a compaction pass expands visible instances into cluster work entries; a
cluster pass refines the mask per cluster; a prefix sum and a scatter produce
one dense indirect draw buffer with a counter per (view, bucket), and each pass
executes its buffer with its counter. Point lights use a bounding sphere for
culling and render all directions in one pass into an octahedral map.

What transfers to erhe: the view set built once per frame, the per-entry mask
over all views in one classification, dense per-view command buffers produced
from the mask, and the sphere test for point lights. What does not transfer
as-is: clusters and mesh shaders (erhe draws whole primitives through vertex
shaders), and the 64-bit mask width (erhe's pass count per frame is in the
tens, but the color passes of several viewports and the id pass count too).

## Design

- **View set.** Once per frame, after the light fits, the renderer builds the
  list of culling views: for each pass its planes (4 side planes for a
  depth-clamped directional pass, 6 otherwise), or for a point light its
  range sphere plus the 6 face octants, with the pass's layer selection and
  `Item_filter`. Each view is a bit index. The color passes of the viewports
  join the same set.
- **SoA bounds per draw list.** `Draw_list` carries, parallel to `entries`,
  the center and the half extents of every entry as six float arrays. The
  registration, transform and bounds hooks that write `entry.world_aabb`
  write them too, and `publish()` copies them to the frame slot with the
  entries, so the render phase reads a frame-stable copy and the arrays cost
  nothing on a frame where no list changed.
- **One classification.** A pass over the view set outside and the entries
  inside: `d = n . c + w + |n| . e`, clear the view bit where `d < 0`. The
  inner loop is lane-wise over the SoA and vectorizes without intrinsics (the
  shape `erhe::math::box_hull.cpp` uses for `find_extreme_box`), on x86 and on
  NEON. Output: one `uint64_t` mask per entry, per list, for the frame.
- **Mask-driven passes.** `draw_list_chunks` takes the view bit instead of the
  planes: the pass byte is `(mask >> view) & 1`, the chunk, record and command
  writes stay as they are. The scalar per-pass cull is gone.
- **GPU.** The same mask, computed by a compute shader from the uploaded SoA
  bounds and view set, followed by command generation on the GPU.
- **Two complete paths.** Phases 1 to 4 are the CPU path, whole on their own:
  every pass culled by the one classification, commands written by the CPU.
  The GPU path is a device capability (native multi-draw indirect with a
  count) and a setting; devices on the emulated multi-draw path and the null
  device run the CPU path, and both paths stay built, gated by the same
  statistics and image checks. Phase 4's record indexing serves both, so the
  choice changes where the mask and the commands are produced, not the
  shader interface.

Correctness bar: every entry that intersects a view's volume is drawn in that
view. There is no reference cull to match bit for bit; a conservative keep is
fine, a wrong reject is a defect. The gate is an image comparison per pass set
and the per-pass `Draw_statistics` counts.

## Phases

Each phase lands on its own, verified, before the next starts.

1. **SoA bounds and the mask plumbing, scalar.** Add the SoA arrays to
   `Draw_list` and their maintenance and publish; add the view set and the
   per-entry mask; switch `draw_list_chunks` to the mask. The classification
   loop is a scalar transliteration of `aabb_in_convex_volume`, so the pass
   bytes, counts and images are identical to today's. Measures the plumbing
   alone.
   Done: `Draw_list_bounds`, `Draw_list_scene::add_cull_view()` /
   `Cull_view`, the masks as render-phase state of the `Draw_list_scene`
   (one `uint64_t` per entry per group of 64 views, so a frame past 64 views
   needs no fallback). Views with bitwise equal planes and the same purpose
   are shared. `Shadow_renderer` adds all of its passes before its first
   draw, so its passes classify together; a color view is added at its
   pass's draw and classifies on its own (a pre-draw hook in the render
   phase would let the color views join the shadow batch). Images and
   per-pass culled counts are byte- and count-identical to the per-pass
   cull on the default, heavy and draws bench scenes, in both render thread
   modes. The plumbing cost was not measured.
2. **Vectorized classification.** Reorder the loop views-outside,
   entries-inside over the SoA; verify the compiler vectorizes it (GCC, clang,
   NDK). Add the point light range sphere as the first test of its 6 faces.
   Expected: the cull share of `draw_shadow` (about 150 to 250 us per frame
   Release on the heavy scene) down to tens of microseconds for all passes.
   Done: `classify_cull_views()` runs per view of the batch one
   branch-free loop per test over the SoA bounds, narrowing a per-entry
   `uint32_t` inside flag (persistent scratch of the `Draw_list_scene`), and
   a last loop ORs the view's bit into the masks; entries without bounds
   are ORed back in, skinned lists set the bit of every view. The plane
   loop keeps the expressions and evaluation order of
   `aabb_in_convex_volume()`, so its decisions are the phase 1 decisions.
   A point light's six faces carry its range sphere
   (`add_point_light_cull_views()`, `Cull_view_kind::cube_face`); the
   sphere test (squared box distance against the squared range, one loop
   per axis) runs once per light per list and seeds the faces' inside
   flags. It rejects boxes in the cube corners beyond the range that the
   face frusta (far plane at the range along the face axis) keep: on the
   heavy scene the two shadowed point lights draw 654 and 843 entries over
   their six faces instead of 961 and 938; every other pass count and
   the images (default, heavy, draws scenes; both render thread modes) are
   identical to phase 1, since a caster beyond the range only shadows
   receivers where the range attenuation is zero. Vectorization, verified
   from the compiler reports: clang-cl (`-Rpass=loop-vectorize`, AVX2)
   and MSVC (`/Qvec-report:2`, SSE2) vectorize all six loops (unbounded
   marks, sphere axis, sphere compare, plane, bit write, skinned bit set).
   MSVC refuses a flag written as a cast of a negated comparison and the
   three sphere axes in one loop, hence the `? 0u : 1u` flags and the
   per-axis loops. GCC and the NDK were not checked. Measured on the heavy
   scene, Release MSVC, Intel iGPU, pipelined, 900 frames, two runs each:
   the shadow batch's classification (`classify_cull_views`, median per
   call) 94 / 92 us -> 41 / 41 us, the whole classification per frame
   117 / 118 us -> 57 / 55 us, `draw_shadow` per frame (18 passes,
   classification included) 684 / 635 us -> 628 / 567 us; the record and
   command writes are the rest of `draw_shadow` (phase 4).
3. **Point cube faces by octant.** A face's four side planes through the light
   are fixed in light space, so a box's face set is a sign and magnitude test
   on its min and max relative to the light; faces and whole cubes with no
   survivors begin no draws. Takes over the "skip empty faces and
   lights" and "range cull" items of [shadows.md](shadows.md).
   Done: `Draw_list_scene::add_point_light_cull_views()` adds the six faces
   of a light as consecutive `Cull_view_kind::cube_face` views (the plane
   views and the `add_cull_view(..., Cull_sphere)` overload of phase 2 are
   gone). Per list the light's sphere test runs once and keeps the per-axis
   gaps `d_k = max(|o_k| - e_k, 0)` (`o = c - light`); each face is one
   lane loop, face +X keeping `o_x + e_x >= max(d_y, d_z)` and -X `e_x -
   o_x >= max(d_y, d_z)`, the other axes alike. The test is exact for the
   octant, so a box it rejects covers no texel of the face; the near plane
   is not tested. The lanes live in `cull_lanes.hpp/.cpp`;
   `test_cull_lanes.cpp` checks on 12000 seeded random boxes around three
   lights (seam-straddling and light-containing boxes included) that every
   box the phase 2 face planes (near plane excluded) keep, the octant keeps,
   and that the sphere test matches the brute-force box-sphere distance,
   plus hand-written cases per face. The bit write also sums each view's
   survivors (entries with the bit set) and `has_cull_view_survivors()`
   reads the sum. An empty face's
   render pass still begins and ends: the face must hold the clear value
   for the forward pass, and the pass's load action is the clear (the
   graphics layer's `Command_buffer::clear_texture()` clears a whole
   texture or view, and on Metal it stages a buffer per call, so a per-face
   clear would need six texture views per cube and a new backend
   operation). Everything else of the face is skipped: buffer, pipeline and
   material binds, camera and control blocks, the record and command
   writes. So "begin no render pass" is "begin no draws": the cube face
   passes begun per frame are unchanged. `Shadow_draw_statistics` keeps
   per face drawn / culled counts (a skipped face reports as culled the
   entries its filter passes, counted once per render) and the skipped
   faces per cube, which MCP `get_scene_lights` reports as
   `shadow_cube_faces` and `shadow_cube_skipped`. Images byte-identical to
   phase 2 (default, heavy, draws scenes; heavy with a point light moved
   out of every caster's reach; both render thread modes), per face counts
   identical to phase 2's planes on the heavy scene (the near plane never
   decided there); the +Y face of both shadowed point lights and the +Z
   face of one are skipped (the moved light: all six). Vectorization from
   the compiler reports: clang-cl (AVX2) and MSVC (SSE2) vectorize the new
   face loop and the fused bit write and survivor count, and all phase 2
   loops still. Heavy
   scene, Release MSVC, Intel iGPU, pipelined, 900 frames, two runs each:
   the shadow batch's classification (median per frame) 46 / 48 us -> 27 /
   27 us, the whole classification per frame 53 / 57 us -> 35 / 35 us,
   `draw_shadow` per frame 560 / 560 us -> 532 / 526 us at 18 -> 15 calls
   per frame.
4. **Shared records, per-pass commands.** The shadow passes' primitive records
   do not depend on the light: upload one record block per list per frame
   and let each pass's commands name their record through the command's
   first-instance field (`gl_BaseInstance`, `Device_info::use_base_instance`)
   instead of `ERHE_DRAW_ID`'s position in the command list. Removes the
   per-pass record memcpy (half of what phase 2 leaves). Needs the shader
   interface change in `erhe_primitive.glsl` and the emulated path to keep
   working through its push constant.
   Done: the shaders index `primitive.primitives[]` with
   `ERHE_PRIMITIVE_INDEX`, which the GLSL preamble defines from
   `Device_info::record_indexing` (`doc/erhe/graphics.md` "Draw records"):
   `Record_indexing::first_instance` reads the instance index that carries
   the command's `first_instance` (Vulkan `gl_InstanceIndex` on the native
   and the push-constant emulated multi-draw alike, the emulation still
   issuing one indirect draw per command; GL `gl_BaseInstance(ARB) +
   gl_InstanceID`), `draw_position` is `ERHE_DRAW_ID`. Vulkan takes
   `first_instance` with `drawIndirectFirstInstance`, GL with base instance
   and GLSL 4.60 / `ARB_shader_draw_parameters`; Metal always takes
   `first_instance` (`[[instance_id]]` includes the base instance); the null
   device and every device without the capabilities take `draw_position`;
   the device logs the choice. Every `Draw_indirect_buffer` writer sets a
   command's `first_instance` to its position (0 under `draw_position`), so
   the color, id and bucket passes draw exactly as before. Under
   `first_instance` the shadow passes share their records
   (`doc/erhe/draw_list_renderer.md` "Record upload"): the first
   `draw_shadow()` that draws a chunk of a shadow list uploads the records
   of every entry of the chunk (`Primitive_buffer::update_records()`, one
   block copy plus the default color / size patch), the later passes bind
   the same range and write only their commands
   (`Draw_record_layout::chunk_entries`, `first_instance = entry - chunk
   begin`), and the `Draw_list_scene::Shadow_record_scope` that
   `Shadow_renderer::render()` holds across its passes releases the ranges
   when it ends, in the same frame. (The color passes share their records
   since the color passes item of "Open", commit 1, and the scope became
   the frame-wide release of `Cull_frame_registry::release_frame_records()`.)
   `draw_position` devices keep the per-pass
   shadow records, the phase 3 path. `erhe_graphics_gpu_tests`
   `Draw_indirect_test.draw_indexed_indirect_first_instance` checks that a
   command's `first_instance` reaches `ERHE_PRIMITIVE_INDEX`. Images
   byte-identical and per-pass / per-light counts identical to phase 3
   (default, heavy, draws scenes; heavy in both render thread modes); a
   deliberately wrong record index changes the heavy image, so the gate
   sees the shared path; heavy with Vulkan validation: no errors. Release
   MSVC, Intel iGPU, pipelined, 900 frames, two runs each, per frame:
   heavy (15 shadow passes) `draw_shadow` 568 / 566 us -> 347 / 301 us, the
   shadow record writes (15 `Primitive_buffer::update` calls, about 6600
   records of 256 bytes) become one `update_records` call of 35 / 36 us,
   `Draw_indirect_buffer::update` 110 / 111 -> 89 / 88 us (17 calls),
   `classify_cull_views` unchanged (41 / 41 -> 39 / 41 us); default scene
   (4 shadow passes) `draw_shadow` 104 / 106 -> 93 / 77 us, shadow record
   writes about 29 us (4 calls) -> 10 / 10 us; draws scene (1 shadow pass,
   where the shared upload copies every entry of the list and the per-pass
   write copied only the passing ones) `draw_shadow` 87 / 86 -> 78 / 80 us,
   shadow record writes about 44 us -> 39 / 40 us, so the single-pass case
   is not slower here. Phase 5 moves the upload to publish time.
5. **GPU classification.** Upload the SoA bounds at publish (change-driven, a
   few floats per entry) and the view set per frame; a compute pass writes the
   mask buffer. Consumed by phase 6; alone it only moves the work.
   Done: `Gpu_cull_classifier` (device-wide: the pipeline of
   `res/shaders/cull_classify.comp`, ring buffer clients) and the
   `Draw_list_scene` GPU half (`draw_list_scene_gpu_cull.cpp`), gated by the
   editor setting `culling_classification` (`cpu` default, `gpu`; Settings
   window, MCP `set_culling_classification`), which reaches `publish()`
   through the frame's `Render_settings` snapshot and is recorded in the
   published slot; the device capability is native multi-draw indirect plus
   `Record_indexing::first_instance`, so the emulated path and the null
   device stay on the CPU path (logged once). `publish()` under `gpu` keeps
   one host-visible bounds buffer per frame slot (all lists back to back, two
   `vec4` per entry, a skinned list unbounded) and writes only the lists
   whose serial or offset differs from what that slot's buffer holds. A
   dispatch cannot be recorded inside a render pass; phase 6a records one
   per scene per frame before the first pass, with no barrier after it
   until phase 6 reads the masks. The shader repeats the lane tests with
   `precise` intermediates.
   Gate (MCP `get_cull_classification_check`, the GPU regions read back and
   compared bit for bit with the same frame's CPU masks): 0 mismatches on
   default (14 entries, 5 views), heavy (2514, 19), draws (2514, 2), heavy in
   four view (34 views, 5 dispatches) and heavy with 8+8+8 shadowed lights in
   four view (92 views, two groups); a deliberately wrong plane test gives
   178 mismatches on heavy. Images and per-pass counts byte- and
   count-identical to phase 4 at `cpu` and `gpu` (default, heavy, draws;
   heavy in both render thread modes); heavy with Vulkan validation: no
   errors, the known warnings only. Release MSVC, Intel iGPU, heavy, 15 s
   per run: `classify_cull_views` 21.7 -> 22.0 us mean per call (the CPU
   classification still runs), dispatch recording 15.8 us median per call
   (two per frame, 10 us self), `Gpu_cull_classifier::record_pending` 11 us
   median, the bounds write 0.2 us on a frame where no list changed and 7.4 us
   median on a frame where an object moved (the heavy scene has one color
   and one shadow list of 1257 entries each, both holding the object, so a
   move rewrites both: per-list writes save nothing there; per-entry dirty
   ranges would); GPU time of the shadow
   batch 8 to 24 us, of the frame-end color batch about 2 us.
6. **GPU command generation.** A compute pass compacts, per view, the visible
   entries into a dense indirect command buffer and a count;
   `vkCmdDrawIndexedIndirectCount` (query `drawIndirectCount`; GL has
   `glMultiDrawElementsIndirectCount`, Metal its indirect command buffers)
   executes it. Phase 4's record indexing is the precondition. Devices on the
   emulated multi-draw path keep phases 2 to 4.
   6a, the pre-draw view hook. Done: `Rendergraph::execute()` runs every
   enabled node's `Rendergraph_node::pre_execute_rendergraph_node()` in
   execution order, then the pre-execute callback
   (`Rendergraph::set_pre_execute_callback()`), before the first node
   executes, outside any render pass (doc/erhe/rendergraph.md "Pre-execute
   hook"). `Shadow_render_node`'s hook applies the frame's configuration
   and calls `Shadow_renderer::fit()` (the frustum fit, once, and the passes'
   culling views); its execute calls `Shadow_renderer::draw()`.
   `Viewport_scene_view`'s and `Scene_image_view`'s hooks add the color
   views (`Composer::add_cull_views()`: one view per draw list scene the
   view's draw-list passes draw, `Draw_list_renderer::add_color_cull_view()`
   from the frame's render context, which the node's hook builds and its
   execute draws with), kept in `Composition_pass_prepared::cull_view` and
   passed to `Draw_list_renderer::render()` as `Render_parameters::cull_view`.
   The editor's callback is `Cull_frame_registry::classify_frame()`: every
   scene that added views this frame (registered at its first
   `add_cull_view()`; every `Draw_list_scene` has a registry, a
   `Gpu_cull_classifier` is optional) runs
   `Draw_list_scene::classify_frame_cull_views()`, the CPU lanes over every
   view of the frame at once and, under `gpu`, the frame's one dispatch with
   one mask region (`group * entry_count + entry`). `record_pending()`,
   `add_pending_scene()`, `record_cull_classification()` and the
   end-of-frame dispatch are gone; a view added after the classification, a
   draw naming an unclassified view and a frame whose views were never
   classified are verified defects. The headset's color passes cull
   through one view of their own ("Headset passes" below). Scenes built without a
   `Gpu_cull_classifier` classify on the CPU (heavy, also in four view,
   byte-identical). `erhe_rendergraph_gpu_tests` checks the hook order. Images and
   per-pass / per-light counts byte- and count-identical to phase 5 at `cpu`
   and `gpu` (default, heavy, draws; heavy in four view; heavy at `gpu` in
   both render thread modes); readback gate 0 mismatches with one dispatch
   per frame: default 5 views, heavy 19, draws 2, heavy in four view 34
   (phase 5: 2, 2, 2 and 5 dispatches); heavy with Vulkan validation: no
   errors, the same warnings as phase 5 in the same run (6 shader output
   not consumed, 9 undefined-to-read-only transitions). Release MSVC, Intel iGPU,
   heavy, `cpu`, pipelined, 900 frames, two runs each, median per frame:
   `classify_cull_views` 38.7 / 39.8 us in two calls -> 39.2 / 38.0 us in
   one, `draw_color` 101.7 / 103.2 -> 92.0 / 90.7 us (its color
   classification moved to the hook), `draw_shadow` 245.5 / 254.6 -> 252.6 /
   255.1 us, `Shadow_renderer::fit()` in the shadow node's hook 958 / 950
   us (the fit `Shadow_renderer::render()` ran before),
   `Composer::add_cull_views()` 1.0 us.
   6b, GPU command generation for the shadow passes. Done:
   `Device_info::use_draw_indirect_count` (Vulkan: `VK_KHR_draw_indirect_count`
   enabled, which enables `drawIndirectCount`, on the native multi-draw path;
   GL 4.6; Metal through indirect command buffers, `doc/plans/metal_backend.md`
   section 6; false on MoltenVK and the null device) and
   `Render_command_encoder::multi_draw_indexed_primitives_indirect_count()`
   (`doc/erhe/graphics.md` "Draw count from the GPU"); the
   `Gpu_cull_classifier` capability now needs the count draw.
   `Shadow_renderer::fit()` gives the frame's `Shadow_command_selection`
   (caster filter, draw-list layers, chunk size); under `gpu` the
   classification then builds a job table per frame (one job per shadow view
   and per chunk of each selected shadow list, its command range aligned to
   32) and records the two passes of `res/shaders/cull_compact.comp`: pass
   bits (one invocation per job entry: view bit and filter on the entry's
   flag bits, into per-job bit words) and compaction (one workgroup per job:
   per-lane runs of words, one prefix sum, commands written in entry order),
   between storage barriers and a command barrier. `publish()` writes the
   command templates (`index_count, first_index, base_vertex` and the flag
   bits) next to the GPU bounds under the same serial check. `draw_shadow()`
   binds the chunk's shared records and draws the job's commands with the
   count draw (`max_draw_count` = chunk size); no `Draw_indirect_buffer`
   write. The order is the CPU order by construction (no atomics on the
   output), so the depth-only and the cube distance passes rasterize the
   same primitive sequence; a first version with one workgroup per job
   scanning 64-entry tiles measured about 100 us GPU on heavy, per-lane runs
   about 45 us, the two-pass split the same at the low GPU clock and scales
   with the job size. The GPU path makes no per-entry decision on the CPU:
   every chunk of a drawn list is one count draw, and the passes' entry and
   culled counts come from the count readback (drawn and filter-passing per
   job) of the retired frame that last used the frame slot, mapped by that
   frame's passes (`Shadow_draw_statistics::gpu_counts_frame_index`, frames
   in flight late). Refused, logged once, while the `Draw_indirect_buffer`
   debug index count clamp is on. Still on the CPU under `gpu`: the CPU
   lanes (for `has_cull_view_survivors()`, the empty cube face skip, and
   then the color passes, which phase 6c moved to the GPU), the job table,
   and the color passes' commands (phase 6c). Gate: MCP `get_cull_classification_check` compares per job the
   GPU count and commands with what the CPU writer produces from the same
   frame's CPU masks: 0 mismatches on default (4 jobs, 24 commands), heavy
   (18 jobs, 6619), draws (1 job, 1044), heavy in four view (30 jobs, 6707),
   heavy in both render thread modes and with validation; per view the GPU
   drawn counts equal the passes' counts (heavy: 6619 over the 8 lights as
   `get_scene_lights` reports them). Images and per-pass / per-light counts
   byte- and count-identical to phase 6a at `cpu` and to `cpu` at `gpu`
   (default, heavy, draws, heavy in four view, heavy at `gpu` in both render
   thread modes); a deliberately wrong `first_instance` in the compaction
   changes the heavy image, so the gate sees the GPU path; heavy with Vulkan
   validation: no errors, the same 6 + 9 warnings as phase 6a.
   `erhe_graphics_gpu_tests` `Draw_indirect_test.draw_indexed_indirect_count`
   / `_count_zero` check that the GPU-read count (2, 0 of max 3) decides.
   Measured on an idle machine (Intel iGPU, pipelined render thread, 900
   frames, median per frame; the same build at the cpu and the gpu setting,
   images byte-identical): heavy scene Release `draw_shadow` 251 -> 86 us,
   `Draw_indirect_buffer::update` 82 us over 17 calls -> 10 us over 2 (the
   color passes), render phase 2829 -> 2693 us; Debug `draw_shadow` 1122 ->
   212 us, render phase 31.9 -> 30.9 ms. Default scene Release `draw_shadow`
   67 -> 28 us, Debug 142 -> 84 us. GPU timers, Release: classification 7.0
   us and command generation 12.6 us on heavy, 3.4 / 4.5 us on default. The
   CPU side of the gpu setting costs 55 to 75 us per frame more than the
   cpu setting in `Cull_frame_registry::classify_frame` (the CPU lanes still
   run for the survivor counts and the job table is built on the CPU); the
   next saving is there. Against the pre-culling baseline the whole plan
   (phases 1 to 6, gpu setting) takes the heavy scene's `draw_shadow` from
   674 to 86 us Release and from 9.7 ms to 0.21 ms Debug, and the Debug
   heavy frame rate from 24 to 32 fps; the Release scenes are GPU-bound on
   that machine, so their frame rate does not move.
   6c, the color passes on the GPU. Done: the draw-list color passes draw
   GPU-generated commands under `gpu`, on devices with
   `Device_info::use_draw_indirect_count` (Metal 3, Metal 4, KosmicKrisp;
   MoltenVK keeps CPU-written commands, unchanged). Commit 1: every
   draw-list pass of a frame, shadow and color, binds the frame's one
   shared record range per list chunk, uploaded by the first pass that
   draws the chunk with the default color / size patch and released by
   `Cull_frame_registry::release_frame_records()` at the end of the frame's
   render phase (`doc/erhe/draw_list_renderer.md` "Record upload"). No
   per-pass constant block was needed: no draw-list shader variant reads a
   record's color or size (polygon fill without shader debug only;
   verified per resolved variant). Commit 2a: the Metal backends read a
   fragment function's dedicated samplers from an argument buffer, so the
   color pipelines can execute indirect command buffers
   (`doc/erhe/metal_backend.md` "Two Sampler Binding Paths"). Commit 2:
   each draw-list color pass registers a `Color_command_selection` with its
   culling view (`Composer::add_cull_views()`), the job table gets one job
   per (selection, color list chunk) after the shadow jobs, each job with
   its own filter (a 16-bit index into the frame's filter table packed
   beside the view), the color jobs are prepared once per frame as one
   set of their own `Indirect_count_draws` right after the compaction, and
   `draw_color()` draws them with the count draw
   (`doc/erhe/draw_list_renderer.md` Q6 "GPU command generation"); the
   passes' counts arrive with the retired frame's readback per selection
   (`Composition_pass_stats::draw_list_counts_frame_index`). Gate: MCP
   `get_cull_classification_check` 0 mismatches and 0 command mismatches
   with the color jobs included (`command_color_job_count`) on default,
   draws, heavy, heavy with selection and heavy in four view (Metal 3,
   Metal 4, KosmicKrisp) and Bistro (Metal 4, 759 jobs, 59 color);
   `render_scene_image` and screenshots byte-identical to the CPU-written
   color commands on Metal 3 and Metal 4 (default, draws, heavy, heavy with
   selection, heavy in four view; Bistro cpu vs gpu); KosmicKrisp
   identical except 3 to 8 pixels off by 1 LSB in the cases with the
   selected-entry passes. Color indirect command buffer per frame in flight
   on Metal: Bistro 10240 commands (7.1 MB, one viewport and one scene
   image view), heavy in four view 11264 (7.8 MB). Measured, Metal 4,
   Release headless, pipelined, `gpu` (Tracy): `draw_color` CPU 106 -> 20
   us per frame on draws and 130 -> 25 us on heavy, render phase about 40
   us lower; the GPU `Main` zone 987 -> 1097 us on draws and 2658 -> 2763
   us on heavy, so the GPU-bound tick grows about 85 us (draws 1709 -> 1795
   us, heavy 3399 -> 3482 us): the main pass executing its draws from the
   indirect command buffer, not the encode kernel (about 6 us; split in
   "Open").
7. **Occlusion.** [occlusion_culling.md](occlusion_culling.md), fed into the
   mask of the camera views as one more test; depends on phase 6 to be worth
   it (its step 7).

Later, outside this plan: an octahedral single-pass point shadow (one view and
one pass per light instead of six, Esoterica's `Point Light Shadows.md`),
which changes the shadow sampling and the caster shader, and screen-size
culling for shadow views, which Esoterica tried and disabled.

## Headset passes

The headset's color passes (the multiview pass, or both eyes of the per-eye
path) cull through one view: the union of the eye frusta, padded, added by
`Headset_view_node::pre_execute_rendergraph_node()` before the frame's
classification like the viewports' color views.

- **Planes, M2.** `Headset_view::update_camera_node()` locates the eye
  views at the frame's predicted display time (`Headset::locate_views()`),
  reduces them to the union of the fov sides about the head origin
  (`cache_combined_eye_frustum()`, the frustum the shadow fit already
  uses: each eye's four corner rays rotated into head space by the inverse
  head orientation times the eye orientation, then the min / max
  horizontal and vertical angles over all corners, so canted eye views are
  covered exactly), and `prepare_cull_planes()` writes the four side planes of that
  union in world space into the frame's slot (one per frame in flight),
  from the head pose the eye cameras are posed relative to (scene root
  node, head pose, camera offset). There is no near or far plane.
- **Margins.** Each side opens by 2 degrees and every plane moves outward
  by 0.1 m. R re-locates the eye views at the same predicted display time
  and renders from those (`Xr_session::render_frame()` /
  `render_frame_multiview()`); the union is computed from the located eye
  orientations (no parallel-eye assumption), so the angular margin covers
  only the drift of that re-locate (a prediction that moved between the two
  locates), and the positional margin covers the eye frusta's apexes half
  the IPD (about 0.032 m) from the head origin and a re-located head
  position. In benchmark mode both sides pose the head
  from the same path pose.
- **View, R.** `Headset_view::add_render_cull_view()` adds the slot's
  planes to the headset scene's `Draw_list_scene` (`Draw_purpose::color`)
  when the frame renders, the draw lists are on and M2 wrote planes for
  the frame; otherwise the passes draw unculled. `render_headset()` hands
  the view to its render contexts (`Render_context::immediate_cull_view`
  with the draw list scene it belongs to), and
  `Composition_pass::render_immediate()` gives it to every draw-list pass
  that draws that scene. The per-pass drawn and culled counts are the
  composition pass statistics (MCP `get_composition_passes`, scene view
  `Headset`).
- **Switch.** `App_settings::headset_culling_session` (default on, never
  persisted; Settings window "Headset Culling"; the editor's
  `--no-headset-culling` turns it off for the session) reaches the hook
  as `Render_settings::headset_culling`, for an A/B without a rebuild.

The headset's color passes go through the draw lists only when the
`use_draw_lists` setting is on; `config/editor/openxr_editor_settings.json`
turns it on (before 2026-10-24 it was off, so the headset drew through the
bucket path, which no culling view reaches). Results on the Quest 3:
[quest_performance.md](quest_performance.md) "Results: culling the headset
passes".

## Measurement

`scripts/render_thread_bench.py --scene heavy --tracy` and
`scripts/tracy_zone_summary.py` for the `draw_shadow`, `draw_color` and
`draw_list_chunks` zones; `Shadow_draw_statistics` per pass (culled and
drawn counts) through the shadow fit debug dump; `render_scene_image` A/B
against the previous phase. Debug and Release both: the per-entry overhead
that dominates Debug is what phases 1 to 4 remove, the GPU phases are
measured on the GPU timers.

## Open

- Mask width: `uint64_t` covers the passes of a frame today; several
  viewports with their own color, id and overlay passes could exceed it, in
  which case the classification runs per 64-view group.
- Skinned entries carry no bounds and pass every view; posed bounds from the
  joints ([draw_list_renderer.md](draw_list_renderer.md) item 1) would let
  them be culled.
- The directional fit culls casters against its own volume before the pass
  frustum exists; the two sets differ and both stay.
- The survivor count behind `has_cull_view_survivors()` is purpose-wide:
  every layer, every flag bit, and a skinned list counts every entry. A cube
  face with any skinned shadow list, or with an entry in range that the
  shadow filter rejects (not a caster), is never skipped. A per-pass caster
  mask applied to the count is the fix if it matters.
- The headset's culling view is one conservative view for both eyes
  ("Headset passes"); per-eye views would cull tighter but need the eye
  poses of R's locate before the classification (`erhe::xr` locating once
  in the hook and `render_frame()` reusing it).
- Under `gpu` on Metal the GPU-generated color commands cost about 110 us
  more GPU time per frame than the CPU-written ones (phase 6c; accepted for
  now). Metal System Trace split (Apple M4 Pro, Release headless,
  pipelined, `force_disable_vsync`, 200 mid-trace frames, median per
  frame, us):

  | Scene, backend | main pass cpu | main pass gpu | vertex cpu -> gpu | fragment cpu -> gpu | color encode kernel |
  |---|---|---|---|---|---|
  | draws, Metal 4 | 988 | 1100 | 403 -> 471 | 584 -> 628 | 5.9 |
  | draws, Metal 3 | 990 | 1100 | 402 -> 471 | 586 -> 629 | 6.2 |
  | heavy, Metal 4 | 2661 | 2766 | 406 -> 473 | 2255 -> 2293 | 6.1 |
  | heavy, Metal 3 | 2660 | 2767 | 403 -> 471 | 2257 -> 2296 | 6.2 |

  The encode kernel is about 6 us, and no empty tail slot executes: the
  kernel writes each job's execution range as {first command, the dense
  GPU count}, so `executeCommandsInBuffer` runs exactly the drawn
  commands. The cost is the main pass executing its draws from the
  indirect command buffer instead of CPU-encoded indirect draws: about
  +68 us vertex and +40 us fragment work for the same draws, the same on
  both Metal backends (so it is the hardware / driver ICB path, not the
  Metal 4 encoder). The fragment part is not explained yet (same pipeline
  state, same draws in the same order). Not pursued further for now.
- Under `gpu` the CPU lanes still run for `has_cull_view_survivors()`;
  dropping them needs the empty face skip from a GPU-written survivor count
  (an indirect clear-only path).
- The compaction pass runs one workgroup per job, so a single large job
  (the draws scene: one shadow pass over one list) is latency-bound (about
  44 us GPU at the low iGPU clock); splitting jobs into several workgroups
  needs a per-job prefix over workgroup counts (one more pass).
- With a node selected and moved (MCP `transform_selection` twice a second
  on the heavy scene), the draw lists change on every frame, also between
  the moves: `flush_pending` about 440 us per frame, a list copy and a bounds
  write per frame. Pre-existing and untraced; the follow-up is which hook
  fires every frame (transform serial, bounds or material).
