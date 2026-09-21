# Deferred brush geometry

Status: proposed

The brush palette prepares the geometry of a brush when something needs it:
at startup only the brushes the startup script places, later the brushes the
UI shows and the brushes the user places. Extends `doc/editor/brushes.md`
(`Brush`, `Brush_data`) and `doc/editor/scene.md` (`Scene_builder`).

## Why

`Scene_builder::make_brushes` builds a `Geometry` and runs the full
`Geometry::process()` for every palette brush - 7 platonic solids, the curved
families (sphere, torus, 2 cylinders, cone, capsule) and 92 Johnson solids -
and the main thread waits for all 104. The default `commands.json` places the
7 platonic solids. Measured on the Debug windowed editor (Tracy, 24 workers,
commit 78015a6d3): the wait is 265 ms of a 1.68 s startup to frame 12, and
about 95 % of the 2.6 s of worker CPU inside it prepares brushes the startup
scene does not use. The platonic task alone ends at 213 ms only because it
shares the workers with the other 97 geometries.

## Requirements

- R1. A `Brush` exists, with its final name, flags and folder placement, when
  `Scene_builder`'s constructor returns, for every brush of the palette the
  `scene_config` flags enable. Names are the persistence contract of the
  builtin asset keys `{builtin, brush, <name>}` that `Editor::Editor`
  registers right after the constructor, and of the sorted palette folders.
- R2. A brush has a geometry state: `unprepared`, `queued`, `preparing`,
  `ready`, `failed`. `ready` means the `Geometry` is built and
  `Geometry::process()` has run with the brush's process flags, and the facet
  statistics (`get_max_corner_count` and friends) are filled.
- R3. Two tiers of consumers.
  - Tier 1, "must have geometry": placing the brush in a scene
    (`Brush::make_instance`, `Brush::get_scaled`, `Brush::get_reference_frame`,
    `Brush_tool` placement and drag-and-drop preview, the `place_brush` MCP
    tools, `Scene_builder::make_mesh_nodes`), using it as a graph source
    (`geometry_source_nodes.cpp`), and writing it to a file (glTF / USD brush
    export). A tier 1 consumer asks for the geometry and gets it `ready`: the
    call returns after preparation has finished, preparing on the calling
    thread when the brush is `unprepared` or `queued`, waiting for the worker
    when it is `preparing`.
  - Tier 2, "benefits from geometry": brush thumbnails (item tree rows, hotbar
    and inventory slots, the geometry graph's brush picker - every
    `Thumbnails::draw` whose callback reaches `Brush_preview::render_preview`)
    and the `get_scene_brushes` MCP query. A tier 2 consumer requests
    preparation and returns at once; while the brush is not `ready` a
    thumbnail shows a spinner and the MCP query reports the state.
- R4. Startup prepares exactly the brushes the startup script places: a
  `scene.add_*` command is a tier 1 consumer of the brushes it instantiates,
  so a `commands.json` that adds Johnson solids or curved shapes, or none at
  all, needs no change here. Nothing else is prepared before frame 1.
- R5. Preparation runs on executor workers, several brushes at a time. Tier 2
  requests are served most-recently-requested first, so the brushes visible
  in the UI right now overtake the ones that scrolled out of view; a request
  for a brush that is already `queued` moves it to the front.
- R6. A preparation task owns everything it reads: the polyhedron source
  (`Json_library`), the `Build_info` and the generator's parameters. It holds
  the brush through a `std::weak_ptr` and gives up when the brush is gone.
- R7. The main thread stays out of the geometry work for tier 2: a frame that
  shows 100 unprepared brushes queues 100 requests and draws 100 spinners.
- R8. `Brush` state that a worker and the main thread both touch is guarded by
  one mutex per brush and a condition variable for the tier 1 wait (no
  atomics). The content-library mutex is not held while a geometry is
  prepared.
- R9. A brush whose preparation fails (`Json_library::make_geometry` returns
  false, zero facets) is `failed`: tier 1 consumers get a null geometry and
  refuse the action with a log line naming the brush, tier 2 consumers draw
  the brush icon. Today such a brush is not created at all; R1 creates it, so
  the palette keeps its name.

