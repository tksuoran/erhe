# Remaining hand-written item state as properties

Status: proposed

Extends `doc/erhe/property_system.md` (the design record of `erhe::property`),
whose section 6 links here. `doc/erhe/property_inventory.md` owns the per-field
status and its "Not yet migrated" table is the work list of this plan; each
phase below removes its row from that table in the commit that lands it.

Decision labels of this plan are `H1`..; `D<n>`, `R<n>` and section numbers
without a document name refer to the design record.

## 1. Goal

Every authored value an item holds is a registered property, so the generic
rows of the Properties window draw it, `Property_set_operation` records its
edits, MCP `get_item_properties` / `set_item_property` reach it, a Style can
hold it (D30) and a multi-selection shows it with mixed-value handling
(`doc/editor/properties_window.md`). The state in scope today is edited by
writing a member directly from an ImGui widget: it has no undo entry, no MCP
property access and no style.

## 2. Scope

The closed list of state this plan migrates, in phase order:

| Phase | Owner | State | Property form |
|---|---|---|---|
| 1 | `erhe::scene::Scene` | `ambient_light` | `vec3`, entry store |
| 2 | `erhe::scene::Layout` | grid track extents, one list per axis | three `float_array`, entry store |
| 3 | `erhe::physics::Collision_filter` | `collision_systems`, `collide_with_systems`, `not_collide_with_systems` | three `string_array`, entry store (new value type) |
| 4 | `erhe::physics::Physics_joint_settings` | `limits`, `drives` | child items with scalar properties (H6) |

The scene's settings-override block (`Scene_settings`, the codegen optionals
drawn by `Properties::scene_properties`) is config state, and its route to
properties is the `Graphics_settings`-as-item work in
`doc/plans/property_system.md`, "Style users beyond the content library's
style items". The variant combos are switches of the variant table
(`doc/erhe/usd_compatibility_design.md` X4), recorded by their own compound
operation.

## 3. Decisions

- H1 `Scene.ambient_light` is a `vec3` with the `color` presentation, default
  `(0, 0, 0)`, `inherits = false` (a scene has no holder above it; a Style
  holds the value for a shared lighting preset, D30). Every reader takes
  `glm::vec3` already (`Light_buffer::update`, the composition pass, the DDGI
  sky radiance, the ray trace renderer), so the fourth component carries
  nothing.
- H2 `Scene` keeps a private `glm::vec3 m_ambient_light` as a MIRROR of the
  effective value, refreshed in `Scene::on_property_changed`, read through
  `get_ambient_light()` - the bridged-owner recipe of section 4.18. The
  per-frame readers read the member and never the store. `set_ambient_light()`
  is the one writer entry point and writes the local value.
