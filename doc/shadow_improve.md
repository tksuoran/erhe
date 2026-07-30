# Shadow fit optimization plan

Plan for optimizing the tight shadow frustum fit pipeline
(`Light::tight_directional_light_projection_transforms()` and its inputs),
derived from the 2026-06-10 pipeline review; refreshed 2026-07-29 after
profiling showed `calculate_bounding_convex_hull` still significant post
steps 1-6 (steps 9-10 and the QuickHull-related future candidates came out of
that round). See `doc/shadows.md` for how the pipeline works; this document
only tracks performance work.

**Scope.** All three light types now cast shadows, but this plan covers the
**directional** tight fit only. Spot lights use a fixed perspective projection
from the light pose, and point lights use an omnidirectional cube map
(`point_light_projection_transforms` + the `Shadow_renderer` point-cube pass; see
`doc/point_light_shadows.md`) -- neither runs the frustum fit, so the steps below
do not apply to them. Point-shadow performance has a different shape (six full
scene re-rasterizations per shadow-casting point light, no caster culling); its
optimization candidates are listed under "Future candidates" and are not yet
scheduled.

Workflow: steps are executed one at a time, in order. After each step is
implemented and builds, the user verifies (correctness in the editor and/or
effect in the Tracy profile) before the step is committed. Update the Status
column as steps land.

## Cost model

Per frame, per shadow render node, with N receivers, M casters, L tight-fitted
directional lights, h = receiver hull vertex count, S = receiver silhouette
edge count:

The table reflects the current state (steps 1-6, 9, 10 in place). M' =
surviving casters after the filter.

| Stage | Cost | Runs |
|---|---|---|
| AABB gather (`get_aabb_world` per mesh) | O(N) corner transforms | 1x per frame |
| Receiver in-frustum filter + corner gather | N x 6 planes, center+extents | 1x per pass (cross-light cache, step 3) |
| Receiver corner hull | QuickHull over 8N points | only when the corner set changed (step 10) |
| Hull/frustum clip + re-hull | SH both ways + QuickHull over the welded set | only when the corner set or frustum changed (step 10) |
| Silhouette + sweep planes | 2D hull (sort) | per light |
| Caster filter | M x (<=12 + 1+S) planes, center+extents | per light |
| Caster per-box clip | boundary boxes: 12 triangles x <=6 planes SH; interior boxes: corner append | per light (step 9) |
| Calipers projection + 2D hull | sort over the fit point set | per light (optimize_rotation) |

The remaining QuickHull runs are the two receiver-side ones, at most once per
pass and skipped entirely on repeated inputs; the per-light caster stage is
linear in M' with no hull build (step 9).

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
| 9 | Per-box caster clip (deletes the per-light caster QuickHull) | Caster stage linear in survivors; equal-or-tighter fit | done |
| 10 | Receiver-cache temporal reuse (corner-set + frustum fingerprint) | Receiver QuickHulls run only when their inputs change | done |

Steps 1-6 are independent and low risk. Steps 7-8 are gated on profiling
results after 1-6. Re-profile between steps; stop when the fit is no longer
significant in the profile. Steps 9-10 came from the 2026-07-29 hull
profiling round.

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

Note: step 10 already skips the receiver hulls on unchanged inputs, including
across camera movement (the fingerprint compares the actual point set, not
scene revisions). What this step adds on fully static frames is skipping the
remaining per-light work: caster filter, per-box clip, calipers, box assembly.

### Step 9: Per-box caster clip

The caster stage built a QuickHull over all 8M' surviving corners only so
F_shadow needed to clip one convex body instead of many. Replaced with per-box
clipping: each surviving AABB is its own convex hull with fixed topology (8
corners plus a constant 12-triangle index table, the same trick as the step 4
frustum table), clipped to the **open** F_shadow with
`clip_convex_hull_points_by_planes`, and the fit uses the union of the clipped
point sets. Two refinements:

- Fast path: a box entirely inside the volume (center+extents test per plane)
  clips to itself; its 8 corners are appended directly, no SH run.
- F_main corners contained in a box are appended (point-in-AABB test, dedup
  bitmask): the surface clip cannot produce F_shadow vertices interior to the
  box - a box strictly containing the whole volume even clips to nothing.
  This is the per-box analog of the old inside-the-hull corner re-insertion.

Correctness and tightness: caster geometry lies in the union of the boxes, and
union(box ^ F_shadow) is a subset of hull(all corners) ^ F_shadow - the hull
additionally covered the empty bridge regions between separated casters - so
the fit still covers every caster and can only get tighter. The caster hull
now exists only as a debug visualization, built when collect_debug is on.

Tracy zones: "fit: filter casters + hull + clip" renamed to "fit: casters",
with children "fit: filter casters" and "fit: clip casters".

