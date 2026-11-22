# Editor coding rules

Stability: stable

Rules specific to code under `src/editor/`, on top of the project-wide rules
in `AGENTS.md`. The editor's structure is described in `doc/editor/editor.md`;
the prioritized architecture backlog is `doc/plans/editor_improvements.md`.

## Part construction and `App_context`

The editor is composed of "parts" (tools, windows, renderers, scene managers,
etc.). `App_context` holds pointers to all parts and shared resources
(`graphics_device`, `imgui_renderer`, `rendergraph`, `mesh_memory`,
`scene_builder`, `headset_view`, `editor_settings`, ...).

**A part constructor accesses `App_context` only to store the
`App_context&` itself into a member variable.** The `App_context` pointers
are assigned *after all parts have been constructed*, so reading e.g.
`context.graphics_device` from inside a part constructor yields `nullptr` (or
a stale value) and crashes. (A few fields, such as `current_command_buffer`,
are populated earlier in init; do not rely on this.)

Each part constructor receives the other parts and resources it needs as
explicit `Part&` / resource-reference constructor arguments and uses those
during construction. `App_context` may be used freely *after* construction
(in per-frame / runtime methods). Helper objects created by a part (e.g.
`Quad_view`) follow the same rule: pass them the needed references explicitly.

## Logging

Editor code logs through the `log_*` spdlog categories declared in
`src/editor/editor_log.hpp` (e.g. `log_startup->info("...", ...)`); pick the
closest existing category or add a new one in `editor_log.{hpp,cpp}` (declare
`extern`, create it in `initialize_logging()` via
`make_logger("editor.<name>")`). Per-category levels live in
`config/editor/logging.json`.

## Editor item flags and types

`erhe::Item_flags` and `erhe::Item_type` hold only what the `erhe::*`
libraries read (`doc/erhe/item.md` "Application bits"). Every editor-only
flag bit and class type index is in `src/editor/editor_item_bits.hpp`:
`Editor_item_flags` (`tool`, `brush`, the hover bits, `active_item`, ...),
`Editor_item_types` (`brush`, `joint`, `style`, ...) and
`Editor_item_properties` (the flag-bridged "Locks" and developer-visibility
properties). `register_editor_item_bits()` registers their labels,
persistent names and masks; `run_editor()` calls it before any item exists,
and an editor test executable that exercises purpose derivation or flag
serialization calls it from its `main()`.

- A new editor flag is the next `application_bit(i)` of `Editor_item_flags`
  plus its `Item_flag_info` row in `editor_item_bits.cpp` (label; the
  persistent name, or `nullptr` for transient state); a transient bit joins
  `Editor_item_flags::transient`. A new editor class takes the next
  `application_index(i)` of `Editor_item_types` and its class-name row.
- Library code never names an editor bit: it takes a mask
  (`Gltf_export_arguments::excluded_item_flag_bits`,
  `Primitive_interface_settings::hovered_flag_bits`) or tests a capability
  bit the editor class ORs into its static type (`Item_type::texture_reference`,
  `style_source`).

## Scene-hosted references in editor parts

Scenes can be closed at runtime (Hierarchy "Close" context menu, MCP
`close_scene`). Any editor part (window, tool, cache, singleton) that stores a
reference across frames to scene-hosted content - items of a scene's content
library (materials, brushes, `Graph_mesh` / `Graph_texture` assets), scene
nodes, meshes, cameras, or the `Scene_root` itself - must handle that scene
closing, or it keeps showing / editing / simulating content of a dead scene
(a recurring bug class).

- A `weak_ptr` alone is NOT sufficient: any cached resolved `shared_ptr`
  (including the part's own resolve cache) keeps the item alive, so expiry
  never signals the close.
- Either subscribe to `App_message_bus::close_scene` and drop the references
  (precedent: `Editor::on_close_scene()` clearing graph editor targets), or
  validate on access that the item's host is still a registered scene:
  `App_scenes::is_host_registered(item->get_item_host())` (precedent:
  `Geometry_graph_window::resolve_target()`).
- `Editor::on_close_scene()` arms a **scene-close leak watchdog**: 60 frames
  after a close it logs a `scene-close leak: ...` warning for every tracked
  item of the closed scene still alive (and an all-released info line when
  clean). Treat these warnings as bugs of this class; when touching close /
  teardown paths, close a scene and grep `logs/log.txt` for
  `scene-close leak` as part of verification.

A scene closing is only half of it: content also leaves the editor **without**
any scene closing, when an undo removes it - undoing a glTF import takes every
imported asset back out of the content library and every imported node back
out of the scene. So a cached reference must handle BOTH.

- Subscribe to `App_message_bus::items_removed` and drop the reference when
  `message.removed->lookup.contains(ptr)` names it (precedent:
  `Animation_window::on_items_removed()`). Handlers do that set lookup ONLY -
  no manager lookups, no linear scans - because undoing a large import
  announces thousands of items in one message.
- The message is published once per frame by
  `Asset_manager::flush_pending_removals()`, from `Editor::frame_prepare_phase()` just before
  the message bus pump. The producers are the content-library detach walk
  (`release_host_for_subtree`), `Item_insert_remove_operation`, and
  `Asset_manager::on_scene_unregistered` - so no new removal path needs its
  own integration.
- Re-resolving state needs the same care as a cached `shared_ptr`: an item
  removed by an undo stays alive in the undo history for redo, so a weak
  "last selected" entry is still lockable and a part that re-resolves it
  every frame will resurrect the reference (`Selection::on_items_removed()`
  forgets those entries for exactly this reason).
- A part that re-resolves a weak reference (`get_last_selected`) into a
  strong member of its own every frame pins the item itself: the member is
  what keeps the weak reference lockable, so it never expires. Keep the
  resolved `shared_ptr` local to the use.
- A persistent scratch that holds item `shared_ptr`s is cleared after each
  use as well as at the point of use (capacity kept); cleared only at the
  next use, its contents keep the items alive between uses and a closed
  scene leaks.
- Verify with `scripts/undo_reference_clearing_smoke_test.py` (drives a
  running editor over MCP) and the `get_editor_references` MCP query, which
  reports every such cached reference. See
  `doc/editor/import_undo_reference_clearing.md`.

## Scenes and tasks

Two mutexes guard scenes against worker tasks: `App_scenes::m_mutex` guards
the set of open scenes, and each `Scene_root`'s `item_host_mutex` guards that
scene's hosted state (nodes, meshes, primitives, attachments, content
library, raytrace scene, physics world, draw-list scene) and its closed
state (`Scene_root::is_closed()`).

