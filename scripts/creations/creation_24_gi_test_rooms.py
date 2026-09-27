#!/usr/bin/env python3
"""Creation 24 - GI Test Rooms.

A test ASSET for the indirect diffuse producers (DDGI today, radiance
cascades next): the stations of doc/plans/radiance_cascades.md section 7.
Every station is built as ITS OWN scene, because the DDGI probe volume is
fitted to the content of the single scene root - one station per scene keeps
each probe grid small and predictable.

All rooms are closed boxes of 0.2 m thick parts (floor, four walls, ceiling),
white albedo 0.8 unless stated, so a surface outside the direct light is lit
only through the indirect term under test. Scene ambient is black in every
station except `courtyard`.

The module is the single source of truth for what each station is:
`scripts/gi_verify.py` imports STATIONS and build_station(). Importing the
module has no side effects.

  STATIONS[name] = {
      "description": ...,          # what the station measures
      "ambient":     [r, g, b],    # scene ambient
      "views":       [...],        # views for the NOMINAL layout (see below)
      "events":      {...},        # dynamic only: name -> fn(creation, info)
      "event_groups": {...},       # dynamic only: event name -> sample groups it changes
      ...
  }

STATIONS[name]["samples_for"](layout) returns the station's SAMPLE GROUPS:
{group name: [{"position": [x, y, z], "normal": [x, y, z]}, ...]}, world
points on the measured surfaces (grids inset from the edges) for the
sample_indirect_diffuse MCP tool, which returns the linear float irradiance
the forward pass shades with. scripts/gi_verify.py measures with these, not
with screenshot pixels; the layout is the one build_station realized.

A view is {"name", "eye", "target", "fov_y_deg", "rects": [...]}; a rect is
{"name", "surface", "stat", "rect": [x0, y0, x1, y1]} with the rectangle in
FRACTIONS of the station's VIEWPORT (origin top-left, x right, y down), so it
is resolution independent. capture_screenshot captures the whole editor
window; info["viewport"] is the viewport's [x, y, width, height] pixel
rectangle inside that image and rect_pixels(rect, info["viewport"]) maps a
rectangle to image pixels. `surface` names the mesh the rectangle lies on (the
check_rects() raycast verifies it), `stat` names the statistic it is for:

  mean_luminance   mean luminance over the rectangle
  mean_rgb         mean colour over the rectangle (colour ratios)
  min_vs_median    darkest pixel against the rectangle median (splotches)
  profile_sample   one sample of an ordered profile; rects carrying the same
                   "profile" key, in table order, form the profile
  temporal_stddev  per-pixel luminance standard deviation over frames

build_station(creation, name, ddgi=True) closes every open scene, builds the
station in a fresh scene and returns an info dict whose "views" are the views
for the REALIZED layout (probe_offset_sweep moves walls after reading the
fitted probe grid, so its rectangles are only known after the build). Use
info["views"], not STATIONS[name]["views"], for measurements.

CLI: --station <name> | all (default all). Each station is built, screenshot
with DDGI enabled (and its first view once more with DDGI disabled), the
rectangles are checked by raycast and drawn onto a debug copy of each view
(logs/creations/gi_test_rooms_<station>_<view>_rects.png), and the scene is
closed before the next station.
"""

import json
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))

from common import (  # noqa: E402
    Creation,
    standard_args,
    fail_soft,
    look_at_quaternion,
    v_add,
    v_sub,
    v_dot,
    v_cross,
    v_norm,
    v_scale,
)

TITLE = "GI Test Rooms"
BASE  = "logs/creations/gi_test_rooms"

# Wall / floor / ceiling thickness of every room shell.
T = 0.2

# Aspect ratio (width / height) the rectangle fractions are computed for:
# the headless editor's viewport with the ini seed from the erhe-creations
# skill (1600 x 860 image). check_rects() raycasts with the LIVE viewport
# aspect, so a different viewport size fails loudly instead of measuring the
# wrong surface.
VIEW_ASPECT = 1600.0 / 860.0

# The viewport draws its navigation gizmo (axis widget + zoom / pan buttons)
# over the top-right corner; no rectangle may reach into it.
NAV_GIZMO_X = 0.88
NAV_GIZMO_Y = 0.33

WHITE = [0.8, 0.8, 0.8]
BLACK = [0.0, 0.0, 0.0]


# --- projection (world quad -> screen fraction rectangle) ---------------------

def camera_basis(eye, target, up=(0.0, 1.0, 0.0)):
    forward = v_norm(v_sub(target, eye))
    right   = v_norm(v_cross(forward, list(up)))
    true_up = v_cross(right, forward)
    return forward, right, true_up


def project(view, point, aspect=VIEW_ASPECT):
    """World point -> (fx, fy) image fraction for a view (top-left origin)."""
    forward, right, up = camera_basis(view["eye"], view["target"])
    d = v_sub(point, view["eye"])
    z = v_dot(d, forward)
    if z <= 1e-4:
        raise ValueError(f"point {point} is behind view {view['name']}")
    tan_half = math.tan(0.5 * math.radians(view["fov_y_deg"]))
    ndc_x = v_dot(d, right) / (z * tan_half * aspect)
    ndc_y = v_dot(d, up) / (z * tan_half)
    return 0.5 + 0.5 * ndc_x, 0.5 - 0.5 * ndc_y


def pixel_ray(view, fx, fy, aspect):
    """Inverse of project(): world ray direction through image fraction."""
    forward, right, up = camera_basis(view["eye"], view["target"])
    tan_half = math.tan(0.5 * math.radians(view["fov_y_deg"]))
    ndc_x = (fx - 0.5) * 2.0
    ndc_y = (0.5 - fy) * 2.0
    direction = v_add(forward, v_add(v_scale(right, ndc_x * tan_half * aspect),
                                     v_scale(up, ndc_y * tan_half)))
    return v_norm(direction)


def _inside_convex(poly, p):
    sign = 0.0
    for i in range(len(poly)):
        a = poly[i]
        b = poly[(i + 1) % len(poly)]
        cross = (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0])
        if abs(cross) < 1e-12:
            continue
        if sign == 0.0:
            sign = cross
        elif (cross > 0.0) != (sign > 0.0):
            return False
    return True


