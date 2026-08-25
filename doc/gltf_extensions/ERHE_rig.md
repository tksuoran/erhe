# ERHE_rig

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
sub-objects. See `doc/ik-settings-requirements.md`.

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
        "flags": ["content", "show_in_ui"]
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
- Absent fields keep the attachment's defaults (forward compatibility);
  unknown fields are ignored.

## Load semantics

Creates an `Ik_settings` attachment on the node. Nodes inside
prefab-instance subtrees are never written (the instance root exports as
an external-asset reference), matching every other per-node pass.

## Schema

[schema/ERHE_rig.schema.json](schema/ERHE_rig.schema.json)
