# Lightmap baking

Stability: experimental

The lightmap baker bakes static scene lighting into a lightmap texture with
minimum authoring effort: lightmap UVs are assigned automatically, texel density
comes from the world-space tile grid, and the bake runs on the GPU using Vulkan
ray query.

The bake is **interactive and iterative**: it runs inside the normal frame loop
under a per-frame GPU budget, the user keeps navigating and editing the scene
while it converges, the viewport always shows the current (partially converged)
lightmap, and edits - moving a light, changing its color, toggling it - restart
accumulation so the lightmap visibly re-converges toward the new lighting.
Baking is a mode you leave on, not a modal job you wait for.

Baking needs `Device_info::use_ray_query` (Vulkan). Runtime *sampling* of the
result works on every backend and on Quest.

Related documents: [`raytrace.md`](raytrace.md) (the GPU ray trace renderer this
builds on), [`lightmap_texture_viewer.md`](lightmap_texture_viewer.md) (the
atlas viewer), [`plans/uv_editor.md`](../plans/uv_editor.md) (UV infrastructure
inventory), [`shadows.md`](../erhe/shadows.md),
[`scene_serialization.md`](scene_serialization.md).

## 1. References

The architecture follows the one the modern bakers converged on (Godot 4
LightmapperRD, Unity Progressive GPU Lightmapper, Unreal GPU Lightmass,
Frostbite Flux, Bakery): automatic per-mesh UV unwrap into a dedicated lightmap
channel, per-instance atlas packing by world-space texel density, UV-space
rasterization into a texel G-buffer, a progressive path-traced gather per texel,
artifact hardening (leak prevention, guided denoising, dilation, seam handling)
and HDR storage.

Read in this order when working here:

- Bakery author, "Baking artifact-free lightmaps on the GPU" -
  https://ndotl.wordpress.com/2018/08/29/baking-artifact-free-lightmaps/ - the
  practical artifact checklist erhe follows (UV-space raster, conservative
  coverage, push-off, bias, dilation, bicubic sampling).
- Godot `modules/lightmapper_rd` (MIT: `lm_raster.glsl`, `lm_compute.glsl`,
  `lm_blendseams.glsl`, the JNLM denoiser) - the reference implementation for
  erhe's denoise and seam-blend passes.
  https://github.com/godotengine/godot/tree/master/modules/lightmapper_rd
- nvpro `vk_mini_path_tracer` - compute + ray query path tracer tutorial.
  https://nvpro-samples.github.io/vk_mini_path_tracer/
- MJP BakingLab (MIT) - reference for basis encodings (flat irradiance, HL2,
  SH L1, SG). https://github.com/TheRealMJP/BakingLab
- ands/seamoptimizer (least-squares seam fix) -
  https://github.com/ands/seamoptimizer
- Castano's Witness posts (sample validity, parameterization, compression) -
  http://www.ludicon.com/castano/blog/articles/lightmap-parameterization/

