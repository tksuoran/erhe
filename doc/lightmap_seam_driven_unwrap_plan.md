# Lighting-driven seams: per-facet first bake, measured seam placement

Status: phase 1 IMPLEMENTED + verified 2026-08-02 (per-facet unwrap mode,
Atlas_parameterizer::per_facet, exposed as "Per-facet" in the Lightmap
window combo and parameterizer="per_facet" over MCP; end-to-end MCP bake
on the default scene produced a correct atlas - floor shadows crisp,
per-facet mini-charts on the polyhedra). Phases 2-4 not started.

Finding recorded during phase 1: Geogram's atlas charts are MIRRORED
relative to the 3D facet winding (CCW facets arrive with negative signed
UV area), and the G-buffer raster's fold-culling convention
(lightmap_baker.cpp cull_mode_back_cw + winding_flip_if) is tuned to
that. An orientation-preserving per-facet flatten rasterized ZERO texels
until its bitangent was mirrored to match (make_atlas.cpp
build_per_facet_charts). Relevant to the upstream report's orientation
observation in doc/geogram_atlas_packing_feature_request.md.

Cost observation from the verify run (128 tpm): dense curved meshes get
gutter-dominated regions exactly as predicted (sphere ~960 facets in a
114^2 region = 2-3 texel charts); flat / large-facet meshes are clean.
Phase 3b (merging) or higher density is where that resolves.

Also added 2026-08-02 (user-directed, per-facet mode):
- Region sizing is UV-coverage-corrected (Lightmap_baker::update_layout):
  side = sqrt(world_area / uv_coverage) * density, so texels-per-meter is
  exact per facet regardless of gutters / packing waste / min-chart
  upscales (coverage floored at 0.05 = max ~4.5x boost).
- Chart reorder by baked luminance ("Reorder Charts By Bake" button, MCP
  lightmap_reorder_charts): after a bake, re-unwrap with charts packed in
  per-facet baked-luminance order (Lightmap_baker::build_chart_order_keys
  -> Make_atlas_operation per_facet_chart_order -> pack sort key), so
  similarly lit facets are atlas neighbors and cross-chart filter-tap /
  dilation pollution picks up similar values (leak camouflage; anisotropic
  grazing-angle sampling motivated it). Measured: mean adjacent-valid-
  texel luminance difference -35% on the default scene.
- Gutter-aware dilation clamp (iterations = min(4, gutter/2)) and a
  Lightmap-window warning when gutter < 2x the filter reach (bilinear 1,
  bicubic 2 texels).

Companion to doc/lightmap_baking_plan.md and
doc/lightmap_texture_viewer_plan.md.

## Idea (user-directed)

Do not special-case poles (cone tips) or any other geometric feature.
Instead:

1. First pass: every triangle/facet is its own UV chart - nothing shares
   texels, so no shared-texel artifact of any kind can exist (cone tips,
   pole fans, curvature singularities, anything).
2. Bake with that layout.
3. Measure, from the baked data, which mesh edges have continuous lighting
   across them and which do not.
4. Seams are then *derived*: keep (or blend away) chart borders where the
   measurement says lighting is continuous; keep true seams only where it
   is not.

This covers every problem case the geometry-heuristic approach would have
to enumerate, because the classifier is the baked lighting itself.

## Why this fits the existing pipeline

- The baker (Lightmap_baker) is chart-topology-agnostic: it consumes
  channel-2 corner UVs and a chart-packed atlas, nothing else. A
  per-facet unwrap needs zero baker changes.
- erhe already owns the packer (pack_charts_with_texel_gutter,
  make_atlas.cpp) and the seam-blend pass (build_seam_vertices +
  record_seam_blend, lightmap_baker.cpp) - both are the natural
  attachment points.
- The atlas readback (debug_write_lightmap_png path) and the corner-UV
  walk (viewer overlay cache / build_seam_vertices) provide everything the
  measurement pass needs.

## Phase 1 - per-facet unwrap mode

New unwrap mode in erhe_geometry make_atlas.cpp that skips Geogram
entirely: each facet becomes one chart, flattened isometrically into its
own plane (local 2D frame from the facet basis; planar facets flatten
exactly, so *zero* parameterization distortion and zero overlapping
triangles by construction). Write the "chart" facet attribute (chart id =
facet id) and per-corner UVs, then reuse the existing
pack_atlas_only_normalize_charts-equivalent scaling +
pack_charts_with_texel_gutter as-is.

