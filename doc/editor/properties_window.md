# Properties window: one path for every row

Stability: stable

The Properties window (`src/editor/windows/properties.cpp`) draws every
authored row through the registered-property path, so a selection of any size
and any mix of types gets the same treatment: per-type sections, mixed-value
display, one compound undo entry per edit, Copy / Paste Properties and Add
Property. The hand-written per-class functions draw read-only diagnostics,
actions and the list editors that have no property form.

The design record is `doc/erhe/property_system.md` (D12 owns the window's generic
section, D30 the holder rule, section 6 the future work);
`doc/erhe/property_inventory.md` owns the per-field status, and its "Not yet
migrated" table lists the hand-written rows that are authored state; section
4.18 of the design record owns the recipe of a per-owner migration.

## 1. Where rows come from

1. Registered properties. `Dependency_property_rows::add_rows` draws the
   properties of an item's own chain, the attached properties of its
   holder type and the secondary properties it holds (D12), for one item
   or for a group of items of one property owner type. `Properties::imgui`
   partitions a multi-selection by owner type (a content-library entry
   standing for its item) and draws
   one such section per type; a single selection draws the section under
   the item's own group through `item_properties` and `dependency_properties`.
2. Hand-written rows. `Properties::item_properties` is the frame (the
   group header, the developer id and flag word, the Add Property and
   Remove Property buttons) around `item_diagnostics`, whose per-class functions
   (`scene_properties`, `light_properties`, `mesh_properties`,
   `node_physics_properties`, `node_joint_properties`,
   `texture_properties`, ...) add the read-only diagnostics, the actions
   and the R5 list editors per item. `material_properties` draws the
   material preview and the BRDF slice for a selected material.

## 2. Requirements

- R1 Every row the window draws for an item comes from the registered
  property path or is a read-only diagnostic. The selector next to Pin
  picks how a multi-selection is drawn: Individual draws every item on
  its own (its group, diagnostics and registered rows);
  Combined draws one section per property owner type and nothing per
  item, and a selection of two types shows one section per type. A
  single item always draws the individual form.
- R1a In a combined section a value the items disagree on shows "mixed"
  in place of a value, per component for a vector (x, y, z, w or the
  color channels each on their own); editing a component sets that
  component on every item and leaves the others as they were, after
  which the shared value shows. A quaternion, a string, an enumeration
  and an object reference are mixed as a whole. A numeric array
  (`float_array`, `int_array`) draws a summary line - the element count
  and the head of the list - and below it one drag field per element, up
  to sixteen; each element mixes on its own, and an edit of one element
  is a set of the whole list (`doc/erhe/property_system.md` D34). A
  `string_array` draws one text field per element, committed when the
  field is deactivated after an edit, plus - where the property says the
  count is the user's (`Property_ui::Array_size::editable`) - a "-"
  button per element and an "Add" button, each of which is one recorded
  set of the whole list; a selection whose lists differ shows the summary
  line alone. A longer list, a read-only one and a selection whose lists
  differ in length stay the summary line alone.
- R2 Authored state is a property, or a bridge (D18) where the storage must
  stay a member: the name (a bridged string property over
  `Item_base::get_name` / `set_name`), the authored persistent flags
  (`show_in_ui`, `lock_edit`, the viewport locks, `no_transform_update`, ...
  as bridged booleans), the tags, and the derived rows that are computed
  properties with setters (D26: `Rendertarget_mesh`, `Animation`,
  `Joint`, the Light derived rows). New authored state of a migrated
  owner is registered the same way rather than hand-written.
- R3 Diagnostics (counts, dimensions, the live rigid body's state,
  raytrace state, skin joints) are read-only rows, drawn per item; they
  are not authored state and need no mixed-value handling. One becomes a
  read-only computed property (D26) where that removes a hand-written
  function for free.
- R4 Every material row is a property row (the slot samplers are the
  seven `<slot>_texture_*` sampler properties, section 4.1 of the design
  record); `material_properties` draws only the preview render and the
  BRDF slice for a selected material.
- R5 List-valued state with no `Property_value` form (the record list
  editors: samplers, animation channels and samplers) and the
  scene's settings-override block keep their hand-written editor, drawn per
  item, and are the documented exception. A list of scalars has a form: it
  is one array property drawn by the generic row
  (`doc/erhe/property_system.md` D34 and D35 - the layout grid track extents
  and the collision filter's three system lists). A fixed set of records has
  one too: it is the per-axis properties of the owner, grouped by axis
  (section 4.22 - a joint-settings item's six degrees of freedom, eleven
  rows each).

## 3. Item-level rows

The rows every item has are properties of `Item_base`:
`name_property` and `tags_property` are string bridges (D18), and each
authored flag bit is a boolean bridge (`lock_edit_property` and the other
flag properties in `item.cpp`; the inventory's `Item_base` table lists them).
Developer mode keeps the id and the whole flag word as read-only diagnostics
(R3).

`lock_edit` carries `Property_flags::writable_when_sealed`, so the seal is
lifted through the same row and the same MCP call that set it.
`Dependency_object::is_write_sealed` is the per-property check that the rows,
the context menu and `set_item_property` share.

Graph-node parameters (section 6 of the design record) are drawn by the Node
Properties window, not this one.

## 4. Verification

Headless over MCP: `get_item_properties` lists the migrated rows with the
right storage and sources; `set_item_property` and `undo` round-trip each
bridged field; a `capture_screenshot` with two nodes selected shows the
combined sections with "mixed" on the components that differ. The edits
are interactive only: select two nodes with meshes and bodies, two
physics materials, and a material plus a body, and check each type has
one section in Combined mode, that dragging one mixed component changes
only that component on every item, and that each edit is one undo entry;
the user drives that check (AGENTS.md "Once the user starts testing").
