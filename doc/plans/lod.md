# Mesh level of detail

Status: in progress

Adds mesh level of detail (LOD) to erhe, in small steps that each ship a
usable, verifiable piece. The first steps are an editor feature: the user
generates LOD chains for the selection, inspects them and forces a level.
Runtime selection and its control properties come after that. Later steps are
chosen from the user's feedback on the earlier ones.

Builds on `doc/erhe/meshoptimizer_integration.md` (the optimized variant on
`Primitive`, the deferred finalize, the filesystem cache),
`doc/erhe/draw_list_renderer.md` (entries, command templates) and
`doc/plans/culling.md` (CPU lanes, GPU command compaction).

## 1. Reference engines: O3DE and Godot

Summarized from the O3DE (Atom) and Godot 4 source trees, 2026-10-31.

| Topic | O3DE | Godot 4 |
|---|---|---|
| Generation | Build time, scene pipeline (`ModelAssetBuilderComponent.cpp` `GenerateNextLod`). Chained from the previous LOD, 50% each, up to 5 LODs. `meshopt_simplify` (positions only) when "preserve topology" is set, `meshopt_simplifySloppy` otherwise. Hand-authored LODs are supported by node selection or name convention | Import time (`ImporterMesh::generate_lods`). Chained from the previous result, half the triangles each step. `meshopt_simplifyWithAttributes` (normals weight 1, colors when present), flags `SPARSE`, `LOCK_BORDER`, `PRUNE` (`REGULARIZE` when deformable). Stops when a step keeps >= 75% of the triangles or at <= 24 indices |
| Seams | Texture seams are kept by meshoptimizer itself (`meshopt_simplify` keeps seams, see section 2.2). Sloppy mode does not keep them | Texture seams are kept as well. Before simplifying, vertices at the same position are welded only when their UV, UV2 and color match, the tangent sign matches and the normals are within `normal_merge_angle` (20 degrees). The weld removes splits that come only from slightly different normals and never merges across a UV seam (`importer_mesh.cpp` 643-720). A remap carries the result back to the real vertices |
| Error | Relative meshoptimizer error, used only to cap a level | Absolute object-space error per level: `max(prev * 1.5, step_error) * meshopt_simplifyScale`. Error is forced to grow at every level |
| Storage | Per-LOD buffers. Generated LODs reuse the vertex data as a copy, with no compaction | Index buffers only, all sharing the surface's vertex buffer, plus one error value per level |
| Selection metric | Screen fraction `cot(fov_y / 2) * R / distance` (ortho: no distance), compared with precomputed per-level ranges from one decay factor (0.5). A minimum coverage culls tiny objects | `error * max_axis_scale * lod_bias / (distance * 2 tan(fov_x / 2))` against `threshold_pixels / viewport_width` (default 1 px). Distance is to the closest point of the world AABB, 0 inside it. The coarsest level that passes is chosen |
| Where it runs | CPU, per view, in the culling jobs after frustum and occlusion culling | CPU, per surface, per instance and per view while render lists are built |
| Shadows | Each shadow view uses its own projection | Shadow passes use the main camera's position and multiplier, so casters match what the viewer sees |
| Per-object control | `Lod Type` Default / Screen Coverage / Specific LOD, `Lod Override`, `Minimum Screen Coverage`, `Quality Decay Rate` | `lod_bias` per instance. Visibility ranges (begin / end / margins / fade, `visibility_parent`) are a separate manual LOD / HLOD system |
| Global control | Quality bias through model tags | `threshold_pixels`, per-viewport override, "Disable Mesh LOD" debug toggle |
| Transitions | Pop. Overlapping ranges exist for a cross-fade that was never built | Pop for mesh LOD. Visibility ranges alpha-fade in the transparent pass, with sorting artifacts |
| Debug | LOD selection radius drawing, per-LOD vertex / triangle counts in the Mesh component | Primitive counts in frame stats, verbose import log. Neither engine has a color-by-level view |

Taken into erhe's design:

- Godot's storage (index-only levels sharing one vertex buffer, one absolute
  error per level). erhe's optimized variant is already the welded,
  fill-only vertex buffer this needs.
- Godot's generation: `meshopt_simplifyWithAttributes`, a chain built from the
  previous level, `LOCK_BORDER`, error forced to grow (`* 1.5`), and the early
  stop at 75%. The normal-only weld with a merge angle is included as well:
  it lets collapses through splits that exist only because of normals, and
  it keeps every UV and color seam.
