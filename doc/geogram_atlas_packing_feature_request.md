# Feature request: resolution-aware chart packing options in `mesh_make_atlas()`

Use case report and API suggestion for
[BrunoLevy/geogram](https://github.com/BrunoLevy/geogram), written from the
perspective of a real-time engine using Geogram for lightmap UV unwrapping.

## Use case

erhe (a C++ scene / rendering engine) uses Geogram to generate lightmap UVs:

- `mesh_make_atlas(mesh, hard_angles, PARAM_ABF, PACK_XATLAS)` produces the
  per-corner `tex_coord` parameterization for each mesh.
- The engine then packs *per-mesh-instance* rectangles into a shared lightmap
  atlas page. Each instance's rectangle gets a resolution derived from its
  world-space surface area times a user-chosen texel density
  (e.g. 64 texels/meter), typically between 32x32 and a few thousand texels
  per instance.
- A GPU pass rasterizes a texel G-buffer in UV space, a ray-query pass bakes
  lighting per texel, and the runtime samples the atlas bilinearly.

This is the standard lightmap pipeline (Bakery, Godot, Bevy and others use the
same shape), and Geogram's one-call atlas maker is very attractive for it -
segmentation, parameterization, validation and packing in one call.

## Problem: chart gutters are sized in a resolution the caller cannot know

`pack_atlas_using_xatlas()` hardcodes the xatlas pack options
(`mesh_param_packer.cpp`):

```cpp
xatlas::PackOptions packerOptions;
packerOptions.padding = 1;
xatlas::PackCharts(atlas, packerOptions);
```

`resolution` and `texelsPerUnit` are left at 0, so xatlas chooses an internal
atlas resolution itself, packs charts with a **1-texel gutter at that internal
resolution**, and Geogram then rescales the result into `[0,1]^2`.

The gutter therefore becomes a *fixed fraction of UV space*, unrelated to the
resolution the caller will actually rasterize at. In our pipeline a small
object's chart set may land in a 50x50..200x200 texel rectangle; the
normalized xatlas gutter shrinks to (well) under one texel at that size.
Consequences observed in practice, on a 1280-quad torus:

- Adjacent charts sit inside each other's bilinear sampling footprint.
  Chart-edge texels ("comb teeth" of thin edge triangles) belonging to one
  chart are sampled by the surface of the neighboring chart, producing dark
  dashed seams that no amount of caller-side dilation can fix (the offending
  texels are *valid* - they just belong to another chart).
- The caller cannot compensate: there is no API to widen the gutter, to set
  the packing resolution, or even to *learn* what internal resolution /
  gutter was used.

The Tetris packer has the same shape of problem: `image_size_in_pixels_` and
`margin_width_in_pixels_` are private constants of the `Packer` class with no
public setters reachable from `mesh_make_atlas()`.

## Second observation: outlier corner UVs at chart corners

On the same coarse torus we also find a small set of facets (36 of 1280)
where three corners carry mutually consistent UVs and the fourth carries a
wildly outlying value - not equal to any UV that the same vertex has in any
neighboring facet (so it is not simple cross-chart mixing; it looks either
like a stale/aliased value in the per-corner commit near points where three
charts meet, or extreme local distortion that `ParamValidator` accepts).
Example, in units of a 201-texel chart set: three corners within a 7-texel
quad, fourth corner 45 texels away. Rasterizing such a facet produces a long
sliver across unrelated charts; for lightmapping this bakes visible garbage.
A validator criterion on per-facet UV edge length ratios (reject/resegment
facets whose UV footprint is an outlier vs. their 3D footprint) would catch
both this and the folded-triangle case below.

## Third observation: folded triangles survive validation

On coarse curved meshes (torus of 1280 quads, sphere, capsule; charts of a few
hundred triangles), the parameterizations returned by both `PARAM_ABF` and
`PARAM_LSCM` contain a small number of winding-flipped (folded) triangles: we
measured 41 of 2560 triangles flipped, overlapping 12.6% of the covered
texels when rasterized. `ParamValidator` apparently accepts these charts.
Rasterizing such a chart writes another surface region's data over good
texels. We now cull the minority winding at rasterization time as a defense,
but a validator option to reject/re-segment charts containing flipped
triangles (or a documented guarantee about orientation) would remove the
problem at the source.

## Suggested API improvements

Any one of these would solve the gutter problem for us; they are ordered by
how directly they map to our need.

1. **Expose pack options.** For example:

   ```cpp
   struct AtlasPackOptions {
       index_t resolution     = 0;  // 0 = let the packer choose
       double  texels_per_unit = 0.0;
       index_t padding_in_texels = 1;
       bool    block_align    = false;
       bool    brute_force    = false;
   };

   void mesh_make_atlas(
       Mesh& mesh,
       double hard_angles_threshold = 45.0,
       ChartParameterizer param = PARAM_ABF,
       ChartPacker pack = PACK_XATLAS,
       bool verbose = false,
       const AtlasPackOptions& pack_options = {}
   );
   ```

   With `resolution` (or `texels_per_unit`) plus `padding_in_texels`, a caller
   that knows its target rasterization density can get gutters that survive
   the final normalization at a predictable width. xatlas already accepts all
   of these; the change is plumbing.

2. **Report the packing result.** Return (or store as mesh attributes) the
   atlas resolution xatlas chose and the normalization scale applied, so the
   caller can at least compute the effective gutter width and choose its
   rasterization resolution to match.

3. **Guaranteed-minimum normalized gutter.** A single
   `double min_gutter_uv` parameter: after packing and normalization, no two
   charts closer than this distance in UV space. Callers can then derive
   `min_gutter_uv = padding_texels / target_resolution`.

4. **Document `PACK_NONE` + `pack_atlas_only_normalize_charts()` + `chart`
   attribute as the "bring your own packer" path.** This combination (which
   we now use as a workaround: parameterize without packing, normalize chart
   scales, read the `chart` facet attribute, pack chart bounding boxes
   ourselves with texel-sized gutters at the known target resolution) works
   well, but its viability is only discoverable by reading
   `mesh_param_packer.cpp`. A few sentences of documentation - and a
   guarantee that `mesh_make_atlas(..., PACK_NONE)` keeps the `chart`
   attribute and per-chart `tex_coord` consistent - would make it a
   supported use case rather than an implementation detail.

## Environment

- Geogram pinned at 5a96c38e (behavior verified unchanged at current main at
  the time of writing for the code paths cited above).
- Windows / MSVC and Linux / clang builds.
- Meshes: closed manifold polygon meshes, fan-triangulated by
  `mesh_make_atlas()` internally; 1k-100k facets.
