# Plan: scene-level acceleration for the `bvh` raytrace backend

## Problem

`src/erhe/raytrace/erhe_raytrace/bvh/bvh_scene.cpp` has no acceleration structure.
`Bvh_scene::commit()` is an empty `// TODO`, and both `intersect()` and
`intersect_instance()` loop linearly over **every** attached instance and geometry:

```cpp
for (const auto& instance : m_instances) { instance->intersect(ray, hit); }
for (const auto& geometry : m_geometries) { geometry->intersect_instance(ray, hit, nullptr); }
```

Only `Bvh_geometry` builds a `bvh::v2::Bvh` (bottom level, over triangles).

How the editor actually uses this (`erhe_scene/mesh_raytrace.cpp`):

- Each `Raytrace_primitive` owns one `IInstance` + one `IScene` holding exactly **one**
  `Bvh_geometry`.
- The scene root owns **one** big `Bvh_scene` with one instance per mesh primitive
  (thousands on Bistro).

So every ray costs O(N) instance transforms + matrix inverses + ray transforms before
any BVH is touched, and `Scene_view::update_hover_with_raytrace()` fires several masked
rays per frame.

## Constraint that shapes the design

A TLAS rebuilt every frame is not affordable: `Mesh::handle_node_transform_update()`
fires per primitive on every transform change, so any moving subtree would dirty the
whole top level each frame, and a synchronous rebuild would land directly on the frame
time.

So the design is **hybrid, with an asynchronous build**:

- Every raytrace child records the tick at which it was last modified.
- Children that have been **static for at least `k_static_delay_ticks`** are eligible
  for the TLAS.
- Children that were modified recently stay on the **current linear path**, which is
  already correct and cheap for small counts.
- The TLAS is built on a **taskflow worker** from an immutable snapshot. While a build
  is in flight, *all* children are traced linearly — exactly today's behaviour, so the
  frame never waits on a build.
- When the build finishes, the main thread validates it and swaps it in. From then on
  the ray traverses the TLAS for members, plus a short linear pass for non-members.
- Modifying a TLAS member removes it from TLAS coverage immediately (see
  *Invalidation*), so a stale TLAS is never consulted for a changed object.

This matches the actual workload: after a level load essentially everything is static
and lands in the TLAS; only the handful of objects the user is dragging (plus tool
geometry) stay on the linear path.

## Design

### Child list and modification stamps

Replace the two parallel vectors with one indexed child list so the TLAS can address
both leaf kinds uniformly:

```cpp
class Bvh_scene_child {
public:
    Bvh_geometry* geometry          {nullptr};  // exactly one of these is set
    Bvh_instance* instance          {nullptr};
    uint64_t      last_modified_tick{0};
    bool          in_tlas           {false};    // covered by the current TLAS
};
std::vector<Bvh_scene_child> m_children;
uint64_t m_tick{0};   // incremented once per commit()
```

`m_tick` advances once per `commit()`. `commit()` is called once per view per frame
(`Scene_view::update_hover_with_raytrace`), so a tick is a frame in practice; with
several views sharing one scene the staleness threshold is reached proportionally
sooner, which is harmless. This keeps the frame number out of the `IScene` interface —
no interface change, no plumbing through the editor.

`attach()` stamps the new child with the current tick, so freshly attached children
start dynamic and settle into the TLAS on their own.

### World bounds per child

- `Bvh_geometry::get_bbox()` → `m_bvh.get_root().get_bbox()` (empty/uncommitted →
  invalid bbox, child skipped).
- `Bvh_instance::get_bbox()` → child scene's bbox transformed by `m_transform`, via
  `erhe::math::Aabb::transformed_by()` (already in `erhe_math/aabb.hpp`) with
  `to_bvh`/`from_bvh`.
- `Bvh_scene::get_bbox()` → cached union of child bboxes, refreshed in `commit()`.

These are backend-local accessors; `IGeometry`/`IInstance`/`IScene` are untouched, so
the `embree`, `tinybvh` and `null` backends are unaffected.

### Modification notification

`Bvh_instance` and `Bvh_geometry` gain a parent-scene back-link list, maintained by
`Bvh_scene::attach`/`detach`. On `set_transform()`, `commit()`, or detach, the child
calls `parent->on_child_modified(child)` for each parent, which:

