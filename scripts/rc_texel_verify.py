#!/usr/bin/env python3
"""Check the radiance cascades raw and merged texels against an analytic
ground truth.

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

Merged checks (doc/editor/radiance_cascades.md "Merge"), from the same
readback (every texel carries its merged value too):

- exact algebra: every merged texel of every cascade against the merge
  recomputed on the CPU from the READ-BACK raw texel and the read-back
  merged texels of the cascade above - the 8 upper probes of
  get_upper_probes() (trilinear 0.25 / 0.75 weights, indices clamped to the
  upper grid), each the average of the 2x2 child texels, restricted to the
  upper probes the read-back probe states mark usable (segment
  unobstructed, upper probe not inside geometry) with the weights
  renormalized, (0, 0) when none is; the top cascade merges with the sky
  (the scene ambient). Tolerance MERGE_ALGEBRA_TOLERANCE
  relative (floor MERGE_ALGEBRA_FLOOR): only the final half-float rounding
  differs. This verifies the shader's arithmetic, not the approximation.
- approximation: for APPROX_SAMPLES cascade 0 texels (probes outside every
  part, fixed seed), the ground truth is the average over the texel's
  octahedral footprint - APPROX_SUBDIVISIONS^2 sub-directions uniform in
  the octahedral parameter, the measure the 2x2 child averages of the merge
  use - of the full-range radiance from the probe centre (closest hit over
  [0, inf), shaded as above; the sky on escape; 0 on a backface). The
  merge interpolates the upper intervals from upper probes that sit
  elsewhere (parallax), so this is an error distribution, not a 0.5 %
  check: relative luminance error |merged - truth| / max(truth, 1 % of the
  sample's mean truth); the script prints median, p90 and max and the worst
  texels, and fails only when the median exceeds APPROX_MEDIAN_BOUND or
  p90 exceeds APPROX_P90_BOUND (bounds set from the measured stations,
  doc/editor/radiance_cascades.md "Verification").
- reduce exact algebra (doc/editor/radiance_cascades.md "Reduce"): for
  REDUCE_PROBES cascade 0 probes of each station (free interior probes,
  fixed picks), the probe field texels of REDUCE_NORMALS - irradiance and
  distance moments at the texel containing each normal, and the probe data
  state - against the reduce recomputed on the CPU from the READ-BACK
  merged cascade 0 texels and signed distances of the same copy: lobe
  weights integrated over each cascade 0 texel footprint exactly like
  compute_octahedral_lobe_weights() (cosine for irradiance, backface texels
  skipped; pow(cos, depth_sharpness) for the moments of
  min(|distance|, r0)), and the solid-angle backface fraction against
  DDGI's 0.25 classification threshold. Tolerance REDUCE_TOLERANCE
  relative (floor REDUCE_FLOOR): the half-float store.
- --mask-check (cornell by default): the merge is linear in the interval
  radiances, so the mean merged cascade 0 radiance with every band shown
  equals the sum of the means with one band shown at a time
  (debug_cascade_mask = everything but that band; the bands are the
  cascades and the sky), within MASK_SUM_TOLERANCE relative.

Supported stations: cornell, emissive_only, courtyard, leak_pair, corridor
(the ones whose parts are exactly room() boxes, panels and analytic lights).

Usage:
    py -3 scripts/rc_texel_verify.py [--station NAME ...] [--merge-mode MODE ...] [--mask-check NAME ...]
                                     [--reuse] [--port N] [--editor PATH]

The merged checks run for each --merge-mode (default: interpolate and
visibility_masked), on the same station build; the raw checks once.

Without --reuse the script launches the headless editor
(build_vs2026_vulkan_headless) like gi_verify.py, backs up the editor config
files first and restores them byte-exactly at the end. Exit code 0 when
every checked texel passes, 1 otherwise.
"""

import argparse
import math
import os
import random
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
SUPPORTED = ("cornell", "emissive_only", "courtyard", "leak_pair", "corridor")
MERGE_MODES = ("interpolate", "visibility_masked")

