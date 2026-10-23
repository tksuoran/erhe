# Clustered light culling

Status: in progress

Extends [../erhe/scene_renderer.md](../erhe/scene_renderer.md) (`Light_set`,
`Light_buffer`, the forward passes) and
[../erhe/shader_variants.md](../erhe/shader_variants.md) (the light-count
axes and the "Light array layout invariant"). It is option 2 of
[bistro_viewport_gpu.md](bistro_viewport_gpu.md), whose "Measurements" and
"Light selection is by scene order" sections hold the numbers this plan is
measured against. Shadow-mapped lights stay as
[../erhe/shadows.md](../erhe/shadows.md) describes them; this plan changes
how many lights are shaded and which lights a fragment evaluates, not how a
light's visibility is sampled.

## What exists

- `Light_set::resolve()` hands out light slots per type in scene order up to
  the preset's `Light_count_limits` (six counts: directional / spot / point,
  shadow-mapped / unshadowed). Slot order is type-major, shadow-mapped first
  within a type. `Graphics_settings::apply_limits` clamps the sum to
  `max_light_count` (`config/editor/renderer.json`, 1024 in the editor;
  `Program_interface_config::max_light_count` defaults to 32).
- `light_block` is a std140 uniform block at binding 1, the header (counts,
  ambient, lightmap flags, BRDF slice, shadow debug light, bias scales, DDGI
  volume); `light_array_block` is a readonly std430 storage block at binding
  9, `light_array.lights[]`, the shaded lights in slot order, 416 bytes per
  light (five mat4, six vec4). `Light_buffer::update()` writes both into
  `CPU_write` ring ranges (`Light_buffer_ranges`) once per pass
  (`Scene_pass_resources::begin_pass`, `Forward_renderer::draw_primitives`),
  so a view writes them several times per frame; every consumer of C6
  binds both.
- `standard.frag` runs six loops whose bounds are the `ERHE_LIGHT_COUNT_*`
  shader key axes, so every partition of the scene's lights is a separate
  set of shader variants: adding, removing, retyping or toggling
  `cast_shadow` on a light recompiles every material variant in use, and
  the viewport, the previews, the headlight and the null-light passes each
  hold their own partition. The spot and point loops skip a light whose
  range attenuation is zero; `get_range_attenuation()` is exactly zero at
  and beyond `range` and is inverse-square with no cutoff for `range <= 0`.
- `erhe_ray_hit.glsl` (`ray_trace.comp`, `ddgi_trace.comp`,
  `ddgi_reference.comp`, `rc_trace.comp`) loops the same array with the
  header's runtime counts and traces a shadow ray only for a light whose
  attenuation is nonzero.
- The lightmap baker reads no `light_block`: `collect_lights` fills its own
  `lightmap_gather` block (at most 16 lights). It is outside this plan.
- Compute, SSBOs, shared memory and SSBO atomics run on Vulkan, Metal and
  OpenGL (`cull_compact.comp`, `id_scan_*.comp`, the content wide-line
  pre-pass). The rendergraph pre-execute callback (`Rendergraph::
  set_pre_execute_callback`, `src/editor/editor.cpp`) runs once per frame on
  the render thread, outside any render pass, before the first node
  executes; `Cull_frame_registry::classify_frame` runs there.
- Free buffer binding points in the shared scene layout: 10-12 (Metal maps
  a GLSL binding to the buffer index; 13 is the acceleration structure, 14
  the texture argument buffer, 15 push constants, 16+ vertex buffers).

## Design

### C1. What is shaded

In clustered mode every active light is shaded, up to `max_light_count`
(raised to 1024; the light array becomes a storage block, C3). The
preset's shadow-mapped counts stay what they are: they size the shadow maps
and hand out the shadow slots in scene order, as now. The preset's
unshadowed counts apply to the loop path only (C8); in clustered mode
`Light_set` gives every active light that did not get a shadow slot an
unshadowed slot. Slot order stays type-major, shadow-mapped first within a
type, so a slot range per (type, shadowed) category exists in both modes.

The mode is a graphics preset field, `light_shading` (`loops` |
`clustered`), so `set_graphics_preset` switches it in a session and the
gates (V1-V6) can compare both on one scene.

### C2. The cluster grid

