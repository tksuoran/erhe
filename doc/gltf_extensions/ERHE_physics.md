# ERHE_physics

Stability: mostly stable

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
- `properties`: the body attachment's local property values as a name to
  text map (`doc/erhe/property_system.md` D14), the registered properties of
  `Node_physics` by name. The map is the attachment's complete local
  set: on load, a value the `KHR_physics_rigid_bodies` motion / collider
  entries carried (mass, gravity factor, velocities, center of mass, the
  material and filter references) that the map does not name is cleared
  again, so a body that inherits it from its node or a style
  (`doc/erhe/property_system.md` D30) still does after a reload. The
  material and filter references resolve by identity from the KHR
  collider, never from the map's text.

Friction, restitution, linear and angular damping, wind receptivity and
density are not here: they belong to the shared physics material the
body's `KHR_physics_rigid_bodies` collider references (its erhe-only
values ride `ERHE_scene` `physics_materials`), and a body without a
material behaves like one with the material defaults.

Both members are written for every exported body so the round-trip is
exact.

## JSON layout

```json
{
    "motion_mode": "dynamic",
    "properties": {"mass": "2.5", "gravity_factor": "0"}
}
```

- `motion_mode`: one of `static`, `kinematic_non_physical`,
  `kinematic_physical`, `dynamic`.

## Load semantics

`motion_mode` is applied onto the rigid-body create info the
`KHR_physics_rigid_bodies` import produces, before the body is created,
overriding the KHR-derived value; `properties` is applied to the created
attachment as described above.

## Schema

[schema/ERHE_physics.schema.json](schema/ERHE_physics.schema.json)
