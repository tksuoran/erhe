#!/usr/bin/env python3
"""Creation 25 - Shadow Test Rooms.

A test ASSET for the shadow paths: the stations of
doc/erhe/shadows.md "Shadow verification". Every station is its own scene,
built only from boxes (a plane is a thin box), with plain white materials
(base colour 1, roughness 1, metallic 0), scene ambient 0 and exactly one
shadow-casting light named "Shadow Light", so the ground truth is analytic:
a receiver point is lit exactly when its segment to the light misses every
box. The light type and pose are set per measurement (apply_light_pose()),
so one station serves directional, spot and point lights.

The module is the DATA SOURCE of scripts/shadow_verify.py, which imports it.
Importing it has no side effects. Everything is plain Python data in
STATION-LOCAL coordinates (the station root node sits at the origin in the
saved asset; floor tops are at y = 0):

  STATIONS[name] = {
      "description":   what the station measures,
      "requirements":  plan requirements it exercises (R1..R7),
      "asset":         the scene asset (File > Load Scene / load_scene),
      "root":          name of the station's single root node (R7 moves it),
      "light":         name of the station's one shadow-casting light,
      "boxes":         [box, ...]        every solid of the station,
      "lights":        {type: pose}      default light pose per light type,
      "views":         [view, ...]       measurement views,
      "texel_mm":      {type: size}      shadow texel world size at the
                                         default pose, 2048 resolution,
      ...station extras (contact_lines, walls)
  }

  box  = {"name", "role": "receiver" | "caster" | "wall", "center": [x, y, z],
          "half_extents": [hx, hy, hz], "rotation_xyzw": [x, y, z, w],
          "casts_shadow": bool}
         The box is center + rotation * (u * half_extents), u in [-1, 1]^3.
         role: receiver = the surface measured for R1 (floors, tiles);
         caster = an occluder that is also measured where lit; wall = an
         occluder whose far side is the region under test (R4).
         casts_shadow False = the box is not in the shadow map (its node's
         shadow_cast property is off); it still receives.
  pose = {"type": "directional" | "spot" | "point", "position": [x, y, z],
          "direction": [x, y, z] (unit, directional / spot; the way the light
          shines), "rotation_xyzw" (the light node rotation: lights shine
          down their -Z), "range", "intensity", and for spot
          "outer_spot_angle_deg" / "inner_spot_angle_deg" (FULL cone angles,
          erhe's Light::outer_spot_angle convention: the spot shadow frustum
          fov equals the outer angle)}
         A directional pose's position only places the node; the shadow fit
         follows the view camera (doc/erhe/shadows.md).
  view = {"name", "eye", "target", "up", "fov_y_deg", "width", "height",
          "near", "far", "shadow_range"}
         render_camera(view, root_offset) turns it into render_scene_image's
         `camera` argument. shadow_range covers the farthest station point
         seen from the eye, so the capped directional fit keeps every
         caster and receiver of the station.

pose_sweep(name, light_type, sweep="full" | "short") is the
pose sweep of a station: head_on_floor moves the light height over 200 steps
of 1 mm and 200 steps spread over 0.5 .. 10 m, then offsets it laterally over
a 5 x 5 grid (425 poses; directional: the 5 x 5 grid only, see
_sweep_head_on_floor); the other stations use 25 poses around the default
one. "short" keeps 5 of them, evenly spread over the full list.

Station-local -> world: every station has ONE root node; R7 translates it.
to_world(point, root_offset), box_to_world(), pose_to_world() and
render_camera() apply that translation (the root is never rotated).

The `cornell` station is res/editor/assets/gi_test_rooms/gi_cornell.glb as
is (creation_24_gi_test_rooms.py builds it; its light is "Cornell Light" and
its root "GI cornell" does not hold the light, so R7 is not run on it).

CLI: --station <name> | all. Default: build each station and render every
view (PNG, plus a shader_debug 30 shadow visibility PNG) with
render_scene_image into logs/creations/shadow_test_rooms_<station>_*.png.
--save-assets: build and save each station to
res/editor/assets/shadow_test_rooms/shadow_<station>.glb (cornell has no
asset of its own). Run it against a running editor with --reuse (a Debug
headless editor outlasts create_scene's first request while it starts up).
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))

from common import (  # noqa: E402
    Creation,
    standard_args,
    fail_soft,
    look_at_quaternion,
    axis_angle_quaternion,
    quat_rotate,
    v_add,
    v_sub,
    v_scale,
    v_norm,
    v_length,
)

TITLE     = "Shadow Test Rooms"
BASE      = "logs/creations/shadow_test_rooms"
ASSET_DIR = "res/editor/assets/shadow_test_rooms"

LIGHT_NAME = "Shadow Light"
WHITE      = [1.0, 1.0, 1.0]
IDENTITY   = [0.0, 0.0, 0.0, 1.0]

# Shadow map resolution the texel_mm comments and values are stated for.
SHADOW_RESOLUTION = 2048

# Floor slab thickness (floor tops at y = 0).
FLOOR_T = 0.1

# Default measurement image size.
IMAGE_W = 768
IMAGE_H = 768

# Default light intensities per type (visibility, not brightness, is what
# the shadow gates measure; these keep the PNG views readable).
INTENSITY = {"directional": 3.0, "spot": 20.0, "point": 20.0}


# --- geometry helpers ------------------------------------------------------------

def box(name, role, lo=None, hi=None, center=None, size=None, rotation_xyzw=None, casts_shadow=True):
    """A box from its [lo, hi] corners (axis aligned) or from center + size
    (+ rotation)."""
    if lo is not None:
        center = [0.5 * (lo[i] + hi[i]) for i in range(3)]
        size   = [hi[i] - lo[i] for i in range(3)]
    return {
        "name":          name,
        "role":          role,
        "center":        [float(v) for v in center],
        "half_extents":  [0.5 * float(v) for v in size],
        "rotation_xyzw": list(rotation_xyzw or IDENTITY),
        "casts_shadow":  bool(casts_shadow),
    }


def floor(name, half_x, half_z, casts_shadow=True):
    return box(name, "receiver", lo=[-half_x, -FLOOR_T, -half_z], hi=[half_x, 0.0, half_z],
               casts_shadow=casts_shadow)


def resting(name, role, x, z, size):
    """An axis-aligned box of `size` resting on the floor (bottom at y = 0)
    centred at (x, z)."""
    return box(name, role, lo=[x - 0.5 * size[0], 0.0, z - 0.5 * size[2]],
               hi=[x + 0.5 * size[0], size[1], z + 0.5 * size[2]])


def footprint_edges(b):
    """The four floor-contact edges ([p0, p1] segments at y = 0) of an axis
    aligned box resting on the floor."""
    cx, _, cz = b["center"]
    hx, _, hz = b["half_extents"]
    corners = [[cx - hx, 0.0, cz - hz], [cx + hx, 0.0, cz - hz], [cx + hx, 0.0, cz + hz], [cx - hx, 0.0, cz + hz]]
    return [[corners[i], corners[(i + 1) % 4]] for i in range(4)]


def light_rotation(position, direction):
    """Light node rotation for a light at `position` shining along
    `direction` (lights shine down their -Z, like the camera)."""
    up = (0.0, 1.0, 0.0)
    if abs(v_norm(direction)[1]) > 0.99:
        up = (0.0, 0.0, -1.0)
    return look_at_quaternion(position, v_add(position, direction), up)


def pose(light_type, position, direction=None, outer_deg=None, inner_deg=None, light_range=20.0):
    entry = {"type": light_type, "position": [float(v) for v in position],
             "range": float(light_range), "intensity": INTENSITY[light_type]}
    if light_type in ("directional", "spot"):
        d = v_norm(direction)
        entry["direction"] = d
        entry["rotation_xyzw"] = light_rotation(entry["position"], d)
    else:
        entry["rotation_xyzw"] = list(IDENTITY)
    if light_type == "spot":
        entry["outer_spot_angle_deg"] = float(outer_deg)
        entry["inner_spot_angle_deg"] = float(inner_deg)
    return entry


def poses_from(position, aim, outer_deg, inner_deg, light_range=20.0):
    """Default poses of all three types from one light position: spot aimed
    at `aim`, point at the same position, directional shining along the
    same direction."""
    direction = v_sub(aim, position)
    return {
        "directional": pose("directional", position, direction, light_range=0.0),
        "spot":        pose("spot", position, direction, outer_deg, inner_deg, light_range),
        "point":       pose("point", position, light_range=light_range),
    }


def view(name, eye, target, fov_y_deg, up=(0.0, 1.0, 0.0), width=IMAGE_W, height=IMAGE_H,
         shadow_range=None, far=None, near=0.02, boxes=None):
    """A measurement view. shadow_range / far default to the distance from
    the eye to the farthest corner of the station's boxes (+10 %)."""
    if (shadow_range is None) or (far is None):
        reach = 0.0
        for b in boxes or []:
            for corner in box_corners(b):
                reach = max(reach, v_length(v_sub(corner, eye)))
        reach = math.ceil(1.1 * reach)
        shadow_range = shadow_range if shadow_range is not None else reach
        far = far if far is not None else reach
    return {"name": name, "eye": [float(v) for v in eye], "target": [float(v) for v in target],
            "up": [float(v) for v in up], "fov_y_deg": float(fov_y_deg), "width": int(width),
            "height": int(height), "near": float(near), "far": float(far),
            "shadow_range": float(shadow_range)}