def inner_rect(view, quad):
    """Screen rectangle inside the projection of a convex world quad: start
    from the rectangle between the two inner projected x / y coordinates and
    shrink it about its centre until all four corners lie inside the
    projected quad, so the rectangle stays on the quad's surface under
    perspective (a floor strip next to a wall projects to a slanted
    trapezoid)."""
    pts = [project(view, p) for p in quad]
    xs = sorted(p[0] for p in pts)
    ys = sorted(p[1] for p in pts)
    x0, x1 = xs[1], xs[2]
    y0, y1 = ys[1], ys[2]
    if (x1 <= x0) or (y1 <= y0):
        raise ValueError(f"degenerate rect for view {view['name']}: {pts}")
    cx, cy = 0.5 * (x0 + x1), 0.5 * (y0 + y1)
    hx, hy = 0.5 * (x1 - x0), 0.5 * (y1 - y0)
    for _ in range(60):
        corners = [(cx - hx, cy - hy), (cx + hx, cy - hy), (cx + hx, cy + hy), (cx - hx, cy + hy)]
        if all(_inside_convex(pts, p) for p in corners):
            break
        hx *= 0.95
        hy *= 0.95
    else:
        raise ValueError(f"no rect fits inside the quad for view {view['name']}: {pts}")
    x0, y0, x1, y1 = cx - hx, cy - hy, cx + hx, cy + hy
    if (x0 < 0.0) or (y0 < 0.0) or (x1 > 1.0) or (y1 > 1.0):
        raise ValueError(f"rect outside the image for view {view['name']}: {[x0, y0, x1, y1]}")
    if (x1 > NAV_GIZMO_X) and (y0 < NAV_GIZMO_Y):
        raise ValueError(f"rect under the navigation gizmo for view {view['name']}: {[x0, y0, x1, y1]}")
    return [round(x0, 4), round(y0, 4), round(x1, 4), round(y1, 4)]


def floor_quad(x0, x1, z0, z1, y=0.0):
    return [[x0, y, z0], [x1, y, z0], [x1, y, z1], [x0, y, z1]]


def wall_x_quad(x, y0, y1, z0, z1):
    """Quad on a plane of constant x."""
    return [[x, y0, z0], [x, y0, z1], [x, y1, z1], [x, y1, z0]]


def wall_z_quad(z, x0, x1, y0, y1):
    """Quad on a plane of constant z."""
    return [[x0, y0, z], [x1, y0, z], [x1, y1, z], [x0, y1, z]]


def rect(name, surface, stat, quad, **extra):
    entry = {"name": name, "surface": surface, "stat": stat, "quad": quad}
    entry.update(extra)
    return entry


def view(name, eye, target, fov_y_deg, rects):
    return {"name": name, "eye": list(eye), "target": list(target),
            "fov_y_deg": float(fov_y_deg), "rects": rects}


def grid_points(origin, axis_u, axis_v, nu, nv, normal):
    """nu x nv sample points {position, normal} spanning the parallelogram
    origin + [0, 1] axis_u + [0, 1] axis_v (ends included)."""
    points = []
    for j in range(nv):
        fv = (j / (nv - 1)) if nv > 1 else 0.5
        for i in range(nu):
            fu = (i / (nu - 1)) if nu > 1 else 0.5
            position = v_add(origin, v_add(v_scale(axis_u, fu), v_scale(axis_v, fv)))
            points.append({"position": [round(p, 6) for p in position], "normal": list(normal)})
    return points


def floor_points(x0, x1, z0, z1, nx=5, nz=5, y=0.0, normal=(0.0, 1.0, 0.0)):
    """Points on a horizontal surface at height y (a floor top by default)."""
    return grid_points([x0, y, z0], [x1 - x0, 0.0, 0.0], [0.0, 0.0, z1 - z0], nx, nz, normal)


def wall_x_points(x, facing, y0, y1, z0, z1, nz=5, ny=5):
    """Points on the plane x = const, normal +X (facing = +1) or -X (-1)."""
    return grid_points([x, y0, z0], [0.0, 0.0, z1 - z0], [0.0, y1 - y0, 0.0], nz, ny,
                       (float(facing), 0.0, 0.0))


def wall_z_points(z, facing, x0, x1, y0, y1, nx=5, ny=5):
    """Points on the plane z = const, normal +Z (facing = +1) or -Z (-1)."""
    return grid_points([x0, y0, z], [x1 - x0, 0.0, 0.0], [0.0, y1 - y0, 0.0], nx, ny,
                       (0.0, 0.0, float(facing)))


def resolve_views(views):
    """Fill in each rect's screen fraction rectangle from its world quad."""
    for v in views:
        for r in v["rects"]:
            r["rect"] = inner_rect(v, r["quad"])
    return views


# --- building blocks -----------------------------------------------------------

def box(c, name, lo, hi, material, parent):
    size   = [hi[i] - lo[i] for i in range(3)]
    center = [0.5 * (hi[i] + lo[i]) for i in range(3)]
    return c.shape("box", name, center, size=size, steps=[0, 0, 0],
                   material_name=material, motion_mode="none",
                   parent_node_id=parent, reuse=False)["node_id"]


FACES = ("Floor", "Ceiling", "Wall -X", "Wall +X", "Wall -Z", "Wall +Z")


def room(c, prefix, parent, lo, hi, materials, skip=()):
    """Closed box room around the INTERIOR [lo, hi]; parts T thick. Floor and
    ceiling cover the full outer footprint, the X walls the full outer depth,
    so the corners are closed. materials: face name -> material (missing
    faces use materials["default"]). Returns face name -> node id."""
    def mat(face):
        return materials.get(face, materials["default"])
    lx, ly, lz = lo
    hx, hy, hz = hi
    parts = {
        "Floor":   ([lx - T, ly - T, lz - T], [hx + T, ly,     hz + T]),
        "Ceiling": ([lx - T, hy,     lz - T], [hx + T, hy + T, hz + T]),
        "Wall -X": ([lx - T, ly,     lz - T], [lx,     hy,     hz + T]),
        "Wall +X": ([hx,     ly,     lz - T], [hx + T, hy,     hz + T]),
        "Wall -Z": ([lx,     ly,     lz - T], [hx,     hy,     lz    ]),
        "Wall +Z": ([lx,     ly,     hz    ], [hx,     hy,     hz + T]),
    }
    ids = {}
    for face in FACES:
        if face in skip:
            continue
        a, b = parts[face]
        ids[face] = box(c, f"{prefix} {face}", a, b, mat(face), parent)
    return ids


def spot_rotation(position, aim):
    """Spot / directional lights shine down their -Z, like the camera."""
    direction = v_sub(aim, position)
    up = (0.0, 1.0, 0.0)
    if abs(v_dot(v_norm(direction), [0.0, 1.0, 0.0])) > 0.99:
        up = (0.0, 0.0, -1.0)
    return look_at_quaternion(position, aim, up)


def spot(c, name, position, aim, intensity, outer_deg, inner_deg, light_range=12.0):
    c.light("spot", name, position, [1.0, 1.0, 1.0], intensity,
            range=light_range, cast_shadow=True,
            outer_spot_angle=math.radians(outer_deg),
            inner_spot_angle=math.radians(inner_deg))
    c.set_node_transform(name, rotation_xyzw=spot_rotation(position, aim))


