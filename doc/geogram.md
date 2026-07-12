# Draft GitHub issue for BrunoLevy/geogram: concurrent use of geogram algorithms from multiple application threads

Status: DRAFT, not yet filed. Prepared 2026-07-12 from a live hang observed in
the erhe editor (Debug build, geogram at the erhe fork pin, base upstream
around `de1b4e61`). Sibling context: https://github.com/BrunoLevy/geogram/issues/367
(same application, different defect).

When filing, everything below the marker is the proposed issue body.

---

## Title

ParallelDelaunay3d cannot be used while other geogram threads are running
(`CellStatusArray::resize` asserts `!Process::is_running_threads()`) - request:
make geogram algorithms usable concurrently from multiple application threads

## What happened

Our application (the erhe 3D editor) runs geogram-based geometry operations on
a thread pool: mesh operations (subdivide, boolean, remesh, ...) execute on
worker threads while the main thread stays interactive. Some of those code
paths use `GEO::parallel_for`, and the main thread occasionally builds small
convex hulls via `GEO::Delaunay::create(3, "PDEL")`.

When the main thread entered `ParallelDelaunay3d::set_vertices()` while a
worker thread had geogram threads running (a `GEO::parallel_for` inside a mesh
build), this debug assertion fired:

```
geo_debug_assert(!Process::is_running_threads());   // delaunay_sync.h:220, CellStatusArray::resize
```

Call stack of the asserting (main) thread:

```
GEO::geo_abort                          geogram/basic/assert.cpp:80
GEO::geo_assertion_failed               geogram/basic/assert.cpp:117
GEO::CellStatusArray::resize            geogram/delaunay/delaunay_sync.h:220
GEO::CellStatusArray::resize            geogram/delaunay/delaunay_sync.h:243
GEO::ParallelDelaunay3d::set_vertices   geogram/delaunay/parallel_delaunay_3d.cpp:2661
erhe::geometry::make_convex_hull        (application)
```

The precondition is documented ("no concurrent thread is currently running"),
so this is arguably by design - but the design makes `ParallelDelaunay3d`
(and, we suspect, other `Process::run_threads`-based algorithms) unusable in
any application that runs geogram work on more than one thread, even when the
two concurrent computations touch completely disjoint data.

## Secondary issue: geo_abort() blocks on getchar() on Windows

On Windows, `geo_abort()` does:

```cpp
void geo_abort() {
#ifdef GEO_OS_WINDOWS
    std::cerr << "Aborting, press any key to continue" << std::endl;
    std::getchar();
#endif
    ...
    abort();
}
```

In an unattended process (headless CI / automation, no console reader), the
`getchar()` never returns, so a failed assertion turns into a silent, eternal
hang instead of a crash. A watchdog sees the process stall, but nothing ever
terminates it. Please consider gating the interactive prompt on something like
`_isatty(_fileno(stdin))`, or an environment variable / `AssertMode`, so
non-interactive processes fail fast.

## What we would like

1. Geogram algorithms usable concurrently from multiple application threads,
   at least when operating on disjoint data. Concretely for this report:
   `ParallelDelaunay3d` either (a) coordinating with `Process` so concurrent
   use is safe, or (b) failing over to the sequential implementation instead
   of asserting when other threads are running.
2. If full concurrency is out of scope: an explicit, documented thread-safety
   contract for the parallel algorithms (which entry points require exclusive
   ownership of geogram's process-global thread state), so applications can
   arrange their own serialization.
3. The `geo_abort()` getchar prompt made non-interactive-safe (see above).

## Our workaround

We switched `make_convex_hull` to the sequential `"BDEL"` Delaunay (hull
inputs are small, so the parallel build gained nothing), and we set
`GEO::set_assert_mode(GEO::ASSERT_THROW)` explicitly when no debugger is
attached, so future geogram assertions surface as catchable exceptions
instead of an interactive abort prompt.
