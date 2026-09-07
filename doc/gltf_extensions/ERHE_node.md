# ERHE_node

## Scope

**Node** extension. Optional (`extensionsUsed` only).

## Overview

Carries the erhe Item state of a node that core glTF cannot express:

- `flags`: the node's persistent Item flags as a name list (see
  [flags.md](flags.md)). Migrates the legacy `erhe_flags` node extras
  (which carried only `exclude_from_prefab`); the extras remain parsed for
  older files, this extension wins when both are present.
- `properties`: the node's local property values as a name to text map
  (`doc/property-system.md` D14): the registered properties of `Node`
  by name, attached properties (D3) by their qualified
  `<owner>.<name>`, such as the `Layout.*` per-child layout hints
  (`ERHE_layout` names the layout itself), and the attachment-class
  values the node holds for the attachments below it (D30, `Light.color`)
  by the same qualified form. Enumerations travel as their labels; an
  object reference travels as the referenced item's name and is resolved
  in the scene once the file's items exist (`doc/property-system.md`
  D28), so a node-held `Node_physics.physics_material` names a physics
  material the same file's `KHR_physics_rigid_bodies` array defines. The
  item-level properties of every node travel here by their plain names:
  `visible`, `purpose` and `active`. `active`
  (`doc/usd-compatibility-plan.md` X2) is USD's prim `active` metadatum -
  `"active": "false"` takes the node and its whole subtree out of
  rendering, picking, simulation and every consumer that walks content,
  and dims the row in the item tree. The subtree effect is derived from
  the values of the node and its ancestors, so it is never written; only
  the node's own value is.
- `style` (optional): the name of the style item the node uses
  (`doc/style-library.md` D4), one of the scene's `ERHE_scene` `styles`;
  emitted only when the node has a style. Assigned on load once the
  styles exist; an unknown name is logged and assigns nothing.
- `prim_class` (optional): the erhe prim class of the node
  (`doc/usd-compatibility-plan.md` C5), `"Scope"` or `"Typed"` - the two
  classes that carry no transform. A node without the field is an `Xform`,
  the class every glTF node has. The reader creates the named class and
  reads no transform for it; the writer gives such a node the identity
  transform and composes the transform that reached the prim into its
  children.
- `prim_type_name` (optional): the USD `typeName` token a `Typed` prim
  carries, empty for a typeless `def`. Written for `"prim_class":
  "Typed"` only - a `Scope` names its own token.
- `mesh_flags` (optional): the persistent Item flags of the node's mesh
  attachment. They ride the node because core glTF meshes have no erhe
  payload of their own and erhe `Mesh` attachments are per node while glTF
  meshes are shareable.

## JSON layout

```json
{
    "flags": ["content", "visible", "show_in_ui"],
    "properties": {"active": "false", "Layout.align_y": "Stretch", "Light.color": "1 0.9 0.8"},
    "prim_class": "Typed",
    "prim_type_name": "Cube",
    "style": "Warm lights",
    "mesh_flags": ["content", "visible", "shadow_cast", "id", "show_in_ui"]
}
```

## Load semantics

The listed set is applied exactly: listed persistent flags are enabled,
unlisted persistent flags are disabled, unknown names are ignored. This
replaces the old fixed default flag sets the loader used to assign.

## Schema

[schema/ERHE_node.schema.json](schema/ERHE_node.schema.json)