A grid belongs to one cluster camera, and every pass of the view that
camera covers reads it. The cluster camera is the view's camera for a
viewport or a scene image capture, and the headset's root camera for both
eyes (the combined-eye frustum the headset's shadow fit already uses,
`Headset_view::update_root_camera_projection`). The grid is a frustum grid
of that camera:

- X and Y: `ceil(width / cluster_tile_size)` by
  `ceil(height / cluster_tile_size)` tiles over the cluster camera's
  normalized device xy, `cluster_tile_size` pixels (default 64) of a nominal
  resolution. That resolution is the viewport's size for a desktop view, and
  the eye resolution widened by the ratio of the combined field of view to
  one eye's for the headset.
- Z: `cluster_slice_count` slices (default 24) of the cluster camera's view
  depth, exponential between its near plane and `z_cluster_far`, the
  largest view depth any binned light reaches (its centre's view depth plus
  its range).
- A fragment finds its cluster from its position, not from `gl_FragCoord`:
  `cluster_from_view_relative` (the cluster camera's clip transform relative
  to the pass's `view_origin`, composed in double as the shadow matrices are,
  `doc/erhe/shadows.md` "View-relative positions") times
  `v_view_relative_position` gives xy, and a dot product with the cluster
  camera's forward axis plus an offset gives the depth. One formula serves
  every backend, with no framebuffer y convention, and both eyes of a
  multiview pass read the same grid.
- The boundary clusters are open toward the outside: the outer ring of
  tiles extends laterally past the frustum's sides, the last slice extends
  past `z_cluster_far`, and a fragment outside the cluster frustum clamps
  into the nearest boundary cluster. Binning (C4) tests the boundary
  clusters with those open sides, so the lists stay conservative for every
  fragment. That covers the parts of each eye's frustum the combined
  frustum leaves out (the eyes sit about half the IPD to either side of its
  apex). No binned light reaches past `z_cluster_far`, so the last slice's
  list contributes zero there.

Bistro's viewport at 1334x1120 is 21 x 18 x 24 = 9072 clusters.

### C3. Light buffers

- `light_block` (binding 1) keeps the header and stays a uniform block. Its
  `lights[]` array moves out.