- Godot's screen-space error metric with the closest-point-on-AABB distance
  and the instance's max axis scale. It needs no per-mesh tuning, where
  O3DE's coverage ranges need a decay factor per mesh.
- O3DE's per-object control set (mode automatic / forced / off, override
  level), plus Godot's bias.
- Both engines pop. erhe pops too, and the transition choice waits for
  feedback (D4).
- New in erhe: a color-by-level debug view (neither engine has one) and the
  GPU command path, which already writes per-view commands per entry.

Left out until feedback asks for them: hand-authored LOD import, visibility
ranges / HLOD trees, O3DE's minimum-coverage culling, per-LOD materials,
skin-pose-aware simplification, and per-viewport thresholds.

## 2. Design for erhe

### 2.1 Where LOD lives: index ranges in the optimized variant

A LOD chain is a list of extra **index ranges into the optimized variant's
vertex buffer**. Level 0 is the optimized fill range that exists today. Level
`i > 0` is a `meshopt_simplifyWithAttributes` result over the same welded
vertices, stored in the same index allocation as level 0. No level has
vertices of its own.

The reasons for this placement:

- The optimized variant is already welded, fill-only and per `Primitive`, so it
  is exactly the input simplification needs. Shared primitives get one chain,
  as one (meshoptimizer requirement 4).
- Everything that must stay exact keeps using the source data: ID rendering
  and picking (`original` variant), BLAS / raytrace, physics collision,
  export. LOD only changes what the fill passes draw.
- The edit bracket drops the optimized variant when an edit starts
  (`primitive.hpp` optimization hold) and the finalize rebuilds it when the
  edit ends. The chain belongs to that variant, so it is invalidated with it.
  The rebuild regenerates the chain only for primitives that had one: the
  `Lod_settings` it was made with are kept on the `Primitive`. Simplification
  costs far more than the weld and reorder passes, and the re-optimization
  after an edit runs uncached, so it stays on the deferred worker.