MERGE_ALGEBRA_TOLERANCE = 2.0e-3  # relative: the half-float store rounds by at most 2^-11
MERGE_ALGEBRA_FLOOR     = 1.0e-4  # absolute floor of the relative error denominator
APPROX_SAMPLES          = 400     # cascade 0 texels per station
APPROX_SUBDIVISIONS     = 8       # sub-directions per texel axis of the ground truth
APPROX_SEED             = 1
APPROX_FLOOR_FRACTION   = 0.1     # relative error denominator floor, of the sample mean truth
# Loose bounds, about twice the worst station measured when the merge was
# written (median 0.13, p90 0.86; doc/editor/radiance_cascades.md
# "Verification"): they catch a broken merge (wrong child texels or upper
# probes read as errors of order 1 on most texels), not the parallax error.
APPROX_MEDIAN_BOUND     = 0.25
APPROX_P90_BOUND        = 1.5
MASK_SUM_TOLERANCE      = 2.0e-3  # relative
REDUCE_PROBES           = 3
REDUCE_NORMALS          = ((0.0, 1.0, 0.0), (1.0, 0.0, 0.0), (0.0, 0.0, -1.0))
REDUCE_TOLERANCE        = 2.0e-3  # relative: the half-float store rounds by at most 2^-11
REDUCE_FLOOR            = 1.0e-4  # absolute floor of the relative error denominator
OCTAHEDRAL_INTEGRATION_CELLS = 32  # c_octahedral_integration_cells (radiance_cascades_layout.hpp)
BACKFACE_FRACTION_THRESHOLD  = 0.25  # DDGI's classification rule
SKY_MASK_BIT            = 12      # Radiance_cascades_renderer::c_sky_mask_bit


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
    if name == "leak_pair":
        lo = [-rooms.PAIR_HALF_X, 0.0, -0.5 * rooms.PAIR_DEPTH]
        hi = [rooms.PAIR_HALF_X, rooms.ROOM_H, 0.5 * rooms.PAIR_DEPTH]
        boxes = room_boxes(lo, hi, white)
        boxes.append(Box("Shared Wall", [-0.5 * rooms.SHARED_T, 0.0, lo[2]],
                         [0.5 * rooms.SHARED_T, rooms.ROOM_H, hi[2]], white["default"]))
        lights = [{"type": "point", "position": [-2.05, 2.2, 0.0], "radiance": [rooms.PAIR_LIGHT_I] * 3,
                   "range": 12.0}]
        return boxes, lights, [0.0, 0.0, 0.0]
    if name == "corridor":
        lo = [-rooms.CORRIDOR_HALF_W, 0.0, 0.0]
        hi = [rooms.CORRIDOR_HALF_W, rooms.CORRIDOR_H, rooms.CORRIDOR_LEN]
        boxes = room_boxes(lo, hi, white)
        lights = [spot_light(rooms.CORRIDOR_LIGHT, [0.0, 1.4, 0.0], rooms.CORRIDOR_LIGHT_I, 90.0, 70.0, 10.0)]
        return boxes, lights, [0.0, 0.0, 0.0]
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
        elif light["type"] == "point":
            to_light = v_sub(light["position"], position)
            distance = math.sqrt(v_dot(to_light, to_light))
            L = v_mul(to_light, 1.0 / distance)
            if v_dot(normal, L) <= 0.0:
                continue
            k = distance / light["range"]
            window = min(1.0, max(0.0, 1.0 - (k ** 4)))
            attenuation = window * window / ((distance * distance) + 1.0)
            if occluded(offset_position, light["position"], boxes):
                continue
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


def wait_updates(c, count, deadline_s=60.0):
    """Wait until the renderer has run `count` more updates (trace + merge)."""
    start = c.call("get_indirect_diffuse_stats")["radiance_cascades"]["update_count"]
    deadline = time.time() + deadline_s
    while True:
        rc = c.call("get_indirect_diffuse_stats")["radiance_cascades"]
        if rc["update_count"] >= start + count:
            return rc
        if time.time() > deadline:
            raise RuntimeError("radiance cascades stopped updating")
        time.sleep(0.1)


def read_all_texels(c, rc):
    """(cascade, probe, texel) -> read-back texel, every texel of every cascade."""
    texels_by_address = {}
    for cascade in rc["cascades"]:
        index = cascade["index"]
        q = cascade["tile_texels"]
        nx, ny, nz = cascade["grid_counts"]
        requests = [{"cascade": index, "probe": [x, y, z], "texel": [u, v]}
                    for z in range(nz) for y in range(ny) for x in range(nx)
                    for v in range(q) for u in range(q)]
        for start in range(0, len(requests), MAX_TEXELS_PER_CALL):
            result = c.call("get_radiance_cascades_texels", {"texels": requests[start:start + MAX_TEXELS_PER_CALL]})
            for texel in result["texels"]:
                texels_by_address[(index, tuple(texel["probe"]), tuple(texel["texel"]))] = texel
    return texels_by_address


