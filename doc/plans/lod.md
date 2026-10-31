# Mesh level of detail

Status: proposed

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
vertex buffer**. Level 0 is the optimized fill range that exists today; level
`i > 0` is a `meshopt_simplify` / `meshopt_simplifyWithAttributes` result over
the same welded vertices, appended to that variant's index allocation. No
level has vertices of its own.

The reasons for this placement:

- The optimized variant is already welded, fill-only and per `Primitive`, so it
  is exactly the input simplification needs. Shared primitives get one chain,
  as one (meshoptimizer requirement 4).
- Everything that must stay exact keeps using the source data: ID rendering
  and picking (`original` variant), BLAS / raytrace, physics collision,
  export. LOD only changes what the fill passes draw.
- The edit bracket already drops the optimized variant when an edit starts
  and rebuilds it when the edit ends. The chain belongs to that variant, so it
  is invalidated and rebuilt with it, with no new rule.
- Index-only levels cost index memory only (a 50% level costs half of level
  0's indices). A level never changes `base_vertex`, the vertex format or the
  quantization affine, so switching level changes only
  `(first_index, index_count)` in a draw command.

Each level records its **object-space error**: `result_error` from
meshoptimizer times the mesh extent (meshoptimizer reports a relative error).
Runtime selection converts this to pixels.

The chain is described by a `Lod_chain` value on the optimized
`Primitive_render_shape`: per level `{first_index, index_count, error}`, level
0 included, at most `max_lod_levels` (8) entries. A primitive without a chain
has one implicit level.

### 2.2 Generation

`erhe::primitive` gains `generate_lod_chain(streams, indices, Lod_settings)`,
private to the target like the other meshoptimizer calls. `Lod_settings` is
plain data:

- `level_count`, `ratio` per step (target index count of level `i` =
  `ratio^i` times level 0), and `max_error` (relative);
- `use_attributes` and the attribute weights (normal, texcoord 0, vertex
  color) for `meshopt_simplifyWithAttributes`;
- `lock_border` (`meshopt_SimplifyLockBorder`) for meshes split into several
  primitives that must not crack apart at the split;
- `sloppy_fallback`: when a level reaches less than a set fraction of its
  target reduction, run `meshopt_simplifySloppy`. Off by default.

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
Three settings would drop this guarantee, and erhe exposes each one as an
explicit setting that is off by default: `meshopt_simplifySloppy`
(`sloppy_fallback`), `meshopt_SimplifyPermissive` (`permissive`, which lets
collapses cross seams) and a `normal_merge_angle` large enough to merge real
hard edges. The step 1 tests include a textured box and a UV sphere with a
seam, and check that the level 1 seam vertices keep their source texcoords.

Each level is simplified from the previous level, not from level 0. A level's
error is `max(previous_error * 1.5, step_error)` times `meshopt_simplifyScale`,
so error grows strictly from level to level. Generation stops when a step
keeps 75% or more of its input triangles, or when 24 or fewer indices remain.
A mesh that cannot simplify therefore gets a short chain, never duplicate
levels. Skinned primitives simplify with the
joint indices locked through the vertex lock array where joints differ across
an edge, so a collapse cannot blend influences. This rule is checked in step
1's tests.

Generation runs where the optimized variant is built: on the deferred
finalize worker for the geometry path, and for the soup path on the import
worker. The editor's on-request generation (step 2) runs the same function on
a worker task and publishes through the same publication point,
`Primitive::publish_optimized_render_shape()`. The edit hold rules therefore
apply to it unchanged.

### 2.3 Runtime selection

Per drawn entry and per view:
`pixels = error * max_axis_scale * projection_scale / distance`. For a
perspective view, `projection_scale` is
`viewport_height / (2 * tan(fov_y / 2))`. For an orthographic view it is
`viewport_height / ortho_height`, with `distance = 1`. `distance` is measured
from the view origin to the closest point of the entry's world AABB (the
bounds the culling lanes already read), and is 0 inside the box, which
selects level 0. The selected level is the coarsest one whose `pixels` is at
most `lod_threshold_pixels`, offset by the node's bias and clamped to its
min / max level.

This is camera-dependent derived state. It is computed where each view's
commands are already written per entry: the CPU command writer, and
`cull_compact.comp` on the GPU path. That loop runs per entry and per frame
already, so selection adds work inside an existing loop and no new per-frame
walk. Entries keep a LOD table index. The command template becomes a per-level
table, written by `publish()` under the same serial check as today.

Shadow views select with the main camera's position and projection scale,
as Godot does, so a caster's shadow matches the level the viewer sees. This
is the proposed default for D3.

### 2.4 Control properties

An attached value group `Lod` on `erhe::scene::Node`, following
`Draw_mode` (`doc/erhe/property_system.md` 4.23 / 4.24). Values ride the node's
`ERHE_node` properties map, so they survive a glTF round trip.

| Property | Type | Default | Meaning |
|---|---|---|---|
| `Lod.mode` | enum `automatic` / `forced` / `off` | `automatic` | `off` draws level 0; `forced` draws `forced_level` |
| `Lod.forced_level` | int | 0 | used when `mode == forced`; clamped to the chain |
| `Lod.bias` | float | 0 | added to the selected level (fractional values are rounded) |
| `Lod.min_level` / `Lod.max_level` | int | 0 / 7 | clamp |

