# Primitive_shape build and state locks

Stability: stable

`erhe::primitive::Primitive_shape` carries **two** mutexes, because the lock
has two unrelated jobs and sharing one made every short state read on the main
thread wait for a worker's multi-second build.

- **`m_build_mutex`** is held for the duration of the expensive idempotent
  builds. Its only job is dedup: a `Primitive` shared by many meshes (every
  chess piece of an imported set, every glTF instance) is converted once. Long
  hold times are fine and intended - `prepare_real_raytrace()` holds it across
  `mesh_from_triangle_soup()` plus `Geometry::process()` (itself serialized
  globally on `erhe::geometry::geogram_lock()`) and the subsequent BVH build,
  which is seconds per large shape in Debug.
- **`m_state_mutex`** is held only for short reads and writes of the shape's
  mutable slots (`m_geometry`, `m_element_mappings`, `m_raytrace`,
  `m_pending_raytrace`, `m_retired_proxy_raytrace`, `m_renderable_mesh`,
  `m_pending_buffer_mesh`, `m_normal_style`). **Never held across a build.**

**Lock order: `Item_host::item_host_mutex` -> `m_build_mutex` ->
`m_state_mutex`.** The main thread genuinely takes the item-host lock and then
a shape lock (the commit closures in
`async_raytrace_kickoff_operation.cpp`). What makes that deadlock-free is that
no `Primitive_shape` method ever touches scene state - `primitive.cpp`
references no `Item_host` - so the reverse edge does not exist. Keep it that
way.

## Which lock each method takes

| method | lock |
| --- | --- |
| `make_geometry()` | build around the build; publish under state |
| `make_raytrace()` | build around the build; assign `m_raytrace` under state |
| `make_raytrace(const GEO::Mesh&)` | build around the build (it has external callers - instanced cubes built from a `Buffer_mesh` with no `Geometry` of their own); the build step itself is `make_raytrace_build_locked()` |
| `make_raytrace_proxy()` | build around the build; publish under state |
| `prepare_real_raytrace()` | build across geometry + BVH build; early-outs and publish under state |
| `commit_real_raytrace()` | **state only** |
| `prepare_geometry_buffer_mesh()` | build across the build; early-outs and publish under state |
| `commit_geometry_buffer_mesh()` | **state only** |
| `get_mesh_facet_from_triangle()` | **state only** |
| `has_raytrace_triangles()`, `has_real_raytrace()`, `has_buffer_mesh_triangles()`, `has_edge_lines()` | state |
| `Primitive_render_shape::make_buffer_mesh()` (both overloads) | build around the build; publish `m_renderable_mesh` + `m_element_mappings` under state |

Because the commits take only the short state lock, the main thread's
`Scene_commit_queue::flush()` - which runs `commit_real_raytrace()` /
`commit_geometry_buffer_mesh()` **while holding the scene's
`item_host_mutex`** - cannot stall behind a worker's build.

## Locking lives in the public entry points only

`std::mutex` is not recursive and both locks have re-entrant call paths, so
all shared work lives in private `*_build_locked()` / `*_state_locked()`
helpers that assume the corresponding lock is already held:

- `make_geometry_build_locked()` for the geometry build,
  `has_real_raytrace_state_locked()` / `has_edge_lines_state_locked()` for the
  state re-entrancy.
- `make_raytrace()` tail-calls the `GEO::Mesh` overload's build step; giving
  both the build lock self-deadlocks, so that step is
  `make_raytrace_build_locked()`.
- `make_buffer_mesh(const Build_info&, Normal_style)` falls through to
  `make_buffer_mesh(const Buffer_info&)`; same trap, same resolution - one
  private `make_buffer_mesh_build_locked()` per overload.

A build writes into **locals** and publishes them under the state lock; it
never hands a member out as an out-parameter. That is what lets a failed build
leave a good raytrace or buffer mesh in place instead of overwriting it with
an empty one.

## Check and publish

Check-and-build must not be split naively - check state, release, take the
build lock, build - or two workers on a shared shape both build, destroying
the dedup. The required sequence, for **every** method that takes the build
lock:

1. take `m_build_mutex`
2. take `m_state_mutex`, run the authoritative early-out checks
   (`has_real_raytrace_state_locked()`, `m_pending_raytrace` /
   `m_pending_buffer_mesh` present), release it
3. build into locals with no state lock held
4. take `m_state_mutex`, **re-check** the same conditions, drop the freshly
   built result if another path won, otherwise publish

A pre-build-lock check is allowed as a fast path, never as the authoritative
one.

Step 4's re-check is not merely defensive. `commit_real_raytrace()` retires
the installed raytrace only when it `is_proxy()`, and that retirement exists
because live `Raytrace_primitive`s hold a raw `IGeometry*`. Installing a stale
second pending after a commit would free an `IGeometry` that meshes still
point at. Steps 1-2 make that unreachable - a pending can only be created
under the build lock - but the invariant would then be a property of the code
shape rather than of the lock, and the re-check pins it where an implementer
can see it.

`prepare_geometry_buffer_mesh()` carries an already-committed early-out
(`has_edge_lines_state_locked()`, mirroring `prepare_real_raytrace()`'s).
Without it a second task sharing the shape rebuilds the whole buffer mesh
after the first commit cleared `m_pending_buffer_mesh`, and commits a
redundant second swap.

## Publish-once for `m_geometry`

`m_geometry` is written in exactly one place after construction -
`make_geometry_build_locked()`, under the locks, only when previously null -
and is never cleared or replaced. Geometry operations build a **new**
`Primitive` and swap primitive lists rather than rebinding a shape's geometry,
and `commit_geometry_buffer_mesh()` touches only the buffer-mesh slots. The
invariant is about the **slot**, not the pointee: glTF import does call
`geometry->process()` in place on an already-published `Geometry`, so this is
not an immutability claim about the `Geometry` object.

`std::atomic<bool> m_geometry_published` records it:

- constructors set it iff a non-null geometry was passed;
- move construction and move assignment carry the source's value and **clear
  the source's** (a moved-from `m_geometry` is null, and leaving the source
  flag set would publish a null). Move operations take no locks and are
  construction-time only;
