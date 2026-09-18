# Scene-level acceleration for the bvh raytrace backend

Stability: mostly stable

`Bvh_scene` (`src/erhe/raytrace/erhe_raytrace/bvh/bvh_scene.cpp`) keeps a
top-level acceleration structure (TLAS) over the children that have stayed
static, and traces the rest linearly. Only the `bvh` backend is affected;
`embree` has its own scene-level acceleration and `tinybvh` and `null` are
untouched, as are the `IScene` / `IGeometry` / `IInstance` interfaces.

How the editor uses this (`erhe_scene/mesh_raytrace.cpp`): each
`Raytrace_primitive` owns one `IInstance` and one `IScene` holding exactly one
`Bvh_geometry`, while the scene root owns one big `Bvh_scene` with one instance
per mesh primitive (thousands on Bistro). Without a TLAS every ray costs O(N)
instance transforms, matrix inverses and ray transforms before any bottom-level
BVH is touched, and `Scene_view::update_hover_with_raytrace()` fires several
masked rays per frame.

## Why hybrid, and why the build is asynchronous

A TLAS rebuilt every frame is not affordable:
`Mesh::handle_node_transform_update()` fires per primitive on every transform
change, so a moving subtree dirties the whole top level each frame and a
synchronous rebuild would land on the frame time.

- Every raytrace child records the tick at which it was last modified.
- Children static for at least `k_static_delay_ticks` are eligible for the TLAS.
- Recently modified children stay on the linear path, which is correct and cheap
  for small counts.
- The TLAS is built on a taskflow worker from an immutable snapshot. While a
  build is in flight, all children are traced linearly, so the frame never waits
  on a build.
- When the build finishes, the main thread validates it and swaps it in. The ray
  then traverses the TLAS for members plus a short linear pass for non-members.
- Modifying a TLAS member removes it from TLAS coverage immediately, so a stale
  TLAS is never consulted for a changed object.

This matches the workload: after a level load essentially everything is static
and lands in the TLAS, and only the handful of objects the user is dragging
(plus tool geometry) stay on the linear path.

## Child list and modification stamps

One indexed child list addresses both leaf kinds uniformly:

```cpp
class Bvh_scene_child {
public:
    Bvh_geometry* geometry          {nullptr};  // exactly one of these is set
    Bvh_instance* instance          {nullptr};
    uint64_t      last_modified_tick{0};
    bool          in_tlas           {false};    // covered by the current TLAS
};
```

Children are indexed by pointer (`std::unordered_map`), and erase swaps with the
last child: modification notifications resolve the child on every mesh transform
change of every frame, so a linear scan of the child list would be an
O(children) regression per moved mesh.

`m_tick` advances once per `commit()`. `commit()` is called once per view per
frame (`Scene_view::update_hover_with_raytrace`), so a tick is a frame in
practice; with several views sharing one scene the staleness threshold is
reached proportionally sooner, which is harmless. This keeps the frame number
out of the `IScene` interface. `attach()` stamps the new child with the current
tick, so freshly attached children start dynamic and settle into the TLAS on
their own.

## World bounds per child

- `Bvh_geometry::get_bbox()` is the bottom-level root bbox (empty or
  uncommitted gives an invalid bbox and the child is skipped).
- `Bvh_instance::get_bbox()` is the child scene's bbox transformed by
  `m_transform` (`erhe::math::Aabb::transformed_by()` with `to_bvh` / `from_bvh`).
- `Bvh_scene::get_bbox()` is the union of child bboxes.

Bounds are computed live through `get_bbox()` (with a recursion guard), so a
parent scene never depends on a child scene having been committed - and a
bottom-up commit would also advance the tick of child scenes shared between
parents. These are backend-local accessors.

## Modification notification

`Bvh_instance` and `Bvh_geometry` keep a parent-scene back-link list, maintained
by `Bvh_scene::attach` / `detach`. On `set_transform()`, `commit()` or detach,
the child calls `parent->on_child_modified(child)` for each parent, which:

