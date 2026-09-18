# Lighting-driven seams: measured seam placement

Status: in progress

Extends [../../lightmap_baking.md](../../lightmap_baking.md) and
[../../lightmap_texture_viewer.md](../../lightmap_texture_viewer.md). Phase 1
(the per-facet unwrap mode) is in place and is described below because the
source cites it; phases 2 to 4 are the open work.

## Idea

Do not special-case poles (cone tips) or any other geometric feature. Instead:

1. First pass: every facet is its own UV chart, so nothing shares texels and no
   shared-texel artifact of any kind can exist - cone tips, pole fans, curvature
   singularities, anything.
2. Bake with that layout.
3. Measure, from the baked data, which mesh edges have continuous lighting
   across them and which do not.
4. Seams are then *derived*: blend away chart borders where the measurement says
   lighting is continuous, and keep true seams only where it is not.

The classifier is the baked lighting itself, so this covers every case a
geometric heuristic would have to enumerate.

This fits the existing pipeline: the baker is chart-topology-agnostic (it
consumes channel-2 corner UVs and a chart-packed atlas), erhe owns the packer
(`pack_charts_with_texel_gutter`, `make_atlas.cpp`) and the seam-blend pass
(`build_seam_vertices` + `record_seam_blend`, `lightmap_baker.cpp`), and the
atlas readback and the corner-UV walk provide everything the measurement needs.

## Phase 1 - per-facet unwrap mode

`make_atlas.cpp` has an unwrap mode that skips Geogram entirely: each facet
becomes one chart, flattened isometrically into its own plane (a local 2D frame
from the facet basis), so a planar facet flattens exactly - zero parameterization
distortion and zero overlapping triangles by construction. It writes the "chart"
facet attribute (chart id = facet id) and the per-corner UVs, then reuses the
existing normalize-charts and `pack_charts_with_texel_gutter` steps. It is
`Atlas_parameterizer::per_facet`, exposed as "Per-facet" in the Lightmap window
combo and as `parameterizer="per_facet"` over MCP, and it feeds the world-space
piece unwraps of `lightmap_prepare_tiles`.

Even alone this is a defect-free baseline to A/B Geogram's parameterizations
against: the Lightmap Texture window's overlap counter must read 0.

The cost that makes it a first pass rather than automatically the final layout:
a chart of side `s` texels with gutter `g` occupies `(s + 2g)^2`, so tiny charts
are gutter-dominated (`s = 8, g = 2` is 2.25x the area). Dense curved meshes hit
this exactly as predicted (a 960-facet sphere in a 114-texel-square region gives
2 to 3 texel charts at 128 texels per meter); flat and large-facet meshes are
clean. Phase 3b, or a higher density, is where that resolves.

## Phase 2 - edge continuity measurement

After a bake with the per-facet layout has converged:

- Read the published atlas back to the CPU through the existing readback path.
- For every interior mesh edge (the shared vertex-id pair from the
  `build_seam_vertices` walk), sample K points along the edge; for each side,
  sample that side's own texels half a texel inward along the side's inward
  perpendicular, in that side's chart UVs.
- Metric per edge: the luminance-weighted relative difference over the K pairs
  (max and mean). Classify continuous (difference below the threshold) or
  discontinuous (a seam is needed). The threshold is a config knob. Start with
  K = 4 and a threshold of about 10% relative luminance, and tune on the default
  scene.
- Persist the classification per edge, keyed like the seam map (the ordered
  vertex-id pair per primitive).

The classification depends on the lighting, so light and occluder edits
invalidate it: reuse the baker's lighting hash tier. For a static bake that is
one measurement after convergence; for interactive baking, either re-measure on
the publish cadence or freeze the classification once made.

## Phase 3 - act on the measurement

a) **Measured seam blending** (do this first). Keep the per-facet layout as the
   final layout and replace the current seam-blend edge list (equal-corner-normal
   seams) with the measured list: blend every edge classified continuous, never
   blend a discontinuous one. This reconstructs visual continuity across the
   fine-grained charts without a second unwrap, and a cone tip is correct by
   construction, because each fan triangle owns its tip texels. Review the atlas
   area (gutter overhead) and the bilinear behaviour with real numbers from the
   default scene at this point.

b) **Measured re-charting** (only if (a)'s area or quality is not good enough).
   Region-grow charts by merging facets across continuous edges (BFS with a
   distortion bound), force seams at discontinuous edges, re-parameterize each
   merged chart (LSCM on the chart subset, or Geogram per chart), repack and
   rebake. This ends with a compact layout whose seams exist only where lighting
   demanded them, and it needs a second bake and a re-unwrap operation that
   preserves undo semantics.

## Phase 4 - tooling

- Lightmap Texture window: an edge-classification overlay (continuous green,
  seam red) on top of the existing chart edges, with counts in the toolbar next
  to the overlap counter.
- MCP: a `lightmap_measure_seams` tool that runs phase 2 on demand and returns
  the per-class edge counts and the worst offenders (mesh, facet pair, metric),
  so the loop is scriptable end to end.

## Open questions

- Whether phase 3a blending should be multi-pass (a wider blend for
  low-frequency lighting). The single 0.5-alpha line pass may leave visible
  steps at a big chart's border; per-facet charts are small, so it is probably
  fine.
- Per-facet mode on very dense meshes: the chart count equals the facet count,
  and `pack_charts_with_texel_gutter` is O(n log n) but atlas pages cap at
  `s_max_page`. Document the practical mesh-size ceiling rather than engineering
  around it.