def check_station(c, name, merge_modes):
    boxes, lights, ambient = describe_station(name)
    rooms.build_station(c, name, ddgi=False)
    rooms.set_indirect_diffuse(c, "radiance_cascades")
    c.mutate("set_radiance_cascades", {"debug_cascade_mask": 0, "merge_mode": merge_modes[0]})
    c.settle()
    rc = wait_sweeps(c, SWEEPS)
    failures = 0
    for mode_index, merge_mode in enumerate(merge_modes):
        if mode_index > 0:
            # The merge re-runs every update; the visibility pass (if any)
            # runs on the first update after the switch.
            c.mutate("set_radiance_cascades", {"merge_mode": merge_mode})
            rc = wait_updates(c, 3)
        texels_by_address = read_all_texels(c, rc)
        if mode_index == 0:
            failures += check_raw(name, rc, texels_by_address, boxes, lights, ambient)
        print(f"  {name} merge mode {merge_mode}:", flush=True)
        failures += check_merge_algebra(name, rc, texels_by_address, ambient)
        failures += check_merge_approximation(name, rc, texels_by_address, boxes, lights, ambient)
        failures += check_reduce(c, name, c.call("get_indirect_diffuse_stats")["radiance_cascades"], boxes)
        stats = c.call("get_indirect_diffuse_stats").get("radiance_cascades", {})
        gpu_ms = stats.get("gpu_ms", {})
        print(f"  {name} cost ({merge_mode}): {stats.get('texels')} texels, {stats.get('texels_per_update')} per update; "
              f"GPU ms average trace {gpu_ms.get('trace', {}).get('average_ms', 0.0):.3f}, "
              f"merge {gpu_ms.get('merge', {}).get('average_ms', 0.0):.3f}, "
              f"reduce {gpu_ms.get('reduce', {}).get('average_ms', 0.0):.3f} ({stats.get('timing_sample_count')} samples); "
              f"visibility pass {stats.get('visibility', {}).get('last_ms', 0.0):.3f} ms "
              f"(runs {stats.get('visibility', {}).get('update_count')})", flush=True)
    c.mutate("set_radiance_cascades", {"merge_mode": "interpolate"})
    return failures


def check_raw(name, rc, texels_by_address, boxes, lights, ambient):
    failures = 0
    for cascade in rc["cascades"]:
        index = cascade["index"]
        counts = {"checked": 0, "skipped": 0, "kind": 0, "distance": 0, "radiance": 0}
        worst_radiance = 0.0
        examples = []
        for texel in (t for (i, _, _), t in texels_by_address.items() if i == index):
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


# --- merged checks -------------------------------------------------------------------

def upper_probe_axis(lower_index, upper_count):
    """get_upper_probe_axis() of radiance_cascades_layout.cpp."""
    last = max(0, upper_count - 1)
    if lower_index % 2 == 0:
        m = lower_index // 2
        return [min(max(m - 1, 0), last), min(max(m, 0), last)], [0.25, 0.75]
    m = (lower_index - 1) // 2
    return [min(max(m, 0), last), min(max(m + 1, 0), last)], [0.75, 0.25]