- Lock order: a thread takes a scene's `item_host_mutex` first and may then
  take `App_scenes::m_mutex` briefly inside it (`is_host_registered()`,
  `copy_scene_roots()`). Code that holds `App_scenes::m_mutex` calls nothing
  that locks a scene.
- The set is written on the main thread only (`register_scene_root()`,
  `unregister_scene_root()`), so the main thread reads it through
  `App_scenes::get_scene_roots()`, which checks the thread. Any other thread
  reads it through `App_scenes::copy_scene_roots()` (clear-and-fill, under
  the set mutex).
- A task that works in a scene holds the scene by shared ownership
  (`async_for_nodes_with_mesh()` keeps the `Scene_root` in its task), and
  each locked section of its work is a `Scene_task_lock`
  (`scene/scene_root.hpp`): it locks the scene's `item_host_mutex` and reads
  the closed state under it. An open scene: the section does its work under
  the lock. A closed scene: the task returns without touching it; an
  operation it builds refuses (`Operation::set_refused()`), so nothing is
  recorded.
- The closed state, not registration, is what a task reads: an undone scene
  open unregisters the scene and a redo registers the same `Scene_root`
  again, so work for an unregistered scene still lands in it. A close is
  final: `Editor::on_close_scene()` is the one place that sets the state, a
  close is not undoable, and `register_to_editor_scenes()` verifies that the
  scene is not closed.
- An operation a task queues (`Operation_stack::queue_from_thread()`) applies
  on the main thread later; `Mesh_operation::execute()` takes a
  `Scene_task_lock` and refuses when the scene closed in between.
- A task holds the lock for the part of its work that reads or writes the
  scene; work on captured input (primitive builds, BVH builds) runs outside
  it (`deferred_finalize_mesh_items()`, the display-color rebuild).
- `Editor::on_close_scene()` first removes the scene from the set, then
  takes the scene's `item_host_mutex`, sets the closed state and detaches the
  PVS consumer: a task inside a locked section finishes it first, and every
  later section finds the scene closed. Each mutex is taken once on that
  path (neither is recursive): code that locks the scene itself (the
  lightmap partitioner's job cancel, the selection change messages) runs
  after that section. `reset_editor_state` closes every scene through the
  same handler.

## ImGui-time work

A window's `imgui()`, a menu entry and a host begin callback run inside the
ImGui iteration of the main thread's prepare phase, and under the render
thread plan they will run while the render phase renders the same frame
(`doc/editor/render_thread.md` section 2.6). Code there reads and writes ImGui
state and the live editor state the windows own; it does not create windows,
viewports or rendergraph nodes, record GPU work, run a blocking load, or
mutate an object the render phase reads (a rendergraph node's material,
size, enabled flag or connections, a renderer's selection). Such work is
deferred in one of two ways:

- `App_context::imgui_windows->queue([...] { ... })`: a one-shot operation
  the editor runs at the start of the next frame's prepare phase
  (`Imgui_windows::flush_queue`, `doc/editor/editor.md` "Frame phases and the
  render thread"), with the upload command buffer open. Menu commands, the
  Operations window's buttons and parameterized invokers, the Item tree's
  context menu operations, the asset browser's inline imports, the Create
  window's rendertarget button and the indirect diffuse source / merge mode
  combos use it.
- A request the owner serves in its own prepare phase step: the Properties
  material preview (`Material_preview::request_preview` -> `update`), the
  BRDF slice (`Brdf_slice::show_brdf_slice` -> `update`), the Depth
  Visualization window (`set_shadow_renderer_node`, the light slot, the size
  and enabled -> `update`), the Lightmap window's buttons (request flags ->
  `update`) and undo / redo (`Operation_stack::queue_undo` / `queue_redo`,
  served by `Operation_stack::update` before the queued operations).

The ImGui-time draw of a texture passes it as a `Texture_reference`, which
the ImGui host resolves in the render phase; after a resize the window shows
the previous render for one frame.

## Config JSON formatting

Applies to JSON files consumed by `erhe_codegen`-generated loaders (the
hand-authored files under `src/editor/config/` that pair with codegen-produced
structs) and to `src/editor/config/logging.json`.

- Write each `"key": value` pair on a single line.
- Omit the `"_version"` key when its value would be `1` (a missing `_version`
  is treated as version `1`). Include `"_version"` only when the object has
  been migrated to version `2` or later.
- Use 4-space indentation, consistent with existing files.
