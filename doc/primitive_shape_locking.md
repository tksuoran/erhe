# Plan: stop main-thread frame work from blocking on async glTF geometry / BVH builds

## Symptom

Loading `res/editor/assets/ABeautifulGame/glTF-Binary/ABeautifulGame.glb` (Metal, Debug):

```
Main loop STALLED: tick has not progressed for 30.7 s. Stuck in phase: 'tick: draw_imgui_windows'
Main loop STALLED: tick has not progressed for 11.0 s. Stuck in phase: 'geometry: update_connectivity + build_edges' (tick thread)
```

## Evidence

`sample` of the live process during a stall. Main thread, 5999 of 8712 samples (~69%) in
`__psynch_mutexwait`:

```
Editor::tick()                                     editor.cpp:731
 Scene_views::update_hover_info()                  viewport_scene_views.cpp:811
  Viewport_window::update_hover_info()             viewport_window.cpp:88
   Viewport_scene_view::update_hover()             viewport_scene_view.cpp:1085
    Scene_view::update_hover_with_raytrace()       scene_view.cpp:568
     Primitive_shape::get_geometry()               primitive.cpp:456
      Primitive_shape::make_geometry()             primitive.cpp:413   <- blocked on m_mutex
```

(Line numbers in the sample are the sampled instruction's line inside each function; the
function definitions at HEAD are `make_geometry` :411, `get_geometry` :453,
`prepare_real_raytrace` :537.)

Concurrently ~14 loader workers are in `Primitive_shape::prepare_real_raytrace()`
(primitive.cpp:537): two running inside `mesh_from_triangle_soup()` / `Geometry::process()`,
the rest blocked on the same shape mutex. The remaining main-thread time (1550 samples) is the
normal `Swapchain_impl::begin_frame()` drawable wait, i.e. not part of the problem.

## Root cause

`Primitive_shape::m_mutex` is a single lock doing two unrelated jobs:

1. **Build dedup** - held across the expensive, idempotent build steps so a Primitive shared by
   many meshes (every chess piece here) is converted once. `prepare_real_raytrace()`
   (primitive.cpp:537-556) holds it across `make_geometry_locked()` - `mesh_from_triangle_soup()`
   plus `Geometry::process()`, itself serialized globally on `erhe::geometry::geogram_lock()`
   (geometry.cpp:1443) - *and* the subsequent BVH build (`Primitive_raytrace(GEO::Mesh)` ->
   `m_rt_geometry->commit()`, primitive.cpp:277-281). Seconds per large shape in Debug.
   `prepare_geometry_buffer_mesh()` (primitive.cpp:612-634) does the same for the buffer mesh.
2. **State protection** - the same lock guards short reads/writes of the raytrace and buffer-mesh
   slots: `get_mesh_facet_from_triangle()` (primitive.cpp:809-822), `commit_real_raytrace()`
   (:557), `commit_geometry_buffer_mesh()` (:638).

Because job 2 shares a lock with job 1, **any short state read on the main thread waits for a
worker's multi-second build**. Two main-thread frame paths do exactly that:

- `Scene_view::update_hover_with_raytrace()` (scene_view.cpp:568, 573) calls `get_geometry()`
  and then `get_mesh_facet_from_triangle()` on the hit shape, every frame, only to look up the
  hit facet normal. That is the sampled stall.
- `get_hit_normal()` (node_raytrace.cpp:100) calls `get_mesh_facet_from_triangle()`
  *unconditionally*, every frame, from `draw_ray_hit()` (physics_tool.cpp:517,
  trs_tool.cpp:1170, transform_tool.cpp:1307/1373).

A second, milder variant: when no worker has started yet, `get_geometry()` does not block - it
*builds the geometry on the main thread*. That is the `geometry: update_connectivity +
build_edges` watchdog signature attributed to the tick thread.

`update_hover_info()` sets no breadcrumb, which is why the watchdog blamed the preceding phase.

Note the wasted work: for a hit on a still-proxy raytrace,
`get_mesh_facet_from_triangle()` returns `GEO::NO_INDEX` anyway, so the geometry the main thread
waited seconds for is not even used that frame.