def point(c, name, position, intensity, light_range=12.0):
    c.light("point", name, position, [1.0, 1.0, 1.0], intensity,
            range=light_range, cast_shadow=True)


def white_materials(c):
    return {"default": c.ensure_material("gi white", base_color=WHITE, metallic=0.0, roughness=0.9)}


# --- DDGI access -----------------------------------------------------------------

# DDGI settings every station runs with: the Ddgi_config defaults, pinned so
# a user's edited editor_settings.json cannot change the fitted grid or the
# measured brightness. The probe overlay would draw into every measurement.
DDGI_SETTINGS = {
    "probe_spacing_m":   1.5,
    "volume_padding_m":  1.0,
    "max_probes":        4096,
    "rays_per_probe":    128,
    "hysteresis":        0.97,
    "intensity":         1.0,
    "debug_draw_probes": False,
}


# Radiance cascades settings every station runs with: the
# Radiance_cascades_config defaults, pinned for the same reason. The field's
# sampling parameters (irradiance / distance texels, biases, intensity) are
# the DDGI settings, pinned by set_radiance_cascades() through DDGI_SETTINGS.
RC_SETTINGS = {
    "probe_spacing_m":      0.5,
    "volume_padding_m":     1.0,
    "max_probes_cascade0":  65536,
    "max_cascades":         8,
    "cascade0_tile_texels": 4,
    "interval_scale":       1.0,
    "texels_per_frame":     65536,
    "hysteresis":           0.9,
    "debug_cascade_mask":   0,
}


def set_radiance_cascades(c, merge_mode="interpolate"):
    """Pin the field sampling settings (DDGI_SETTINGS, without selecting DDGI)
    and the radiance cascades settings (RC_SETTINGS plus merge_mode), and
    select radiance cascades as the indirect diffuse source."""
    c.mutate("set_ddgi", dict(DDGI_SETTINGS))
    settings = dict(RC_SETTINGS)
    settings["merge_mode"] = merge_mode
    c.mutate("set_radiance_cascades", settings)
    return set_indirect_diffuse(c, "radiance_cascades")


def set_indirect_diffuse(c, source):
    """Select the indirect diffuse producer: "ambient", "ddgi" or
    "radiance_cascades" (doc/editor/radiance_cascades.md "Source selection")."""
    return c.mutate("set_indirect_diffuse", {"source": source})


def set_ddgi(c, enabled):
    """Pin the DDGI settings (DDGI_SETTINGS) and select DDGI (enabled) or the
    flat ambient term as the indirect diffuse source."""
    c.mutate("set_ddgi", dict(DDGI_SETTINGS))
    return set_indirect_diffuse(c, "ddgi" if enabled else "ambient")


def read_grid(c, deadline_s=30.0):
    """The fitted DDGI grid (origin, spacing, counts) once DDGI is active."""
    deadline = time.time() + deadline_s
    while True:
        stats = c.call("get_indirect_diffuse_stats").get("ddgi", {})
        counts = stats.get("grid_counts", [0, 0, 0])
        if stats.get("active") and min(counts) > 0:
            return {"origin": stats["grid_origin"], "spacing": stats["grid_spacing"],
                    "counts": counts, "probe_count": stats.get("probe_count")}
        if time.time() > deadline:
            raise RuntimeError(f"DDGI did not become active: {stats}")
        time.sleep(0.1)


def wait_ddgi_updates(c, updates=300, deadline_s=90.0):
    """Let the probe field converge: wait until the renderer has run
    `updates` more probe updates (hysteresis 0.97 needs a few hundred)."""
    start = c.call("get_indirect_diffuse_stats").get("ddgi", {}).get("update_count", 0)
    deadline = time.time() + deadline_s
    while time.time() < deadline:
        now = c.call("get_indirect_diffuse_stats").get("ddgi", {}).get("update_count", 0)
        if now - start >= updates:
            return now - start
        time.sleep(0.25)
    return None


def reset_ddgi_volume(c):
    """DDGI off for a few frames: the renderer drops its grid and volume, so
    the next enable fits THIS station's content exactly (a live grid only
    ever grows to include new content, which would carry the previous
    station's extent over)."""
    set_ddgi(c, False)
    c.settle()
    time.sleep(0.3)


# --- stations ----------------------------------------------------------------------
#
# Each station: build(c, root) -> layout (realized positions), and
# views(layout) -> the views for that layout. NOMINAL layouts give the
# static STATIONS[...]["views"].

# leak_pair ----------------------------------------------------------------------
PAIR_HALF_X   = 4.05          # interior x in [-4.05, 4.05]
PAIR_DEPTH    = 4.0
ROOM_H        = 3.0
SHARED_T      = 0.1
PAIR_LIGHT_I  = 8.0


def build_pair(c, prefix, parent, zc, shared_x, mats):
    lo = [-PAIR_HALF_X, 0.0, zc - 0.5 * PAIR_DEPTH]
    hi = [ PAIR_HALF_X, ROOM_H, zc + 0.5 * PAIR_DEPTH]
    group = c.group(prefix, [0.0, 0.0, zc], parent_node_id=parent)
    room(c, prefix, group, lo, hi, mats)
    box(c, f"{prefix} Shared Wall",
        [shared_x - 0.5 * SHARED_T, 0.0, lo[2]], [shared_x + 0.5 * SHARED_T, ROOM_H, hi[2]],
        mats["default"], group)
    point(c, f"{prefix} Light", [-2.05 - 0.0, 2.2, zc], PAIR_LIGHT_I)
    return group


def pair_views(prefix, zc, shared_x, suffix=""):
    wall_a = shared_x - 0.5 * SHARED_T
    wall_b = shared_x + 0.5 * SHARED_T
    target_y = 1.0
    # The cameras do not follow the shared wall (probe_offset_sweep moves
    # it), so the floor rectangles stay put; only the wall rectangle moves.
    va = view(f"room_a{suffix}", [-3.85, 2.5, zc + 1.85], [0.0, target_y, zc - 0.6], 70.0, [
        rect("a_floor", f"{prefix} Floor", "mean_luminance", floor_quad(-3.2, -1.6, zc - 1.2, zc + 0.2)),
        rect("a_shared_wall", f"{prefix} Shared Wall", "mean_luminance",
             wall_x_quad(wall_a, 0.4, 2.4, zc - 1.6, zc + 1.0)),
    ])
    vb = view(f"room_b{suffix}", [3.85, 2.5, zc + 1.85], [0.0, target_y, zc - 0.6], 70.0, [
        rect("b_floor", f"{prefix} Floor", "mean_luminance", floor_quad(2.0, 3.4, zc - 1.2, zc + 0.2)),
        rect("b_shared_wall", f"{prefix} Shared Wall", "mean_luminance",
             wall_x_quad(wall_b, 0.4, 2.4, zc - 1.6, zc + 1.0)),
    ])
    return [va, vb]