erhe follows the Bakery article closely, with two deliberate exceptions: the
denoiser is a Vulkan-native compute pass (Godot's JNLM port) rather than the
article's OptiX AI denoiser, because the article's reversible-tonemap trick is
OptiX-specific and JNLM works in linear HDR directly; and mip-level UV chart
repacking stays out of scope until the lightmap has mips at all.

## 2. Design decisions

- **Ray query in compute**, not a ray tracing pipeline. The gather reuses
  `ray_trace.comp`'s BLAS / TLAS setup, instance records, buffer-device-address
  attribute fetch, and `erhe_light.glsl` / `erhe_bxdf.glsl`.
- **UV channel 2** (`tex_coord` usage_index 2) is the lightmap channel, in
  stream 1 of the `Mesh_memory` vertex formats. Channels 0 and 1 keep their
  meanings (glTF `TEXCOORD_0` / `TEXCOORD_1`), so the lightmap needed its own.
- **Unwrap with `make_atlas`** (`erhe::geometry::operation`, Geogram
  parameterizers plus xatlas chart packing) per piece into normalized charts.
- **Per-instance atlasing**: each lightmapped piece gets a rectangle in a tile
  texture, sized by `surface_area x density`. The per-draw
  `vec4 uv_scale_offset` lives in the primitive / draw data, not the material,
  because the same material must work lightmapped and not.
- **Flat RGB irradiance**, no directionality. SH L1 is a later extension the
  storage layout does not preclude.
- **The lightmap holds full lighting (direct + indirect) and analytic lights are
  gated off per lightmapped draw.** The same per-primitive region gate that
  enables the lightmap sample skips the analytic light loops in
  `standard.frag`, so a mesh is lit either entirely by its bake or entirely
  analytically, never both. A light edit therefore shows through
  re-convergence (direct lands after one full atlas sweep, bounces follow)
  rather than instantly, and the runtime cost of a lightmapped mesh is minimal.
- **Interactive progressive bake**: the gather runs every frame as a budgeted
  compute dispatch, accumulating samples per texel across frames. The viewport
  samples the live running-average texture.
- **Change-driven invalidation, cheapest first** (section 3a).
- **Texel density comes from the tile grid**: `tile_texture_size` divided by the
  cell size gives a tile's nominal texels per meter. Everything else is a fixed
  default (padding 4 texels, the sample and bounce targets).
- **Static classification**: `Item_flags::lightmapped` (bit 30) serializes by
  name and round-trips through glTF. Skinned meshes are excluded (they have no
  BLAS).

## 3. Architecture

```
   per piece       Unwrap: make_atlas -> texcoord channel 2       (CPU, cached)
                                     |
   per tile        Atlas layout: pack piece rects,                (CPU)
                   assign uv_scale_offset per piece
                                     |
                   Texel G-buffer: raster charts in UV space      (GPU raster,
                   -> position / normal / albedo / smooth pos      conservative
                                     |                             coverage)
                   Gather: compute + rayQueryEXT                  (GPU,
                   explicit light sampling + cosine hemisphere     progressive)
                   indirect, virtual offset, bias, backface kill
                                     |
                   Post: resolve -> JNLM denoise -> dilate        (GPU compute
                   -> seam blend                                   + raster)
                                     |
                   Runtime: standard.frag samples the lightmap    (all backends)
                   via the texture heap, uv2 * scale + offset
```

The bake core is `src/editor/renderers/lightmap_baker.{hpp,cpp}` with shaders in
`res/editor/shaders/`; `Lightmap_partitioner` and `Lightmap_grid` own the
world-space partition (sections 9 and 10), `Lightmap_streamer` the disk
residency, and the Lightmap window plus the `lightmap_*` MCP tools are thin
clients of all of them.

## 3a. Interactive bake loop and invalidation

Per frame, while baking is enabled and `use_ray_query` is true:

1. **Collect changes** since the last frame and downgrade the bake state to the
   cheapest level that covers them. Three FNV hash tiers drive this:

   | Change | Response |
   |---|---|
   | Light moved / recolored / toggled, ambient, emissive material edit | reset accumulation (zero the sample counts; keep the G-buffer, atlas and BLAS) |
   | Region transform changed | re-raster that region's G-buffer, refresh the TLAS, reset accumulation |
   | Geometry edited | rebuild the BLAS, re-unwrap if topology changed, then as above |
   | Lightmapped set or grid parameters changed | redo the atlas layout, full G-buffer, reset |
   | Camera motion, dynamic (non-lightmapped) object motion | nothing: the bake input is unaffected |

   Continuous edits need no debounce: a reset is a cheap clear, and the cost is
   that convergence time restarts, which is the expected behaviour.

2. **Dispatch a budgeted gather slice.** A persistent tile cursor walks the
   atlas in bands of at most 2^18 texels per frame, recorded into the frame
   command buffer through ring buffers and the per-frame-in-flight TLAS slots.
   Early passes sweep the whole atlas at one sample per texel, so the entire
   scene gets a rough answer fast instead of converging the first tile fully.

3. **Publish.** The resolve pass writes the running average (sum / count) into
   the display atlas the renderer samples. During sweep 0, and after any reset,
   the raw average publishes every tick for immediate feedback; from sweep 1 on,
   publishing happens only on sweep completion, as resolve -> denoise -> dilate
   -> seam blend. Mid-sweep ticks skip resolve and dilate entirely, so the
   steady-state per-tick cost is lower and the viewport (and the bounce
   feedback) sample a stable denoised atlas.

4. **Bounce feedback.** Indirect rays sample the *published* atlas at hit points
   through the per-instance texcoord-2 address (`Lm_instance_record` SSBO), so
   the bake behaves like progressive radiosity: after a light edit direct light
   snaps in within a sweep or two and bounces flow in over the following
   seconds.

State per bake session: the accumulation atlas (RGBA32F sum + count), the
published display atlas, the texel G-buffer, the atlas layout, the tile cursor
and the per-cause dirty flags.

**Bake lifecycle.** Stopping is pausing: `set_baking_enabled(false)` keeps the
whole working set, Start continues and Reset restarts. Pausing (including
completion of Single Iteration, which bakes until the minimum active-tile sweep
count plus one) autosaves resident dirty tiles through the save-on-evict drain.

**Binding invariant.** The baker and the disk streamer each write
`Mesh_primitive::lightmap_uv_scale_offset` in their **own** atlas slot space, so
every binding-owner transition must republish: resuming drops the baker's
published-regions flag, and the baker-to-streamer handoff calls
`Lightmap_streamer::reapply_regions`. The streamer owns the binding only while
the baker is idle with nothing unsaved and nothing stale.

**Staleness always shows white, never an old bake.** The display clears to white;
a prepare commit clears the display immediately and requests a single iteration;
a freshly assigned display slot is white-overwritten before its first publish
(a disk restore unqueues its slot); a reset white-clears inactive resident slots;
and the three-tier hashes run every frame while *not* baking, so a scene change
during a pause whites out the display and takes the binding back.

**Resident and active are nested camera-ranked sets.** The resident set (the
slot grid, `resident_tile_budget`) drives slot assignment; the active set (its
`active_tile_budget` prefix) drives gathering. Inactive resident tiles keep
their slot and their last publish, and restore-on-activate gates on residency.
Tile-bounds debug colours: white = active, cyan = resident, purple =
non-resident.

## 4. Phases

The baker is built out of these parts; the labels are cited from source
comments.

**Phase 1 - lightmap UV channel and unwrap.** `tex_coord` usage_index 2 in the
`Mesh_memory` stream-1 vertex formats and in `Primitive_builder`, the
`USE_VERTEX_VARYING_TEXCOORD2` shader bool and `v_texcoord_2` varying, the
`Item_flags::lightmapped` flag and its UI, and the
`generate_mesh_atlas_texture_coordinates` unwrap into channel 2. The "TexCoord 2
(Lightmap)" shader debug mode shows the charts.

**Phase 2 - atlas layout and texel G-buffer.** Region sizing
(`sqrt(world_area / uv_coverage) * density`, so texels per meter is exact per
region regardless of gutters, packing waste and min-chart upscales, with
coverage floored at 0.05), skyline packing, and the UV-space raster pass. The
vertex shader outputs `clip = (uv2 * scale + offset) * 2 - 1` and the fragment
writes world-space attributes into four RGBA32F / RGBA16F targets: position,
normal (with the world texel size in `.w`), albedo and Phong-tessellated smooth
position.

**Phase 3 - gather.** A compute shader over valid texels reusing
`ray_trace.comp`'s TLAS binding, instance records, material SSBO and light
structures: explicit light sampling to every light with a traced shadow ray,
plus one cosine-sampled hemisphere ray per texel per sample whose radiance is
`albedo_hit * published_irradiance(hit lightmap UV)`.

**Phase 4 - post-processing.** Dilation (valid to invalid 8-neighborhood, about
`padding` iterations), JNLM denoise, and the seam blend, in that order.

**Phase 5 - runtime sampling.** `Mesh_primitive::lightmap_uv_scale_offset` rides
the `Primitive_buffer` as a vec4 into a flat varying (location 22 - the lit
fragment path has no draw id, so per-draw data reaches `standard.frag` as flat
varyings rather than `primitive.primitives[]` indexing). The atlas is bound as
`s_lightmap` (`c_texture_heap_slot_lightmap = 4`) by
`Light_buffer::bind_lightmap` from `Forward_renderer`, with a black fallback;
`standard.frag` replaces the ambient term with
`sample_lightmap_bicubic()` when the region is valid, and gates the analytic
light loops off for that draw.

**Phase 6 - persistence.** Baked tiles are written to disk as `.lmt` payloads
with a manifest, and streamed back by residency; see section 9.

## 5. Artifact defenses

These replace ad-hoc defenses with the article's, and each one is load-bearing:

1. **Native conservative rasterization.** `VK_EXT_conservative_rasterization`
   (overestimation, properties-only - no feature struct) is detected in
   `query_device_extensions`, exposed as
   `Device_info::use_conservative_rasterization` and opted into per pipeline
   through `Rasterization_state::conservative_enable`. With it the G-buffer
   rasters one pass (the center tap); the 9-tap jitter loop is the fallback on
   devices without it.
2. **Leak defenses at the source.** Ray origins use an adaptive bias
   (`pos += sign(dir) * max(abs(pos) * 2e-7, 1 micron)`) for both shadow and
   bounce rays. A virtual offset (sample push-off) pass rewrites the position
   G-buffer in place after every G-buffer bake - it runs **once**, as a
   pre-pass: doing it per texel per sample inside the gather tripled frame time.
   The world texel size rides in G-buffer `normal.w` (derivative trick at raster
   time, times sqrt(2)). A bounce ray hitting a backface closer than one texel
   zeroes that texel's accumulation (dilation fills it); farther backface hits
   contribute nothing. Shadow rays trace with
   `gl_RayFlagsCullBackFacingTrianglesEXT`.
3. **Smooth position and the terminator.** The fourth G-buffer attachment is a
   Phong-tessellated smooth position, computed per fragment without vertex fetch
   or barycentric extensions: `smooth(p) = p - (M(p) * p - b(p))`, where
   `M = sum w_i n_i n_i^T` and `b = sum w_i dot(p_i, n_i) n_i` interpolate
   linearly as varyings and the quadratic term falls out of applying the
   interpolated `M` to the interpolated `p`. The face normal is deliberately not
   stored: its only consumer is the "smooth position must stay on the front side
   of the face plane" validation, and the fragment shader already has the face
   normal from `dFdx` / `dFdy` of world position, so that validation happens at
   raster time - which also keeps the pass within the graphics layer's
   four-color-attachment limit. The neighbor-geometry validation runs in the
   one-shot adjust pass (which has the TLAS): a segment ray from the flat to the
   smooth position, and any hit keeps the flat position. The winner goes through
   the virtual-offset probes and into the position G-buffer, so the gather is
   untouched.
4. **JNLM denoise.** A port of Godot's `lm_compute.glsl` MODE_DENOISE joint
   non-local means (MIT, based on YoctoImageDenoiser; attribution in the code),
   guided by the G-buffer albedo and normal. Validity comes from the published
   alpha and a zero-length normal, which naturally excludes backface-invalidated
   texels. The window sizes are compile-time defines and are smaller than
   Godot's (half search 3 versus 10, half patch 1 versus 3) because this runs
   inside the interactive loop, not once at bake end; the sigmas are Godot's
   defaults. Denoise writes into the dilate scratch, and dilation then runs an
   odd iteration count so the final pass lands in the published texture.
5. **Seam blend.** Seam detection runs on the GEO mesh in
   `build_seam_vertices()` (called from `update_layout`), where shared vertex
   ids make the position test exact: a facet edge whose second occurrence
   carries different channel-2 UVs is a seam, and corner normals must match
   within dot 0.99 per endpoint, because a hard edge has genuinely discontinuous
   lighting and must not be blended. Per publish, after denoise and dilate, the
   published atlas is copied into the dilate scratch (which avoids the
   read/write hazard) and each seam draws as two atlas-space lines, one per
   side, sampling the **opposite** side from the copy at alpha 0.5 with standard
   alpha blending (destination alpha is preserved - it is the validity flag).
   Because both directions sample the pre-pass copy, one pass equalizes both
   sides to their average, so Godot's depth-mask and eight jitter passes are not
   needed (dilation already guards the bilinear skirt).
6. **Bicubic sampling.** `sample_lightmap_bicubic()` in `standard.frag` does
   cubic B-spline reconstruction with the standard four-bilinear-tap trick. The
   4x4 footprint reads at most 2 texels outside a chart, inside the 4-texel
   dilation skirt.

Gutter-aware dilation clamps the iteration count to `min(4, gutter / 2)`, and
the Lightmap window warns when the gutter is under twice the filter reach
(bilinear 1 texel, bicubic 2).

## 6. Requirements the bake core observes

These keep a non-interactive mode cheap to add (see the plan):

- The bake core (`Lightmap_baker`: atlas layout, G-buffer pass, gather
  scheduling, post passes, persistence) does not depend on the editor UI, ImGui,
  windows or input. The Lightmap window is a thin client, as `Ray_trace_window`
  is to `Ray_trace_renderer`.
- The per-frame budget is a parameter for which "unbounded" is a valid value
  (dispatch until done, no publish cadence needed).
- Convergence is measurable: the scheduler exposes minimum and average samples
  per texel, so "converged" can be a termination criterion and not just a
  progress bar.
- Invalidation (section 3a step 1) is an optional input source; with no editor
  driving it, the bake is a straight run to convergence.

## 7. Diagnostics

- `ERHE_LM_NO_INDIRECT=1` compiles the bounce out (a pure-direct atlas) and
  `ERHE_LM_NO_SMOOTH=1` compiles the smooth-position adoption out, for A/B runs.
- `debug_write_gbuffer_pngs` dumps the position, normal and albedo targets and
  logs per-region smooth-versus-flat delta statistics;
  `debug_write_lightmap_png` dumps the published atlas through a Reinhard plus
  gamma mapping.
- `Lightmap_report` (on `App_context`) collects UV unwrap exceptions (per-mesh
  catch plus an automatic per-facet retry in `Make_atlas_operation`), layout
  warnings (density flex, budget clamps), and bake, persist and stream errors,
  and renders them as the red / yellow "Problems" list in the Lightmap window.
- MCP: `lightmap_prepare_tiles`, `lightmap_prepare_cancel`,
  `lightmap_revert_tiles`, `lightmap_set_render`, `lightmap_bake_gbuffer`,
  `lightmap_bake_direct`, `lightmap_set_baking`, `lightmap_get_tiles`,
  `lightmap_subdivide_tile`, `lightmap_merge_tile`, `lightmap_save_all_tiles`,
  `lightmap_bake_to_disk`, `lightmap_clear_tiles`, `lightmap_reorder_charts`,
  `lightmap_frame_selection`. The usual headless loop is
  `set_item_property lightmapped true` -> `lightmap_prepare_tiles {scene_name}`
  (poll `get_async_status` until idle) -> `lightmap_bake_gbuffer` ->
  `lightmap_bake_direct`, or `lightmap_set_baking` for the interactive bake.

## 8. Traps

- **A per-draw UBO binding declared vertex-stage-only must not be read in the
  fragment shader.** The read is undefined and can silently alias another field
  (an NVIDIA driver aliased `world_from_node` column 1, so every unrotated draw
  baked albedo `(0,1,0)` and the bounce tinted the whole atlas green). Per-draw
  values the G-buffer fragment needs ride a vertex-to-fragment varying.
- Vulkan offsets `combined_image_sampler` bindings past the max buffer binding
  in a bind group; raw bindings (acceleration structure, storage image) are not.
  Pick user binding points that do not collide after the offset.
- `accelerationStructureEXT` must be declared manually in GLSL; samplers,
  storage images and uniform blocks are auto-injected from the bind group
  layout.
- Bake command buffers use thread slot 6 (7 is the texture-graph export slot).
- Geogram's atlas charts are **mirrored** relative to the 3D facet winding (a
  CCW facet arrives with negative signed UV area), and the G-buffer raster's
  fold-culling convention (`cull_mode_back_cw` plus `winding_flip_if`) is tuned
  to that. A new unwrap mode must match it, or it rasterizes zero texels.