def box_corners(b):
    corners = []
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            for sz in (-1.0, 1.0):
                local = [sx * b["half_extents"][0], sy * b["half_extents"][1], sz * b["half_extents"][2]]
                corners.append(v_add(b["center"], quat_rotate(b["rotation_xyzw"], local)))
    return corners


def spot_texel_mm(distance, outer_deg):
    """World size of one spot shadow texel at `distance` along the axis."""
    return 1000.0 * 2.0 * distance * math.tan(0.5 * math.radians(outer_deg)) / SHADOW_RESOLUTION


def point_texel_mm(distance):
    """World size of one cube face texel at `distance` (90 degree faces)."""
    return 1000.0 * 2.0 * distance / SHADOW_RESOLUTION


def ortho_texel_mm(extent):
    """Directional texel for a fit `extent` metres wide (fit_to_casters over
    the station's casters; the fit follows the view camera)."""
    return 1000.0 * extent / SHADOW_RESOLUTION


# --- stations ----------------------------------------------------------------------
#
# Texel sizes in the comments are at SHADOW_RESOLUTION 2048 and the default
# pose (STATIONS[...]["texel_mm"] holds the computed values). The thinnest
# part of every station (1 cm plate / walls, 2 cm tiles and plates) spans at
# least two texels at every default pose.

# head_on_floor ---------------------------------------------------------------------
# 12 x 12 m floor, light 3 m straight above its centre: the lit floor faces
# the light head-on (dz_dUV = 0), the head-on tie (shadows.md "Minimum bias").
# Texels: spot (90 deg) 2.9 mm at the floor centre; point 2.9 mm; directional
# ~5.9 mm (fit to the 12 m floor).
HEAD_ON_HEIGHT = 3.0
HEAD_ON_BOXES = [floor("Floor", 6.0, 6.0)]
HEAD_ON_LIGHTS = poses_from([0.0, HEAD_ON_HEIGHT, 0.0], [0.0, 0.0, 0.0], 90.0, 72.0)


