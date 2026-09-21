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
- **Layout** (`src/erhe/scene/erhe_scene/layout.{hpp,cpp}`,
  `layout_system.{hpp,cpp}`): the container values as a group of the node
  keyed on `Layout.type`, with the solve registration in a per-scene
  `erhe::scene::Layout_system` owned by `Scene`
  (`doc/erhe/property_system.md` section 4.13, `doc/erhe/layout.md`). Its
  retirement deleted the `ERHE_layout` glTF extension (D8).
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

| Class | USD counterpart | Verdict | Runtime state | Per node | Saved in | Form |
|-------|-----------------|---------|---------------|----------|----------|------|
| `Node_physics` | `PhysicsRigidBodyAPI`, `PhysicsCollisionAPI`, `PhysicsMassAPI`, `PhysicsMaterialAPI` binding (all applied API schemas) | property | body, create-info mirror, world registration | 1 | `KHR_physics_rigid_bodies`, `ERHE_physics`, UsdPhysics | D1 + D2 |
| `Node_joint` | `UsdPhysicsJoint` and its subclasses: typed prims deriving `UsdGeomImageable`, `physics:body0` / `body1` relationships | **type** | constraint, body pointers | many | `physicsJoints`, UsdPhysics joint prim | D3 |
| `Geometry_graph_mesh` | none of its own; the same shape as `material:binding`, a relationship from the prim to a resource prim | property | controlled mesh, ghost mesh, controlled body, applied revision | 1 | `ERHE_node_graphs` bindings, USD `erhe:scene` block | D1 + D2 |
| `Prefab_instance` | `references` / `payload` list ops and `variants`: prim metadata, neither a prim nor an attribute | prim-held structure | none | many (one per arc) | glTF `externalAsset`, USD arcs | D4 |
| `Brush_placement` | none; three session values about the node | property | none | 1 | nothing | D1, session values (D5) |
| `Grid` | none; editor-settings content that outlives every scene, so it has no scene to be a prim of | item outside the hierarchy | settings-store autosave, matrices | 1 | editor settings | D6 |
| `Frame_controller` (editor and `src/example`) | none; holds no authored value | neither: tool-owned object | input axes, pose | 1 | nothing | D7 |
| `Four_view_link` | none; holds no authored value | neither: part-owned object | back pointer to `Four_view` | 1 | nothing | D7 |

`Joint` is the only new prim type. The typed prims USD has for the other
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

**D3. A joint is a prim.** `Node_joint` becomes `editor::Joint`, a typed
prim deriving `erhe::scene::Imageable` - the level `UsdPhysicsJoint`
derives - so it carries `visible` / `purpose` and no transform of its own.
It sits anywhere in the hierarchy (the importers place it where the file
has it; the Create menu places it below the active item). `Joint.body_0`
and `Joint.body_1` are weak object references (D28) to the two frame nodes:
a frame is the referenced node's world transform, the two-node model the
constraint code, the glTF writer (joint node + `connectedNode`) and the USD
reader (`<joint>_frame0` / `_frame1`) already use. The USD writer derives
`physics:body0` / `body1` and the local frames from the frame nodes as it
does today. `Joint.joint_settings` and `Joint.enable_collision` are own
properties of the class. Any number of joints may name one body. The
constraint lives in the physics system of D2, keyed by the `Joint` prim,
and is rebuilt on a change of one of the four properties.

**D4. Composition arcs are a node-held record list.** `Xformable` gains
`get_composition_arcs() -> std::span<const Composition_arc>` (source path,
prim path, arc kind, variant selections), with a setter used by the prefab
library and the importers, cloned with the node. A read-only computed
property `Prefab_instance.arcs` renders the list as text for the Properties
window and MCP. `instance_structure`, `prefab_library`, the exporters and
`instance_override` read the span; "is a carrier" is `!arcs.empty()`.

**D5. Session values.** A value that is never saved (`Brush_placement.*`)
is registered without the serialize flag; both exporters and the clipboard
skip it through that flag alone.

**D6. A grid is an item that names its node.** `Grid` derives from
`erhe::Item<Item_base, Item_base, Grid>` and is owned by `Grid_tool` in
every case. `Grid.frame_node` (weak object reference) names the node whose
world transform the grid follows; null means the world frame. The grid
subscribes to that node through D7's observer and drops the reference on
`items_removed` and `close_scene` (AGENTS.md "Scene-hosted references in
editor parts").

**D7. Node transform observer.** `Xformable::add_transform_observer(callback)
-> Observer_token` invokes the callback from `handle_transform_update`.
`Frame_controller` (editor and example) and `Four_view`'s camera links
become plain non-item objects owned by `Fly_camera_tool` / `Four_view`,
holding a weak node reference and a token. This is the replacement for
`Node_attachment::handle_node_transform_update`.

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

- **P4. `Brush_placement`** (D5). Suites: Mcp_ brush cases,
  `undo_reference_clearing_smoke_test.py`.
- **P5. `Frame_controller`, `Four_view_link`** (D7), including `src/example`.
  Verified with `get_four_views` focus-link checks and a scripted fly-camera
  gesture (`doc/agents/mcp_ui_driving.md`).
- **P6. `Grid`** (D6).
- **P7. `Geometry_graph_mesh`.** Roundtrip geometry-graph leg.
- **P8. `Node_physics`.** ~35 `get_attachment<Node_physics>` sites move to
  `read_node_physics` / the physics system. Deletes `ERHE_physics`. Suites:
  physics (both backends), usd, `physics_drag_joint_sweep.py` 16/16.
- **P9. `Node_joint` -> `Joint` prim** (D3). New `Item_type` bit, icon,
  Create menu entry and MCP `create_joint`; glTF import places the prim
  below the joint's node. Same suites as P8; creation 21 rebuilt by its
  script.
- **P10. `Prefab_instance`** (D4). Suites: usd, scene, gltf; roundtrip
  references, variants and override legs.
- **P11. Delete the attachment infrastructure.** `Node_attachment`,
  `Node::attach` / `detach` / `get_attachments` / `get_attachment<T>`,
  `Node_attach_operation`, `Scene_commands::remove_attachment`, the
  attachment catalog, `Attachment_kind::api_schema`, MCP `add_node_attachment`
  / `remove_node_attachment`, the `node_tree_expand_attachments` setting and
  the Hierarchy attachment rows and icons, the Properties per-attachment
  sections, `Item_type::node_attachment`. Feature icons in the Hierarchy row
  are drawn from each group's key property.

P4-P7 are independent of each other; P9 follows P8; P11 is last.

## Decision to confirm before P10

D4 (arcs as prim-held structure, not properties): the verdict follows from
arcs being prim metadata in USD, and the property system has no
array-of-records type to hold them otherwise.

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