- Index-only levels cost index memory only (a 50% level costs half of level
  0's indices). A level never changes `base_vertex`, the vertex format or the
  quantization affine, so switching level changes only
  `(first_index, index_count)` in a draw command.

`Buffer_mesh` holds one `Index_range` per primitive mode today
(`buffer_mesh.hpp`). The levels are a new kind of range, separate from those:
a `Lod_chain` on the optimized `Buffer_mesh` with up to `max_lod_levels` (8)
entries `{Index_range, error}`, level 0 included. A buffer mesh without a
chain has one implicit level, its fill range. Registration reads the chain;
the draw path reads the copies registration made, never the chain.

Each level records its **object-space error** (section 2.2). Runtime selection
converts it to pixels.

Paths that always draw level 0 (they read the variant's fill range, not the
chain): the bucket / `Forward_renderer` / `Shadow_renderer` fallback path
(previews, tool and brush roots, thumbnails, `use_draw_lists = false`,
`SHADER_DEBUG != 0`, the `EDGE_LINES_FROM_ID` passes), the PVS bake, the ID
pass, BLAS and raytrace sources, and the edge-line passes. The step 4 gates
cover both `use_draw_lists` settings.

### 2.2 Generation

`erhe::primitive` gains `generate_lod_chain(positions, attributes, indices,
const Lod_settings&)`, which returns the level indices and errors. It takes
spans, so it does not depend on which path produced the variant, and
meshoptimizer stays private to the target. `Lod_settings` is a `class` with
enum fields where a choice is binary:

- `level_count` (default 6). `ratio` per step (default 0.5): the target
  index count of level `i` is `ratio` times level `i - 1`.
- `Lod_attributes` `positions_only` / `normals_and_colors` (default), with
  the weights (normal 1.0, color 1.0) for `meshopt_simplifyWithAttributes`.
- `Lod_border` `free` / `locked` (`meshopt_SimplifyLockBorder`; default
  `locked`), so meshes split into several primitives do not crack apart at
  the split.
- `Lod_seams` `keep` (default) / `permissive` (`meshopt_SimplifyPermissive`).
- `Lod_fallback` `none` (default) / `sloppy` (`meshopt_simplifySloppy` for a
  level that misses its target).
- `normal_merge_angle` (default 20 degrees): before simplifying, vertices at
  the same position are welded into a simplification-only vertex set when
  all their other attributes match (texcoords, color, tangent sign) and
  their normals lie within the angle. The result is mapped back to the real
  vertices through the inverse remap (Godot's weld). Vertices with
  different texcoords are never welded, so the weld keeps texture seams. It
  removes only normal splits that are almost smooth.

Texture seams are kept by meshoptimizer. It classifies a vertex that shares
its position with another vertex, along exactly two seam edges, as a seam
vertex (`Kind_Seam` in `simplifier.cpp`). Such a vertex collapses only along
the seam, onto a neighbour on the same seam, and both sides of the seam
collapse together, so the seam line stays closed and each side keeps its own
UVs. Vertices on more complex seam junctions are locked. Seams therefore
limit how far a mesh simplifies, never what it looks like along the seam.
Three settings drop this guarantee, and each is an explicit setting that is
off by default: `Lod_fallback::sloppy`, `Lod_seams::permissive`, and a
`normal_merge_angle` large enough to merge real hard edges. The step 1 tests
include a textured box and a UV sphere with a seam, and check that the level 1
seam vertices keep their source texcoords.

Each level is simplified from the previous level, not from level 0. A level's
error is `max(previous_error * 1.5, step_error)` times `meshopt_simplifyScale`,
so error grows strictly from level to level. Generation stops when a step
keeps 75% or more of its input triangles, or when 24 or fewer indices remain.
A mesh that cannot simplify therefore gets a short chain, never duplicate
levels. Skinned primitives simplify with the vertex lock array set on
vertices whose joints differ from a neighbour's, so a collapse cannot blend
influences. Step 1's tests check this rule.

Three paths produce the optimized variant, and each one gets the chain from
its own inputs:

- **Geometry path** (deferred finalize and edit re-optimization): from the
  staged streams, after the weld and reorder passes. `allocate_buffers()`
  runs after the build (`primitive_builder.hpp`), so the allocation is sized
  for all levels in the same transaction.
- **Soup path** (import): `build_buffer_mesh_from_triangle_soup()` sizes its
  allocation from the soup. It is changed to size for the levels too.
- **Prepared / stored path** (`Primitive::make_prepared_meshes`,
  `make_stored_meshes`; `ERHE_mesh_optimized` / `ERHE_gpu_vertex_streams`,
  `doc/plans/gltf_fast_load.md`): no meshoptimizer runs and no CPU soup
  stays. Quest Bistro loads this way. Its levels come from the file (D5).
  Until step 6 writes them there, on-request generation (step 3) reads the
  variant's streams back through
  `Triangle_soup_source::load_stored_vertex_streams()`.

On-request generation runs `generate_lod_chain()` on a worker task, builds a
new optimized `Buffer_mesh` (level 0 plus the levels in one index
allocation), and publishes it through
`Primitive::publish_optimized_render_shape()`. Publication re-registers every
mesh that shares the primitive, and the edit-hold rules apply to it
unchanged.

### 2.3 Runtime selection

Per drawn entry and per view:
`pixels = error * max_axis_scale * projection_scale / distance`. For a
perspective view, `projection_scale` is
`viewport_height / (2 * tan(fov_y / 2))`. For an orthographic view it is
`viewport_height / ortho_height`, with `distance = 1`. `distance` is measured
from the view origin to the closest point of the entry's world AABB (the
bounds the culling lanes already read), and is 0 inside the box, which
selects level 0. The selected level is the coarsest one whose `pixels` is at
most `lod_threshold_pixels`, plus the node's integer bias, clamped to its
min / max level and to the chain.

Skinned entries have no world bounds in the draw lists (`Aabb{}`, never
culled) and draw level 0. Selection from the posed bounds
(`record_skinned_changed_bounds`) is a later item.

The headset's multiview passes select with the union view's centre eye (the
`immediate_cull_view` union in `headset_view.cpp`), so both eyes draw the
same level.

This is camera-dependent derived state. It is computed where each view's
commands are already written per entry: the CPU command writer, and
`cull_compact.comp` on the GPU path. That loop already runs per entry and per
frame, so selection adds work inside an existing loop and no new per-frame
walk.

Both writers get selection, because `culling_classification` defaults to
`gpu` (28dc7349a). On devices with `Device_info::use_draw_indirect_count`
(native Vulkan with `drawIndirectCount`, Metal 3 and 4) `cull_compact.comp`
writes the default path's color and shadow commands. The CPU writer serves
the other devices (MoltenVK, OpenGL without the count draw) and the `cpu`
setting. The CPU / GPU check (`get_cull_classification_check`) keeps the two
equal. The selection arithmetic follows the cull lanes' rule (`precise`, no
contraction) so both writers pick the same level bit for bit.

GPU data layout (step 4b):