## Design

- D1. Geometry source. Every palette brush is created with
  `Brush_data::geometry_generator` and no `geometry`. The generator is the
  whole recipe - build the mesh (platonic builder, curved shape generator with
  the `m_detail`-derived counts, or `Json_library::make_geometry`), then
  `process()` with that brush's flags and texcoord usage indices - and
  captures its inputs by value or `shared_ptr` (R6). `Json_library` becomes a
  `std::shared_ptr<const Json_library>` owned by `Scene_builder` and the
  generators; it is read-only after construction.
- D2. Johnson solid names. `Json_library` exposes the display name of a key
  (the JSON `"name"` member) without building a mesh, and `make_json_brushes`
  passes it as `Brush_data::name`. `Brush_data::get_name()` keeps its
  fallback to the geometry name for brushes created with a finished geometry
  (floor brush, user-created brushes, imported brushes).
- D3. `Brush` API.
  - `get_geometry()` is the tier 1 entry point (R3): it returns the `ready`
    geometry, preparing or waiting as needed. Its existing callers keep
    calling it, which is what makes them tier 1 without an edit.
  - `request_geometry()` is the tier 2 entry point: `unprepared` becomes
    `queued` and the brush goes to the front of the preparation queue;
    `queued` moves to the front; other states do nothing.
  - `get_geometry_state()` reads the state.
  - `update_facet_statistics()` runs inside preparation, under the brush
    mutex, right after the geometry is stored.
- D4. `Brush_geometry_queue` (new, `src/editor/brushes/`), owned by
  `Scene_builder` next to the palette, constructed with the `tf::Executor&`.
  It holds the pending brushes as `std::weak_ptr<Brush>` in request order
  under its own mutex, and keeps at most `executor.num_workers() - 1`
  preparation tasks in flight, each started through `erhe::task::spawn`. A
  task pops the most recent request, locks the brush, skips it unless it is
  still `queued`, sets `preparing`, releases the lock, runs the generator,
  then stores the result under the lock, sets `ready` or `failed` and
  notifies the condition variable. A finished task starts the next one while
  requests remain. Tier 1 preparation on the calling thread goes through the
  same state transitions, so a brush is prepared once whichever side gets
  there first.
- D5. Thumbnails. The callback `item_tree_window.cpp`, `hotbar.cpp`,
  `inventory_window.cpp` and the geometry graph picker hand to
  `Thumbnails::draw` calls `brush->request_geometry()` and renders the
  preview only when the state is `ready`; otherwise the row draws a spinner
  in the thumbnail's square, keeping the row height. The spinner is a new
  `erhe::imgui` helper (an arc on the window draw list whose start angle is a
  function of `ImGui::GetTime()`), so it animates from the draw that is
  already happening and holds no state. A thumbnail slot is
  allocated when the first preview is rendered, so waiting brushes cost no
  thumbnail slots. The preview of a brush is rendered once it turns `ready`
  because the same row asks again on the next frame it is visible; no
  per-frame poll exists beyond the draw that is already happening.
- D6. `Brush_preview::render_preview` and `Brush::get_scaled` stay tier 1
  internally (they need the GPU primitive); D5 is what keeps them from being
  reached for a brush that is not `ready`.
- D7. `Scene_builder::make_brushes` creates the brushes and returns: no
  taskflow, no wait, no `mesh_memory.flush` for brushes (nothing was
  uploaded). `make_mesh_nodes` reaches `Brush::make_instance` ->
  `late_initialize` -> `get_geometry()`, which prepares the placed brushes on
  the main thread one after another. When a `scene.add_*` command places more
  than a handful (Johnson solids: 92), `make_mesh_nodes` first calls
  `request_geometry()` on all of them and then instantiates them in order, so
  the workers prepare the tail while the main thread prepares the head.
- D8. Lifetime. The queue's tasks hold `std::weak_ptr<Brush>` and the
  generator's own inputs only (R6), so closing the scene that owns the
  palette, or editor exit, needs no cancellation: a task that finds its brush
  gone returns. `Scene_builder` destroys the queue after the palette;
  in-flight tasks finish against brushes kept alive by their own
  `shared_ptr` lock for the duration of the task.

