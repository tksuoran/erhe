# ERHE_camera

## Scope

**Camera** extension. Optional (`extensionsUsed` only).

## Overview

Carries the FULL `erhe::scene::Projection` plus erhe camera state. The core
glTF camera (yfov/aspect, xmag/ymag) is only an interchange approximation:
it cannot express asymmetric frusta, XR projections, offset orthographic
projections, or erhe's per-type fov fields. Readers that understand this
extension reconstruct the projection from it and treat the core camera as
fallback only.

## JSON layout

```json
{
    "projection_type": "perspective_vertical",
    "z_near": 0.03,
    "z_far": 80,
    "fov_x": 0,
    "fov_y": 0.6108652,
    "fov_left": -0.5,
    "fov_right": 0.5,
    "fov_up": 0.5,
    "fov_down": -0.5,
    "ortho_left": -0.5,
    "ortho_width": 1,
    "ortho_bottom": -0.5,
    "ortho_height": 1,
    "frustum_left": -0.5,
    "frustum_right": 0.5,
    "frustum_bottom": -0.5,
    "frustum_top": 0.5,
    "exposure": 1,
    "shadow_range": 22,
    "flags": ["content", "show_in_ui"]
}
```

- `projection_type`: one of `other`, `perspective_horizontal`,
  `perspective_vertical`, `perspective`, `perspective_xr`,
  `orthogonal_horizontal`, `orthogonal_vertical`, `orthogonal`,
  `orthogonal_rectangle`, `generic_frustum`.
- All projection fields are always written (the active subset depends on
  `projection_type`); angles are radians, distances scene units.
- `exposure`, `shadow_range`: erhe camera parameters.
- `flags`: the camera attachment's persistent Item flags
  (see [flags.md](flags.md)).

## Schema

[schema/ERHE_camera.schema.json](schema/ERHE_camera.schema.json)