def pair_samples(zc, shared_x, prefix=""):
    """Room A / room B sample groups of one leak pair: the floor (5 x 5,
    0.4 m clear of every wall) and the shared wall's face on each side."""
    wall_a = shared_x - 0.5 * SHARED_T
    wall_b = shared_x + 0.5 * SHARED_T
    inset  = 0.4
    z0, z1 = zc - 0.5 * PAIR_DEPTH + inset, zc + 0.5 * PAIR_DEPTH - inset
    return {
        f"{prefix}a_floor": floor_points(-PAIR_HALF_X + inset, wall_a - inset, z0, z1),
        f"{prefix}b_floor": floor_points(wall_b + inset, PAIR_HALF_X - inset, z0, z1),
        f"{prefix}a_wall":  wall_x_points(wall_a, -1, 0.3, ROOM_H - 0.3, z0, z1),
        f"{prefix}b_wall":  wall_x_points(wall_b, +1, 0.3, ROOM_H - 0.3, z0, z1),
    }


def samples_leak_pair(layout):
    return pair_samples(0.0, layout["shared_x"])


def build_leak_pair(c, root):
    build_pair(c, "Pair", root, 0.0, 0.0, white_materials(c))
    return {"shared_x": 0.0}


def views_leak_pair(layout):
    return pair_views("Pair", 0.0, layout["shared_x"])


# probe_offset_sweep ---------------------------------------------------------------
SWEEP_PITCH    = PAIR_DEPTH + 2.0 * T + 0.0   # pairs sit wall to wall along z
SWEEP_OFFSETS  = [0.0, 0.25, 0.5]              # wall centre -> nearest probe plane, in spacings
SWEEP_ANNEX_Z  = 3.0 * SWEEP_PITCH
CRAWL_H        = 0.6
SLAB_T         = 0.2
SLAB_GAP_X     = 3.05                          # raised floor spans x < this
PILLAR_SIDE    = 0.3
PILLAR_NOMINAL = [-1.0, SWEEP_ANNEX_Z]


def sweep_pair_prefix(index):
    return f"Pair {index}"


def build_probe_offset_sweep(c, root):
    mats = white_materials(c)
    for index in range(len(SWEEP_OFFSETS)):
        build_pair(c, sweep_pair_prefix(index), root, index * SWEEP_PITCH, 0.0, mats)

    # Annex: one 8.1 x 3 x 4 m room with a raised floor over a 0.6 m crawl
    # space; the slab stops 1 m short of the +X wall, so light reaches the
    # crawl space only through that gap. A 0.3 m pillar stands on the slab.
    zc = SWEEP_ANNEX_Z
    lo = [-PAIR_HALF_X, 0.0, zc - 0.5 * PAIR_DEPTH]
    hi = [ PAIR_HALF_X, ROOM_H, zc + 0.5 * PAIR_DEPTH]
    annex = c.group("Annex", [0.0, 0.0, zc], parent_node_id=root)
    room(c, "Annex", annex, lo, hi, mats)
    box(c, "Annex Raised Floor", [lo[0], CRAWL_H, lo[2]], [SLAB_GAP_X, CRAWL_H + SLAB_T, hi[2]],
        mats["default"], annex)
    slab_top = CRAWL_H + SLAB_T
    px, pz = PILLAR_NOMINAL
    box(c, "Annex Pillar", [px - 0.5 * PILLAR_SIDE, slab_top, pz - 0.5 * PILLAR_SIDE],
        [px + 0.5 * PILLAR_SIDE, ROOM_H, pz + 0.5 * PILLAR_SIDE], mats["default"], annex)
    # Over the gap: direct light falls through it onto the crawl space floor
    # and the +X wall below the slab; deeper in, the crawl space is lit
    # indirectly only.
    point(c, "Annex Light", [3.55, 2.4, zc], PAIR_LIGHT_I)
    c.settle()

    # Read the fitted grid, then move each shared wall and the pillar onto
    # their probe-relative positions. The station's outer shell does not
    # move, so the content AABB - and with it the grid - stays the same.
    reset_ddgi_volume(c)
    set_ddgi(c, True)
    grid = read_grid(c)
    ox, _, oz = grid["origin"]
    sx, _, sz = grid["spacing"]

    def nearest_plane(value, origin, spacing):
        return origin + round((value - origin) / spacing) * spacing

    walls = []
    for index, offset in enumerate(SWEEP_OFFSETS):
        plane = nearest_plane(0.0, ox, sx)
        x = plane + offset * sx
        zc_i = index * SWEEP_PITCH
        c.set_node_transform(f"{sweep_pair_prefix(index)} Shared Wall", translation=[x, 0.5 * ROOM_H, zc_i])
        k = (x - ox) / sx
        realized = abs(k - round(k))
        walls.append({"pair": sweep_pair_prefix(index), "shared_x": x,
                      "requested_offset": offset, "realized_offset": round(realized, 4)})
    pillar_x = nearest_plane(PILLAR_NOMINAL[0], ox, sx)
    pillar_z = nearest_plane(PILLAR_NOMINAL[1], oz, sz)
    c.set_node_transform("Annex Pillar", translation=[pillar_x, 0.5 * (slab_top + ROOM_H), pillar_z])
    c.settle()
    time.sleep(0.3)
    after = read_grid(c)
    same = (after["counts"] == grid["counts"]) and all(
        abs(a - b) < 1e-4 for a, b in zip(after["origin"] + after["spacing"], grid["origin"] + grid["spacing"]))
    if not same:
        raise RuntimeError(f"probe_offset_sweep: grid changed by the wall moves: {grid} -> {after}")
    return {"walls": walls, "pillar": [pillar_x, pillar_z], "grid": grid, "grid_unchanged": same}


def views_probe_offset_sweep(layout):
    views = []
    for index, wall in enumerate(layout["walls"]):
        views += pair_views(wall["pair"], index * SWEEP_PITCH, wall["shared_x"], suffix=f"_{index}")
    zc = SWEEP_ANNEX_Z
    views.append(view("crawl", [-3.95, 0.5, zc], [0.5, 0.0, zc], 80.0, [
        rect("crawl_floor", "Annex Floor", "min_vs_median", floor_quad(-3.0, -0.5, zc - 1.2, zc + 1.2)),
        rect("crawl_floor_mean", "Annex Floor", "mean_luminance", floor_quad(-3.0, -0.5, zc - 1.2, zc + 1.2)),
    ]))
    px, pz = layout["pillar"]
    slab_top = CRAWL_H + SLAB_T
    h = 0.5 * PILLAR_SIDE
    views.append(view("pillar", [px - 1.5, 2.2, pz + 1.2], [px, 1.1, pz], 70.0, [
        rect("pillar_face", "Annex Pillar", "min_vs_median", wall_z_quad(pz + h, px - 0.1, px + 0.1, slab_top + 0.2, 1.9)),
        rect("pillar_base", "Annex Raised Floor", "min_vs_median",
             floor_quad(px - 1.0, px - h - 0.05, pz - 0.6, pz + 0.9, y=slab_top)),
    ]))
    return views


