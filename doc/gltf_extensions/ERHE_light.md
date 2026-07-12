# ERHE_light

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

## JSON layout

```json
{
    "cast_shadow": true,
    "infinite_range": true,
    "flags": ["content", "visible", "show_in_ui"]
}
```

## Schema

[schema/ERHE_light.schema.json](schema/ERHE_light.schema.json)
