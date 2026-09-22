# Node attachments as attached properties

Status: in progress

Per-node data belongs in properties of the node, grouped in the Properties
window by `Property_ui::group`. Every `erhe::scene::Node_attachment` subclass
is retired by the phases below, and the last phase deletes `Node_attachment`
itself. Extends `doc/erhe/property_system.md` (section 4.14 is the first
attached-property user, section 4.19 the first retirement).

## Why

An attachment is a second item the user has to create before the data
exists, a second lifetime to clone and serialize, and a second place the
Properties window draws rows from. Attached properties give the same data a
computed default instead of a creation-time capture, a `visible_when`
instead of an `can_add` gate, Style and undo support for free, and one
serialization carrier. Instance overrides address items by path, and an
attachment has no path; a value on the node is overridable as it stands.

## Done

- **Node_joint -> `Joint` prim** (`src/editor/scene/joint.{hpp,cpp}`,
  `joint_system.{hpp,cpp}`): the joint as a typed prim deriving
  `erhe::scene::Imageable`, naming its two frame nodes with `Joint.body_0` /
  `Joint.body_1` (D3), with the six-dof constraint, the two body pointers and
  the shared-settings observer in a per-scene `Joint_system` held by
  `Scene_root` (`doc/erhe/property_system.md` section 4.17,
  `doc/editor/physics.md`). A joint prim is reported to the system through the
  prim registration hook (`Item_host::register_prim`), not the node hooks: a
  joint carries no transform and is no node. The native carriers - the
  `KHR_physics_rigid_bodies` joint of a node and the UsdPhysics joint prims -
  stay, fed from the scene's `Joint` prims by `build_physics_description()`
  (D8), and each importer places the prim below the prim whose body is the
  joint's first party.
- **Node_physics** (`src/editor/scene/node_physics.{hpp,cpp}`,
  `node_physics_system.{hpp,cpp}`): the rigid body of a node as a value group
  keyed on `Node_physics.motion_mode`, with the collision shape, the body, the
  create-info mirror, the world registration and the material / filter
  observers in a per-scene `Node_physics_system`
  (`doc/erhe/property_system.md` section 4.26, `doc/editor/scene.md`). The
  native carriers - `KHR_physics_rigid_bodies` and the UsdPhysics schemas -
  stay, fed from `read_node_physics()` (D8), and its retirement deleted the
  `ERHE_physics` glTF extension: what the native carriers cannot state - which
  of the two kinematic modes a kinematic body is in
  (`doc/erhe/khr_physics_rigid_bodies_support.md`, "Motion modes in a file") -
  rides `ERHE_node` `properties` and `erhe:Node_physics:<name>`.
  `Motion_mode::e_none` is the new enum value the key property defaults to.
- **Ik** (`src/editor/scene/ik_properties.{hpp,cpp}`): the per-bone IK
  locks, limits, stiffness, rest rotation and pole, group "IK"
  (`doc/erhe/property_system.md` section 4.19,
  `doc/plans/rigging/ik_settings.md`).
- **Draw_mode** (`src/editor/scene/draw_mode_properties.{hpp,cpp}`,
  `src/editor/scene/draw_mode_system.{hpp,cpp}`): `UsdGeomModelAPI` as a value
  group keyed on `Draw_mode.apply_draw_mode`, with the pruning, the cached
  extent and the card proxy in a per-scene `Draw_mode_system`
  (`doc/erhe/property_system.md` section 4.24, `doc/editor/scene.md`). Its
  retirement deleted the applied-schema attachment registry (D9).
- **Geometry_graph_mesh**
  (`src/editor/geometry_graph/geometry_graph_mesh.{hpp,cpp}`,
  `geometry_graph_mesh_system.{hpp,cpp}`): the geometry graph a node sources
  its mesh from as a one-value group keyed on
  `Geometry_graph_mesh.graph_mesh`, with the controlled mesh, ghost mesh,
  rigid body and applied bake revision in a per-scene
  `Geometry_graph_mesh_system` (`doc/erhe/property_system.md` section 4.25,
  `doc/editor/geometry_graph_mesh.md`). The binding's native carriers -
  `ERHE_node_graphs` `node_bindings` and the USD `erhe:scene` block's
  `graph_meshes.bound_prims` - stay, so the value is registered without the
  serialize flag (D5 + D8).