- The editor tick pushes `lightmap_config` tile size and budget into the baker
  every frame, so MCP overrides of those do not survive into interactive baking:
  config values rule.
- Piece meshes do not carry `Item_flags::lightmapped`; the partitioned layout
  enumerates them through the partitioner store, and the tick hash mixes the
  piece buffer meshes explicitly.

## 9. Spatial tiling, bake to disk, and streaming

Baking and rendering work with bounded memory regardless of world extents, mesh
count or vertex density.

- **Grid.** The world XZ plane is covered by a uniform quadtree grid
  (`Lightmap_tile_key {level, ix, iz}`, `renderers/lightmap_grid.hpp`): level-0
  cells of `lightmap.cell_size_m` anchored at multiples of the cell size from
  the world origin, over the XZ AABBs of the lightmapped content. Tile
  boundaries depend only on the grid parameters and the overrides, never on
  content, so they are stable across edits and sessions. Level +1 halves the
  cell and doubles the nominal texel density; level -1 does the opposite.
- **Per-tile down-only density.** Nominal texels per meter is
  `tile_texture_size` divided by the cell side; packing flexes the density
  **down** per tile (and reports it) when content does not fit, never up. Each
  tile packs its regions (skyline, big first) into one `tile_texture_size`
  square texture.
- **Subdivide and merge.** Density is controlled by scene-persisted leaf
  overrides (`Scene_settings::lightmap_tile_overrides`, a list of
  `{level, ix, iz}` with a non-zero level, saved through the `ERHE_scene`
  extension). With a live partition, changing an override launches an
  asynchronous re-prepare.
