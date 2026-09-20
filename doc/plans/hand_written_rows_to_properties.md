# Remaining hand-written item state as properties

Status: in progress

Extends `doc/erhe/property_system.md` (the design record of `erhe::property`),
whose section 6 links here. `doc/erhe/property_inventory.md` owns the per-field
status and its "Not yet migrated" table is the work list of this plan; each
phase below removes its row from that table in the commit that lands it.

Decision labels of this plan are `H4`..; `D<n>`, `R<n>` and section numbers
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

Landed; `doc/erhe/property_system.md` section 4.20 states what the scene's
ambient color and its two file forms now are.

### Phase 2: Layout grid track extents

Landed; `doc/erhe/property_system.md` section 4.13 states what the three
per-axis extent lists now are and how the array row draws them.

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
