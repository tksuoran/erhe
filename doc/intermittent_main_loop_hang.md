# Intermittent main-loop hang (render thread CPU spin)

## Status

Open. **Localized** to `Build_context::build_polygon_fill()` via the watchdog
(see "Confirmed observation" below); root cause not yet fixed. Intermittent.
This document describes the symptom, how to confirm it, the diagnostic logging
that pinpoints it, and the candidate causes.

## Confirmed observation (2026-06-01, Quest 3, Vulkan/OpenXR)

After ~9 launches the hang reproduced. The watchdog reported:

```
[editor.watchdog] Main loop STALLED: tick has not progressed for 30.7 s.
                  Stuck in phase: 'primitive: build_polygon_fill' (tick thread 0x...).
  breadcrumb ...: primitive: build_polygon_fill
  breadcrumb ...: primitive: build_edge_lines
  breadcrumb ...: primitive: build_centroid_points
  ... (many rapid primitive builds, then stuck on the last build_polygon_fill)
```

`/proc/<pid>/task/<render-tid>/stat` confirmed the render thread in state `R`
with `utime` climbing (~160 s of CPU), `stime` flat -- a pure userspace spin.
The rapid breadcrumb cycle is **thumbnail generation** (`tick: thumbnails
update` -> `Brush_preview::render_preview` -> `Brush::get_scaled()` lazily
builds the preview primitive on the main thread); one brush's
`build_polygon_fill()` then spun forever.

`build_polygon_fill()`'s own loops are bounded by `mesh.facets` /
`facets.corners(facet)` and all its callees are straight-line (no loops), so
the spin is a **corrupt / absurdly-sized Geogram `GEO::Mesh`** (e.g. a `nb()`
near `0xFFFFFFFF`, or a corrupt `facet_ptr` making one facet's corner range
span billions). Editor init **joins** all worker tasks (`taskflow_future.get()`)
before the main loop, so the mesh is **not** being mutated concurrently at
build time -- the corruption happened **earlier, during parallel init**, where
brush geometry is processed with Geogram (`Geometry::process()` ->
`facets.connect()` / `update_connectivity()` / subdivision) on **taskflow
worker threads** (`Scene_builder::make_brushes()`,
`src/editor/scene/scene_builder.cpp`). Geogram carries process-global state and
is not fully thread-safe, so concurrent `process()` across workers is the
leading suspect for occasionally leaving one mesh malformed.

Added diagnostics to name the culprit on the next repro:
- `Brush_preview::render_preview()` sets `thumbnail: brush '<name>'` before the
  lazy build (`src/editor/preview/brush_preview.cpp`).
- The `build_polygon_fill` breadcrumb now carries the mesh counts (`facets=...
  verts=... corners=...`) (`src/erhe/primitive/erhe_primitive/primitive_builder.cpp`).
  Absurd counts confirm a pre-corrupted mesh; sane counts point at internal
  `facet_ptr` corruption (a single facet with a billions-long corner range).
- `Geometry::process()` validates the mesh's structural sanity on the worker
  thread right after building it and logs `MESH CORRUPT ... geometry '<name>'`
  the moment it is malformed -- catching + naming the culprit during init,
  before any thumbnail hang. Gated by `ERHE_DEBUG_VALIDATE_GEOMETRY`
  (`src/erhe/geometry/erhe_geometry/geometry.cpp`). Confirmed it never
  false-positives on healthy meshes (many clean desktop + Quest runs).

## Reproduction attempts (2026-06-01)