- **kd tree emission.** Each quadtree split is one X plane and two Z planes, so
  the clipper (section 10) consumes an ordinary kd tree. A world-origin quadtree
  has no aligned cell spanning the origin, so the tree root is a fixed origin
  cross (x = 0, z = 0) with one aligned subtree per occupied signed quadrant.
  Empty quadrants are tile -1 leaves and the partitioner drops any piece routed
  there; occupancy is AABB-conservative, so none should be.
- **Region addressing.** `Instance_region` rects and `uv_scale_offset` are
  tile-local. The renderer-facing mapping goes through the tile's *display slot*
  (`Atlas_layout::display_uv_scale_offset`): the display atlas is a grid of
  `ceil(sqrt(resident_tile_budget))` squared tile-sized slots, so the forward
  renderer still samples one plain 2D texture and no shader changes are needed.
- **Interactive residency.** The tick ranks tiles by camera distance to their
  world bounds each frame and hands the display slots to the nearest; an evicted
  tile releases its fp32 accumulation. Bounce feedback across non-resident tiles
  reads black (an accepted bias).
- **Bake to disk** (`start_offline_bake` / `offline_tick`, the Lightmap window's
  "Batch Process All Tiles"; MCP `lightmap_bake_to_disk`): one tile per frame -
  G-buffer raster, `offline_sweeps` full-tile gather submits,
  resolve / denoise / dilate / seam-blend, CPU readback, then
  `Lightmap_tile_io` writes `<scene>.lightmap/tile_L<level>_<ix>_<iz>.lmt` (a
  64-byte ELMT header, which carries the saved sweep count, plus raw RGBA16F)
  and rewrites `manifest.json` (version, tile size, density, bake-parameter
  hash, world bounds and the region table per tile, keyed by grid key). Only one
  tile's working set is ever resident. An incremental manifest lists every
  layout tile before all payloads exist, and the streamer stats payloads at
  manifest load and skips absent ones - those are not-yet-baked, not errors.
