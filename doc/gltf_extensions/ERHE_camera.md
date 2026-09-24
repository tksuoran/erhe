# ERHE_camera

Stability: mostly stable

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
    "perspective_z_near": 0.03,
    "perspective_z_far": 80,
    "orthographic_z_near": -256,
    "orthographic_z_far": 256,
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
    "infinite_z_far": false,
    "exposure": 1,
    "shadow_range": 22,
    "flags": ["content", "show_in_ui"],
    "properties": {"fov_y": "0.6108652", "perspective_z_far": "80"}
}
```

- `projection_type`: one of `other`, `perspective_horizontal`,
  `perspective_vertical`, `perspective`, `perspective_xr`,
  `orthographic_horizontal`, `orthographic_vertical`, `orthographic`,
  `orthographic_rectangle`, `generic_frustum`.
- All projection fields are always written (the active subset depends on
  `projection_type`); angles are radians, distances scene units.
- `perspective_z_near`, `perspective_z_far`: the clip range of the
  perspective types, `perspective_xr` and `generic_frustum` (distances in
  front of the camera, `perspective_z_near > 0`).
- `orthographic_z_near`, `orthographic_z_far`: the clip range of the
  orthographic types, signed view-axis distances: an orthographic near plane
  may lie behind the camera (negative `orthographic_z_near`).
- `infinite_z_far` (boolean, default `false`): the perspective projection
  types put their far plane at infinity, which is what an absent
  `camera.perspective.zfar` means in core glTF. `perspective_z_far` stays a finite,
  meaningful number while this is set - it is the depth hint the rest of the
  editor works from (shadow range fitting, gizmo distances, the properties
  slider) - and only the projection matrix is unbounded. The exporter omits
  the core camera's `zfar` when this is set, so a reader without this
  extension also sees an infinite camera. Ignored for the orthographic
  types, where glTF requires a finite `zfar`.
- `exposure`, `shadow_range`: erhe camera parameters.
- `properties`: the camera's local property values as a name to text map
  (`doc/erhe/property_system.md` D14), the registered properties of `Camera`
  by name. The map is the camera's complete local set: on load, a
  projection field of this extension or of the core camera that the map
  does not name is cleared again, so a camera that inherits it from its
  node (`doc/erhe/property_system.md` D30) still does after a reload.
- `flags`: the camera prim's persistent Item flags
  (see [flags.md](flags.md)).

## Schema

[schema/ERHE_camera.schema.json](schema/ERHE_camera.schema.json)
