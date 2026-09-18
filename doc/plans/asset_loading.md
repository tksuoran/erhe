# Asset loading: outstanding work

Status: proposed

Extends [`doc/async_asset_loading.md`](../async_asset_loading.md) and its
design record [`doc/async_asset_loading_design.md`](../async_asset_loading_design.md),
plus [`doc/asset_manager.md`](../asset_manager.md),
[`doc/reloadable_asset_loads.md`](../reloadable_asset_loads.md) and
[`doc/ring_buffer_memory.md`](../ring_buffer_memory.md), which describe the
loading pipeline that exists.

In rough priority order.

## 1. Dependent child tasks

Design record 2.10 states the three rules that hold because
`Asset_manager::get_or_load_container` and `Prefab_library`'s nested
`resolve_external_assets` recursion are synchronous. Turning them into
task-spawning child loads replaces all three at once, and is what section
2.10's shape was written for:

- `Prefab_library::get_or_load` and `Asset_manager::get_or_load_container`
  create a child `Asset_load_task` and return pending with the child handle.
- Phase 1b becomes a suspend point: a task that hits a pending child records
  the dependency, returns `running` and is not advanced again until every
  child handle has settled, then re-runs the whole 1b step (design record 2.9
  says why re-running, not resuming mid-step, is required).
- `Prefab_library::reload`'s `ERHE_VERIFY(m_active_load_stack.empty())` is
  replaced by a dependency-graph query: reload is rejected or queued while a
  task targeting that path is live, and the MCP `reload_prefab` tool reports
  that.
- `m_active_load_stack` cycle detection moves from a call-stack property to a
  property of the dependency graph: a child task whose path is already an
  ancestor in the graph fails the way the recursive check does now.
- Child tasks are shared, so they are refcounted by their dependents. Both
  spawners are content-addressed caches and a child targets a FILE, not a
  scene, so two parents can legitimately depend on one child. Cancel
  propagates to a child only when its last dependent cancels; a cancelled or
  failed child settles its remaining parents as failed instead of leaving them
  suspended forever; and design record 2.3 invariant 4 extends to dependents.
  Without this, closing one scene can cancel a child another scene's load is
  suspended on.

## 2. `get_or_load_container` as a leaf task

Container loads are leaf tasks - they deliberately do not resolve references
transitively - so this needs no dependency graph and can land before item 1.
It is what the pending machinery (`Asset_resolve_state::pending`,
`Asset_manager::acquire_or_pending`, `Asset_reference::needs_resolve`) exists
for; today that machinery is in place but never fires, because `acquire` never
returns pending. Doing it requires the parse-time substitution sites
(`resolve_material_asset_references`,
`acquire_import_materials_as_references`) to suspend rather than fall through
to their "keep the imported copy" path.

## 3. The second parse `materials_as_references` makes

Importing with `materials_as_references = true` parses the same file twice:
once for the import, once inside
`acquire_import_materials_as_references` -> `acquire` ->
`get_or_load_container`, which builds a container record holding its own
`Gltf_data` and drains its textures to the GPU. The record is independent of
the import, so dropping the import does not release it and the memory win for
that flag is roughly halved. Teach the import path to reuse the record's parse
the way the scene-open flow does through `take_adopted_parse`.

## 4. Make `Scene_open` reloadable

`Scene_open_operation` keeps an entire `Scene_root` and its content library
alive across undo by design: `undo()` only unregisters the scene and drops the
browser window, and redo re-registers the same living objects without
re-importing. So undoing "open a large glTF as a scene" frees nothing - the
case that motivates doc/reloadable_asset_loads.md most.

The mechanism carries over unchanged: give it a recipe (the path is already a
member) and let `on_lossless_undo()` drop the `Scene_root`. What makes it a
separate phase is the teardown. Dropping a scene needs the cleanup
`Editor::on_close_scene()` performs - selection, viewport unbinding, tool
re-homing, browser windows, the scene-close leak watchdog - WITHOUT its
`clear_history()`, which is exactly what this must not do. Sketch:

- Factor the reusable half of `Editor::on_close_scene()` out of the
  history-clearing half.
- `Scene_open_operation::on_lossless_undo()` runs the reusable half and
  releases `m_scene_root` / `m_content_library`.
- `execute()` re-opens from the path when those are null, through
  `open_scene_gltf` the way the first execute does.
- Watch `Asset_manager::on_scene_unregistered`: it already announces the
  record's assets, and `take_adopted_parse` is destructive, so a re-open
  re-parses from disk rather than adopting a moved-from parse.

Expect the same identity consequences as the import case - fresh ids, and
`Items_removed_message` clearing cached references.

## 5. Release prefab templates

`Prefab_library` templates are never released, and clones share the template's
primitives, so an import that pulled in prefabs keeps those templates whatever
else is dropped.

## 6. Parallelise the worker `Buffer_mesh` build

`build_imported_buffer_meshes` is deliberately serial: the parse clones a mesh
per instantiating node and the clones share `Primitive` objects, while
`make_buffer_mesh` has no per-shape serialization, so a per-mesh fan-out could
build one shared primitive twice at once. Add that serialization first.

## 7. Slice publish

`max_publish_items_per_frame` exists but is unused; publish is atomic. Only
worth doing if a huge tree measurably overruns the frame.

## 8. Decode backpressure

`max_decoded_bytes_in_flight` is likewise carried but not enforced: the parse
decodes every referenced image before the task reaches residency, so peak
decoded-pixel memory is not yet bounded. It must throttle worker scheduling,
not just buffer results (design record 2.4).

## 9. The remaining synchronous MCP tools

`reload_prefab` and `load_asset_file` still load synchronously. Each either
waits on a handle or returns a handle id plus a documented poll, as
`instantiate_prefab` does.

## 10. Ring buffer follow-ups

From `doc/ring_buffer_memory.md`:

- Dedicated one-shot staging for very large uploads (image mip chains, mesh
  vertex / index pool block init): a plain host-visible `Buffer`, the copy
  recorded, freed through `add_completion_handler` - transient, never joining
  the permanent ring pool. A request larger than the whole budget must take
  this path rather than forcing a giant ring buffer.
- A device-level ring-buffer budget (`m_ring_buffer_total_bytes` plus a config
  knob), counting the BAR heap toward it, with an allocator backstop that
  reports "would exceed budget" so a loading thread can flush and wait -
  except on the render thread mid-frame, where waiting on its own frame would
  deadlock, so it allocates anyway and warns.
- Idle reclaim on the GL and Metal backends; only the Vulkan backend erases
  idle ring buffers today.

## 11. Re-test the presentation stall

The hypothesis that motivated the asynchronous pipeline is that the macOS
presentation stall is triggered by a load blocking tick 1 with no run-loop
servicing; that blocking is gone for every converted path. Caution: the
VirtualCity repro previously wedged the machine badly enough to need a hard
reboot.
