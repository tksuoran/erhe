# Node attachments as attached properties

Status: proposed

Per-node data belongs in properties of the node, grouped in the Properties
window by `Property_ui::group`. Node attachments are retired one at a time,
each by the recipe below. Extends `doc/erhe/property_system.md` (section
4.14 is the first attached-property user, section 4.19 the first retirement).

## Why

An attachment is a second item the user has to create before the data
exists, a second lifetime to clone and serialize, and a second place the
Properties window draws rows from. Attached properties give the same data a
computed default instead of a creation-time capture, a `visible_when`
instead of an `can_add` gate, Style and undo support for free, and one
serialization carrier.

## Done

- **Ik** (`src/editor/scene/ik_properties.{hpp,cpp}`): the per-bone IK
  locks, limits, stiffness, rest rotation and pole, group "IK"
  (`doc/erhe/property_system.md` section 4.19,
  `doc/plans/rigging/ik_settings.md`).

## Candidates

The add-attachment catalog (`src/editor/scene/attachment_types.cpp`) holds,
in its own order: `rigid_body` (`Node_physics`), `joint` (`Node_joint`),
`layout` (`erhe::scene::Layout`), `grid` (`Grid`), `frame_controller`
(`Frame_controller`) and `draw_mode` (`Draw_mode`). Each is a candidate; the
ones whose value set is small and whose behavior is a per-frame read of that
set - `draw_mode`, `grid`, `layout` - convert most directly. An attachment
that owns runtime state beyond its values (`Node_physics` owns a body in the
physics world, `Node_joint` a constraint) needs that state driven from a
property change hook before it can be converted, and `Layout` also carries
per-child hints that are already attached properties (section 4.14).

## Recipe, as Ik proved it

1. Register the values with `register_attached`, owner type the retiring
   class's name, holder type `erhe::scene::Node`, one UI group, from a
   holder class with static members only.
2. Give a value that the attachment used to capture at creation a computed
   default (D31) instead, so it is correct on every node without an
   authoring step.
3. Give every value the same `visible_when`, so the D12 listing rule offers
   the rows on exactly the nodes the data means something for; this replaces
   the catalog entry's `can_add` gate.
4. Use a weak object reference (D28) for a value naming another node, so it
   is never an ownership edge.
5. Decide `inherits` deliberately. A per-instance value does not inherit,
   and a set shared between nodes is a Style holding those values.
6. Read them through one free function returning a plain record, and keep
   the solver or runtime consumer reading that record.
7. Let the values ride the node's `ERHE_node` `properties` map, plus
   `property_node_refs` for a reference naming a node
   (`doc/gltf_extensions/ERHE_node.md`); delete the owner's own glTF
   extension, its schema and its spec page.
8. Delete the class, its catalog entry, its `Item_type` bit, its
   `Scene_commands` creator and its MCP `add_node_attachment` value in the
   same commit, and move its tests onto nodes holding the values.
