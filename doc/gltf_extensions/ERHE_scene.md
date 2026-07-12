# ERHE_scene

## Scope

**Scene** extension. Optional (`extensionsUsed` only).

## Overview

Marks a file as an erhe-authored scene and carries per-scene editor state.
Its presence in `extensionsUsed` is the discriminator between "open as a
full erhe scene" and "import as an asset": scene saves always write it,
plain interchange exports never do.

- `ambient_light`: scene ambient light color (RGBA; erhe #237).
- `enable_physics`: whether the scene owns a physics world.
- `settings` (optional): per-scene overrides of editor-global settings
  (erhe #239) as a `Scene_settings` JSON object (the erhe_codegen schema in
  `src/editor/scene/definitions/scene_settings.py`; each absent / null
  field means "use the editor-global default"). Omitted entirely when no
  override is engaged.

## JSON layout

```json
{
    "ambient_light": [0.1, 0.1, 0.12, 1],
    "enable_physics": true,
    "settings": {
        "post_processing": false,
        "clear_color": [0, 0, 0, 1]
    }
}
```

## Load semantics

Consumed by the Open-Scene path when constructing the `Scene_root`.
Importing a file that carries `ERHE_scene` as an ASSET into an existing
scene deliberately ignores it - an import must not change the target
scene's settings.

## Schema

[schema/ERHE_scene.schema.json](schema/ERHE_scene.schema.json)
(the `settings` object is validated by the erhe_codegen `Scene_settings`
schema, not duplicated here)