Verify: no missing or clipped shadows with fit_to_casters on; the fit box in
the shadow-fit dump is equal or smaller on the same scene+camera; the caster
QuickHull zone is gone from Tracy; the "fit: clip casters" zone stays small
(most boxes should take the fast path).

### Step 10: Receiver-cache temporal reuse

`ensure_receiver_cache` gathers the in-frustum receiver corner set into a
scratch and compares it (exact float equality - the corners are copied from
the same AABB source every pass) against the previous pass's set:

- Corner set unchanged: the corner hull depends on nothing else, so the
  QuickHull over 8N points is skipped and the stored hull reused.
- Corner set and view frustum corners both unchanged: the clip and re-hull
  inputs are also identical, so the whole cached result (clipped_hull,
  hull_valid) stands and the function returns after the gather.

The common camera-only movement case then pays for the in-frustum filter and
the comparison; the hulls rerun only on frames where the surviving receiver
set actually changes (cull boundary crossings). Reuse is disabled while
collect_debug is on (the debug vectors must be refilled each pass).

Verify: static scene + moving camera shows the receiver hull zone only on
frames where the in-frustum receiver set changes; fully static shows no
receiver clip / re-hull zones either; identical fit results with the reuse
paths exercised (same shadow-fit dump across a camera pan).

## Future candidates (not scheduled; decide from profiling after step 8)

All remaining QuickHull work is on the receiver side (step 9 removed the
caster-side hull), so the QuickHull-targeted candidates below matter only
when the receiver hulls still show after step 10.

- QuickHull library per-call overhead (the library is vendored in
  `src/quickhull`, freely modifiable): `createConvexHalfEdgeMesh` ends with
  `m_indexVectorPool.clear()`, discarding the warm per-face index-vector pool
  the thread_local instance exists to keep; and the `ConvexHull` result
  object allocates per call a fresh un-reserved vertex buffer, a
  `vector<bool>`, a face stack and an `unordered_map` vertex remap (one node
  allocation per hull vertex) - all copied out by erhe immediately and
  thrown away. Keep the pool warm across calls; add an entry point that
  walks `MeshBuilder` faces (skipping disabled ones - the DFS adjacency
  order is irrelevant to erhe's consumers) directly into
  `erhe::math::Convex_hull` with a persistent flat remap array. Also remove
  the `std::cerr` horizon-edge failure print (frame-spike hazard).
- Interior-point prune before the receiver hull (Akl-Toussaint at box
  granularity): gather extreme corners along ~14 fixed directions (axes +
  diagonals), hull those <= 14 points, then drop every receiver AABB fully
  inside that inner polytope with the p-vertex test - one test per box, not
  per corner - before expanding survivors to corners. QuickHull's own
  initial tetrahedron already discards points inside it, but on flat scenes
  (ground plane) that tetrahedron is thin and nearly useless; the
  multi-direction polytope is strong exactly there.
- QuickHull epsilon: `calculate_bounding_convex_hull` passes 1e-6 where the
  library float default is 1e-4. Tighter epsilon keeps near-coplanar corner
  grids (boxes on a ground plane, shared wall heights) from merging into
  faces, inflating face and iteration counts. Trying 1e-4 bounds the hull
  under-coverage at ~eps x scene scale (1 cm at 100 m), absorbed laterally
  by the 2-texel snap padding - but A/B with the shadow-fit dump before
  trusting it.
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
  O(h^2); h is small, so only if profiling says otherwise). Note the
  calipers *input* point set grew with step 9 (union of per-box clip points
  instead of the clipped whole-set hull); the 2D hull sort absorbs that, and
  h itself stays small.

### Point-light cube shadows (separate path, not part of the directional plan)

The cube path re-rasterizes the whole shadow-caster set into all six faces of
every shadow-casting point light with no culling, which is the dominant point
shadow cost. Candidates, in rough priority order (none scheduled; gate on a
Tracy capture with point shadows enabled):

- **Per-face caster culling.** Each cube face is a 90-degree frustum; cull caster
  AABBs against it (and against the light range sphere) so a face only draws the
  casters that can fall in it. Reuses `aabb_in_frustum` / the p-vertex test from
  step 2.
- **Skip empty faces / lights.** A face (or a whole cube) with no surviving
  casters can clear-only; a light whose range sphere contains no casters needs no
  cube at all.
- **Range/contribution cull.** Casters fully outside the light range, or whose
  shadow cannot reach any receiver, never need rasterizing.
- **Shared depth scratch serialization.** All six faces reuse one 2D depth
  texture, so the passes serialize on a write-after-write barrier. Per-cube (or
  per-face) depth would let the faces overlap, at a memory cost.
- **Resolution / count budget.** `point_shadow_resolution` x
  `point_shadow_light_count` R32F cube arrays are heavy (the High preset is
  hundreds of MB); revisit defaults and consider per-light resolution by
  screen-space size.
