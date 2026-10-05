# Data-model undo and reflective MCP item edits

Status: proposed

Audit items 13 and 14 of `doc/reference/audit_erhe_2026_09_30.md` section 8,
worked together because both stand on the dependency-property registry
(`doc/erhe/property.md`). Extends `doc/editor/operations.md` (undo) and
`doc/agents/mcp_api_guidelines.md` (MCP document edits). Selection and order
are recorded in `doc/plans/audit_2026_09_30_followups.md`.

## 1. Goal

- Item 14: an operation that edits properties records what it wrote instead
  of hand-coding the before / after of each field. Undo restores the exact
  prior local layer (value, expression or "unauthored") of every property
  the operation wrote, and every such write reaches the editor's document
  consequences (`App_context::on_item_property_changed`) on execute, undo and
  redo alike. Structural changes (insert / remove / reparent, geometry
  replacement) stay bespoke operations.
- Item 13: MCP item edits go through one generic verb whose accepted
  properties, types, enums and documentation come from the registry, so a
  property added to a class is editable and described over MCP with no MCP
  code. The per-type edit tools lose their property halves.
- One stated notification design for document edits (section 6), decided
  before any mechanism is removed.

Out of scope: operation merging / coalescing, per-scene undo histories, a
Lua / Python binding (it would bind the same verb and schema later),
`Compound_operation` rollback on partial failure.

## 2. Facts this plan stands on (2026-10-05)

- `Dependency_object::read_local_state` / `apply_local_state`
  (`dependency_object.hpp:299-300`) read and restore one property's local
  layer as `std::optional<Local_state>`, `Local_state` =
  `variant<Property_value, Expression_text>`, `nullopt` = no local value.
  `Property_set_operation` (`src/editor/operations/property_set_operation.*`)
  already stores exactly this, so exact restore is solved per property.
- `Property_changed_args` carries effective old / new values and sources and
  is delivered synchronously for every effective change, including inherited,
  style, reference and `default_from` propagation, and coalesced by
  `Change_batch`. It is therefore the wrong signal to record for undo: a
  recording of it would contain consequences as well as edits. What undo
  needs is the set of local-layer writes.
- No global hook sees every property write. `on_item_property_changed`
  (`src/editor/app_context.cpp:19`) is reached only through
  `apply_item_property` (Property_set_operation) and `Style_set_operation`;
  direct `set_value` calls skip it.
- `Property_set_apply_operation` (multi-property, used by `edit_material`
  and `dependency_property_rows.cpp`) stores `optional<Property_value>`
  before states, so undo loses an expression layer. That is a live defect.
- MCP: 298 static tools in `config/editor/mcp_tools.json`, dispatched by
  name (`mcp_server.cpp:576`); arguments are not checked against
  `inputSchema`. `set_item_property` (`mcp_server_properties.cpp:323`) is
  already reflective for one property. Undo holes that remain after audit
  item 5: `edit_physics_body`, `edit_joint`, `edit_physics_material`,
  `edit_collision_filter`, `edit_physics_joint_settings` write directly, and
  `set_scene_settings` assigns the codegen `Scene_settings` directly. The
  rule that this is a defect is in `doc/agents/mcp_api_guidelines.md`
  "Document edits are operations".
- `Property_metadata` / `Property_ui` provide type (20 `Property_type`
  values), enum labels and values, default, read-only, tooltip, group, UI
  min / max / step, presentation. Min / max are UI hints, not enforced; real
  constraints are opaque `coerce` / validate callbacks.
- Side effects outside the property layer that operations send by hand:
  `node_touched` and physics teleport (`Node_transform_operation`),
  `mesh_geometry_changed` (geometry operations), collision shape and motion
  mode restore (`Mesh_operation`). `INode_system::on_values_changed`
  (physics, rig, layout, draw mode) is driven by property callbacks, so
  restoring the property restores those systems.
- `metadata.property_changed` callbacks run synchronously inside the write
  (`dependency_object.cpp:1449-1451`), so a callback that writes another
  property's local layer does so during the edit.
- Not every property write passes `set_value_internal`:
  `Item_base::set_flag_bits` writes `m_flag_bits` directly (`item.cpp:480`;
  the flag properties are bridges whose `set` calls it, `item.cpp:247-262`),
  and `Node::set_parent_from_node` goes through
  `Xformable::handle_transform_update` (`node.cpp:421`). Such member writes
  are invisible to a library-level recording.