- The job record (`cull_compact.comp`, one `uvec4`: view / filter, first
  entry, count, command offset) has no free word. The selection inputs (view
  origin, `projection_scale`, threshold) go in the per-view record of the
  view set that `cull_classify.comp` reads (12 `vec4` today; one more), so
  every job of a view reads them.
- The per-entry templates (`cull_templates.templates[2 * entry]`) keep their
  stride, which bounds, templates and columns share. The levels go in a
  separate per-level template buffer. The entry's index into it goes in a
  free word of the entry's second template `uvec4`, if one is free (checked at
  step 4b); otherwise it gets a column of its own.
- `Draw_list_entry` (static_assert at most 64 bytes) gains a 32-bit
  `lod_table_index`; there is room within the 64 bytes.

`Render_settings` is snapshotted per frame slot, so toggling `lod_enabled`
while frames are in flight takes effect at a frame boundary with no extra
handling.

### 2.4 Shadows

The rule (D3): a caster that is also drawn as a receiver in a color view that
samples the shadow map draws the same level in the shadow pass as in that
color view. Otherwise self-shadowing breaks: the receiver surface and the
caster surface differ, and the shadow test compares one against the other.
Casters that are not drawn in such a color view (outside the camera frustum,
occluded, PVS-rejected) select from the shadow view's own origin and
projection, as O3DE does. Directional cascades are orthographic and pick by
shadow texel size, which matches what the shadow map can resolve.

Consequences for two existing mechanisms:

- The shadow map cache keeps a pass when its light, `clip_from_world`, cull
  view and draw-list content are unchanged (`shadow_renderer.hpp`
  `is_kept()` / `store()`). The viewer camera is in none of these keys. With
  the rule above, the levels the color views selected for the pass's casters
  join the key: a pass is invalidated when one of those levels changes, not on
  every camera move. The hysteresis of section 2.6 keeps level changes rare.
- Shadow passes of several viewports with bitwise-equal planes share one
  `Cull_view` and one GPU job per chunk (`draw_list_scene.cpp`,
  `doc/erhe/draw_list_renderer.md` Q6). Proposed: the passes stay shared while
  every viewport sampling the map selects the same levels for its visible
  casters, and split per viewport for a frame where they differ. Directional
  cascades are fitted per camera already and are not affected.

The shadow pass therefore runs its level selection after the color views'
selection of the same frame, and reads it per entry. On the GPU path that is
an ordering constraint between the color and shadow compaction jobs.

### 2.5 Control properties

An attached value group `Lod` on `erhe::scene::Node`, following
`Draw_mode` and `Node_physics` (`doc/erhe/property_system.md` 4.23, 4.24,
4.26). Values ride the node's `ERHE_node` properties map, so they survive a
glTF round trip.

| Property | Type | Default | Inherits | Meaning |
|---|---|---|---|---|
| `Lod.apply_lod` | bool (group key) | false | no | the node carries its own LOD opinion |
| `Lod.mode` | enum `automatic` / `forced` / `off` | `automatic` | yes | `off` draws level 0, `forced` draws `forced_level` |
| `Lod.forced_level` | int | 0 | yes | used when `mode == forced`, clamped to the chain |
| `Lod.bias` | int | 0 | yes | added to the selected level |
| `Lod.min_level` / `Lod.max_level` | int | 0 / 7 | yes | clamp |

The key does not inherit (4.23); the values do, so setting them on a group
node applies to its subtree. Every value's `property_changed` is
`erhe::scene::node_system_property_changed`, which reaches a `Lod_system`
node system owned per scene (`node_system.hpp`). The system resolves the
effective values of the changed subtree and mirrors them into the draw-list
objects through a new `Scene_host` virtual, because `erhe::scene` cannot see
`Draw_list_scene` (R0a). The draw path never walks the hierarchy.

Global settings go in `Render_settings` (graphics preset): `lod_enabled`,
`lod_threshold_pixels` (default 1.0), `lod_bias` (int) and the hysteresis band
(section 2.6). Generation defaults go in an editor config section
(`Lod_settings`), used by the editor's LOD window. Import never generates
levels (D1).

### 2.6 Transitions

Levels pop (D4), with hysteresis: a view moves an entry to a coarser level only
when the coarser level's `pixels` is below the threshold by a band, and back
to a finer level only when the current level's `pixels` exceeds the threshold.
This needs the previously selected level per view and per entry, kept beside
the culling state in both command writers and reset when the view's key
(`Gpu_command_key`) changes, as the occlusion bits are. The state is written
in the loop that already runs per entry, so no new per-frame walk is added.