def expected_merge(index, texel, rc, texels_by_address, ambient):
    """rc_merge.comp on the read-back raw / upper merged texels (mask 0).
    visibility_masked (the texels carry probe states): upper probes whose
    segment is blocked (the lower texel's upper_visible_mask) or that are
    inside geometry (the upper texel's probe_inside) are skipped and the
    remaining weights renormalized; with none left, upper is (0, 0).
    interpolate: all 8 with their trilinear weights."""
    raw = texel["radiance"] + [texel["beta"]]
    if index == len(rc["cascades"]) - 1:
        upper = list(ambient) + [1.0]
    else:
        upper_counts = rc["cascades"][index + 1]["grid_counts"]
        axes = [upper_probe_axis(texel["probe"][a], upper_counts[a]) for a in range(3)]
        u, v = texel["texel"]
        upper = [0.0, 0.0, 0.0, 0.0]
        weight_sum = 0.0
        for k in range(2):
            for j in range(2):
                for i in range(2):
                    weight = axes[0][1][i] * axes[1][1][j] * axes[2][1][k]
                    probe = (axes[0][0][i], axes[1][0][j], axes[2][0][k])
                    if "upper_visible_mask" in texel:  # visibility_masked
                        visible = (texel["upper_visible_mask"] >> (i + (2 * j) + (4 * k))) & 1
                        usable = visible and not texels_by_address[(index + 1, probe, (0, 0))]["probe_inside"]
                    else:  # interpolate
                        usable = True
                    if usable:
                        weight_sum += weight
                    if not usable:
                        continue
                    for du, dv in ((0, 0), (1, 0), (0, 1), (1, 1)):
                        child = texels_by_address[(index + 1, probe, (2 * u + du, 2 * v + dv))]
                        value = child["merged_radiance"] + [child["merged_beta"]]
                        for ch in range(4):
                            upper[ch] += weight * 0.25 * value[ch]
        upper = [x / weight_sum for x in upper] if weight_sum > 0.0 else [0.0, 0.0, 0.0, 0.0]
    return [raw[ch] + (raw[3] * upper[ch]) for ch in range(3)] + [raw[3] * upper[3]]


def check_merge_algebra(name, rc, texels_by_address, ambient):
    failures = 0
    for cascade in rc["cascades"]:
        index = cascade["index"]
        checked = 0
        bad = 0
        worst = 0.0
        examples = []
        for (c_index, probe, uv), texel in texels_by_address.items():
            if c_index != index:
                continue
            expected = expected_merge(index, texel, rc, texels_by_address, ambient)
            measured = texel["merged_radiance"] + [texel["merged_beta"]]
            error = max(abs(measured[ch] - expected[ch]) / max(abs(expected[ch]), MERGE_ALGEBRA_FLOOR)
                        for ch in range(4))
            checked += 1
            worst = max(worst, error)
            if error > MERGE_ALGEBRA_TOLERANCE:
                bad += 1
                if len(examples) < 5:
                    examples.append(f"      probe {list(probe)} texel {list(uv)}: merged {measured} expected {expected}")
        failures += bad
        print(f"  {name} cascade {index} merge algebra: {'PASS' if bad == 0 else 'FAIL'} checked {checked}, "
              f"failures {bad} (worst relative {worst:.5f})", flush=True)
        for line in examples:
            print(line)
    return failures


def luminance(rgb):
    return (0.2126 * rgb[0]) + (0.7152 * rgb[1]) + (0.0722 * rgb[2])


def full_range_radiance(origin, direction, boxes, lights, ambient):
    hit = closest_hit(origin, direction, 0.0, math.inf, boxes)
    if hit is None:
        return list(ambient)
    t, normal, side, box = hit
    if side == "back":
        return [0.0, 0.0, 0.0]
    position = v_add(origin, v_mul(direction, t))
    return shade(position, normal, direction, box.material, lights, ambient, boxes)


def octahedral_decode(f):
    n = [f[0], f[1], 1.0 - abs(f[0]) - abs(f[1])]
    t = max(-n[2], 0.0)
    n[0] += -t if n[0] > 0.0 else t
    n[1] += -t if n[1] > 0.0 else t
    return v_norm(n)


def interior_of(name):
    """(lo, hi) of the rooms' free interior: the space the field serves."""
    if name == "cornell":
        h = rooms.CORNELL_HALF
        return [-h, 0.0, -h], [h, 3.0, h]
    if name == "emissive_only":
        h = rooms.EMISSIVE_HALF
        return [-h, 0.0, -h], [h, 3.0, h]
    if name == "courtyard":
        h = rooms.COURTYARD_HALF
        return [-h, 0.0, -h], [h, rooms.COURTYARD_H, h]
    if name == "leak_pair":
        return [-rooms.PAIR_HALF_X, 0.0, -0.5 * rooms.PAIR_DEPTH], [rooms.PAIR_HALF_X, rooms.ROOM_H, 0.5 * rooms.PAIR_DEPTH]
    if name == "corridor":
        return [-rooms.CORRIDOR_HALF_W, 0.0, 0.0], [rooms.CORRIDOR_HALF_W, rooms.CORRIDOR_H, rooms.CORRIDOR_LEN]
    raise ValueError(name)