- **Save on evict.** The residency ranking never drops a tile whose published
  content is unsaved (`Tile_state::dirty_since_save`): it parks the tile in a
  pending-save queue keeping its slot (gathering stops), and the tile is
  persisted before it is released. "Save All Tiles" (button, MCP
  `lightmap_save_all_tiles`) writes every resident published tile now;
  non-resident tiles have nothing in memory to save.
- **Restore on activate.** A tile that gains a display slot with no accumulated
  content is queued for restore; the saved payload is validated against the
  current layout - bake-parameter hash, tile size, bounds and the full region
  packing table must match - and uploaded into the display slot, so the tile
  shows its saved bake instead of re-baking from black. The fp32 accumulation is
  not persisted, so gathering restarts, but republish is held until fresh sweeps
  reach the payload's saved sweep count, so the display never regresses to an
  early-sweep result. Lighting and occluder invalidations mark restores stale,
  so a reset never resurrects pre-edit content; a declined restore re-bakes from
  scratch.
- **Streaming** (`Lightmap_streamer`). When the baker does not own the lightmap
  binding, the streamer keeps the `resident_tile_budget` nearest tiles resident
  in its own slot atlas (worker-thread file read, staging-buffer upload, at most
  one load per frame, eviction hysteresis of a quarter of the incoming tile's XZ
  extent) and remaps `Mesh_primitive::lightmap_uv_scale_offset` per residency
  change. Region identity across reloads is the node path plus the node index
  path (root-to-node child indices, which disambiguates duplicate names) plus
  the mesh name, primitive index and piece ordinal; a piece stores the **source**
  mesh identity and the streamer resolves it through the live partition
  (`Lightmap_partitioner::find_piece`). A rename orphans a region, which then
  renders unlit and never corrupts anything.