# grazing_fan -------------------------------------------------------------------------
# Eight 0.5 x 0.5 m tiles, 2 cm thick, 0.6 m above a 5 x 5 m floor, whose
# normals make 0, 15, 30, 45, 60, 75, 85 and 88 degrees with the light axis
# (straight down). The 0 degree tile sits under the light; the others stand
# on a 1.1 m ring around it, tilted about their radial axis (the normal leans
# tangentially), so for the point / spot default 8 m above the centre the
# angle to the light is acos(cos(theta) * cos(phi)) with the ring's
# off-axis angle phi = 7.8 deg - within 0.03 deg of theta from 60 deg up.
# Texels: spot (24 deg) 1.7 mm; point 7.8 mm; directional ~2.4 mm.
GRAZING_ANGLES  = [0.0, 15.0, 30.0, 45.0, 60.0, 75.0, 85.0, 88.0]
GRAZING_RING    = 1.1
GRAZING_TILE    = 0.5
GRAZING_TILE_T  = 0.02
GRAZING_Y       = 0.6
GRAZING_HEIGHT  = 8.0


def _grazing_boxes():
    boxes = [floor("Floor", 2.5, 2.5)]
    ring = GRAZING_ANGLES[1:]
    for i, theta in enumerate(GRAZING_ANGLES):
        if i == 0:
            center = [0.0, GRAZING_Y, 0.0]
            rotation = list(IDENTITY)
        else:
            azimuth = 2.0 * math.pi * (i - 1) / len(ring)
            radial = [math.cos(azimuth), 0.0, math.sin(azimuth)]
            center = [GRAZING_RING * radial[0], GRAZING_Y, GRAZING_RING * radial[2]]
            rotation = axis_angle_quaternion(radial, math.radians(theta))
        boxes.append(box(f"Tile {theta:g}", "receiver", center=center,
                         size=[GRAZING_TILE, GRAZING_TILE_T, GRAZING_TILE], rotation_xyzw=rotation))
    return boxes


GRAZING_BOXES  = _grazing_boxes()
GRAZING_LIGHTS = poses_from([0.0, GRAZING_HEIGHT, 0.0], [0.0, 0.0, 0.0], 24.0, 20.0)


# contact_blocks ---------------------------------------------------------------------
# A 0.5 m cube, a 1 cm plate and a 5 cm x 1.2 m post resting on a 6 x 6 m
# floor, light up and to the +X +Z side so each casts a shadow from its
# contact line. Texels: spot (60 deg, 4.5 m) 2.5 mm; point 4.4 mm;
# directional ~3.4 mm (7 m fit across the floor).
CONTACT_CASTERS = [
    resting("Cube",  "caster", -0.9, -0.4, [0.5, 0.5, 0.5]),
    resting("Plate", "caster",  0.5, -0.6, [0.6, 0.01, 0.6]),
    resting("Post",  "caster",  0.3,  0.7, [0.05, 1.2, 0.05]),
]
CONTACT_BOXES  = [floor("Floor", 3.0, 3.0)] + CONTACT_CASTERS
CONTACT_LIGHTS = poses_from([2.0, 3.5, 2.0], [0.0, 0.0, 0.0], 60.0, 50.0)
CONTACT_LINES  = {b["name"]: footprint_edges(b) for b in CONTACT_CASTERS}


# thin_walls ---------------------------------------------------------------------------
# Five closed huts (four walls + roof, all of one thickness: 1, 2, 5, 10,
# 20 cm) on one floor, interior 0.8 x 0.5 x 0.8 m, light outside on the +Z
# side. Every point of a hut's interior floor is behind a wall for every
# light pose outside the hut (R4, including the wall-floor and wall-wall
# joins: the X walls span the full outer depth, the roof the full outer
# footprint). One view per hut looks at the interior from inside.
# Texels: spot (95 deg, 4.1 m) 4.4 mm; point 4.0 mm; directional ~4.4 mm.
THIN_THICKNESSES = [0.01, 0.02, 0.05, 0.10, 0.20]
THIN_SPACING     = 1.6
THIN_HALF        = 0.4       # interior half size in x and z
THIN_H           = 0.5       # interior height


def _hut_x(index):
    return (index - 0.5 * (len(THIN_THICKNESSES) - 1)) * THIN_SPACING


def _hut_label(t):
    return f"{round(t * 100.0):d}cm"


def _thin_wall_boxes():
    boxes = [floor("Floor", 4.5, 2.0)]
    for index, t in enumerate(THIN_THICKNESSES):
        cx = _hut_x(index)
        h, y = THIN_HALF, THIN_H
        label = _hut_label(t)
        boxes += [
            box(f"Hut {label} Wall -X", "wall", lo=[cx - h - t, 0.0, -h - t], hi=[cx - h,     y, h + t]),
            box(f"Hut {label} Wall +X", "wall", lo=[cx + h,     0.0, -h - t], hi=[cx + h + t, y, h + t]),
            box(f"Hut {label} Wall -Z", "wall", lo=[cx - h,     0.0, -h - t], hi=[cx + h,     y, -h]),
            box(f"Hut {label} Wall +Z", "wall", lo=[cx - h,     0.0,  h],     hi=[cx + h,     y, h + t]),
            box(f"Hut {label} Roof",    "wall", lo=[cx - h - t, y,   -h - t], hi=[cx + h + t, y + t, h + t]),
        ]
    return boxes


