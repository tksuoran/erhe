# Intermittent main-loop hang (render thread CPU spin)

## ROOT CAUSE IDENTIFIED (2026-06-02): Geogram FMA contraction on Android/clang

**The hang is Geogram's 3D Delaunay `locate_inexact()` point-location walk
looping forever, because Geogram's geometric predicates are compiled WITH
floating-point FMA contraction on Android (clang).**

Native backtrace of the spinning tick thread (from a forced-crash tombstone on
the S23 -- see "Getting a native backtrace" below):

```
Editor::tick -> Thumbnails::update -> Brush_preview::render_preview
  -> Brush::get_scaled -> Brush::late_initialize
  -> erhe::geometry::make_convex_hull(GEO::Mesh, GEO::Mesh)
  -> GEO::Delaunay::set_vertices -> ... -> GEO::Delaunay3d::locate_inexact  [SPIN]
```

Evidence chain (every earlier observation is now explained):
- `make_convex_hull` (geometry.cpp) runs a Geogram 3D Delaunay over a brush's
  vertices. The **cone** is the trigger: its base is a ring of **coplanar**
  points -- a degenerate 3D-Delaunay configuration.
- Geogram's predicates use Shewchuk-style error-free FP transformations + exact
  orientation tests. **An FMA (`a*b+c` fused, single rounding) breaks them** --
  it changes the sign of a near-zero orientation determinant. Geogram knows this:
  its Linux platform CMake sets `-frounding-math -ffp-contract=off` ("disable
  automatic generation of FMAs ... would break exact predicates"), and
  `delaunay_3d.cpp` literally comments `// locate_inexact() loops forever !`.
- Geogram's **`Android-generic` (and `Darwin`) platform configs are empty** and
  omit those flags; erhe did not add them either. So on the NDK clang (arm64) the
  default `-ffp-contract=on` fuses Geogram's predicate arithmetic -> wrong signs
  on the cone's degenerate points -> the Delaunay walk never terminates.
- **ARM-only**: clang contracts to FMA; **MSVC (x86 desktop) does not** by default
  -> x86 never reproduced. Confirmed: the headless repro spins ~2/10 on the S23
  arm64 but **0/30 on x86/MSVC**.
- **Intermittent**: Geogram randomizes Delaunay insertion order (BRIO), re-seeded
  per process; only some orders build the cycle-triggering tetrahedralization.
- **HWASan-silent**: the walk reads in-bounds memory -- an FP/logic
  non-termination, not memory corruption. HWASan reproduced it as a *spin* with no
  tag-mismatch, ruling out the earlier heap-OOB/UAF hypothesis.
- **`geogram_soak` was clean** earlier because it exercised `process()`, never
  `make_convex_hull`/Delaunay.
- The earlier "`build_polygon_fill`" localization was a **stale breadcrumb**:
  `get_scaled()` builds the render primitive (breadcrumb `build_polygon_fill`) and
  *then* calls `late_initialize()` -> `make_convex_hull` (which sets no breadcrumb)
  -- that is where it actually spins.

**Fix:** restore Geogram's intended FP flag on non-MSVC compilers --
`target_compile_options(geogram PRIVATE -ffp-contract=off)` in the top-level
`CMakeLists.txt` after the geogram `CPMAddPackage`. This makes Geogram's
predicates correct (as Geogram itself requires on Linux). It is NOT the
`PDEL -> BDEL` change tried first (a workaround on a wrong "parallel race"
hypothesis -- serial BDEL spins too, so that change does not help and should be
reverted).

**Status: root cause confirmed + fix verified on device (2026-06-02).** Building
geogram with `-ffp-contract=off` takes the headless repro
(`geogram_soak --convex-hull`, `src/geogram_soak/main.cpp`) from ~25% spin (15/60
fresh runs) to **0/100** fresh runs (both BDEL and PDEL) plus 5000 clean builds
in one process -- a decisive A/B on the same S23, same session. x86/MSVC is 0/30
either way. The editor soaks **0 hangs** end-to-end (150/150, then 100/100 after
the cleanup below; the unfixed editor hung ~1/24). Cleanup is done:
`make_convex_hull` restored to the original PDEL via `Delaunay::create(dim, name)`
(no global set_arg), and the temporary build_polygon_fill guard / corner-walk
iteration cap / make_brushes post-join validator removed. The main-loop watchdog
+ breadcrumbs are kept as general infrastructure.

---

*The sections below are the investigation trail that led here. Several interim
hypotheses in them (parallel-Geogram race, heap OOB/UAF, `build_polygon_fill`)
were SUPERSEDED by the root cause above.*

## Status

**Reproduced under controlled conditions (2026-06-02)** -- see "Controlled
reproduction" below. **Localized** to `build_polygon_fill()` via the watchdog;
the corrupt `GEO::Mesh` is produced earlier, during parallel brush init. Root
cause not yet fixed. The `ERHE_DEBUG_VALIDATE_GEOMETRY` validator was found to
be the **timing suppressor** that hid the bug for 58 clean runs; with it OFF and
an optimized (release) build the hang reproduces at ~1/8 on Quest.

**UPDATE 2026-06-02 (continued) -- headless / cross-device investigation.** See
"Headless reproduction & cross-device investigation" below. Key results:

- A standalone headless harness (`src/geogram_soak/`) that mirrors
  `make_brushes()` exactly -- same shapes, `process()` flags, the ~92 Johnson
  solids each running `GEO::mesh_repair`, a shared parsed JSON document read
  concurrently, oversubscribed -- ran **>1.3M concurrent builds and ~1000 fresh-
  process cold trials on both an S23 phone and the Quest with NO reproduction**.
  Strong evidence **against** "independent per-thread `GEO::Mesh` builds corrupt
  on their own" (Geogram-only bug).
- The **full editor reproduces it on a non-Quest ARM phone (Samsung S23)** when
  built as the flat `mobile` flavor: 1 hang in 40 cold launches, identical
  signature (`build_polygon_fill`, `facets=801`). So the bug is **not** Quest- or
  immersive-specific, and a **headset-free repro path exists** (soak the `mobile`
  editor on a phone). The culprit brush is the **`cone`** (the detail-4 cone has
  801 facets; the doc's earlier "sphere" guess was wrong -- the sphere is 768).
- Together: the corruption needs the **editor's broader concurrent-init context**,
  not `make_brushes()`' Geogram calls in isolation.
- The culprit is **deterministically the `cone` brush** (all three captured
  reproductions stall in `build_polygon_fill facets=801 verts=801 corners=3200`,
  preceded by `thumbnail: brush 'cone'`; the 801-facet mesh is the detail-4 cone,
  not the sphere=768).
- An instrumented soak then showed the spinning cone mesh is **count-sane**
  (neither the post-join validator nor the `build_polygon_fill` guard fired). With
  bounded loops and no looping callees, that means **intermittent memory
  corruption of the cone mesh** -- NOT a static `make_brushes` defect, NOT generic
  concurrent Geogram (1.3M isolated builds are clean), and NOT the in-place
  `transform` aliasing (handled, and deterministic anyway). **Recommended next
  step: a HWASan/ASan editor build on a phone** to catch the corrupting write at
  its source. See "Findings from the instrumented soak" below.

This document describes the symptom, how to confirm it, the diagnostic logging
that pinpoints it, the controlled reproduction, and the candidate causes.

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

**REVISED 2026-06-02:** the "do NOT brute-force it" conclusion above was based on
soaking *with the validator active*. The validator itself was the suppressor;
once removed, a brute-force soak reproduced the hang at ~1/8. See "Controlled
reproduction" immediately below.

## Controlled reproduction (2026-06-02, Quest 3, Vulkan)

The bug was reproduced on demand by removing the suppressor and using an
optimized build, then soaking with a scripted clean-reinstall harness
(`scripts/soak_quest.py`).

- **Repro build:** Quest *release* (Android `release` -> CMake RelWithDebInfo,
  optimized) with `ERHE_DEBUG_VALIDATE_GEOMETRY 0` (validator OFF). It hung on
  run 8 of a clean-reinstall soak (HANG=1, PASS=7) -- consistent with the
  original ~1/9 rate. The 40 runs immediately before (debug, validator on) were
  all clean.
- **The validator was the suppressor.** Timeline: the original hang reproduced
  *with* the watchdog + breadcrumbs present but *before* the
  `ERHE_DEBUG_VALIDATE_GEOMETRY` validator was added. After it was added: 58
  consecutive clean runs (18 noted on 2026-06-01 + 40 on 2026-06-02, all debug).
  With it removed (+ release): hung at run 8. The validator does a full mesh walk
  per brush *on the worker thread*, inside the suspected concurrent-Geogram
  window, so it perturbs the timing -- a heisenbug, exactly like the rendezvous
  amplifier tried on 2026-06-01.
- **Captured signature** (`logs/soak_iter48_*.log`):

  ```
  [editor.startup] Init: editor ready, entering main loop          (tick thread)
  [erhe.primitive.primitive_builder] Warning: Used fallback smooth normal
  ... tick thread goes silent (spinning in build_polygon_fill) ...
  [editor.watchdog] Main loop STALLED: ... Stuck in phase:
      'primitive: build_polygon_fill facets=801 verts=801 corners=3200'
  ```

  It hung before completing frame 1 (first-frame thumbnail build). The mesh
  counts are **sane** (801 facets / 801 verts / 3200 corners ~= a quad mesh), so
  the spin is **internal per-facet `facet_ptr` corruption** (one facet's corner
  range is wrong), not a blown `nb()` -- confirming the "sane counts" prediction.
  The preceding `Used fallback smooth normal` shows the same mesh is already
  degenerate when normals are computed.

- **Detection harness** (`scripts/soak_quest.py`): clean-reinstall each run
  (cold SPIR-V), phase-aware watch (generous init/SPIR-V ceiling, tight
  post-init window), with `editor.cpp` emitting `Main loop: completed frame N`
  as the PASS signal; the watchdog `Main loop STALLED` line is the authoritative
  hang signal and it lingers 8 s after a stall to capture the breadcrumb ring.
  Healthy cold-start baselines measured: debug init ~6.5 s / post ~1.0 s; release
  init ~2.6 s / post ~0.5 s.

## Headless reproduction & cross-device investigation (2026-06-02, continued)

Goal: a minimal, headset-free ARM repro to decide *editor-misuse-of-Geogram* vs
*a Geogram bug*. Two tools were built.

### 1. `src/geogram_soak/` -- standalone headless harness (does NOT reproduce)

A tiny executable (built on desktop and Android) that mirrors
`Scene_builder::make_brushes()` with no rendering / SDL / Vulkan / headset: N
taskflow workers each build a brush-shape on their own `GEO::Mesh` and run the
same `Geometry::process()` flags, then validate after the join. It can also build
all ~92 Johnson solids (`--johnson <johnson.json>`) exactly as the editor does --
a single shared parsed `rapidjson::Document` read concurrently, and per solid the
`Json_library::make_geometry` body including `GEO::mesh_repair()` (the one Geogram
call the basic shapes never make). Run it on a device with
`scripts/run_geogram_soak.py` (pushes the arm64 ELF, runs via `adb shell`).

Knobs: `--workers N` (1 = sequential), `--multithread on|off` (Geogram
`sys:multithread`), `--johnson PATH`, `--iters`, `--batch`, `--detail`.

**Result: no reproduction.** >1.3M concurrent builds and ~1000 fresh-process cold
trials, on both the S23 and the Quest, with workers 8 and oversubscribed 24,
`multithread on`, with and without the Johnson/`mesh_repair` workload. This is
strong evidence **against** bucket (b) "independent per-thread `GEO::Mesh` builds
corrupt on their own" -- the isolated brush-build concurrency is clean.

### 2. Full `mobile` editor on a phone -- DOES reproduce (headset-free)

The `mobile` Gradle flavor builds the editor flat (no OpenXR). On a Samsung S23
(Snapdragon 8 Gen 2, ARM) it runs normally, and **soaking it reproduced the hang:
1 in 40 cold launches**, identical signature -- `build_polygon_fill` spin on
`facets=801 verts=801 corners=3200`. The breadcrumb ring names the culprit brush
as **`cone`** (the detail-4 cone is 801 facets; the sphere alongside it is 768,
so the doc's earlier "sphere" guess was wrong). Soak it with:

```
scripts\build_android.bat mobile assembleMobileRelease
ANDROID_SERIAL=<phone> py -3 scripts/soak_quest.py batch --start 1 --end 80 \
    --flavor mobile --build-type release --init-ceiling 90 --post-window 30
```

`soak_quest.py` was generalized for `--flavor mobile|quest`. A phone must be awake
+ unlocked (`adb shell svc power stayon true`) for the flat activity to stay
resumed. Rate is ~1/40 on S23 vs ~1/8 on Quest, but it is the same bug.

**The `mobile` editor does NOT run on the Quest**: the Quest's window manager
sleeps a flat 2D activity ~0.7 s into init (`SDL onStop()`, `isSleeping=true`),
and `Android_Vulkan_CreateSurface` then dereferences the torn-down window ->
SIGSEGV at `erhe::window::Context_window::create_vulkan_surface`. So the flat
editor can only be soaked on a phone, not the Quest.

### What this tells us about the cause

The isolated harness is clean (1.3M+ builds) but the **full editor reproduces it**
(both Quest immersive and S23 flat). The differentiator is the editor's broader
concurrent init: `make_brushes()` runs its nested brush taskflow on the **same
executor** that is simultaneously running ~20 other Layer-1 init tasks. The
corruption needs that contention (shared executor / heap / some process-global),
which the isolated harness lacks. This argues bucket (a) editor-misuse over (b),
and reopens whether the corrupt mesh is even produced inside `make_brushes` or
only damaged afterwards (e.g. heap corruption from a concurrent Layer-1 task).

### Instrumentation added to localize the corruption window

Two temporary validators (both reuse the new public
`erhe::geometry::validate_mesh_structure()`, a bounded structural check):

- **`Primitive_builder::build_polygon_fill` guard** -- validates the mesh at the
  spin site; on corruption logs `MESH CORRUPT at build_polygon_fill ...` (naming
  the bad facet) and **skips the fill** so the editor no longer hangs.
- **`Scene_builder::make_brushes` post-join validator** -- walks every brush mesh
  on the main thread right after the parallel build joins; logs `MESH CORRUPT
  after make_brushes join ...` if a mesh is already corrupt there.

If the post-join validator fires, the corruption happened **during** the parallel
brush build; if it stays silent but the fill guard fires, the corruption happened
**after** `make_brushes`. `soak_quest.py` now treats a `MESH CORRUPT` log line as
a reproduction (the guard prevents the spin, so detection is log-based, not the
watchdog stall). These validators are TEMPORARY -- remove once the root cause is
fixed.

### Findings from the instrumented soak (2026-06-02, late)

Re-soaked the instrumented `mobile` editor on the S23 via warm-relaunch
(`soak_quest.py batch --no-reinstall`, which relaunches the already-installed app
each iteration -- fresh process, no `adb install`, so it dodges the Samsung Auto
Blocker "Send app for a security check?" dialog and is faster). It reproduced
again. Key results:

- **The culprit is deterministically the `cone` brush.** All three captured
  reproductions (original Quest + both S23) stall in `build_polygon_fill
  facets=801 verts=801 corners=3200`, immediately after `thumbnail: brush 'cone'`.
  The 801-facet mesh is the detail-4 cone (the sphere is 768). Every other brush
  preview builds fine first. (Note: startup builds two cone geometries -- the
  unnamed `Handle_visualizations::make_arrow_cone()` and the named brush "cone";
  the breadcrumb `thumbnail: brush 'cone'` is the brush one.)
- **Neither validator fired.** The mesh's counts, per-facet corner counts, and
  corner sum are all SANE when `build_polygon_fill` spins.
- `build_polygon_fill`'s loops are bounded by exactly those (sane) counts, and its
  callees are straight-line attribute lookups (verified: `build_tangent_frame`,
  `build_vertex_normal`, etc. have no loops). So a spin on a count-sane mesh means
  either (a) the mesh is mutated *during* the walk, or (b) a corner's vertex index
  is out of range -- which `validate_mesh_structure` does NOT currently check --
  so `build_polygon_fill`'s `mesh_vertex_to_vertex_buffer_index[vertex]` write
  goes OOB and corrupts the heap (possibly the mesh's own arrays) mid-walk.

### What the evidence rules OUT

- **Not a standalone Geogram thread-safety bug.** The isolated `geogram_soak`
  harness did 1.3M+ concurrent independent `process()` builds (incl. `mesh_repair`,
  Johnson solids, oversubscribed) with zero corruption. Generic concurrent Geogram
  use is fine.
- **Not the in-place `transform(*cone, *cone, swap_xy)` aliasing.** `transform_mesh`
  guards every copy with `&source_mesh != &destination_mesh` (so `src==dst` skips
  the copy) and transforms points/attributes element-wise (alias-safe). And an
  aliasing bug is a deterministic code path -- it would hang every launch, not
  ~1/40.
- **Not any purely deterministic defect** in the cone build (a bad constant, a
  fixed off-by-one): intermittency (~1/40 S23, ~1/8 Quest) excludes them.

### Leading hypothesis + recommended next step

Intermittency + full-editor-only + deterministic cone *victim* + count-sane mesh
together point to **the cone mesh's memory being intermittently corrupted by
something OUTSIDE its own build**: one of (1) a *specific* unsynchronized write to
the cone mesh / a shared structure (not generic Geogram), (2) uninitialized
memory, or (3) a heap OOB / use-after-free from another init task that lands on
the cone mesh's allocation.

The decisive instrument for intermittent memory corruption in the full app is a
**HWASan (arm64) or ASan editor build run on the S23** -- it catches the bad
write/read at its source and names the culprit, instead of inferring from the
downstream spin. The Android CMake currently forces `ERHE_USE_ASAN OFF` (root
`CMakeLists.txt`), so that override must be lifted and the NDK sanitizer runtime
bundled into the APK. Cheaper interim steps: (a) tighten `validate_mesh_structure`
to bounds-check each corner's vertex index (the gap above) so the guard /
post-join validator can catch a bad-index form; (b) run the `build_polygon_fill`
iteration-cap soak -- the cap logs live-vs-entry mesh counts on overrun, so
*differing* counts prove concurrent mutation while *stable* counts point to a
pre-existing bad index / heap corruption.

### Tooling added this session (all on `main`)

- `src/geogram_soak/` + `scripts/run_geogram_soak.py` -- headless harness.
- `erhe::geometry::validate_mesh_structure()` -- shared bounded structural check.
- `scripts/soak_quest.py` -- generalized `--flavor mobile|quest`, added
  `--no-reinstall` (warm relaunch, dodges the install dialog), `MESH CORRUPT`
  detection (the guard prevents the spin, so detection is log-based), and an
  install-timeout guard.
- Temporary validators: `Primitive_builder::build_polygon_fill` guard + iteration
  cap, and the `Scene_builder::make_brushes` post-join validator.
- `AGENTS.md` -- how to disable the mobile Auto Blocker / Play Protect install
  dialog (or use `--no-reinstall`).

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

- **Next direction (2026-06-02):** build a minimal, head-less ARM repro to
  separate editor-misuse-of-Geogram from a bug in Geogram itself; see
  `prompt_queue.txt` for the plan handoff. Cheapest decisive test: set
  `threading.thread_count` to 1 (sequential `make_brushes()`) and soak -- if the
  hang vanishes, it is a concurrency issue.
- The watchdog + breadcrumbs are diagnostics only; they do not fix the hang.
  Once the log identifies the stuck phase on a recurrence, fix the actual loop
  (no band-aids).
- **Recommended:** prototype the guarded/serialized Geogram variant above and
  soak-test it (see "Decisive test").
- The `ERHE_DEBUG_VALIDATE_GEOMETRY` mesh validator
  (`src/erhe/geometry/erhe_geometry/geometry.cpp`) is now set to **0 (OFF)**: it
  was found to be the timing suppressor (see "Controlled reproduction"). For a
  non-perturbing detector, validate meshes AFTER the parallel join in
  `Scene_builder::make_brushes()` rather than inside `process()` on the worker
  thread. It remains a TEMPORARY diagnostic; remove once the root cause is fixed.
- A timing amplifier (rendezvous barrier forcing concurrent Geogram entry) was
  tried and REMOVED -- it suppressed the race rather than amplifying it, which
  is itself evidence the race is real but timing-subtle.
- If a future recurrence points the stuck phase elsewhere (a rendergraph node or
  `Graph::sort()`), consider a hard iteration cap with a loud error so a
  never-progress condition fails fast instead of spinning.