- Setters that are property writes: `Material::set_data` (all scalars,
  texture references and per-slot sampler state, in one `Change_batch`,
  `material.cpp:725-757`); `Joint::set_body_1` / `set_settings` /
  `set_enable_collision` (`joint.cpp:158-181`); the `Physics_material` and
  `Collision_filter` setters (`physics_material.hpp:103-111`,
  `collision_filter.hpp:65`).
- Not property writes: `Node_physics_system::set_collision_shape`
  (`mcp_server_physics.cpp:378`; no operation exists for it, and
  `Mesh_operation::restore_physics` writes it directly);
  `Variant_select_operation` writes `Scene_root::get_variant_table()` and
  `Scene_settings::variant_selections` (`variant_select_operation.cpp:340-360`),
  its property writes are sibling operations in the compound built by
  `make_select_variant_operation`; `set_scene_settings` assigns
  `sr->get_scene_settings()` (`mcp_server_scene_action.cpp:396`).
- Sub-object resolution exists: `Mesh_primitive::get_owner()` /
  `get_index()` (`mesh.hpp:71-72`). The only other non-item
  `Dependency_object` is `Property_style` (`property_style.hpp:19`), written
  only by `Style_set_operation`.

## 3. Design: property write recording (item 14)

### 3.1 Library: `erhe::property` write recording

A recording scope collects the local-layer writes made on the current
thread while it is open:

```cpp
class Property_write_record
{
public:
    Dependency_object*              object;
    const Dependency_property*      property;
    std::optional<Local_state>      before; // first write's prior state
    std::optional<Local_state>      after;  // state after the last write
};

class Property_write_recording // RAII, one active per thread, no nesting
{
public:
    Property_write_recording();
    ~Property_write_recording();
    [[nodiscard]] auto take_records() -> std::vector<Property_write_record>;
};
```

- The hook sits in the local-layer write path after the seal / read-only /
  validate gates (`set_value_internal`, `clear_value`, `set_expression`, and
  `apply_local_state`, which reaches them). Bridged properties written
  through `set_value` are recorded (the bridge `set` runs inside that path).
  For a writable computed property the computed branch records nothing; its
  `compute_set` writes the stored target through `set_value`, which records
  the target once. `set_current_value` over an installed expression is not
  recorded: `read_local_state` returns the expression text before and
  after, so there is nothing to restore; without an expression it writes
  the local layer and is recorded. A `Property_key` write or clear of a
  read-only property is not recorded (and its refusals are not counted):
  `apply_local_state` cannot restore it, and it is owner-maintained state
  that the owner re-derives when the authored writes are restored.
  Propagation, animated, style and reference layers are not recorded.
- An object destroyed while the recording is open drops its records and
  refusals (its destructor tells the active recording), so no record
  points at a destroyed object.
- Member writes that bypass `set_value` (`set_flag_bits`,
  `set_parent_from_node`, any setter writing a plain member) are invisible to
  the recording. The rule for edit functions is therefore: write through
  `set_value` / property setters only.
- Per (object, property) the first write records `before` via
  `read_local_state` before the write; `after` is read when the recording is
  taken, so a property written several times appears once.
- A write rejected by seal / read-only / validate is not recorded.
- The active recording is a `static thread_local` pointer (no atomics).
  Writes on other threads are not recorded; operations execute on the main
  thread (`Operation_stack` verifies it).
- Cost when no recording is active: one thread_local pointer test per local
  write. Records live in a vector the recording owns; it is not per-frame
  code.
- The recording counts refused writes (seal, read-only, validate, bridge
  `validate` such as name uniqueness, `dependency_object.cpp:780-786`) and
  exposes the count with the property of each refusal, so the operation can
  report them (3.2).
- `Change_batch`: a batch opened and closed inside the recording flushes its
  cascades inside it (`flush_batch`, `dependency_object.cpp:1525`), which is
  tested. A batch still open on a recorded object when the records are taken
  is a usage error; the recording checks the objects it recorded (it reads
  their batch depth as a friend), which is the only place it can occur.
- Tests in `src/erhe/property/test/`: first-before / last-after, clear to
  unauthored, expression restore, bridged property, writable computed
  property recorded once on its target, refused write counted and absent,
  propagation to descendants absent, batch closed inside the recording,
  no recording -> no records. `CHANGELOG.md` entry.

### 3.2 Editor: `Property_edit_operation`

One operation class that replaces hand-written property before / after:

- Constructed with a description and an edit function
  (`std::function<void()>`) that performs property writes on items
  (setters such as `Light::set_intensity` are fine; they write
  properties). The function captures what it edits; no context object is
  passed, because the cross-scene reference check below runs on the
  records, which covers object values written through setters as well.
- First `execute`: opens a `Property_write_recording`, runs the edit
  function once, takes the records and resolves each record's object to an
  owning item handle (`shared_ptr<Item_base>` plus the sub-object index of
  D29 when the object is a `Mesh_primitive`, via `get_owner()` /
  `get_index()`). A record whose object does not resolve (a `Property_style`,
  or an object no item owns) is a fatal error: the edit function wrote
  something that is not document state of a live item, which belongs in a
  bespoke operation. The edit function is released after the first execute.
- Refusals: a write refused during the edit (gates, bridge validate), or
  a recorded object value that the cross-scene reference check of
  `apply_item_property` (`is_item_reference_allowed`) refuses, puts the
  operation in error (`Operation::set_error`, `operation.hpp:64`) naming
  the property; the writes that did happen are restored from their before
  states and no undo entry is left (`Operation_stack` does not record an
  operation that is in error after its first execute). An edit that
  recorded nothing is also an error.
- Redo applies the `after` states in record order; undo applies the
  `before` states also in record order, each followed by
  `on_item_property_changed`. Forward order is required for cascades: an
  edit that writes A, whose callback writes B, records [A, B]; undo writes
  A = a0 (the callback writes B = f(a0)) and then B = b0, which restores an
  independently authored b0. Reverse order would lose it. The first
  execute does not re-apply (the writes happened); it only runs
  `on_item_property_changed` per record.
- Asset reference userships (`Property_set_operation`'s
  `adopt_userships`) are adopted for every object-reference record.
- `collect_item_references` reports every recorded item, so asset unload
  refusal keeps working.
- Cascaded writes: a callback that writes another local layer during the
  edit is recorded as well. The A2 test authors the cascade target locally
  before the edit and checks that undo returns every recorded local layer
  to its before state. A callback that writes only on one transition
  defeats any fixed order, so the gate for every migrated operation is the
  undo / redo round trip of the item's full property dump (3.3).

`Property_set_operation` stays as the single-property form (its follow-up
operations and computed-write construction are used by the UI rows); its
apply code is shared with `Property_edit_operation` so both reach the same
consequences.

### 3.3 Which operations move