def _thin_walls():
    """Per hut: wall thickness, interior AABB and the receiver region behind
    the walls (the interior floor, y = 0)."""
    huts = []
    for index, t in enumerate(THIN_THICKNESSES):
        cx = _hut_x(index)
        huts.append({
            "name":          f"Hut {_hut_label(t)}",
            "thickness":     t,
            "interior_lo":   [cx - THIN_HALF, 0.0, -THIN_HALF],
            "interior_hi":   [cx + THIN_HALF, THIN_H, THIN_HALF],
            "region_lo":     [cx - THIN_HALF, 0.0, -THIN_HALF],
            "region_hi":     [cx + THIN_HALF, 0.0, THIN_HALF],
            "view":          f"inside_{_hut_label(t)}",
        })
    return huts


THIN_BOXES  = _thin_wall_boxes()
THIN_WALLS  = _thin_walls()
THIN_LIGHTS = poses_from([0.0, 2.5, 3.5], [0.0, 0.25, 0.0], 95.0, 85.0, light_range=15.0)


# depth_range ------------------------------------------------------------------------------
# A 8 x 8 m floor that does NOT cast (shadow_cast off: with fit_to_casters
# the fitted far plane then hugs the lowest caster and the floor lies beyond
# it), a floating 2 cm plate 1.5 m up, and a 10 cm block 0.5 m under the
# light (near the spot near plane 0.04 m at reverse-Z's precise end; the
# near plane of the directional fit). Exercises doc/erhe/shadows.md
# "Receivers outside the fitted depth range".
# Texels: spot (90 deg, 6 m) 5.9 mm at the floor, 4.4 mm at the plate;
# point 5.9 mm; directional ~2.4 mm (fit to the plate and block only).
DEPTH_BOXES = [
    floor("Floor", 4.0, 4.0, casts_shadow=False),
    box("Plate", "caster", center=[-0.8, 1.5, 0.4], size=[0.8, 0.02, 0.8]),
    box("Near Block", "caster", center=[0.25, 5.45, 0.0], size=[0.1, 0.1, 0.1]),
]
DEPTH_LIGHTS = poses_from([0.0, 6.0, 0.0], [0.5, 0.0, 0.5], 90.0, 80.0)


# cube_seams ---------------------------------------------------------------------------------
# Closed 6 x 3 x 6 m room (10 cm walls) with the point light at its centre,
# 1.5 m up. Casters straddle cube face boundaries of the light: posts on the
# horizontal diagonals (|x| = |z|, X / Z seams), floor blocks on the
# |x| = |y - 1.5| seams (X / -Y, Z / -Y) and ceiling blocks on the +Y seams.
# The floor-wall and wall-wall edges of the room are seams too.
# Texels: point 1.5 mm at 1.5 m, 4.4 mm at the far corners; spot (120 deg)
# 3.2 mm at the aim point; directional: everything inside the closed room is
# shadowed (an occlusion test), fit ~6 m -> 3 mm.
SEAM_HALF  = 3.0
SEAM_H     = 3.0
SEAM_T     = 0.1
SEAM_LIGHT = [0.0, 1.5, 0.0]


def _seam_boxes():
    h, y, t = SEAM_HALF, SEAM_H, SEAM_T
    boxes = [
        box("Floor",   "receiver", lo=[-h - t, -t, -h - t], hi=[h + t, 0.0,   h + t]),
        box("Ceiling", "wall",     lo=[-h - t, y,  -h - t], hi=[h + t, y + t, h + t]),
        box("Wall -X", "wall",     lo=[-h - t, 0.0, -h - t], hi=[-h, y, h + t]),
        box("Wall +X", "wall",     lo=[h,      0.0, -h - t], hi=[h + t, y, h + t]),
        box("Wall -Z", "wall",     lo=[-h,     0.0, -h - t], hi=[h, y, -h]),
        box("Wall +Z", "wall",     lo=[-h,     0.0,  h],     hi=[h, y, h + t]),
    ]
    d = 1.5 / math.sqrt(2.0)
    for sx, sz, tag in ((1, 1, "+X+Z"), (-1, 1, "-X+Z"), (-1, -1, "-X-Z"), (1, -1, "+X-Z")):
        boxes.append(resting(f"Post {tag}", "caster", sx * d, sz * d, [0.1, 1.0, 0.1]))
    for x, z, tag in ((1.35, 0.0, "+X"), (-1.35, 0.0, "-X"), (0.0, 1.35, "+Z"), (0.0, -1.35, "-Z")):
        boxes.append(resting(f"Floor Block {tag}", "caster", x, z, [0.3, 0.3, 0.3]))
        boxes.append(box(f"Ceiling Block {tag}", "caster", center=[x, SEAM_H - 0.15, z], size=[0.3, 0.3, 0.3]))
    return boxes


SEAM_BOXES  = _seam_boxes()
SEAM_LIGHTS = poses_from(SEAM_LIGHT, [0.8, 0.0, 0.6], 120.0, 100.0, light_range=10.0)


# spot_cones --------------------------------------------------------------------------------------
# One spot 4.1 m from a 10 x 10 m floor aimed at its centre; the pose sweep
# measures the outer angles 5, 45 and 80 degrees. A 10 cm cube sits inside
# the 5 degree footprint (radius 0.18 m), a 0.4 m box inside the 45 degree
# one, a 5 cm post inside the 80 degree one.
# Texels: spot 0.18 mm (5 deg), 1.7 mm (45 deg), 3.4 mm (80 deg); point
# 4.0 mm; directional ~4.9 mm.
SPOT_CONE_ANGLES = [5.0, 45.0, 80.0]
SPOT_BOXES = [
    floor("Floor", 5.0, 5.0),
    resting("Small Cube", "caster", 0.08, 0.0, [0.1, 0.1, 0.1]),
    resting("Box",        "caster", 0.8, -0.6, [0.4, 0.4, 0.4]),
    resting("Post",       "caster", -2.0, 0.8, [0.05, 1.0, 0.05]),
]
SPOT_LIGHTS = poses_from([0.0, 4.0, 1.0], [0.0, 0.0, 0.0], 45.0, 36.0)


