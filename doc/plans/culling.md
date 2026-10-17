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
3. **Point cube faces by octant.** A face's four side planes through the light
   are fixed in light space, so a box's face set is a sign and magnitude test
   on its min and max relative to the light; faces and whole cubes with no
   survivors begin no render pass. Takes over the "skip empty faces and
   lights" and "range cull" items of [shadows.md](shadows.md).
4. **Shared records, per-pass commands.** The shadow passes' primitive records
   do not depend on the light: upload one record block per list per frame
   and let each pass's commands name their record through the command's
   first-instance field (`gl_BaseInstance`, `Device_info::use_base_instance`)
   instead of `ERHE_DRAW_ID`'s position in the command list. Removes the
   per-pass record memcpy (half of what phase 2 leaves). Needs the shader
   interface change in `erhe_primitive.glsl` and the emulated path to keep
   working through its push constant.
5. **GPU classification.** Upload the SoA bounds at publish (change-driven, a
   few floats per entry) and the view set per frame; a compute pass writes the
   mask buffer. Consumed by phase 6; alone it only moves the work.
6. **GPU command generation.** A compute pass compacts, per view, the visible
   entries into a dense indirect command buffer and a count;
   `vkCmdDrawIndexedIndirectCount` (query `drawIndirectCount`; GL has
   `glMultiDrawElementsIndirectCount`, Metal its indirect command buffers)
   executes it. Phase 4's record indexing is the precondition. Devices on the
   emulated multi-draw path keep phases 2 to 4.
7. **Occlusion.** [occlusion_culling.md](occlusion_culling.md), fed into the
   mask of the camera views as one more test; depends on phase 6 to be worth
   it (its step 7).

Later, outside this plan: an octahedral single-pass point shadow (one view and
one pass per light instead of six, Esoterica's `Point Light Shadows.md`),
which changes the shadow sampling and the caster shader, and screen-size
culling for shadow views, which Esoterica tried and disabled.

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
