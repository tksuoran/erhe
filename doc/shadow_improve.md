# Shadow fit optimization plan

Plan for optimizing the tight shadow frustum fit pipeline
(`Light::tight_directional_light_projection_transforms()` and its inputs),
derived from the 2026-06-10 pipeline review. See `doc/shadows.md` for how the
pipeline works; this document only tracks performance work.

Workflow: steps are executed one at a time, in order. After each step is
implemented and builds, the user verifies (correctness in the editor and/or
effect in the Tracy profile) before the step is committed. Update the Status
column as steps land.

## Cost model

Per frame, per shadow render node, with N receivers, M casters, L tight-fitted
directional lights, h = receiver hull vertex count, S = receiver silhouette
edge count:

| Stage | Cost | Runs |
|---|---|---|
| AABB gather (`get_aabb_world` per mesh) | O(N) corner transforms | 1x per frame |
| Receiver in-frustum filter | N x 6 planes x 8 corner dots | per light |
| Receiver corner expand + 3D hull | QuickHull over 8N points | per light |
| Hull/frustum clip | SH over ~2h triangles x 6 planes + QuickHull over frustum corners + 12 frustum triangles x ~2h hull planes | per light |
| Re-hull of clipped points | QuickHull over duplicate-heavy set | per light |
| Silhouette + sweep planes | 2D hull (sort) | per light |
| Caster filter | M x (<=12 + 1+S) planes x 8 corner dots | per light |
| Caster hull + clip | QuickHull over 8M' + SH clip | per light |

Key structural fact: the receiver pipeline rows (filter, hull, clip, re-hull)
do not depend on the light, yet run inside the per-light loop.

## Steps

| # | Step | Expected effect | Status |
|---|------|-----------------|--------|
| 1 | Profiling instrumentation (Tracy zones per stage) | Attribution, no perf change | done (c5d60708) |
| 2 | Center+extents (p-vertex) AABB-vs-plane tests | ~4x fewer flops on the hottest linear loops | done (d1e91f3f) |
| 3 | Hoist light-independent receiver pipeline out of the per-light loop | ~x L on the receiver pipeline | done (a00ecd5b) |
| 4 | Fixed frustum hull topology in the clip | Removes one QuickHull per clip call + near-plane epsilon hazard | done |
| 5 | Weld clipped points before re-hull; dedupe coplanar hull face planes | Shrinks re-hull input ~3-6x; fewer pass-2 clip planes | done |
| 6 | Allocation hygiene: QuickHull instance reuse, span-based hull input, persistent gather | Removes per-light-per-frame heap churn | done |
| 7 | Cap receiver silhouette plane count (conservative simplification) | Bounds caster filter cost as receiver complexity grows | pending |
| 8 | Temporal whole-fit skip via input revision tracking | Near-zero cost on static frames | pending |

Steps 1-6 are independent and low risk. Steps 7-8 are gated on profiling
results after 1-6. Re-profile between steps; stop when the fit is no longer
significant in the profile.

### Step 1: Profiling instrumentation