- **Side-data ownership.** Every scene has a persistent
  `Scene_settings::scene_id` (lazy `Scene_root::get_scene_id`, a timestamp plus
  64 random bits, saved through the `ERHE_scene` extension). The manifest stamps
  it, and the streamer and restore-on-activate reject other scenes' sets, so
  unsaved scenes share `untitled.lightmap` safely. Creating a scratch scene
  purges stale sets, and "Clear All Tiles" (button, MCP
  `lightmap_clear_tiles`) wipes memory and disk. A bake-parameter hash mismatch
  flags the set stale in the Problems list.

## 10. World-space tile partitioning

Every lightmap-enabled mesh is baked in world space, every mesh/primitive
instance is unique after baking, and meshes are split into the spatial tiles by
clipping triangles at the tile boundary planes with all vertex attributes
interpolated. The clip results are binary exact across the two tiles of a plane,
and the originals stay in memory so the clip can be redone when parameters
change.

- **Clipper** (`erhe::geometry::operation::clip_by_tile_tree`,
  `src/erhe/geometry/erhe_geometry/operation/clip_tile_tree.{hpp,cpp}`): clips a
  world-space geometry once down the tile tree (`Clip_tree_node`: axis 0 = X /
  YZ plane, axis 2 = Z / XY plane, axis -1 = overflow split routed by the
  pre-assigned tile). Each plane is applied to each fan-triangulated fragment
  exactly once, and cuts are memoized per (canonical edge, tree node) so both
  sides reference one record. Emission goes through a `Geometry_operation`
  subclass whose identical ordered source lists make
  `interpolate_mesh_attributes()` produce bitwise-identical positions and
  attributes in both pieces (asserted by `test/test_clip_tile_tree.cpp` with
  memcmp). A vertex exactly on a plane is emitted to both sides unmodified.
  Facet provenance is kept as the facet attribute `clip_source_facet`.