The leading hypothesis was a concurrent-Geogram race during parallel brush
init. Two things argue the lever is timing-subtle (likely an ARM memory-model
race, benign under x86's stronger ordering), and that instrumentation perturbs
it:

- A timing **amplifier** (sleeps, then a rendezvous barrier forcing 4 worker
  threads into `facets.connect()` simultaneously) was tried to make it
  reproduce on every launch. It did the OPPOSITE -- **8/8 clean on Quest and
  8/8 clean on desktop even with forced overlap**. The amplifier was removed
  (kept only the validation). A coarse timing-overlap race would have fired
  under forced overlap; it did not.
- At natural timing with validation, **10/10 clean on Quest** (18 clean total
  since the one captured hang). So the rate is well below the ~1/9 first seen,
  and the diagnostics likely settle the timing slightly.

Conclusion / strategy: do NOT brute-force it in a worn launch batch (it
exhausts the tester for little yield). Instead **catch it opportunistically**:
keep the instrumented build in use; the watchdog (separate thread, does NOT
perturb init timing) will catch any hang and name the phase + mesh counts, and
the validator names a corrupt brush during init when it fires. When it next
happens, read this doc and analyze the captured log. The non-perturbing
watchdog is the primary detector; the validator is a bonus early namer.

## Symptom

Shortly after the editor finishes initialization, the editor stops producing
frames and appears frozen. In the headset (OpenXR/Quest) the compositor keeps
displaying the last frame it received, so whatever was on screen at the moment
of the stall stays frozen (e.g. the init loading panel); on desktop a stall
would likewise freeze the window.

Reproduction so far: confirmed on Quest 3 (ARM). It has NEVER reproduced on
Windows (x86) -- including many deliberate attempts. It may have reproduced on
macOS; if that was Apple Silicon (ARM), it is consistent with the
ARM-memory-model hypothesis discussed below (a data race benign under x86's
stronger ordering but harmful under weaker ARM ordering).

Observed once on Quest 3 (Vulkan/OpenXR):

- Editor logged `Init: editor ready, entering main loop`, then created the
  Hud/Hotbar quad layers, built a mesh (`[erhe.primitive.primitive_builder]
  Warning: Used fallback smooth normal`), and then went completely silent --
  no further `erhe`-tagged log lines while the rest of the system log kept
  flowing.

## How to confirm it is this issue

The defining characteristic is a **userspace CPU spin on the render thread**,
as opposed to a GPU wait or a mutex deadlock. To confirm (no root required on
a debuggable build):

1. Find the editor pid and its render thread (the thread that emitted the
   startup logs; on the observed incident it was the second TID in the
   process):

   ```bash
   ADB="$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe"
   PID=$("$ADB" shell pidof org.libsdl.app.quest | tr -d '\r')
   "$ADB" shell ps -T -p $PID -o TID,NAME,STAT
   ```

2. Sample the suspect thread's scheduler state and CPU time a few times:

   ```bash
   for i in $(seq 1 8); do
     "$ADB" shell "cat /proc/$PID/task/<TID>/stat" | tr -d '\r' \
       | awk '{print "state="$3" utime="$14" stime="$15}'
     "$ADB" shell "cat /proc/$PID/task/<TID>/wchan"; echo
     sleep 0.4
   done
   ```

   This issue looks like:
   - `state=R` persistently (Running), not `S`/`D` (a deadlock/GPU wait would
     be `S` or `D`).
   - `utime` climbing steadily, `stime` flat (pure userspace compute; the loop
     is not syscalling).
   - `wchan` empty / `0` (not blocked in the kernel).

If instead the thread is in `S`/`D` with a non-empty `wchan`, it is a
different problem (blocked on a lock or a fence), not this one.

`debuggerd -b <tid>` and `simpleperf` both require root on stock Quest, so a
native backtrace is usually not obtainable on-device. That is the whole reason
for the watchdog logging below.

## Diagnostic logging (added for this issue)

Because the spinning thread never returns to a logging point, it cannot say
where it is stuck. Two mechanisms work around that:

### 1. Breadcrumbs (`erhe::log::set_breadcrumb`)

A lightweight, mutex-guarded record of the most recent named execution phase,
plus a ring of the last 32 phases (each with thread id and a monotonic
timestamp). Declared in `src/erhe/log/erhe_log/log.hpp`, implemented in
`log.cpp`. `set_breadcrumb(std::string_view)` is cheap (one uncontended lock;
the current-text buffer is reused) and safe from any thread.

Breadcrumbs are set at:

- `Editor::tick()` major phases (`tick: wait_frame`, `tick: xr poll_events`,
  `tick: xr begin_frame`, `tick: xr update_actions`, `tick: fixed_step
  (physics)`, `tick: imgui process_events + commands`, `tick:
  draw_imgui_windows`, `tick: thumbnails update`, `tick: update_transforms`,
  `tick: rendergraph execute`, `tick: headset end_frame`, `tick: submit +
  end_frame`) -- `src/editor/editor.cpp`.
- Each rendergraph node before it executes (breadcrumb = node name) --
  `src/erhe/rendergraph/erhe_rendergraph/rendergraph.cpp`.
- `Geometry::process()` sub-steps (`geometry: facets.connect`, `geometry:
  update_connectivity + build_edges`, `geometry:
  compute_smooth_vertex_normals`, ...) --
  `src/erhe/geometry/erhe_geometry/geometry.cpp`.
- `Primitive_builder::build()` sub-steps (`primitive: build_polygon_fill`,
  `primitive: build_edge_lines`, `primitive: build_centroid_points`) --
  `src/erhe/primitive/erhe_primitive/primitive_builder.cpp`.

### 2. Main-loop watchdog thread

`Editor` starts a watchdog thread right after `entering main loop`
(`start_main_loop_watchdog()` in `src/editor/editor.cpp`) and joins it first
thing in `~Editor`. The render thread runs `tick()` with `m_in_tick == true`;
the watchdog wakes once per second and, if a tick has been in progress
(`m_in_tick == true`) without the tick-thread breadcrumb advancing for more
than 5 seconds, it logs (tag `editor.watchdog`):

```
Main loop STALLED: tick has not progressed for N.N s. Stuck in phase: '<phase>' (tick thread 0x...).
  breadcrumb t=...s thread=0x...: <phase>
  breadcrumb t=...s thread=0x...: <phase>
  ... (the recent ring, oldest -> newest)
```

The watchdog attributes the stuck phase to the **tick thread** specifically
(worker threads also set breadcrumbs during background geometry processing, so
the newest breadcrumb from the tick thread is the authoritative one). It only
fires while a tick is actually in progress, so legitimate idle/throttle between
frames (including the in-tick 250 ms OpenXR off-head throttle) does not trip
it.

## Procedure when it recurs

1. Capture the log. On Quest, keep `scripts/quest_logcat.sh` streaming to disk
   before/while it happens (the in-memory ring rolls over). On desktop, use
   `logs/log.txt`.
2. Grep for the watchdog report:

   ```bash
   grep -n "Main loop STALLED" logs/log.txt        # desktop
   grep -nE " erhe +:.*(watchdog|STALLED|breadcrumb)" logs/quest_*.log  # Quest
   ```

3. Read `Stuck in phase: '<phase>'`. That is where the spin is. The ring dump
   below it shows the sequence of phases leading in.
4. Map the phase to the suspect and narrow:
   - `tick: rendergraph execute` followed by a node-name breadcrumb -> the spin
     is inside that rendergraph node, OR inside `Graph::sort()` (cycle) if the
     last breadcrumb is still `tick: rendergraph execute` with no node name.
     See Candidate 1/2.
   - `primitive: build_*` or `geometry: *` -> degenerate-geometry walk. See
     Candidate 3.
   - `tick: xr poll_events` -> XR event drain. See Candidate 4 (less likely;
     that loop syscalls, so `stime` would climb -- re-check the thread sample).
5. If a native stack is obtainable (rooted device, or reproduce on desktop
   under a debugger), capture the render-thread callstack -- that pins it
   immediately. Per the project convention, gather callstacks of *all* threads,
   not just the spinning one.

## Candidate causes

These came from a source audit of every loop on the per-frame / first-frame
render-thread path. Ranked against the evidence (pure userspace, no syscalls,
log went silent -- so anything that syscalls per iteration or logs per
iteration is a poor fit).

### Candidate 1 -- rendergraph topological sort (`Graph::sort()`)

`src/erhe/graph/erhe_graph/graph.cpp`, `while (!unsorted_nodes.empty())`.
Pure userspace (`std::find_if` over nodes, no syscalls). A genuine cycle is
detected and returns with an error log, but a link/pin bug where a satisfied
dependency looks unmet (or `found_node` mishandling) could spin without
shrinking `unsorted_nodes`. Runs every frame from `Rendergraph::execute()`.
Fits the "no syscalls" signature. Would tend to be deterministic for a given
graph, so less consistent with "intermittent" unless the graph is mutated at
runtime (hover/scene changes do alter it).

### Candidate 2 -- a specific rendergraph node's `execute_rendergraph_node()`

`src/erhe/rendergraph/erhe_rendergraph/rendergraph.cpp` loop. If one node's
execute spins, the per-node breadcrumb names it. Investigate that node.

### Candidate 3 -- degenerate-geometry walk in primitive/geometry build

`Primitive_builder::build_polygon_fill()` /
`Geometry::build_extra_connectivity()`
(`src/erhe/geometry/erhe_geometry/geometry.cpp`) walk corner rings. The
observed incident's last log was `Used fallback smooth normal`, implicating
this area. The current double-loop in `build_extra_connectivity` is bounded
(`O(n^2)` in the vertex's corner count), so it is slow rather than infinite on
its own -- but non-manifold / degenerate input (the same input that triggers
the fallback-normal path) is the thing to scrutinize here, including the
Geogram-backed connect/subdivision steps. Pure userspace; fits the signature.

### Candidate 4 -- XR event drain (`Xr_instance::poll_xr_events`)

`src/erhe/xr/erhe_xr/xr_instance.cpp`, `for (;;)` draining `xrPollEvent`. Only
exits on `XR_EVENT_UNAVAILABLE`. In theory could spin if the runtime refills
events as fast as they are drained. BUT each iteration calls `xrPollEvent`
(a syscall/ioctl -> `stime` would climb) and logs every event (`XR event ...`
-> the log would flood, not go silent). Both contradict the observed evidence,
so this is the **least likely** of the four. Keep it on the list only because
it is on the per-frame path; the breadcrumb `tick: xr poll_events` plus a
climbing `stime` would confirm it if it ever is the cause.

## Leading root cause: concurrent Geogram use during parallel init

The captured spin reads a **corrupt `GEO::Mesh`** (facet/corner structure). The
mesh is built during init, so understanding init concurrency is key.

### Init concurrency map

- One `tf::Executor` is created with `m_editor_settings.threading.thread_count`
  workers (`editor.cpp:730-734`, ~8), used for both init and runtime.
- **Layer 1 -- main init taskflow** (~20+ tasks, `editor.cpp:745-1760`, run via
  `m_executor->run(taskflow)` while the main thread blocks on
  `taskflow_future.get()` pumping the loading screen). Many tasks are
  free-running and parallel (shader load, the various renderers, thumbnails,
  rendergraph, material preview); others are serialized by `.succeed()` edges
  (`Imgui_windows` -> `Default scene` -> `Scene_builder` (7 deps) -> headset
  attach / tool+UI groups).
- **Layer 2 -- nested brush taskflow** inside `Scene_builder`, on the SAME
  executor (`scene_builder.cpp:622-638`): ~5-6 categories build CONCURRENTLY
  with no edges between them -- Platonic solids, Spheres, Tori, Cylinders,
  Cones (+ Johnson solids). `future.wait()` blocks the constructor until done.
  Each task, on its own task-local `GEO::Mesh`: (1) Geogram shape generation
  (`make_sphere`/`make_cube`/...), then (2) `Geometry::process(flags)` (Geogram
  `facets.connect`, `build_edges`, centroids, smooth normals, texcoords,
  tangents).

### What is shared across those concurrent tasks

| Shared state | Concurrent? | Guarded? |
|---|---|---|
| **Geogram process-global state** (allocators, attribute system, logger client `s_geogram_logger_client`, predicates) | Yes -- every concurrent generate/`process()` | **No** |
| Content library + brush lists | Yes | Yes (`content_library->mutex`, `m_brush_mutex`) |
| `Mesh_memory` pools / transfer queue | **No** -- brush GPU-buffer build is LAZY on the main thread (`brush.cpp` `late_initialize`/`get_scaled`, run during thumbnails), not on the workers | n/a here |
| `current_command_buffer` pointer | Yes (workers deref while main `pump()` reseats) | documented pre-existing hazard |

Each brush's `GEO::Mesh` is task-local, so it is not directly shared. The one
thing genuinely shared and unguarded across the ~6 concurrent `process()` calls
is **Geogram's process-global state**. The `erhe::geometry` review
(`src/erhe/geometry/claude_review.md`) itself flags "some global state". If that
is not thread-safe, a concurrent `process()` can corrupt a mesh's internal
facet/corner arrays -> `build_polygon_fill` later spins over it. This fits every
observation: intermittent, ARM-sensitive (benign on x86 desktop), and
*suppressed* by the timing amplifier.

Note: the brush GPU-buffer path (`Mesh_memory` pool / transfer-queue
allocation) is **deferred to the main thread**, so despite being unguarded it is
NOT exercised concurrently during brush building -- it is not the cause of this
hang (though it may deserve its own audit).

### Decisive test (not yet run)

Serialize the brush `process()` calls -- or wrap Geogram use in one global mutex
-- and soak-test (many launches). If the hang never recurs, concurrent Geogram
use is confirmed. Because Geogram is not thread-safe, guarding it is the
*correct* fix, not a band-aid. This is the recommended next step.

## Notes / next steps

- The watchdog + breadcrumbs are diagnostics only; they do not fix the hang.
  Once the log identifies the stuck phase on a recurrence, fix the actual loop
  (no band-aids).
- **Recommended:** prototype the guarded/serialized Geogram variant above and
  soak-test it (see "Decisive test").
- The `ERHE_DEBUG_VALIDATE_GEOMETRY` mesh validator
  (`src/erhe/geometry/erhe_geometry/geometry.cpp`) is a TEMPORARY diagnostic
  left active to name a corrupt brush during init; remove it once the root
  cause is fixed.
- A timing amplifier (rendezvous barrier forcing concurrent Geogram entry) was
  tried and REMOVED -- it suppressed the race rather than amplifying it, which
  is itself evidence the race is real but timing-subtle.
- If a future recurrence points the stuck phase elsewhere (a rendergraph node or
  `Graph::sort()`), consider a hard iteration cap with a loud error so a
  never-progress condition fails fast instead of spinning.