- **Layout** (`src/erhe/scene/erhe_scene/layout.{hpp,cpp}`,
  `layout_system.{hpp,cpp}`): the container values as a group of the node
  keyed on `Layout.type`, with the solve registration in a per-scene
  `erhe::scene::Layout_system` owned by `Scene`
  (`doc/erhe/property_system.md` section 4.13, `doc/erhe/layout.md`). Its
  retirement deleted the `ERHE_layout` glTF extension (D8).
- **Brush_placement** (`src/editor/brushes/brush_placement.{hpp,cpp}`): the
  brush, facet and corner of a placed node as a group of the node keyed on
  `Brush_placement.brush`, registered without the serialize flag (D5), read
  with `read_brush_placement()` (`doc/erhe/property_system.md` section 4.11,
  `doc/editor/brushes.md`). No runtime state, so no node system.
- **`Grid`** (`src/editor/grid/grid.{hpp,cpp}`, `grid_tool.{hpp,cpp}`): an
  item of its own owned by `Grid_tool`, naming the node whose frame it
  follows by `Grid.frame_node` and following that node's transform through
  D7's observer (`doc/editor/grid.md`,
  `doc/erhe/property_system.md` section 4.11). Its retirement deleted the
  `grid` attachment catalog entry, `Scene_commands::attach_new_grid` and the
  MCP `add_node_attachment` `grid` key.