def samples_probe_offset_sweep(layout):
    """Per pair i the leak groups prefixed "pair<i>_"; the annex adds the
    pillar's four faces, the raised floor ring around the pillar base and the
    crawl-space floor deep under the slab (the plan's rectangle region)."""
    groups = {}
    for index, wall in enumerate(layout["walls"]):
        groups.update(pair_samples(index * SWEEP_PITCH, wall["shared_x"], prefix=f"pair{index}_"))
    zc = SWEEP_ANNEX_Z
    px, pz = layout["pillar"]
    slab_top = CRAWL_H + SLAB_T
    h = 0.5 * PILLAR_SIDE
    y0, y1 = slab_top + 0.2, ROOM_H - 0.4
    face = []
    face += wall_z_points(pz + h, +1, px - h + 0.05, px + h - 0.05, y0, y1, nx=3, ny=5)
    face += wall_z_points(pz - h, -1, px - h + 0.05, px + h - 0.05, y0, y1, nx=3, ny=5)
    face += wall_x_points(px + h, +1, y0, y1, pz - h + 0.05, pz + h - 0.05, nz=3, ny=5)
    face += wall_x_points(px - h, -1, y0, y1, pz - h + 0.05, pz + h - 0.05, nz=3, ny=5)
    base = []
    for d in (0.1, 0.3, 0.6):
        r = h + d
        base += floor_points(px - h + 0.05, px + h - 0.05, pz + r, pz + r, nx=3, nz=1, y=slab_top)
        base += floor_points(px - h + 0.05, px + h - 0.05, pz - r, pz - r, nx=3, nz=1, y=slab_top)
        base += floor_points(px + r, px + r, pz - h + 0.05, pz + h - 0.05, nx=1, nz=3, y=slab_top)
        base += floor_points(px - r, px - r, pz - h + 0.05, pz + h - 0.05, nx=1, nz=3, y=slab_top)
    groups["pillar_faces"] = face
    groups["pillar_base"]  = base
    groups["crawl_floor"]  = floor_points(-3.0, -0.5, zc - 1.2, zc + 1.2)
    return groups


SWEEP_NOMINAL_LAYOUT = {
    "walls": [{"pair": sweep_pair_prefix(i), "shared_x": 0.0, "requested_offset": o, "realized_offset": None}
              for i, o in enumerate(SWEEP_OFFSETS)],
    "pillar": list(PILLAR_NOMINAL),
}


# cornell ---------------------------------------------------------------------------
CORNELL_HALF    = 1.5
CORNELL_LIGHT   = [0.0, 2.9, 0.0]
CORNELL_LIGHT_I = 12.0
RED   = [0.8, 0.08, 0.08]
GREEN = [0.08, 0.8, 0.08]


def cornell_materials(c):
    mats = white_materials(c)
    mats["Wall -X"] = c.ensure_material("gi red",   base_color=RED,   metallic=0.0, roughness=0.9)
    mats["Wall +X"] = c.ensure_material("gi green", base_color=GREEN, metallic=0.0, roughness=0.9)
    return mats


def build_cornell_room(c, root, skip=()):
    lo = [-CORNELL_HALF, 0.0, -CORNELL_HALF]
    hi = [ CORNELL_HALF, 3.0,  CORNELL_HALF]
    group = c.group("Cornell", [0.0, 0.0, 0.0], parent_node_id=root)
    mats = cornell_materials(c)
    room(c, "Cornell", group, lo, hi, mats, skip=skip)
    # Cone half angle 45 deg: the whole floor and the coloured walls below
    # 1.4 m are lit directly, so a single bounce off the walls already
    # carries their colour onto the floor strips (a cone that stops short of
    # the walls leaves the colour to the second bounce).
    spot(c, "Cornell Light", CORNELL_LIGHT, [0.0, 0.0, 0.0], CORNELL_LIGHT_I, 90.0, 70.0)
    return group, mats


def build_cornell(c, root):
    build_cornell_room(c, root)
    return {}


def cornell_main_view(extra_rects=(), back_wall=True):
    rects = [
        rect("red_strip",   "Cornell Floor", "mean_rgb", floor_quad(-1.45, -0.85, -1.4, -0.7)),
        rect("green_strip", "Cornell Floor", "mean_rgb", floor_quad( 0.85,  1.45, -1.4, -0.7)),
        rect("floor_center", "Cornell Floor", "mean_luminance", floor_quad(-0.5, 0.5, -0.9, 0.1)),
    ]
    if back_wall:
        rects.append(rect("back_wall", "Cornell Wall -Z", "temporal_stddev",
                          wall_z_quad(-CORNELL_HALF, -0.8, 0.8, 1.2, 2.5)))
    rects += list(extra_rects)
    return view("main", [0.0, 1.7, 1.4], [0.0, 0.9, -1.5], 75.0, rects)


def views_cornell(layout):
    del layout
    return [cornell_main_view()]


def cornell_floor_samples():
    """Floor strips next to the red (-X) and green (+X) walls and the floor
    centre of the Cornell room."""
    return {
        "red_strip":   floor_points(-CORNELL_HALF + 0.15, -CORNELL_HALF + 0.55, -1.3, 1.3, nx=3, nz=7),
        "green_strip": floor_points( CORNELL_HALF - 0.55,  CORNELL_HALF - 0.15, -1.3, 1.3, nx=3, nz=7),
        "floor":       floor_points(-1.2, 1.2, -1.2, 1.2),
    }


def samples_cornell(layout):
    del layout
    groups = cornell_floor_samples()
    groups["back_wall"] = wall_z_points(-CORNELL_HALF, +1, -1.2, 1.2, 0.4, 2.6)
    return groups


# emissive_only -----------------------------------------------------------------------
EMISSIVE_HALF   = 2.0
EMISSIVE_PANELS = [("Panel 1.0", 1.0, -1.2), ("Panel 0.25", 0.25, 0.0), ("Panel 0.05", 0.05, 1.2)]
EMISSIVE_Y      = 0.8
EMISSIVE_VALUE  = 4.0


def build_emissive_only(c, root):
    lo = [-EMISSIVE_HALF, 0.0, -EMISSIVE_HALF]
    hi = [ EMISSIVE_HALF, 3.0,  EMISSIVE_HALF]
    group = c.group("Emissive Room", [0.0, 0.0, 0.0], parent_node_id=root)
    mats = white_materials(c)
    room(c, "Emissive Room", group, lo, hi, mats)
    glow = c.ensure_material("gi emitter", base_color=[0.05, 0.05, 0.05], metallic=0.0, roughness=0.9,
                             emissive=[EMISSIVE_VALUE] * 3)
    for name, side, x in EMISSIVE_PANELS:
        box(c, name, [x - 0.5 * side, EMISSIVE_Y - 0.5 * side, lo[2]],
            [x + 0.5 * side, EMISSIVE_Y + 0.5 * side, lo[2] + 0.02], glow, group)
    return {}