1. sets `child.last_modified_tick = m_tick` (the child is dynamic again),
2. evicts the child from TLAS coverage when `in_tlas`,
3. propagates upward to *its* parents (a scene whose bounds changed changes its
   parents' bounds too), with a visited guard so a cyclic graph terminates.

`enable()`, `disable()` and `set_mask()` deliberately do not count as
modifications: the leaf callback calls `instance->intersect()` /
`geometry->intersect_instance()`, which already test `m_enabled` and the mask.
Mesh visibility toggles are frequent and must not evict anything.

## Build state machine

`Tlas_state` is `none`, `building` or `ready`:

- **none** - no TLAS; `intersect()` is linear over all children.
- **building** - a worker is building from a snapshot; `intersect()` is still
  linear over all children, and the worker touches no scene state.
- **ready** - `intersect()` traverses the TLAS for members, then linear over the
  rest.

`commit()` drives it on the main thread:

```
++m_tick
if (state == building && the async result is ready)
    take the result;
    discard it if m_build_generation changed while it was building
    (a member was modified or detached mid-build), else state = ready

if (state != building) {
    static_candidates = children with (m_tick - last_modified_tick) >= k_static_delay_ticks
    if (worth rebuilding) kick off an async build over static_candidates
}
```

"Worth rebuilding" means at least `k_min_tlas_children` candidates (below that
the linear path wins anyway, and this is what keeps the thousands of
one-geometry per-primitive scenes from ever allocating a TLAS), and either there
is no TLAS yet or the static-child count changed or a member was evicted since
the last build, and at least `k_rebuild_cooldown_ticks` have passed since the
last build *started* - counting from the start, not the finish, so a build that
keeps getting aborted cannot spin. The cooldown is the whole hysteresis: a
change-fraction threshold would leave newly settled children off the BVH
indefinitely in small scenes, for no real gain.

## Async build

The snapshot handed to the worker is pure data - `std::vector<BBox>`,
`std::vector<Vec3>` centers, and the candidate child pointers only as an
ordering key. The worker calls `bvh::v2::DefaultBuilder<Node>::build()`
(`Quality::Low`; box-level SAH quality matters far less than build time) and
returns the BVH, the member list and the build generation. It dereferences no
`Bvh_geometry` or `Bvh_instance` and reads no scene state, so nothing needs
locking.

Dispatch goes through `erhe::raytrace::get_executor()`, set once by the editor at
startup from `App_context::executor` through
`erhe::raytrace::set_executor(tf::Executor*)` and null by default. With a null
executor the build runs synchronously, which keeps the unit tests deterministic
and keeps headless and tool use working without an executor.

The async handshake is a `shared_ptr` task with an atomic `done` flag, polled in
`commit()`. The worker writes only into the task, which it co-owns, so a scene
destroyed mid-build needs no wait in the destructor.

## Invalidation

A TLAS is never used for something that has changed. Any modification to a
member sets `in_tlas = false` and nulls its slot in the TLAS member array; the
leaf callback skips null slots and the child is traced by the linear pass
instead, since it is now dynamic. The TLAS keeps a stale (now empty) box for
that slot, which costs a few wasted node tests and never a wrong result, so one
moving object does not throw away the other thousands. A rebuild is then needed
only to reclaim quality, which the hysteresis schedules in the background.

Detach does both: eviction plus nulling the pointer, so a destroyed object is
never dereferenced. `m_build_generation` is incremented on every modification
and detach, and a build result carrying a stale generation is dropped on
arrival.

## intersect()

`intersect()` and `intersect_instance()` are one private
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

1. **`bvh_ray.tmax` is updated from `ray.t_far` after every child hit.**
   `Bvh_geometry::intersect_instance` and `Bvh_instance::intersect` write
   `ray.t_far`; without propagating it the node culling never tightens and most
   of the TLAS benefit is lost.
2. **TLAS first, dynamic pass second.** Both narrow `ray.t_far`, so either order
   is correct, but the TLAS usually produces a near hit cheaply and tightens the
   linear pass.
3. **`intersect()` never mutates.** It neither builds nor swaps; a dirty scene
   just uses the linear path. That keeps `intersect()` callable from several
   threads at once and makes a missing `commit()` a performance bug, not a data
   race.

`Bvh_instance::intersect()` returns false for a null instanced scene, which can
happen once an instance outlives its scene.

## Threading contract (stated in the header)

- Mutation - `attach`, `detach`, `set_transform`, `commit` - is single-threaded.
  The editor funnels scene mutation through `Scene_commit_queue` on the main
  thread.
- `intersect()` is read-only and may run concurrently with other `intersect()`
  calls, but not concurrently with mutation.
- The background build sees only its immutable snapshot; the only cross-thread
  state is the task handshake and an atomic generation counter.

## Tests

`src/erhe/raytrace/test/` covers the semantics that must not move
(`test_scene`, `test_geometry`, `test_instance`, `test_hierarchy`,
`test_masking`). With no executor injected, builds are synchronous, so every
case stays deterministic. The scene-acceleration cases:

- **Many-children scene** (64 boxes on a grid) committed enough times to go
  static - the same closest hit as a linear reference, for rays hitting the
  first, middle, last and no box.
- **Closest-hit ordering** - overlapping boxes along the ray, both attach
  orders (this guards the `bvh_ray.tmax` update).
- **Static promotion** - a child is not in the TLAS before
  `k_static_delay_ticks` and is after; the results are identical either way.
- **Modify a member** - move an instance that is in the TLAS, commit, trace:
  the hit follows the moved instance.
- **Detach a member** - the detached child is not hit and nothing dereferences
  it.
- **Attach after the TLAS is ready** - the new child is hit immediately via the
  dynamic path, before it ever becomes static.
- **Trace without commit** - still the correct hit, via the linear path.
- **Async path** - inject a real `tf::Executor`, commit in a loop until the
  state reaches ready, and assert the results are identical at every commit
  along the way (tracing during `building` is correct). Drive it by commit
  count, never by sleeping.
- **Nested scenes** - `test_hierarchy` with each level above
  `k_min_tlas_children`.

## Measurement

`ERHE_PROFILE_FUNCTION` covers `Bvh_scene::intersect`, `commit`, the snapshot
and the worker build, so build cost shows on the worker and not on the frame.
Two properties to confirm on a large scene: no build time lands on the main
thread, and hover cost drops from O(N) instances to the TLAS path. On the
default scene the root scene builds one BVH (`Bvh_scene rt_root_scene scene BVH
built for 7 children`) and the per-primitive scenes build none; moving one
object while raycasting leaves the rest of the BVH standing (the run shows
member evictions, not whole-BVH invalidations).

## Out of scope

- The `embree` and `tinybvh` backends.
- Skinned meshes: their BVH is rest-pose only and they are picked by the ID
  renderer (`Raytrace_node_mask::skinned`).
- Refit (`bvh::v2::Bvh::refit()`) as an alternative to rebuilding: with the
  static / dynamic split, members are by definition not moving, so there is
  nothing to refit. Revisit only if the split's constants turn out not to hold.

## Future work

- [plans/raytrace.md](../plans/raytrace.md) - measuring the hover path on a large
  scene and tuning the static-delay and cooldown constants.
