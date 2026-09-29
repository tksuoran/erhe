# Shadow follow-ups

Status: proposed

Extends [../shadows.md](../erhe/shadows.md) (the shadow pipeline),
[../shadow_tight_fit.md](../erhe/shadow_tight_fit.md) (its cost model and standing
optimizations) and [../point_light_shadows.md](../erhe/point_light_shadows.md) (the
cube path). Decide each item from a Tracy capture rather than in advance.

## Bias

The shadow bias design and its verification are in
[../erhe/shadows.md](../erhe/shadows.md) ("Bias technique", "Shadow
verification"); Metal verification and the forward pass cost (G7) remain in
[shadow_robustness.md](shadow_robustness.md).

## Cap the receiver silhouette plane count

`receiver_filter_planes` is 1 + the silhouette edge count, unbounded as receiver
geometry gets complex, and every caster pays per plane. Simplify the 2D
silhouette hull conservatively to at most K edges (K around 8 to 12) by
repeatedly removing the vertex whose removal adds the least area and replacing
its two edges with their intersection point, which grows the volume outward
only. Verify that no caster is culled that was previously kept.

## Temporal whole-fit skip

With camera, lights and all caster / receiver world AABBs unchanged since the
previous frame, reuse the previous fit output entirely: caster filter, per-box
clip, calipers and box assembly. This needs cheap change detection (revision
counters bumped on node transform, hierarchy and geometry change). Do the
whole-fit skip only - finer caching interacts with texel snapping. The receiver
cache already skips the receiver hulls on unchanged inputs, including across
camera movement, so this item is about the remaining per-light work.

## QuickHull library work

All remaining QuickHull work is on the receiver side, so these matter only while
the receiver hulls still show in a profile. The library is vendored in
`src/quickhull` and is freely modifiable.

- **Per-call overhead.** `createConvexHalfEdgeMesh` ends with
  `m_indexVectorPool.clear()`, discarding the warm per-face index-vector pool
  the `thread_local` instance exists to keep; the `ConvexHull` result object
  allocates a fresh un-reserved vertex buffer, a `vector<bool>`, a face stack and
  an `unordered_map` vertex remap per call, all copied out immediately. Keep the
  pool warm across calls and add an entry point that walks `MeshBuilder` faces
  (skipping disabled ones) directly into `erhe::math::Convex_hull` with a
  persistent flat remap array. Also remove the `std::cerr` horizon-edge failure
  print, which is a frame-spike hazard.
- **Epsilon.** `calculate_bounding_convex_hull` passes 1e-6 where the library's
  float default is 1e-4. A tighter epsilon keeps near-coplanar corner grids
  (boxes on a ground plane, shared wall heights) from merging into faces, which
  inflates face and iteration counts. 1e-4 bounds hull under-coverage at about
  eps x scene scale (1 cm at 100 m), absorbed laterally by the two-texel snap
  padding - A/B it against the shadow-fit debug data before trusting it.

## Cheaper receiver pipelines

- **Interior-point prune before the receiver hull** (Akl-Toussaint at box
  granularity): gather extreme corners along about 14 fixed directions (axes and
  diagonals), hull those points, then drop every receiver AABB fully inside that
  inner polytope with the center+extents test - one test per box, not per corner
  - before expanding survivors to corners. QuickHull's own initial tetrahedron
  discards interior points already, but on flat scenes (a ground plane) that
  tetrahedron is thin and nearly useless, which is exactly where the
  multi-direction polytope is strong.
- **Polyhedral clip**: clip the receiver hull as a connected half-edge mesh
  plane by plane, producing the exact intersection mesh. This deletes the re-hull
  entirely along with the degenerate-input risk of hulling coplanar point sets.
- **Pure-2D receiver pipeline**: project receiver corners along the light, take
  the 2D hull, intersect with the frustum's projected silhouette and cap at
  `s = max(min_s(receivers), min_s(frustum))`. This deletes all 3D hull and clip
  work; it is conservative but looser where receivers extend beyond the frustum
  along the light axis. A/B the culled-caster counts before committing.
- **Per-light parallel fit** (fork-join) and a SIMD / SoA caster filter.
- **True O(h) rotating calipers** in `calculate_min_area_obb_2d`, which is
  O(h^2) today; h is small, so only if a profile says otherwise.

## Point-light cube shadow budget

The cube path re-rasterizes the whole shadow-caster set into all six faces of
every shadow-casting point light with no culling, which is the dominant point
shadow cost.

- **Per-face caster culling.** Each cube face is a 90-degree frustum; cull
  caster AABBs against it and against the light range sphere, so a face draws
  only the casters that can fall in it. This reuses `aabb_in_frustum` and the
  center+extents test.
- **Skip empty faces and lights.** A face (or a whole cube) with no surviving
  casters can clear only; a light whose range sphere contains no casters needs
  no cube at all.
- **Range and contribution cull.** Casters fully outside the light range, or
  whose shadow cannot reach any receiver, never need rasterizing.
- **Per-cube or per-face depth.** The six faces share one 2D depth scratch, so
  the passes serialize on a write-after-write barrier; separate depth lets them
  overlap, at a memory cost.
- **Resolution and count budget.** `point_shadow_resolution` x
  `point_shadow_light_count` R32F cube arrays reach hundreds of MB at the high
  preset; revisit the defaults and consider per-light resolution by screen-space
  size.
