# Data-model undo and reflective MCP item edits: remaining work

Status: in progress

Audit items 13 and 14 of `doc/reference/audit_erhe_2026_09_30.md` section 8.
Extends `doc/editor/operations.md` "Property_edit_operation" (undoable
property edits, `Scene_settings_set_operation`), `doc/editor/coding_rules.md`
"Document change notifications" (which mechanism owns which change) and
`doc/agents/mcp_api_guidelines.md` "Document edits are operations". The
rest of the landed design is in `doc/erhe/property.md` "Write recording",
`doc/erhe/property_system.md` (D18 bridged clear, D31 per-object default)
and `doc/agents/mcp_server_usage.md` (`get_property_schema`,
`set_item_properties` / `set_item_property`, `set_collision_shape`,
`rebuild_joint`, `set_scene_settings`).

## 1. Scene settings written outside the operation

The Properties window's scene override rows (`windows/properties.cpp`,
the `override_struct` lambda: sky, grid, physics, shadow frustum fit,
camera controls) and `Fly_camera_tool::get_writable_camera_controls()`
(`tools/fly_camera_tool.cpp`) write the scene's `Scene_settings` directly.
Those edits are not undoable, and undoing an earlier `set_scene_settings`
assigns the operation's before struct, which reverts them. Both go through
`Scene_settings_set_operation` (built at the edit's commit, e.g.
`ImGui::IsItemDeactivatedAfterEdit()`, so a slider drag is one entry), and
`Mcp_test.document_edits_record_one_undo_entry_each` style coverage is added
for the window path where a test can reach it.

## 2. Notification gaps

Open items of the notification design (`doc/editor/coding_rules.md`
"Document change notifications"):

- A committed mesh property edit that changes light transport (`visible`,
  `shadow_cast`, `double_sided`, a material assignment) does not queue
  `Scene_lighting_changed_message`: `App_context::on_item_property_changed()`
  announces lighting for `Material` and `Light` items and for transform
  writes only. The indirect diffuse producers keep their history after such
  an edit. The hook announces it for the properties that change what probe
  rays see, decided by a property flag rather than by item type.
- The draw-list partition rebuild (`Draw_list_scene::rebuild_all()` for
  `affects_draw_list_partition` / `affects_shader_variant` properties) runs
  only from `on_item_property_changed()`, so a write made outside a
  property operation leaves the lists stale. The rebuild belongs to a
  consumer that observes the write itself (a property observer or the
  `Scene_host` path), so every write path reaches it.
- `erhe::scene_renderer::Material_set::update()` compares each slot's
  recorded material change serial every frame (`material_set.cpp`), the
  "changed?" poll `AGENTS.md` names. The change-driven form is an
  `Observer_token` per slot that marks the slot dirty on a material
  property change.
- A transform written through a property operation (`set_item_properties`
  on translation / rotation / scale, its undo and redo) is announced as a
  committed node transform but does not teleport the node's physics body,
  as `Node_transform_operation` does (`Node_physics_system::teleport_to_node`).

## 3. Property_edit_operation: seal written twice in one edit

An edit that unseals an item, writes it and re-seals it within one edit
records a seal whose before and after states both seal, so no placement
of the seal record restores the writes between them: rollback, undo and
redo log the refused writes as errors (`doc/editor/operations.md`
"Property_edit_operation"). Restoring such an edit needs the seal applied
around each restore (lift, restore the records, re-apply the recorded
state). No in-tree edit does this today; the item is to decide whether to
support it or to refuse such an edit at its first execute.

## 4. Verification gaps

- `Mcp_test.select_variant_undo_redo_round_trips_every_prims_property_dump`
  skips unless the editor is built with `ERHE_USD_LIBRARY=lightusd`; it has
  not run yet. Run it on a lightusd headless tree.
- The long and GPU-heavy scripts whose MCP calls moved from the per-type
  edit tools to `set_item_properties` / `set_collision_shape` /
  `set_scene_settings` have only been checked with `py_compile`:
  `scripts/scene_roundtrip_verify.py`, `scripts/shadow_verify.py`,
  `scripts/mesh_ab_capture.py` and the creations 15, 17, 18, 24 and 25
  under `scripts/creations/`. Each needs one full run against the headless
  editor.
- Material values: the removed `edit_material` clamped its inputs. A
  property write refuses `metallic`, `opacity`, `transmission`,
  `occlusion_texture_strength` and `alpha_cutoff` outside [0, 1] and takes
  `base_color`, `roughness`, `reflectance`, `emissive` and `ior` as given.
  The script runs above confirm no caller relied on the clamp.

## 5. Later

- A scripting binding (Lua or Python) binds the same verb
  (`set_item_properties`) and schema (`get_property_schema`); not started.
