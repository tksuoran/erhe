# ERHE_physics_joint

Stability: mostly stable

## Scope

**Node** extension, on the node carrying a `KHR_physics_rigid_bodies`
`joint`. Optional (`extensionsUsed` only).

## Dependencies

Meaningful only together with `KHR_physics_rigid_bodies`. It rides the NODE
because the joint entry of that extension is a nameless member of the node's
rigid body object with no room for erhe state.

## Overview

Carries the state of the erhe `Joint` prim (`src/editor/scene/joint.hpp`)
that the `KHR_physics_rigid_bodies` joint entry cannot express:

- `name`: the Joint prim's name.
- `flags`: the Joint prim's persistent Item flags (see [flags.md](flags.md)).
- `properties`: the Joint prim's local property values as a name to text
  map (`doc/erhe/property_system.md` D14), the item-level ones such as
  `active` and `visible` included. `"active": "false"` keeps the joint out
  of the simulation (no constraint is built). The values the KHR joint entry
  states - the two frame nodes, the joint settings and `enableCollision` -
  are not repeated here; the KHR entry is authoritative for them.

On load the flags and property values are applied to the Joint prim the
importer creates for the node's KHR joint, before the prim enters the scene.

## JSON layout

```json
{
    "name": "Elbow hinge",
    "flags": ["content", "visible", "show_in_ui"],
    "properties": {"active": "false"}
}
```

## Schema

[schema/ERHE_physics_joint.schema.json](schema/ERHE_physics_joint.schema.json)
