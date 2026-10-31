# LOD selection on the GPU, inside the culling classification

Status: proposed

The build order for automatic LOD level selection (`doc/plans/lod.md`
section 2.3, decisions D3 and D4). It replaces that plan's step 4 ordering
(4a CPU writer, 4b GPU writer, 4c shadows) and its step 5 (hysteresis). The
design is accepted by the user (section 9) and not started.
Selection runs in the culling classification, on the CPU lanes and in
`res/shaders/cull_classify.comp`, with the same arithmetic, so both produce
the same level bit for bit; the command writers only read the result.

Builds on the culling classification and GPU command generation of
`doc/erhe/draw_list_renderer.md` Q6 (`doc/plans/culling.md` phases 5 and 6),
the occlusion test's cross-frame view matching
(`doc/plans/occlusion_culling.md` step 3b), the forced level of R1c, and
the per-level edge lines (`doc/erhe/meshoptimizer_integration.md` "Level of detail").

## 1. Where selection runs: the classification

Selection is computed per (entry, LOD column) in the classification, beside
the visibility masks:

- **CPU**: a new lane loop in `cull_lanes.cpp` (`lod_lanes()`), run by
  `Draw_list_scene::classify_cull_views()` inside the existing per-list,
  per-view loop, over the same SoA bounds.
- **GPU**: `cull_classify.comp`, inside the existing per-entry loop over the
  views.

The output is one level region per frame: 4 bits per (LOD column, entry), at
`levels[(column / 8) * entry_count + entry]`, nibble `column % 8` (the mask
layout, eight columns per word). The command writers read it: the CPU writer
(`Draw_indirect_buffer::update(..., Draw_record_layout::chunk_entries)`) and
`cull_compact.comp` pass 1 (section 3, "Compaction").

Reasons:

- The classification already holds every input: the entry's bounds (the
  distance is to the closest point of the world AABB), the per-view records,
  and one invocation per entry that loops over all views of the frame. So
  the shadow rule D3 (section 4) is a local computation in one invocation:
  no cross-entry dependency, no ordering between color and shadow
  compaction jobs, no partner-entry index.
- `get_cull_classification_check` compares whole regions, so the check
  extends from masks to levels (every column, every entry), a stronger gate
  than comparing written commands.
- Hysteresis needs the previous frame's level per (column, entry). A region
  written every frame in a fixed layout is exactly the state the next frame
  reads, matched across frames as the occlusion words are (section 6).

What each side is for. The CPU classification runs every frame in both
`culling_classification` settings (`classify_frame_cull_views()` runs the
CPU lanes, then records the dispatch under `gpu`), and so does `lod_lanes()`.
The CPU levels serve the CPU-written commands (MoltenVK, GL without the
count draw, the `cpu` setting), the check, the edge-line feed and stats
(section 5) and the shadow map cache (section 4), all without a readback.
The GPU levels serve the GPU compaction without a per-frame upload of an
entries x columns region. The CPU cost lands in the render phase (R), which
is 3.4 ms on Quest; step 2a measures it.

Levels do not depend on visibility. A level is written for every entry of
the column's purpose (and, for shadow entries, the color columns, section
4), inside the frustum or not, occluded or not, PVS-culled or not. The
occlusion boxes are the entry AABB and the PVS columns are per entry, so
neither interacts with the level. A level stays stable while visibility
flips, which the shadow map cache and hysteresis both need.

Alternatives considered:

- **In `cull_compact.comp`, per job** (`doc/plans/lod.md` 2.3's first
  placement). It evaluates only the surviving entries, but a shadow job
  then needs the color jobs' result for the same mesh in another entry of
  another list (an ordering constraint between jobs plus a partner index),
  the CPU has no matching value without a second evaluation, and the check
  would compare commands, not levels. Rejected.
- **CPU selection, uploaded per frame.** A per-frame upload sized entries x
  columns. Rejected by AGENTS.md "No update each frame" and by the reason the
  GPU classification exists.

