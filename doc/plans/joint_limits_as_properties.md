# Joint limits and drives as per-axis properties

Status: proposed

Phase 4 of [hand_written_rows_to_properties.md](hand_written_rows_to_properties.md)
sends its requirements pass here. Extends `doc/erhe/property_system.md` (the
design record of `erhe::property`); `D<n>`, `R<n>` and bare section numbers
refer to that document, and the migration recipe is its section 4.18.
Decision labels of this document are `J1`..

## 1. Goal

`erhe::physics::Physics_joint_settings` holds its six degrees of freedom as
registered properties, so the generic rows of the Properties window draw them,
`Property_set_operation` records every edit, MCP `get_item_properties` /
`set_item_property` reach them, a folder or a Style holds them (D30), and an
edit reaches a live constraint the frame it happens. A settings item states
exactly one limit and one drive per degree of freedom, and states them at the
granularity the simulation and both file formats already use.

## 2. The six axes

J1 The degrees of freedom are the closed set `trans_x`, `trans_y`, `trans_z`,
`rot_x`, `rot_y`, `rot_z`, in that order, indices 0..5. This is the order of
`erhe::physics::Six_dof_constraint_settings::limits` and `::drives`
(`src/erhe/physics/erhe_physics/iconstraint.hpp`), and the axis token set of
the USD multi-apply instances the writer authors (`transX`..`rotZ`,
`axis_instance_name` in `src/erhe/usd/erhe_usd/usd_export.cpp`). An axis token
is the prefix of every property name of that axis, so a property name states
which degree of freedom it belongs to.

J2 A settings item states at most one limit and at most one drive per axis.
Both backends accept exactly that (one `Constraint_axis_limit` and one
`Constraint_axis_drive` per index), the USD schemas admit exactly that (one
`PhysicsLimitAPI:<axis>` and one `PhysicsDriveAPI:<axis>` instance per axis),
and it is what `Node_joint::build_constraint` reduces any richer input to
today. The record lists `Physics_joint_settings::limits` and `::drives` and
the classes `Joint_limit` and `Joint_drive` are removed: the states they can
hold and the six axes cannot (two limits on one axis, a drive naming axis 7, a
limit over several axes whose meaning the simulation does not implement) stop
being representable.

## 3. Properties

J3 Eleven properties per axis, 66 in all, registered in the entry store on
owner type `Physics_joint_settings::property_owner_type()` by a table walk over
the six axis tokens (`std::array<erhe::property::Property<T>, 6>` per row of
the table below, each element registered with its own qualified name). All of
them are `Property_flags::serialize | Property_flags::native_gltf` (J14) and
`.inherits = true`: every one has a plain scalar default, so a value supplied
by a Physics Joints folder or a Style is an ordinary opinion that the item's
own local value overrides, and the hazard that kept the collision filter's
lists local (section 4.21: an empty list is the allowlist / denylist rule
itself) does not arise here.

| Name | Type | Default | UI |
|---|---|---|---|
| `<axis>_limit` | `Joint_axis_limit` enum | `free` | label "Limit" |
| `<axis>_limit_min` | float | 0 | `visible_when` limit is `limited`; `angle_degrees` on the rotation axes |
| `<axis>_limit_max` | float | 0 | as `_limit_min` |
| `<axis>_limit_stiffness` | float | 0 | `visible_when` limit is `limited`; min 0 |
| `<axis>_limit_damping` | float | 0 | `visible_when` limit is `limited`; min 0 |
| `<axis>_drive` | `Joint_axis_drive` enum | `off` | label "Drive" |
| `<axis>_drive_max_force` | float | 0 | `visible_when` drive is not `off`; min 0 |
| `<axis>_drive_position_target` | float | 0 | `visible_when` drive is not `off`; `angle_degrees` on the rotation axes |
| `<axis>_drive_velocity_target` | float | 0 | as `_drive_position_target` |
| `<axis>_drive_stiffness` | float | 0 | `visible_when` drive is not `off`; min 0 |
| `<axis>_drive_damping` | float | 0 | `visible_when` drive is not `off`; min 0 |