Only two Tracy zones exist inside the fit ("fit: filter casters + hull +
clip", "fit: optimize rotation (calipers)"); the receiver pipeline is
invisible inside the first. Add `ERHE_PROFILE_FUNCTION()` /
`ERHE_PROFILE_SCOPE()` zones so each stage shows separately:

- `erhe_math/math_util.cpp`: `calculate_bounding_convex_hull` (both
  overloads), `clip_convex_hull_points_by_planes`,
  `clip_convex_hull_points_to_frustum`,
  `build_shadow_caster_cull_planes_from_hull`.
- `erhe_scene/light_frustum_fit.cpp`: `build_receiver_cull_planes`
  (function zone), plus split the caster block into "fit: filter casters"
  and "fit: caster hull + clip".

The profile wrapper has no TracyPlot macro; if per-frame counters (N, M,
survivors, hull sizes, plane counts) turn out to be needed, add an
`ERHE_PROFILE_PLOT` wrapper to `erhe_profile` as a follow-up.

Verify: zones visible and correctly nested in a Tracy capture.

### Step 2: Center+extents AABB-vs-plane tests

`aabb_in_frustum` (first loop), `aabb_in_convex_volume` and
`first_rejecting_plane` test 8 corners per plane (8 dot4 each). The per-plane
"all corners outside" rejection is mathematically identical to the p-vertex
form: `dist(plane, center) + dot(abs(plane.xyz), extents) < 0` - 2 dots
instead of 8, exact same conservative result. Hits M casters x (<=13+S)
planes and N receivers x 6 planes per light.

Verify: identical cull results (same receiver/caster classifications in the
shadow-fit dump before/after on the same scene+camera), profile shows the
filter loops shrink.

### Step 3: Hoist light-independent receiver work out of the per-light loop

Everything in `build_receiver_cull_planes` up to and including the re-hulled
clipped receiver hull depends only on (receiver AABBs, view frustum). Compute
once per `Light_projections::apply()`, keep per light only the
light-direction-dependent part (`build_shadow_caster_cull_planes_from_hull`).
Also the prerequisite for step 8.

Verify: identical fit results with multiple shadow-casting directional
lights; receiver-pipeline zones appear once per frame instead of once per
light.

### Step 4: Fixed frustum hull topology in the clip

`clip_convex_hull_points_to_frustum` rebuilds the frustum hull with QuickHull
from the 8 corners on every call. `extract_frustum_corners` ordering is
fixed, so the 12 frustum triangles are a compile-time index table. Also
avoids QuickHull epsilon-merging the tiny near rectangle at close range
(latent robustness hazard).

Verify: identical clip output (dump receiver_clipped_points before/after);
one fewer QuickHull zone per clip in Tracy.

### Step 5: Weld before re-hull; dedupe coplanar face planes

The per-triangle Sutherland-Hodgman clip emits each shared vertex once per
incident triangle (observed: 30 clip points -> 12 unique hull vertices).
Weld (sort + unique with epsilon, or quantized hash) before the re-hull, and
deduplicate coplanar triangle planes before pass 2 clips the frustum against
them (box-like hull: 12 triangle planes, 6 unique faces). Also shrinks
`optimize_rotation`'s projection input on the caster side.

Verify: identical re-hulled hull (same vertex set in dump); re-hull zone
shrinks.

### Step 6: Allocation hygiene

- Persistent scratch buffers for the per-fit vectors (receiver points,
  hulls, clipped sets, plane lists, caster corners, projected points).
- Reuse a `quickhull::QuickHull<float>` instance (it keeps internal pools
  for exactly this); currently constructed per `calculate_bounding_convex_hull`
  call.
- Add a `std::span<const glm::vec3>` overload of
  `calculate_bounding_convex_hull`; `glm::vec3` and
  `quickhull::Vector3<float>` are layout-identical 12-byte PODs, so the
  input can be passed without the current double copy through
  `Point_vector_bounding_volume_source`.
- Persist the gather vectors in `Shadow_renderer::render`.

Verify: identical results; allocation count per frame drops (Tracy memory
zones or allocator stats).

Outcome: implemented as the span-based zero-copy hull input, a thread_local
QuickHull instance (its internal pools persist across calls), persistent
gather vectors in Shadow_renderer, and the receiver cache buffers from step
3. The remaining small per-fit vectors (plane lists, silhouettes, clip
scratch) were deliberately left as-is - threading a scratch context through
them is invasive and only worth it if profiling still shows allocator time
in the fit.

### Step 7: Cap receiver silhouette plane count

`receiver_filter_planes` is 1 + silhouette edge count, unbounded as receiver
geometry gets complex; every caster pays per plane. Conservatively simplify
the 2D silhouette hull to <= K edges (repeatedly remove the vertex whose
removal adds least area, replacing its two edges with their intersection
point - strictly outward, so the volume only grows). K ~ 8-12.

Verify: no caster culled that was previously kept (the cull may only get
looser); caster filter zone bounded with many receivers.

### Step 8: Temporal whole-fit skip

With camera, lights and all caster/receiver world AABBs unchanged since the
previous frame, reuse the previous fit output entirely. Requires cheap
change detection: revision counters bumped on node transform / hierarchy /
geometry change, compared per frame. Do the whole-fit skip only; finer
caching interacts with texel snapping and is not worth the risk until this
is measured.

Verify: fit zones disappear on static frames; any scene change (move object,
move camera, toggle visibility) re-fits on the next frame.

## Future candidates (not scheduled; decide from profiling after step 8)

- Polyhedral clip: clip the receiver hull as a connected half-edge mesh
  plane by plane, producing the exact intersection mesh; deletes the re-hull
  entirely and the degenerate-input risk of hulling coplanar point sets.
- Pure-2D receiver pipeline: project receiver corners along the light, 2D
  hull, intersect with the frustum's projected silhouette, cap at
  s = max(min_s(receivers), min_s(frustum)). Deletes all 3D hull/clip work;
  conservative but looser where receivers extend beyond the frustum along
  the light axis (silhouette(hull ^ frustum) is a subset of
  silhouette(hull) ^ silhouette(frustum)). A/B the culled-caster counts
  before committing.
- Per-light parallel fit (fork-join), SIMD/SoA caster filter.
- True O(h) rotating calipers in `calculate_min_area_obb_2d` (currently
  O(h^2); h is small, so only if profiling says otherwise).