- `light_array_block` (binding 9, storage, std430, readonly): the shaded
  lights in slot order, `Light` (48-byte shading records since the phase 5
  split; the shadow-mapped lights' records are bindings 11 and 12). Written once per view
  per frame into a `CPU_write` ring range, at the actual light count (no
  padding to `max_light_count`), and bound by every pass of that view, the
  binning dispatch (C4) included. `Light_buffer::update()` returns the
  header range and the array range; passes that today call `update()` per
  pass bind the view's ranges instead.
- The grid parameters (`Light_cluster_grid`: `cluster_clip_from_cluster_relative`,
  the cluster camera's view origin and depth axis, the slice scale / bias /
  near, the tile and slice counts) are the `cluster_*` fields of the
  `light_block` header, written per pass from the view's grid.
- `light_cluster_block` (binding 10, storage, std430, readonly,
  `light_cluster.data[]`, layout in `light_cluster.hpp`): three uints per
  cluster - the offset of its list in `data[]`, the spot shadow-mapped |
  spot unshadowed counts, the point shadow-mapped | point unshadowed counts
  (16 bits each) - followed by the lists, two 16-bit light slots per uint,
  ascending, at most `cluster_capacity` (default 64) per cluster. The CPU
  binner packs the lists compactly; the GPU binner writes them at fixed
  per-cluster offsets, which the same header format expresses.
- Directional lights and lights with `range <= 0` are not binned: they
  reach every fragment. They keep their slots and are looped by the header's
  runtime counts (directional) or listed in every cluster (unbounded spot /
  point, so the category ranges stay contiguous in slot order).
- Every pass that draws `standard.frag` binds a cluster block. A pass whose
  view was not binned (material, brush and scene previews, the BRDF slice,
  null-light passes) binds the single-cluster grid: 1 x 1 x 1, listing every
  shaded slot. Its cost is the loop path's; it keeps the bind group complete
  and the shader single-path.

Storage per view and frame in flight at fixed offsets (GPU binning): 12 header
bytes per cluster plus `cluster_capacity / 2` uints - 9072 x (12 + 128)
bytes = 1.3 MB for Bistro's viewport. The cluster buffer is a `GPU_access` ring range acquired
by the render phase per view and frame, the precedent being the GPU cull
masks (`draw_list_scene_gpu_cull.cpp`); frame retirement reclaims it.

### C4. Binning

Binning runs on the render thread in the rendergraph pre-execute callback,
once per binned view per frame, after the shadow fit (the light set and
projections of the frame are final) and before the first color pass:

- The CPU builds the grid and the fp32 inputs of the tests
  (`Light_cluster_binner::prepare()`): per slice its near and far depth and
  the bounds widening, per tile corner the view-space ray, and per binned
  slot the light's range sphere in the cluster camera's node space (centre,
  radius `range` times `1 + 2^-10` so a fragment that rounds across a
  cluster boundary still finds every light that reaches it). It computes
  the light centres anyway, to find `z_cluster_far` before the grid exists,
  so they are uploaded with the other inputs (16 bytes per light) instead of
  being recomputed by a separate dispatch: one dispatch per view.
- One compute invocation per cluster (`res/shaders/light_cluster_bin.comp`).
  The invocation computes its cluster's view-space bounds from the corner
  rays and the slice depths (open on the boundary sides, C2), then walks
  the spot and point slots in ascending order, tests each light's sphere
  against the bounds, and appends passing slots. The walk in slot order
  makes each list ascending and the category counts a running tally, with
  no atomics and no sort. The tests are the CPU binner's fp32 operations
  with every intermediate `precise`, so both binners produce the same lists
  bit for bit (up to denormal flushing, which matters only for radii around
  1e-19). The fixed-offset lists cost `cluster_count * (3 + ceil(capacity /
  2)) * 4` bytes per view per frame in flight: 1.3 MB for Bistro's
  viewport, about 7 MB at 3840 x 2160.
- Each cluster holds at most `cluster_capacity` lights (a renderer config
  value, default 64); the limit is fixed (user decision). A cluster whose
  list would exceed it stores the first `cluster_capacity` slots and counts
  as overflowing: the binning pass accumulates the overflowing clusters and
  the longest list asked for into a per-frame counter (shared-memory
  reduction per workgroup, one atomic per workgroup), copied into the
  view's persistent host-readable readback buffer (one slot per frame in
  flight, outside the ring buffers) and read when the view's next frame in
  that slot starts binning (the frame has retired then). A count that changes logs a warning naming the
  view, the count and the largest list length the grid asked for (once each
  time the count changes); MCP `get_light_cluster_stats` reports the same
  values (V6). The cluster header carries no per-cluster overflow mark: the
  fragment lookup reads the cut list as any other.
- A spot light that passes the sphere test of a closed cluster is also
  tested by its widened cone against the bounding sphere of the cluster's
  bounds (phase 5; `doc/erhe/scene_renderer.md` `Light_cluster_binner`),
  with 16 more bytes of input per binned light.

A `Light_cluster_binner` CPU implementation of the same tests over the same
inputs is the reference: the unit tests run it, and MCP
`get_light_cluster_check` compares the GPU lists of the retired frame with
it cluster by cluster (V2), as `get_cull_classification_check` does for the
GPU cull masks. The editor setting `light_cluster_binning` (`gpu` default,
`cpu`; Settings window, MCP `set_light_cluster_binning`) selects the binner;
in `gpu` mode the CPU binner's tests run only for the check. Every backend
with compute storage blocks bins on the GPU; a device whose binning
pipeline does not build bins on the CPU.

Binning reruns every frame; the inputs change with every camera move and
every light edit, and the dispatch is a few microseconds against a pass of
milliseconds. A skip on unchanged inputs (the camera's view and projection,
the light set generation and snapshots, the grid) uses the shadow map
cache's key pattern (`doc/erhe/shadows.md` "Shadow map cache") and is a
phase 5 item, taken only if V5 shows the dispatch in the frame.

### C5. The fragment loops

`standard.frag` in clustered mode:

1. Directional: two loops with the header's runtime bounds,
   `directional_shadow_count` and `directional_light_count -
   directional_shadow_count`, bodies as today.
2. Find the cluster from `v_view_relative_position` through
   `cluster_from_view_relative` and the cluster camera's depth axis,
   clamped into the grid (C2).
3. Four loops over the cluster's list, bounded by its four category counts,
   with today's bodies for spot shadow-mapped, spot unshadowed, point
   shadow-mapped and point unshadowed. The slot comes from the packed list;
   everything else is unchanged.

