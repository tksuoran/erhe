# ERHE_rig

Stability: experimental

## Scope

**Node** extension. Optional (`extensionsUsed` only).

## Overview

Carries the node's per-bone rig data. Currently one optional sub-object,
`ik`: the node's `Ik_settings` attachment (per-axis IK DOF locks, joint
rotation limits, stiffness, the rest orientation defining the limits' zero,
and the pole target and pole angle steering the bend of a chain the node
governs). Written for every node carrying the attachment, all-default values
included - the attachment's presence is itself user intent. The sub-object
carries the attachment's persistent Item flags (see [flags.md](flags.md)).
Future rig data (per-chain settings) will ride as sibling sub-objects. See
`doc/plans/rigging/ik_settings.md` and `doc/plans/rigging/pole_target.md`.

General transform channel locks (`lock_translation_x` ... `lock_scale_z`)
are NOT part of this extension: they are Item flags and ride
`ERHE_node.flags` like every other persistent flag.

## JSON layout

```json
{
    "ik": {
        "name": "IK settings",
        "lock": [false, false, true],
        "limit": [true, false, false],
        "min": [-2.6179938, -3.1415927, -3.1415927],
        "max": [0.0, 3.1415927, 3.1415927],
        "stiffness": [0.0, 0.0, 0.0],
        "rest_rotation": [0.0, 0.0, 0.0, 1.0],
        "pole_target": 12,
        "pole_angle": 1.5707963,
        "flags": ["content", "show_in_ui"],
        "properties": {
            "lock_z": "true",
            "limit_min": "-2.6179938 -3.1415927 -3.1415927",
            "rest_rotation": "0 0 0 1"
        }
    }
}
```

- `lock` / `limit`: per local axis (x, y, z). `lock` wins over `limit`
  on the same axis.
- `min` / `max`: limit angles in radians, relative to `rest_rotation`;
  `min[i]` must be in `[-pi, 0]`, `max[i]` in `[0, pi]` (the rest angle 0
  is always legal). Out-of-range values are clamped on import.
- `stiffness`: per-axis, `[0, 0.99]` (clamped on import). Serialized for
  schema stability; not yet used by the solver.
- `rest_rotation`: glTF-order unit quaternion `[x, y, z, w]` - the
  reference orientation whose deviation the limits bound (the limited
  quantity is `inverse(rest_rotation) * parent_from_node_rotation`,
  enforced via swing/twist decomposition).
- `pole_target`: the **glTF node index** of the node whose direction the bend
  of a chain this attachment governs is aimed at - the same way
  `KHR_physics_rigid_bodies` names a joint's `connectedNode`, so the pole is
  found by index in the file's own node table and survives renames, import
  roots and duplicate names. Absent means no pole; a pole outside the
  exported asset is written as no pole. A value that is not a node index of
  the file loads as no pole, with a warning; every other field is unaffected.
- `pole_angle`: the swivel offset about the chain's root-to-effector line, in
  radians, right-handed about the root-to-effector direction. Absent means
  `0`. A non-finite value is ignored with a warning.
- `properties`: the attachment's local property values by name
  (`doc/erhe/property_system.md` D23 and section 4.19), the attachment's
  complete local set: the explicit fields above are the effective values
  for readers without the property system, and a field the map does not
  name holds no local value after the load, so a value inherited from the
  node chain (`ERHE_node` `properties`, `Ik_settings.limit_x`) or a style
  inherits again. `rest_rotation` does not inherit and is normally local.
  A file without the map loads every explicit field as a local value.
- Absent fields keep the attachment's defaults (forward compatibility);
  unknown fields are ignored.

## Load semantics

Creates an `Ik_settings` attachment on the node. A `pole_target` resolves
through the parse's own node table, so it always lands on the imported copy
of the pole, whether the file is loaded as a scene or imported under an
import root. Nodes inside
prefab-instance subtrees are never written (the instance root exports as
an external-asset reference), matching every other per-node pass.

## Schema

[schema/ERHE_rig.schema.json](schema/ERHE_rig.schema.json)
