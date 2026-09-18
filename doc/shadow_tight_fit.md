# Shadow fit performance

Stability: mostly stable

The directional tight fit
(`Light::tight_directional_light_projection_transforms()` in
`light_frustum_fit.cpp` and its inputs) runs per shadow-casting directional
light per frame, over every visible caster and receiver. This document holds its
cost model and the optimizations that shape it; [`shadows.md`](shadows.md)
describes what the pipeline computes.

**Scope.** Directional lights only. Spot lights use a fixed perspective
projection from the light pose, and point lights use an omnidirectional cube map
([`point_light_shadows.md`](point_light_shadows.md)); neither runs the frustum
fit.

## Cost model

Per frame, per shadow render node, with N receivers, M casters, L tight-fitted
directional lights, S = receiver silhouette edge count and M' = the casters
surviving the filter:

| Stage | Cost | Runs |
|---|---|---|
| AABB gather (`get_aabb_world` per mesh) | O(N) corner transforms | once per frame |
| Receiver in-frustum filter + corner gather | N x 6 planes, center+extents | once per pass (cross-light cache) |
| Receiver corner hull | QuickHull over 8N points | only when the corner set changed |
| Hull / frustum clip + re-hull | Sutherland-Hodgman both ways + QuickHull over the welded set | only when the corner set or frustum changed |
| Silhouette + sweep planes | 2D hull (sort) | per light |
| Caster filter | M x (<=12 + 1 + S) planes, center+extents | per light |
| Caster per-box clip | boundary boxes: 12 triangles x <=6 planes Sutherland-Hodgman; interior boxes: corner append | per light |
| Calipers projection + 2D hull | sort over the fit point set | per light, with `optimize_rotation` |

The only QuickHull runs left are the two receiver-side ones, at most once per
pass and skipped entirely on repeated inputs. The per-light caster stage is
linear in M' and builds no hull.

## Standing optimizations

**Center+extents plane tests.** `aabb_in_frustum`, `aabb_in_convex_volume` and
`first_rejecting_plane` reject a box against a plane with
`dist(plane, center) + dot(abs(plane.xyz), extents) < 0` - two dot products
instead of eight, with the same conservative result as testing all corners.

**Light-independent receiver work is hoisted.** Everything in
`build_receiver_cull_planes` up to and including the re-hulled clipped receiver
hull depends only on the receiver AABBs and the view frustum, so it is computed
once per `Light_projections::apply()`; only
`build_shadow_caster_cull_planes_from_hull` is per light.

**Fixed hull topology in the clips.** The frustum's 12 triangles and a box's 12
triangles are compile-time index tables, so `clip_convex_hull_points_to_frustum`
and the per-box caster clip never rebuild a hull from their corners. Besides
being cheaper, this avoids QuickHull epsilon-merging the tiny near rectangle at
close range.

**Weld before re-hull, dedupe coplanar face planes.** Per-triangle
Sutherland-Hodgman emits each shared vertex once per incident triangle, so the
clipped point set is welded (sort + unique with epsilon) before the re-hull, and
coplanar triangle planes are deduplicated before the second pass clips the
frustum against them.

**Allocation hygiene.** `calculate_bounding_convex_hull` takes a
`std::span<const glm::vec3>` (`glm::vec3` and `quickhull::Vector3<float>` are
layout-identical 12-byte PODs, so no copy is needed), the QuickHull instance is
`thread_local` so its internal pools persist across calls, and the gather
vectors in `Shadow_renderer::render` and the receiver cache buffers are
persistent. The small per-fit vectors (plane lists, silhouettes, clip scratch)
are still per-call: threading a scratch context through them is invasive and
only worth it if profiling shows allocator time in the fit.

**Per-box caster clip.** Each surviving caster AABB is clipped to the open
F_shadow on its own and the fit takes the union of the clipped point sets, so no
hull is built over all surviving corners. A box entirely inside the volume
(center+extents test per plane) contributes its 8 corners directly with no
Sutherland-Hodgman run, and F_main corners contained in a box are appended
(point-in-AABB test with a dedup bitmask) because a surface clip cannot produce
volume vertices interior to the box. This is correct and never looser: caster
geometry lies in the union of the boxes, and `union(box ^ F_shadow)` is a subset
of `hull(all corners) ^ F_shadow`, which additionally covered the empty bridge
regions between separated casters. The caster hull survives only as a debug
visualization, built when `collect_debug` is on.

**Receiver cache.** `ensure_receiver_cache` gathers the in-frustum receiver
corner set into a scratch and compares it, by exact float equality (the corners
are copied from the same AABB source every pass), against the previous pass:

- corner set unchanged: the corner hull depends on nothing else, so the
  QuickHull over 8N points is skipped and the stored hull is reused;
- corner set and view frustum corners both unchanged: the clip and re-hull
  inputs are identical too, so the whole cached result stands and the function
  returns after the gather.

Camera-only movement therefore pays for the in-frustum filter and the
comparison; the hulls rerun only on frames where the surviving receiver set
changes. Reuse is disabled while `collect_debug` is on, because the debug
vectors must be refilled each pass.

**Profiling zones.** `ERHE_PROFILE_*` zones cover
`calculate_bounding_convex_hull` (both overloads),
`clip_convex_hull_points_by_planes`, `clip_convex_hull_points_to_frustum`,
`build_shadow_caster_cull_planes_from_hull` and `build_receiver_cull_planes`,
plus "fit: casters" with its "fit: filter casters" and "fit: clip casters"
children and "fit: optimize rotation (calipers)".

## Verification

Any change here keeps the fit output identical or tighter at the same coverage:

- the same receiver and caster classifications in the shadow-fit debug data on
  the same scene and camera, and no missing or clipped shadows with
  `fit_to_casters` on;
- with a static scene and a moving camera, the receiver hull zones appear only
  on frames where the in-frustum receiver set changes, and a fully static frame
  shows no receiver clip or re-hull zones;
- the "fit: clip casters" zone stays small, because most boxes take the
  interior fast path.

## Future work

- [plans/shadows.md](plans/shadows.md) - remaining fit and point-shadow
  performance candidates.