J4 `erhe::physics::Joint_axis_limit` is `free` or `limited`, with an
`Enum_info` table so the generic enum row draws it. `free` is the axis the
constraint leaves alone; `limited` is the axis the four values below it
describe. A fixed axis is `limited` with equal min and max, which is what both
formats and both backends spell (`Joint_reach` calls an axis fixed when
`max - min` is below `c_fixed_axis_epsilon`).

J5 `erhe::physics::Joint_axis_drive` is `off`, `force` or `acceleration`, with
an `Enum_info` table. It replaces the pair (`Joint_drive` exists, `Drive_mode`)
of today, so a drive that is described but disabled is unrepresentable.
`Drive_type` is removed: the axis token says whether a drive is linear or
angular.

J6 Three values are unbounded rather than zero when the item supplies no
opinion, and each says so through its value source, the section 4.18 rule for
an unset optional:

- `<axis>_limit_min` and `<axis>_limit_max` with source `default` are the
  unbounded side of the limit. The mirror (J7) substitutes the backend's
  unbounded value, which is `std::numeric_limits<float>::lowest()` / `::max()`
  on a translation axis and `-pi` / `+pi` on a rotation axis - the substitution
  `Node_joint::apply_limit_to_axis` performs today, moved to the settings item
  where the axis index that selects it is known.
- `<axis>_limit_stiffness` of zero is a hard limit; a non-zero value is the
  soft-limit spring. This is the meaning `Constraint_axis_limit::stiffness`
  already carries through `std::optional` and the meaning a zero-stiffness
  spring has.