# cornell ------------------------------------------------------------------------------------------
# res/editor/assets/gi_test_rooms/gi_cornell.glb as is. Geometry read from
# the loaded asset over MCP (get_node_details world_aabb, 2026-09-28) and
# hard-coded here; built by creation_24_gi_test_rooms.py build_cornell_room
# (3 x 3 x 3 m interior, 0.2 m parts, red -X / green +X walls). Its light is
# the spot "Cornell Light" at (0, 2.9, 0) pointing straight down, outer 90 /
# inner 70 deg, range 12, intensity 12 - the head-on tie case.
# Texels: spot 2.8 mm at the floor; point 2.8 mm; directional: all interior
# surfaces shadowed (closed room), fit ~3.4 m -> 1.7 mm.
CORNELL_BOXES = [
    box("Cornell Floor",   "receiver", lo=[-1.7, -0.2, -1.7], hi=[1.7, 0.0, 1.7]),
    box("Cornell Ceiling", "wall",     lo=[-1.7, 3.0, -1.7],  hi=[1.7, 3.2, 1.7]),
    box("Cornell Wall -X", "wall",     lo=[-1.7, 0.0, -1.7],  hi=[-1.5, 3.0, 1.7]),
    box("Cornell Wall +X", "wall",     lo=[1.5, 0.0, -1.7],   hi=[1.7, 3.0, 1.7]),
    box("Cornell Wall -Z", "wall",     lo=[-1.5, 0.0, -1.7],  hi=[1.5, 3.0, -1.5]),
    box("Cornell Wall +Z", "wall",     lo=[-1.5, 0.0, 1.5],   hi=[1.5, 3.0, 1.7]),
]
CORNELL_LIGHTS = poses_from([0.0, 2.9, 0.0], [0.0, 0.0, 0.0], 90.0, 70.0, light_range=12.0)
CORNELL_LIGHTS["spot"]["intensity"] = 12.0
CORNELL_LIGHTS["point"]["intensity"] = 12.0


# --- station table ---------------------------------------------------------------------------------

TOP_DOWN_UP = (0.0, 0.0, -1.0)


def _station(description, requirements, boxes, lights, views, texel_mm, **extra):
    entry = {
        "description":  description,
        "requirements": list(requirements),
        "boxes":        boxes,
        "lights":       lights,
        "views":        views,
        "texel_mm":     texel_mm,
        "light":        LIGHT_NAME,
    }
    entry.update(extra)
    return entry


def _texels(spot_distance, spot_outer, point_distance, ortho_extent):
    return {"spot": round(spot_texel_mm(spot_distance, spot_outer), 2),
            "point": round(point_texel_mm(point_distance), 2),
            "directional": round(ortho_texel_mm(ortho_extent), 2)}


