# Node attachments as attached properties

Status: proposed

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

## Inventory

One row per `Node_attachment` subclass; "form" names the design entry that
replaces it.

| Class | Values | Runtime state | Per node | Saved in | Form |
|-------|--------|---------------|----------|----------|------|
| `Draw_mode` | 13 entry | card proxy mesh, pruning, cached extent | 1 | USD `GeomModelAPI`, glTF `ERHE_node` | D1 + D2 |
| `erhe::scene::Layout` | 11 entry (+ 8 child hints, attached already) | `Scene::update_layouts` registration, mirrors | 1 | glTF `ERHE_layout` | D1 + D2 |
| `Brush_placement` | brush ref, facet, corner | none | 1 | nothing | D1, session values (D5) |
| `Geometry_graph_mesh` | `graph_mesh` ref | controlled mesh, ghost mesh, controlled body, applied revision | 1 | `ERHE_node_graphs` bindings, USD `erhe:scene` block | D1 + D2 |
| `Node_physics` | 10 entry + `collision_mesh` weak ref | body, create-info mirror, world registration | 1 | `KHR_physics_rigid_bodies`, `ERHE_physics`, UsdPhysics | D1 + D2 |
| `Node_joint` | connected node, settings ref, enable_collision | constraint, body pointers | many | `physicsJoints`, UsdPhysics joint prim | D3 |
| `Prefab_instance` | source path, prim path, arc kind, variant selections | none | many (one per arc) | glTF `externalAsset`, USD arcs | D4 |
| `Grid` | 22 entry | settings-store autosave, matrices | 1 | editor settings | D6 |
| `Frame_controller` (editor and `src/example`) | none | input axes, pose | 1 | nothing | D7 |
| `Four_view_link` | none | back pointer to `Four_view` | 1 | nothing | D7 |

## Design

**D1. Value group with a key property.** A retiring class `X` registers its
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

The key property is registered with `inherits = false`. Every other value
of the group has `visible_when` = "key property effective value is not the
default", so the D12 listing rule shows the group on exactly the nodes that
carry the feature and Add Property offers the key property everywhere else.
One free function per group, `read_<x>(const Node&) -> std::optional<X_data>`,
returns a plain record when the feature is present; every consumer that
today calls `get_attachment<X>` reads that record.

**D2. Per-scene system for runtime state.** Runtime objects that exist
because of a value group (physics body, card proxy mesh, layout solve
registration, graph-controlled meshes) are owned by one system object per
group per scene, held by `Scene_root` (for `Layout`, by `erhe::scene::Scene`).
A system keeps its per-node runtime record in a map keyed by `Node*` and is
driven from three change sites:

1. `Property_metadata::property_changed` on each value of the group: finds
   the node's host, and calls the system's `on_values_changed(node,
   property)`. A key-property change creates or destroys the record.
2. Node registration: `Scene_host::register_node` / `unregister_node` call
   each system's `on_node_registered` / `on_node_unregistered`, which test
   the key property. This is the replacement for
   `Node_attachment::handle_item_host_update`.
3. The derived `Item_flags::active` bit: the node's
   `handle_flag_bits_update` forwards to the systems
   (`Node_physics` and `Draw_mode` use it today).

The system is the only holder of raw pointers into its records, records
hold no `shared_ptr` to the node, and `on_node_unregistered` erases the
record, so a scene close releases everything without a `close_scene`
subscription.

**D3. A joint is a prim.** `Node_joint` becomes `editor::Joint`, an
`Xformable` child prim of the first body's node. Its own transform is the
joint frame in that body's space; `Joint.connected_node` (weak object
reference, D28) names the second frame node. This is the shape the USD
reader already builds (`<joint>_frame0`), it matches the UsdPhysics joint
prim and the glTF joint node, and any number of joints per body needs no
indexing. The constraint lives in the physics system of D2, keyed by the
`Joint` prim.

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
nodes). `register_applied_schema_attachment`,
`link_carrier_attachments_to_target` and the attachment pairing loop are
deleted in the phase that retires their last user (`Draw_mode`, then
`Node_physics`).

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

- **P1. Key-property listing + node systems (D1, D2 infrastructure).**
  `erhe::property`: tests for a group whose rows follow the key property.
  `Scene_host` system hooks; no class retired. Suites: property, item, scene.
- **P2. `Draw_mode`.** Smallest group with runtime state; proves D2 and
  removes the applied-schema attachment registry's first user (D9). Suites:
  usd, scene; DrawModes.usd survey row stays `works`.
- **P3. `Layout`.** `Layout.type = none`; `Scene::update_layouts` iterates
  the layout system's records. Deletes `ERHE_layout` (D8). Suites: scene;
  roundtrip layout leg.
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
- **P9. `Node_joint` -> `Joint` prim** (D3). Same suites as P8; creation 21
  rebuilt by its script.
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

P2-P7 are independent of each other after P1; P9 follows P8; P11 is last.

## Decisions to confirm before the phase that needs them

- D3 (joint as a prim) before P9. The alternative, a fixed number of indexed
  joint groups per node, caps joints per body and was not chosen.
- D4 (arcs as a node-held record list, not properties) before P10: the
  property system has no array-of-records type, and the arc list is
  structure, not an authored value.

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