- **`Frame_controller` and `Four_view`'s camera links** (D7): plain objects
  owned by `Fly_camera_tool` / `Four_view`, naming their node by `weak_ptr`
  and following its transform through
  `Xformable::add_transform_observer` (`doc/erhe/scene.md` "Transform
  observers", `doc/editor/scene.md`, `doc/editor/four_view.md`).
- **`Prefab_instance` -> composition arc record** (D4): the arcs a carrier
  prim holds are its own record on `erhe::Typed`
  (`erhe_item/composition_arc.hpp`, `doc/erhe/item.md` "Composition arcs",
  `doc/erhe/property_system.md` section 4.27), read by
  `src/editor/prefabs/instance_structure.{hpp,cpp}` (carrier and seal
  predicates), the prefab library, both exporters, the provenance and MCP
  `get_node_details.composition_arcs`. Its retirement deleted the class, its
  icon entry and the `Item_type::prefab_instance` bit.
- **P1, the D1 and D2 infrastructure.** The key-property rule and its
  `visible_when` (`erhe_property/attached_group.hpp`,
  `doc/erhe/property_system.md` section 4.23) and the node systems and their
  three change sites (`erhe_scene/node_system.hpp`, `Scene::add_node_system`,
  `doc/erhe/scene.md` "Node systems").

## Inventory and verdicts

Rule: a class whose USD counterpart is a typed prim becomes a prim type in
the erhe hierarchy (`doc/erhe/usd_compatibility_design.md` C5); a class whose
USD counterpart is an applied API schema, a relationship or plain attributes
on the prim becomes a value group (D1); prim metadata stays prim-held
structure (D4). Where USD has no counterpart the verdict rests on the
class's own shape, stated in the row.

Every class the inventory listed is retired (see "Done").

`Joint` was the only new prim type. The typed prims USD has for the other
physics and imaging concepts (`Mesh`, `Camera`, the lights, `Scope`,
`PointInstancer`, `Skeleton`) are prim types in erhe already.

## Design

**D1. Value group with a key property.** The rule and the helpers that serve
it stand in `doc/erhe/property_system.md` section 4.23; what follows is the
per-group application of it. A retiring class `X` registers its
values with `register_attached`, owner type `X`, holder type
`erhe::scene::Node`, one UI group, from a holder class with static members
only (`src/editor/scene/<x>_properties.{hpp,cpp}`; `Layout` stays in
`erhe::scene`). Exactly one value of the group is its key property, and the
node carries the feature while the key property's effective value differs
from its default:

| Group | Key property | Default (feature absent) |
|-------|--------------|--------------------------|
| `Draw_mode` | `Draw_mode.apply_draw_mode` | `false` |
| `Layout` | `Layout.type` | `none` (new enum value) |
| `Brush_placement` | `Brush_placement.brush` | null |
| `Geometry_graph_mesh` | `Geometry_graph_mesh.graph_mesh` | null |
| `Node_physics` | `Node_physics.motion_mode` | `none` (new enum value) |

One free function per group, `read_<x>(const Node&) -> std::optional<X_data>`,
returns a plain record when the feature is present; every consumer that
today calls `get_attachment<X>` reads that record.

**D2. Per-scene system for runtime state.** Runtime objects that exist
because of a value group (physics body, card proxy mesh, layout solve
registration, graph-controlled meshes) are owned by one system object per
group per scene, held by `Scene_root` (for `Layout`, by `erhe::scene::Scene`).
The interface, the registration and the three change sites that drive a
system stand in `doc/erhe/scene.md` "Node systems"; each phase writes the
group's system against them. The systems replace
`Node_attachment::handle_item_host_update` and the attachment's own flag-bit
hook.

**D3. A joint is a prim.** Carried out; the standing description is
`doc/erhe/property_system.md` section 4.17 (the four properties),
`doc/editor/physics.md` (the constraint and the drag that reads it) and
`doc/erhe/usd_compatibility.md` (the two file carriers).

**D4. Composition arcs are a prim-held record list.** `erhe::Typed` holds
`std::unique_ptr<Composition_arcs>` (`erhe_item/composition_arc.hpp`), null
on every prim that carries no arc, so the carrier test
`has_composition_arcs()` is a null check and a prim without arcs costs one
pointer. `Composition_arc` is plain data: the source file path, the target
prim path, the arc kind (`reference` | `payload`) and the `variants`
selection the arc carries, one record per arc in authored order;
`get_composition_arcs() -> std::span<const Composition_arc>` is empty when
null, `set_composition_arcs(std::vector<Composition_arc>)` with an empty
vector releases the storage, and the clone constructor deep-copies the list
so a pasted instance is an instance. The record sits on `Typed`, not
`Xformable`, because a USD arc is applied to a prim of any type (`Scope`,
`Material`, a typeless `def`); the `Xform` wrapping the USD importer gives
an arc on a non-`Xformable` prim (`doc/erhe/usd_compatibility_design.md` S1)
stays, and retiring it is future work outside this plan. A read-only
computed string property of `Typed`, `Composition.arcs`, renders the list
for the Properties window and MCP, shown only while the list is non-empty.
`instance_structure`, `prefab_library`, the selection redirect, the
Hierarchy row, the exporters and `erhe::scene::instance_override` read the
prim directly; the `Item_type::prefab_instance` bit is deleted. The record
is held by the prim rather than a table on the scene or the item host: a
prefab template tree has no host, a clipboard clone has none until it is
pasted, and an entry keyed by the item has to outlive the item's scene
membership for redo, which is the set of rules `Content_library`'s
metadata table needed and this record does not.

**D5. Session values.** A value that is never saved (`Brush_placement.*`)
is registered without the serialize flag, and both exporters skip it through
that flag alone. A clone copies every local value whatever its flags, so a
duplicated node keeps such a value - which is what a placement wants, and
what `Draw_mode.source_directory` already relies on.

**D6. A grid is an item that names its node.** Stated by
`doc/editor/grid.md` "Frame".

**D7. Node transform observer.** `Xformable::add_transform_observer(callback)
-> Transform_observer_token`, the replacement for
`Node_attachment::handle_node_transform_update`, is stated by
`doc/erhe/scene.md` "Transform observers". A part that follows one prim's
transform without being an item in the scene is a plain object owned by its
tool, holding a weak node reference and a token.

**D8. Native file carriers stay, fed from the record.** `KHR_physics_rigid_bodies`,
UsdPhysics schemas and `GeomModelAPI` are produced from `read_<x>(node)` and
consumed by writing the node's values. Everything a native carrier cannot
hold rides `ERHE_node.properties` / `erhe:Owner:name`. `ERHE_physics` and
`ERHE_layout` are deleted with their schemas and spec pages; files written
before the phase lose those payloads (the Ik precedent, no migration
reader).

**D9. Prefab interplay.** Values on the instance node reach the template
node through the existing reference layer (`link_instance_to_template` pairs
nodes). `register_applied_schema_attachment` and the applied-schema
attachment registry are gone with `Draw_mode`, their only user;
`link_carrier_values_to_target` pairs a carrier prim with its arc target
through the reference layer when the carrier authors a value of any group.

## Phases

Each phase is one or more commits through
`doc/agents/orchestration_harness.md`, verified headless, and ends with the
phase's class, catalog entry, `Item_type` bit, `Scene_commands` creator and
MCP `add_node_attachment` key deleted and its standing text moved into the
owning document (`doc/erhe/property_system.md` section 4, `doc/editor/*.md`).
Per-phase verification: the listed test suites, `scripts/scene_roundtrip_verify.py`
at its baseline, a scene close with no `scene-close leak` line, and one
headless MCP session that sets the key property, undoes it, and saves and
reopens.

- **P11. Delete the attachment infrastructure.** `Node_attachment`,
  `Node::attach` / `detach` / `get_attachments` / `get_attachment<T>`,
  `Node_attach_operation`, `Scene_commands::remove_attachment`, the
  attachment catalog, `Attachment_kind::api_schema`, MCP `add_node_attachment`
  / `remove_node_attachment`, the `node_tree_expand_attachments` setting and
  the Hierarchy attachment rows and icons, the Properties per-attachment
  sections, `Item_type::node_attachment`. Feature icons in the Hierarchy row
  are drawn from each group's key property.

P11 is last.

## How the remaining phases are worked

P11 remains. It is worked through
`doc/agents/orchestration_harness.md` on `build_vs2026_vulkan_headless`
(tests on), one coder per commit, the orchestrator reviewing and committing.

- A brief lists the files the coder never stages, reverts or deletes:
  `config/editor/desktop_windows.json`, `config/editor/editor_settings.json`,
  `config/editor/desktop_window_imgui_host_imgui.ini`, `prompt_queue.txt`,
  `memory-bank/*`; a layout-sensitive `Mcp_` failure is reported, and the ini
  stays. The coder reads its editor's MCP port from `logs/log.txt`, kills only
  editors it launched, and removes script output it creates
  (`res/editor/graphs/`).
- A phase with more than about twenty consumer sites is split into several
  briefs up front, each ending in a building tree: the value or prim and its
  system with every consumer, then the file carriers, then deletions, scripts
  and documents.
- Baselines a phase must hold: `erhe_property_tests` 147, `erhe_item_tests`
  195, `erhe_scene_tests` 167, `erhe_usd_tests` 405, `erhe_physics_tests` 34
  (Jolt) and 85 (Box3D tree); `scripts/scene_roundtrip_verify.py` total 468 with `ERHE_USDCHECKER` set (466 without: the usdchecker
  section is two checks),
  pass 465, the three failures being the falling-body positions of the P6
  bodies, `textured ... local_property_names` and `references_override: the
  def below a carrier authored nothing`; `ctest -R "Mcp_"` 72 of 73, the
  failure being the layout-dependent
  `property_row_is_addressable_by_its_label` (or, under another layout, the
  gizmo-drag case); `scripts/undo_reference_clearing_smoke_test.py` 59;
  `scripts/geometry_nodes_smoke_test.py` 136;
  `scripts/physics_drag_joint_sweep.py` 16 of 16 on `--jolt` and `--box3d`.
- P11 starts from `src/editor/scene/attachment_types.{hpp,cpp}`: the applied
  API schema catalog is empty and the Add Attachment menu is hidden while it
  is; `Joint` is a child-prim catalog key, which stays.

## Recipe for one value group, as Ik proved it

1. Register the values as D1 states.
2. Give a value that the attachment used to capture at creation a computed
   default (D31) instead, so it is correct on every node without an
   authoring step.
3. Use a weak object reference (D28) for a value naming another node, so it
   is never an ownership edge.
4. Decide `inherits` deliberately. A per-instance value does not inherit,
   and a set shared between nodes is a Style holding those values.
5. Read the values through the group's `read_<x>` function only.
6. Let the values ride the node's `ERHE_node` `properties` map, plus
   `property_node_refs` for a reference naming a node
   (`doc/gltf_extensions/ERHE_node.md`).
7. Delete the class and everything the Phases preamble lists in the same
   commit, and move its tests onto nodes holding the values.