- H3 The file forms keep their field: `ERHE_scene.ambient_light` and the USD
  `erhe:scene` block write the effective color as four numbers with the fourth
  `0`, and read the first three. `ERHE_scene` gains `properties` (the scene
  item's complete local set, the `ERHE_light` rule of section 4.18) and
  `style`, so a style-held ambient color stays style-held across a reload; on
  load a file without `properties` makes `ambient_light` a local value.
  `doc/gltf_extensions/ERHE_scene.md` and its schema change in the same commit.
- H4 A list of scalars is one array property: the whole list is the value, an
  edit of one element is a set of the whole list, and the generic row gains a
  per-element editor for the array types (today it shows a read-only summary,
  `dependency_property_rows.cpp`). Element count rules that depend on another
  property (the track count of a layout axis) are a `coerce` callback (D7) on
  the array property, so the list is resized where the value is produced and
  the draw code mutates nothing.
- H5 `Property_type::string_array` (`std::vector<std::string>`) joins
  `float_array` and `int_array`, with the same standing: not an expression
  target, D16 text form a quoted, space-separated list, USD type `string[]`.
- H6 A list of RECORDS (`Physics_joint_settings::limits`, `::drives`) becomes
  child items of the settings item, one `Joint_limit` / `Joint_drive` item per
  element, each with scalar properties (`linear_axes` and `angular_axes` as
  `ivec3` masks, `min`, `max`, `stiffness`, `damping`; drive `type`, `mode`,
  `axis`, `max_force`, `position_target`, `velocity_target`, `stiffness`,
  `damping`; an unset optional is source `default`, the section 4.18 rule).
  Add and remove are `Item_insert_remove_operation`, order is child order, and
  the resource tree already shows children (U4). This phase is designed in its
  own requirements pass before any code: the KHR physics import/export and the
  USD joint writer read the vectors today, and whether the vectors stay as a
  mirror rebuilt from the children is the question that pass answers.

## 4. Phases

Each phase is one commit series through `doc/agents/orchestration_harness.md`,
verified by the section 4.18 verification loop plus the checks named here.

### Phase 1: Scene.ambient_light

1. `scene.{hpp,cpp}`: register `Scene::ambient_light_property` (H1), add the
   mirror, `get_ambient_light()`, `set_ambient_light()` and the
   `on_property_changed` override (H2); the public `ambient_light` member goes
   away. The copy constructor and `operator=` copy the local value through the
   store.
2. Readers move to `get_ambient_light()`: `composition_pass.cpp` (2 sites),
   `ddgi_renderer.cpp`, `ray_trace_renderer.cpp`, `example.cpp`,
   `mcp_server_scene_query.cpp`, the two exporters.
3. Writers move to `set_ambient_light()`: `brush_preview.cpp`, `gltf.cpp`,
   `usd.cpp` (scene state, then the DomeLight product, which stays a local
   value), and MCP `set_scene_settings.ambient_light`, which records a
   `Property_set_operation` so it becomes undoable like `set_item_property`.
4. `scene_builder.cpp` records `Property_set_operation` in its compound;
   `operations/ambient_light_operation.{hpp,cpp}` is deleted (CMakeLists, then
   `scripts\configure_ninja_win_clang.bat`).
5. `Properties::scene_properties` loses its "Ambient Light" row; the generic
   section draws it.
6. H3 file forms; extend the `ERHE_scene` section of
   `scripts/scene_roundtrip_verify.py` with a local and a style-held ambient
   color.
7. Test: `src/erhe/scene/test/test_scene_properties.cpp` (default, setter to
   mirror, untyped access, style-held value reaches the mirror, clone).
8. Docs: a "Scene" subsection in section 4 of the design record, the inventory
   row moves to the migrated tables, R5 of
   `doc/editor/properties_window.md` names the settings-override block only.

Headless check beyond the loop: `set_item_property` of `ambient_light` on the
scene item, `capture_screenshot` shows the ambient term, `undo` restores it,
`get_undo_redo_stack` shows one entry.

### Phase 2: Layout grid track extents

1. The array row editor of H4 in `dependency_property_rows.cpp`: one drag
   field per element for `float_array` / `int_array`, mixed-value per element,
   one `Property_set_operation` per completed edit.
2. `Layout::grid_track_extent_x/y/z_property` (`float_array`, default empty =
   uniform tracks, `visible_when` layout type is grid), `coerce` sizes a
   non-empty list to the axis track count, `m_grid_track_extent` becomes the
   mirror. A "Custom" toggle is a `Property_row_action` that seeds the list
   from the volume (the action the hand-written row performs today).
3. `Properties::layout_properties` is deleted; `ERHE_layout` keeps its
   `grid_track_extent_x/y/z` fields, written from the mirror, and its
   `properties` map decides whether the value stays local (section 4.13).
4. `test_layout_properties.cpp` gains the coerce and mirror cases.

### Phase 3: Collision_filter system lists

1. H5 in `erhe::property` (value type, D16 text, tests), the USD writer's
   `string[]` spelling, and a string element editor in the H4 row (add,
   remove, edit on deactivate).
2. The three lists become entry-store properties of `Collision_filter`; the
   members become mirrors; `on_property_changed` calls what
   `reapply_collision_filter` calls today, so the consequence follows every
   source of a change.
3. `Properties::collision_filter_properties` is deleted; the KHR collision
   filter export reads the mirrors unchanged.

### Phase 4: Physics_joint_settings limits and drives

The H6 requirements pass produces a plan document of its own under
`doc/plans/`; this plan's phase ends when that document exists and the
inventory row points at it.

## 5. Verification

Per phase: the section 4.18 loop, the owner's gtest suite,
`scripts/scene_roundtrip_verify.py` at its baseline, a scene close with
`all N released` in `logs/log.txt`, and `py -3 scripts/check_doc_links.py`.
The user's interactive check per phase: edit the value in the Properties
window, Ctrl+Z restores it, a Style holding the value drives an item that has
no local value.
