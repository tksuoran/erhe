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
| Calipers projection + 2D hull | interior-box prune over the caster boxes, sort over the survivors | per light, with `optimize_rotation` |

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

**One pass over the casters.** The per-caster cull and the clip share one loop
over the caster AABBs: the center and extents of a box are formed once, tested
against the filter planes, the receiver planes and the open shadow volume planes
(each plane stored with its absolute normal, `Abs_plane`, so no per-box
`abs`), and the survivor goes straight to the interior list (no corner list is
built for it) or to the per-box clip. The verdict arithmetic is that of
`aabb_in_convex_volume()`, so the surviving set is unchanged.

**Box extents from center and extents.** `fit_box_of_aabbs` projects each box
center through the light space map and adds `abs(map) * half diagonal`, which is
the min and max over the 8 corners without projecting them. It rounds
differently from projecting each corner (measured: within 2 ulp of the largest
world coordinate), so the box grows by 16 epsilon times the largest world
coordinate, which keeps it from ever being smaller than the exact corner
projection. The light plane hull, the point counts and the caster verdicts stay
bit-identical to the corner version; the box before stabilization is larger by
at most that margin, and the stabilized box lands one snap step away in about
3% of its extents. On the heavy bench scene (Release, about 800 surviving
casters per light) the extents went from 17.5 to 4.9 us per light and the
caster cull plus clip from 56 to 34 us; the whole fit from about 120 to 85 us.

**Interior-box prune for the light plane hull.** The rotation stage needs the
2D hull of the projected fit points. Sorting all 8 corners of every surviving
caster box dominated the fit (the rotating calipers itself costs a few
microseconds for hulls of tens of vertices). Interior boxes (entirely inside
the open shadow volume) are therefore kept as boxes in
`Shadow_fit_scratch::interior_caster_aabbs`, not expanded into points, and
`erhe::math::calculate_bounding_convex_hull_of_boxes()` builds the hull:
a polygon from the extreme corners in 16 fixed light plane directions, a
center and extents test of every box against its edges, and a sort over the
corners of the boxes that are not inside it (corners inside the polygon are
dropped too). The margin of the test is far above float rounding, so the hull
is bit-identical to hulling every corner; the light space extents of the
boxes come from `fit_box_of_aabbs` (below). On the heavy bench scene
(about 800 surviving casters) this took the per-light fit from 320 to 700 us
down to 120 to 310 us, with about 96% of interior boxes pruned. The boundary
boxes (clipped to the volume) and the view frustum corners they contain stay
in `Shadow_fit_scratch::boundary_points`. The expanded point list
(`caster_points`) exists only when `collect_debug` is on.

A hull of the previous frame's participants as the seed was measured and is
not worth its bookkeeping (about 13% better than the fixed directions).
Reusing the receiver hull across frames helps only when objects move: with a
moving camera the hull of the in-frustum receiver corners changed on about
90% of the frames.

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

## Experiments and verification tools

- `src/erhe/math/test/test_shadow_fit_bench.cpp` (synthetic boxes) and
  `test_shadow_fit_replay.cpp` (captured fit inputs) time the fit stages and
  compare the pruned hull with the plain one. Both skip unless
  `ERHE_FIT_BENCH=1` (bench) or `ERHE_FIT_CAPTURE_DIR` (replay) is set, and
  only mean something in a Release build.
- `scripts/shadow_fit_capture.py` records the replay inputs from a running
  headless editor on the heavy bench scene (physics is switched off first: a
  teleported body shoves its neighbours and pollutes later frames).
- `scripts/shadow_fit_ab.py` launches one editor binary, plays scripted
  camera, light and mesh moves and writes the fit results (hull, rotation
  result, light space box after every stage) as text. Two builds of the fit
  must produce byte-identical files; `get_shadow_fit_debug` carries these
  fields.

## Future work

- [plans/shadows.md](../plans/shadows.md) - remaining fit and point-shadow
  performance candidates.
