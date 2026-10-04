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
  `Asset_manager::flush_pending_removals()`, from `Editor::tick` just before
  the message bus pump. The producers are the content-library detach walk
  (`release_host_for_subtree`), `Item_insert_remove_operation`, and
  `Asset_manager::on_scene_unregistered` - so no new removal path needs its
  own integration.
- Re-resolving state needs the same care as a cached `shared_ptr`: an item
  removed by an undo stays alive in the undo history for redo, so a weak
  "last selected" entry is still lockable and a part that re-resolves it
  every frame will resurrect the reference (`Selection::on_items_removed()`
  forgets those entries for exactly this reason).
- Verify with `scripts/undo_reference_clearing_smoke_test.py` (drives a
  running editor over MCP) and the `get_editor_references` MCP query, which
  reports every such cached reference. See
  `doc/editor/import_undo_reference_clearing.md`.

## Config JSON formatting

Applies to JSON files consumed by `erhe_codegen`-generated loaders (the
hand-authored files under `src/editor/config/` that pair with codegen-produced
structs) and to `src/editor/config/logging.json`.

- Write each `"key": value` pair on a single line.
- Omit the `"_version"` key when its value would be `1` (a missing `_version`
  is treated as version `1`). Include `"_version"` only when the object has
  been migrated to version `2` or later.
- Use 4-space indentation, consistent with existing files.
