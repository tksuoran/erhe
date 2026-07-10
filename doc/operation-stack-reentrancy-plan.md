# Operation_stack re-entrancy fix -- plan

Status: IMPLEMENTED 2026-07-10 (commits A/B/C as laid out under
"Implementation order"; commit C's MCP tool is `open_scene`, and D3 was
implemented as `make_import_gltf_operation()` returning the described
compound rather than an out-parameter -- same composability, less surface).
Written 2026-07-10 from a fresh-context audit of all Operation_stack call
sites.

## The bug

Asset Browser -> right-click a .gltf/.glb -> "Open '<path>'" crashes with
`std::system_error` (`resource_deadlock_would_occur`) from `std::mutex::lock`:

    Operation_stack::update()            locks m_mutex, executes queued ops
      Scene_open_operation::execute()      (asset_browser.cpp:55)
        import_gltf()                        (parsers/gltf.cpp)
          Operation_stack::queue()           locks m_mutex again, same thread -> throw

`Operation_stack` (operation_stack.{hpp,cpp}) runs operations *while holding*
its non-recursive `m_mutex` in `update()`, `execute_now()`, `undo()`, `redo()`.
Any `Operation::execute()`/`undo()` that calls back into the stack deadlocks.
`Scene_open_operation` is the first observed case; the hazard is structural.

## Audit: threading reality (question 1)

Every Operation_stack entry point is called from the main thread only:

- `queue()` (~100 sites): ImGui windows/tools/context menus (all run inside
  `Imgui_windows::draw_imgui_windows()` in `Editor::tick`), command handlers
  (input dispatch, main thread), MCP handlers (`Mcp_server::
  process_queued_requests()` runs on the main thread once per frame --
  mcp_server.cpp:334 comment), startup-script command handlers
  (`run_startup_script()`, main thread, editor.cpp:2148), and message-bus
  subscription callbacks (pumped by `m_app_message_bus->update()` in tick).
  The scene_root.cpp sites are ImGui item/context-menu callbacks; the
  scene_builder.cpp sites run via `scene.add_*` startup commands. All main
  thread.
- `execute_now()`: geometry/texture graph windows and graph-node parameter
  commits (ImGui), MCP animation/graph handlers. Main thread.
- `undo()`/`redo()`: `Undo_command`/`Redo_command` (key/menu bindings) and MCP
  tools. Main thread.
- `update()`: `Editor::tick` (editor.cpp:449) and the init-time drain
  (editor.cpp:2165). Main thread.
- `clear_history()`: scene-close handling via the message-bus pump. Main thread.

Worker threads: the only live `tf::Executor` async job today is the geometry
graph background evaluation (geometry_graph_window.cpp:516); it evaluates a
shadow graph and sets a done flag -- results are applied on the main thread in
`finish_evaluation()`. It never touches Operation_stack. The historical
worker-thread operation execution paths (`executor.silent_async` in
operations_window.cpp:1449/1860/1892, transform_tool.cpp:154) -- the likely
original reason for the mutex -- are all commented out.

Note also that the mutex never provided real thread safety: `can_undo()`,
`can_redo()`, `get_undo_stack()`, `get_redo_stack()`, and `imgui()` already
read the containers without the lock. The class is de-facto main-thread-only;
the mutex is vestigial and is now the direct cause of a crash.

One construction-order wrinkle: Operation_stack is *constructed* inside an
init taskflow task (editor.cpp:1505), i.e. possibly on a worker thread, so an
owner-thread id must not be captured in the constructor. No stack method is
called during that phase; first use is the startup script (main thread).

## Design decision D1 (root cause): main-thread contract, no mutex

Replace "lock around everything" with an explicit, asserted single-thread
ownership contract:

1. Add `std::thread::id main_thread_id` to `App_context`, assigned at the top
   of editor init on the main thread (before parts construction; plain field,
   same family as `executor` / `current_command_buffer`). Precedent for
   owner-thread verification: `gl_gpu_timer.cpp`, `gl_render_pass.cpp`,
   `gl_context_provider.cpp`.
2. Delete `m_mutex` from Operation_stack. In every public entry point
   (`queue`, `execute_now`, `undo`, `redo`, `update`, `clear_history`,
   `can_undo`, `can_redo`), `ERHE_VERIFY(std::this_thread::get_id() ==
   m_context.main_thread_id)`. Cost is a thread-id compare on rare calls;
   keep it on in all build configs so any future off-thread caller fails
   loudly instead of racing silently (No Band-Aid: the invariant becomes
   visible instead of being masked by a lock that never protected the reads).
3. Delete the stale `// TODO ?` lock comments in `can_undo()`/`can_redo()`
   (question 3): with the asserted contract the unlocked reads are correct by
   construction. Same for `imgui()` and the `Mcp_server` friend accessors.
4. Delete the commented-out `silent_async` operation-execution blocks
   (operations_window.cpp, transform_tool.cpp). If off-main-thread operation
   execution ever returns, it must be designed as "compute on worker, apply
   through the main-thread stack" (like the geometry graph evaluation), not
   by re-adding a lock around execute().

## Design decision D2: re-entrancy semantics (question 2)

Contract: **during operation execution, the only legal Operation_stack call
is `queue()`**. Nested `execute_now()`, `undo()`, `redo()` are programming
errors and assert.

