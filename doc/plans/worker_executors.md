# Worker executors: foreground and background

Status: in progress

Requirements for running editor worker tasks on two Taskflow executors: a
foreground executor at normal thread priority and a background executor
whose threads run at lowered priority. Both run in parallel at full width
and the operating system's scheduler arbitrates between them, so background
work yields CPU time to interaction without a task-count budget in the
editor. This document holds the requirements, the facts they rest on, the
design and the build order. It extends `doc/erhe/task.md`.

## Why

A burst of worker tasks competes with the main and render threads at equal
priority, and work done on the main thread to avoid that stretches frames
instead. The case that raised it: the Level Kit window builds every kit
shape once for its thumbnail, two per frame on the main thread
(`doc/editor/level_tools.md` "Shape thumbnails"); in a Debug build under a
debugger the frame's update phase averaged 58 ms for one 60-frame window
while about 240 meshes were built in the first five seconds. Moving such work
to worker tasks removes the main-thread cost only when the workers themselves
do not slow interaction. The requirement is general; kit thumbnails are one
user of it.

## Facts the requirements rest on

- The editor creates one `tf::Executor` in the `Editor` constructor
  (`src/editor/editor.cpp`), `threading.thread_count` workers wide (a value
  of 0 or less: one per hardware thread; the definition's default is 8), and
  publishes it as `App_context::executor`. Its width is fixed for the
  session.
- Every task spawn goes through the `erhe::task` wrappers (`spawn`,
  `spawn_dependent`, `run`, `emplace`); `scripts/check_task_spawns.py`
  enforces it. There are 29 call sites in 18 files, among them glTF load
  (`erhe_gltf/gltf_fastgltf.cpp`, `assets/gltf_load_task.cpp`), the brush
  geometry queue (`brushes/brush_geometry_queue.cpp`), texture file loading,
  the navigation build, the lightmap partitioner and streamer, the PVS page
  streamer, shader session programs, shader analytics and scene export.
- Library code receives its executor from the caller (`erhe::gltf`,
  `erhe::scene_renderer` PVS page streamer, the `erhe::raytrace` task
  spawner), and the same library path serves work the user waits for and
  work nobody waits for.
- Taskflow 4.1.0 has no task priority and no thread priority setting, and an
  executor's worker count is fixed at construction.
- `tf::Executor` takes a `tf::WorkerInterface` whose
  `scheduler_prologue(worker)` runs on each worker thread before its
  scheduling loop: the place to set a thread's OS priority and name.
- Thread priority arbitrates CPU time only. Background threads still share
  SMT sibling cores, the package's power and thermal limit, memory bandwidth
  and mutexes with the main and render threads.
- `tf::Executor` reports `num_workers()`, `num_waiters()` (workers parked
  with nothing to run), `num_topologies()` and `this_worker_id()` (-1 on a
  thread that is not one of its workers). It reports no count of queued
  tasks.
- `Scene_commit_queue::flush()` applies every pending commit in one tick of
  the main thread. Its users are work the user waits for (deferred glTF
  finalize, make-raytrace).
- Work nobody waits for hands its results over through state its owner
  polls while it draws or updates: a brush's geometry slot, the texture file
  loader's and the lightmap streamer's `finished` / `ready` flags, the kit
  thumbnail queue.
- `App_context::is_scene_load_in_flight()` is the one predicate behind
  `get_async_status` "idle" and the editor's own gates (scene save, the
  lightmap window, scene views): `pending_async_ops`, `running_async_ops`,
  queued operations, pending scene commits, asset load tasks and pending
  kit refreshes. The benchmark settle condition reads brush geometry
  idleness beside it, as a term of its own.
- `Brush_geometry_queue` keeps its own pending list, most recently requested
  first, and caps its tasks in flight at the executor's worker count less
  one. A consumer that must have a brush's geometry (`Brush::get_geometry()`,
  tier 1 of `doc/editor/brushes.md`) runs the generator on the calling
  thread, or waits for the worker that is already preparing it.
