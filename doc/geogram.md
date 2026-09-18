# Geogram in erhe

Stability: mostly stable

erhe uses [Geogram](https://github.com/BrunoLevy/geogram) as the backend of
`erhe::geometry`. Geogram's algorithms carry two constraints that erhe has to
satisfy from the outside: its process-global thread state permits only one
algorithm at a time, and its exact predicates require a compiler that does not
fuse their arithmetic. This document states both contracts, the guard erhe puts
in front of degenerate convex-hull input, and the infrastructure that names
where a thread is stuck when one of them is violated.

The upstream request that would let the first contract be relaxed is drafted in
`doc/reference/geogram_thread_safety_issue.md`.

## Build contract: no FMA contraction in Geogram

Geogram's geometric predicates use Shewchuk-style error-free transformations
plus exact orientation tests. A fused multiply-add (`a*b+c` with a single
rounding) changes the sign of a near-zero orientation determinant and breaks
them. Geogram states this itself: its Linux platform files set
`-frounding-math -ffp-contract=off` to disable automatic generation of FMAs,
which "would break exact predicates", and `delaunay_3d.cpp` carries the comment
`// locate_inexact() loops forever !`.

Geogram's `Android-generic` and `Darwin` platform configs are empty and omit
the flag, so erhe restores it for every non-MSVC compiler in the top-level
`CMakeLists.txt` right after the geogram `CPMAddPackage`:

```cmake
if (NOT MSVC)
    foreach (geo_target geogram geogram_third_party geogram_num_3rdparty)
        if (TARGET ${geo_target})
            target_compile_options(${geo_target} PRIVATE -ffp-contract=off)
        endif ()
    endforeach ()
endif ()
```

Without it, clang (arm64 in particular) contracts the predicate arithmetic and
Geogram's Delaunay `locate_inexact()` point-location walk never terminates on
degenerate input - a brush cone's coplanar base ring is enough. The failure is
a pure userspace spin: no crash, no memory error, no log line, and it is
intermittent because Geogram randomizes Delaunay insertion order (BRIO) per
process, so only some orders build the cycle-triggering tetrahedralization.
MSVC does not contract by default, so x86 desktop builds never showed it; the
headless reproduction ran ~25% spin on an arm64 phone and 0/100 with the flag
restored. Any new build configuration that compiles Geogram must keep this
flag, and the GLSL and remesh sources `erhe::geometry` emits for other tools to
build repeat the requirement in a comment.

## Threading contract: one geogram algorithm at a time

Geogram's Windows thread-pool manager
(`WindowsThreadPoolManager::run_concurrent_threads`, `process_win.cpp`) resets
a **static** `threadCounter_` shared by all invocations, so two threads
entering `GEO::parallel_for` simultaneously corrupt each other's thread-id
assignment and some worker slices never run. It surfaced as
`GEO::Geom::colocate()` leaving `old2new` entries at `NO_INDEX` (assert at
`colocate.cpp:254`) when two deferred glTF finalize tasks converted triangle
soups concurrently. `ParallelDelaunay3d` refuses the situation outright
(`CellStatusArray::resize` asserts `!Process::is_running_threads()`). Upstream
tracks the general problem - global static state in CVT / LBFGS, "Delaunay on
two meshes in parallel" unsupported - as BrunoLevy/geogram#68.

erhe therefore serializes every entry into a geogram *algorithm* on one
process-wide recursive mutex, `erhe::geometry::geogram_lock()` (`geometry.hpp`).
Geogram still parallelizes each call internally across cores, so the throughput
cost is small. Lock order: it is the innermost lock - never acquire a scene
(`Item_host`) or `Primitive_shape` mutex while holding it.

Guarded choke points (each takes the lock internally):

- `Geometry::process()` (xatlas atlas generation, repair-ish steps)
- `erhe::primitive::mesh_from_triangle_soup()` (colocate)
- `erhe::geometry::make_convex_hull()` (Delaunay branch)
- `operation::Repair/Weld/Remesh/Decimate/Smooth::build()` (mesh_repair,
  MeshSurfaceIntersection, CVT remesh/decimate/smooth)
- `Geometry_operation::run_mesh_boolean_operation()` (mesh_boolean_operation)
- `operation::generate_mesh_atlas_texture_coordinates()` - only its
  Geogram-parameterizer branch (mesh_make_atlas / pack_atlas_only_
  normalize_charts); the per_facet branch reaches no Geogram algorithm
  (mesh-local loops + attribute binds; mesh.cpp `connect()`/`copy()` are
  serial at the pin) and runs UNLOCKED, so per-facet unwraps of different
  meshes parallelize across workers
- `Json_library` polyhedron load (mesh_repair) in the editor
- editor `Mesh_operation::make_entries` additionally wraps the whole
  geometry-operation callback (belt and suspenders for operations not listed
  above); an operation whose implementation locks internally exactly where
  Geogram is involved opts out via `m_callback_requires_geogram_lock = false`
  (currently only `Make_atlas_operation`)

NOT guarded (mesh-local, no geogram algorithm): element/attribute
construction, `facets.connect()`, `geometry_from_flat_data`,
`compute_mesh_tangents`, plain mesh reads (buffer-mesh and raytrace builds),
`operation::bake_transform()` and `operation::clip_by_tile_tree()` (pure
per-invocation clipping state; their piece post_processing self-locks via
`Geometry::process()` - see the thread-safety note in `clip_tile_tree.hpp`).

`make_convex_hull()` uses the sequential `"BDEL"` Delaunay for the same
reason: it runs on async operation workers while other geogram work may be in
flight, and hull inputs are small enough that the parallel build buys nothing.

When the fork gains a reentrant thread manager (per-invocation context instead
of the static counter) and upstream #68 lands, this contract can be relaxed.

## Degenerate convex hull input

Geogram's Delaunay has no usable answer for a point set that spans no volume,
and its behavior differs per implementation: `"BDEL"` (sequential, the one
`erhe::geometry::make_convex_hull()` uses) logs `Warning: All the points are
coplanar` and returns a triangulation that is not a hull, while `"PDEL"`
(parallel) never returns from `set_vertices()` for the same input. Neither is
reportable to the caller.

`make_convex_hull()` therefore classifies its input with
`erhe::math::classify_affine_span()` - the O(n), allocation-free far-point /
far-from-line / far-from-plane search - and refuses anything but a volumetric
set before Geogram is reached, returning false and logging the reason. Every
caller treats false as "no hull for this geometry": the collision shape stays
absent and the MCP `create_shape` convex hull tool answers with an
`isError` reply naming the reason.

## Naming a thread that spins inside Geogram

A thread spinning inside a Geogram walk never returns to a logging point, so it
cannot report where it is. Two mechanisms stand in for it, and they are kept as
general infrastructure rather than as diagnostics of one past defect.

### Breadcrumbs

`erhe::log::set_breadcrumb(std::string_view)` records the most recent named
execution phase plus a ring of the last 32 phases, each with thread id and a
monotonic timestamp (declared in `src/erhe/log/erhe_log/log.hpp`). It costs one
uncontended mutex lock, reuses the current-text buffer, and is safe from any
thread. Breadcrumbs are set at:

- `Editor::tick()` major phases (`tick: wait_frame`, `tick: xr poll_events`,
  `tick: fixed_step (physics)`, `tick: thumbnails update`,
  `tick: rendergraph execute`, `tick: submit + end_frame`, ...) in
  `src/editor/editor.cpp`.
- Each rendergraph node before it executes, breadcrumb = node name
  (`src/erhe/rendergraph/erhe_rendergraph/rendergraph.cpp`).
- `Geometry::process()` sub-steps (`geometry: facets.connect`,
  `geometry: update_connectivity + build_edges`,
  `geometry: compute_smooth_vertex_normals`, ...).
- `Primitive_builder::build()` sub-steps (`primitive: build_polygon_fill`,
  `primitive: build_edge_lines`, `primitive: build_centroid_points`).
- `Brush_preview::render_preview`, naming the brush whose preview primitive is
  being built lazily.

A phase that sets no breadcrumb of its own is reported under the last one set,
so a breadcrumb naming a step is evidence of where the thread entered, not
proof of which call it is inside.

### Main-loop watchdog

`Editor::start_main_loop_watchdog()` starts a thread right after `entering main
loop` and joins it first thing in `~Editor`. It wakes once per second and, if a
tick has been in progress without the tick-thread breadcrumb advancing for more
than 5 seconds, logs under the `editor.watchdog` tag:

```
Main loop STALLED: tick has not progressed for N.N s. Stuck in phase: '<phase>' (tick thread 0x...).
  breadcrumb t=...s thread=0x...: <phase>
  ... (the recent ring, oldest -> newest)
```

The stuck phase is attributed to the tick thread specifically - worker threads
also set breadcrumbs during background geometry processing, so the newest
breadcrumb from the tick thread is the authoritative one. The watchdog fires
only while a tick is actually in progress, so idle or throttled frames
(including the in-tick 250 ms OpenXR off-head throttle) do not trip it.

When a stall is reported: on Quest keep `scripts/quest_logcat.sh` streaming to
disk beforehand (the in-memory ring rolls over), on desktop read `logs/log.txt`,
and grep for `Main loop STALLED`. The ring dump below the report gives the
sequence of phases leading in.

### Structural mesh validation

`erhe::geometry::validate_mesh_structure()` (`geometry.hpp`) is a bounded,
allocation-free structural check of a `GEO::Mesh`: absurd facet / vertex /
corner counts, and facets whose corner range is wrong. It is pure (no logging)
so callers format their own context, and it checks the counts first so an
absurd facet count cannot make the check itself spin. It exists so a mesh
corrupted during concurrent processing can be named the moment it happens
rather than when some later unbounded per-facet walk trips over it.
`ERHE_DEBUG_VALIDATE_GEOMETRY` (off by default) runs it right after
`Geometry::process()` on the worker thread; leave it off for normal runs, since
validating there perturbs any race being hunted.

### Soak harnesses

`src/geogram_soak/` is a headless executable, built on desktop and Android,
that mirrors `Scene_builder::make_brushes()` with no rendering, SDL, Vulkan or
headset: N taskflow workers each build a brush shape on their own `GEO::Mesh`,
run the same `Geometry::process()` flags, and validate after the join. It can
also build all ~92 Johnson solids (`--johnson <johnson.json>`) exactly as the
editor does - one shared parsed `rapidjson::Document` read concurrently, and
per solid the `Json_library::make_geometry` body including `GEO::mesh_repair()`.
`--convex-hull` exercises the Delaunay path that FMA contraction breaks.
Knobs: `--workers N` (1 = sequential), `--multithread on|off` (Geogram
`sys:multithread`), `--johnson PATH`, `--iters`, `--batch`, `--detail`.

It links only `erhe::geometry` and taskflow, and it is built for Android
deliberately, because the predicate defect is ARM-only:
`scripts/run_geogram_soak.py` pushes the arm64 ELF and runs it over
`adb shell`, with no headset or controllers involved.
`scripts/soak_quest.py` soaks the full editor instead, cold-starting it
repeatedly on a device (`--flavor mobile|quest`) and watching the log for
`Main loop: completed frame 10` (pass), `Main loop STALLED` (the watchdog's
authoritative signal) and `MESH CORRUPT`.