## Phases

Each phase builds, runs and is committed on its own.

1. `Json_library` display-name lookup (D2) and brush creation with names for
   all Johnson solids; geometry still eager. Check: palette names and folder
   order identical to before (`get_scene_brushes` MCP query, before / after
   diff).
2. Geometry state, per-brush mutex and condition variable, `get_geometry()` /
   `request_geometry()` / `get_geometry_state()` on `Brush` (D3, R2, R8, R9),
   still fed eagerly. Unit-testable without the editor: a generator that
   blocks on a test latch, a second thread calling `get_geometry()`.
3. Generators for every palette brush (D1), `make_brushes` without the
   taskflow (D7), tier 1 preparation on the calling thread. At this point
   startup prepares only what `commands.json` places and everything else is
   prepared on first use, on the main thread.
4. `Brush_geometry_queue` (D4, R5, R6) and the `make_mesh_nodes` pre-request
   (D7).
5. The `erhe::imgui` spinner helper; thumbnails: request + spinner (D5);
   `get_scene_brushes` reports the state.
6. Documentation: `doc/editor/brushes.md` states the geometry states and the
   two tiers; `doc/editor/scene.md` states what `make_brushes` does; this
   plan is deleted.

## Verification

- Measurement protocol (same as the 2026-09-21 startup work): ninja Debug
  tree configured with `"-DERHE_TRACY_ON_DEMAND=OFF"`, `tracy-capture -o
  <file> -f` started before the editor, `ERHE_AI_DRIVER=1`, `request_exit`
  over MCP after `Main loop: completed frame 12`; `tracy-csvexport -u` for
  per-zone events. Three runs before, three after, report each run.
  Acceptance: `Scene_builder::make_brushes` under 20 ms; startup to frame 12
  lower by at least 200 ms than the three-run baseline taken at the phase 3
  parent commit; worst of the three runs counts.
- Startup script variants, each started three times: default
  `commands.json`; one with `scene.add_johnson_solids`; one with
  `scene.add_curved_shapes`; one with no `scene.add_*` mesh command. Every
  placed brush renders (in-editor `capture_screenshot`), none aborts.
- Tier 1 right after startup, over MCP: `place_brush` of a Johnson solid and
  of the torus as the first call after frame 12 returns a node whose mesh has
  the expected facet count.
- Tier 2: open the Brushes folders in the item tree through the UI-driving
  tools (`doc/agents/mcp_ui_driving.md`), capture a screenshot within the
  first frames (spinners present), poll `get_scene_brushes` until every
  visible brush is `ready`, capture again (thumbnails present). The frame
  that first shows the folder stays under 50 ms in the Tracy capture.
- Concurrency: 20 startups with `ERHE_DEBUG_VALIDATE_GEOMETRY` set to 1 and a
  script that, right after frame 12, requests every brush (tier 2) and places
  ten of them (tier 1) while the queue is busy; no `MESH CORRUPT` line, exit
  code 0.
- Scene close: `close_scene` while the queue is busy, then grep
  `logs/log.txt` for `scene-close leak`.
- `erhe_geometry_tests`, `erhe_primitive_tests`, `erhe_scene_tests`,
  `ctest -R "Mcp_"`.

## Facts the implementation relies on

- `Geometry::process()` takes no `geogram_lock()` and runs in parallel on
  workers (`doc/erhe/geogram.md`); `Json_library::make_geometry` takes the
  lock around `GEO::mesh_repair` only.
- `Brush::late_initialize()` already defers the GPU primitive, raytrace shape
  and collision shape to the first `make_instance()`; `Brush::get_geometry()`
  already calls `Brush_data::geometry_generator` on first use, with no
  synchronisation and no current user.
- `Scene_builder::make_brush` holds `Content_library::mutex` around the
  `Brush` construction and `set_parent`; with D7 that runs on the main thread
  only.
- The floor brush is built in `Scene_builder::add_room` from the command's
  arguments with a finished geometry and stays that way.
- `ensure_brushes(mass_scale, detail)` is a no-op after the constructor has
  built the palette (`m_brushes_built`); generators read `m_detail` and
  `m_mass_scale` at creation time, by value.
