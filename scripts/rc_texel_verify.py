#!/usr/bin/env python3
"""Check the radiance cascades raw texels against an analytic ground truth.

doc/editor/radiance_cascades.md "Verification": builds GI test stations of
scripts/creations/creation_24_gi_test_rooms.py (STATIONS / build_station)
in the headless editor, selects radiance cascades, waits until the trace
cursor has swept every texel twice (get_indirect_diffuse_stats
'completed_sweeps'), reads every raw texel of every cascade back with
get_radiance_cascades_texels and compares each one against a CPU reference:

- geometry: a ray / axis-aligned box intersection of the station's parts
  (the boxes creation_24's room() builds) from the probe centre along the
  texel-centre direction over the cascade's interval [t_i, t_{i+1}] - miss
  (beta 1, radiance 0), front face hit (beta 0) or backface hit (beta 0,
  radiance 0); cascade 0 also compares the signed hit distance;
- radiance of a front face hit: the shade_surface() formula of
  res/shaders/erhe_ray_hit.glsl for the station's materials (GGX + Lambert,
  reflectance 0.5, roughness 0.9), scene ambient x base colour, emission,
  and the station's lights (spot / directional, range window, cone
  smoothstep, shadow ray against the same boxes).

Pass criteria: no hit / miss / backface disagreement, cascade 0 signed
distance within DISTANCE_TOLERANCE, front face radiance within
RADIANCE_TOLERANCE relative per channel (floor RADIANCE_FLOOR; the raw
atlases are half floats). Skip rule: a texel is left out when its reference
hit lies within SKIP_MARGIN of the interval ends (grazing the interval
boundary), or when another part has a crossing of the other kind (front
vs back) within SKIP_MARGIN of the hit - coincident faces of touching parts,
such as the floor top under a wall bottom, where the GPU may commit either
face. Skipped texels are counted in the output.

Supported stations: cornell, emissive_only, courtyard (the ones whose parts
are exactly room() boxes, panels and analytic lights).

Usage:
    py -3 scripts/rc_texel_verify.py [--station NAME ...] [--reuse] [--port N] [--editor PATH]

Without --reuse the script launches the headless editor
(build_vs2026_vulkan_headless) like gi_verify.py, backs up the editor config
files first and restores them byte-exactly at the end. Exit code 0 when
every checked texel passes, 1 otherwise.
"""

import argparse
import math
import os
import sys
import time

SCRIPTS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPTS_DIR)
sys.path.insert(0, os.path.join(SCRIPTS_DIR, "creations"))

import creation_24_gi_test_rooms as rooms  # noqa: E402
import gi_verify  # noqa: E402
from common import Creation  # noqa: E402

RADIANCE_TOLERANCE = 0.005   # relative, per channel
RADIANCE_FLOOR     = 1.0e-3  # absolute floor of the relative error denominator
DISTANCE_TOLERANCE = 1.0e-3  # metres
SKIP_MARGIN        = 1.0e-4  # metres
SWEEPS             = 2
MAX_TEXELS_PER_CALL = 4096
SUPPORTED = ("cornell", "emissive_only", "courtyard")


# --- vector helpers -------------------------------------------------------------

def v_add(a, b):
    return [a[i] + b[i] for i in range(3)]


def v_sub(a, b):
    return [a[i] - b[i] for i in range(3)]


def v_mul(a, s):
    return [a[i] * s for i in range(3)]


def v_dot(a, b):
    return sum(a[i] * b[i] for i in range(3))


def v_norm(a):
    length = math.sqrt(v_dot(a, a))
    return [x / length for x in a]


# --- station description --------------------------------------------------------

class Material:
    def __init__(self, base_color, emissive=(0.0, 0.0, 0.0)):
        self.base_color = list(base_color)
        self.emissive = list(emissive)


class Box:
    def __init__(self, name, lo, hi, material):
        self.name = name
        self.lo = list(lo)
        self.hi = list(hi)
        self.material = material


def room_boxes(lo, hi, materials, skip=()):
    """The parts creation_24's room() builds around the interior [lo, hi]."""
    t = rooms.T
    lx, ly, lz = lo
    hx, hy, hz = hi
    parts = {
        "Floor":   ([lx - t, ly - t, lz - t], [hx + t, ly,     hz + t]),
        "Ceiling": ([lx - t, hy,     lz - t], [hx + t, hy + t, hz + t]),
        "Wall -X": ([lx - t, ly,     lz - t], [lx,     hy,     hz + t]),
        "Wall +X": ([hx,     ly,     lz - t], [hx + t, hy,     hz + t]),
        "Wall -Z": ([lx,     ly,     lz - t], [hx,     hy,     lz    ]),
        "Wall +Z": ([lx,     ly,     hz    ], [hx,     hy,     hz + t]),
    }
    return [Box(face, a, b, materials.get(face, materials["default"]))
            for face, (a, b) in parts.items() if face not in skip]