def views_emissive_only(layout):
    del layout
    z_wall = -EMISSIVE_HALF
    rects = []
    for name, side, x in EMISSIVE_PANELS:
        tag = name.split()[1]
        rects.append(rect(f"floor_{tag}", "Emissive Room Floor", "mean_luminance",
                          floor_quad(x - 0.3, x + 0.3, z_wall + 0.1, z_wall + 0.7)))
    rects.append(rect("room_floor", "Emissive Room Floor", "mean_luminance",
                      floor_quad(-1.5, 1.5, -1.2, 0.0)))
    rects.append(rect("floor_1.0_noise", "Emissive Room Floor", "temporal_stddev",
                      floor_quad(-1.5, -0.9, z_wall + 0.1, z_wall + 0.7)))
    return [view("main", [0.0, 1.9, 1.85], [0.0, 0.5, -2.0], 70.0, rects)]


def samples_emissive_only(layout):
    """floor_<side>: the floor right in front of each panel; room_floor: the
    whole floor (its median is the reference level)."""
    del layout
    z_wall = -EMISSIVE_HALF
    groups = {}
    for name, side, x in EMISSIVE_PANELS:
        tag = name.split()[1]
        groups[f"floor_{tag}"] = floor_points(x - 0.3, x + 0.3, z_wall + 0.1, z_wall + 0.6, nx=3, nz=3)
    groups["room_floor"] = floor_points(-EMISSIVE_HALF + 0.3, EMISSIVE_HALF - 0.3,
                                        -EMISSIVE_HALF + 0.3, EMISSIVE_HALF - 0.3, nx=7, nz=7)
    return groups


# corridor ----------------------------------------------------------------------------
CORRIDOR_HALF_W = 0.75
CORRIDOR_H      = 2.5
CORRIDOR_LEN    = 24.0
CORRIDOR_LIGHT  = [0.0, 1.8, 1.2]
CORRIDOR_LIGHT_I = 150.0
CORRIDOR_SAMPLES = [2.0 + 2.0 * i for i in range(9)]    # z of the profile samples


def build_corridor(c, root):
    lo = [-CORRIDOR_HALF_W, 0.0, 0.0]
    hi = [ CORRIDOR_HALF_W, CORRIDOR_H, CORRIDOR_LEN]
    group = c.group("Corridor", [0.0, 0.0, 0.5 * CORRIDOR_LEN], parent_node_id=root)
    room(c, "Corridor", group, lo, hi, white_materials(c))
    # Aimed at the end wall: the lit end wall is the source the far corridor
    # sees; the floor from z = 2 m on is lit indirectly only.
    spot(c, "Corridor Light", CORRIDOR_LIGHT, [0.0, 1.4, 0.0], CORRIDOR_LIGHT_I, 90.0, 70.0, light_range=10.0)
    return {}


def views_corridor(layout):
    del layout
    rects = []
    for i, z in enumerate(CORRIDOR_SAMPLES):
        rects.append(rect(f"profile_{i:02d}", "Corridor Floor", "profile_sample",
                          floor_quad(-0.3, 0.3, z - 0.5, z + 0.5), profile="floor_centre_line", z=z))
    rects.append(rect("end_wall", "Corridor Wall -Z", "mean_luminance",
                      wall_z_quad(0.0, -0.5, 0.5, 0.6, 2.0)))
    return [view("main", [0.0, 2.2, CORRIDOR_LEN - 0.2], [0.0, 0.6, 11.0], 50.0, rects)]


CORRIDOR_PROFILE_Z = [2.0 + 1.0 * i for i in range(21)]    # z of the floor profile points


def samples_corridor(layout):
    """profile: the floor centre line from z = 2 m to 22 m, 1 m apart, in
    order of distance from the lit end wall."""
    del layout
    return {
        "profile": [{"position": [0.0, 0.0, z], "normal": [0.0, 1.0, 0.0]} for z in CORRIDOR_PROFILE_Z],
    }


# courtyard ---------------------------------------------------------------------------
COURTYARD_HALF    = 4.0
COURTYARD_H       = 3.0
COURTYARD_AMBIENT = [0.08, 0.1, 0.14]
SUN_INTENSITY     = 3.0


def build_courtyard(c, root):
    lo = [-COURTYARD_HALF, 0.0, -COURTYARD_HALF]
    hi = [ COURTYARD_HALF, COURTYARD_H, COURTYARD_HALF]
    group = c.group("Courtyard", [0.0, 0.0, 0.0], parent_node_id=root)
    room(c, "Courtyard", group, lo, hi, white_materials(c), skip=("Ceiling",))
    # Sun from +Z, 45 degrees up: the +Z wall's inner face and the floor
    # strip at its foot are in shadow and see only sky + bounce.
    c.light("directional", "Sun", [0.0, 8.0, 8.0], [1.0, 0.98, 0.94], SUN_INTENSITY, cast_shadow=True)
    c.set_node_transform("Sun", rotation_xyzw=spot_rotation([0.0, 8.0, 8.0], [0.0, 0.0, 0.0]))
    return {}


def views_courtyard(layout):
    del layout
    return [view("main", [0.0, 1.6, -3.6], [0.0, 1.2, 4.0], 70.0, [
        rect("shadowed_wall", "Courtyard Wall +Z", "mean_luminance",
             wall_z_quad(COURTYARD_HALF, -2.0, 2.0, 0.8, 2.6)),
        rect("shadowed_floor", "Courtyard Floor", "mean_luminance",
             floor_quad(-2.0, 2.0, 2.2, 3.6)),
        rect("sunlit_floor", "Courtyard Floor", "mean_luminance",
             floor_quad(-2.0, 2.0, -1.5, 0.5)),
    ])]


def samples_courtyard(layout):
    del layout
    return {
        "shadowed_wall":  wall_z_points(COURTYARD_HALF, -1, -3.0, 3.0, 0.5, 2.6),
        "shadowed_floor": floor_points(-3.0, 3.0, 2.6, 3.6, nx=5, nz=3),
        "sunlit_floor":   floor_points(-3.0, 3.0, -2.0, 0.0, nx=5, nz=3),
    }


# dynamic -----------------------------------------------------------------------------
DOOR_W, DOOR_H, DOOR_T = 1.0, 2.0, 0.1
SIDE_DEPTH  = 2.0
LIGHT_MOVE  = [0.0, 0.0, 1.0]