STATIONS = {
    "head_on_floor": _station(
        "12 x 12 m floor with the light on the axis above it: R1 at dz_dUV = 0, R6 over the pose sweep.",
        ["R1", "R6", "R7"], HEAD_ON_BOXES, HEAD_ON_LIGHTS,
        [view("top", [0.0, 7.0, 0.0], [0.0, 0.0, 0.0], 60.0, up=TOP_DOWN_UP, boxes=HEAD_ON_BOXES)],
        _texels(3.0, 90.0, 3.0, 12.0)),
    "grazing_fan": _station(
        "Eight 2 cm tiles whose normals make 0 .. 88 degrees with the light axis above a floor: R1 across "
        "orientations, D3.",
        ["R1"], GRAZING_BOXES, GRAZING_LIGHTS,
        [view("top", [0.0, 6.0, 0.0], [0.0, 0.0, 0.0], 45.0, up=TOP_DOWN_UP, boxes=GRAZING_BOXES),
         view("side_z", [0.0, 2.2, 3.2], [0.0, 0.5, 0.0], 60.0, boxes=GRAZING_BOXES),
         view("side_x", [3.2, 2.2, 0.0], [0.0, 0.5, 0.0], 60.0, boxes=GRAZING_BOXES)],
        _texels(8.0, 24.0, 8.0, 5.0),
        tile_angles_deg=list(GRAZING_ANGLES)),
    "contact_blocks": _station(
        "Cube, 1 cm plate and thin post resting on a floor: R2 occlusion, R3 contact, R5 edge placement.",
        ["R2", "R3", "R5", "R7"], CONTACT_BOXES, CONTACT_LIGHTS,
        [view("top", [0.0, 5.0, 0.0], [0.0, 0.0, 0.0], 50.0, up=TOP_DOWN_UP, boxes=CONTACT_BOXES),
         view("oblique", [-1.0, 2.2, 2.8], [-0.2, 0.1, -0.3], 60.0, boxes=CONTACT_BOXES)],
        _texels(math.sqrt(2.0 * 2.0 + 3.5 * 3.5 + 2.0 * 2.0), 60.0, 4.5, 7.0),
        contact_lines=CONTACT_LINES),
    "thin_walls": _station(
        "Five closed huts with 1, 2, 5, 10, 20 cm walls and roof, light outside on the +Z side: R4, the "
        "interior floor is behind a wall for every light pose.",
        ["R4"], THIN_BOXES, THIN_LIGHTS,
        [view("overview", [0.0, 3.0, 4.5], [0.0, 0.2, 0.0], 70.0, boxes=THIN_BOXES)]
        + [view(hut["view"], [hut["interior_lo"][0] + 0.05, 0.45, 0.35],
                [hut["interior_hi"][0] - 0.1, 0.0, -0.3], 90.0, near=0.01, boxes=THIN_BOXES)
           for hut in THIN_WALLS],
        _texels(4.1, 95.0, 4.1, 9.0),
        walls=THIN_WALLS),
    "depth_range": _station(
        "Non-casting floor below the fitted far plane, a floating plate, and a block next to the light's "
        "near plane: R1, R2 at the depth clamp paths.",
        ["R1", "R2"], DEPTH_BOXES, DEPTH_LIGHTS,
        [view("top", [0.0, 11.0, 0.0], [0.0, 0.0, 0.0], 42.0, up=TOP_DOWN_UP, boxes=DEPTH_BOXES),
         # The camera below the Near Block: the block lies between the light
         # and the view frustum, so the directional fit's
         # near_from_main_frustum puts the near plane below it and depth
         # clamp keeps it in the map with vertex depths above 1 (shadows.md "Minimum bias").
         view("under_block", [0.0, 3.0, 0.0], [0.0, 0.0, 0.0], 90.0, up=TOP_DOWN_UP, boxes=DEPTH_BOXES)],
        _texels(6.0, 90.0, 6.0, 5.0)),
    "cube_seams": _station(
        "Point light in a closed room, casters straddling its cube face boundaries: R1, R2 for the point "
        "cube.",
        ["R1", "R2"], SEAM_BOXES, SEAM_LIGHTS,
        [view("floor", [0.0, 2.9, 0.0], [0.0, 0.0, 0.0], 110.0, up=TOP_DOWN_UP, boxes=SEAM_BOXES),
         view("ceiling", [0.0, 0.1, 0.0], [0.0, 3.0, 0.0], 110.0, up=TOP_DOWN_UP, boxes=SEAM_BOXES),
         view("corner", [2.8, 2.8, 2.8], [-1.0, 0.3, -1.0], 90.0, boxes=SEAM_BOXES)],
        _texels(1.9, 120.0, 1.5, 6.0)),
    "spot_cones": _station(
        "One spot aimed at a floor with outer angles 5, 45 and 80 degrees, casters inside each footprint: "
        "R1, R5 across projection widths.",
        ["R1", "R5"], SPOT_BOXES, SPOT_LIGHTS,
        [view("top", [0.0, 7.0, 0.0], [0.0, 0.0, 0.0], 70.0, up=TOP_DOWN_UP, boxes=SPOT_BOXES),
         view("centre", [0.0, 1.2, 0.0], [0.0, 0.0, 0.0], 30.0, up=TOP_DOWN_UP, boxes=SPOT_BOXES)],
        {"spot": {f"{a:g}": round(spot_texel_mm(math.sqrt(17.0), a), 2) for a in SPOT_CONE_ANGLES},
         "point": round(point_texel_mm(math.sqrt(17.0)), 2), "directional": round(ortho_texel_mm(10.0), 2)},
        cone_angles_deg=list(SPOT_CONE_ANGLES)),
    "cornell": _station(
        "gi_cornell.glb at its saved light pose: the head-on tie as a regression.",
        ["R1"], CORNELL_BOXES, CORNELL_LIGHTS,
        [view("main", [0.0, 1.7, 1.4], [0.0, 0.9, -1.5], 75.0, boxes=CORNELL_BOXES),
         view("top", [0.0, 2.8, 0.0], [0.0, 0.0, 0.0], 70.0, up=TOP_DOWN_UP, boxes=CORNELL_BOXES)],
        _texels(2.9, 90.0, 2.9, 3.4),
        light="Cornell Light", root="GI cornell", asset="res/editor/assets/gi_test_rooms/gi_cornell.glb",
        prebuilt=True),
}

for _name, _entry in STATIONS.items():
    _entry.setdefault("root", f"Shadow {_name}")
    _entry.setdefault("asset", f"{ASSET_DIR}/shadow_{_name}.glb")
    _entry.setdefault("prebuilt", False)


# --- pose sweeps (shadows.md "Matrices") -----------------------------------------------------------

SWEEP_STEP_M   = 0.013    # lateral light offset step: a few texels, never texel aligned
SWEEP_STEP_DEG = 0.25     # directional tilt step


def _perpendicular_basis(direction):
    d = v_norm(direction)
    helper = [1.0, 0.0, 0.0] if abs(d[0]) < 0.9 else [0.0, 0.0, 1.0]
    u = v_norm([d[1] * helper[2] - d[2] * helper[1],
                d[2] * helper[0] - d[0] * helper[2],
                d[0] * helper[1] - d[1] * helper[0]])
    w = [d[1] * u[2] - d[2] * u[1], d[2] * u[0] - d[0] * u[2], d[0] * u[1] - d[1] * u[0]]
    return u, w


def _moved(base, position=None, direction=None, outer_deg=None):
    """A copy of pose `base` with a new position / direction / outer angle
    (inner angle keeps its ratio to the outer one)."""
    p = dict(base)
    if position is not None:
        p["position"] = [float(v) for v in position]
    if direction is not None:
        p["direction"] = v_norm(direction)
    if outer_deg is not None:
        ratio = base["inner_spot_angle_deg"] / base["outer_spot_angle_deg"]
        p["outer_spot_angle_deg"] = float(outer_deg)
        p["inner_spot_angle_deg"] = float(outer_deg) * ratio
    if p["type"] != "point":
        p["rotation_xyzw"] = light_rotation(p["position"], p["direction"])
    return p


