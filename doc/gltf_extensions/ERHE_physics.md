# ERHE_physics

## Scope

**Node** extension, on a node carrying a `KHR_physics_rigid_bodies` body
(motion, collider or trigger). Optional (`extensionsUsed` only).

## Dependencies

Meaningful only together with `KHR_physics_rigid_bodies`; it refines the
body that extension describes.

## Overview

Carries erhe rigid-body state `KHR_physics_rigid_bodies` has no carrier
for:

- `motion_mode`: erhe's four motion modes. KHR has a single `isKinematic`
  bool, which cannot distinguish `kinematic_non_physical` from
  `kinematic_physical`, and static bodies have no motion object at all.
- `friction`, `restitution`: per-BODY values. KHR only carries them on
  shared physics materials; erhe bodies have them even with no material
  assigned.
- `linear_damping`, `angular_damping`: no KHR carrier at all.

All fields are written for every exported body so the round-trip is exact.

## JSON layout

```json
{
    "motion_mode": "dynamic",
    "friction": 0.5,
    "restitution": 0.2,
    "linear_damping": 0.05,
    "angular_damping": 0.05
}
```

- `motion_mode`: one of `static`, `kinematic_non_physical`,
  `kinematic_physical`, `dynamic`.

## Load semantics

Applied onto the rigid-body create info the `KHR_physics_rigid_bodies`
import produces, before the body is created; each present field overrides
the KHR-derived value.

## Schema

[schema/ERHE_physics.schema.json](schema/ERHE_physics.schema.json)