- **Split estimate.** `Lightmap_baker::compute_tile_split_estimate` is
  self-contained: it enumerates the lightmapped meshes **without** the
  channel-2 UV precondition, sizes each region as
  `sqrt(world_area / c_estimated_uv_coverage) * texels_per_meter`, and returns
  the kd tree plus the per-source-primitive tile assignments without touching
  the active layout. With the world-origin grid the boundaries are exact and
  content-independent, so no whole-mesh unwrap is needed to place them.
- **Partitioner** (`Lightmap_partitioner`,
  `src/editor/renderers/lightmap_partitioner.{hpp,cpp}`): per lightmapped
  mesh/primitive instance, `get_geometry()` -> `bake_transform(world_from_node)`
  -> `clip_by_tile_tree` -> per-piece `make_atlas` (usage 2, the tile's nominal
  density, per-facet fallback) -> a new `Primitive` (renderable and raytrace).
  Pieces become `Mesh_primitive`s of one mesh per source mesh, on identity nodes
  under the "Lightmap Pieces" group, so each piece carries its own
  `lightmap_uv_scale_offset` and the renderer, raytrace and shadow paths are
  untouched by world-space vertices on an identity transform. Originals stay in
  the scene and the store keeps their primitives for revert and re-prepare. The
  partition is a derived bake artifact and is deliberately outside the undo
  stack.
- **Prepare is asynchronous.** `request_prepare` snapshots on the main thread
  (tile config, split estimate, per-region tasks) and runs the heavy phase - per
  region world-space bake and clip, per piece unwrap and primitive build, all
  parallel - on the app executor; `Lightmap_partitioner::update()` commits on the
  main thread (validate against the live scene, swap the partition, one relayout
  and publish). The old partition stays live during the flight, and a structural
  scene change (primitive swap, mesh removal) or a cancel aborts the commit and
  keeps it. `pending_async_ops` is held for the flight, so every `async_busy`
  gate covers it. The in-flight async-operations guard stays on Prepare: a
  queued mesh operation swapping source primitives mid-partition would clip
  stale geometry.