Exposure: a new Atlas_parameterizer-level choice ("per_facet") through the
existing knob chain (Lightmap_config.uv_parameterizer combo, MCP
lightmap_generate_uvs parameterizer arg) - the plumbing added 2026-08-02
already carries it.

Cost note (why this is a first pass, not automatically the final layout):
a chart of side s texels with gutter g occupies (s + 2g)^2, so tiny charts
are gutter-dominated (s = 8, g = 2 -> 2.25x area). Acceptable for the
coarse default scene; the plan's later phases decide whether the final
layout keeps it.

Independent payoff: even alone, per-facet mode is a defect-free baseline
(the Lightmap Texture window's overlap counter must read 0) to A/B
against Geogram's parameterizations.

## Next up (user-directed, 2026-08-02) - procedural sky lighting

Include lighting from our procedural sky in the bake. Ordered BEFORE
phase 2: the edge-continuity classifier measures baked luminance, so the
lighting environment should be complete before thresholds are tuned
against it. (Today the gather loop only handles punctual lights -
directional/point/spot shadow rays plus one diffuse bounce; rays that
escape to the sky contribute nothing.)

## Phase 2 - edge continuity measurement

After a bake with the per-facet layout has converged (N sweeps):

- Read the published atlas back to CPU (existing readback path).
- For every interior mesh edge (shared vertex-id pair, the
  build_seam_vertices walk): sample K points along the edge; for each
  side, sample that side's own texels half a texel inward along the
  side's inward perpendicular (in that side's chart UVs).
- Metric per edge: luminance-weighted relative difference across the K
  pairs (max and mean). Classify: continuous (difference below threshold)
  vs discontinuous (seam needed). Threshold is a config knob.
- Persist the classification per edge (keyed like the seam map: ordered
  vertex-id pair per primitive).

Caveat to encode in the design: the classification depends on the
lighting, so light/occluder edits invalidate it (reuse the baker's
m_hash_lighting tier). For a static bake that is one measurement after
convergence; for interactive baking, re-measure on the publish cadence or
freeze the classification once made.

## Phase 3 - act on the measurement (two milestones, cheap first)

a) Measured seam blending (recommended first milestone). Keep the
   per-facet layout as the final layout. Replace the current seam-blend
   edge list (today: equal-corner-normal seams only) with the measured
   list: blend every edge classified continuous, never blend
   discontinuous ones. This reconstructs visual continuity across the
   fine-grained charts without a second unwrap, and the cone tip is
   correct by construction (each fan triangle owns its tip texels).
   Risk: atlas area (gutter overhead) and bilinear behavior reviewed at
   this point with real numbers from the default scene.

b) Measured re-charting (full version, only if (a)'s area/quality is not
   good enough). Region-grow charts by merging facets across continuous
   edges (BFS with a distortion bound), forced seams at discontinuous
   edges; re-parameterize each merged chart (LSCM on the chart subset -
   or Geogram per chart), repack, rebake. Ends with a compact layout
   whose seams exist only where lighting demanded them. This is the
   "dynamically add seams where needed" end state; it needs a second
   bake and a re-unwrap operation that preserves undo semantics.

## Phase 4 - tooling

- Lightmap Texture window: edge-classification overlay (continuous =
  green, seam = red) on top of the existing chart edges; counts in the
  toolbar next to the overlap counter.
- MCP: a lightmap_measure_seams tool that runs phase 2 on demand and
  returns the per-class edge counts + worst offenders (mesh, facet pair,
  metric), so the loop is scriptable end to end.

## Open questions (decide during implementation)

- K (samples per edge) and the difference threshold: start K = 4,
  threshold ~10% relative luminance; tune on the default scene.
- Whether phase 3a blending should be multi-pass (wider blend for
  low-frequency lighting) - today's single 0.5-alpha line pass may leave
  visible steps at big charts' borders; per-facet charts are small, so
  probably fine.
- Per-facet mode and very dense meshes: chart count = facet count;
  pack_charts_with_texel_gutter is O(n log n) but atlas pages cap at
  4096^2 (s_max_page) - document the practical mesh-size ceiling rather
  than engineering around it now.