- A kit thumbnail's cost on the main thread is `Level_kits::make_brush()`
  (the shape's geometry and part geometries) and `Brush::get_scaled()`
  (`late_initialize()`: renderable meshes and a ray-trace BVH per part),
  then one `Brush_preview::render_parts_preview()`.
- Editor shutdown stops the shader session programs, calls
  `wait_for_all()` on the executor, waits for `Scene_payload_release`,
  clears `Item_async_task_guard` and `Scene_commit_queue`, then destroys the
  executor.
- Two mutexes guard scenes against tasks. Each `Scene_root` has
  `item_host_mutex` for its hosted state (`doc/editor/coding_rules.md`
  "Scenes and tasks"). `App_scenes::m_mutex` guards the set of open scenes:
  `register_scene_root()`, `unregister_scene_root()` and
  `is_host_registered()` lock it; `get_scene_roots()` returns the vector
  without it.
- How today's tasks use them, as far as read for this plan: an async item
  operation locks `item_host_mutex` on the main thread while it gathers its
  input; workers that apply to a scene check `is_host_registered()` first
  (`async_raytrace_kickoff_operation.cpp`, `scene_root.cpp`);
  `Editor::on_close_scene()` shows no lock of `item_host_mutex`; and
  `reset_editor_state` discards the queued operations before it closes the
  scenes. Whether every path is covered is not established (build step 3).
- A task keeps alive what it uses by shared ownership, and a closed scene's
  payload is freed by a task of `Scene_payload_release`.
- Work outside the executor: the BVH builder (`erhe_raytrace/bvh`), geogram
  and xatlas run thread pools of their own, at normal priority.
- A thread holding a GL worker context slot never spawns tasks
  (`doc/erhe/gl_worker_context_enforcement.md`); the Debug task guard
  (`attach_gl_context_task_guard`) observes the executor.

## Requirements

### W1. Two executors

- W1.1 The editor owns two executors: the foreground executor and the
  background executor. Both live for the whole session and run tasks in
  parallel; the editor places no limit on the number of tasks running on
  the two together.
- W1.2 The foreground executor runs work the user waits for (opening a
  scene, an import, an export the user started) and all work of editor
  startup.
- W1.3 The background executor runs work nobody waits for: thumbnails,
  prewarming, deferred geometry, and streaming (lightmap tiles, texture
  file decode, PVS pages) outside a scene load. Streaming requested while a
  scene is loading runs on the foreground executor.
- W1.4 The code that starts a piece of work names its executor, and the
  choice is visible there. Tasks spawned inside that work, library code
  included, run on the executor of the work they belong to.
- W1.5 `scripts/check_task_spawns.py` verifies that every spawn names or
  inherits an executor through the `erhe::task` wrappers.
- W1.6 Per-worker-thread setup (thread name, profiler registration, the
  Debug GL context task guard, any per-thread library state) runs on the
  workers of both executors.

### W2. Background workers run at lowered priority

- W2.1 Every worker thread of the background executor runs at an OS
  priority below the main thread, the render thread and the foreground
  workers, on Linux, Windows, macOS and Android.
- W2.2 The lowering is moderate: a background thread that holds a mutex the
  main thread needs still gets CPU time while the foreground executor is
  saturated. The levels are nice 10 on Linux and Android,
  `QOS_CLASS_UTILITY` on macOS and `THREAD_PRIORITY_BELOW_NORMAL` on
  Windows.
- W2.3 The priority is set once per worker thread when it starts.
- W2.4 A platform where lowering the priority fails logs one warning at
  startup and runs the background workers at normal priority.
- W2.5 The main thread, the render thread and the foreground workers keep
  the priority they have today.

### W3. Executor thread counts

- W3.1 Each executor has a thread count setting of its own: one for the
  foreground executor and one for the background executor.
- W3.2 The foreground setting keeps the meaning and the default that
  `threading.thread_count` has today (a value of 0 or less: one worker per
  hardware thread).
- W3.3 The background setting defaults to 4 threads. It is the one control
  for the costs priority does not arbitrate (SMT siblings, power and
  thermal limits, memory).
- W3.4 Both settings are read when the executors are constructed; the
  settings UI states that they take effect at the next start of the editor.

### W4. Results on the main thread

- W4.1 The main-thread time a frame spends applying background results is
  measured against a per-frame soft limit. A frame that exceeds it logs a
  warning naming the owners and their times; no result is held back for
  the limit.
- W4.2 Results of foreground work are applied in full on the tick they
  arrive, as today.
- W4.3 The frame on which a background result is applied varies from run
  to run, under `ERHE_DETERMINISTIC_CLOCK` too. The state after all pending
  results are applied is the same whatever the order and frames of their
  arrival, and scripts and tests wait for that state through W5.3.
- W4.4 A consumer that needs a background result now (the user clicks a
  thing whose data is still queued) gets it without waiting behind the
  background queue: the owner of the pending work runs that item on the
  calling thread or on the foreground executor. An item whose background
  task is already running is awaited.

### W5. Lifetime

- W5.1 Background work runs to completion: the editor drops no queued
  background task and discards no result because a scene closes or the
  editor state is reset.
- W5.2 A task of either executor that works on a scene's hosted state holds
  the scene's `item_host_mutex` while it does. Scene close and
  `reset_editor_state` take that lock, so they wait for such a task to
  release it before the scene's state is torn down.
- W5.3 `get_async_status` reports background work that is queued, running
  or waiting to be applied, per owner, with a verdict of its own
  (`background_idle`). Background work that changes scene state also counts
  in `App_context::is_scene_load_in_flight()`; background work that changes
  none (thumbnails, prewarming) stays out of it, so it holds up no save and
  no scene gate.
- W5.4 A task keeps alive what it uses by shared ownership; shutdown waits
  for both executors as it waits for the one executor today.
- W5.5 The set of open scenes is guarded by a mutex of its own. Adding and
  removing a scene hold it, and so does every read of the set from a task;
  a task learns under it whether its scene is still open.

### W6. Observability

- W6.1 The editor reports, on request (MCP and a window), per executor: the
  worker count, the thread priority in effect, the busy workers and the
  running task graphs; and per owner of background work, the items queued,
  running and waiting to be applied.
- W6.2 The "Phase timing" log line or a line beside it carries the number
  of busy workers of each executor, so a slow frame can be read against
  worker load.

### W7. First users

- W7.1 Kit thumbnails build their geometry on the background executor; the
  main thread uploads and renders the ones that are ready.
- W7.2 The existing spawn sites are each assigned to an executor per W1.4;
  the assignment list is part of the design.
- W7.3 `Brush_geometry_queue` runs deferred brush geometry on the
  background executor; its tier 1 consumers are its W4.4 path.

### W8. Acceptance measurement

- W8.1 A scripted Release run opens the Level Kit window on a fresh start
  and records frame times while all kit thumbnails build. It runs three
  ways: thumbnails on the main thread (today), background executor at its
  default 4 threads, background executor at the foreground thread count.
- W8.2 Goal, not a pass condition: with the background executor at its
  default thread count, no frame of the burst is longer than twice the
  median frame of the same scene at rest. The run reports the longest and
  the 99th percentile frame of the burst against that figure.
- W8.3 The run at the foreground thread count is recorded beside it, as the
  measure of what the default costs in thumbnail completion time and saves
  in frame time.
- W8.4 Editor startup time to the first interactive frame is no longer than
  it is today.
- W8.5 The measurement is made on desktop. The same runs on the Quest,
  which give the background thread count default for the device, are made
  when a headset is available; until then the Quest uses the desktop
  default.

## Known limits

- The thread pools outside the executors are not arbitrated by the
  executors. On Linux a thread inherits the nice value and the name of the
  thread that creates it and cannot raise its priority again. The BVH build
  pool and the oneTBB workers are shared by every caller and are started
  from the main thread at startup (D3.4), so they run at normal priority
  for background callers too. Geogram's OpenMP pools are per calling thread
  and carry its priority: a background worker's pool runs lowered, and
  every thread that runs a geogram `parallel_for` keeps its pool threads
  for the session. A Jolt job pool takes the priority of the thread that
  constructs its `Scene_root` (D3.5).
- Lowered priority leaves the shared costs named in the facts above; the
  background thread count (W3.3) is the control for them.
- A disk cache of kit thumbnail images is a plan of its own.

## Design

### D1. `erhe::task` makes the executors

- D1.1 `erhe::task::make_executor(const Executor_create_info&)` returns a
  `std::unique_ptr<tf::Executor>`. `Executor_create_info` holds a name, a
  thread count and a `Worker_priority` (`normal`, `lowered`). The function
  constructs the `tf::Executor` with a `tf::WorkerInterface` whose
  `scheduler_prologue` names the thread (`<name> <index>`, for the profiler
  and the OS) and applies the priority.
- D1.2 `Worker_priority::lowered` is, on the worker thread itself:
  `setpriority(PRIO_PROCESS, gettid(), 10)` on Linux and Android (a thread
  id sets the nice value of that one thread),
  `pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0)` on macOS,
  `SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL)` on
  Windows. Lowering a thread's own priority needs no privilege on any of
  them.
- D1.3 The prologue records, under a mutex, whether the call succeeded and
  the value read back (`getpriority`, `pthread_get_qos_class_np`,
  `GetThreadPriority`). `erhe::task::get_worker_priority_report(executor)`
  returns it for W6.1; the first failure logs the one warning of W2.4.
- D1.4 `erhe::task::Executors` is a small class holding references to the
  foreground and the background `tf::Executor`. `get_inherited()` returns
  the background executor when the calling thread is one of its workers
  (`this_worker_id() >= 0`) and the foreground executor otherwise.
- D1.5 A worker of a `Worker_priority::lowered` executor made by
  `make_executor()` spawns onto its own executor only. The worker's
  prologue records its priority in a `thread_local`; the spawn wrappers
  check it beside the GL worker context check and `ERHE_FATAL`, naming the
  spawn site, when such a worker spawns onto an executor it is not a worker
  of. No lock and no registration. This is W1.4 enforced, and it is what
  makes the shutdown order of D5.6 complete.
- D1.6 The wrappers keep taking `tf::Executor&`. The executor a task runs
  on is the one its spawn names; nothing is queued or gated in the library.

### D2. The editor owns two executors

- D2.1 `Threading_config` (version 5) gains `background_thread_count`,
  default 4; `thread_count` keeps its name, meaning and default and is
  shown as the foreground thread count. Both rows say "Read at startup".
  A background value of 0 or less is one worker per hardware thread.
- D2.2 `Editor` constructs `m_foreground_executor` (`normal`, name
  "Worker") and `m_background_executor` (`lowered`, name "Bg worker":
  a Linux thread name holds 15 characters), and attaches the Debug GL
  context task guard to both.
- D2.3 `App_context::executor` is replaced by
  `App_context::foreground_executor`, `App_context::background_executor`
  and `App_context::executors` (D1.4). There is no member named
  `executor`, so every use site names its executor and the compiler finds
  the ones that do not.
- D2.4 A class that holds a `tf::Executor&` keeps holding one; the code
  that constructs it (mostly `Editor`) passes the executor of the "Executor
  assignment" table.
- D2.5 Library code keeps receiving its executor from its caller, and
  passes it on to what it spawns. The `erhe::raytrace` task spawner, which
  is one function for the process, spawns onto
  `executors.get_inherited()`.
- D2.6 The streaming sites choose at each spawn:
  `App_context::get_streaming_executor()` returns the foreground executor
  while `is_scene_load_in_flight()` and the background executor otherwise.
  `Pvs_page_streamer` (a library class) receives that choice as a function
  from the editor in place of its executor pointer.
- D2.7 `scripts/check_task_spawns.py` additionally flags a `tf::Executor`
  constructed in `src/editor` or `src/erhe` outside `src/erhe/task` and the
  test directories, so no executor exists without the setup of D1.1.

### D3. Rules for code that uses the executors

- D3.1 A task of the foreground executor never blocks on background work
  (a `wait()` on a future of the background executor, a condition variable
  a background task signals). The main thread does so only through a W4.4
  path that is documented at its owner.
- D3.2 A task of the foreground executor may spawn onto the background
  executor (work that follows a load and that nobody waits for).
- D3.3 The GL worker context slots are one pool for both executors. A
  background task holds a slot for the GPU allocation only, as
  `App_scenes::build_display_color_primitive()` does.

- D3.4 A thread pool that outlives the task that first uses it and is
  shared by every caller (the BVH build pool, the oneTBB workers) is
  started from the main thread at editor startup, before the executors
  exist (`start_outside_thread_pools()`).
- D3.5 A `Scene_root` is constructed on the main thread or by foreground
  work, never by background work: its physics job pool takes the
  constructing thread's priority.

### D4. Results of background work on the main thread

- D4.1 Background work hands its results to its owner through a
  mutex-guarded ready list of the owner; the owner applies them in its
  existing prepare-phase or draw call. `Scene_commit_queue` stays the path
  of foreground work and applies in full (W4.2).
- D4.2 `Background_apply_meter` (editor, owned by `Editor`, published in
  `App_context`) is the measurement of W4.1. An owner applies its ready
  items inside a `Background_apply_meter::Scope` that names the owner and
  adds the wall-clock duration to the frame's total. `end_frame()` compares
  the total with the setting `threading.background_apply_soft_limit_ms`
  (default 2.0, read at each `end_frame()`) and logs the warning with the
  per-owner times when it is exceeded, at most once per second.
- D4.3 An owner applies every ready item on the frame it finds it; the
  meter defers nothing.
- D4.4 The first owner is `Kit_thumbnails` (D6). The texture file loader
  and the lightmap streamer put their applies inside a scope as well, so
  the warning names them.

### D5. Lifetime

- D5.1 Two locks, in the roles they have today: `App_scenes::m_mutex` for
  the set of open scenes and each scene's `item_host_mutex` for its hosted
  state. Lock order: a thread never acquires a scene's `item_host_mutex`
  while it holds `App_scenes::m_mutex`; the reverse nesting (the set mutex
  taken briefly inside a scene lock) is the allowed one.
- D5.2 A task that works in a scene holds a `std::shared_ptr<Scene_root>`
  (or locks a weak one), locks the scene's `item_host_mutex`, then asks
  `App_scenes::is_host_registered()`. Registered: it does its work under
  the scene lock. Not registered: the scene closed before the task got
  there, and the task returns without touching it.
- D5.3 Scene close removes the scene from the set under
  `App_scenes::m_mutex`, releases that mutex, then locks the scene's
  `item_host_mutex` for the teardown of its hosted state. A task already
  inside the scene finishes its locked section first; a task that arrives
  later sees the scene unregistered (D5.2). `reset_editor_state` closes
  each scene this way.
- D5.4 `App_scenes::get_scene_roots()` is main-thread only and says so
  with a thread check; a task that needs the set uses an accessor that
  copies it under the mutex.
- D5.5 A background task holds the lock for the part of its work that
  touches the scene and no longer: the main thread waits on a lowered
  priority thread for as long as the lock is held (W2.2). Work that needs
  no scene state (kit thumbnails, brush geometry) captures its input by
  value or by shared ownership and takes no scene lock (the rule of
  `doc/editor/brushes.md` G7).
- D5.6 Shutdown, in this order: owners of pending background work stop
  taking requests (`Brush_geometry_queue`, `Kit_thumbnails`, the shader
  session programs); `m_foreground_executor->wait_for_all()`;
  `m_background_executor->wait_for_all()`; then the existing sequence
  (`Scene_payload_release`, `Item_async_task_guard`, `Scene_commit_queue`),
  and both executors are destroyed.
  The foreground executor is drained first because its tasks may spawn
  background work (D3.2) and never the reverse (D1.5).
- D5.7 An owner's `stop()` at shutdown drops its pending list, as
  `Brush_geometry_queue_state::stop()` does today. This is the end of the
  session, not a scene close: W5.1 holds.

### D6. Kit thumbnails

- D6.1 `Kit_thumbnails::queue()` puts the shape on a pending list (most
  recently requested first, as the brush queue) and starts build tasks on
  the background executor, at most `num_workers()` in flight.
- D6.2 A build task owns its input by value (`Kit_parameter_values`, the
  `Build_info`, the normal style, the mesh key). It makes the part
  geometries (`make_kit_part_geometries()`, or `make_kit_geometry()` for a
  one-slot shape) and one `erhe::primitive::Primitive` per part with its
  renderable mesh (fill triangles only), inside a `Scoped_worker_context`
  for the GPU allocation where the device has worker contexts. It makes no
  brush, no collision shape and no ray-trace BVH.
- D6.3 The task puts `{shape index, mesh key, part primitives | failure}`
  on the ready list. `Kit_thumbnails::update()` (prepare phase) takes every
  ready item inside a meter scope (D4.2): an item whose mesh key is still the one
  wanted is rendered with `Brush_preview::render_parts_preview()` and its
  primitives released; an item of superseded lengths is released unrendered
  (its shape is already queued again).
- D6.4 The task state is co-owned by `Kit_thumbnails` and the tasks in
  flight (`shared_ptr`, as `Brush_geometry_queue_state`).
- D6.5 `Kit_thumbnail_state` gains `building`. `level_get_kit_thumbnails`
  reports it, and takes `request_all` to queue every shape without drawing
  its row (for the measurement of W8).
- D6.6 On a device without worker contexts the build runs in `update()` on
  the main thread, one shape per frame, as
  `Geometry_graph_window` falls back today.

### D7. Brush geometry queue

- D7.1 `Scene_builder` constructs `Brush_geometry_queue` with the
  background executor. Its cap becomes `num_workers()`: the executor is no
  longer shared with the work the old "less one" left room for.
- D7.2 Tier 1 consumers are unchanged and are the W4.4 path.

### D8. Reporting

- D8.1 `get_async_status` gains `background`: per owner (brush geometry,
  kit thumbnails, later owners) `queued`, `running`, `ready`; and
  `background_idle`. `idle` keeps its meaning (W5.3).
- D8.2 MCP `get_executor_status` and a "Workers" section of an editor
  window (which window is chosen at the step) report per executor: name, worker count,
  requested and read-back priority (D1.3), busy workers
  (`num_workers() - num_waiters()`) and `num_topologies()`; and the owners
  of D8.1.
- D8.3 The "Phase timing" line gains `workers fg <n> bg <n>`: the busy
  worker counts sampled once per tick and averaged over the line's frames.

## Executor assignment

A site moves to the background executor only when it is listed here as
background; everything else runs on the foreground executor, as all of it
does today.

| Site | Executor |
|---|---|
| `items.cpp` async item operations | foreground |
| `assets/gltf_load_task.cpp` scan, parse, build; `Asset_manager` load ticks | foreground |
| `erhe_gltf/gltf_fastgltf.cpp` | the caller's |
| `navigation/navigation_build.cpp`, `scene/navigation_build_job.cpp` | foreground |
| `assets/scene_export.cpp` | foreground |
| `renderers/lightmap_partitioner.cpp` prepare | foreground |
| `geometry_graph/geometry_graph_window.cpp` evaluate | foreground |
| `renderers/programs.cpp` (startup shader programs) | foreground |
| `transform/transform_tool.cpp`, `scene/scene_builder.cpp` startup work, `Draw_list_scene_dependencies` | foreground |
| `erhe::raytrace` task spawner | inherited (D2.5) |
| `brushes/brush_geometry_queue.cpp` | background |
| `level/kit_thumbnails.cpp` (new) | background |
| `scene/scene_payload_release.cpp` | background |
| `renderers/shader_analytics_service.cpp` | background |
| `renderers/shader_session_programs.cpp` statistics and analysis jobs | background |
| `asset_browser/asset_browser.cpp` directory scan and glTF scan | background |
| `renderers/lightmap_streamer.cpp` tile load | foreground during a scene load, else background (D2.6) |
| `graphics/texture_file_loader.cpp` decode | foreground during a scene load, else background (D2.6) |
| `erhe_scene_renderer/pvs_page_streamer.cpp` | foreground during a scene load, else background (D2.6) |

## Build order

Each step is one commit that builds the `editor`, `src/example`,
`src/hello_swap` and `src/hextiles` targets and passes
`scripts/check_task_spawns.py`, the unit tests and `ctest -L editor`.

1. **Baseline measurement.** `scripts/worker_executors_bench.py`: launches
   the Release editor on the wall clock with `ERHE_DEFAULT_LAYOUT=1`, waits
   for `get_async_status` idle, records the resting frame times, opens the
   Level Kit window, requests every thumbnail and records frame times until
   all are `current`. It reports median, 99th percentile and longest frame
   and the burst's duration, and startup time to the first frame after the
   startup scene is idle (the "first interactive frame" of W8.4). This step
   adds the per-frame times it needs to an MCP tool when
   `get_frame_pacing_status` lacks them, and `request_all` (D6.5). The
   numbers of this run are the "today" column of W8.1, recorded in
   `doc/reference/`.
2. **`erhe::task::make_executor`** (D1.1 - D1.3): the worker interface,
   names, the priority calls per platform, the report, and a unit test
   (`erhe_task_tests`) that a lowered executor's workers report the lowered
   value and a normal one's do not. `Editor` constructs its one executor
   through it. `check_task_spawns.py` gains D2.7. `CHANGELOG.md`.
3. **Scenes and tasks** (D5.1 - D5.5; fixes today's code, independent of
   the second executor): read every task that touches a scene's hosted
   state and every path of `Editor::on_close_scene()` and
   `reset_editor_state`, bring them to the lock rules, add the thread check
   and the copying accessor of D5.4. Verified by a test that closes a scene
   while async item operations and a glTF load of it are in flight, run in
   the thread sanitizer tree as well. `doc/editor/coding_rules.md` states
   the rules.
4. **Two executors in the editor** (D1.4, D1.5, D2, D5.6): `Threading_config`
   version 5, both executors, the `App_context` members, every construction
   and use site given the foreground executor. This step moves no site:
   behavior is unchanged and the background executor is idle. The Debug
   task guard on both, the background-spawn check, the shutdown order.
   `doc/erhe/task.md` and `doc/editor/editor.md` updated.
5. **Reporting** (D8): `get_async_status` `background`, `get_executor_status`,
   the window section, the "Phase timing" addition; `mcp_tools.json` and
   `mcp_server_tests`.
6. **Background sites that need no new code path** move to the background
   executor, after the outside pools are made safe (D3.4):
   `Brush_geometry_queue` (D7), `Scene_payload_release`, shader analytics
   and session program jobs, the asset browser scans, each with its owner's
   counts in D8.1. Built as one commit.
7. **`Background_apply_meter`** (D4.2 - D4.4) with its setting, the
   warning, and the streaming sites' executor choice (D2.6) with their
   scopes.
8. **Kit thumbnails on the background executor** (D6): the build task, the
   ready list, the metered `update()`, the `building` state, the fallback
   of D6.6. Verified with `level_tools_verify.py` (its thumbnail checks
   wait on `background_idle`), a run of the thread sanitizer tree over the
   thumbnail burst, and thumbnail images compared with those of step 1.
   `doc/editor/level_tools.md` "Shape thumbnails" rewritten.
9. **Acceptance measurement** (W8): the script of step 1 at background
   thread counts 4 and the foreground count; the three columns recorded in
   `doc/reference/`, W8.2 and W8.4 read against them. The Quest runs stay
   in `doc/plans/open_items.md`.

Steps 2, 4 and 5 change no behavior a user sees. Step 3 corrects scene
close under running tasks. Step 6 is where background work first runs at
lowered priority; step 8 is the one that removes the hitch that raised this
plan.

## State

- Build steps 1 - 9 are built and verified on desktop (Linux, Vulkan):
  `erhe_task_tests`, `ctest -L editor`, the level / curve / navigation
  verify scripts, the scene-close test, and the user's hands-on test of the
  windowed editor.
- Acceptance (`doc/reference/worker_executors_bench.md`): with the default
  4 background threads the longest frame of the kit thumbnail burst is
  about twice the resting median (the W8.2 goal: met in two runs of three,
  missed by 0.12 ms in the third), against 17 times with the main-thread
  build; the burst completes in 0.10 s against 0.54 s; startup is unchanged
  (W8.4). At the foreground thread count the burst completes in 0.05 s and
  its longest frame is 4.7 - 8.4 ms: many thumbnails become ready in one
  frame and the main thread renders them all (W4.1 warns there).

## Remaining

- The Quest runs of W8.5 (`doc/plans/open_items.md` "Quest session
  follow-ups").
- The user's review of R5.
- The macOS and Windows branches of `Worker_priority::lowered` and the
  embree branch of `erhe::raytrace::start_thread_pool()` are not compiled
  yet; the first build on each platform is their check.
- Open items found on the way (`doc/plans/open_items.md`): the thread
  sanitizer run of scene close under tasks; `Mesh_operation::make_entries()`
  holding the scene lock across its geometry work; verify checks that
  depend on the window set of `desktop_windows.json`.
- Not enforced in code: D3.5 (a `Scene_root` is never constructed by
  background work). The oneTBB warm-up of D3.4 waits until every worker has
  arrived; it has no bound if oneTBB supplies fewer threads than it reports.
- `scripts/level_tools_verify.py` runs clean with `--cwd <copy of the
  committed config>`.

## Rulings

- R1. Streaming sites (lightmap tiles, texture file decode, PVS pages) run
  on the foreground executor during a scene load and on the background
  executor otherwise (W1.3, D2.6).
- R2. A background worker that spawns onto the foreground executor is
  fatal (D1.5); the rule is revisited when a real case needs the spawn.
- R3. The foreground setting keeps the name `thread_count` (D2.1).
- R4. The apply limit is soft: a warning when exceeded, nothing held back
  (W4.1, D4).
- R5. Kit thumbnails are built without a brush (D6.2). Chosen by the
  agent; open to the user's review.