def _grid_5x5(base, step_m=SWEEP_STEP_M, step_deg=SWEEP_STEP_DEG):
    """25 poses around `base`: spot / point translated over a 5 x 5 grid in
    the plane across the light axis (horizontal for point), direction kept;
    directional tilted over a 5 x 5 grid of step_deg."""
    poses = []
    if base["type"] == "directional":
        u, w = _perpendicular_basis(base["direction"])
        for j in range(-2, 3):
            for i in range(-2, 3):
                a = math.radians(step_deg * i)
                b = math.radians(step_deg * j)
                d = v_add(base["direction"], v_add(v_scale(u, math.tan(a)), v_scale(w, math.tan(b))))
                poses.append(_moved(base, direction=d))
        return poses
    if base["type"] == "spot":
        u, w = _perpendicular_basis(base["direction"])
    else:
        u, w = [1.0, 0.0, 0.0], [0.0, 0.0, 1.0]
    for j in range(-2, 3):
        for i in range(-2, 3):
            offset = v_add(v_scale(u, step_m * i), v_scale(w, step_m * j))
            poses.append(_moved(base, position=v_add(base["position"], offset)))
    return poses


def _sweep_head_on_floor(light_type):
    """Spot / point: height 3.000 .. 3.199 m in 1 mm steps (200), 200
    heights geometrically spread over 0.5 .. 10 m, then the 5 x 5 lateral
    grid of 0.5 m steps at the default height (425 poses). Directional: the
    light stays straight down (the head-on case); the 25 poses are the
    lateral grid positions, which move only the light node - the directional
    fit follows the view camera, so a directional sweep also has to move
    the view (the verify script's job)."""
    base = STATIONS["head_on_floor"]["lights"][light_type]
    poses = []
    if light_type != "directional":
        for k in range(200):
            poses.append(_moved(base, position=[0.0, HEAD_ON_HEIGHT + 0.001 * k, 0.0]))
        for k in range(200):
            height = 0.5 * math.pow(10.0 / 0.5, k / 199.0)
            poses.append(_moved(base, position=[0.0, height, 0.0]))
    for j in range(-2, 3):
        for i in range(-2, 3):
            poses.append(_moved(base, position=[0.5 * i, HEAD_ON_HEIGHT, 0.5 * j]))
    return poses


def _sweep_spot_cones(light_type):
    """Spot: for each outer angle 5 / 45 / 80 deg the eight non-centre poses
    of a 3 x 3 lateral grid, plus the default pose (25). Other types: the
    5 x 5 grid."""
    base = STATIONS["spot_cones"]["lights"][light_type]
    if light_type != "spot":
        return _grid_5x5(base)
    u, w = _perpendicular_basis(base["direction"])
    poses = [dict(base)]
    for outer in SPOT_CONE_ANGLES:
        for j in range(-1, 2):
            for i in range(-1, 2):
                if (i == 0) and (j == 0):
                    continue
                offset = v_add(v_scale(u, SWEEP_STEP_M * i), v_scale(w, SWEEP_STEP_M * j))
                poses.append(_moved(base, position=v_add(base["position"], offset), outer_deg=outer))
    return poses


def pose_sweep(name, light_type, sweep="full"):
    """The station's light pose sweep (shadows.md "Matrices") for one light type,
    station-local. sweep "short" keeps 5 poses evenly spread over the full
    list (first = the default pose)."""
    if name == "head_on_floor":
        poses = _sweep_head_on_floor(light_type)
    elif name == "spot_cones":
        poses = _sweep_spot_cones(light_type)
    else:
        poses = _grid_5x5(STATIONS[name]["lights"][light_type])
    if sweep == "short":
        count = 5
        picks = sorted({round(i * (len(poses) - 1) / (count - 1)) for i in range(count)})
        poses = [poses[i] for i in picks]
    elif sweep != "full":
        raise ValueError(f"sweep must be 'full' or 'short', not {sweep!r}")
    return poses


# --- station-local -> world ------------------------------------------------------------------------

def to_world(point, root_offset=(0.0, 0.0, 0.0)):
    """Station-local point -> world, for the station root translated by
    root_offset (R7: 1 km / 10 km)."""
    return [float(point[i]) + float(root_offset[i]) for i in range(3)]


def box_to_world(b, root_offset=(0.0, 0.0, 0.0)):
    out = dict(b)
    out["center"] = to_world(b["center"], root_offset)
    return out


def pose_to_world(p, root_offset=(0.0, 0.0, 0.0)):
    out = dict(p)
    out["position"] = to_world(p["position"], root_offset)
    return out


def render_camera(v, root_offset=(0.0, 0.0, 0.0)):
    """A view as render_scene_image's `camera` argument, in world space."""
    return {"eye": to_world(v["eye"], root_offset), "target": to_world(v["target"], root_offset),
            "up": list(v["up"]), "fov_y_degrees": v["fov_y_deg"], "near": v["near"], "far": v["far"],
            "shadow_range": v["shadow_range"]}


def apply_light_pose(c, scene, light_name, p, root_offset=(0.0, 0.0, 0.0)):
    """Set the station light to pose `p` (station-local; root translated by
    root_offset): edit_light sets type, range, intensity and spot angles,
    set_node_transform the world translation + rotation (edit_light's own
    `position` resets the node rotation)."""
    args = {"scene_name": scene, "light_name": light_name, "type": p["type"], "range": p["range"],
            "intensity": p["intensity"], "cast_shadow": True}
    if p["type"] == "spot":
        args["outer_spot_angle"] = math.radians(p["outer_spot_angle_deg"])
        args["inner_spot_angle"] = math.radians(p["inner_spot_angle_deg"])
    c.mutate("edit_light", args)
    c.set_node_transform(light_name, translation=to_world(p["position"], root_offset),
                         rotation_xyzw=p["rotation_xyzw"])


# --- building ---------------------------------------------------------------------------------------