- `make_geometry_build_locked()` stores `true` with release ordering
  immediately after assigning `m_geometry`, inside the `m_state_mutex`
  critical section.

The mappings publish with it, in the **same** critical section, so the
mappings and the geometry they index become visible together:

```cpp
{
    const std::lock_guard<std::mutex> state_lock{m_state_mutex};
    m_element_mappings = std::move(local_mappings);
    m_geometry         = std::move(local_geometry);
    m_geometry_published.store(true, std::memory_order_release);
}
```

A geometry publish can never clobber mappings installed by a commit:
`commit_geometry_buffer_mesh()` requires a prior `prepare`, which requires a
published geometry, so the publish always precedes any commit. The
mappings-empty-on-entry `ERHE_VERIFY`s belong in the **step-2 state-lock
critical section**, not merely at the top of the function:
`m_element_mappings` is a state-lock-protected slot that
`commit_geometry_buffer_mesh()` writes, so asserting on it under the build
lock alone would be exactly the unsynchronized member read this design
removes.

## The non-blocking reader

`get_geometry_const()` never builds and never blocks; it returns null until
publish:

```cpp
auto Primitive_shape::get_geometry_const() const -> const std::shared_ptr<erhe::geometry::Geometry>&
{
    if (!m_geometry_published.load(std::memory_order_acquire)) {
        static const std::shared_ptr<erhe::geometry::Geometry> empty{};
        return empty;
    }
    return m_geometry;
}
```

Returning a reference stays safe: after publish the slot is immutable, and
before publish it returns a reference to an immutable function-local static.
No lock, so no flicker when an unrelated thread holds a shape lock.
`get_geometry()`'s fast path goes through the same acquire load.

`get_geometry()` builds on demand and **may block for seconds behind a loader
worker**, so main-thread per-frame code must not call it. Every per-frame
main-thread reader uses `get_geometry_const()` and handles a null geometry:
raytrace hover, ID-render hover, headset hover, `get_hit_normal()`,
`Mesh_component_selection::is_live()` (an identity comparison against a
`Geometry` the caller already holds - if the caller holds one it was
published), the drag-select facet scan, and `is_geometry_shared()`, which
scans every mesh in every mesh layer for pointer identity and would otherwise
force synchronous main-thread construction of every not-yet-built shape in the
scene.

The deliberately blocking on-demand callers - physics collision import,
geometry operations, brushes, lightmap baking, the paint tool, MCP queries -
keep blocking. They want the geometry and are user-initiated, not per-frame.

`node_raytrace.cpp`'s `get_hit_normal()` additionally guards `facet ==
GEO::NO_INDEX`, which a proxy or retired-proxy hit produces while `geometry`
is non-null.

## Behavior while a mesh is still loading

Hovering a mesh whose geometry is not yet published (proxy raytrace) gives a
valid hit with position, mesh, primitive index and the raytrace triangle
normal, but no `geometry`, no `facet` and no facet-normal override - exactly
the AABB-proxy contract stated on `Primitive_shape`. Once the deferred
finalize commits, the next frame's hover is fully detailed. Fully loaded
scenes are unaffected. Note that for a hit on a still-proxy raytrace,
`get_mesh_facet_from_triangle()` returns `GEO::NO_INDEX` anyway, so blocking
for the geometry would not even have produced a facet that frame.

## Limits

- **Reference-returning accessors stay unsynchronized.**
  `get_element_mappings()`, `get_raytrace()` and `get_renderable_mesh()` hand
  out references to state that `commit_*` mutates; a lock cannot make that
  safe without changing their signatures.
  - `get_renderable_mesh()` is the hot one, read every frame by the indirect
    draw buffer, the primitive buffer, the draw list, the scene TLAS and the
    viewport. Its safety rests entirely on the convention that
    `commit_geometry_buffer_mesh()` is called under `item_host_mutex`. With
    the split that convention is a **required contract, not an accident**: the
    state lock does not protect those readers, so every `commit_*` call site
    must keep holding the item-host lock.
  - `get_element_mappings()` and `get_raytrace()` readers are all main-thread.
- `Brush::get_geometry()` is a different lazy builder, called from a per-frame
  ImGui window: the same shape of problem in a different subsystem.

## Verifying a change here

1. Load a large glTF scene (ABeautifulGame) for about a minute and assert zero
   `Main loop STALLED` lines in `logs/log.txt`. The breadcrumb ring is shared
   across all threads and the loader workers spam it, so the absence of a
   watchdog line is corroborated by (2) rather than trusted alone.
2. Sample the process during the load: the main thread must appear in neither
   `Primitive_shape::make_geometry` nor `get_mesh_facet_from_triangle` nor a
   mutex wait on a shape lock.
3. Repeat with a second scene (Sponza).
4. Headless: `pick_at` during a concurrent scene load returns in milliseconds,
   and on a fully loaded scene still resolves full facet detail (a facet id
   and the facet-normal override).
5. Interactively, **during** a load, drag with the transform tool so
   `draw_ray_hit()` -> `get_hit_normal()` runs while workers are building;
   that is the unconditional path a post-load hover check does not cover.

## Future work

- [Mesh memory and primitive shapes](../plans/mesh_memory.md) - giving the three
  reference-returning accessors signatures that can be synchronized.