def inside_box(point, lo, hi):
    return all(lo[i] < point[i] < hi[i] for i in range(3))


def inside_any(point, boxes):
    return any(all(box.lo[i] <= point[i] <= box.hi[i] for i in range(3)) for box in boxes)


def nearest_face_distance(point, boxes):
    """Distance from a free point to the nearest part (for the worst-texel report)."""
    best = math.inf
    for box in boxes:
        d = [max(box.lo[i] - point[i], 0.0, point[i] - box.hi[i]) for i in range(3)]
        best = min(best, math.sqrt(v_dot(d, d)))
    return best


def hidden_upper_weight(position, rc, boxes):
    """Per cascade 1 .. N-1, the trilinear weight of the 8 cascade probes
    around `position` that the position cannot see (the segment to the
    upper probe crosses a part, or the upper probe is inside one). The
    merge interpolates those probes' intervals, so where this is > 0 the
    merged texel carries light from the other side of a wall (or misses
    light an occluded upper probe does not see)."""
    result = []
    for cascade in rc["cascades"][1:]:
        origin = cascade["grid_origin"]
        spacing = cascade["grid_spacing"]
        counts = cascade["grid_counts"]
        axes = []
        for a in range(3):
            g = (position[a] - origin[a]) / spacing[a]
            i0 = min(max(int(math.floor(g)), 0), counts[a] - 1)
            i1 = min(i0 + 1, counts[a] - 1)
            f = min(max(g - i0, 0.0), 1.0)
            axes.append(((i0, 1.0 - f), (i1, f)))
        hidden = 0.0
        for ix, wx in axes[0]:
            for iy, wy in axes[1]:
                for iz, wz in axes[2]:
                    weight = wx * wy * wz
                    if weight <= 0.0:
                        continue
                    upper = [origin[0] + ix * spacing[0], origin[1] + iy * spacing[1], origin[2] + iz * spacing[2]]
                    delta = v_sub(upper, position)
                    length = math.sqrt(v_dot(delta, delta))
                    blocked = inside_any(upper, boxes) or ((length > 1.0e-6) and (
                        closest_hit(position, v_mul(delta, 1.0 / length), 0.0, length, boxes) is not None))
                    if blocked:
                        hidden += weight
        result.append(hidden)
    return result


def distribution(values):
    values = sorted(values)
    if not values:
        return "-"
    return (f"n {len(values)}, median {values[len(values) // 2]:.4f}, "
            f"p90 {values[min(len(values) - 1, int(0.9 * len(values)))]:.4f}, max {values[-1]:.4f}")


