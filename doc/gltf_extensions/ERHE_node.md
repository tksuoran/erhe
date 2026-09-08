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
- `overrides` (optional): the sparse overrides the prefab instance the
  node carries holds (`doc/usd-compatibility-plan.md` X2). A carrier node
  is written with `externalAssetIndex` and no children - the referenced
  file supplies the instance content - but a value the user changed inside
  the instance belongs to this file, and this is where it travels. One
  entry per item inside the instance that holds any; what an override is
  is stated once, in `src/erhe/scene/erhe_scene/instance_override.hpp`.
  - `path`: the item's path below the arc's target clone, in the erhe
    path form (`Hierarchy::get_path()`, names separated by `/`). The empty
    path is the target clone itself.
  - `properties`: the item's own local values, in the same name to text
    form `properties` above uses; `active` rides it like any other.
  - `transform` (optional): the item's local transform as 16 floats in
    erhe's column-major order, written only when it differs from the
    template counterpart's.
  - `material` (optional): the path of the material item the entry binds,
    in the same path form `path` uses, written only when the item's mesh
    binds a material the template counterpart's mesh does not. A binding
    that covers one group of facets rather than the whole mesh is an entry
    of its own whose `path` ends in the name of the group.

  The reader records them and the editor applies them to the fresh clones
  while they are still writable, which is what lets a sealed glTF instance
  receive them; a template reload re-reads the overrides off the clones
  and puts them back, so an edit inside an instance survives it.
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
    "overrides": [
        {"path": "arm", "properties": {"visible": "false"}, "transform": [1,0,0,0, 0,1,0,0, 0,0,1,0, 1,2,3,1]},
        {"path": "arm/plate", "properties": {"active": "false"}},
        {"path": "arm/plate/front", "properties": {}, "material": "Materials/Copper"}
    ],
    "mesh_flags": ["content", "visible", "shadow_cast", "id", "show_in_ui"]
}
```

## Load semantics

The listed set is applied exactly: listed persistent flags are enabled,
unlisted persistent flags are disabled, unknown names are ignored. This
replaces the old fixed default flag sets the loader used to assign.

## Schema

[schema/ERHE_node.schema.json](schema/ERHE_node.schema.json)