- Add `bool m_executing{false}` (main-thread only, no atomics). Set around
  the `operation->execute(...)` / `operation->undo(...)` calls in `update()`,
  `execute_now()`, `undo()`, `redo()`.
- `queue()`: always legal; just appends to `m_queued`.
- `execute_now()`, `undo()`, `redo()`, `clear_history()`:
  `ERHE_VERIFY(!m_executing)`.

`update()` drains with an index loop so `m_queued` may legally grow while
draining -- nested queued operations execute in the **same** update pass, in
append order (no one-frame latency, unlike the reverted batch-swap patch):

    void Operation_stack::update()
    {
        // ERHE_VERIFY(main thread); ERHE_VERIFY(!m_executing)
        if (m_queued.empty()) return;
        for (std::size_t i = 0; i < m_queued.size(); ++i) {
            std::shared_ptr<Operation> operation = m_queued[i]; // copy: push_back may reallocate
            m_executing = true;
            operation->execute(m_context);
            m_executing = false;
            m_executed.push_back(std::move(operation));
        }
        m_queued.clear();  // keeps capacity (allocation discipline)
        m_undone.clear();
    }

Properties:

- Same-frame semantics: `Scene_open_operation` executes, its queued import
  compound executes immediately after it within the same `update()` --
  resolves the "one frame later" ordering/latency concern from the reverted
  fix. The init-time drain at editor.cpp:2165 also still sees the fully
  drained scene before prewarm.
- No windows where `m_executed`/`m_undone` are observable in a half-updated
  state across lock scopes -- there are no lock scopes; everything is a plain
  single-threaded function again.
- Interleaving with `execute_now()`: unchanged -- `execute_now()` still runs
  immediately at its call site (MCP/graph edits need same-call observability)
  and is simply illegal while another operation is executing (asserted).
- Exception from `execute()` propagates as today (crash handler); no worse.
- A pathological operation that re-queues itself forever becomes an unbounded
  loop inside one update(); today it would deadlock instead. Optionally
  ERHE_VERIFY a generous cap (e.g. executed-per-update < 100000) to turn that
  bug into a loud abort; not load-bearing.

## Design decision D3: Scene_open undo granularity (questions 2 + 4)

With D2 alone, "Open" produces two undo entries: [Scene_open_operation,
"Import glTF ..." compound]. Undo order is coherent (import content removed
first, then scene unregistered) but one user action should be one Ctrl+Z.

Fix by making import composable instead of self-queueing:

- `import_gltf()` gains an out-parameter variant: append the built operations
  to a caller-provided `std::vector<std::shared_ptr<Operation>>&` instead of
  queueing the compound (the existing entry point keeps queueing for its
  other callers: asset-browser "Import" into the current scene, prefab
  drag-drop -- those are single-compound undo units already and stay as-is).
- `Scene_open_operation::execute()` (first_time path) builds the compound and
  executes it inline via `compound->execute(context)` as part of its own
  execution. Its `undo()` stays unregister-only: the imported content lives in
  `m_scene_root`, which the operation keeps alive, so redo re-registers the
  scene with content intact and does not re-import. The compound is one-shot
  and not stored.
- Result: "Open scene" is exactly one entry on the executed stack, and the
  Scene_open path no longer queues from inside execute() at all. D2 still
  guards the structural hazard for any future nested-queue caller.

D3 is separable from D1+D2 and should be its own commit. The alternative
"Scene_open stops being an Operation" is rejected: undoable open (unregister
scene + remove browser window, redo restores both) is deliberate #240/#241
behavior and works; there is no need to redesign it.

## Implementation order

1. Commit A (root cause): D1 + D2. Small, mechanical, fixes the crash for
   every nested-queue caller.
2. Commit B (UX): D3, import_gltf out-parameter + Scene_open composition.
3. Optional commit C (verification infra): MCP tool `open_scene(path)` that
   constructs + queues `Scene_open_operation` (requires exporting the class
   from asset_browser.cpp or moving it to its own file). Makes this whole
   area headless-verifiable; CLAUDE.md policy is to grow the MCP surface for
   debugging needs.

## Verification

- Build: ninja win vulkan + headless VS tree.
- Headless (with commit C, or approximated without): drive `open_scene` on a
  test .glb; before the fix this deadlock-crashes, after it succeeds;
  `get_undo_redo_stack` shows one "Scene_open_operation" entry (D3);
  undo -> scene gone, redo -> scene back (screenshots).
- Windowed (user at the controls): Asset Browser -> right-click glTF ->
  "Open"; then Ctrl+Z / Ctrl+Y.
- Regression: normal editing loop (create_shape via MCP, undo/redo), startup
  script drain at init, scene close (`clear_history`).

## Open points for review

1. `ERHE_VERIFY` main-thread checks kept in release builds -- agreed? (Cost
   is negligible; failure mode without them is a silent data race.)
2. Same-pass drain (D2) means an operation queued during execution runs in
   the same frame. This is intended; flag if any caller depends on
   next-frame deferral (none found in the audit).
3. Delete the commented-out `silent_async` blocks (D1.4) -- they document an
   old design; the git history preserves them.
4. Commit C (MCP `open_scene`) -- do it now or defer?