- `<axis>_drive_max_force` of zero is an unlimited drive force. A drive that
  applies no force is `off`, so zero is free to mean unlimited, and the
  generic float row stays draggable, which `std::numeric_limits<float>::
  infinity()` (today's `Joint_drive::max_force` default) does not.

## 4. Mirrors and the consumers

J7 `Physics_joint_settings` keeps `std::array<erhe::physics::
Constraint_axis_limit, 6>` and `std::array<erhe::physics::
Constraint_axis_drive, 6>` as MIRRORS of the effective values, refreshed in
`on_property_changed` (the bridged-owner recipe of section 4.18), exposed as
`get_axis_limits()` and `get_axis_drives()`. The mirror types are the ones
`Six_dof_constraint_settings` is made of, so the reader that needs them copies
the arrays whole. There is no second list to keep in step with the store: the
consumers below read the mirror, and nothing rebuilds a record list.

The closed list of readers the mirror serves, which is why it is the mirror and
not a rebuilt record list:

- `Node_joint::build_constraint` (`src/editor/scene/node_joint.cpp`) copies the
  two arrays into `Six_dof_constraint_settings`, and its per-limit loop, its
  multi-axis warning and its out-of-range-axis warning go away with the states
  they guarded. `Constraint_axis_drive` has no acceleration-mode field, so
  `Physics_joint_settings::on_property_changed` warns once per item when an
  axis drive is `acceleration` and mirrors it as `force`; the property keeps
  the authored value, so the file round trips.
- `Joint_reach` (`src/erhe/physics/erhe_physics/joint_reach.{hpp,cpp}`) and
  `Physics_drag_constraint` read `Six_dof_constraint_settings::limits`, never
  the records, and are unchanged.
- `Operations::is_hinge_settings`
  (`src/editor/operations/operations_window.cpp`) reads the mirror: a hinge is
  the settings item whose three translation axes are fixed and whose two
  angular axes other than one are fixed.
- `build_physics_description` (`src/editor/parsers/physics_export.cpp`) and
  `import_physics` (`src/editor/parsers/physics_import.cpp`) translate between
  the properties and `erhe::scene::Physics_joint_description` (J12, J13).
- The USD reader and writer reach the item only through that description
  (`src/editor/parsers/usd.cpp`), so they read the mirror through it too.

## 5. The consequence of an edit

J8 A live constraint follows every source of a change, the way a collision
filter's assignment does (section 4.21): `Node_joint` subscribes an
any-property observer (D21) to the `Physics_joint_settings` it resolves,
whenever its `joint_settings` property is set and in its constructors, with
its constraint rebuild as the callback. The observer rebuilds only for a
change of a property owned by `Physics_joint_settings`
(`is_owner_type_or_descendant` on the changed property's owner type): a
rebuild re-captures the joint frames and teleports both bodies to rest, so a
toggle of the settings item's `visible` would stop a swinging body dead.
`Properties::node_joint_properties` keeps the "Connect to Selected Node"
action, the constraint-state diagnostic and the "Rebuild Joint" button -
which is an action, not state: a rebuild re-captures the joint frames after
the user has moved the nodes, which no property change announces. What goes
away is the need to press it after editing the shared settings, and the
scene-scanning `rebuild_joints_using_settings`
(`src/editor/scene/physics_edits.{hpp,cpp}`), as `reapply_collision_filter`'s
scanning helper was.

## 6. The Properties window

J9 `Properties::physics_joint_settings_properties`
(`src/editor/windows/properties.cpp`) and its `c_drive_type_names` /
`c_drive_mode_names` tables are removed; the generic section draws all 66 rows.
The rows are grouped by axis through `Property_ui::group`: `Translation X`,
`Translation Y`, `Translation Z`, `Rotation X`, `Rotation Y`, `Rotation Z`,
eleven rows each, of which the `visible_when` callbacks of J3 leave two while
the axis is free and undriven. There is no add, remove or reorder: the axes are
the six the joint has, so `Item_insert_remove_operation`, the Hierarchy Create
menu and a row action have nothing to create, and undo is the
`Property_set_operation` every generic row already records.

## 7. Persistence

J10 The item has no child items and gains none, so no writer needs a new
exclusion. This is the reason the child-item form of the parent plan's H6 is
not taken: the USD writer plans every `erhe::Typed` child of a planned prim as
a prim of its own (`write_plan_prim` / `is_carried_without_content_flag`,
`src/erhe/usd/erhe_usd/usd_export.cpp`), and a joint-settings item is already
a planned prim (`is_physics_resource_prim`), so a `Joint_limit` child item
would be written as a prim under it unless both passes learned to skip it, and
`erhe::Item_type` has seven free bits left (`count = 57`,
`src/erhe/item/erhe_item/item.hpp`) for the two classes it would need.

J11 USD: each axis whose `<axis>_limit` is `limited` is one
`PhysicsLimitAPI:<axis>` instance and each axis whose `<axis>_drive` is not
`off` is one `PhysicsDriveAPI:<axis>` instance - the instances
`write_joint_limits_and_drives` already authors, now one per axis by
construction rather than by a loop over a record's axis list. On read,
`join_limits` (`src/erhe/usd/erhe_usd/usd_import_physics.cpp`) is removed: each
instance sets the properties of the axis it names, and a `distance` instance
sets the three translation axes to the same values, which is the mapping it has
today. The 66 registrations are excluded from the USD physics record by a
`c_physics_joint_description_fields` list in `src/editor/parsers/usd.cpp`, the
way `c_collision_filter_description_fields` excludes the filter's three lists,
so the prim's attributes are written once.

J12 glTF export: the `KHR_physics_rigid_bodies` `physicsJoints` entry is built
from the mirror - one `Physics_joint_limit` per `limited` axis, its
`linear_axes` or `angular_axes` naming exactly that axis, `min` and `max` set
only where the property has a value source other than `default`; one
`Physics_joint_drive` per axis that is not `off`. A settings item authored from
a file that grouped several axes in one limit entry therefore exports as one
entry per axis: that is what erhe simulates either way (`Node_joint` warns
today that "radial limits are approximated per-axis"), and it is what the USD
writer already emits for the same item.

J13 glTF import: each entry of `physicsJoints[i].limits` sets the properties of
every axis it names, and each entry of `.drives` the properties of the axis it
names. When two entries name one axis, the later entry wins and the import logs
one warning naming the joint and the axis - the silent overwrite of today, made
visible.

J14 `ERHE_scene` gains a `physics_joints` array, one entry per
`physicsJoints` index, of the same shape as `physics_materials`:
`{"name", "properties"}`, where `properties` is the item's complete local set
(`json_properties`, `src/editor/parsers/gltf_extensions_export.cpp`). It is
what lets a value the item inherits from a folder or a Style stay inherited
after a reload, since the KHR entry carries effective values; the import half
already consumes it (`Physics_import_item::properties` with
`Physics_property_set::complete_local_set`,
`src/editor/parsers/physics_import.cpp`), and `parse_gltf_physics_item_names`
gains the read of the array the way it reads `physics_materials`. It also gives
a joint-settings item its name back across a reload: `PhysicsJoint` has no name
field, so `Physics_joint_description::name` is empty on the glTF path today and
every reloaded item is renamed "Physics joint `<i>`".

J15 The known open defect - `import_gltf` of an exported `physics.glb` failing
to parse `KHR_physics_rigid_bodies.physicsJoints[].limits` - stays open. This
work keeps every field spelling of the entry and changes only how many entries
the writer emits, so it neither fixes nor worsens the failure; phase 2 records
it as the known failure of its `.glb` leg. One hazard next to it does go away:
`out_drive.maxForce` is written from a `float` whose default is infinity
(`src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp`), which is not a JSON number,
while J6 makes the default a finite zero.

## 8. MCP

J16 Every tool keeps its argument shape, so `scripts/creations/common.py`'s
`joint_settings()` and creation 21 run unchanged; the arrays become a facade
over the properties.

- `create_physics_joint_settings`: `limits` and `drives` keep the KHR-shaped
  arrays (`linear_axes` / `angular_axes` / `min` / `max` / `stiffness` /
  `damping`; `type` / `mode` / `axis` / `max_force` / `position_target` /
  `velocity_target` / `stiffness` / `damping`), applied per axis by the J13
  rule, including its later-entry-wins warning. An `axis` outside 0..2 and an
  entry naming no axis are refused with an error naming the entry index, which
  today is silently ignored or warned about at constraint build time.
- `edit_physics_joint_settings`: the same arrays, written through the property
  setters; `new_name` unchanged. It stays non-undoable, as
  `edit_collision_filter` is, and it no longer calls
  `rebuild_joints_using_settings` (J8). `set_item_property` of one of the 66
  names is the undoable route.
- `get_physics_items`: each joint-settings entry keeps `name`, `id`, `limits`
  and `drives`, rebuilt from the mirror in the KHR shape, so a caller that
  wrote an array reads the same array back.
- `get_item_properties` / `set_item_property` /
  `get_addable_item_properties` reach all 66 by qualified name with no tool
  change.
- `get_node_details` keeps naming a `Node_joint`'s settings item by name.

`parse_joint_limits`, `parse_joint_drives` and `joint_settings_to_json`
(`src/editor/mcp/mcp_server_shared.{hpp,cpp}`) keep their signatures against
the item instead of the record vectors.

## 9. Phases

Each phase is one commit through `doc/agents/orchestration_harness.md`.

### Phase 1: the model and its consumers

Files: `src/erhe/physics/erhe_physics/physics_joint_settings.{hpp,cpp}`
(J3-J7), `src/erhe/physics/test/test_joint_settings_properties.cpp` and
`src/erhe/physics/test/CMakeLists.txt` (new test, the shape of
`test_collision_filter_properties.cpp`: defaults, setter to mirror, untyped
access with enum labels, an inherited value reaching the mirror, clone),
`src/editor/scene/node_joint.{hpp,cpp}` (mirror copy, J8 observer),
`src/editor/scene/physics_edits.{hpp,cpp}` (the file is removed with its one
helper; the "Rebuild Joint" button stays, with its tooltip reworded to name
moving the nodes),
`src/editor/operations/operations_window.cpp` (`is_hinge_settings`, the hinge /
ball creation at `create_joint_settings`'s caller),
`src/editor/scene/scene_commands.cpp` (joint creation),
`src/editor/windows/properties.{hpp,cpp}` (J9),
`src/editor/mcp/mcp_server_shared.{hpp,cpp}` and
`src/editor/mcp/mcp_server_physics.cpp` (J16),
`src/editor/parsers/physics_export.cpp` and
`src/editor/parsers/physics_import.cpp` (J12, J13 in their description form).
The type deletion of J2 reaches all of these, so they land together.

Verification:
`cmake --build build_vs2026_vulkan_headless --target editor --config Debug`;
`./scripts/build_ninja_win_vulkan.bat editor`;
`build_ninja_win_vulkan/bin/erhe_physics_tests.exe` (27 today plus the new
cases) and the same suite in a box3d tree (78 today);
`py -3 scripts/creations/creation_21_newtons_cradle.py --scene-only` builds the
cradle over the unchanged MCP arguments;
`py -3 scripts/physics_drag_joint_sweep.py` passes 16/16 on both backends with
the worst hold at or below the 0.23 mm it reports today;
over MCP on a running headless editor: `set_item_property` of
`Physics_joint_settings.rot_z_limit` to `limited` on the cradle's hinge while
the simulation runs changes the swing without a "Rebuild Joint", `undo`
restores it; `close_scene` then `scene-close check: all N released` in
`logs/log.txt`.

### Phase 2: the two file formats

Files: `src/erhe/usd/erhe_usd/usd_import_physics.cpp` (J11 read),
`src/erhe/usd/erhe_usd/usd_export.cpp` (J11 write),
`src/editor/parsers/usd.cpp` (`c_physics_joint_description_fields`),
`src/editor/parsers/gltf_extensions_export.cpp` and
`src/editor/parsers/gltf_extensions_import.cpp` and
`src/editor/parsers/gltf_physics_import.cpp` (J14),
`src/erhe/usd/test/` (the `physics.usda` fixture's expectations).

Verification: `erhe_usd_tests` at its baseline plus the new per-axis cases;
`py -3 scripts/scene_roundtrip_verify.py` at 457/459 (the two known failures);
a save of the `physics.usda` fixture is byte-identical on the second save;
`usdchecker` reports Success on that save; a joint-settings item whose
`rot_z_limit_max` comes from a Physics Joints folder still reads source
`inherited` after `save_scene` + `open_scene` on both `.glb` and `.usda`; the
`.glb` re-import failure of J15 is recorded as the known failure.

### Phase 3: the documents

Files: `doc/erhe/property_system.md` (a new section 4.22 stating the six axes,
the 66 properties, the mirrors and the observer path; section 4.17's
"Rebuild Joint" sentence, which now names moving the nodes),
`doc/erhe/physics.md`,
`doc/editor/properties_window.md`, `doc/gltf_extensions/` (the `ERHE_scene`
`physics_joints` array), `doc/erhe/usd_compatibility.md` (the Physics table's
limit and drive rows), `doc/plans/hand_written_rows_to_properties.md` (phase 4
closed, H6 superseded by this document's J2 and J10) and this document (Status
becomes `in progress`, then the document is deleted with its last phase).
Verification: `py -3 scripts/check_doc_links.py` reports 0 problems.

## 10. The user's interactive checklist

- A Physics Joints library item shows six axis groups in the Properties window;
  setting an axis limit to `limited` reveals its four value rows and hides them
  again on `free`.
- Dragging a rotation limit's Min or Max shows degrees and moves a jointed body
  in the viewport while the simulation runs, with no "Rebuild Joint"; the
  button is still there, and pressing it after moving the joint's nodes
  re-captures the joint frames.
- Ctrl+Z restores the previous value, once per completed drag.
- A Style holding `Physics_joint_settings.rot_z_limit_max` drives a settings
  item that has no local value for it, and clearing the item's local value
  returns it to the Style's.
- The Newton's cradle scene (creation 21) swings as it did before.
