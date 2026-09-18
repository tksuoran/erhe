# ERHE_light

Stability: mostly stable

## Scope

**Node** extension, on the node carrying a `KHR_lights_punctual` light
reference. Optional (`extensionsUsed` only).

## Dependencies

Meaningful only together with `KHR_lights_punctual`. It rides the NODE
rather than the light entry because erhe lights are 1:1 with their node
and per-light-entry hooks would need extra parser surface.

## Overview

Carries erhe light state `KHR_lights_punctual` cannot express:

- `cast_shadow`: whether the light casts shadows (silently lost before).
- `infinite_range`: erhe uses range 0 to mean infinite while
  `KHR_lights_punctual` omits `range` for infinite and readers commonly
  default a missing range; this explicit marker resolves the asymmetry.
- `flags`: the light attachment's persistent Item flags
  (see [flags.md](flags.md)).
- `properties`: the light's local property values as a name to text map
  (`doc/erhe/property_system.md` D14), the registered properties of `Light`
  by name. The map is the light's complete local set: on load, a value
  the `KHR_lights_punctual` entry carried (color, intensity, range, spot
  angles) that the map does not name is cleared again, so a light that
  inherits it from its node (`doc/erhe/property_system.md` D30) still does
  after a reload.

## JSON layout

```json
{
    "cast_shadow": true,
    "infinite_range": true,
    "flags": ["content", "visible", "show_in_ui"],
    "properties": {"intensity": "300", "temperature": "3200"}
}
```

## Schema

[schema/ERHE_light.schema.json](schema/ERHE_light.schema.json)