def spot_light(position, aim, intensity, outer_deg, inner_deg, light_range):
    return {"type": "spot", "position": list(position), "direction": v_norm(v_sub(aim, position)),
            "radiance": [intensity] * 3, "range": light_range,
            # Light_buffer: cos(angle / 2)
            "outer_cos": math.cos(math.radians(outer_deg) * 0.5),
            "inner_cos": math.cos(math.radians(inner_deg) * 0.5)}


def describe_station(name):
    """-> (boxes, lights, ambient) mirroring creation_24's builder."""
    white = {"default": Material(rooms.WHITE)}
    if name == "cornell":
        h = rooms.CORNELL_HALF
        materials = dict(white)
        materials["Wall -X"] = Material(rooms.RED)
        materials["Wall +X"] = Material(rooms.GREEN)
        boxes = room_boxes([-h, 0.0, -h], [h, 3.0, h], materials)
        lights = [spot_light(rooms.CORNELL_LIGHT, [0.0, 0.0, 0.0], rooms.CORNELL_LIGHT_I, 90.0, 70.0, 12.0)]
        return boxes, lights, [0.0, 0.0, 0.0]
    if name == "emissive_only":
        h = rooms.EMISSIVE_HALF
        lo = [-h, 0.0, -h]
        boxes = room_boxes(lo, [h, 3.0, h], white)
        glow = Material([0.05, 0.05, 0.05], [rooms.EMISSIVE_VALUE] * 3)
        for panel, side, x in rooms.EMISSIVE_PANELS:
            boxes.append(Box(panel,
                             [x - 0.5 * side, rooms.EMISSIVE_Y - 0.5 * side, lo[2]],
                             [x + 0.5 * side, rooms.EMISSIVE_Y + 0.5 * side, lo[2] + 0.02], glow))
        return boxes, [], [0.0, 0.0, 0.0]
    if name == "courtyard":
        h = rooms.COURTYARD_HALF
        boxes = room_boxes([-h, 0.0, -h], [h, rooms.COURTYARD_H, h], white, skip=("Ceiling",))
        # Sun at (0, 8, 8) aimed at the origin: light comes from +Y+Z.
        lights = [{"type": "directional", "to_light": v_norm([0.0, 8.0, 8.0]),
                   "radiance": v_mul([1.0, 0.98, 0.94], rooms.SUN_INTENSITY)}]
        return boxes, lights, list(rooms.COURTYARD_AMBIENT)
    raise ValueError(f"station {name!r} not supported; one of {', '.join(SUPPORTED)}")


# --- reference ray ----------------------------------------------------------------

def box_crossings(origin, direction, box):
    """(t, outward normal, "front" | "back") where the line crosses the box."""
    t0, t1 = -math.inf, math.inf
    n0 = n1 = None
    for i in range(3):
        if abs(direction[i]) < 1.0e-12:
            if (origin[i] < box.lo[i]) or (origin[i] > box.hi[i]):
                return []
            continue
        ta = (box.lo[i] - origin[i]) / direction[i]
        tb = (box.hi[i] - origin[i]) / direction[i]
        na = [0.0, 0.0, 0.0]
        nb = [0.0, 0.0, 0.0]
        na[i] = -1.0
        nb[i] = 1.0
        if ta > tb:
            ta, tb, na, nb = tb, ta, nb, na
        if ta > t0:
            t0, n0 = ta, na
        if tb < t1:
            t1, n1 = tb, nb
    if t0 > t1:
        return []
    return [(t0, n0, "front"), (t1, n1, "back")]


def closest_hit(origin, direction, t_min, t_max, boxes):
    best = None
    for box in boxes:
        for t, normal, side in box_crossings(origin, direction, box):
            if (t_min <= t <= t_max) and ((best is None) or (t < best[0])):
                best = (t, normal, side, box)
    return best


def occluded(position, target, boxes):
    delta = v_sub(target, position)
    length = math.sqrt(v_dot(delta, delta))
    return closest_hit(position, v_mul(delta, 1.0 / length), 1.0e-3, length - 1.0e-3, boxes) is not None