def build_dynamic(c, root):
    group, mats = build_cornell_room(c, root, skip=("Wall -Z",))
    white = mats["default"]
    z0, z1 = -CORNELL_HALF - T, -CORNELL_HALF
    # Back wall with a door opening, and a pocket door that slides into the
    # right-hand wall piece.
    box(c, "Cornell Wall -Z Left",  [-CORNELL_HALF, 0.0, z0], [-0.5 * DOOR_W, 3.0, z1], white, group)
    box(c, "Cornell Wall -Z Right", [0.5 * DOOR_W, 0.0, z0], [CORNELL_HALF, 3.0, z1], white, group)
    box(c, "Cornell Wall -Z Lintel", [-0.5 * DOOR_W, DOOR_H, z0], [0.5 * DOOR_W, 3.0, z1], white, group)
    zc = 0.5 * (z0 + z1)
    box(c, "Door", [-0.5 * DOOR_W, 0.0, zc - 0.5 * DOOR_T], [0.5 * DOOR_W, DOOR_H, zc + 0.5 * DOOR_T], white, group)
    # Dark side room behind the door, sharing the back wall.
    side = c.group("Side Room", [0.0, 0.0, z0 - 0.5 * SIDE_DEPTH], parent_node_id=root)
    room(c, "Side Room", side, [-CORNELL_HALF, 0.0, z0 - SIDE_DEPTH], [CORNELL_HALF, 3.0, z0],
         {"default": white}, skip=("Wall +Z",))
    return {"door_closed_x": 0.0, "light": list(CORNELL_LIGHT)}


def views_dynamic(layout):
    del layout
    z_back = -CORNELL_HALF - T
    main = cornell_main_view(back_wall=False)
    # From the side room's far wall toward the door.
    side = view("side_room", [0.0, 2.7, z_back - SIDE_DEPTH + 0.1], [0.0, 0.3, z_back], 70.0, [
        rect("side_floor", "Side Room Floor", "mean_luminance",
             floor_quad(-1.0, 1.0, z_back - 1.2, z_back - 0.2)),
    ])
    return [main, side]


def samples_dynamic(layout):
    """The Cornell floor groups plus the side room floor behind the door."""
    del layout
    z_back = -CORNELL_HALF - T
    groups = cornell_floor_samples()
    groups["side_floor"] = floor_points(-1.2, 1.2, z_back - SIDE_DEPTH + 0.3, z_back - 0.3)
    return groups


def event_move_light(c, info):
    """Move the Cornell spot 1 m toward the front wall (+Z)."""
    del info
    c.set_node_transform("Cornell Light", translation=v_add(CORNELL_LIGHT, LIGHT_MOVE))


def event_open_door(c, info):
    """Slide the door fully open into the right-hand wall piece (+X)."""
    del info
    zc = -CORNELL_HALF - 0.5 * T
    c.set_node_transform("Door", translation=[DOOR_W, 0.5 * DOOR_H, zc])


# --- station table -------------------------------------------------------------------

def _station(description, build, views, samples, ambient=BLACK, nominal_layout=None, events=None,
             event_groups=None):
    return {
        "description": description,
        "build": build,
        "views_for": views,
        "views": resolve_views(views(nominal_layout or {"shared_x": 0.0})),
        "samples_for": samples,
        "ambient": list(ambient),
        "events": events or {},
        "event_groups": event_groups or {},
    }


STATIONS = {
    "leak_pair": _station(
        "Two 4 x 3 x 4 m rooms sharing one 0.1 m wall, point light in room A only: "
        "room B mean luminance relative to room A (leak through a wall thinner than the probe spacing).",
        build_leak_pair, views_leak_pair, samples_leak_pair),
    "probe_offset_sweep": _station(
        "Three leak pairs whose shared walls sit 0.0 / 0.25 / 0.5 probe spacings from the nearest probe "
        "plane, plus an annex with a 0.6 m crawl space under a raised floor and a 0.3 m pillar centred on "
        "a probe: leak ratio per offset, dark splotches at the pillar and in the crawl space.",
        build_probe_offset_sweep, views_probe_offset_sweep, samples_probe_offset_sweep,
        nominal_layout=SWEEP_NOMINAL_LAYOUT),
    "cornell": _station(
        "3 x 3 x 3 m room, red -X wall, green +X wall, spot light aimed at the floor: red / green ratio "
        "of the floor strip next to each coloured wall; back wall temporal noise.",
        build_cornell, views_cornell, samples_cornell),
    "emissive_only": _station(
        "Closed room without analytic lights, emissive panels of 1.0 / 0.25 / 0.05 m side on the back "
        "wall: floor luminance in front of each panel, temporal noise.",
        build_emissive_only, views_emissive_only, samples_emissive_only),
    "corridor": _station(
        "1.5 x 2.5 x 24 m corridor, spot light on the end wall at z = 0: floor luminance profile along "
        "the centre line (monotonic falloff, no steps).",
        build_corridor, views_corridor, samples_corridor),
    "courtyard": _station(
        "Walled 8 x 8 m yard, open top, sun from +Z at 45 degrees, non-black ambient as sky: shadowed "
        "wall luminance.",
        build_courtyard, views_courtyard, samples_courtyard, ambient=COURTYARD_AMBIENT),
    "dynamic": _station(
        "cornell plus a dark side room behind a 1 x 2 m pocket door; events move the light 1 m and slide "
        "the door open: frames until mean luminance settles after each event.",
        build_dynamic, views_dynamic, samples_dynamic,
        events={"move_light": event_move_light, "open_door": event_open_door},
        event_groups={"move_light": ["floor", "red_strip", "green_strip"], "open_door": ["side_floor"]}),
}


def set_headlight(c, enabled):
    """Graphics_settings::headlight_when_unlit (app-wide): a scene without
    lights (emissive_only) is otherwise lit by each viewport's own camera
    headlight. Returns the previous value, for the caller to restore."""
    previous = c.call("set_graphics_settings", {}).get("headlight_when_unlit", True)
    c.mutate("set_graphics_settings", {"headlight_when_unlit": bool(enabled)})
    return previous