The six `ERHE_LIGHT_COUNT_*` axes are not part of a clustered key: the
loops are runtime-bounded, so one variant set serves every light layout and
a light edit compiles nothing. `LIGHT_SHADING_CLUSTERED` (a scene-level
bool axis set by the render environment like the light counts are now)
selects the path while both exist.

Because the lists are ascending and the per-category order is the loop
path's slot order, a fragment's sum runs over the same lights in the same
order as the loop path, minus lights whose range attenuation is zero there,
which add nothing in either path. With every light fitting the preset's
limits the two paths are expected to produce identical images (V1).

### C6. Other light consumers

- Shadow caster passes (`Shadow_renderer`): read `lights[light_control_
  block.light_index]` from `light_array_block`; no clusters.
- `erhe_ray_hit.glsl` consumers (ray trace, DDGI, radiance cascades): read
  `light_array_block` with the header's runtime counts, all shaded lights,
  no clusters (they hit arbitrary world points). Their layouts
  (`ddgi_renderer.cpp`, `radiance_cascades_renderer.cpp`,
  `ray_trace_renderer.cpp`) gain binding 9. In clustered mode they shade
  every light, so their per-ray loop is longer by the lights the preset
  used to leave out; the shadow ray stays gated on nonzero attenuation. V5
  measures the trace passes on Bistro in both modes.
- Lightmap baker: unchanged (own gather block).
- Texel renderer (`shadow_debug.vert`): reads slot 0 from
  `light_array_block`.
- `Scene_preview` (material, brush, thumbnails): single-cluster grid (C3);
  its `get_light_count_limits()` stays.
- Example (`src/example`): single-cluster grid; hextiles and hello_swap read
  no lights.

### C7. XR

- One grid for both eyes, on the headset's root camera (C2), binned once
  per frame. The multiview pass and the per-eye fallback passes bind the
  same cluster range; neither indexes it by `c_view_index`.
- `cluster_from_view_relative` is relative to the pass's `view_origin`,
  which the camera block sets to the first view's position for every view
  of a multiview pass, so both eyes transform their positions with the same
  matrix. The per-eye fallback writes each eye's own `view_origin`, so it
  composes the matrix per eye pass from the same cluster camera.
- The headset's shadow fit stays cyclopean (`doc/erhe/multiview.md`
  "Shadow / light buffer"); the shadow fit and the cluster grid share the
  root camera.

### C8. The loop path

The loop path stays (user decision): it is the identity reference of V1
and the baseline of the performance comparison V5, selected per session by
`light_shading` = `loops`. Its six light-count axes and the preset's
unshadowed counts stay with it.

## Phases

Each phase is one commit (or a short series), reviewed, with its gates.
Phases 1 (the light array as a storage block, C3), 2 and 3 are built, and
of phase 5 the spot cone test and the light record split; phase 4 (XR) is
not built.

Phase 2 (CPU binning and the clustered shader path) is built; the code is
described in [../erhe/scene_renderer.md](../erhe/scene_renderer.md)
(`Light_buffer`, `Light_set`, `Light_cluster_binner`) and
[../erhe/shader_variants.md](../erhe/shader_variants.md) "Light shading".
Gates passed on the phase 2 commit:

- V1: the default scene (two cameras; inline, dedicated and pipelined
  render thread) on Metal and MoltenVK (validation clean), and Bistro with
  all 43 lights within the loop limits, byte-identical between loop and
  clustered mode; loop mode byte-identical to the phase 1 commit.
- V3: covered by the Bistro V1 run (loop mode evaluates every light at every
  fragment); the debug setting forcing the single-cluster grid is not built.
- V4: in clustered mode adding, retyping and removing spot, point and
  directional lights logs no `Shader_variant_cache miss` (headless Metal);
  the same edits in loop mode compile new variants.
- V6 (CPU half): `light_cluster_capacity` 1 changes the image and logs the
  overflow warning.
- `erhe_scene_renderer_tests` `Light_cluster_test`: every light reaching a
  sampled position (inside the cluster frustum, beside it, behind the camera
  and past the far end; perspective with Vulkan and OpenGL conventions, an
  asymmetric XR-style perspective, orthographic) is in the list of the
  cluster an fp32 copy of `erhe_light_cluster_index()` picks; the lists are
  tight (no list holds every binned light; a far cluster does not list a
  small light); lists ascending; category counts; directional lights never
  binned; unbounded spot lights in every cluster; the capacity cut keeps the
  lowest slots; the shaded total stops at the light array capacity; the
  single-cluster layout.