### 2.7 Edge lines

The edge-line passes draw per-level edge lines built from the simplified
triangles (D2), so a mesh drawn at level `i` shows the edges of level `i`.
Because edge-line vertices are per edge endpoint (with the adjacent faces'
normals and winding), each `Lod_level` owns edge-line vertex (and joint)
ranges rather than line indices. Built and fed by
`doc/plans/selection_outline_lod.md`; the generation and storage are in
`doc/erhe/meshoptimizer_integration.md` "Level of detail".

## 3. Decisions (user, 2026-10-31)

- D1: LODs are generated only on request from the editor; import never
  generates them.
- D2: per-level edge lines built from the simplified triangles, every edge
  of the level's triangles (section 2.7; `doc/plans/selection_outline_lod.md`).
- D3: a caster drawn as a receiver selects the same level in the shadow pass
  as in the color view; other casters select from the shadow view (section
  2.4; step 4c).
- D4: pop with hysteresis (section 2.6; step 5).
- D5: levels stored in the prepared glTF (`ERHE_gpu_vertex_streams` /
  `ERHE_mesh_optimized`), written by the exporter, no work at load (step 6).

## 4. Build order

Each step is one commit (or a small series) with docs in the same commit, and
it is verified before the next step starts. When a step lands, the
present-tense description moves into `doc/erhe/meshoptimizer_integration.md`
(generation, chain storage), `doc/erhe/draw_list_renderer.md` (entries,
templates, selection) and a new editor document `lod.md` under
doc/editor (window, properties).

### Step 1: LOD chain generation in erhe::primitive

Done; described in `doc/erhe/meshoptimizer_integration.md` "Level of
detail". `Buffer_mesh::index_buffer_range.count` of an optimized mesh with
a chain covers every level; every draw, BLAS and lightmap consumer reads
`triangle_fill_indices`, and only the Properties "Indices" row and the MCP
buffer dump report the whole allocation, which is what they describe.

### Step 2: Lod properties and forced level

Done; described in `doc/erhe/property_system.md` section 4.29 (the `Lod`
group, in `erhe::scene` beside `Layout`, since its system reaches the draw
lists through a `Scene_host` virtual), `doc/erhe/scene.md` "Level of detail
control" (`Lod_system`, `Scene_host::on_mesh_lod_changed`) and
`doc/erhe/draw_list_renderer.md` R1c (the forced level's range copied into
the entries at registration). The MCP `get_lod_info` reports the effective
record and each entry's level; the level is set with `set_item_property`.

Verified on the headless Metal editor (default scene, a group node with a UV
sphere and a torus, `generate_lods` on both, 5 and 6 levels):
`render_scene_image` with no `Lod` value is byte-identical to the build
before the step, in both `culling_classification` settings and with
`use_draw_lists` off; forced levels 0..5 on the torus report entry levels
0..5 (1792, 896, 448, 224, 112, 56 triangles, color and shadow), level 0 is
byte-identical and every other level differs, and level 6 clamps to 5; cpu
and gpu classification give identical images per level; with
`use_draw_lists` off level 0 is drawn; values on the group reach both meshes
(local values on a mesh win) and clearing `Lod.apply_lod` restores the step-1
image; a level forced before `generate_lods` shows once the chain is
published; undo / redo of a level edit restore the levels and images; a PVS
baked over the meshes stays ready across re-registrations; the values
survive a glTF save and reload, and `scene_roundtrip_verify.py` checks them.

### Step 3: editor LOD window (generation for the selection)

Built; described in `doc/editor/lod.md` (the LOD window, its forced level
slider and the `lod` config section) and
`doc/erhe/meshoptimizer_integration.md` "Level of detail"
(`request_lod_generation()`, the MCP tools `generate_lods` and
`get_lod_info`).

Verified on the headless Metal editor (default scene plus a static torus,
64 x 32 steps): `render_scene_image` renders through the composition passes'
draw-list path (`Scene_image_view::prepare_composition()` ->
`Composition_pass::prepare()`, `use_draw_lists` on, no shader debug), so the
forced level reaches it. "Generate for selection" clicked in the window
(`imgui_click`) builds 6 levels (4096, 2048, 1024, 512, 256, 128 triangles);
level 0 is byte-identical to the image before generation. A slider drag to
level 4 reports level 4 for the color and shadow entries in `get_lod_info`
and a different image, recorded as one compound undo entry; a second drag to
level 2, then undo, restores level 4 and its image; undo of the first drag
restores the image without `Lod`, and redo level 4. "Release" restores the
image without `Lod` (undo restores level 4); "Clear for selection" removes the
chain and restores that image too. Doubling the camera distance (3.75 m to
7.5 m) halves every level's pixel error in the table (level 1 2.676 to 1.338,
level 5 66.039 to 33.019).