def isotropic_brdf(base_color, roughness, L, V, N):
    """erhe_bxdf.glsl isotropic_brdf(), metallic 0, reflectance 0.5."""
    H = v_norm(v_add(L, V))
    n_dot_l = min(1.0, max(0.0, v_dot(N, L)))
    n_dot_v = min(1.0, max(0.0, v_dot(N, V)))
    n_dot_h = v_dot(N, H)
    v_dot_h = v_dot(V, H)
    alpha = roughness * roughness
    a = n_dot_h * alpha
    k = alpha / max(1.0 - (n_dot_h * n_dot_h) + (a * a), 1.0e-7)
    D = k * k / math.pi
    a2 = alpha * alpha
    gv = n_dot_l * math.sqrt((n_dot_v * n_dot_v * (1.0 - a2)) + a2)
    gl = n_dot_v * math.sqrt((n_dot_l * n_dot_l * (1.0 - a2)) + a2)
    vis = max(0.5 / max(gv + gl, 1.0e-8), 0.0)
    f0 = 0.16 * 0.5 * 0.5
    F = f0 + ((1.0 - f0) * ((1.0 - min(1.0, max(0.0, v_dot_h))) ** 5))
    return [max(v_dot(N, L), 0.0) * (((1.0 - F) * base_color[i] / math.pi) + (D * vis * F)) for i in range(3)]


def shade(position, normal, direction, material, lights, ambient, boxes):
    """erhe_ray_hit.glsl shade_surface() for these stations' materials."""
    color = [material.emissive[i] + (ambient[i] * material.base_color[i]) for i in range(3)]
    V = v_mul(direction, -1.0)
    offset_position = v_add(position, v_mul(normal, 1.0e-3))
    for light in lights:
        if light["type"] == "directional":
            L = light["to_light"]
            if (v_dot(normal, L) <= 0.0) or occluded(offset_position, v_add(position, v_mul(L, 1000.0)), boxes):
                continue
            attenuation = 1.0
        else:
            to_light = v_sub(light["position"], position)
            distance = math.sqrt(v_dot(to_light, to_light))
            L = v_mul(to_light, 1.0 / distance)
            if v_dot(normal, L) <= 0.0:
                continue
            k = distance / light["range"]
            window = min(1.0, max(0.0, 1.0 - (k ** 4)))
            attenuation = window * window / ((distance * distance) + 1.0)
            actual_cos = v_dot(light["direction"], v_mul(L, -1.0))
            if actual_cos <= light["outer_cos"]:
                continue
            if actual_cos < light["inner_cos"]:
                x = (actual_cos - light["outer_cos"]) / (light["inner_cos"] - light["outer_cos"])
                attenuation *= x * x * (3.0 - (2.0 * x))
            if occluded(offset_position, light["position"], boxes):
                continue
        f = isotropic_brdf(material.base_color, 0.9, L, V, normal)
        for i in range(3):
            color[i] += light["radiance"][i] * attenuation * f[i]
    return color


def reference_texel(texel, boxes, lights, ambient):
    """-> dict(kind, radiance, beta, distance, skip)."""
    origin = texel["probe_position"]
    direction = texel["direction"]
    t_start, t_end = texel["interval"]
    hit = closest_hit(origin, direction, t_start, t_end, boxes)
    if hit is None:
        return {"kind": "miss", "radiance": [0.0, 0.0, 0.0], "beta": 1.0, "distance": t_end, "skip": False}
    t, normal, side, box = hit
    skip = min(abs(t - t_start), abs(t_end - t)) < SKIP_MARGIN
    for other in boxes:
        if other is box:
            continue
        for t2, _, side2 in box_crossings(origin, direction, other):
            if (abs(t2 - t) < SKIP_MARGIN) and (side2 != side):
                skip = True
    if side == "back":
        return {"kind": "back", "radiance": [0.0, 0.0, 0.0], "beta": 0.0, "distance": -t, "skip": skip, "part": box.name}
    position = v_add(origin, v_mul(direction, t))
    return {"kind": "front", "radiance": shade(position, normal, direction, box.material, lights, ambient, boxes),
            "beta": 0.0, "distance": t, "skip": skip, "part": box.name}


# --- station run --------------------------------------------------------------------

def wait_sweeps(c, count, deadline_s=180.0):
    deadline = time.time() + deadline_s
    while True:
        rc = c.call("get_indirect_diffuse_stats").get("radiance_cascades", {})
        if rc.get("active") and (rc.get("completed_sweeps", 0) >= count):
            return rc
        if time.time() > deadline:
            raise RuntimeError(f"radiance cascades did not complete {count} sweeps: {rc}")
        time.sleep(0.2)