## Design

### 1. Split `Primitive_shape::m_mutex` into a build lock and a state lock

This is the root-cause fix; everything else follows from it.

- `m_build_mutex` - held for the duration of the expensive idempotent builds. Its only job is
  dedup. Long hold times are fine and intended.
- `m_state_mutex` - held only for short reads/writes of the shape's mutable slots
  (`m_geometry` publish, `m_element_mappings`, `m_raytrace`, `m_pending_raytrace`,
  `m_retired_proxy_raytrace`, `m_renderable_mesh`, `m_pending_buffer_mesh`, `m_normal_style`).
  Never held across a build.

**Full lock order: `Item_host::item_host_mutex` -> `m_build_mutex` -> `m_state_mutex`.**
The main thread genuinely takes the item-host lock and then a shape lock (the commit closures,
async_raytrace_kickoff_operation.cpp:168-183). What makes that deadlock-free is that no
`Primitive_shape` method ever touches scene state - `primitive.cpp` references no `Item_host` -
so the reverse edge does not exist. Keep it that way; this is the contract already sketched at
primitive.hpp:80-85, now stated in full.

New shape of the affected methods:

| method | today | after |
| --- | --- | --- |
| `make_geometry()` | `m_mutex` around build | `m_build_mutex` around build; publish under `m_state_mutex` |
| `make_raytrace()` | `m_mutex` around build | `m_build_mutex` around build; assign `m_raytrace` under `m_state_mutex` |
| `make_raytrace(const GEO::Mesh&)` | **no lock** (relies on its only caller holding `m_mutex`) | becomes a private `make_raytrace_build_locked(const GEO::Mesh&)`: builds into a local, assigns under `m_state_mutex`, caller holds `m_build_mutex` - it has no external callers |
| `make_raytrace_proxy()` | `m_mutex` | `m_build_mutex` + publish under `m_state_mutex` |
| `prepare_real_raytrace()` | `m_mutex` across geometry + BVH build | `m_build_mutex` across both; early-outs and publish under `m_state_mutex` (see "check and publish" below) |
| `commit_real_raytrace()` | `m_mutex` | **`m_state_mutex` only** |
| `prepare_geometry_buffer_mesh()` | `m_mutex` across build | `m_build_mutex` across build; early-outs and publish under `m_state_mutex` |
| `commit_geometry_buffer_mesh()` | `m_mutex` | **`m_state_mutex` only** |
| `get_mesh_facet_from_triangle()` | `m_mutex` | **`m_state_mutex` only** |
| `has_raytrace_triangles()`, `has_real_raytrace()`, `has_buffer_mesh_triangles()`, `has_edge_lines()` | **no lock at all** (racy today) | `m_state_mutex` |
| `Primitive_render_shape::make_buffer_mesh()` (both overloads) | **no lock at all** | `m_build_mutex` around the build, publish `m_renderable_mesh` + `m_element_mappings` under `m_state_mutex` (both become locals - see below) |

**Re-entrancy: `std::mutex` is not recursive, and both locks have re-entrant call paths today.**
The rule is that *locking lives in the public entry points only*; all shared work moves into
private `*_build_locked()` / `*_state_locked()` helpers that assume the corresponding lock is
already held. Concretely:

- today's `make_geometry_locked()` becomes `make_geometry_build_locked()`
- `has_real_raytrace_state_locked()`, `has_edge_lines_state_locked()` for the state re-entrancy
- **`make_raytrace()` (primitive.cpp:497) tail-calls `make_raytrace(m_geometry->get_mesh())` at
  primitive.cpp:512.** Giving both the build lock self-deadlocks. The callee becomes the private
  `make_raytrace_build_locked()`; only `make_raytrace()` takes the lock.
- **`make_buffer_mesh(const Build_info&, Normal_style)` (primitive.cpp:650) falls through to
  `make_buffer_mesh(const Buffer_info&)` at primitive.cpp:666.** Same trap, same resolution: one
  private `make_buffer_mesh_build_locked()` per overload, locking only in the public entry points.