Phase 2 measurements (V5 recipe: Release headless Metal, M4 Pro,
`force_disable_vsync` and `frame_pacing_tier` off, Bistro at the
`scripts/bistro_frame_bench.py` camera, configurations switched with
`set_graphics_preset` in one session, medians of `get_gpu_timers` and of
the phase log's R):
`Viewport window` GPU 3.50-3.53 ms loops Medium (16 shaded), 4.26 ms loops
with all 43, 3.66 ms clustered (43); render thread CPU 1.44-1.60 / 1.83 /
2.50 ms - the CPU binner costs about 1 ms per frame (it tests every tile of
every slice a light's depth range spans), which phase 3 removes.

Phase 3 (GPU binning) is built; the code is described in
[../erhe/scene_renderer.md](../erhe/scene_renderer.md)
(`Light_cluster_gpu_binner`, `Light_cluster_view_state`,
`Light_cluster_binner`). Gates passed on the phase 3 change (headless Metal
and windowed MoltenVK, Debug, M4 Pro):

- V2: `get_light_cluster_check` 0 mismatches on the default scene with 16
  added spot and point lights (Camera A at four poses) and on Bistro (two
  camera poses, both viewports of the session), on Metal and MoltenVK; the
  overflow counters agree with the CPU's at the default capacity and at
  capacity 8.
- V1 with GPU lists: the default scene (gate cameras a and b) and Bistro
  with all 43 lights within the loop limits byte-identical between loop
  and clustered mode, on Metal and MoltenVK (validation clean). With the
  16 added lights camera a was identical and camera b differed in 196
  pixels: `anisotropic_brdf()` took `fwidth()` of the per-light half vector
  inside the light loops, where a quad whose pixels read different cluster
  lists differences different lights. The BRDF now takes its kernel from
  derivatives formed once per fragment
  ([../erhe/scene_renderer.md](../erhe/scene_renderer.md) "Specular
  anti-aliasing"), and with 16 added spot and point lights both cameras are
  byte-identical between loop and clustered mode in all three specular
  anti-aliasing modes, on Metal and MoltenVK. The CPU and GPU binners'
  images are identical in every case.
- V6: Bistro has no overflowing cluster at capacity 64 (longest list 22 at
  the bench camera, 27 at a street camera); at capacity 8 the stats report
  1180 / 7016 overflowing clusters and the warning logs once per change.
- `erhe_scene_renderer_gpu_tests` `Light_cluster_gpu_test`: GPU lists equal
  the CPU lists cluster by cluster on a synthetic set of 80 spot and point
  lights, at the default capacity and at capacity 3 (overflow counters
  equal), and with point lights whose radius sweeps +/- 4 ulps around the
  distance to a cluster's bounds (the sweeps straddle the decision; a
  shader that fuses the distance's multiply-adds fails this test).

Phase 3 measurements (the V5 recipe of phase 2, Release headless Metal, one
session): `Viewport window` GPU 3.48-3.49 ms loops Medium, 4.23 ms loops with
all 43, 3.64 ms clustered (43) with either binner; the binning dispatch
13-14 us for Bistro's viewport (9072 clusters, 43 lights) and 3 us for an
empty 378-cluster view; render thread CPU (R) 1.57-1.76 ms loops Medium,
1.97 ms loops with all 43, 1.93-1.95 ms clustered with GPU binning, 2.17 ms
clustered with CPU binning (whose tests now run over precomputed cluster
bounds; phase 2's binner measured 2.50 ms). R in clustered mode with GPU
binning equals loop mode with the same 43 lights shaded.

2. **CPU binning and the clustered shader path.** Grid (C2), cluster block
   (C3), the light array written once per view per frame and bound by that
   view's passes, `max_light_count` 1024, `Light_cluster_binner` filling a
   `CPU_write` range per view in the pre-execute callback, `LIGHT_SHADING_CLUSTERED` path in `standard.frag`
   (C5), `light_shading` preset field, single-cluster grid for unbinned
   passes, `Light_set` hand-out for clustered mode (C1). Unit tests of the
   binner (sphere vs AABB on boundaries, open boundary clusters, fragments
   outside the cluster frustum, unbounded lights, overflow). Gate: V1, V3,
   V4.
3. **GPU binning.** The two dispatches of C4 into a `GPU_access` range,
   overflow counter and readback, MCP `get_light_cluster_check` and
   `get_light_cluster_stats`. The CPU binner remains the test reference.
   Gate: V2 (0 mismatches), V1 again with GPU lists, V5, V6.
4. **XR.** The headset's grid on the root camera, bound by the multiview
   and the per-eye passes (C7). Gate: V3 on a multiview headless test (two
   views with the eye offsets and field of view of a Quest 3 into a layered
   target, each eye's image identical to the single-cluster grid's) and a
   Quest run (needs the user with the headset).
5. **Refinements, each only when V5 shows it pays:** spot cone (built,
   see "Phase 5 spot cone" below); skip binning on unchanged inputs (C4); shared-memory light tiles in the
   cluster loop for scenes with hundreds of lights; splitting `Light` into a
   shading record and a shadow record indexed by shadow slot (built, see
   "Phase 5 light record split" below).

Phase 5 spot cone (built): a spot light that passes the sphere test of a
closed cluster is tested by its cone against the sphere around the
cluster's bounds (`doc/erhe/scene_renderer.md` `Light_cluster_binner`
holds the test and its fp32 margins). Gates (Debug, M4 Pro, Metal and
MoltenVK):

- V2: `get_light_cluster_check` 0 mismatches on the default scene with 16
  added spot and point lights (four Camera A poses) and on Bistro (bench
  and street camera).
- V1: loop and clustered mode byte-identical on the default scene with the
  16 added lights (gate cameras a and b; CPU and GPU binning) and on Bistro
  with all 43 lights within the loop limits; validation clean.
- `Light_cluster_test` `spot_cone_lists_are_conservative` (outer angles
  0.5 to 359 degrees, positions just inside the outer angle from 1e-4 to
  0.999 of the range) and `spot_cone_culls_clusters_behind_and_beside`;
  `Light_cluster_gpu_test` `gpu_spot_cones_equal_cpu` (the same angle range
  plus outer-angle sweeps in 2^-20 relative steps that straddle the fp32
  tangency decision of target clusters).

Measurements, Bistro at the bench camera, Release Metal, fullscreen
3840 x 1440 at 120 Hz (viewport 2224 x 1360, 18480 clusters), Medium
shadow counts with all 43 lights in clustered mode, `Viewport window`
fragment phase from Metal System Trace (medians over about 300 frames):

| | listed entries | longest list | fragment ms | vertex ms |
|---|---|---|---|---|
| sphere only | 113561 | 20 | 4.67-4.75 | 2.00-2.04 |
| sphere and cone | 109538 | 20 | 4.32 | 2.00 |
| loops Medium (16 shaded), same sessions | - | - | 4.10-4.18 | 2.00 |
| clustered, shadows off: before / after | - | - | 3.81 / 3.63 | 2.0 |

At the windowed 1334 x 1120 viewport the listed entries fall from 57449 to
55733 at the bench camera and from 91342 to 58220 at the street camera
(longest lists 22 and 27, unchanged). Clustered mode with all 43 lights now
costs 0.15 ms of fragment time over loop mode with 16 (was 0.57 ms). The
3.5 % fewer list entries remove 7 % of the fragment time; which of the
removed entries carry that cost (shadow-mapped spot lights, overdrawn
foliage fragments) is not measured.

Phase 5 light record split (built): the 416-byte `Light` is split along
what the fragment light loops read. Every shaded light has a 48-byte
shading record (`light_array`, binding 9, by slot), which the unshadowed
loops and the ray-hit loops read; every shadow-mapped light has a 208-byte
hot record (`light_shadowed_array`, binding 11) holding the shading fields
and everything its shadow lookup reads, so its loop iteration reads one
contiguous record with no dependent load, and a 128-byte cold record of
world-space shadow transforms for the debug views (`light_shadow_world_array`,
binding 12). The shadow record index is the slot less the unshadowed slots
before it, computed without memory access. Layout in
[../erhe/scene_renderer.md](../erhe/scene_renderer.md) "Light records";
`Light_projections::apply()` composes the view-relative matrices and the
bias limits for shadow-mapped slots only. Gates (M4 Pro, Debug): the
default scene with the 16 added lights (loops and clustered, cameras a and
b, shader debug 28 and 30 for a directional, a spot and a point light, the
distance technique and hard filtering) byte-identical to the previous
commit on Metal and MoltenVK (validation clean); the ray trace readback and
the radiance cascades render byte-identical, DDGI within its
nondeterminism; ctest 1785 / 1788 passed (Metal / MoltenVK);
`get_light_cluster_check` 0 mismatches. Bistro's `render_scene_image` takes
one of two images run to run at the previous commit as well (98 pixels
apart, same binary), so it is compared by its set of outcomes; the split
produces one of them.

Measurements (Bistro bench camera, Release Metal, M4 Pro), fullscreen
`Viewport window` fragment phase from Metal System Trace (medians over
190-410 frames). Back-to-back sessions of one binary drifted by up to 6 %,
so the layouts were measured interleaved (previous commit, a first split
with one shadow record per shadow-mapped light reached through an index in
the shading record, this split; two rounds):

| | previous commit | indexed shadow record | hot / cold records |
|---|---|---|---|
| loops Medium (16 shaded) | 4.15-4.21 | 3.98-3.99 | 3.97-4.03 |
| clustered (43) | 4.25-4.30 | 4.22 | 4.18-4.22 |
| clustered, shadows off | 3.58-3.62 | 3.56-3.57 | 3.52-3.55 |

The vertex phase stays 2.0 ms. Render thread CPU (R, headless, vsync and
pacing off, two sessions each) loops Medium 1.58-1.80 -> 1.46-1.57 ms,
clustered 1.86-1.97 -> 1.65-1.70 ms: the skipped caster vertex rounding and
view-relative composition of the unshadowed lights. DDGI, radiance cascades
and the ray trace do no GPU work on Bistro in the bench session (zero
timers; the ray trace reports no instances), so the ray-hit loops are not
measured there.

## Verification

- **V1. Image identity.** With every scene light within the preset's limits
  (Medium on the default scene and creation scenes; a preset with limits
  covering Bistro's 43 lights), `render_scene_image` in clustered mode is
  byte-identical to loop mode, on Vulkan (MoltenVK), Metal and OpenGL, in
  all render-thread configurations (`memory-bank/local/metal_gates/gate.py`
  and the render-thread gate scripts). A difference is investigated to its
  cause (a light missing from a list, or a compiler reassociating the
  runtime loop); it is not accepted as noise.
- **V2. List identity.** GPU lists equal the CPU reference for every cluster
  of every binned view, the headset grid included.
- **V3. Conservativeness.** On Bistro with every light shaded, clustered
  mode with the real grid is byte-identical to clustered mode with the
  single-cluster grid forced for the viewport (a debug setting): every light
  the grid leaves out of a cluster contributes zero to every fragment in it.
- **V4. Variants.** Adding, removing and retyping lights in clustered mode
  compiles no new `standard` variant (shader cache counts over MCP).
- **V5. Cost.** Bistro, headless Metal, `frame_pacing_tier` off,
  `force_disable_vsync`, the bench camera of `scripts/bistro_frame_bench.py`
  (`bistro_viewport_gpu.md` "Measurements"): `Viewport window` GPU time in
  loop mode (Medium, 16 shaded) and clustered mode (Medium shadow counts, all
  43 shaded), plus the binning dispatches and the DDGI / RC / ray trace pass
  times in both modes. The target is clustered with 43 lights at or below
  loop mode with 16.
- **V6. Overflow.** Bistro reports zero overflowing clusters at the default
  capacity; a test scene with more overlapping lights than the capacity
  reports them and logs once.
- Vulkan validation clean on every phase that touches binds or barriers
  (`doc/erhe/gpu_coding_rules.md`).

## Open

1. Shadow slots remain by scene order (`bistro_viewport_gpu.md` option 1 is
   not pursued), so on Bistro the far shadow-mapped point lights keep their
   cube faces while near lights shade unshadowed; clustered mode makes the
   near lights lit, not shadowed.
2. Phase 4 (XR): the headset's cluster grid and its multiview headless
   gate; the Quest run needs the user with the headset.
3. Phase 5: skip binning on unchanged inputs (C4), and shared-memory light
   tiles for scenes with hundreds of lights, each only when V5 shows it
   pays.
4. V3's debug setting forcing the single-cluster grid for the viewport.
5. V1 on OpenGL: the gates ran on Metal and MoltenVK.