Found while verifying step 2a, not yet investigated:
- After an in-place edit (vertex move, recalculate normals) the
  re-optimization welds nothing (torus 8192 -> 8192 vertices, against
  8192 -> 2145 at creation); not checked whether this happens without LOD.
- Meshes with per-facet texture coordinates
  (`generate_facet_texture_coordinates`, e.g. a Catmull-Clark result) get no
  chain with `Lod_seams::keep`: every edge is a seam. `permissive` gives 4
  levels.
- A hold that begins between the finalize snapshot and its commit still lets
  a geometry-path rebuild swap in the base built before the edit (the edit
  re-optimization has the same exposure; the variant publish is refused).

Open: hand the window to the user for the first interactive pass, and record
the feedback in section 6.

### Step 4: automatic selection

- 4a, CPU writer: per-level command templates, the entry's
  `lod_table_index`, selection for color passes, `Render_settings` globals
  plus Settings / graphics-preset UI. Debug view: color by selected level
  through the `FLAT_DRAW_COLOR` variant (dbb087c9e). Stats per pass: entries
  per level and triangles drawn (`get_lod_stats`). Gates: with
  `lod_enabled = false` and with every entry forced to level 0, images are
  byte-identical to HEAD, for both `use_draw_lists` settings. A scripted
  camera dolly switches levels at the distances the formula predicts.
- 4b, GPU writer: the layout of section 2.3 and selection in
  `cull_compact.comp`. `get_cull_classification_check` is extended to the
  selected level per command: 0 mismatches on default, heavy, Bistro and four
  view, in both culling-classification settings.
- 4c, shadows: selection in the shadow jobs and the CPU shadow writer per
  section 2.4 (visible casters reuse the color view's level). Gates: no
  self-shadow difference between level 0 and a forced level on a scene with
  self-shadowing casters; the shadow map cache check
  (`scripts/shadow_map_cache_check.py`) keeps passes while the camera moves
  without changing any selected level.

### Step 5: transitions

- Hysteresis (section 2.6) in both command writers. Gate: a camera dolly that
  stops inside the band does not flip levels from frame to frame.

### Step 6: persistence and import

- The meshoptimizer cache stores remaps, not indices
  (`mesh_optimizer_cache.cpp`), so LOD levels need a new payload section and
  a format version bump. `Lod_settings` goes in the key.
- The prepared glTF (`ERHE_gpu_vertex_streams` / `ERHE_mesh_optimized`)
  carries the levels and their edge-line vertex (and joint) data, or
  regenerates the edge lines at load from the stored level indices (CPU
  only, no simplification); the exporter writes the levels and loading does
  no simplification work.
- Measure on Bistro: Release desktop (GPU-bound viewport) and Quest, where
  Bistro is geometry-bound (memory-bank/topics/quest.md). The Quest sweep in
  `doc/reference/quest_bench_runs.md` measures 10.6 ms of binning in every
  Bistro configuration; binning scales with the submitted triangles, so it
  is the first number LOD should move. Run `scripts/quest_bench.py
  --perfetto` with a configuration from `scripts/quest_bench_configs/` before
  and after, and record both runs (`--record`).
- The overdraw view (`set_overdraw_visualization`, `get_overdraw_stats`;
  `doc/plans/overdraw_visualization.md`) in `shaded` mode shows the
  fragment-side effect: far small triangles at level 0 cost quad overdraw
  that a coarser level removes.

### Later, from feedback

Visibility ranges / HLOD (Godot's visibility ranges: whole-node swaps such as
a card proxy, which `Draw_mode` cards already resemble), skinned selection
from posed bounds, meshlet / cluster LOD, per-material LOD thresholds, and LOD
for edge lines and the ID pass.

## 5. Verification recipe

Headless Vulkan editor (`erhe-headless-verify`): generate LODs on a creation
scene through MCP, `render_scene_image` per forced level, and compare the
triangle counts `get_lod_info` reports with the drawn counts from the pass
stats. Byte-identity gates for the off states as in step 4a. Interactive
checks (window feel, popping visibility) go to the user.

## 6. Feedback log

Empty until step 3 reaches the user.
