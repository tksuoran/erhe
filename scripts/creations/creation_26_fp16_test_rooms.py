#!/usr/bin/env python3
"""Creation 26 - fp16 Test Rooms.

A test ASSET for the standard shader's fp16 paths (doc/plans/shader_fp16.md
sections 2 - 4): every station is its own scene with at most one light (the
low-end configuration), black ambient unless the station sets one, no
textures, and the camera given per case to render_scene_image, so the fp32
chain (shader_fp16_paths 0) and each fp16 mask render the same pixels and
scripts/shader_fp16_verify.py measures their distance.

The module is the DATA SOURCE of scripts/shader_fp16_verify.py, which
imports it. Importing it has no side effects. Everything is plain Python
data:

  STATIONS[name] = {
      "description": what the station measures,
      "paths":       the fp16 paths it exercises (P1..P5),
      "build":       build(c) builds the station in the open scene of
                     Creation c (materials, meshes, the light),
      "cases":       [case, ...]
  }
  case = {"name", "light": light or None, "camera": render_scene_image
          camera dict (eye, target, up, fov_y_degrees, near, far,
          exposure), "gated": bool (False = the case is reported but no
          gate applies: it shows what the fp16 path loses by design),
          "note": why it is not gated}
  light = {"type": "directional" | "spot" | "point", "position",
           "direction" (the way the light shines), "intensity",
           "range", "outer_spot_angle_deg", "inner_spot_angle_deg",
           "cast_shadow"}
         apply_light(c, light) switches the station light to it.

Stations (plan section 4):
  roughness_metallic (A): spheres over roughness 0.02 .. 1.0 and metallic
      0 / 1 in gray and red, lit from 15, 45 and 85 degrees off the view
      direction. The columns below the fp16 roughness floor (0.09) are a
      separate, ungated case: the fp16 material path raises them to the
      floor by design.
  grazing (B): one large sphere with the light from the side (N.L sweeps 1
      to 0 across the terminator) and from behind the view (the lit
      crescent is seen at grazing view angles).
  hdr_ramp (C): a white floor under a directional light whose radiance
      steps 2^-12 .. 2^14, rendered at exposures 2^-8, 1 and 2^4, and
      emissive strips over 2^-12 .. 2^10 beside it.
  dark_room (D): ambient 2^-10 on base colors 2^-6 .. 2^-2 with a 2^-6
      light; every lit pixel sits near the fp16 denormal range.
  attenuation (E): a point light 0.1 m .. 500 m above a floor, and a spot
      light with 1 .. 89 degree cones.

CLI: --station <name> | all. Default: build each station and render every
case (PNG) with render_scene_image into
logs/creations/fp16_test_rooms_<station>_<case>.png.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))

from common import (  # noqa: E402
    Creation,
    standard_args,
    fail_soft,
    v_sub,
    v_norm,
    v_scale,
)
from creation_25_shadow_test_rooms import light_rotation  # noqa: E402

TITLE      = "fp16 Test Rooms"
BASE       = "logs/creations/fp16_test_rooms"
LIGHT_NAME = "fp16 Light"
IMAGE_W    = 768
IMAGE_H    = 768


# --- data helpers -------------------------------------------------------------------------------

def camera(eye, target, fov_y_deg=45.0, near=0.05, far=200.0, exposure=1.0, up=(0.0, 1.0, 0.0)):
    return {"eye": [float(v) for v in eye], "target": [float(v) for v in target],
            "up": [float(v) for v in up], "fov_y_degrees": float(fov_y_deg),
            "near": float(near), "far": float(far), "exposure": float(exposure)}


def directional(direction, intensity, position=(0.0, 5.0, 0.0), cast_shadow=True):
    return {"type": "directional", "position": list(position), "direction": v_norm(direction),
            "intensity": float(intensity), "range": 0.0, "cast_shadow": cast_shadow}


def point(position, intensity, light_range, cast_shadow=True):
    return {"type": "point", "position": list(position), "direction": [0.0, -1.0, 0.0],
            "intensity": float(intensity), "range": float(light_range), "cast_shadow": cast_shadow}


def spot(position, direction, intensity, light_range, outer_deg, inner_deg, cast_shadow=True):
    return {"type": "spot", "position": list(position), "direction": v_norm(direction),
            "intensity": float(intensity), "range": float(light_range),
            "outer_spot_angle_deg": float(outer_deg), "inner_spot_angle_deg": float(inner_deg),
            "cast_shadow": cast_shadow}


def case(name, light, cam, gated=True, note=""):
    return {"name": name, "light": light, "camera": cam, "gated": gated, "note": note}


def rotate_about(v, axis, angle_rad):
    """Rodrigues rotation of v about the unit axis."""
    axis = v_norm(axis)
    c = math.cos(angle_rad)
    s = math.sin(angle_rad)
    cross = [axis[1] * v[2] - axis[2] * v[1], axis[2] * v[0] - axis[0] * v[2], axis[0] * v[1] - axis[1] * v[0]]
    d = axis[0] * v[0] + axis[1] * v[1] + axis[2] * v[2]
    return [v[i] * c + cross[i] * s + axis[i] * d * (1.0 - c) for i in range(3)]


def light_off_view(cam, angle_deg, about=(1.0, 0.0, 0.0)):
    """A directional light whose direction toward the light is the view
    direction (from the camera target to the eye) tilted by angle_deg about
    the given axis; the light shines the opposite way."""
    to_eye = v_norm(v_sub(cam["eye"], cam["target"]))
    to_light = rotate_about(to_eye, about, math.radians(angle_deg))
    return directional(v_scale(to_light, -1.0), 3.0)


# --- station A: roughness and metallic sweep ------------------------------------------------

ROUGHNESS_ABOVE_FLOOR = [0.09, 0.13, 0.2, 0.3, 0.5, 1.0]
ROUGHNESS_BELOW_FLOOR = [0.02, 0.03, 0.05, 0.07]
GRID_ROWS = [  # (label, base color, metallic)
    ("gray_dielectric", [0.5, 0.5, 0.5], 0.0),
    ("gray_metal",      [0.5, 0.5, 0.5], 1.0),
    ("red_dielectric",  [0.8, 0.05, 0.02], 0.0),
    ("red_metal",       [0.8, 0.05, 0.02], 1.0),
]
SPHERE_RADIUS = 0.42
GRID_STEP     = 1.0
ABOVE_X0      = 0.0
BELOW_X0      = 8.0


def _grid_camera(x0, columns):
    cx = x0 + 0.5 * GRID_STEP * (columns - 1)
    cz = 0.5 * GRID_STEP * (len(GRID_ROWS) - 1)
    return camera([cx, 7.0, cz + 5.5], [cx, 0.0, cz], fov_y_deg=42.0)


def build_roughness_metallic(c, root):
    for row, (label, color, metallic) in enumerate(GRID_ROWS):
        z = row * GRID_STEP
        for x0, values in ((ABOVE_X0, ROUGHNESS_ABOVE_FLOOR), (BELOW_X0, ROUGHNESS_BELOW_FLOOR)):
            for column, roughness in enumerate(values):
                name = f"{label} r{roughness:g}"
                material = c.ensure_material(name, base_color=color, metallic=metallic, roughness=roughness)
                c.shape("uv_sphere", name, [x0 + column * GRID_STEP, SPHERE_RADIUS, z], radius=SPHERE_RADIUS,
                        slice_count=96, stack_count=48, material_name=material, motion_mode="none",
                        parent_node_id=root)


def _roughness_cases():
    cases = []
    cam_above = _grid_camera(ABOVE_X0, len(ROUGHNESS_ABOVE_FLOOR))
    cam_below = _grid_camera(BELOW_X0, len(ROUGHNESS_BELOW_FLOOR))
    for angle in (15.0, 45.0, 85.0):
        cases.append(case(f"light_{int(angle)}deg", light_off_view(cam_above, angle), cam_above))
    for angle in (15.0, 45.0):
        cases.append(case(f"below_floor_light_{int(angle)}deg", light_off_view(cam_below, angle), cam_below,
                          gated=False, note="roughness 0.02 .. 0.07 is below the fp16 material floor 0.09"))
    return cases


# --- station B: grazing ramp --------------------------------------------------------------------

def build_grazing(c, root):
    material = c.ensure_material("grazing gray", base_color=[0.6, 0.6, 0.6], metallic=0.0, roughness=0.4)
    c.shape("uv_sphere", "Grazing Sphere", [0.0, 0.0, 0.0], radius=3.0, slice_count=192, stack_count=96,
            material_name=material, motion_mode="none", parent_node_id=root)


def _grazing_cases():
    cam = camera([0.0, 0.0, 9.0], [0.0, 0.0, 0.0], fov_y_deg=42.0)
    return [
        case("side",   directional([-1.0, 0.0, 0.0], 3.0), cam),
        case("behind", light_off_view(cam, 160.0, about=(0.0, 1.0, 0.0)), cam),
    ]


# --- station C: HDR radiance ramp ---------------------------------------------------------------

RADIANCE_EXPONENTS = [-12, -8, -4, 0, 4, 8, 12, 14]
EXPOSURES          = [2.0 ** -8, 1.0, 2.0 ** 4]
EMISSIVE_EXPONENTS = list(range(-12, 11, 2))  # 12 strips, 2^-12 .. 2^10


def build_hdr_ramp(c, root):
    floor = c.ensure_material("ramp white", base_color=[1.0, 1.0, 1.0], metallic=0.0, roughness=0.5)
    c.shape("box", "Ramp Floor", [6.0, -0.05, 0.0], size=[14.0, 0.1, 4.0], steps=[0, 0, 0],
            material_name=floor, motion_mode="none", parent_node_id=root)
    for index, exponent in enumerate(EMISSIVE_EXPONENTS):
        value = 2.0 ** exponent
        material = c.ensure_material(f"emissive 2^{exponent}", base_color=[0.0, 0.0, 0.0], metallic=0.0,
                                     roughness=1.0, emissive=[value, value, value])
        c.shape("box", f"Emissive 2^{exponent}", [index * 1.0 + 0.5, 0.1, 1.2], size=[0.8, 0.1, 0.8],
                steps=[0, 0, 0], material_name=material, motion_mode="none", parent_node_id=root)


def _hdr_cases():
    cam = camera([6.0, 9.0, 4.0], [6.0, 0.0, 0.2], fov_y_deg=50.0)
    cases = []
    for exponent in RADIANCE_EXPONENTS:
        for exposure in EXPOSURES:
            cam_e = dict(cam, exposure=exposure)
            cases.append(case(f"radiance_2^{exponent}_exposure_2^{int(math.log2(exposure))}",
                              directional([0.2, -1.0, 0.1], 2.0 ** exponent), cam_e))
    return cases


# --- station D: dark room -----------------------------------------------------------------------

DARK_EXPONENTS = [-6, -5, -4, -3, -2]


def build_dark_room(c, root):
    for index, exponent in enumerate(DARK_EXPONENTS):
        value = 2.0 ** exponent
        material = c.ensure_material(f"dark 2^{exponent}", base_color=[value, value, value], metallic=0.0,
                                     roughness=0.8)
        c.shape("box", f"Dark 2^{exponent}", [index * 1.2, 0.4, 0.0], size=[0.8, 0.8, 0.8], steps=[0, 0, 0],
                material_name=material, motion_mode="none", parent_node_id=root)
    floor = c.ensure_material("dark floor", base_color=[2.0 ** -5] * 3, metallic=0.0, roughness=1.0)
    c.shape("box", "Dark Floor", [2.4, -0.05, 0.0], size=[8.0, 0.1, 4.0], steps=[0, 0, 0],
            material_name=floor, motion_mode="none", parent_node_id=root)


def _dark_cases():
    cam = camera([2.4, 4.0, 5.0], [2.4, 0.3, 0.0], fov_y_deg=45.0)
    light = directional([0.3, -1.0, -0.5], 2.0 ** -6)
    return [
        case("exposure_1",  light, cam),
        case("exposure_16", light, dict(cam, exposure=16.0)),
    ]


# --- station E: attenuation range ---------------------------------------------------------------

POINT_HEIGHTS = [0.1, 1.0, 10.0, 100.0, 500.0]
SPOT_CONES    = [1.0, 10.0, 45.0, 89.0]


def build_attenuation(c, root):
    floor = c.ensure_material("attenuation white", base_color=[0.8, 0.8, 0.8], metallic=0.0, roughness=0.6)
    c.shape("box", "Attenuation Floor", [0.0, -0.05, 0.0], size=[4000.0, 0.1, 4000.0], steps=[0, 0, 0],
            material_name=floor, motion_mode="none", parent_node_id=root)


# The fp16 attenuation path's content boundary (doc/plans/shader_fp16.md
# P4): the inverse square term 1 / (d^2 + 1) is below the fp16 normal
# range past 128 m from the light, where a flush-to-zero driver reads it as
# zero. A point light case whose visible floor reaches beyond it is not
# gated.
FP16_ATTENUATION_RANGE_M = 128.0


def _attenuation_cases():
    cases = []
    for height in POINT_HEIGHTS:
        far = 20.0 * height + 20.0
        cam = camera([0.0, 2.0 * height + 0.3, 2.0 * height + 0.3], [0.0, 0.0, 0.0], fov_y_deg=60.0,
                     near=0.02, far=far)
        beyond = (2.0 * height) > FP16_ATTENUATION_RANGE_M
        cases.append(case(f"point_{height:g}m", point([0.0, height, 0.0], 4.0 * (height * height + 1.0), 1000.0), cam,
                          gated=not beyond,
                          note="" if not beyond else f"receivers beyond {FP16_ATTENUATION_RANGE_M:g} m from the light (fp16 inverse square underflow)"))
    cam = camera([0.0, 8.0, 8.0], [0.0, 0.0, 0.0], fov_y_deg=60.0)
    for cone in SPOT_CONES:
        cases.append(case(f"spot_{cone:g}deg", spot([0.0, 5.0, 0.0], [0.0, -1.0, 0.0], 100.0, 1000.0, cone, 0.8 * cone), cam))
    return cases


# --- stations -----------------------------------------------------------------------------------

STATIONS = {
    "roughness_metallic": {
        "description": "roughness 0.02 .. 1.0 x metallic 0 / 1 spheres, gray and red, three light angles",
        "paths": "P1 P2",
        "build": build_roughness_metallic,
        "cases": _roughness_cases(),
    },
    "grazing": {
        "description": "one large sphere lit from the side and from behind the view",
        "paths": "P2",
        "build": build_grazing,
        "cases": _grazing_cases(),
    },
    "hdr_ramp": {
        "description": "radiance 2^-12 .. 2^14 at exposures 2^-8, 1, 2^4; emissive strips 2^-12 .. 2^10",
        "paths": "P1 P3",
        "build": build_hdr_ramp,
        "cases": _hdr_cases(),
    },
    "dark_room": {
        "description": "ambient 2^-10, base colors 2^-6 .. 2^-2, light 2^-6",
        "paths": "P3",
        "build": build_dark_room,
        "cases": _dark_cases(),
    },
    "attenuation": {
        "description": "point light 0.1 .. 500 m above a floor; spot cones 1 .. 89 degrees",
        "paths": "P3 P4",
        "build": build_attenuation,
        "cases": _attenuation_cases(),
    },
}

STATION_AMBIENT = {"dark_room": [2.0 ** -10] * 3}


# --- driving ------------------------------------------------------------------------------------

def apply_light(c, light):
    """Switch the station light to `light` (type, intensity, range, cone,
    pose)."""
    args = {"scene_name": c.scene, "light_name": LIGHT_NAME, "type": light["type"], "range": light["range"],
            "intensity": light["intensity"], "cast_shadow": light["cast_shadow"]}
    if light["type"] == "spot":
        args["outer_spot_angle"] = math.radians(light["outer_spot_angle_deg"])
        args["inner_spot_angle"] = math.radians(light["inner_spot_angle_deg"])
    c.mutate("edit_light", args)
    c.set_node_transform(LIGHT_NAME, translation=light["position"],
                         rotation_xyzw=light_rotation(light["position"], light["direction"]))


def build_station(c, name):
    """Close every open scene and build station `name` in a fresh scene: one
    root node holding the meshes, the light at the first case's pose."""
    station = STATIONS[name]
    c.close_all_scenes()
    c.new_scene()
    c.ambience(ambient=STATION_AMBIENT.get(name, [0.0, 0.0, 0.0]), clear_color=[0.0, 0.0, 0.0, 1.0],
               grid=False, sky=False)
    root = c.group(f"fp16 {name}", [0.0, 0.0, 0.0])
    station["build"](c, root)
    first = station["cases"][0]["light"]
    c.light(first["type"], LIGHT_NAME, first["position"], [1.0, 1.0, 1.0], first["intensity"],
            range=first["range"], cast_shadow=first["cast_shadow"])
    c.mutate("reparent_item", {"scene_name": c.scene, "item_name": LIGHT_NAME, "parent_id": int(root)})
    apply_light(c, first)
    c.settle()


def render_case(c, name, case_, output="png", path=None, msaa_samples=None, color_format=None):
    extension = "png" if output == "png" else "pfm"
    args = {"scene": c.scene, "camera": case_["camera"], "width": IMAGE_W, "height": IMAGE_H,
            "output": output, "path": path or f"{BASE}_{name}_{case_['name']}.{extension}"}
    if msaa_samples is not None:
        args["msaa_samples"] = msaa_samples
    if color_format is not None:
        args["color_format"] = color_format
    return c.call("render_scene_image", args)


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
    headlight = c.call("set_graphics_settings", {}).get("headlight_when_unlit", True)
    with fail_soft(c, BASE):
        try:
            c.mutate("set_graphics_settings", {"headlight_when_unlit": False})
            for name in names:
                build_station(c, name)
                for case_ in STATIONS[name]["cases"]:
                    apply_light(c, case_["light"])
                    c.settle()
                    render_case(c, name, case_)
                report.append(f"{name}: rendered {len(STATIONS[name]['cases'])} case(s) to {BASE}_{name}_*.png")
            c.close_all_scenes()
        finally:
            c.mutate("set_graphics_settings", {"headlight_when_unlit": bool(headlight)})
    print("\n".join(report))


if __name__ == "__main__":
    main()
