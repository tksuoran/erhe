# ERHE_rig

Stability: experimental

## Scope

**Node** extension. Optional (`extensionsUsed` only).

## Overview

Carries the node's per-bone rig data. Currently one optional sub-object,
`ik`: the node's `Ik_settings` attachment (per-axis IK DOF locks, joint
rotation limits, stiffness, and the rest orientation defining the limits'
zero). Written for every node carrying the attachment, all-default values
included - the attachment's presence is itself user intent. The sub-object
carries the attachment's persistent Item flags (see [flags.md](flags.md)).
Future rig data (pole targets, per-chain settings) will ride as sibling
sub-objects. See `doc/plans/rigging/ik_settings.md`.

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

Creates an `Ik_settings` attachment on the node. Nodes inside
prefab-instance subtrees are never written (the instance root exports as
an external-asset reference), matching every other per-node pass.

## Schema

[schema/ERHE_rig.schema.json](schema/ERHE_rig.schema.json)