In `make_buffer_mesh()`, note that `build_buffer_mesh()` is handed `m_renderable_mesh` and
`m_element_mappings` as out-params today (primitive.cpp:655-661), with the mappings cleared
field-by-field at :653-655. Both must become locals published under `m_state_mutex`, exactly as
the geometry path in §2 - this row is not "wrap the existing call in a lock".

The `make_buffer_mesh()` overloads (primitive.cpp:650-681) are on the glTF load path via
`Primitive::make_renderable_mesh` (primitive.cpp:949, called from gltf.cpp:582, :680), where the
shape is not yet published to the scene and no worker can be inside a `prepare_*` on it. They are
included anyway rather than argued around: they clear and refill `m_element_mappings` and
overwrite `m_renderable_mesh` with no lock at all, and the argument for why that is safe is a
timing argument, not a structural one.

**Check and publish (the part that must not be got wrong).** Today `prepare_real_raytrace`
(primitive.cpp:537-556) and `prepare_geometry_buffer_mesh` (:612-634) check-and-build under one
lock, so "a second worker sees the first worker's result" is automatic. A naive split - check
state, release, take the build lock, build - lets two workers on a shared shape both build,
destroying exactly the dedup this plan promises to preserve. The required sequence is:

1. take `m_build_mutex`
2. take `m_state_mutex`, run the authoritative early-out checks (`has_real_raytrace_state_locked()`,
   `m_pending_raytrace` / `m_pending_buffer_mesh` present), release it
3. build into locals with no state lock held
4. take `m_state_mutex`, **re-check** the same conditions, drop the freshly built result if
   another path won, otherwise publish

A pre-build-lock check is allowed only as a fast path, never as the authoritative one.

Step 4's re-check is not merely defensive. `commit_real_raytrace()` retires the installed
raytrace only when it `is_proxy()` (primitive.cpp:561-566); the retirement exists because live
`Raytrace_primitive`s hold a raw `IGeometry*` (mesh.cpp:61-64). Installing a stale second pending
after a commit would therefore free an `IGeometry` that meshes still point at. With steps 1-2 in
place that is unreachable - a pending can only be created under the build lock - but the
invariant is then a property of the code shape rather than of the lock, so the re-check pins it
down where an implementer can see it.

`prepare_geometry_buffer_mesh` additionally gains the already-committed early-out it lacks today
(`has_edge_lines_state_locked()`, mirroring `prepare_real_raytrace`'s `has_real_raytrace()` at
:540). Without it, a second task sharing the shape rebuilds the whole buffer mesh after the
first commit cleared `m_pending_buffer_mesh`, and commits a redundant second swap.

`make_geometry_build_locked()` returns the built geometry to its caller rather than leaving
callers to read `m_geometry` afterwards: `prepare_real_raytrace` (primitive.cpp:549) and
`prepare_geometry_buffer_mesh` (:625) both dereference `m_geometry` outside the state lock today,
which is correct only via the publish-once argument. Passing the local through removes the
question.

Consequences that matter:

- The main thread's `Scene_commit_queue::flush()` (editor.cpp:606) runs `commit_real_raytrace()`
  / `commit_geometry_buffer_mesh()` **while holding `scene_root->item_host_mutex`**
  (async_raytrace_kickoff_operation.cpp:168-183). Today those take the build-and-state lock; that
  it does not stall in practice depends on an undocumented timing invariant (every shape in a
  commit batch already has its `m_pending_*` set, so a later worker `prepare_*` returns
  immediately). After the split the commits take only the short state lock and the dependency
  disappears.
- The build-dedup guarantee is preserved by the step 1-4 sequence above, not by the split itself.
- The newly-locking `has_*` accessors are not on any per-frame path. Their complete caller set,
  including the `Primitive::has_renderable_triangles()` / `has_raytrace_triangles()` forwarders
  (primitive.cpp:879-893), is example.cpp:175, brush.cpp:126/135/221/398,
  async_raytrace_kickoff_operation.cpp:125/131, operations_window.cpp:2204. No renderer path goes
  through them.