def build_station(c, name, ddgi=True):
    """Close every open scene, build station `name` in a fresh scene and
    return its info: {"station", "scene", "layout", "views", "samples",
    "grid"}. With ddgi=True DDGI is enabled on the fitted grid when this
    returns; otherwise it is disabled (probe_offset_sweep always enables it while it
    places its walls, so both variants get the same wall positions).

    Turns the app-wide unlit-scene headlight off (no station may be lit by
    anything but its own lights and emitters); info["headlight_before"] is
    the previous value - restore it with set_headlight() when done."""
    station = STATIONS[name]
    c.close_all_scenes()
    c.new_scene()
    headlight_before = set_headlight(c, False)
    reset_ddgi_volume(c)
    c.ambience(ambient=station["ambient"], clear_color=[0.0, 0.0, 0.0, 1.0], grid=False, sky=False)
    c.shadow_range(30.0, z_far=60.0)
    root = c.group(f"GI {name}", [0.0, 0.0, 0.0])
    layout = station["build"](c, root)
    c.settle()
    views = resolve_views(station["views_for"](layout if layout else {"shared_x": 0.0}))
    samples = station["samples_for"](layout if layout else {"shared_x": 0.0})
    info = {"station": name, "scene": c.scene, "layout": layout, "views": views, "samples": samples, "grid": None,
            "viewport": viewport_rect(c), "headlight_before": headlight_before}
    if ddgi:
        set_ddgi(c, True)
        info["grid"] = read_grid(c)
    else:
        set_ddgi(c, False)
    return info


def place_view(c, v):
    cameras = c.call("get_scene_cameras", {"scene_name": c.scene}).get("cameras", [])
    c.mutate("edit_camera", {"scene_name": c.scene, "camera_id": cameras[0]["id"],
                             "fov_y": math.radians(v["fov_y_deg"]), "z_near": 0.02})
    c.place_camera(v["eye"], v["target"])


def viewport_rect(c):
    """[x, y, width, height] in pixels of the station scene's viewport inside
    capture_screenshot's image (the capture is the whole editor window; the
    rectangle fractions are relative to this viewport region)."""
    for vp in c.call("get_viewports").get("viewports", []):
        if vp.get("scene") == c.scene:
            return [int(vp["x"]), int(vp["y"]), int(vp["width"]), int(vp["height"])]
    raise RuntimeError("no viewport for the station scene")


def rect_pixels(fraction_rect, viewport):
    """Rectangle fractions -> [x0, y0, x1, y1] pixels of the captured image."""
    vx, vy, vw, vh = viewport
    x0, y0, x1, y1 = fraction_rect
    return [round(vx + x0 * vw), round(vy + y0 * vh), round(vx + x1 * vw), round(vy + y1 * vh)]


def live_aspect(c):
    _, _, width, height = viewport_rect(c)
    return float(width) / float(height)


def check_rects(c, info):
    """Raycast the centre and four inset corners of every rectangle through
    the LIVE camera model and assert each ray hits the rectangle's surface.
    Returns a list of failure strings (empty = all on target)."""
    aspect = live_aspect(c)
    failures = []
    for v in info["views"]:
        queries = []
        labels = []
        for r in v["rects"]:
            x0, y0, x1, y1 = r["rect"]
            ix, iy = 0.08 * (x1 - x0), 0.08 * (y1 - y0)
            for fx, fy in ((0.5 * (x0 + x1), 0.5 * (y0 + y1)), (x0 + ix, y0 + iy), (x1 - ix, y0 + iy),
                           (x0 + ix, y1 - iy), (x1 - ix, y1 - iy)):
                queries.append({"type": "raycast", "origin": v["eye"],
                                "direction": pixel_ray(v, fx, fy, aspect), "max_distance": 100.0})
                labels.append(r)
        results = c.geometry_query(queries)
        for r, hit in zip(labels, results):
            got = hit.get("node_name") if hit.get("hit") else None
            if got != r["surface"]:
                failures.append(f"{info['station']}/{v['name']}/{r['name']}: hit {got!r}, expected {r['surface']!r}")
    return failures


def draw_rects(png_path, v, viewport, out_path):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print(f"Pillow not available - rects for {v['name']}: " +
              ", ".join(f"{r['name']}={rect_pixels(r['rect'], viewport)}" for r in v["rects"]))
        return
    image = Image.open(png_path).convert("RGB")
    draw = ImageDraw.Draw(image)
    vx, vy, vw, vh = viewport
    draw.rectangle([vx, vy, vx + vw - 1, vy + vh - 1], outline=(0, 255, 255), width=1)
    for r in v["rects"]:
        box_px = rect_pixels(r["rect"], viewport)
        draw.rectangle(box_px, outline=(255, 0, 255), width=2)
        draw.text((box_px[0] + 3, box_px[1] + 2), r["name"], fill=(255, 255, 0))
    image.save(out_path)


# --- CLI -------------------------------------------------------------------------------

def run_station(c, name, report):
    print(f"=== station {name}")
    info = build_station(c, name, ddgi=True)
    grid = info["grid"]
    line = f"{name}: grid counts {grid['counts']} spacing {[round(s, 3) for s in grid['spacing']]}"
    if name == "probe_offset_sweep":
        layout = info["layout"]
        offsets = [w["realized_offset"] for w in layout["walls"]]
        line += f", realized offsets {offsets}, pillar {[round(v, 3) for v in layout['pillar']]}, grid unchanged {layout['grid_unchanged']}"
    updates = wait_ddgi_updates(c)
    line += f", converge updates {updates}"
    with open(f"{BASE}_{name}_info.json", "w", encoding="utf-8") as info_file:
        json.dump(info, info_file, indent=1)
    failures = []
    for v in info["views"]:
        place_view(c, v)
        png = f"{BASE}_{name}_{v['name']}.png"
        c.screenshot(png)
        draw_rects(png, v, info["viewport"], f"{BASE}_{name}_{v['name']}_rects.png")
    failures += check_rects(c, info)
    if name == "dynamic":
        for event_name, event in STATIONS[name]["events"].items():
            event(c, info)
            c.settle()
            wait_ddgi_updates(c, updates=200)
            v = info["views"][1] if event_name == "open_door" else info["views"][0]
            place_view(c, v)
            c.screenshot(f"{BASE}_{name}_after_{event_name}_{v['name']}.png")
        line += f", events {list(STATIONS[name]['events'])} ok"
    # One capture of the first view with DDGI off (the flat ambient term).
    set_ddgi(c, False)
    c.settle()
    time.sleep(0.3)
    v = info["views"][0]
    place_view(c, v)
    c.screenshot(f"{BASE}_{name}_{v['name']}_ddgi_off.png")
    line += f", rect check {'ok' if not failures else 'FAILED'}"
    report.append(line)
    for failure in failures:
        report.append("  " + failure)
    return not failures


def add_script_arguments(parser):
    parser.add_argument("--station", default="all",
                        help="station name or 'all' (" + ", ".join(STATIONS) + ")")


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
    ok = True
    headlight = c.call("set_graphics_settings", {}).get("headlight_when_unlit", True)
    with fail_soft(c, BASE):
        try:
            for name in names:
                ok = run_station(c, name, report) and ok
            c.close_all_scenes()
        finally:
            set_headlight(c, headlight)
    print("\n".join(report))
    print(f"{TITLE}: {'all rectangles on target' if ok else 'RECTANGLE CHECK FAILED'}")
    if not ok:
        sys.exit(1)


if __name__ == "__main__":
    main()