The values inherit down the hierarchy, so setting them on a scene's root or a
group node applies to its subtree. The effective values are resolved at the
change site and mirrored into the draw-list objects through the existing
change notification (`on_property_changed`). The draw path never walks the
hierarchy.

Global settings go in `Render_settings` (graphics preset): `lod_enabled`,
`lod_threshold_pixels` (default 1.0), `lod_bias`. Generation defaults go in
an editor config section (`Lod_settings`), used by the import path when
`generate_lods_on_import` is on (off by default until step 6).

## 3. Decisions for the user

Ask before the step that needs each one.

- D1 (step 1): generate LODs at import automatically, or only on request
  from the editor? The plan starts with on request only.
- D2 (step 2): the edge-line passes draw the `original` variant, so while a
  level is forced the edge lines show the full-resolution mesh over the
  simplified fill. Options: hide edge lines on meshes drawn at level > 0,
  build per-level line indices from the simplified triangles, or accept the
  mismatch in the first version.
- D3 (step 4): shadow LOD. Proposed: the main camera's position and scale,
  as Godot does, so there is no mismatch between a mesh and its own shadow.
  Alternatives: per shadow view from its own projection, as O3DE does, or the
  main camera's level minus one. With several viewports, "main camera" means
  the view the shadow pass is rendered for.
- D4 (step 5): transitions. Both reference engines pop and rely on the 1 px
  threshold. Options: pop (the default), a small hysteresis band (needs
  per-view per-entry state), or a screen-door dithered cross-fade in the
  opaque pass (a second draw during the fade plus a shader variant).

## 4. Build order

Each step is one commit (or a small series) with docs in the same commit, and
it is verified before the next step starts.

### Step 1: LOD chain generation in erhe::primitive

- `Lod_settings`, `Lod_chain`, `generate_lod_chain()` in `erhe_primitive`.
- Store the chain on the optimized `Primitive_render_shape`. Append the level
  indices to the optimized index allocation, sized up front in the deferred
  allocation transaction so it stays one allocation.
- Statistics: per-level triangle count and error in the per-primitive log
  line, and a `lod` section in MCP `get_memory_usage`.
- Unit tests: sphere / box / skinned box chains (monotonic counts, monotonic
  errors, indices in range, early stop on a cube, locked joints).
- No rendering change: nothing selects a level above 0 yet.

### Step 2: editor LOD window (generation for the selection, forced preview)

- New ImGui window `LOD` (`src/editor/windows/lod_window.*`): `Lod_settings`
  widgets, a `Generate for selection` button (a worker task per unique
  `Primitive` of the selected meshes), and a table per primitive: level,
  triangles, ratio, error (object space and current pixels for the active
  viewport).
- A level slider per selection that writes `Lod.mode = forced` +
  `Lod.forced_level` (needs step 3's properties; until then the window holds
  a session-only override in the draw-list object). The draw-list entry is
  re-registered with the forced level's range at the edit site, which is
  change-driven: no per-frame selection exists yet.
- MCP tools: `generate_lods(node_ids, settings)`, `get_lod_info(node_id)`,
  `set_lod_override(node_id, level)`; verification with
  `render_scene_image` per level (triangle count visible in wireframe /
  solid wireframe if D2 allows).
- Hand to the user for the first interactive pass; record the feedback here.

### Step 3: Lod control properties

- `Lod` attached value group (section 2.4), shown in the Properties window,
  serialized, inherited, glTF round trip covered by
  `scene_roundtrip_verify.py`.
- The LOD window's slider becomes a writer of these properties; the
  session-only override is removed.

### Step 4: automatic selection (CPU command path)

- Per-level command templates, LOD table index per entry, selection in the
  CPU command writer for color and shadow passes (D3).
- `Render_settings` globals plus Settings / graphics-preset UI.
- Debug view: color by selected level through the existing
  `FLAT_DRAW_COLOR` variant (dbb087c9e). Stats per pass: entries per level
  and triangles drawn.
- Gates: with `lod_enabled = false` images are byte-identical to HEAD; with
  `forced 0` byte-identical; a scripted camera dolly shows levels switching
  at the expected distances (`get_lod_stats`).

### Step 5: GPU command path and transitions

- Selection in `cull_compact.comp` from the per-level table; the CPU / GPU
  check (`get_cull_classification_check`) extended to the selected level with
  0 mismatches.
- D4's transition choice.

### Step 6: persistence and import

- LOD chains go into the meshoptimizer filesystem cache key (settings
  included) and into the glTF fast-load prepared data, so a reload replays
  them.
- `generate_lods_on_import` per D1.
- Measure on Bistro: Release desktop (GPU-bound viewport) and Quest, where
  Bistro is geometry-bound (memory-bank/topics/quest.md), so the triangle
  reduction should show in frame time.

### Later, from feedback

Visibility ranges / HLOD (Godot's visibility ranges: whole-node swaps such as
a card proxy, which `Draw_mode` cards already resemble), meshlet / cluster
LOD, per-material LOD thresholds, and LOD for edge lines and the ID pass.

## 5. Verification recipe

Headless Vulkan editor (`erhe-headless-verify`): generate LODs on a creation
scene through MCP, `render_scene_image` per forced level and compare triangle
counts reported by `get_lod_info` with the drawn counts from the pass stats.
Byte-identity gates for the off states as in step 4. Interactive checks
(window feel, popping visibility) go to the user.

## 6. Feedback log

Empty until step 2 reaches the user.