- **Pieces are render proxies.** `Item_flags::render_proxy` (bit 31) marks the
  piece meshes, nodes and group, and `Item_flags::proxy_hidden` (bit 32) marks
  the originals while "Render with lightmaps" is on; neither flag persists.
  Pieces have no `show_in_ui`, no `Item_flags::id` (so no ID render), raytrace
  mask 0 (rays pass through, as for bone proxies) and are skipped by glTF
  export. Originals stay `visible`: ID render, raytrace picking, selection, the
  gizmo and export all keep operating on the source, while the visual content
  passes, the shadow passes and the bake occluder set exclude them. Selection
  outlines still render for proxy-hidden sources through a dedicated always-on
  depth-only pass ("Selection stencil mask (proxy hidden)") that writes
  silhouette stencil bit 7, with the outline pass using a proxy-inclusive
  filter; the source's edges coincide with the proxies' surfaces, so the outline
  lands exactly around the rendered geometry. The selection fill highlight stays
  excluded, because it is coplanar with the proxies and would z-fight.
- **Editing a partitioned source auto-re-prepares.** A transform move or a
  geometry swap (geometry identity is snapshot per source primitive) launches an
  asynchronous re-prepare once the edit settles (about 60 frames of stable
  source-state hash). The old pieces and the old lightmap keep rendering until
  the commit swaps them, and the resident tiles are saved right before the
  launch, so restore-on-activate brings the unaffected tiles straight back after
  the commit while the edited tiles re-bake progressively. A moved proxy-hidden
  source does not reset accumulation by itself (it is not in the occluder hash);
  the reset arrives with the commit's piece swap.
- **Partitioned layout.** With a prepared partition, `update_layout` derives
  regions from the piece meshes with their pre-assigned tiles - the tile tree is
  the stored partition, so there is no kd re-split. Packing can only
  density-flex, and a piece that cannot fit even at minimum density is dropped
  with an error. A prepared partition is the only layout source: without one,
  `update_layout` clears the layout and returns false.
- **Render toggle and white fallback.** "Render with lightmaps" (persisted as
  `lightmap.render_with_lightmaps`) flips visibility between originals and
  pieces. A non-resident region publishes the sentinel `vec4(-1,0,0,0)`, and
  `standard.frag` renders that as flat white ambient with the analytic lights
  still gated off, so every lightmapped piece keeps rendering until its tile
  loads. Bounce feedback clamps the sentinel to zero, because white would inject
  fake energy.
- **Reorder Charts By Bake** (per tile, the active set, or all tiles; MCP
  `lightmap_reorder_charts`) re-prepares with per-facet baked-luminance order
  keys routed to the new pieces by (source mesh, source primitive, tile)
  identity, so similarly lit facets become atlas neighbours and cross-chart
  filter-tap and dilation pollution picks up similar values. Clipping is
  deterministic for unchanged sources and `make_atlas` preserves facet order, so
  keys indexed by the old piece's facet ids apply to the re-clipped pieces. It
  is idempotent for unchanged lighting.
- **Accepted limitations.** Cross-tile cut boundaries are genuine lightmap seams
  (the positions are crack-free; only the shading is discontinuous). Exactness
  holds within one mesh; pre-existing inter-mesh cracks are unchanged. Skinned
  meshes stay excluded. The clipper fan-triangulates every facet, including
  unclipped ones. Reordering a tile leaves it stale until the next bake.

## Future work

- [plans/lightmap/lightmap_baking.md](../plans/lightmap/lightmap_baking.md) -
  metals, sky lighting in the bake, GLB persistence, the command-line bake and
  the open partition defects.
- [plans/lightmap/tiling.md](../plans/lightmap/tiling.md) - open defects and
  unexercised cases of the world-space partition.
- [plans/lightmap/seam_driven_unwrap.md](../plans/lightmap/seam_driven_unwrap.md) -
  measuring seam placement from the baked lighting.