### 2. Publish-once invariant for `m_geometry`, made explicit

`m_geometry` is written in exactly one place after construction - `make_geometry_locked()`
(primitive.cpp:447), under the lock, only when previously null. It is never cleared or replaced:
the only other writes repo-wide are the move ctor (:376), move assignment (:388) and the
ctor init (:398). Geometry operations build a **new** `Primitive` and swap primitive lists
(mesh_operation.cpp:340, :98) rather than rebinding a shape's geometry, and
`commit_geometry_buffer_mesh()` touches only the buffer-mesh slots (:637-648). The invariant is
about the **slot**, not the pointee: `gltf.cpp:672-678` does call `geometry->process()` in place
on an already-published Geometry, so this is not an immutability claim about the Geometry object.

Add `std::atomic<bool> m_geometry_published` recording that:

- constructors: `true` iff a non-null geometry was passed
- move ctor / move assignment: carry the source's value and **clear the source's** (a moved-from
  `m_geometry` is null; leaving the source flag set would publish a null). Move operations take
  no locks and are not thread-safe - document that they are construction-time only.
- `make_geometry_build_locked()`: `store(true, std::memory_order_release)` immediately after
  `m_geometry = std::move(geometry)`, inside the `m_state_mutex` critical section

`make_geometry_build_locked()` (today's `make_geometry_locked()`) also stops writing
`m_element_mappings` in place (primitive.cpp:441 passes the member straight into
`mesh_from_triangle_soup`). It builds into a
local `Element_mappings` and publishes it in the **same** `m_state_mutex` critical section as
`m_geometry`, gated by the same publish-once flag, so the mappings and the geometry they index
become visible together:

```cpp
{
    const std::lock_guard<std::mutex> state_lock{m_state_mutex};
    m_element_mappings = std::move(local_mappings);
    m_geometry         = std::move(local_geometry);
    m_geometry_published.store(true, std::memory_order_release);
}
```

A geometry publish can never clobber mappings installed by a commit:
`commit_geometry_buffer_mesh()` (primitive.cpp:637-648) requires a prior `prepare`, which
requires a published geometry, so the publish always precedes any commit. The two
`ERHE_VERIFY`s at primitive.cpp:420-421 (mappings empty on entry) move into the **step-2
`m_state_mutex` critical section** of the check-and-publish sequence - not merely "to the top of
the function". `m_element_mappings` is a state-lock-protected slot that
`commit_geometry_buffer_mesh()` writes, so asserting on it under the build lock alone would be
the very unsynchronized member read this plan removes elsewhere. The conditions cannot change
between step 2 and the build (a commit requires a published geometry), and as post-build checks
on a local they would prove nothing.

### 3. `get_geometry_const()` becomes the non-blocking, race-free reader

It already exists, is already `const`, is already null-able, and already has 8 callers that want
a read without a build. Today it reads `m_geometry` with no synchronization at all.

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

Returning a reference stays safe: after publish the slot is immutable, before publish we return a
reference to an immutable function-local static. No lock, so no flicker when an unrelated thread
holds a shape lock. All 8 existing callers (gltf_fastgltf.cpp:4587,4640; gltf.cpp:670;
debug_visualizations.cpp:1881; mcp_server_scene_query.cpp:406; operations_window.cpp:2853;
mesh_intersect.cpp:96; properties.cpp:652) keep the owning shape alive across the use.

`get_geometry()`'s fast path (primitive.cpp:453-459) currently reads `m_geometry` unlocked too;
route it through the same acquire load so the blocking accessor is synchronized as well.

Document both contracts on the declarations: `get_geometry_const()` never builds and never
blocks, returns null until publish; `get_geometry()` builds on demand and may block for seconds
behind a loader worker - main-thread per-frame code must not call it.

### 4. Convert the per-frame main-thread readers to the non-blocking accessor

`get_geometry()` -> `get_geometry_const()`:

- `src/editor/scene/scene_view.cpp:568` - raytrace hover
- `src/editor/scene/viewport_scene_view.cpp:1135` - ID-render hover
- `src/editor/xr/headset_view.cpp:852` - headset hover
- `src/editor/scene/node_raytrace.cpp:101` - `get_hit_normal()`
- `src/editor/tools/mesh_component_selection.cpp:424` - `is_live()`, an identity comparison
  against a Geometry the caller already holds; if the caller holds one it was published. This is
  the highest-leverage line: `is_live()` is called every frame from 8 sites (items.cpp:131,
  developer/selection_window.cpp:54, operations_window.cpp:139/615/1556,
  mesh_component_selection_tool.cpp:764/1056/1101, mesh_component_transform.cpp:111/249)
- `src/editor/tools/mesh_component_selection_tool.cpp:1179` - `add_facets_from_scan`, per frame
  while drag-selecting; it skips a null geometry, so it must not build one
- `src/editor/transform/mesh_component_transform.cpp:995` - `is_geometry_shared()` scans every
  mesh in every mesh layer for pointer identity; today it can force synchronous main-thread
  construction of every not-yet-built shape in the scene

All of these already handle a null geometry (`if (entry.geometry)`, `if (!geometry) continue;`,
`== geometry`).

Additionally guard `node_raytrace.cpp:100-107`: `facet` may be `GEO::NO_INDEX` (proxy or
retired-proxy hit) while `geometry` is non-null, and :105 feeds it to `mesh_facet_normalf()`
unchecked. Add `if (facet == GEO::NO_INDEX) { return hit.normal; }`. This is a latent
out-of-range access today; the fix widens the window in which it is reachable.

### 5. Breadcrumb for the phase that was actually stuck

`Editor::tick()` sets no breadcrumb between `tick: draw_imgui_windows` (editor.cpp:707) and
`tick: fixed_step` at :799, so the watchdog misattributed the hover stall. Add
`erhe::log::set_breadcrumb("tick: update_hover_info")` before
`m_viewport_scene_views->update_hover_info(imgui_host)` (editor.cpp:731).

## Non-goals (explicit, so the doc does not over-claim)

- **Reference-returning accessors stay unsynchronized.** `get_element_mappings()`
  (primitive.cpp:824), `get_raytrace()` (:466) and `get_renderable_mesh()` (primitive.hpp:147)
  hand out references to state that `commit_*` mutates; a lock cannot make that safe without
  changing their signatures.
  - `get_renderable_mesh()` is the hot one: read every frame by draw_indirect_buffer.cpp:75/147,
    primitive_buffer.cpp:198/310, draw_list_scene.cpp:66, scene_tlas.cpp:182,
    viewport_scene_view.cpp:104, mesh.cpp:318. Its safety rests entirely on the convention that
    `commit_geometry_buffer_mesh()` is called under `item_host_mutex`, documented at
    scene_root.cpp:1386-1391. **After the split that convention becomes a required contract, not
    an accident**: the state lock does not protect those readers. Every `commit_*` call site
    (async_raytrace_kickoff_operation.cpp:168-172, operations_window.cpp:2214-2218) must keep
    holding the item-host lock.
  - `get_element_mappings()` readers (paint_tool.cpp:307, mesh_component_transform.cpp:693/807 -
    the complete set) and `get_raytrace()` readers (mesh.cpp:61, properties.cpp:658) are
    main-thread. This is a pre-existing gap, not fixed here, and is called out as follow-up work
    rather than claimed as done.
- `Brush::get_geometry()` (brush.cpp:326) is a different lazy builder called from a per-frame
  ImGui window (brush_tool.cpp:842). Same shape of problem, different subsystem, out of scope.
- No change to what the loader builds, or to when it commits.
- The deliberately blocking on-demand callers of `get_geometry()` (physics collision import,
  geometry operations, brushes, lightmap baking, paint tool, MCP queries) keep blocking. They
  want the geometry and are user-initiated, not per-frame.

## Behavior change

While a mesh is still loading (proxy raytrace, geometry not yet published), hovering it gives a
valid hit with position, mesh, primitive index and the raytrace triangle normal, but no
`geometry`, no `facet` and no facet-normal override. That is exactly the documented AABB-proxy
contract (primitive.hpp:30-35). Once the deferred finalize commits, the next frame's hover is
fully detailed. No change for fully loaded scenes.

## Verification

1. Build `editor` for Metal Debug.
2. Run `--scene res/editor/assets/ABeautifulGame/glTF-Binary/ABeautifulGame.glb` for ~60 s;
   assert zero `Main loop STALLED` lines (baseline: 4 in 60 s, up to 30 s stuck). The breadcrumb
   ring is 32 entries shared across all threads (log.cpp:38) and ~14 workers spam it, so the
   *absence* of a watchdog line is corroborated by (3) rather than trusted alone.
3. `sample` the process during the load; assert the main thread appears in neither
   `Primitive_shape::make_geometry` nor `get_mesh_facet_from_triangle` nor `__psynch_mutexwait`
   on a shape lock.
4. Repeat (2) with a second scene (Sponza) as a sanity check.
5. `python3 scripts/test_editor_mcp.py --unit-only` (55 tests), run serially.
6. Manual: after the load settles, hover a chess piece and confirm facet hover still works
   (mesh component selection highlight / hover tool window).
7. Manual, **during** the load: drag with the transform tool so `draw_ray_hit()` ->
   `get_hit_normal()` runs while workers are building. node_raytrace.cpp:100 is the
   unconditional path and step 6 only covers the post-load state.
8. The change is partly justified as a race fix, so where a sanitizer build is available, run
   the load under it; erhe ships asan configure scripts (scripts/configure_xcode_metal_asan.sh)
   but no tsan one, so absent tsan this is covered by the assertions and by keeping every
   converted accessor free of unsynchronized member reads.

## Implementation note

The check-and-publish sequence (§1, steps 1-4) applies to **every** method that takes the build
lock, not only the two `prepare_*` methods where it is spelled out: `make_geometry()`,
`make_raytrace()`, `make_raytrace_proxy()`, `prepare_real_raytrace()`,
`prepare_geometry_buffer_mesh()` and both `make_buffer_mesh()` overloads. The correctness
argument for the retired-proxy path depends on all of them following it.

## Results

Implemented and verified on Metal / Debug.

| check | before | after |
| --- | --- | --- |
| `Main loop STALLED` lines, ABeautifulGame, 60-70 s run | 4 (worst: 30.7 s stuck) | **0** |
| `Main loop STALLED` lines, Sponza, 75 s run | - | **0** |
| main-thread samples in `make_geometry` / `get_mesh_facet_from_triangle` / mutex wait, sampled 12 s during the load | 5999 / 8712 (~69%) | **0 / 7952** |
| `scripts/test_editor_mcp.py --unit-only` | 55 passed | **55 passed** |

After the fix the main thread's sampled time is the normal `Swapchain_impl::begin_frame()`
drawable wait plus real render work.

Hover was exercised headlessly through the `pick_at` MCP tool, which runs the same
`Viewport_scene_view::update_hover()` the interactive pointer runs:

- fully loaded scene: hit on `Chessboard` reporting `facet: 27755` - the converted
  `get_geometry_const()` + `get_mesh_facet_from_triangle()` path still resolves full facet
  detail, including the facet-normal override
- **during** a concurrent scene load: repeated picks returned in ~10 ms each, where the same
  call previously blocked for the duration of a worker's geometry / BVH build

One correction to the plan as written: `make_raytrace(const GEO::Mesh&)` does have an external
caller (scene_builder.cpp:1253, instanced cubes built from a Buffer_mesh with no Geometry of
their own), so it stays public and takes the build lock; only the internal build step moved to
`make_raytrace_build_locked()`.

Two incidental improvements fall out of the "build aside, publish valid results only" rule:
`make_raytrace(const GEO::Mesh&)` and `make_buffer_mesh()` no longer overwrite a good raytrace /
buffer mesh with an empty one when their build fails.