Moves onto `Property_edit_operation` (each verified by an undo / redo
round-trip of the item's full property dump):

- `Property_set_apply_operation` (fixes the expression-loss defect).
- `Material_change_operation` (`Material::set_data` writes only
  properties).
- In `make_select_variant_operation`, the per-opinion
  `Property_set_operation`s collapse into one `Property_edit_operation`;
  `Variant_select_operation` itself stays (it writes the variant table and
  `Scene_settings`).
- `Item_set_flag_bits_operation`: its one caller (`scene_root.cpp:1139`,
  `no_transform_update`) is rewritten to write
  `no_transform_update_property` through `set_value` inside a
  `Property_edit_operation`, and the class is deleted. `set_flag_bits`
  itself stays a member write and is never called from an edit function.
- The MCP edits of step A3.

Stays bespoke: every structural operation (insert / remove / parent /
reposition / scope / library attach), the `Mesh_operation` family and other
geometry edits, `Node_transform_operation` (bus message, physics teleport,
and `set_parent_from_node` is a member write), `Style_set_operation` (style
layer), `Variant_select_operation`, animation edits, lightmap tile
overrides, scene builder / raytrace / import operations, and the new
collision shape and scene settings operations of A3.

## 4. Design: reflective MCP item edits (item 13)

### 4.1 `set_item_properties`

```json
{"name": "set_item_properties",
 "arguments": {"item_id": 12, "scene_name": "...", "sub_object": 0,
               "properties": {"intensity": 5.0,
                              "color": [1, 0.5, 0.2],
                              "range": null,
                              "inner_spot_angle": {"expression": "outer_spot_angle * 0.5"},
                              "physics_material": {"reference_id": 33}}}}
```

- Property lookup, value parsing, reference resolution, read-only / seal /
  validate checks are the ones of `set_item_property`, moved into a shared
  function both tools call.
- All properties are checked before anything is written (lookup, parse,
  read-only, seal, `validate_value`, and the cross-scene reference check);
  one bad entry fails the whole call with a message naming the property. A
  refusal that only shows at write time (bridge validate) fails the
  operation as in 3.2, nothing stays written, and the reply names it.
- One `Property_edit_operation`, `execute_now`, one undo entry. The reply
  lists per property the before and after local layer and the effective
  value after the write.
- `set_item_property` stays (it is the most used reflective tool in scripts
  and docs) and becomes a one-entry call of the same code. Its behavior
  changes from `queue()` with `"queued": true` (`mcp_server_properties.cpp:377-386`)
  to `execute_now` with the before / after reply, so a later call in the
  same `batch` sees the write. No in-repo script reads `queued` from it;
  C1 states the change in `doc/agents/mcp_api_guidelines.md`.

### 4.2 `get_property_schema`

Returns a JSON schema per owner type, generated from the registry at run
time: `{"item_type": "Light"}` -> `{"type":"object","properties":{...}}`
with each property's JSON type from a fixed mapping table covering all 20
`Property_type` values (`property_value.hpp:92-124`): `vec2/3/4`, `ivec2/3/4`
and `quat` (xyzw) as fixed-length number arrays, `mat4` as 16 numbers,
`float_array` / `int_array` / `string_array` as variable-length `items`
arrays, `string` and `asset_path` as strings, `object` and `weak_object` as
`{"reference_id"}`; `enum` lists the enumerator names that `parse_value`
accepts, not the UI labels; `default`, `readOnly`, `description` from the
tooltip plus the group, the UI range as `x-erhe-ui-minimum` /
`x-erhe-ui-maximum` (hints, not bounds, so standard `minimum` / `maximum`
are not used) and `x-erhe-developer-only`. `Property_ui::visible_when` is
per object and not in the schema; `get_item_properties` reports what one
object shows. Attached and computed-writable properties appear under their
qualified names. Owner types are those `Property_registry` knows; the
editor maps item type names to owner types the way `get_item_properties`
does.

The static `mcp_tools.json` descriptor of `set_item_properties` stays
generic and points to `get_property_schema`. This replaces the audit's
"generate `mcp_tools.json` descriptors from metadata": once the per-type
edit tools lose their property halves, there is nothing per-type left to
generate.

### 4.3 The per-type edit tools

| Tool | Property fields -> `set_item_properties` | What stays |
|---|---|---|
| `edit_light` | type, color, intensity, range, cast_shadow, spot angles | `position` already exists as `set_node_transform`; tool deleted |
| `edit_camera` | exposure, shadow_range, fov_y, z_near / z_far | z_near / z_far name the projection-specific property; tool deleted |
| `edit_material` | the scalar / color fields, slot texture references and the per-slot sampler properties | tool deleted (`Material::set_data` writes only properties) |
| `edit_physics_body` | motion_mode, is_trigger, center_of_mass_offset, gravity_factor, initial linear / angular velocity, physics_material, collision_filter, mass (all `Node_physics` properties, A3) | `set_collision_shape` (shape arguments, through `Collision_shape_set_operation`, A3); `wake` is listed in the descriptor but the edit handler never read it (only `create_physics_body` does); runtime state, so `wake_physics_body` if kept |
| `edit_physics_material`, `edit_collision_filter` | every field (setters are `set_value`; `new_name` is the `name` property, A3) | tools deleted |
| `edit_physics_joint_settings` | every field: `new_name` is the `name` property, the `limits` / `drives` entries write the 66 axis properties (`Physics_joint_settings::limit_*_property` / `drive_*_property`, eleven per axis); no non-property field (A3) | tool deleted |
| `edit_joint` | body_1, settings, enable_collision (setters are `set_value`) | A3 confirmed the rebuild path: a write of body_0 / body_1 / joint_settings / enable_collision reaches `Joint::on_property_changed` -> `Joint_system::on_values_changed`, which destroys and recreates the constraint, so the edit, its undo and its redo rebuild (tested through the live constraint's limits); an edit of the settings item rebuilds every joint using it through `Joint_system::observe_settings`. The tool never called `rebuild()` after its writes; its `rebuild` argument is an explicit re-capture of the joint frames from the current poses (runtime state, outside undo) and needs a runtime tool of its own (`rebuild_joint`) before the tool is deleted |
| `set_scene_settings` | `ambient_light` | `settings` / `merge` stays, through `Scene_settings_set_operation` (codegen struct before / after, A3; `scene_id` and `variant_selections` are kept and a different value is refused) |
| `new_name` arguments of the physics tools | `name` property | - |

Every caller is migrated in the same commit: `scripts/*.py`,
`scripts/creations/`, `src/editor/mcp/test/mcp_server_tests.cpp`, the
skills under `.claude/`, and `doc/`. Measured blast radius (2026-10-05):
under 40 script lines, about 25 test lines, about 96 doc lines.
`mcp_tools.json` is recounted before C3 (the scout counted 298 entries,
the reviewer 329 `"name"` keys, which include nested schema names).

Follow-up found in A3: the Properties window scene override rows
(`properties.cpp`, `override_struct`) and `Fly_camera_tool::get_writable_camera_controls`
write `Scene_settings` directly - not undoable, and an undo of an earlier
`set_scene_settings` reverts them; they are to go through
`Scene_settings_set_operation`.

## 5. Steps

Each step is one commit (builds, tests, docs, `CHANGELOG.md` where an
`erhe::*` API changes), worked through the orchestration harness
(`doc/agents/orchestration_harness.md`), and gets a Fable review at medium
effort before commit. Targets that must build: `editor`, `src/example`,
`src/hello_swap`, `src/hextiles`, the `erhe::*` libraries.

| Step | Content | Depends on | Size |
|---|---|---|---|
| A1 | `Property_write_recording` in `erhe::property` with its tests (3.1) | - | S |
| A2 | `Property_edit_operation` (3.2); shared apply code with `Property_set_operation`; `editor_operation_tests` for record / undo / redo and a cascade | A1 | M |
| A3 | Close the MCP undo holes: the property fields of the five physics tools through `Property_edit_operation`; new `Collision_shape_set_operation` (also used by `Mesh_operation::restore_physics` if that removes its direct write); `Scene_settings_set_operation`, whose execute / undo notify each consumer of the changed `Scene_settings` fields directly (named in the commit, no per-frame comparison; `Lightmap_tile_overrides_operation` is the precedent); confirm the joint rebuild path (4.3); cases added to `Mcp_test.document_edits_record_one_undo_entry_each`. May split into two commits (physics, scene settings) | A2 | L |
| A4 | Move the operations of 3.3 onto `Property_edit_operation`; test that an expression survives undo of a multi-property edit; full property dump undo / redo round trip per migrated operation; operation class count before / after in the commit message | A2 | M |
| B1 | Notification decision (section 6): written, then shown to the user before any code | A4 | S |
| C1 | `set_item_properties` and the shared set path (4.1); tests: multi-property one undo entry, all-or-nothing, sub-object, expression, reference, reset to default | A2 | M |
| C2 | `get_property_schema` (4.2); test: for every registered owner type the schema is valid JSON, and setting every writable property to its schema default through `set_item_properties` succeeds | C1 | M |
| C3 | Per-type edit tools reduced (4.3) with all callers migrated; `mcp_tools.json` entries removed or replaced; line delta of `src/editor/mcp` in the commit message | C1, C2, A3 | M |

A3 and C1 can swap order; C3 must come after both.

## 6. Notification design (B1)

The audit asks for one notification design instead of four (property
observers and metadata callbacks, `INode_system`, `Scene_host` callbacks,
`Transform_observer`, plus the editor message buses). The facts so far:
`INode_system::on_values_changed` is itself driven by the property
`property_changed` metadata callback, so it is a property observer with a
system dispatcher; `Scene_host` carries coarse mesh / light structure
events that are not property changes and may come from worker threads;
`Transform_observer` carries world-transform dirtiness, which is derived
state, not a property write.

After A4 every document property edit runs through one of two operation
classes and reaches `on_item_property_changed`. B1 writes down which
mechanism owns which kind of event (document edit consequences,
derived-state invalidation, structure events, UI notification), lists each
mechanism whose uses all fall under another one, and proposes the removals.
No mechanism is removed in this plan without the user's approval of B1.

## 7. Risks and open questions

- Bridged properties always report a local value, so their "before" is
  never unauthored; restoring writes the old value, which is the intended
  behavior for transform / name.
- Edit functions that also mutate non-property state (including
  `set_flag_bits` and `set_parent_from_node`) would lose that state on
  undo. The rule is in `doc/editor/operations.md`
  "Property_edit_operation", with why the item mutation serial cannot
  serve as a debug check for it.
- Removing per-type tools changes the MCP API used by creation scripts;
  all in-repo callers are migrated in C3, and the guideline document says
  which tool to use instead.