def check_station(c, name):
    boxes, lights, ambient = describe_station(name)
    rooms.build_station(c, name, ddgi=False)
    rooms.set_indirect_diffuse(c, "radiance_cascades")
    c.settle()
    rc = wait_sweeps(c, SWEEPS)
    failures = 0
    for cascade in rc["cascades"]:
        index = cascade["index"]
        q = cascade["tile_texels"]
        nx, ny, nz = cascade["grid_counts"]
        requests = [{"cascade": index, "probe": [x, y, z], "texel": [u, v]}
                    for z in range(nz) for y in range(ny) for x in range(nx)
                    for v in range(q) for u in range(q)]
        counts = {"checked": 0, "skipped": 0, "kind": 0, "distance": 0, "radiance": 0}
        worst_radiance = 0.0
        examples = []
        for start in range(0, len(requests), MAX_TEXELS_PER_CALL):
            result = c.call("get_radiance_cascades_texels", {"texels": requests[start:start + MAX_TEXELS_PER_CALL]})
            for texel in result["texels"]:
                expected = reference_texel(texel, boxes, lights, ambient)
                if expected["skip"]:
                    counts["skipped"] += 1
                    continue
                counts["checked"] += 1
                problem = None
                measured_miss = texel["beta"] > 0.5
                if measured_miss != (expected["kind"] == "miss"):
                    counts["kind"] += 1
                    problem = "hit/miss"
                elif (index == 0) and ((texel["signed_distance"] < 0.0) != (expected["kind"] == "back")):
                    counts["kind"] += 1
                    problem = "backface"
                elif (index == 0) and (abs(texel["signed_distance"] - expected["distance"]) > DISTANCE_TOLERANCE):
                    counts["distance"] += 1
                    problem = "distance"
                else:
                    error = max(abs(texel["radiance"][i] - expected["radiance"][i]) / max(abs(expected["radiance"][i]), RADIANCE_FLOOR)
                                for i in range(3))
                    worst_radiance = max(worst_radiance, error)
                    if error > RADIANCE_TOLERANCE:
                        counts["radiance"] += 1
                        problem = f"radiance error {error:.4f}"
                if (problem is not None) and (len(examples) < 5):
                    examples.append(f"      {problem}: probe {texel['probe']} texel {texel['texel']} measured "
                                    f"{texel['radiance']} beta {texel['beta']} d {texel.get('signed_distance')}; "
                                    f"expected {expected['kind']} {expected['radiance']} d {expected['distance']:.4f}")
        bad = counts["kind"] + counts["distance"] + counts["radiance"]
        failures += bad
        print(f"  {name} cascade {index}: {'PASS' if bad == 0 else 'FAIL'} checked {counts['checked']}, "
              f"skipped {counts['skipped']}, kind mismatches {counts['kind']}, distance {counts['distance']}, "
              f"radiance {counts['radiance']} (worst relative {worst_radiance:.5f})", flush=True)
        for line in examples:
            print(line)
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--station", nargs="+", default=list(SUPPORTED), choices=SUPPORTED)
    parser.add_argument("--reuse", action="store_true", help="drive the editor already running on --port")
    parser.add_argument("--port", type=int, default=3743)
    parser.add_argument("--editor", default=gi_verify.DEFAULT_EDITOR)
    args = parser.parse_args()

    backup = None
    process = None
    port = args.port
    if not args.reuse:
        backup = gi_verify.Config_backup()
        process, port = gi_verify.launch_editor(args.editor)
        print(f"launched editor pid {process.pid} on port {port}")
    c = None
    try:
        c = Creation("rc_texel_verify", port=port, pause_s=0.0, reuse=True, manage_windows=False)
        if process is not None:
            pid = c.call("get_server_info").get("pid")
            if pid != process.pid:
                raise RuntimeError(f"port {port} is served by pid {pid}, not the launched editor {process.pid}")
        headlight = c.call("set_graphics_settings", {}).get("headlight_when_unlit", True)
        failures = 0
        try:
            for name in args.station:
                failures += check_station(c, name)
            c.close_all_scenes()
        finally:
            rooms.set_headlight(c, headlight)
        print("PASS" if failures == 0 else f"FAIL: {failures} texel(s)")
        return 0 if failures == 0 else 1
    finally:
        if process is not None:
            gi_verify.stop_editor(c, process) if c is not None else process.kill()
        if backup is not None:
            backup.restore()


if __name__ == "__main__":
    sys.exit(main())