def set_headlight(c, enabled):
    """Graphics_settings::headlight_when_unlit (app-wide); returns the
    previous value for the caller to restore."""
    previous = c.call("set_graphics_settings", {}).get("headlight_when_unlit", True)
    c.mutate("set_graphics_settings", {"headlight_when_unlit": bool(enabled)})
    return previous


def place_view(c, v):
    cameras = c.call("get_scene_cameras", {"scene_name": c.scene}).get("cameras", [])
    c.mutate("edit_camera", {"scene_name": c.scene, "camera_id": cameras[0]["id"],
                             "fov_y": math.radians(v["fov_y_deg"]), "z_near": v["near"],
                             "z_far": v["far"], "shadow_range": v["shadow_range"]})
    c.place_camera(v["eye"], v["target"], tuple(v["up"]))


def build_station(c, name):
    """Close every open scene and build station `name` in a fresh scene:
    one root node holding every box and the light, the light at its default
    spot pose (the type every station opens with; apply_light_pose()
    switches it), the scene camera at the first view."""
    station = STATIONS[name]
    if station["prebuilt"]:
        raise ValueError(f"station {name} is the prebuilt asset {station['asset']}")
    c.close_all_scenes()
    c.new_scene()
    c.ambience(ambient=[0.0, 0.0, 0.0], clear_color=[0.0, 0.0, 0.0, 1.0], grid=False, sky=False)
    root = c.group(station["root"], [0.0, 0.0, 0.0])
    material = c.ensure_material("shadow white", base_color=WHITE, metallic=0.0, roughness=1.0)
    for b in station["boxes"]:
        size = [2.0 * h for h in b["half_extents"]]
        kwargs = {}
        if b["rotation_xyzw"] != IDENTITY:
            kwargs["rotation_xyzw"] = b["rotation_xyzw"]
        result = c.shape("box", b["name"], b["center"], size=size, steps=[0, 0, 0],
                         material_name=material, motion_mode="none", parent_node_id=root,
                         reuse=False, **kwargs)
        if not b["casts_shadow"]:
            c.mutate("set_item_property", {"item_id": int(result["node_id"]), "property": "shadow_cast",
                                           "value": "false"})
    p = station["lights"]["spot"]
    c.light("spot", LIGHT_NAME, p["position"], [1.0, 1.0, 1.0], p["intensity"], range=p["range"],
            cast_shadow=True, outer_spot_angle=math.radians(p["outer_spot_angle_deg"]),
            inner_spot_angle=math.radians(p["inner_spot_angle_deg"]))
    c.set_node_transform(LIGHT_NAME, rotation_xyzw=p["rotation_xyzw"])
    c.mutate("reparent_item", {"scene_name": c.scene, "item_name": LIGHT_NAME, "parent_id": int(root)})
    c.settle()
    place_view(c, station["views"][0])
    c.settle()


def save_station(c, name, report):
    """Build station `name` and save it as a loadable scene asset."""
    print(f"=== save station {name}")
    build_station(c, name)
    # Every part is a private mesh, so the brush library a new scene starts
    # with is geometry the asset does not use.
    if c.node_by_name("Brushes") is not None:
        c.mutate("delete_nodes", {"scene_name": c.scene, "names": ["Brushes"]})
    c.settle()
    path = STATIONS[name]["asset"]
    c.save(path)
    report.append(f"{name}: saved {path} ({os.path.getsize(path)} bytes)")


def render_station(c, name, report):
    """Render every view of the open station (PNG) and one shader_debug 30
    shadow visibility PNG of the first view."""
    station = STATIONS[name]
    for v in station["views"]:
        c.call("render_scene_image", {"scene": c.scene, "camera": render_camera(v), "width": v["width"],
                                      "height": v["height"], "output": "png",
                                      "path": f"{BASE}_{name}_{v['name']}.png"})
    v = station["views"][0]
    c.call("render_scene_image", {"scene": c.scene, "camera": render_camera(v), "width": v["width"],
                                  "height": v["height"], "output": "png", "shader_debug": 30,
                                  "shadow_debug_light": station["light"],
                                  "path": f"{BASE}_{name}_{v['name']}_visibility.png"})
    report.append(f"{name}: rendered {len(station['views'])} view(s) to {BASE}_{name}_*.png")


def add_script_arguments(parser):
    parser.add_argument("--station", default="all",
                        help="station name or 'all' (" + ", ".join(STATIONS) + ")")
    parser.add_argument("--save-assets", action="store_true",
                        help="build and save each station to " + ASSET_DIR + "/shadow_<station>.glb "
                             "instead of the render run")


def main():
    args = standard_args(TITLE, add_script_arguments)
    names = list(STATIONS) if args.station == "all" else [args.station]
    for name in names:
        if name not in STATIONS:
            raise SystemExit(f"unknown station {name!r}; one of {', '.join(STATIONS)}")
    c = Creation(TITLE, port=args.port, pause_s=0.0, editor_exe=args.editor_exe,
                 reuse=args.reuse, keep_scenes=args.keep_scenes,
                 manage_windows=not args.keep_windows)
    report = []
    headlight = c.call("set_graphics_settings", {}).get("headlight_when_unlit", True)
    with fail_soft(c, BASE):
        try:
            set_headlight(c, False)
            for name in names:
                station = STATIONS[name]
                if station["prebuilt"]:
                    if args.save_assets:
                        report.append(f"{name}: prebuilt asset {station['asset']} (not rebuilt)")
                        continue
                    c.close_all_scenes()
                    c.load(station["asset"])
                elif args.save_assets:
                    save_station(c, name, report)
                    continue
                else:
                    build_station(c, name)
                render_station(c, name, report)
            c.close_all_scenes()
        finally:
            set_headlight(c, headlight)
    print("\n".join(report))


if __name__ == "__main__":
    main()