## 2. Arithmetic: multiply and add only, precise

The selection must give the same bits on the CPU and the GPU. The cull lanes'
rule applies (`precise`, no contraction, the same expression order), and the
formula avoids division and square root, which the Vulkan and Metal
precision rules do not make correctly rounded:

```
k     = max_axis_scale * projection_scale       (per entry and column)
d2    = sum over axes of gap_a * gap_a,
        gap_a = max(abs(c_a - o_a) - e_a, 0)    (perspective; 1 for ortho)
level L passes when (error_L * k) * (error_L * k) <= threshold2 * d2
```

`threshold2 = lod_threshold_pixels * lod_threshold_pixels` is computed on the
CPU once per frame and passed as float bits. `projection_scale` is computed
on the CPU per view (`viewport_height / (tan(up) - tan(down))`, the LOD
window's rule in `doc/editor/lod.md`; ortho: `viewport_height /
ortho_height`). Inside the box `d2 = 0` and only level 0 (error 0) passes.
Errors grow strictly along a chain (`doc/plans/lod.md` 2.2), so the passing
levels are a prefix of the chain and the selected level is its last. Then
the integer rules: plus the entry's bias and the global bias, clamped to the
entry's min / max and to the chain.

The gap expression is the one `cull_lanes::sphere_axis_lanes` uses (with the
view origin for the sphere center), so it reuses tested code on both sides.

Subnormals: the GPU may flush a subnormal intermediate to zero where the CPU
keeps it, as for the cull lanes. The products here are far from that range
for real scenes (errors of millimetres and up, `k` in the hundreds and up);
the level check reports any case where it is not.

## 3. Data layout

### Per entry

- `Draw_list_entry` gains `lod_table_index` (32 bits; `0xffffffff` when the
  entry's variant has no chain) and `lod_word` (32 bits: the packed effective
  `Lod` record, mode 2 bits, forced level 3, bias 4 signed, min 3, max 3,
  and the chain's level count 4). The entry is 56 bytes today and 64 after
  this, within its static_assert.
- GPU templates (`cull_templates.templates[2 * entry + k]`, written by
  `publish()` under the per-list serial check) have free words: `[0].w`
  takes `lod_word`, `[1].z` takes `lod_table_index`. The stride, shared
  with bounds and columns, does not change.
- `max_axis_scale` goes in the free `w` of `cull_bounds.bounds[2 * entry +
  0]` and in a seventh array of `Draw_list_bounds`, written by
  `write_object_bounds()` (registration, transform hook), so the bounds
  rewrite that already follows a transform change carries it.
- Change sites, never per frame. Today a `Lod` change that keeps the forced
  level (bias, min / max that do not clamp it) only updates the record map
  (`apply_lod()`). With automatic selection every effective-record change
  rewrites the entries' `lod_word` and moves the list serial: the list copy
  at publish, the list's GPU bounds and template rewrite, and a shadow
  cache content-serial change for that list follow, as for a forced-level
  edit today. These are property edits, so the cost is per edit. A chain
  publication re-registers the sharing meshes (R12), which writes
  `lod_table_index`.

### Level tables

One buffer per `Draw_list_scene` and frame slot, written by `publish()` only
when the set of chains changed (a table serial, like the list serials): per
chain, `level_count` entries of `uvec4 (index_count, first_index,
floatBitsToUint(error), 0)`, level 0 first. `lod_table_index` points at the
chain's first level. A chain is shared by every entry of every mesh that
shares the primitive, so the table has one row per chain, not per entry.
`base_vertex` stays in the template: a level never changes it.

### Level regions

Persistent buffers per frame slot, owned by the `Draw_list_scene` like the
occlusion words (`Occlusion_slot`, `prepare_occlusion_slot()` in
`draw_list_scene_gpu_cull.cpp`), sized to the high-water mark, from step 2b
on. Not device ring ranges: the masks' ring ranges are released after the
dispatch, and the next frame's classification reads this frame's levels
(hysteresis, section 6). The slot records frame index, entry count and
`entry_layout_serial`, and the previous slot is used under the occlusion
rule (the frame right before, same entry layout). The CPU keeps its
previous region per frame in the same way.

### Per view and per frame

- The view record grows from 12 to 14 `vec4`
  (`Gpu_cull_classifier::vec4s_per_view`): `[12] = selection origin.xyz,
  projection_scale`; `[13] = LOD column (exact small float, -1 = none),
  projection kind (0 perspective, 1 orthographic), previous column (-1 =
  none, section 6), shadow mode (section 4)`. The color column mask of a
  shadow view goes in a per-view word table beside the views.
- A frame header `lod_header = active, threshold2, coarsen threshold2
  (hysteresis), global bias`. `active = 0` when `lod_enabled` is false in
  the frame's `Render_settings` snapshot or the scene has no entry with a
  chain (a count kept by registration): classify writes no levels, both
  writers read the templates, and the frame is HEAD's frame.
- `add_cull_view()` takes a `Lod_view` (origin, projection scale, kind, a
  `lod_key`, and for a shadow view the scene views that sample its map). A
  view without one has no LOD column and draws the template range (R1c: the
  forced level, or level 0).
- **View identity.** `add_cull_view()` shares a view today when purpose,
  planes and PVS box are bitwise equal. The `Lod_view`'s origin, projection
  scale, kind and `lod_key` join that identity; the sampling scene views do
  not, they are merged (union) into the shared view. Reason: equal planes
  with a different projection scale are a legitimate case (a
  `render_scene_image` capture or a second viewport at another height from
  the same camera) and select different levels, so a VERIFY would abort on
  valid use. The cost is one more classified view in those cases. Shadow
  views of several viewports for one light still share: their `Lod_view`
  comes from the light and its map, not from the viewport.
- **Columns.** One per color view with a `Lod_view`, one per 2D shadow view,
  one per shadowed point light: `add_point_light_cull_views()` maps its six
  face views to one column (they select from the light origin with the same
  projection). At most `max_lod_columns` (64) per frame, of which at most
  `max_lod_color_columns` (8) color columns. A view past either limit gets
  no column and draws the template range; a warning is logged once.

### Compaction

`cull_compact.comp` finds a job's column in the job word: `job.x` packs the
view in bits 0..15 and the filter index in 16..20 (`max_job_filters` = 32),
and the LOD column takes bits 21..31 (`0x7ff` = none). Its bind group gains
two bindings, the level region and the level table. Pass 1 takes
`(index_count, first_index)` from the table row of the entry's level only
when `lod_header.active` and the entry has a table and the job has a column;
otherwise from the template as today.

Memory: one 4-bit level per (column, entry) over all published entries.
Bistro, one viewport (estimate, measured at step 2b): about 20000 entries x
(1 color + about 20 shadow columns) / 2 bytes, about 210 kB per frame slot.
If measured too large, the region splits per purpose (color entries need
only color columns); not built first.

## 4. Shadows (D3)

A shadow entry's invocation evaluates every color column of the frame (at
most 8): the color selection with the color column's inputs, with
hysteresis, and the color view's frustum test on the entry's bounds. It
writes those levels into its own row of the color columns. Every color
column is evaluated, whether or not a shadow view names it, so the row's
history starts in the same frame as the color entry's (section 6). Then for
each shadow view:

- shadow mode `select`: inside at least one sampling color view's frustum:
  the finest (smallest) of those color levels; otherwise the shadow view's
  own selection (spot: perspective from the light with the map resolution;
  point: the cube face projection, `projection_scale = resolution / 2`;
  directional pass: orthographic, `viewport_height / ortho_height` of the
  pass, which picks by shadow texel size);
- shadow mode `template`: a sampling scene view has no LOD column (a shader
  debug view, a view on the bucket path, a view past the column limit). Its
  receivers draw the template range, so the shadow view draws every caster
  at the template range too, with no selection.

The sampling scene views are named in the shadow view's `Lod_view` (the
shadow node's viewport) and resolved to columns in
`classify_frame_cull_views()`, after every hook added its views, so the hook
order does not matter.

The shadow map cache (`Shadow_map_cache::is_kept()`, read in
`Shadow_renderer::draw()`, after the classification) keeps a pass only when
its column's levels equal those of the same column in the previous frame:
`lod_lanes()` counts, per shadow column, the entries whose level differs
from the previous region's (section 6), and a column with no previous match
counts as changed. A camera move that changes no selected level keeps every
cached pass.

## 5. CPU consumers of the selected level

All read the CPU levels of the frame, which the classification computed in
R; none waits for the GPU.

- **Edge lines and the selection outline** (`doc/editor/lod.md` "Edge lines and the selection outline"). `App_rendering::prepare_wide_line_feed()` (M2) keeps writing one
  record per mesh primitive, now with the edge ranges of every level of the
  chain (level 0 from the original variant, as today) and the position of
  the mesh primitive's color entry in the frame's published slot (new
  `Draw_list_scene::find_published_color_entry(frame_index, mesh,
  primitive_index)`). `Content_wide_line_renderer::add_feed()` (R, in the
  viewport node's execute, after the rendergraph pre-execute
  classification) picks the range of the entry's level in the pass's cull
  view (`Draw_list_scene::get_lod_level(frame_index, cull_view, list,
  entry)`) and VERIFYs that the slot entry belongs to the record's object
  and primitive. A view without a LOD column feeds the forced level, as
  today. The render phase still reads no mesh.
- **`get_lod_info`, the LOD window's "Drawn" column, `get_lod_stats`.** After
  the classification, R copies the levels of a requested mesh set (the LOD
  window's selection, an MCP request) into a `Render_frame_results` slot
  (`doc/editor/render_thread.md`); M2 readers read the last rendered frame's
  copy, one frame late. Per-pass stats (entries per level, triangles drawn):
  the CPU writer counts while writing; on the GPU path the compaction adds
  per-job level counts (8 words per job, a region after the count slots, an
  order-independent shared sum) to the count buffer the retired-frame
  readback already decodes (`get_retired_gpu_color_counts()`), so the GPU
  stats include occlusion like the drawn counts do.

Alternatives considered for the edge feed:

- The CPU re-evaluates the selection in M2 for the few fed meshes. It needs
  the R-side hysteresis state of the previous frame, which in pipelined mode
  (M2 of N runs during R of N-1) is not available, and it relies on M2 and R
  computing bitwise-equal view inputs. Rejected.
- A GPU-driven wide-line dispatch reading the level region. The expansion
  compute takes CPU-written records whose edge counts size its dispatch;
  this needs an indirect dispatch and a GPU-side record table. More work for
  a handful of meshes. Not first.

## 6. Hysteresis (D4)

A column moves an entry to a coarser level only when the coarser level
passes `coarsen threshold2` (`(threshold * band)^2`, band < 1, a
`Render_settings` value), and to a finer level as soon as the current level
fails `threshold2`. The previous level per (column, entry) is the previous
frame's level region (section 3, "Level regions"):

- Columns are matched across frames by `lod_key`, node-free so a rebuilt
  rendergraph keeps it: color, the scene view; 2D shadow, (light id, kind,
  pass index); point light, (light id, kind); a directional pass fitted to
  a camera adds the scene view. The scene view in a key is a raw pointer
  today (step 1); a scene view destroyed and another allocated at the same
  address between two frames would alias, so step 4 keys it by a
  scene-view serial id instead. The view record names the previous frame's
  column for the same key, or none, as the occlusion record names the
  previous frame's view group and bit (`doc/plans/occlusion_culling.md` 3b).
- No previous column, a changed entry layout (`entry_layout_serial`), a
  frame that is not the one right after the previous, or a
  `culling_classification` switch (the GPU has no previous region on its
  first `gpu` frame) start selection fresh, on both sides in the same frame.
- A shadow entry's color-column rows follow the color entry's history by
  construction: the color and shadow entries of a mesh primitive are
  registered together (R3), every color column is evaluated for both every
  frame (section 4), and every restart above applies to both rows at once.
  So both see the same inputs from the same first frame and hold the same
  level, and a shadow view that starts naming a color view later reads a
  row that is already in step.

## 7. Views

- **Viewports, four view, `render_scene_image`**: one color column each, from
  `Composer::add_cull_views()` (origin = camera position, projection scale
  from the render context's camera and viewport).
- **Headset**: one column for the union view the headset node hook adds
  (`doc/plans/culling.md` "Headset passes"), with the centre eye as origin
  and the per-eye projection scale (eye viewport height over the eye's fov
  side tangents), so both eyes and the multiview pass draw the same level.
- **Lightmap bake and PVS bake views**: no `Lod_view`, so they draw the
  template range, the forced level (level 0 without one), not level 0 as
  `doc/plans/lod.md` 2.1 lists for the PVS bake.
- **Bucket path** (previews, tools, thumbnails, brush roots,
  `use_draw_lists = false`, shader debug): reads the variant's fill range,
  level 0; R1c's forced level is in the entries only.
- **ID pass, BLAS, raytrace**: level 0 (`doc/plans/lod.md` 2.1).

## 8. Entries without selection

- Skinned entries (no bounds, `Draw_mobility::skinned`): the template range,
  level 0 or forced; the posed-bounds selection is `doc/plans/lod.md`
  "Later".
- Entries without a chain (`lod_table_index` none) and unbounded entries:
  level 0, written as 0 in the region.
- `Lod.mode` `off`: level 0; `forced`: the forced level clamped to the
  chain, both written into the region, so the commands come from one path.

## 9. Decisions (user, 2026-10-31)

1. A caster counts as drawn as a receiver in a color view when it is inside
   that view's frustum; occluded or PVS-culled casters inside the frustum
   take the color level (section 4).
2. A shadow pass shared by several viewports takes the finest level of the
   sampling viewports. Caveat: in a coarser viewport a receiver drawn at
   level L and its finer caster differ by up to `error_L`, which the
   texel-derived shadow bias does not cover, so shadow acne is possible
   there.
3. `lod_enabled` defaults to off until the dolly and Bistro / Quest
   measurements; then the user decides.

## 10. Build order

Each step is one commit (or a small series) with its docs; the present-tense
description moves into `doc/erhe/draw_list_renderer.md` (R1c, Q6) and
`doc/editor/lod.md` as each lands. Every editor-driven gate runs on the
headless Metal editor (`build_xcode_metal_headless`). Metal 3 and Metal 4
run the GPU path; MoltenVK covers the CPU-command path (no count draw);
KosmicKrisp is the Vulkan GPU path. Common gates, for every step:

- G-off: with `lod_enabled = false`, and with every entry forced to level 0,
  `render_scene_image` is byte-identical to the build before the step, in
  both `culling_classification` settings and both `use_draw_lists` settings.
- G-check: `get_cull_classification_check` reports 0 mismatches (masks, and
  levels from step 2b on) on default, heavy, Bistro and heavy in four view.
- `ctest -L "gpu|editor"` on Metal, MoltenVK and KosmicKrisp.

### Step 1: data plumbing, no behaviour change

State: done; the data is described in `doc/erhe/draw_list_renderer.md` R1c
and the settings in `doc/editor/lod.md` "Graphics preset settings". No view
has a LOD column yet (`[13].x = -1`); columns come with step 2a.

`Draw_list_entry::lod_table_index` / `lod_word`, the `max_axis_scale` bounds
array and GPU word, the level tables, the template words, the `Lod_view`
argument of `add_cull_view()` and its part in view identity (color views of
viewports, scene image, headset; shadow views get theirs in step 5), the
14-`vec4` view record, the `lod_header`, `Render_settings` `lod_enabled`
(default false), `lod_threshold_pixels` (1.0), `lod_bias` (0) and the
Settings / graphics preset UI. Every effective-record change rewrites
`lod_word` (section 3). Gates: G-off, G-check; a bias edit moves the list
serial (MCP `get_draw_lists`); idle frames write no GPU bounds
(`get_gpu_bounds_write_count()` 0).

### Step 2a: color selection on the CPU

`lod_lanes()` with deviceless tests in `test_cull_lanes.cpp` (predicted
switch distances, inside-box level 0, ortho, bias and clamps, chain end);
the CPU level region; the CPU writer reads it; the column limits with their
warning. Under `gpu` frames the GPU path still draws the template range;
`lod_enabled` stays off by default. Gates (`culling_classification = cpu`,
MoltenVK and Metal): G-off; `scripts/lod_dolly_verify.py` (new) dollies the
camera away from a torus with a 6-level chain and checks that the selected
level changes at `d = error_L * k / threshold` within one dolly step, for
every level; cost: `lod_lanes()` time beside `classify_cull_views()` on
Bistro and heavy (Tracy, Release), recorded in the step's commit.

### Step 2b: color selection on the GPU

`cull_classify.comp` writes the level region into the persistent per-slot
buffers; `cull_compact.comp` reads the column from the job word and the
level table (section 3, "Compaction"); `get_cull_classification_check`
compares the level regions. Gates: G-off; G-check with LOD on (levels), on
Metal 3, Metal 4 and KosmicKrisp; the dolly script passes in both settings
and the cpu and gpu images are byte-identical per pose; cost: `cull_classify`
GPU time on heavy and Bistro before and after (GPU timers, Release); the
level region size on Bistro.

### Step 3: CPU consumers

The edge feed resolved in R (section 5), the `Render_frame_results` level
copy for `get_lod_info` and the LOD window, `get_lod_stats` on both paths
(the per-job level counts). Gates: the selection outline of a dollied torus
traces the level the fill draws (the outline image of automatic level L
equals the image of forced level L at the same pose, both settings);
`get_lod_stats` CPU and GPU counts equal with occlusion off; G-off.

### Step 4: hysteresis

The previous-region read on both sides, `lod_key` matching, the restarts of
section 6 and the `Render_settings` band. Before shadows, because the shadow
cache test of step 5 reads the matched previous region. Replaces
`doc/plans/lod.md` step 5. Gates: G-off; G-check with hysteresis on; a dolly
that stops inside the band keeps the level for 300 frames; an entry layout
change (add a mesh) and a `culling_classification` switch restart selection
without a mismatch.

### Step 5: shadows

Shadow `Lod_view`s from `Shadow_renderer::fit()` (spot, point, directional)
with the sampling viewports, the color-column evaluation in the shadow
entries' invocations and lanes, the `template` shadow mode, the shadow
cache's level-change test. Gates: G-off; G-check (shadow columns and the
shadow entries' color rows); self-shadow: at a pose where every color entry
selects level L, the automatic image is byte-identical to the image with
every entry forced to L; `scripts/shadow_map_cache_check.py` keeps its
passes, and a camera move that changes no level keeps every cached pass (the
cached-pass counts in the shadow draw statistics); four view with two
viewports at different distances from one caster selects the finer level in
the shared spot pass; a shader debug viewport sampling a spot map draws its
casters at the template range.

### Step 6: headset and measurement

Quest: the headset union view's column, both eyes the same level (the
multiview image of a forced level L equals automatic L at a pose selecting
L), the `lod_lanes()` cost in R, and `scripts/quest_bench.py --perfetto
--record` on Bistro with LOD chains (stored chains need `doc/plans/lod.md`
step 6) before and after: binning and app GPU time. Desktop Release Bistro:
GPU Main before and after.

### Later

A color-by-level debug view: the per-draw record holds no level, so the
shader would bind the level region and find the entry through the chunk
begin plus `first_instance`; not part of these gates.