def check_merge_approximation(name, rc, texels_by_address, boxes, lights, ambient):
    q = rc["cascades"][0]["tile_texels"]
    n = APPROX_SUBDIVISIONS
    lo, hi = interior_of(name)
    candidates = [texel for (index, _, _), texel in texels_by_address.items()
                  if (index == 0) and inside_box(texel["probe_position"], lo, hi)
                  and not inside_any(texel["probe_position"], boxes)]
    candidates.sort(key=lambda texel: (tuple(texel["probe"]), tuple(texel["texel"])))
    sample = random.Random(APPROX_SEED).sample(candidates, min(APPROX_SAMPLES, len(candidates)))
    rows = []
    for texel in sample:
        u, v = texel["texel"]
        truth = [0.0, 0.0, 0.0]
        for sv in range(n):
            for su in range(n):
                f = [(((u + ((su + 0.5) / n)) / q) * 2.0) - 1.0, (((v + ((sv + 0.5) / n)) / q) * 2.0) - 1.0]
                radiance = full_range_radiance(texel["probe_position"], octahedral_decode(f), boxes, lights, ambient)
                truth = v_add(truth, radiance)
        truth = v_mul(truth, 1.0 / (n * n))
        rows.append((texel, luminance(truth), luminance(texel["merged_radiance"])))
    hidden_by_probe = {}
    for texel, _, _ in rows:
        key = tuple(texel["probe"])
        if key not in hidden_by_probe:
            hidden_by_probe[key] = hidden_upper_weight(texel["probe_position"], rc, boxes)
    mean_truth = sum(row[1] for row in rows) / max(1, len(rows))
    floor = max(APPROX_FLOOR_FRACTION * mean_truth, 1.0e-6)
    errors = sorted((((abs(merged - truth) / max(truth, floor)), texel, truth, merged) for texel, truth, merged in rows),
                    key=lambda row: row[0])
    if not errors:
        print(f"  {name} merge approximation: no sample texels")
        return 0
    values = [e[0] for e in errors]
    median = values[len(values) // 2]
    p90 = values[min(len(values) - 1, int(0.9 * len(values)))]
    mean_signed = sum((merged - truth) for _, _, truth, merged in errors) / (len(errors) * max(mean_truth, 1.0e-9))
    ok = (median <= APPROX_MEDIAN_BOUND) and (p90 <= APPROX_P90_BOUND)
    print(f"  {name} merge approximation: {'PASS' if ok else 'FAIL'} {len(values)} cascade 0 texels, "
          f"{n}x{n} sub-directions: relative error median {median:.4f}, p90 {p90:.4f}, max {values[-1]:.4f}; "
          f"mean truth {mean_truth:.5f}, mean bias {mean_signed:+.4f} "
          f"(bounds median {APPROX_MEDIAN_BOUND}, p90 {APPROX_P90_BOUND})", flush=True)
    # Split by the cascade 1 stencil (the nearest upper field), and the mean
    # hidden weight per cascade over the sample.
    visible = [e[0] for e in errors if hidden_by_probe[tuple(e[1]["probe"])][0] == 0.0]
    occluded_upper = [e[0] for e in errors if hidden_by_probe[tuple(e[1]["probe"])][0] > 0.0]
    cascade_count = len(rc["cascades"])
    mean_hidden = [sum(hidden_by_probe[tuple(e[1]["probe"])][k] for e in errors) / len(errors)
                   for k in range(cascade_count - 1)]
    print(f"      cascade 1 upper probes all visible: {distribution(visible)}")
    print(f"      cascade 1 upper probe hidden:       {distribution(occluded_upper)}")
    print(f"      mean hidden upper weight, cascades 1..{cascade_count - 1}: "
          f"{' '.join(f'{w:.2f}' for w in mean_hidden)}")
    if name == "leak_pair":
        # Room B (x > shared wall) has no light: whatever it reads is leaked.
        room_b = [e for e in errors if e[1]["probe_position"][0] > 0.5 * rooms.SHARED_T]
        if room_b:
            mean_b_truth = sum(e[2] for e in room_b) / len(room_b)
            mean_b_merged = sum(e[3] for e in room_b) / len(room_b)
            worst_b = sorted(room_b, key=lambda e: e[3] - e[2])[-3:]
            print(f"      room B: {len(room_b)} texels, mean truth {mean_b_truth:.5f}, mean merged {mean_b_merged:.5f} "
                  f"(room A mean truth {mean_truth:.5f}); largest merged - truth: "
                  + ", ".join(f"{e[3] - e[2]:.4f} at probe {e[1]['probe']} texel {e[1]['texel']}" for e in worst_b))
    for error, texel, truth, merged in errors[-3:]:
        print(f"      worst {error:.3f}: probe {texel['probe']} at {[round(x, 3) for x in texel['probe_position']]} "
              f"texel {texel['texel']} dir {[round(x, 3) for x in texel['direction']]}: merged {merged:.5f} "
              f"truth {truth:.5f}, nearest part {nearest_face_distance(texel['probe_position'], boxes):.3f} m, "
              f"hidden upper weight per cascade {' '.join(f'{w:.2f}' for w in hidden_by_probe[tuple(texel['probe'])])}")
    return 0 if ok else 1


# --- reduce check ---------------------------------------------------------------------

def octahedral_encode(d):
    s = abs(d[0]) + abs(d[1]) + abs(d[2])
    n = [d[0] / s, d[1] / s, d[2] / s]
    if n[2] < 0.0:
        return [(1.0 - abs(n[1])) * (1.0 if n[0] >= 0.0 else -1.0),
                (1.0 - abs(n[0])) * (1.0 if n[1] >= 0.0 else -1.0)]
    return [n[0], n[1]]


def texel_direction(texel, tile_texels):
    return octahedral_decode([(((texel[0] + 0.5) / tile_texels) * 2.0) - 1.0,
                              (((texel[1] + 0.5) / tile_texels) * 2.0) - 1.0])


def direction_texel(direction, tile_texels):
    f = octahedral_encode(direction)
    return [min(max(int(math.floor(((f[i] * 0.5) + 0.5) * tile_texels)), 0), tile_texels - 1) for i in range(2)]


def texel_subdivisions(tile_texels):
    """(texel index, direction, solid angle) of every integration square of a
    tile, as for_each_texel_subdivision() in radiance_cascades_layout.cpp."""
    s = max(1, OCTAHEDRAL_INTEGRATION_CELLS // max(1, tile_texels))
    cells = tile_texels * s
    side = 2.0 / cells
    out = []
    for v in range(tile_texels):
        for u in range(tile_texels):
            for b in range(s):
                for a in range(s):
                    f = [-1.0 + (((u * s) + a + 0.5) * side), -1.0 + (((v * s) + b + 0.5) * side)]
                    w = octahedral_decode(f)
                    l1 = abs(w[0]) + abs(w[1]) + abs(w[2])
                    out.append(((v * tile_texels) + u, w, side * side * l1 * l1 * l1))
    return out


def lobe_weights(direction, tile_texels, exponent, subdivisions):
    weights = [0.0] * (tile_texels * tile_texels)
    for index, w, solid_angle in subdivisions:
        cos_angle = v_dot(direction, w)
        if cos_angle > 0.0:
            weights[index] += (cos_angle ** exponent) * solid_angle
    return weights


def relative_error(measured, expected):
    return max(abs(m - e) / max(abs(e), REDUCE_FLOOR) for m, e in zip(measured, expected))


def check_reduce(c, name, rc, boxes):
    cascade0 = rc["cascades"][0]
    q = cascade0["tile_texels"]
    r0 = rc["r0"]
    nx, ny, nz = cascade0["grid_counts"]
    lo, hi = interior_of(name)
    free = []
    for z in range(nz):
        for y in range(ny):
            for x in range(nx):
                position = [cascade0["grid_origin"][i] + ([x, y, z][i] * cascade0["grid_spacing"][i]) for i in range(3)]
                if inside_box(position, lo, hi) and not inside_any(position, boxes):
                    free.append([x, y, z])
    if len(free) < REDUCE_PROBES:
        print(f"  {name} reduce: fewer than {REDUCE_PROBES} free cascade 0 probes")
        return 1
    probes = [free[((i + 1) * len(free)) // (REDUCE_PROBES + 1)] for i in range(REDUCE_PROBES)]
    field = rc.get("field") or {}
    irradiance_texels = field.get("irradiance_texels")
    distance_texels = field.get("distance_texels")
    if irradiance_texels is None:
        print(f"  {name} reduce: FAIL no probe field in get_indirect_diffuse_stats")
        return 1
    texel_requests = [{"cascade": 0, "probe": p, "texel": [u, v]} for p in probes for v in range(q) for u in range(q)]
    field_requests = []
    for p in probes:
        for normal in REDUCE_NORMALS:
            field_requests.append({"probe": p, "texel": direction_texel(normal, irradiance_texels), "atlas": "irradiance"})
            field_requests.append({"probe": p, "texel": direction_texel(normal, distance_texels), "atlas": "distance"})
    result = c.call("get_radiance_cascades_texels", {"texels": texel_requests, "field_texels": field_requests})
    depth_sharpness = result["field"]["depth_sharpness"]
    by_address = {(tuple(t["probe"]), tuple(t["texel"])): t for t in result["texels"]}
    subdivisions = texel_subdivisions(q)
    solid_angles = [0.0] * (q * q)
    for index, _, solid_angle in subdivisions:
        solid_angles[index] += solid_angle

    failures = 0
    worst = 0.0
    checked = 0
    for entry in result["field_texels"]:
        probe = tuple(entry["probe"])
        texels = [by_address[(probe, (j % q, j // q))] for j in range(q * q)]
        # The field texel's own direction: the centre of its output texel.
        tile = irradiance_texels if entry["atlas"] == "irradiance" else distance_texels
        direction = texel_direction(entry["texel"], tile)
        if entry["atlas"] == "irradiance":
            weights = lobe_weights(direction, q, 1.0, subdivisions)
            total = [0.0, 0.0, 0.0]
            weight_sum = 0.0
            for j, texel in enumerate(texels):
                if (weights[j] <= 0.0) or (texel["signed_distance"] < 0.0):
                    continue
                total = v_add(total, v_mul(texel["merged_radiance"], weights[j]))
                weight_sum += weights[j]
            expected = v_mul(total, 1.0 / weight_sum) if weight_sum > 1.0e-9 else [0.0, 0.0, 0.0]
            measured = entry["irradiance"]
        else:
            weights = lobe_weights(direction, q, depth_sharpness, subdivisions)
            total = [0.0, 0.0]
            weight_sum = 0.0
            for j, texel in enumerate(texels):
                if weights[j] <= 0.0:
                    continue
                d = min(abs(texel["signed_distance"]), r0)
                total = [total[0] + (d * weights[j]), total[1] + (d * d * weights[j])]
                weight_sum += weights[j]
            expected = [total[0] / weight_sum, total[1] / weight_sum] if weight_sum > 1.0e-9 else [0.0, 0.0]
            measured = entry["moments"]
        backface = sum(solid_angles[j] for j, texel in enumerate(texels) if texel["signed_distance"] < 0.0)
        expected_state = 0.0 if backface > (BACKFACE_FRACTION_THRESHOLD * sum(solid_angles)) else 1.0
        error = relative_error(measured, expected)
        worst = max(worst, error)
        checked += 1
        if (error > REDUCE_TOLERANCE) or (entry["probe_state"] != expected_state):
            failures += 1
            print(f"      FAIL probe {list(probe)} {entry['atlas']} texel {entry['texel']}: measured {measured}, "
                  f"expected {expected}, state {entry['probe_state']} (expected {expected_state})")
    print(f"  {name} reduce exact algebra: {'PASS' if failures == 0 else 'FAIL'} {checked} field texels of "
          f"{REDUCE_PROBES} probes {[list(p) for p in probes]}, worst relative difference {worst:.5f}", flush=True)
    return failures


def check_mask_decomposition(c, name):
    """Mean merged cascade 0 radiance: all bands shown == sum over single bands."""
    _, _, ambient = describe_station(name)
    rooms.build_station(c, name, ddgi=False)
    rooms.set_indirect_diffuse(c, "radiance_cascades")
    c.mutate("set_radiance_cascades", {"debug_cascade_mask": 0})
    c.settle()
    rc = wait_sweeps(c, SWEEPS)
    count = rc["cascade_count"]
    everything = ((1 << count) - 1) | (1 << SKY_MASK_BIT)

    def mean_merged(mask):
        c.mutate("set_radiance_cascades", {"debug_cascade_mask": mask})
        summary = c.call("get_radiance_cascades_texels", {"texels": []})["cascades"][0]
        return summary["mean_merged_radiance"]

    try:
        full = mean_merged(0)
        bands = [(f"cascade {i}", 1 << i) for i in range(count)] + [("sky", 1 << SKY_MASK_BIT)]
        total = [0.0, 0.0, 0.0]
        parts = []
        for label, bit in bands:
            value = mean_merged(everything & ~bit)
            parts.append((label, value))
            total = v_add(total, value)
        none = mean_merged(everything)
    finally:
        c.mutate("set_radiance_cascades", {"debug_cascade_mask": 0})
    error = max(abs(total[i] - full[i]) / max(abs(full[i]), 1.0e-6) for i in range(3))
    ok = (error <= MASK_SUM_TOLERANCE) and (max(abs(x) for x in none) <= 1.0e-6)
    print(f"  {name} mask decomposition: {'PASS' if ok else 'FAIL'} mean merged cascade 0 luminance, all bands "
          f"{luminance(full):.6f}, sum of single bands {luminance(total):.6f} (worst channel relative {error:.5f}), "
          f"all masked {luminance(none):.2e}", flush=True)
    for label, value in parts:
        print(f"      {label:10s} {luminance(value):.6f} ({100.0 * luminance(value) / max(luminance(full), 1.0e-12):5.1f} %)")
    return 0 if ok else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--station", nargs="+", default=list(SUPPORTED), choices=SUPPORTED)
    parser.add_argument("--merge-mode", nargs="+", default=list(MERGE_MODES), choices=MERGE_MODES,
                        help="merge modes whose merged checks run (default: both)")
    parser.add_argument("--mask-check", nargs="*", default=["cornell"], choices=SUPPORTED,
                        help="stations for the debug_cascade_mask decomposition check (none: skip)")
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
                failures += check_station(c, name, args.merge_mode)
            for name in args.mask_check:
                failures += check_mask_decomposition(c, name)
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