1. sets `child.last_modified_tick = m_tick` (child is dynamic again),
2. evicts the child from TLAS coverage if `in_tlas` (see *Invalidation*),
3. propagates upward to *its* parents (a scene whose bounds changed changes its
   parents' bounds too), with a visited guard so a cyclic graph terminates.

`enable()`/`disable()`/`set_mask()` deliberately do **not** count as modifications: the
leaf callback calls `instance->intersect()` / `geometry->intersect_instance()`, which
already test `m_enabled` and the mask. Mesh visibility toggles are frequent and must
not evict anything.

### Build state machine

```cpp
enum class Tlas_state { none, building, ready };
```

- **none** — no TLAS. `intersect()` = linear over all children (today's behaviour).
- **building** — a worker is building from a snapshot. `intersect()` still linear over
  all children. The worker touches no scene state.
- **ready** — `intersect()` traverses the TLAS for members, then linear over the rest.

`commit()` (main thread) drives it:

```
++m_tick
// child scenes are not committed from here: bounds are computed live in
// get_bbox(), so a parent never depends on a child scene's commit

if (state == building && the async result is ready)
    take the result;
    discard it if m_build_generation changed while it was building
    (a member was modified or detached mid-build), else state = ready

if (state != building) {
    static_candidates = children with (m_tick - last_modified_tick) >= k_static_delay_ticks
    if (worth rebuilding) kick off an async build over static_candidates
}
```

"Worth rebuilding" — hysteresis, so the build does not churn:

- at least `k_min_tlas_children` candidates (below that the linear path wins anyway —
  this is also what keeps the thousands of one-geometry per-primitive scenes from ever
  allocating a TLAS),
- and either there is no TLAS yet, or the static-child count changed or a member was
  evicted since the last build,
- and at least `k_rebuild_cooldown_ticks` since the last build *started* (counting from
  the start, not the finish, so a build that keeps getting aborted cannot spin).

### Async build

The snapshot handed to the worker is **pure data** — `std::vector<BBox>`,
`std::vector<Vec3>` centers, and the candidate child pointers *only* as an ordering
key. The worker calls `bvh::v2::DefaultBuilder<Node>::build()` (`Quality::Low`; box
level SAH quality matters far less than build time) and returns `{Bvh, member list,
build generation}`. It dereferences no `Bvh_geometry`/`Bvh_instance` and reads no
scene state, so no locking is needed anywhere.

Dispatch through taskflow:

```cpp
tf::Executor* executor = erhe::raytrace::get_executor();      // may be nullptr
if (executor != nullptr) {
    m_build_future = executor->async([snapshot = std::move(snapshot)]() { ... });
} else {
    build synchronously                                        // tests, headless tools
}
```

`erhe_raytrace` does not have an executor today. Add a tiny injection point
(`erhe::raytrace::set_executor(tf::Executor*)` / `get_executor()`), set once by the
editor at startup from `App_context::executor`, defaulting to `nullptr`. The null case
building synchronously keeps the unit tests deterministic and keeps headless/tool use
working without an executor. `erhe_raytrace/CMakeLists.txt` gains `Taskflow` in its
link libraries (as `erhe_gltf` already does).

Completion is polled in `commit()` (`m_build_future.wait_for(0s)`), never waited on.
The destructor must wait for an in-flight build before destroying the snapshot.

### Invalidation

The requirement is that a TLAS is never used for something that has changed. Two
mechanisms, introduced in that order:

1. **Whole-TLAS invalidation** (step 5, simple and obviously correct): any modification
   to a member sets `state = none` and increments `m_build_generation`; everything goes
   back to the linear path until the next background build completes.
2. **Per-member eviction** (step 7, the optimisation): the modified member gets
   `in_tlas = false` and its slot in the TLAS member array is nulled. The leaf callback
   skips null slots; the child is traced by the linear pass instead, since it is now
   dynamic. The TLAS keeps a stale (now empty) box for that slot — that costs a few
   wasted node tests, never a wrong result. One moving object therefore does not throw
   away the other 5000. A rebuild is then only needed to reclaim quality, which the
   hysteresis rules above schedule in the background.

Detach of a member does both: eviction plus nulling the pointer, so a destroyed object
is never dereferenced.

`m_build_generation` is incremented on every modification and detach; a build result
carrying a stale generation is dropped on arrival.

### intersect()

`intersect()` and `intersect_instance()` collapse into one private
`intersect_children(Ray&, Hit&, Bvh_instance* in_instance)`:

```
if (state == ready) {
    bvh_ray from ray
    m_tlas.intersect<false, false>(bvh_ray, root, stack, leaf_fn)
      leaf_fn: for each prim in [begin, end):
          child = m_tlas_members[should_permute ? i : m_tlas.prim_ids[i]]
          skip if evicted (null)
          hit = child.instance ? instance->intersect(ray, hit)
                               : geometry->intersect_instance(ray, hit, in_instance)
          if (hit) { is_hit = true; bvh_ray.tmax = ray.t_far; }   // narrow for culling
          return false;                                           // closest hit: never stop early
}
linear pass over children with !in_tlas   // dynamic set, or all children when not ready
```

Three things this must get right:

1. **`bvh_ray.tmax` must be updated from `ray.t_far` after every child hit** —
   `Bvh_geometry::intersect_instance` and `Bvh_instance::intersect` write `ray.t_far`.
   Without this the node culling never tightens and most of the TLAS benefit is lost.
2. **TLAS first, dynamic pass second.** Both narrow `ray.t_far`, so either order is
   correct, but the TLAS usually produces a near hit cheaply and tightens the linear
   pass.
3. **`intersect()` never mutates.** It neither builds nor swaps; a dirty scene just
   uses the linear path. That keeps `intersect()` callable from several threads at
   once and makes a missing `commit()` a performance bug, not a data race.

### Threading contract (write it in the header)

- Mutation — `attach`, `detach`, `set_transform`, `commit` — is single-threaded. The
  editor already funnels scene mutation through `Scene_commit_queue` on the main
  thread.
- `intersect()` is read-only and may run concurrently with other `intersect()` calls,
  but not concurrently with mutation.
- The background build sees only its immutable snapshot; the only cross-thread state
  is the future and an atomic generation counter.

### Latent bug fixed along the way

`Bvh_scene::intersect_instance()` with `in_instance == nullptr` traverses instances but
**skips geometries entirely** — inconsistent with `intersect()`. Unreachable today
(`Bvh_instance::intersect` always passes `this`), but the unified `intersect_children`
removes the asymmetry rather than preserving it.

## Steps

Each step is edit → build → independent review → test → commit.

1. **Bounds plumbing.** `Bvh_geometry::get_bbox()`, `Bvh_instance::get_bbox()`,
   `Bvh_scene::get_bbox()` + cached union. No behaviour change.
2. **Unified child list.** Replace `m_geometries`/`m_instances` with `m_children`;
   collapse `intersect`/`intersect_instance` into `intersect_children`, linear only.
   Fixes the `in_instance == nullptr` asymmetry. Existing tests must pass unchanged —
   the no-regression checkpoint.
3. **Parent back-links, ticks, modification stamps.** `on_child_modified` with upward
   propagation and cycle guard; `m_tick` advancing in `commit()`. Still fully linear;
   this step is pure bookkeeping and is tested by asserting which children are
   classified static/dynamic after a sequence of commits and transform changes.
4. **Synchronous TLAS.** Build in `commit()` over the static set (no executor yet),
   traverse it in `intersect_children`, linear pass for the rest. This is where the
   traversal correctness (`bvh_ray.tmax`, closest hit, masking) gets nailed down with
   the executor out of the picture.
5. **Whole-TLAS invalidation** on modification/detach.
6. **Async build.** `erhe::raytrace::set_executor` + taskflow dispatch, `none →
   building → ready` state machine, generation validation on completion, destructor
   waits. Editor sets the executor at startup. Null executor keeps the synchronous
   path from step 4, so every earlier test still exercises the same code.
7. **Per-member eviction** replacing whole-TLAS invalidation, plus the rebuild
   hysteresis (`k_min_tlas_children`, `k_rebuild_fraction`, `k_rebuild_cooldown_ticks`).
8. **Measurement and tuning.** Tune `k_static_delay_ticks` and the hysteresis
   constants from real numbers, then strip temporary instrumentation.

## Tests

`src/erhe/raytrace/test/` already covers the semantics that must not move
(`test_scene`, `test_geometry`, `test_instance`, `test_hierarchy`, `test_masking`) —
run at every step. With no executor injected, builds are synchronous, so all of these
stay deterministic. New cases:

- **Many-children scene** (64 boxes on a grid) committed enough times to go static —
  same closest hit as a linear reference, for rays hitting the first, middle, last and
  no box.
- **Closest-hit ordering** — overlapping boxes along the ray, both attach orders
  (guards the `bvh_ray.tmax` update).
- **Static promotion** — a child is not in the TLAS before `k_static_delay_ticks` and
  is after; results identical either way.
- **Modify a member** — move an instance that is in the TLAS, commit, trace: the hit
  follows the moved instance (this is the eviction/invalidation test, and the one that
  would catch a stale TLAS).
- **Detach a member** — detached child is not hit, and nothing dereferences it.
- **Attach after the TLAS is ready** — new child is hit immediately via the dynamic
  path, before it ever becomes static.
- **Trace without commit** — still the correct hit, via the linear path.
- **Async path** — inject a real `tf::Executor`, commit in a loop until the state
  reaches ready, assert the results are identical at every commit along the way
  (i.e. tracing during `building` is correct). No sleeps; drive it by commit count.
- **Nested scenes** — `test_hierarchy` with each level above `k_min_tlas_children`.

## Measurement

- `ERHE_PROFILE_FUNCTION` is already in `Bvh_scene::intersect`; add scopes for
  `commit`, for the snapshot, and for the worker build (so build cost shows up on the
  worker, not the frame).
- Frame-time check: the whole point is that no build lands on the main thread. Verify
  in the profiler that Bistro shows TLAS build time on a worker only, and that hover
  cost drops from O(N) instances to the TLAS path.
- Verify the per-primitive scenes (1 geometry each) never allocate a TLAS.
- Verify that dragging one object does not throw away the whole TLAS once step 7
  lands (member count should stay high while the dragged object is dynamic).

## Out of scope

- `embree` and `tinybvh` backends (embree has its own scene-level acceleration).
- Skinned meshes — their BVH is rest-pose only and they are picked by the ID renderer
  (`Raytrace_node_mask::skinned`); unchanged here.
- Any change to the `IScene`/`IGeometry`/`IInstance` interfaces.
- Refit (`bvh::v2::Bvh::refit()` exists) as an alternative to rebuilding: with the
  static/dynamic split, members by definition are not moving, so there is nothing to
  refit. Left as a note in case the split's constants turn out not to hold.


## Status: implemented

All eight steps landed, one commit each, tests green at every step
(`src/erhe/raytrace/test/`, 44 tests). What ended up differing from the plan above:

- **No recursive child-scene commit.** Bounds are computed live through
  `Bvh_scene::get_bbox()` (recursion guarded), so a parent scene never depends on a
  child scene having been committed. The plan's bottom-up commit would also have
  advanced the tick of child scenes shared between parents.
- **Async handshake is a `shared_ptr` task with an atomic `done` flag**, polled in
  `commit()`, rather than a future. The worker writes only into the task, which it
  co-owns, so a scene destroyed mid-build needs no wait in the destructor.
- **Children are indexed by pointer** (`std::unordered_map`), and erase swaps with the
  last child. Modification notifications resolve the child on every mesh transform
  change of every frame, so the linear scan the child list started with would have been
  an O(children) regression per moved mesh.
- **Hysteresis is the cooldown alone.** A change-fraction threshold would have left
  newly settled children off the BVH indefinitely in small scenes, for no real gain.
- `Bvh_instance::intersect()` now returns false for a null instanced scene instead of
  dereferencing it, which can happen once an instance outlives its scene.

Verified in the editor (Vulkan Debug) over its MCP server:

- `Bvh_scene rt_root_scene scene BVH built for 7 children` — the root scene does build
  one, and the per-primitive scenes do not.
- 120 raycast sweeps over the default scene, well past the static delay and the
  cooldown: identical hits, mesh names and distances throughout.
- 40 iterations of moving a cube while raycasting: hits follow the cube and nothing
  hits the position it vacated, with 2 builds and 0 whole-BVH invalidations over the
  whole run — one moving object evicts itself and leaves the rest of the BVH standing.

Still open: measuring the hover-path win on a large scene (Bistro) in the profiler, and
tuning `k_static_delay_ticks` / `k_rebuild_cooldown_ticks` from those numbers.
