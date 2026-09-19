# IK settings as node properties

Status: in progress

Replaces the `Ik_settings` node attachment of `ik_settings.md` with
attached properties held by the bone node itself. It is the first step of
the general direction (decided with the user, 2026-09-19): per-node data
lives in properties of the node, grouped in the Properties window by
`Property_ui::group`, and node attachments are retired one at a time.
Solver behavior (`ik_settings.md` sections 3 and 4, `pole_target.md` R5 to
R16) is unchanged; this plan changes where the values live, how they are
found, and how they are saved.

## Requirements

**P1. Weak object reference.** `erhe::property` has a second object
reference kind, `Weak_object_reference` (`Property_type::weak_object`),
holding a `std::weak_ptr<Dependency_object>`. It is entry-stored like any
value. Reading it yields the locked target, or an empty reference when the
target has expired. Its text form, its parse through the referencing
object's `resolve_expression_object`, its Properties row (the
`reference_item_types` picker), its MCP get / set form and its late glTF
resolution (`Unresolved_object_property`) are those of `Object_reference`.
Like `Object_reference` it takes no part in expressions or animation. Two
weak references compare equal when they name the same control block
(`owner_before` both ways). A typed accessor pair mirrors
`Member_value_traits<std::shared_ptr<U>>` for the weak kind.

**P2. Owner.** The editor class `Ik` (`src/editor/scene/ik_properties.{hpp,cpp}`,
a registration holder with static members only) registers every IK value
with `register_attached`, owner type `Ik`, holder type
`erhe::scene::Node`, UI group `IK`. Qualified names: `Ik.lock_x`,
`Ik.lock_y`, `Ik.lock_z`, `Ik.limit_x`, `Ik.limit_y`, `Ik.limit_z`,
`Ik.limit_min`, `Ik.limit_max`, `Ik.stiffness`, `Ik.rest_rotation`,
`Ik.pole_target`, `Ik.pole_angle`. Types, defaults, coercion, presentation,
tooltips and `developer_only` are those of `doc/erhe/property_system.md`
section 4.19. `Ik.pole_target` is a `Weak_object_reference` (P1) with
`reference_item_types = erhe::Item_type::xformable`. `erhe::scene` names
nothing of IK.

**P3. Inheritance.** Every `Ik.*` property is registered with
`inherits = false`. A limit set shared by several bones is a Style holding
the `Ik.*` values.

**P4. Visibility.** Each `Ik.*` property's `visible_when` is "the object is
a Node carrying `Item_flags::bone`". The D12 listing rule then offers the
rows on bones, and a non-bone node that holds a local `Ik.*` value still
lists it.

**P5. Rest rotation.** `Ik.rest_rotation` has a per-object default (D31
`compute_default`): when the node and its parent node are joints of the
same `erhe::scene::Skin` (first such skin in scene order), the rotation of
`inverse(world_from_bind(parent)) * world_from_bind(joint)`,
orthonormalized; otherwise identity. The bind-pose lookup is
`erhe::scene::get_bind_pose_local_rotation(const Node&) ->
std::optional<glm::quat>` in `erhe_scene/skin.{hpp,cpp}`, lifted from
`capture_ik_rest_rotation`. A local value overrides the default. The
Properties action "Set rest from current pose" is drawn for a bone node in
the node's own section, next to the IK group, and records one
`Property_set_operation` of `Ik.rest_rotation`.

**P6. Reading.** `read_ik_settings(const erhe::scene::Node&) ->
Ik_settings_data` (in `ik_properties.{hpp,cpp}`) reads the effective
values; `Ik_settings_data` keeps its fields. `Ik_drag::resolve_constraint`
calls it once per chain joint at drag begin and ORs the channel-lock flags
as today; a joint whose locks and limits are all off is unconstrained, so
the constraint-free Phase 1 path is taken exactly when it is today for a
chain without attachments. `Ik_drag::discover_pole` reads
`Ik.pole_target` and `Ik.pole_angle` from the scanned joint nodes (R5, R6
of `pole_target.md`, with "attachment" read as "node").

**P7. Removal.** The following are deleted: class `Ik_settings`
(`node_ik_settings.{hpp,cpp}`), its attachment catalog entry,
`Scene_commands::attach_new_ik_settings` and `capture_ik_rest_rotation`,
`Item_type::ik_settings` (bit 57 becomes free), the `ik_settings` value of
the MCP `add_node_attachment` type enum, and the USD save warning's
attachment count, which counts nodes holding a local `Ik.*` value instead.

**P8. glTF.** The `Ik.*` local values ride the node's
`ERHE_node.properties` map by qualified name (D14), `Ik.pole_target`
through the late by-name resolution that node-held object references use.
`ERHE_rig` is removed: writer, reader (`import_rigs`), the
`Gltf_export_arguments::node_extensions_builder` use that carried the pole
node index, the `ERHE_rig` spec page, its schema, and its rows in
`doc/gltf_extensions/README.md` and `doc/editor/scene_serialization.md`.
Files saved with `ERHE_rig` load without IK values (decided with the user:
no migration).
Trap hit before (`pole_target.md`, glTF section): a pole reference written
as an item path failed to resolve when the file was imported under an
import root. The pole must survive (a) save + open and (b) import of the
saved file into another scene; the round-trip script checks both.

**P9. Clone and prefab.** Entry-stored values copy with the node (D10), so
a cloned or prefab-instantiated bone keeps its `Ik.*` values and names the
same pole node.

## Verification

- `erhe_property_tests`: weak reference set / get / expiry / equality /
  to-string / parse / copy.
- `editor_ik_solver_tests`: `test_ik_settings_properties.cpp` rewritten
  against nodes holding `Ik.*` values (defaults, coercion, non-inheritance
  from a parent bone, style-supplied values, computed rest default with and
  without a skin, clone keeps values and pole).
- `scripts/ik_pole_verify.py` and `scripts/ik_effector_orientation_verify.py`
  pass with `set_item_property` addressed to the bone node and the `Ik.*`
  names, with no `add_node_attachment` step.
- `scripts/scene_roundtrip_verify.py`: the rig leg sets `Ik.*` values and a
  pole, and checks them after save + open and after import into a second
  scene; totals match the baseline except the known failures.
- Scene close after a drag with a pole logs no `scene-close leak`.

## Commits

1. **P1** - `erhe::property` weak reference kind, every `Property_type`
   switch site, tests.
2. **P2 to P7, P9** - `Ik` attached properties, bind-pose helper, drag
   integration, Properties action, removal of the attachment, tests and
   the two IK verify scripts. glTF: `ERHE_rig` writer and reader deleted in
   this commit (the class they build is gone).
3. **P8** - `ERHE_node.properties` carriage of `Ik.pole_target` verified
   and fixed for both reload forms, `ERHE_rig` documents removed,
   round-trip script leg.
4. Documents: `ik_settings.md`, `pole_target.md`, `ik_drag_options.md`,
   `interactive_test_pass.md`, `doc/erhe/property_system.md` section 4.19 and
   D-list entry for the weak kind, `doc/erhe/property_inventory.md`
   rewritten to the present state; this plan is then deleted.
