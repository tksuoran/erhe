#!/usr/bin/env python3
"""Measure the shadow paths against analytic ground truth on the shadow test stations.

doc/plans/shadow_robustness.md sections 5 to 7 (T6): loads each station asset
of scripts/creations/creation_25_shadow_test_rooms.py (imported: every box,
light pose, view, pose sweep and contact line comes from that module) in a
headless editor, walks the test matrix with the MCP tool `set_graphics_preset`
and the pose sweep with `edit_light` (the module's apply_light_pose()), renders
each view with `render_scene_image`, applies the section 6 gates, prints a
PASS / FAIL table, writes logs/shadow_verify/<timestamp>.json and exits
non-zero on a FAIL with --enforce.

Usage:
    py -3 scripts/shadow_verify.py [--matrix core|full] [--poses full|short]
                                   [--station NAME[,NAME]] [--light TYPE[,TYPE]]
                                   [--config NAME[,NAME]] [--around PRESET|all]
                                   [--set KEY=VALUE ...] [--root-offset X ...]
                                   [--runs N] [--enforce] [--no-g6]
                                   [--save-images none|failing|all] [--workers N]
                                   [--list-configs] [--reuse] [--port N] [--editor PATH]

Renders (all `output: "linear"`, `msaa_samples: 0`, so pixels of the two
renders of one camera correspond 1:1; PNG output is never measured, it has
bloom):
  - shader_debug 36 world_position, color_format rgba32f: the receiver world
    position per pixel. It depends only on the camera and the station root,
    so it is rendered once per (station, view, camera offset) and cached for
    every config, light and pose.
  - shader_debug 30 shadow_visibility of the station light
    (`shadow_debug_light`), rgba16f: 0 = shadowed .. 1 = lit. The reply's
    `shadow_lights` entry gives the light's texture_from_world and map
    resolution as that render's shadow pass used them (the directional fit
    follows the camera).

Ground truth per pixel. The pixel's box is the station box whose surface is
nearest its world position (within 3 mm; farther pixels are "unknown" and
excluded); its face normal is that box face's normal. The pixel is occluded
when the segment from its world position to the light (point / spot: to the
light position; directional: along -direction, 10 km) overlaps a
shadow-casting station box other than its own by more than 0.1 mm (oriented
slab test in the box frame). Excluded from every gate: background, unknown
surface, N.L < 0.05 (back-facing or beyond the R1 grazing limit - the lit
shading multiplies visibility by max(N.L, 0), so visibility there does not
reach the image), outside the light's shadow map (uv outside the one-texel
border, w <= 0), outside the spot cone, beyond the light range.

Edge band (section 6: within the filter footprint radius + 1 shadow texel of
the analytic shadow boundary, in shadow-map texel space). With J the
Jacobian of the light's texel coordinates at the receiver point (directional
/ spot: uv of texture_from_world, one texel = 1 / resolution; point: the cube
face coordinates (v_a / |v_major|, v_b / |v_major|), one texel =
2 / cube resolution), the in-plane world displacements Du, Dv that move one
texel along the light's u / v axes are solved in the receiver's face plane
(tangent basis T: (J T) x = texel step). The pixel is in the band when the
analytic occlusion at any corner P + r (i Du + j Dv), i, j = +-1, of its
footprint square (r = filter radius + 1) differs from the occlusion at P (a
straight boundary crossing the square separates a corner from the centre).
A thin shadow, or a shadow corner poking into the square between two
corners, is caught in the image instead: the pixel is also in the band when
a 4-neighbour pixel pair on one box with differing analytic occlusion lies
within ceil(sqrt(2) r texels) of it (texels converted to pixels with the
pixel's world footprint; at most 32 pixels). Filter radius in texels: hard
0.5, pcf_2x2 1, pcf_4x4 2, pcf_6x6 3 (distance technique the same K); point
lights 0.5 (the cube lookup is a single compare).

Gates (section 6; every gate is the worst value over poses, views and runs):
  G1 acne:       band-free lit pixels with visibility < 0.999: count = 0.
  G2 occlusion:  band-free shadowed pixels with visibility > 0.001: count = 0.
  G3 contact:    contact_blocks. Along every contact line (a resting caster's
                 footprint edge) in 16 bins, the distance from the line to
                 the nearest floor pixel with visibility <= 0.5 on the shadow
                 side (searched up to the analytic shadow extent), in shadow
                 texels through J. Only bins whose pixels are <= 0.5 texel
                 and whose analytic shadow starts at the line count. <= 1.5
                 texels (depth), <= 2.5 (distance technique). A bin with no
                 shadowed pixel reports its analytic extent.
  G4 leaks:      thin_walls. Per hut, pixels inside the hut (analytically
                 shadowed: every interior point is behind a wall) with
                 visibility > 0.5. Gated (= 0) for walls >= 2 cm at a map
                 resolution >= 2048; reported for all. The hut interiors
                 are left out of G1 / G2 (G4 owns them).
  G5 edge:       contact_blocks and spot_cones. Measured edges are the 0.5
                 visibility crossings between 4-neighbour pixels on one box
                 (linear interpolation of the world positions), where the
                 pair spans <= 0.5 texel. The offset is the texel-space
                 distance to the nearest analytic boundary (16 directions,
                 steps of 1/16 texel to 2, 1/4 to 8 texels), positive when
                 the crossing lies in the analytically lit region. Crossings
                 with no boundary within 8 texels are counted as spurious
                 (G1 / G2 see them). |mean signed| <= 1 texel per image,
                 worst |offset| <= filter radius + 1.
  G6 stability:  directional light, head_on_floor and contact_blocks, first
                 view, default pose: eight renders with the camera moved by
                 k / 8 shadow texel (k = 1..8) along the texel u axis (texel
                 world size from texture_from_world), world position
                 re-rendered per camera. Pixels outside the band and the
                 excluded set in both renders, on the same box and the same
                 analytic side in both, whose visibility changed by > 0.001:
                 count = 0.
  G7 cost:       n/a (phase 3).

Matrix (section 5). The configs of every committed preset of
config/editor/graphics_presets.json are set field by field with
set_graphics_preset (plus use_draw_lists true, the committed default), so the
editor's active preset does not matter. --matrix core adds one-axis-at-a-time
variations around the --around preset (default Medium; "all" = around each
preset): shadow_filter, shadow_bias (slope_scaled, wide filters only),
shadow_technique, shadow_depth_bits (16, 24, 32; values the device does not
support are reported as skipped and a preset value it does not support runs
at the next larger supported size, as the editor does), shadow_cull_mode,
forward-Z (a second editor launched with ERHE_FORCE_DISABLE_REVERSE_DEPTH=1;
skipped with --reuse), use_draw_lists, shadow_resolution and
point_shadow_resolution (both 512, both 2048). --matrix full is the product of
the axes. The distance technique covers directional and spot lights only (the
point light path samples its distance cube for both techniques); point x
distance cells are reported as unsupported.

Poses: --poses short (default) = the module's "short" sweep (5 poses per
station and light type), full = section 5's sweep. head_on_floor directional
poses move the camera with the lateral light offset (the directional fit
follows the camera). Measured on a Debug headless Vulkan editor (AMD iGPU):
see doc/plans/shadow_robustness.md section 9 for the wall time of the
default core run.

Analysis runs in --workers processes (default: half the CPUs; more slow the
editor's renders) while the editor renders; occlusion and band masks are
cached per world image and light pose, so configs that differ only in how the
shader uses the map share them.
--root-offset translates the station root along x (R7: 1000 10000). Output: the table on stdout, logs/shadow_verify/<timestamp>.json
(per cell worst values plus per pose / view detail of every failure: failing
pixel count and example pixel coordinates with world points), and with
--save-images failing the visibility image of the worst image of every failing cell
under logs/shadow_verify/<timestamp>/ (gray = visibility, red = G1 pixel,
cyan = G2 pixel, dark blue = excluded).

Without --reuse the script launches its own headless editor(s)
(ERHE_AI_DRIVER=1, empty startup commands, preferred MCP port --port),
backs up every file under config/ and restores changed ones byte-exactly
afterwards, and stops only the editor processes it launched.
"""

import argparse
import concurrent.futures
import datetime
import fnmatch
import itertools
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

import numpy as np

SCRIPTS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPTS_DIR)
sys.path.insert(0, SCRIPTS_DIR)
sys.path.insert(0, os.path.join(SCRIPTS_DIR, "creations"))

import creation_25_shadow_test_rooms as rooms  # noqa: E402

DEFAULT_EDITOR = os.path.join("build_vs2026_vulkan_headless", "bin", "Debug", "editor.exe")
LOG_PATH = os.path.join(REPO_ROOT, "logs", "log.txt")
OUT_DIR = os.path.join(REPO_ROOT, "logs", "shadow_verify")
PRESETS_PATH = os.path.join(REPO_ROOT, "config", "editor", "graphics_presets.json")
LIGHT_TYPES = ["directional", "spot", "point"]

PRESET_FIELDS = ["shadow_filter", "shadow_bias", "shadow_technique", "shadow_cull_mode", "shadow_depth_bits",
                 "shadow_resolution", "point_shadow_resolution", "shadow_depth_bias_constant",
                 "shadow_depth_bias_slope"]
FILTERS = ["hard", "pcf_2x2", "pcf_4x4", "pcf_6x6"]
WIDE_FILTERS = ["pcf_4x4", "pcf_6x6"]
FILTER_RADIUS = {"hard": 0.5, "pcf_2x2": 1.0, "pcf_4x4": 2.0, "pcf_6x6": 3.0}
POINT_FILTER_RADIUS = 0.5

# Ground truth tolerances.
SURFACE_TOLERANCE_M = 0.003      # pixel -> box surface
OCCLUSION_OVERLAP_M = 1.0e-4     # segment / box overlap that counts as a hit
DIRECTIONAL_FAR_M = 1.0e4
MIN_N_DOT_L = 0.05               # R1's grazing limit
LIT_BELOW = 0.999                # G1: a lit pixel darker than this is acne
SHADOW_ABOVE = 0.001             # G2: a shadowed pixel brighter than this leaks
STABLE_DELTA = 0.001             # G6
MAX_PIXEL_TEXELS = 0.5           # G3 / G5: pixel footprint limit
G5_SEARCH = np.concatenate([np.arange(1.0, 32.0) / 16.0, np.arange(2.0, 8.0 + 1.0e-9, 0.25)])
G5_DIRECTIONS = 16
G3_BINS = 16
EXAMPLES = 5
IMAGE_BAND_MAX_PX = 32

G3_LIMIT = {"depth": 1.5, "distance": 2.5}
G4_MIN_THICKNESS = 0.02 - 1.0e-9
G4_MIN_RESOLUTION = 2048
G6_STATIONS = ["head_on_floor", "contact_blocks"]
G5_STATIONS = ["contact_blocks", "spot_cones"]
GATES = ["G1", "G2", "G3", "G4", "G5", "G6", "G7"]


# --- editor process + config hygiene -------------------------------------------------

class Config_backup:
    """Byte-exact copies of every file under config/ (an editor run rewrites
    window / settings files); restore() writes back the changed ones."""

    def __init__(self):
        self.directory = tempfile.mkdtemp(prefix="shadow_verify_config_")
        self.saved = []
        root = os.path.join(REPO_ROOT, "config")
        for folder, _, files in os.walk(root):
            for name in files:
                source = os.path.join(folder, name)
                relative = os.path.relpath(source, REPO_ROOT)
                target = os.path.join(self.directory, relative.replace(os.sep, "__"))
                shutil.copyfile(source, target)
                self.saved.append((relative, target))

    def restore(self):
        for relative, target in self.saved:
            destination = os.path.join(REPO_ROOT, relative)
            with open(target, "rb") as handle:
                data = handle.read()
            current = None
            if os.path.isfile(destination):
                with open(destination, "rb") as handle:
                    current = handle.read()
            if current != data:
                with open(destination, "wb") as handle:
                    handle.write(data)
                print(f"restored {relative}")
        known = {relative for relative, _ in self.saved}
        for folder, _, files in os.walk(os.path.join(REPO_ROOT, "config")):
            for name in files:
                relative = os.path.relpath(os.path.join(folder, name), REPO_ROOT)
                if relative not in known:
                    print(f"note: {relative} appeared during the run (left in place)")
        shutil.rmtree(self.directory, ignore_errors=True)


def launch_editor(editor_exe, port, forward_z):
    """Launch a headless editor without a default scene; return (process, port).
    The editor is identified by its pid in its 'MCP server: listening' log line."""
    exe = editor_exe if os.path.isabs(editor_exe) else os.path.join(REPO_ROOT, editor_exe)
    if not os.path.isfile(exe):
        raise RuntimeError(f"no editor at {exe}")
    env = dict(os.environ, ERHE_AI_DRIVER="1", ERHE_MCP_PORT=str(port))
    if forward_z:
        env["ERHE_FORCE_DISABLE_REVERSE_DEPTH"] = "1"
    else:
        env.pop("ERHE_FORCE_DISABLE_REVERSE_DEPTH", None)
    flags = 0
    if os.name == "nt":
        flags = subprocess.DETACHED_PROCESS | subprocess.CREATE_NO_WINDOW
    process = subprocess.Popen([exe, "--commands", os.path.join("config", "editor", "commands_empty.json")],
                               cwd=REPO_ROOT, env=env, creationflags=flags,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    pattern = re.compile(r"MCP server: listening on 127\.0\.0\.1:(\d+) \(pid (\d+)")
    deadline = time.monotonic() + 300.0
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"editor exited with {process.returncode} during startup; see logs/log.txt")
        try:
            with open(LOG_PATH, "r", encoding="utf-8", errors="replace") as handle:
                text = handle.read()
        except OSError:
            text = ""
        for match in pattern.finditer(text):
            if int(match.group(2)) == process.pid:
                tail = text[match.end():]
                if "Main loop: completed frame 12" in tail:
                    return process, int(match.group(1))
        time.sleep(1.0)
    process.kill()
    raise RuntimeError("editor did not become ready")


def stop_editor(c, process):
    try:
        if c is not None:
            c.client.call("request_exit")
    except Exception:  # noqa: BLE001 - the editor may already be gone
        pass
    try:
        process.wait(timeout=60.0)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10.0)


# --- matrix ---------------------------------------------------------------------------

def load_presets():
    with open(PRESETS_PATH, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    return {p["name"]: p for p in data["presets"]}


def effective_depth_bits(bits, supported):
    """The size the editor uses for a preset depth size: the value itself when
    supported, else the next larger supported size (else the largest)."""
    if bits in supported:
        return bits
    larger = sorted(b for b in supported if b > bits)
    return larger[0] if larger else max(supported)


def base_config(name, preset, supported_bits):
    fields = {k: preset[k] for k in PRESET_FIELDS}
    note = ""
    bits = effective_depth_bits(int(fields["shadow_depth_bits"]), supported_bits)
    if bits != fields["shadow_depth_bits"]:
        note = f"preset depth {fields['shadow_depth_bits']} unsupported -> {bits}"
        fields["shadow_depth_bits"] = bits
    fields["use_draw_lists"] = True
    return {"name": name, "fields": fields, "forward_z": False, "note": note}


def core_variations(base, supported_bits, skipped):
    f = base["fields"]
    out = []

    def add(label, forward_z=False, **changes):
        fields = dict(f, **changes)
        out.append({"name": f"{base['name']}/{label}", "fields": fields, "forward_z": forward_z, "note": ""})

    for value in FILTERS:
        if value != f["shadow_filter"]:
            add(f"shadow_filter={value}", shadow_filter=value)
    if f["shadow_filter"] in WIDE_FILTERS:
        for value in ("receiver_plane", "slope_scaled"):
            if value != f["shadow_bias"]:
                add(f"shadow_bias={value}", shadow_bias=value)
    for value in ("depth", "distance"):
        if value != f["shadow_technique"]:
            add(f"shadow_technique={value}", shadow_technique=value)
    for value in (16, 24, 32):
        if value == f["shadow_depth_bits"]:
            continue
        if value not in supported_bits:
            skipped.append(f"{base['name']}/shadow_depth_bits={value}: unsupported by the device")
            continue
        add(f"shadow_depth_bits={value}", shadow_depth_bits=value)
    for value in ("cull_front", "cull_back", "cull_none"):
        if value != f["shadow_cull_mode"]:
            add(f"shadow_cull_mode={value}", shadow_cull_mode=value)
    add("forward_z", forward_z=True)
    add(f"use_draw_lists={'false' if f['use_draw_lists'] else 'true'}", use_draw_lists=not f["use_draw_lists"])
    for value in (512, 2048):
        if (value != f["shadow_resolution"]) or (value != f["point_shadow_resolution"]):
            add(f"resolution={value}", shadow_resolution=value, point_shadow_resolution=value)
    return out


def full_matrix(base, supported_bits, skipped):
    out = []
    for bits in (16, 24, 32):
        if bits not in supported_bits:
            skipped.append(f"full/shadow_depth_bits={bits}: unsupported by the device")
    bits_values = [b for b in (16, 24, 32) if b in supported_bits]
    for filt, tech, bits, cull, fz, dl, res in itertools.product(
            FILTERS, ("depth", "distance"), bits_values, ("cull_front", "cull_back", "cull_none"),
            (False, True), (True, False), (512, 2048)):
        biases = ["receiver_plane", "slope_scaled"] if filt in WIDE_FILTERS else [base["fields"]["shadow_bias"]]
        for bias in biases:
            fields = dict(base["fields"], shadow_filter=filt, shadow_bias=bias, shadow_technique=tech,
                          shadow_depth_bits=bits, shadow_cull_mode=cull, use_draw_lists=dl,
                          shadow_resolution=res, point_shadow_resolution=res)
            name = (f"full/{filt}/{bias}/{tech}/d{bits}/{cull}/{'fz' if fz else 'rz'}/"
                    f"{'dl' if dl else 'nodl'}/r{res}")
            out.append({"name": name, "fields": fields, "forward_z": fz, "note": ""})
    return out


def build_matrix(args, supported_bits, skipped):
    presets = load_presets()
    bases = [base_config(name, p, supported_bits) for name, p in presets.items()]
    configs = list(bases)
    if args.matrix == "core":
        around = [b for b in bases if (args.around == "all") or (b["name"] == args.around)]
        if not around:
            raise SystemExit(f"--around {args.around!r}: no such preset ({', '.join(presets)})")
        for base in around:
            configs += core_variations(base, supported_bits, skipped)
    else:
        medium = next((b for b in bases if b["name"] == args.around), bases[0])
        configs += full_matrix(medium, supported_bits, skipped)
    # Dedupe identical configs (a variation can equal another preset).
    seen = {}
    unique = []
    for config in configs:
        key = (json.dumps(config["fields"], sort_keys=True), config["forward_z"])
        if key in seen:
            continue
        seen[key] = config["name"]
        unique.append(config)
    return unique


# --- station geometry ---------------------------------------------------------------------

def quat_to_matrix(q):
    x, y, z, w = q
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w)],
        [2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)],
    ], dtype=np.float64)


class Geometry:
    """World-space boxes of one station at one root offset."""

    def __init__(self, station_name, root_offset, dtype=np.float64):
        station = rooms.STATIONS[station_name]
        self.names = []
        centers, rotations, halves, casts = [], [], [], []
        for b in station["boxes"]:
            w = rooms.box_to_world(b, root_offset)
            self.names.append(b["name"])
            centers.append(w["center"])
            rotations.append(quat_to_matrix(b["rotation_xyzw"]))   # columns = local axes in world
            halves.append(b["half_extents"])
            casts.append(bool(b["casts_shadow"]))
        self.center = np.array(centers, dtype=dtype)
        self.rotation = np.array(rotations, dtype=dtype)
        self.half = np.array(halves, dtype=dtype)
        self.casts = np.array(casts, dtype=bool)
        self.count = len(self.names)


class Light:
    def __init__(self, pose_world, offset=(0.0, 0.0, 0.0), dtype=np.float64):
        self.type = pose_world["type"]
        self.position = (np.array(pose_world["position"], dtype=np.float64) - np.array(offset)).astype(dtype)
        self.direction = np.array(pose_world.get("direction", [0.0, -1.0, 0.0]), dtype=dtype)
        self.range = float(pose_world.get("range", 0.0))
        self.outer_deg = float(pose_world.get("outer_spot_angle_deg", 180.0))


def surface_of(points, geo):
    """-> (own box index or -1, face normal) of world points (N, 3)."""
    n = len(points)
    best = np.full(n, np.inf)
    own = np.full(n, -1, dtype=np.int32)
    normal = np.zeros((n, 3))
    for b in range(geo.count):
        q = (points - geo.center[b]) @ geo.rotation[b]
        m = np.abs(q) - geo.half[b]
        axis = np.argmax(m, axis=1)
        s = m[np.arange(n), axis]
        better = np.abs(s) < best
        if not better.any():
            continue
        best[better] = np.abs(s[better])
        own[better] = b
        sign = np.sign(q[np.arange(n), axis])[better]
        sign[sign == 0.0] = 1.0
        normal[better] = geo.rotation[b][:, axis[better]].T * sign[:, None]
    own[best > SURFACE_TOLERANCE_M] = -1
    return own, normal


def occluded(points, own, light, geo):
    """Analytic occlusion of world points (N, 3) toward the light, ignoring
    each point's own box. Per box: a bounding sphere reject (distance from
    the box centre to the segment), then the slab test in the box frame on
    the survivors."""
    n = len(points)
    hit = np.zeros(n, dtype=bool)
    if n == 0:
        return hit
    if light.type == "directional":
        D = np.broadcast_to(-light.direction / np.linalg.norm(light.direction) * DIRECTIONAL_FAR_M, points.shape)
    else:
        D = light.position - points
    DD = np.einsum("ij,ij->i", D, D)
    for b in range(geo.count):
        if not geo.casts[b]:
            continue
        R = geo.rotation[b]
        h = geo.half[b]
        w = geo.center[b] - points
        t = np.clip(np.einsum("ij,ij->i", w, D) / DD, 0.0, 1.0)
        diff = w - (t[:, None] * D)
        candidate = (np.einsum("ij,ij->i", diff, diff) <= float(np.dot(h, h))) & (own != b)
        index = np.nonzero(candidate)[0]
        if index.size == 0:
            continue
        oc = (points[index] - geo.center[b]) @ R
        d = D[index] @ R
        with np.errstate(divide="ignore", invalid="ignore"):
            inv = 1.0 / d
            t1 = (-h - oc) * inv
            t2 = (h - oc) * inv
            t_near = np.maximum(np.minimum(t1, t2).max(axis=1), 0.0)
            t_far = np.minimum(np.maximum(t1, t2).min(axis=1), 1.0)
            overlap = (t_far - t_near) * np.sqrt(DD[index])
        hit[index[overlap > OCCLUSION_OVERLAP_M]] = True
    return hit


def texel_jacobian(points, light, M, resolution):
    """-> (J (N, 2, 3), texel step, in_map (N,)): Jacobian of the light's
    texel coordinates at the points, the size of one texel in those
    coordinates and whether the point maps into the shadow map."""
    n = len(points)
    if light.type == "point":
        v = points - light.position
        major = np.argmax(np.abs(v), axis=1)
        a_axis = (major + 1) % 3
        b_axis = (major + 2) % 3
        rows = np.arange(n)
        vm = v[rows, major]
        J = np.zeros((n, 2, 3))
        for k, other in enumerate((a_axis, b_axis)):
            J[rows, k, other] = 1.0 / np.abs(vm)
            J[rows, k, major] = -v[rows, other] * np.sign(vm) / (vm * vm)
        in_map = np.ones(n, dtype=bool)
        return J, 2.0 / resolution[0], in_map
    ph = np.concatenate([points, np.ones((n, 1))], axis=1)
    num = ph @ M.T                  # (N, 4) rows: u*w, v*w, z*w, w
    w = num[:, 3]
    u = num[:, 0] / w
    v = num[:, 1] / w
    J = np.zeros((n, 2, 3))
    J[:, 0, :] = (M[0, :3][None, :] * w[:, None] - num[:, 0:1] * M[3, :3][None, :]) / (w * w)[:, None]
    J[:, 1, :] = (M[1, :3][None, :] * w[:, None] - num[:, 1:2] * M[3, :3][None, :]) / (w * w)[:, None]
    border_u = 1.0 / resolution[0]
    border_v = 1.0 / resolution[1]
    in_map = (w > 0.0) & (u >= border_u) & (u <= 1.0 - border_u) & (v >= border_v) & (v <= 1.0 - border_v)
    return J, 1.0 / resolution[0], in_map


def texel_displacements(J, normal, step):
    """In-plane world displacements (Du, Dv) of one texel along the light's
    texel axes: solve (J T) x = step e_k in the receiver's tangent basis T."""
    helper = np.where(np.abs(normal[:, 0:1]) < 0.9, np.array([[1.0, 0.0, 0.0]]), np.array([[0.0, 1.0, 0.0]]))
    t1 = np.cross(normal, helper)
    t1 /= np.linalg.norm(t1, axis=1, keepdims=True)
    t2 = np.cross(normal, t1)
    T = np.stack([t1, t2], axis=2)          # (N, 3, 2)
    A = J @ T                               # (N, 2, 2)
    det = A[:, 0, 0] * A[:, 1, 1] - A[:, 0, 1] * A[:, 1, 0]
    ok = np.abs(det) > 1.0e-12
    safe = np.where(ok, det, 1.0)
    inv = np.empty_like(A)
    inv[:, 0, 0] = A[:, 1, 1] / safe
    inv[:, 0, 1] = -A[:, 0, 1] / safe
    inv[:, 1, 0] = -A[:, 1, 0] / safe
    inv[:, 1, 1] = A[:, 0, 0] / safe
    Du = (T @ (inv[:, :, 0:1] * step))[:, :, 0]
    Dv = (T @ (inv[:, :, 1:2] * step))[:, :, 0]
    Du[~ok] = np.nan
    Dv[~ok] = np.nan
    return Du, Dv, ok


# --- image analysis (runs in worker processes) --------------------------------------------

_WORLD_CACHE = {}
_GEO_CACHE = {}


def read_pfm(path):
    with open(path, "rb") as handle:
        kind = handle.readline().strip()
        width, height = (int(v) for v in handle.readline().split())
        scale = float(handle.readline().strip())
        data = np.fromfile(handle, dtype="<f4" if scale < 0 else ">f4")
    channels = 3 if kind == b"PF" else 1
    image = data.reshape(height, width, channels)
    return np.flipud(image).copy()


def geometry(station, offset, dtype=np.float64):
    key = (station, tuple(offset), np.dtype(dtype).name)
    if key not in _GEO_CACHE:
        _GEO_CACHE[key] = Geometry(station, offset, dtype)
    return _GEO_CACHE[key]


def world_data(path, station, offset):
    """Per world-position image: flat pixel indices of covered pixels, their
    world points, own box and face normal (cached per worker)."""
    if path in _WORLD_CACHE:
        return _WORLD_CACHE[path]
    if len(_WORLD_CACHE) > 48:
        _WORLD_CACHE.clear()
    world = np.load(path).astype(np.float64)
    h, w, _ = world.shape
    flat = world.reshape(-1, 3)
    covered = np.nonzero(~np.isnan(flat).any(axis=1))[0]
    points = flat[covered]
    geo = geometry(station, offset)
    own, normal = surface_of(points, geo)
    data = {"shape": (h, w), "index": covered, "points": points, "own": own, "normal": normal}
    _WORLD_CACHE[path] = data
    return data


def cache_key(*parts):
    import hashlib
    return hashlib.sha1(json.dumps(parts, sort_keys=True, default=str).encode("utf-8")).hexdigest()


def cache_load(job, key, count):
    path = os.path.join(job["cache_dir"], key + ".npy")
    if not os.path.isfile(path):
        return None
    try:
        packed = np.load(path)
    except (OSError, ValueError):
        return None
    return np.unpackbits(packed, count=count).astype(bool)


def cache_store(job, key, mask):
    path = os.path.join(job["cache_dir"], key + ".npy")
    temporary = f"{path}.{os.getpid()}.tmp.npy"
    np.save(temporary, np.packbits(mask))
    os.replace(temporary, path)


def image_band(wd, occ, excluded, Du, Dv, r):
    """The part of the edge band the corner samples can miss, found in the
    image (see the module docstring)."""
    h, w = wd["shape"]
    size = h * w
    index = wd["index"]
    occ_img = np.full(size, -1, dtype=np.int8)
    occ_img[index[~excluded]] = occ[~excluded].astype(np.int8)
    own_img = np.full(size, -1, dtype=np.int32)
    own_img[index] = wd["own"]
    occ_img = occ_img.reshape(h, w)
    own_img = own_img.reshape(h, w)
    boundary = np.zeros((h, w), dtype=bool)
    for a_sl, b_sl in (((slice(None), slice(None, -1)), (slice(None), slice(1, None))),
                       ((slice(None, -1), slice(None)), (slice(1, None), slice(None)))):
        oa, ob = occ_img[a_sl], occ_img[b_sl]
        edge = (oa >= 0) & (ob >= 0) & (oa != ob) & (own_img[a_sl] == own_img[b_sl])
        boundary[a_sl] |= edge
        boundary[b_sl] |= edge
    if not boundary.any():
        return np.zeros(len(index), dtype=bool)
    # Pixel world size from the row neighbour (column neighbour at the edge).
    world = np.full((size, 3), np.nan)
    world[index] = wd["points"]
    world = world.reshape(h, w, 3)
    step = np.full((h, w), np.nan)
    step[:, :-1] = np.linalg.norm(world[:, 1:] - world[:, :-1], axis=2)
    column = np.linalg.norm(world[1:, :] - world[:-1, :], axis=2)
    step[:-1, :] = np.where(np.isnan(step[:-1, :]), column, step[:-1, :])
    texel = np.maximum(np.linalg.norm(Du, axis=1), np.linalg.norm(Dv, axis=1))
    pixel = step.reshape(-1)[index]
    with np.errstate(divide="ignore", invalid="ignore"):
        radius_px = np.ceil(r * math.sqrt(2.0) * texel / pixel)
    radius_px = np.nan_to_num(radius_px, nan=1.0, posinf=IMAGE_BAND_MAX_PX)
    radius_px = np.clip(radius_px, 1, IMAGE_BAND_MAX_PX).astype(np.int32)
    out = np.zeros(len(index), dtype=bool)
    grown = boundary
    for k in range(1, int(radius_px.max()) + 1):
        padded = np.pad(grown, 1)
        grown = (padded[:-2, :-2] | padded[:-2, 1:-1] | padded[:-2, 2:] | padded[1:-1, :-2] | padded[1:-1, 1:-1]
                 | padded[1:-1, 2:] | padded[2:, :-2] | padded[2:, 1:-1] | padded[2:, 2:])
        at_k = radius_px == k
        if at_k.any():
            out[at_k] = grown.reshape(-1)[index[at_k]]
    return out


def classify(job):
    """Per covered pixel: excluded / occluded / band, plus J and the texel
    displacements (reused by G3 / G5)."""
    wd = world_data(job["world_path"], job["station"], job["offset"])
    geo = geometry(job["station"], job["offset"])
    light = Light(job["light"])
    points = wd["points"]
    own = wd["own"]
    normal = wd["normal"]
    if light.type == "directional":
        to_light = np.broadcast_to(-light.direction / np.linalg.norm(light.direction), points.shape)
        distance = np.zeros(len(points))
    else:
        delta = light.position - points
        distance = np.linalg.norm(delta, axis=1)
        to_light = delta / np.maximum(distance, 1.0e-12)[:, None]
    n_dot_l = np.einsum("ij,ij->i", normal, to_light)
    # Occlusion runs in station-local float32 (station extents are metres,
    # so float32 resolves far below OCCLUSION_OVERLAP_M at any root offset).
    offset = np.array(job["offset"], dtype=np.float64)
    geo_local = geometry(job["station"], (0.0, 0.0, 0.0), np.float32)
    light_local = Light(job["light"], offset, np.float32)

    def occ_fn(world_points, own_index):
        return occluded((world_points - offset).astype(np.float32), own_index, light_local, geo_local)

    M = np.array(job["texture_from_world"], dtype=np.float64) if job["texture_from_world"] is not None else None
    J, step, in_map = texel_jacobian(points, light, M, job["resolution"])
    excluded = (own < 0) | ~(n_dot_l >= MIN_N_DOT_L) | ~in_map
    if light.type == "spot":
        axis = light.direction / np.linalg.norm(light.direction)
        cos_angle = np.einsum("ij,j->i", -to_light, axis)
        excluded |= cos_angle < math.cos(math.radians(0.5 * light.outer_deg))
    if (light.type in ("spot", "point")) and (light.range > 0.0):
        excluded |= distance > light.range
    Du, Dv, ok = texel_displacements(J, normal, step)
    excluded |= ~ok
    r = job["band_radius"]
    # The occlusion depends on the world image and the light pose, the band
    # also on the light's texel mapping and radius: both are cached on disk
    # (job["cache_dir"]), so configs that differ only in what the shader does
    # with the map (cull, bias, depth bits, technique, ...) share them.
    occ_key = cache_key(job["world_path"], job["light"])
    band_key = cache_key(occ_key, r, job["resolution"],
                         None if M is None else np.round(M[[0, 1, 3], :], 7).tolist())
    occ = cache_load(job, occ_key, len(points))
    if occ is None:
        occ = occ_fn(points, own)
        cache_store(job, occ_key, occ)
    band = cache_load(job, band_key, len(points))
    if band is None:
        band = np.zeros(len(points), dtype=bool)
        active = np.nonzero(~excluded)[0]
        for i, j in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
            moved = points[active] + r * ((i * Du[active]) + (j * Dv[active]))
            band[active] |= occ_fn(moved, own[active]) != occ[active]
        cache_store(job, band_key, band)
    band |= image_band(wd, occ, excluded, Du, Dv, r)
    return {"wd": wd, "geo": geo, "light": light, "excluded": excluded, "occ": occ, "band": band,
            "J": J, "step": step, "Du": Du, "Dv": Dv, "n_dot_l": n_dot_l, "occ_fn": occ_fn}


def examples(wd, mask, vis_flat):
    idx = np.nonzero(mask)[0][:EXAMPLES]
    h, w = wd["shape"]
    out = []
    for k in idx:
        pixel = int(wd["index"][k])
        out.append({"pixel": [pixel % w, pixel // w], "world": [round(float(v), 5) for v in wd["points"][k]],
                    "visibility": round(float(vis_flat[pixel]), 4)})
    return out


def texel_length(J, vector, step):
    """Length in texels of world displacement `vector` (N, 3) through J."""
    return np.linalg.norm(np.einsum("nij,nj->ni", J, vector), axis=1) / step


def measure_g3(job, cls, vis_flat):
    """Contact gap along the station's contact lines -> (worst texels, detail)."""
    wd, geo, light = cls["wd"], cls["geo"], cls["light"]
    points, own = wd["points"], wd["own"]
    floor = geo.names.index("Floor")
    usable = (own == floor) & ~cls["excluded"]
    # Pixel footprint in texels: distance to the next pixel in the row.
    h, w = wd["shape"]
    world_full = np.full((h * w, 3), np.nan)
    world_full[wd["index"]] = points
    neighbour = np.roll(world_full.reshape(h, w, 3), -1, axis=1).reshape(-1, 3)[wd["index"]]
    pixel_step = texel_length(cls["J"], neighbour - points, cls["step"])
    usable &= np.nan_to_num(pixel_step, nan=np.inf) <= MAX_PIXEL_TEXELS
    shadowed = vis_flat[wd["index"]] <= 0.5
    worst = None
    detail = []
    offset = np.array(job["offset"], dtype=np.float64)
    station = rooms.STATIONS[job["station"]]
    for caster, lines in station["contact_lines"].items():
        caster_index = geo.names.index(caster)
        center = geo.center[caster_index]
        for p0, p1 in lines:
            a = np.array(p0, dtype=np.float64) + offset
            b = np.array(p1, dtype=np.float64) + offset
            length = np.linalg.norm(b - a)
            t = (b - a) / length
            outward = np.array([t[2], 0.0, -t[0]])
            mid = 0.5 * (a + b)
            if np.dot(outward, mid - center) < 0.0:
                outward = -outward
            rel = points - a
            along = rel @ t
            out = rel @ outward
            for k in range(G3_BINS):
                s0 = length * (0.1 + (0.8 * k / G3_BINS))
                s1 = length * (0.1 + (0.8 * (k + 1) / G3_BINS))
                q = a + (0.5 * (s0 + s1)) * t
                # Analytic shadow extent along `outward` from q (texels).
                probe = q[None, :] + outward[None, :] * 1.0e-4
                if not cls["occ_fn"](probe, np.array([floor]))[0]:
                    continue
                Jq, step_q, _ = texel_jacobian(q[None, :], light,
                                               np.array(job["texture_from_world"]) if job["texture_from_world"] is not None else None,
                                               job["resolution"])
                texels_per_m = texel_length(Jq, outward[None, :], step_q)[0]
                if not (texels_per_m > 0.0):
                    continue
                steps = np.arange(1, 81) * (0.25 / texels_per_m)   # up to 20 texels
                line = q[None, :] + outward[None, :] * steps[:, None]
                occ = cls["occ_fn"](line, np.full(len(steps), floor))
                extent = steps[-1] if occ.all() else steps[np.argmin(occ)]
                in_bin = usable & (along >= s0) & (along < s1) & (out > 0.0) & (out <= extent)
                if not in_bin.any():
                    continue
                # The bin counts only where its pixels reach the line (the
                # strip next to it can be hidden or outside the view).
                if out[in_bin].min() * texels_per_m > 0.25:
                    continue
                hits = in_bin & shadowed
                if hits.any():
                    gap_m = float(out[hits].min())
                    missing = False
                else:
                    gap_m = float(extent)
                    missing = True
                gap = gap_m * texels_per_m
                entry = {"caster": caster, "edge": [round(float(v), 4) for v in (a - offset)] + [round(float(v), 4) for v in (b - offset)],
                         "bin": k, "gap_texels": round(gap, 3), "missing": missing}
                if (worst is None) or (gap > worst):
                    worst = gap
                if missing or (gap > job["g3_limit"]):
                    detail.append(entry)
    return worst, detail[:EXAMPLES]


def measure_g4(job, cls, vis_flat):
    """Lit pixels inside each hut -> {hut: {count, total, thickness, gated}}."""
    wd = cls["wd"]
    points = wd["points"]
    offset = np.array(job["offset"], dtype=np.float64)
    visible = ~cls["excluded"]
    lit = vis_flat[wd["index"]] > 0.5
    out = {}
    inside_any = np.zeros(len(points), dtype=bool)
    for hut in rooms.STATIONS["thin_walls"]["walls"]:
        lo = np.array(hut["interior_lo"]) + offset - 1.0e-3
        hi = np.array(hut["interior_hi"]) + offset + 1.0e-3
        inside = np.all((points >= lo) & (points <= hi), axis=1)
        inside_any |= inside
        mask = inside & visible
        leak = mask & lit
        out[hut["name"]] = {"thickness": hut["thickness"], "count": int(leak.sum()), "total": int(mask.sum()),
                            "gated": bool((hut["thickness"] >= G4_MIN_THICKNESS)
                                          and (job["resolution"][0] >= G4_MIN_RESOLUTION)),
                            "examples": examples(wd, leak, vis_flat)}
    return out, inside_any


def measure_g5(job, cls, vis_flat):
    """Edge placement of the measured 0.5 crossings -> dict or None."""
    wd, geo, light = cls["wd"], cls["geo"], cls["light"]
    h, w = wd["shape"]
    n_full = h * w
    slot = np.full(n_full, -1, dtype=np.int64)
    slot[wd["index"]] = np.arange(len(wd["index"]))
    usable = ~cls["excluded"]
    vis_c = vis_flat[wd["index"]]
    pairs_a, pairs_b = [], []
    grid = slot.reshape(h, w)
    for da, db in ((grid[:, :-1], grid[:, 1:]), (grid[:-1, :], grid[1:, :])):
        a = da.reshape(-1)
        b = db.reshape(-1)
        keep = (a >= 0) & (b >= 0)
        a, b = a[keep], b[keep]
        keep = usable[a] & usable[b] & (wd["own"][a] == wd["own"][b])
        a, b = a[keep], b[keep]
        va, vb = vis_c[a], vis_c[b]
        cross = ((va <= 0.5) & (vb > 0.5)) | ((vb <= 0.5) & (va > 0.5))
        pairs_a.append(a[cross])
        pairs_b.append(b[cross])
    a = np.concatenate(pairs_a)
    b = np.concatenate(pairs_b)
    if a.size == 0:
        return None
    pa, pb = wd["points"][a], wd["points"][b]
    span = texel_length(cls["J"][a], pb - pa, cls["step"])
    fine = np.nan_to_num(span, nan=np.inf) <= MAX_PIXEL_TEXELS
    a, b, pa, pb = a[fine], b[fine], pa[fine], pb[fine]
    if a.size == 0:
        return {"crossings": 0, "unresolved": int((~fine).sum())}
    va, vb = vis_c[a], vis_c[b]
    f = np.clip((0.5 - va) / (vb - va), 0.0, 1.0)
    x = pa + f[:, None] * (pb - pa)
    own = wd["own"][a]
    base = cls["occ_fn"](x, own)
    Du, Dv = cls["Du"][a], cls["Dv"][a]
    count = len(x)
    best = np.full(count, np.inf)
    angles = np.arange(G5_DIRECTIONS) * (2.0 * math.pi / G5_DIRECTIONS)
    for angle in angles:
        direction = (math.cos(angle) * Du) + (math.sin(angle) * Dv)
        pts = (x[:, None, :] + G5_SEARCH[None, :, None] * direction[:, None, :]).reshape(-1, 3)
        occ = cls["occ_fn"](pts, np.repeat(own, len(G5_SEARCH))).reshape(count, len(G5_SEARCH))
        differs = occ != base[:, None]
        first = np.where(differs.any(axis=1), G5_SEARCH[np.argmax(differs, axis=1)], np.inf)
        best = np.minimum(best, first)
    found = np.isfinite(best)
    signed = np.where(base[found], -best[found], best[found])
    result = {"crossings": int(found.sum()), "spurious": int((~found).sum()), "unresolved": int((~fine).sum())}
    if found.any():
        result["mean_signed"] = float(signed.mean())
        result["worst_abs"] = float(np.abs(signed).max())
        worst_k = np.nonzero(found)[0][np.argmax(np.abs(signed))]
        result["worst_world"] = [round(float(v), 5) for v in x[worst_k]]
    return result


def analyze(job):
    """One mode 30 render against the ground truth -> metrics dict."""
    started = time.time()
    vis = job["vis"]
    vis_flat = vis.reshape(-1)
    cls = classify(job)
    wd = cls["wd"]
    vis_c = vis_flat[wd["index"]]
    measured = ~cls["excluded"] & ~cls["band"]
    hut_interior = None
    result = {"key": job["key"]}
    if job["station"] == "thin_walls":
        result["G4"], hut_interior = measure_g4(job, cls, vis_flat)
        measured &= ~hut_interior
    lit = measured & ~cls["occ"]
    shadow = measured & cls["occ"]
    g1 = lit & (vis_c < LIT_BELOW)
    g2 = shadow & (vis_c > SHADOW_ABOVE)
    result["G1"] = {"count": int(g1.sum()), "total": int(lit.sum()), "examples": examples(wd, g1, vis_flat)}
    result["G2"] = {"count": int(g2.sum()), "total": int(shadow.sum()), "examples": examples(wd, g2, vis_flat)}
    result["pixels"] = {"covered": int(len(wd["index"])), "excluded": int(cls["excluded"].sum()),
                        "band": int((cls["band"] & ~cls["excluded"]).sum()), "unknown": int((wd["own"] < 0).sum())}
    if job.get("g3"):
        worst, detail = measure_g3(job, cls, vis_flat)
        result["G3"] = {"worst_texels": worst, "examples": detail}
    if job.get("g5"):
        result["G5"] = measure_g5(job, cls, vis_flat)
    if job.get("return_classes"):
        classes = np.zeros(vis_flat.shape, dtype=np.uint8)       # 0 background
        classes[wd["index"]] = 1                                  # measured
        classes[wd["index"][cls["excluded"]]] = 2
        classes[wd["index"][cls["band"] & ~cls["excluded"]]] = 3
        classes[wd["index"][g1]] = 4
        classes[wd["index"][g2]] = 5
        result["classes"] = classes.reshape(vis.shape)
    result["analysis_s"] = round(time.time() - started, 2)
    return result


def analyze_g6(job):
    """Visibility changes outside the band between the base camera and one
    moved camera, compared pixel by pixel."""
    base = dict(job, world_path=job["base_world_path"], vis=job["base_vis"],
                texture_from_world=job["base_texture_from_world"])
    moved = dict(job, world_path=job["moved_world_path"], vis=job["moved_vis"],
                 texture_from_world=job["moved_texture_from_world"])
    masks = []
    owners = []
    for j in (base, moved):
        cls = classify(j)
        wd = cls["wd"]
        size = wd["shape"][0] * wd["shape"][1]
        stable = np.zeros(size, dtype=bool)
        stable[wd["index"][~cls["excluded"] & ~cls["band"]]] = True
        # Surface and analytic class per pixel: a pixel that moved onto
        # another box or across the analytic boundary changes legitimately.
        owner = np.full(size, -2, dtype=np.int64)
        owner[wd["index"]] = (wd["own"].astype(np.int64) * 2) + cls["occ"].astype(np.int64)
        masks.append(stable)
        owners.append(owner)
    both = masks[0] & masks[1] & (owners[0] == owners[1])
    delta = np.abs(job["moved_vis"].reshape(-1) - job["base_vis"].reshape(-1))
    changed = both & (delta > STABLE_DELTA)
    idx = np.nonzero(changed)[0][:EXAMPLES]
    w = job["base_vis"].shape[1]
    return {"key": job["key"], "G6": {"count": int(changed.sum()), "total": int(both.sum()),
                                      "examples": [{"pixel": [int(k % w), int(k // w)]} for k in idx]}}


def run_job(job):
    if job["kind"] == "g6":
        return analyze_g6(job)
    return analyze(job)


# --- editor driving -------------------------------------------------------------------------

class Session:
    """One editor: station loading, config application and renders."""

    def __init__(self, c, run_dir, forward_z):
        self.c = c
        self.forward_z = forward_z
        self.tmp_dir = os.path.join(run_dir, "tmp")
        self.world_dir = os.path.join(run_dir, "world")
        self.cache_dir = os.path.join(run_dir, "cache")
        os.makedirs(self.cache_dir, exist_ok=True)
        os.makedirs(self.tmp_dir, exist_ok=True)
        os.makedirs(self.world_dir, exist_ok=True)
        self.world_cache = {}
        self.render_seconds = []
        self.station = None
        self.config_name = None

    def supported_depth_bits(self):
        try:
            self.c.call("set_graphics_preset", {"shadow_depth_bits": 1})
        except RuntimeError as error:
            match = re.search(r"supported: ([0-9, ]+)", str(error))
            if match:
                return sorted(int(v) for v in match.group(1).split(",") if v.strip())
            raise
        raise RuntimeError("set_graphics_preset accepted shadow_depth_bits 1")

    def environment(self):
        self.c.call("set_graphics_settings", {"headlight_when_unlit": False, "sky_enabled": False,
                                              "grid_visible": False})
        try:
            self.c.call("set_indirect_diffuse", {"source": "ambient"})
        except RuntimeError:
            pass

    def apply_config(self, config, overrides):
        args = dict(config["fields"])
        args.update(overrides)
        result = self.c.call("set_graphics_preset", args)
        if not result.get("shadow_enable", True):
            raise RuntimeError("the active preset has shadow_enable false")
        for key, value in args.items():
            if key in result and result[key] != value:
                raise RuntimeError(f"set_graphics_preset: {key} = {result[key]}, requested {value}")
        self.config_name = config["name"]
        self.c.settle()
        return result

    def load_station(self, name, offset):
        station = rooms.STATIONS[name]
        self.c.close_all_scenes()
        self.c.load(station["asset"])
        if any(v != 0.0 for v in offset):
            self.c.set_node_transform(station["root"], translation=list(offset))
        self.c.settle()
        self.station = name

    def render(self, camera, view, shader_debug, light_name=None):
        path = os.path.join(self.tmp_dir, f"render_{shader_debug}.pfm")
        args = {"scene": self.c.scene, "camera": camera, "width": view["width"], "height": view["height"],
                "output": "linear", "shader_debug": shader_debug, "msaa_samples": 0,
                "path": os.path.relpath(path, REPO_ROOT).replace(os.sep, "/")}
        if shader_debug == 36:
            args["color_format"] = "rgba32f"
        if light_name is not None:
            args["shadow_debug_light"] = light_name
        started = time.time()
        reply = self.c.call("render_scene_image", args)
        image = read_pfm(path)
        self.render_seconds.append(time.time() - started)
        return reply, image

    def world(self, station, view, offset, camera_shift):
        key = (station, view["name"], tuple(offset), tuple(round(v, 9) for v in camera_shift))
        if key in self.world_cache:
            return self.world_cache[key]
        camera = shifted_camera(view, offset, camera_shift)
        _, image = self.render(camera, view, 36)
        path = os.path.join(self.world_dir, f"world_{'fz' if self.forward_z else 'rz'}_{len(self.world_cache):05d}.npy")
        np.save(path, image.astype(np.float32))
        self.world_cache[key] = path
        return path


def shifted_camera(view, offset, shift):
    camera = rooms.render_camera(view, offset)
    camera["eye"] = [camera["eye"][i] + shift[i] for i in range(3)]
    camera["target"] = [camera["target"][i] + shift[i] for i in range(3)]
    return camera


def contact_views():
    """Close-up views of the contact_blocks contact lines (G3 / G5 need
    pixels finer than a shadow texel): per caster, a camera outside the
    footprint corner opposite the default light, looking down at it."""
    station = rooms.STATIONS["contact_blocks"]
    light = station["lights"]["spot"]["position"]
    views = []
    for caster in station["contact_lines"]:
        b = next(x for x in station["boxes"] if x["name"] == caster)
        cx, _, cz = b["center"]
        hx, _, hz = b["half_extents"]
        sx = -1.0 if light[0] > cx else 1.0
        sz = -1.0 if light[2] > cz else 1.0
        corner = [cx + sx * hx, 0.0, cz + sz * hz]
        away = [sx / math.sqrt(2.0), 0.0, sz / math.sqrt(2.0)]
        eye = [corner[0] + 0.28 * away[0], 0.42, corner[2] + 0.28 * away[2]]
        target = [corner[0] + 0.06 * away[0], 0.0, corner[2] + 0.06 * away[2]]
        views.append(rooms.view(f"contact_{caster.lower()}", eye, target, 40.0, near=0.01,
                                boxes=station["boxes"]))
    return views


def station_views(name):
    views = list(rooms.STATIONS[name]["views"])
    if name == "contact_blocks":
        views += contact_views()
    return views


def pose_camera_shift(station, light_type, pose):
    """head_on_floor directional: the camera follows the lateral light offset."""
    if (station == "head_on_floor") and (light_type == "directional"):
        base = rooms.STATIONS[station]["lights"][light_type]["position"]
        return [pose["position"][0] - base[0], 0.0, pose["position"][2] - base[2]]
    return [0.0, 0.0, 0.0]


def find_shadow_light(reply, light_name, light_type):
    for entry in reply.get("shadow_lights", []):
        if entry.get("name") == light_name:
            if entry.get("type") != light_type:
                raise RuntimeError(f"{light_name}: render used type {entry.get('type')}, expected {light_type}")
            return entry
    raise RuntimeError(f"{light_name} is not shadow-mapped in the render: {reply.get('shadow_lights')}")


# --- aggregation + report --------------------------------------------------------------------

def cell_key(config, light, station):
    return f"{config}|{light}|{station}"


def new_cell(config, light, station, technique, resolution):
    return {"config": config, "light": light, "station": station, "technique": technique,
            "resolution": resolution, "status": "measured", "images": 0,
            "G1": None, "G2": None, "G3": None, "G4": {}, "G5": None, "G6": None,
            "failures": [], "worst_image": None}


def fold(cell, result, context, filter_radius, g3_limit):
    """Fold one analysis result into its cell (worst over images)."""
    cell["images"] += 1
    score = 0
    for gate in ("G1", "G2"):
        r = result[gate]
        if (cell[gate] is None) or (r["count"] > cell[gate]["count"]):
            cell[gate] = {"count": r["count"], "total": r["total"],
                          "fraction": (r["count"] / r["total"]) if r["total"] else 0.0}
        if r["count"] > 0:
            score += r["count"]
            cell["failures"].append(dict(context, gate=gate, count=r["count"], total=r["total"],
                                         examples=r["examples"]))
    if result.get("G3") is not None:
        worst = result["G3"]["worst_texels"]
        if worst is not None and ((cell["G3"] is None) or (worst > cell["G3"])):
            cell["G3"] = worst
        if worst is not None and worst > g3_limit:
            cell["failures"].append(dict(context, gate="G3", worst_texels=worst, examples=result["G3"]["examples"]))
    if result.get("G4") is not None:
        for hut, r in result["G4"].items():
            previous = cell["G4"].get(hut)
            if previous is None:
                cell["G4"][hut] = {k: r[k] for k in ("thickness", "count", "total", "gated")}
            else:
                previous["count"] = max(previous["count"], r["count"])
                previous["total"] = max(previous["total"], r["total"])
            if r["count"] > 0 and r["gated"]:
                score += r["count"]
                cell["failures"].append(dict(context, gate="G4", hut=hut, count=r["count"], examples=r["examples"]))
    g5 = result.get("G5")
    if g5 and ("worst_abs" in g5):
        c5 = cell["G5"] or {"mean_abs": 0.0, "worst_abs": 0.0, "crossings": 0, "spurious": 0}
        c5["mean_abs"] = max(c5["mean_abs"], abs(g5["mean_signed"]))
        c5["worst_abs"] = max(c5["worst_abs"], g5["worst_abs"])
        c5["crossings"] += g5["crossings"]
        c5["spurious"] += g5["spurious"]
        cell["G5"] = c5
        if (abs(g5["mean_signed"]) > 1.0) or (g5["worst_abs"] > filter_radius + 1.0):
            cell["failures"].append(dict(context, gate="G5", mean_signed=g5["mean_signed"],
                                         worst_abs=g5["worst_abs"], worst_world=g5.get("worst_world")))
    return score


def verdicts(cell):
    """-> {gate: text} for the table; FAIL / PASS with the worst value."""
    out = {}
    if cell["status"] != "measured":
        for gate in GATES:
            out[gate] = cell["status"]
        return out
    for gate in ("G1", "G2"):
        v = cell[gate]
        if v is None:
            out[gate] = "-"
        else:
            out[gate] = ("PASS" if v["count"] == 0 else f"FAIL {v['count']} ({100.0 * v['fraction']:.2g}%)")
    limit = G3_LIMIT[cell["technique"]]
    if cell["station"] == "contact_blocks":
        out["G3"] = "-" if cell["G3"] is None else (
            f"{'PASS' if cell['G3'] <= limit else 'FAIL'} {cell['G3']:.2f}t")
    else:
        out["G3"] = "-"
    if cell["station"] == "thin_walls" and cell["G4"]:
        failed = [f"{round(r['thickness'] * 100):d}cm:{r['count']}" for r in cell["G4"].values()
                  if r["gated"] and r["count"] > 0]
        report = [f"{round(r['thickness'] * 100):d}cm:{r['count']}" for r in cell["G4"].values() if r["count"] > 0]
        ungated = [f"{round(r['thickness'] * 100):d}cm:{r['count']}" for r in cell["G4"].values()
                   if (not r["gated"]) and r["count"] > 0]
        out["G4"] = (("FAIL " + ",".join(failed) + ((" (" + ",".join(ungated) + ")") if ungated else ""))
                     if failed else ("PASS" + ((" (" + ",".join(report) + ")") if report else "")))
    else:
        out["G4"] = "-"
    radius = POINT_FILTER_RADIUS if cell["light"] == "point" else cell.get("filter_radius", 0.5)
    if cell["G5"] is not None:
        ok = (cell["G5"]["mean_abs"] <= 1.0) and (cell["G5"]["worst_abs"] <= radius + 1.0)
        out["G5"] = f"{'PASS' if ok else 'FAIL'} {cell['G5']['mean_abs']:.2f}/{cell['G5']['worst_abs']:.2f}t"
    else:
        out["G5"] = "-"
    if cell["G6"] is not None:
        out["G6"] = "PASS" if cell["G6"]["count"] == 0 else f"FAIL {cell['G6']['count']}"
    else:
        out["G6"] = "-"
    out["G7"] = "n/a (phase 3)"
    return out


TABLE_WIDTHS = {"G1": 20, "G2": 20, "G3": 12, "G4": 40, "G5": 18, "G6": 10}


def print_table(cells, order):
    header = f"{'config':34s} {'light':11s} {'station':15s}" + "".join(
        f" {g:>{TABLE_WIDTHS[g]}s}" for g in GATES[:-1]) + "  G7"
    print(header)
    print("-" * len(header))
    failures = 0
    for key in order:
        cell = cells[key]
        v = verdicts(cell)
        failures += sum(1 for g in GATES if v[g].startswith("FAIL"))
        print(f"{cell['config'][:34]:34s} {cell['light']:11s} {cell['station']:15s}"
              + "".join(f" {v[g]:>{TABLE_WIDTHS[g]}s}" for g in GATES[:-1]) + f"  {v['G7']}")
    return failures


def save_worst_image(path, vis, classes):
    from PIL import Image
    gray = np.clip(vis, 0.0, 1.0)
    rgb = np.stack([gray, gray, gray], axis=2)
    rgb[classes == 0] = [0.0, 0.0, 0.0]
    rgb[classes == 2] = rgb[classes == 2] * 0.3 + np.array([0.0, 0.0, 0.35])
    rgb[classes == 4] = [1.0, 0.0, 0.0]
    rgb[classes == 5] = [0.0, 1.0, 1.0]
    Image.fromarray((rgb * 255.0 + 0.5).astype(np.uint8)).save(path)


# --- main loop ------------------------------------------------------------------------------------

def parse_overrides(items):
    out = {}
    for item in items:
        key, sep, value = item.partition("=")
        if not key or not sep:
            raise SystemExit(f"--set {item!r} is not KEY=VALUE")
        if value.lower() in ("true", "false"):
            out[key] = value.lower() == "true"
            continue
        for kind in (int, float):
            try:
                out[key] = kind(value)
                break
            except ValueError:
                continue
        else:
            out[key] = value
    return out


def select(names, pattern, what):
    if pattern in (None, "all"):
        return list(names)
    wanted = [p.strip() for p in pattern.split(",") if p.strip()]
    out = [n for n in names if any(fnmatch.fnmatchcase(n, p) for p in wanted)]
    if not out:
        raise SystemExit(f"--{what} {pattern!r} matches nothing; one of: {', '.join(names)}")
    return out


def measure_editor(session, pool, configs, args, cells, order, overrides, run_index, runs, image_dir, pending_images):
    """All stations x configs x lights x poses x views on one editor."""
    stations = select(list(rooms.STATIONS), args.station, "station")
    lights = select(LIGHT_TYPES, args.light, "light")
    offsets = [[float(v), 0.0, 0.0] for v in args.root_offset]
    pending = []
    for offset in offsets:
        for station in stations:
            station_info = rooms.STATIONS[station]
            session.load_station(station, offset)
            views = station_views(station)
            for config in configs:
                session.apply_config(config, overrides)
                fields = dict(config["fields"], **overrides)
                for light_type in lights:
                    name = config["name"] + (f"@{offset[0]:g}m" if offset[0] else "")
                    key = cell_key(name, light_type, station)
                    technique = fields["shadow_technique"]
                    resolution = fields["point_shadow_resolution"] if light_type == "point" else fields["shadow_resolution"]
                    if key not in cells:
                        cells[key] = new_cell(name, light_type, station, technique, resolution)
                        order.append(key)
                    cell = cells[key]
                    radius = POINT_FILTER_RADIUS if light_type == "point" else FILTER_RADIUS[fields["shadow_filter"]]
                    cell["filter_radius"] = radius
                    if (light_type == "point") and (technique == "distance"):
                        cell["status"] = "unsupported"
                        continue
                    started = time.time()
                    poses = rooms.pose_sweep(station, light_type, args.poses)
                    futures = []
                    for pose_index, pose in enumerate(poses):
                        rooms.apply_light_pose(session.c, session.c.scene, station_info["light"], pose, offset)
                        pose_world = rooms.pose_to_world(pose, offset)
                        shift = pose_camera_shift(station, light_type, pose)
                        for view in views:
                            world_path = session.world(station, view, offset, shift)
                            reply, image = session.render(shifted_camera(view, offset, shift), view, 30,
                                                          station_info["light"])
                            entry = find_shadow_light(reply, station_info["light"], light_type)
                            vis = image[:, :, 0].astype(np.float32)
                            job = {"kind": "image", "key": key, "station": station, "offset": offset,
                                   "cache_dir": session.cache_dir,
                                   "world_path": world_path, "vis": vis, "light": pose_world,
                                   "texture_from_world": entry.get("texture_from_world"),
                                   "resolution": entry["resolution"], "band_radius": radius + 1.0,
                                   "g3": station == "contact_blocks", "g3_limit": G3_LIMIT[technique],
                                   "g5": station in G5_STATIONS, "return_classes": args.save_images != "none"}
                            context = {"run": run_index, "pose": pose_index, "view": view["name"],
                                       "light_pose": {k: pose[k] for k in ("position", "direction", "outer_spot_angle_deg")
                                                      if k in pose}}
                            context["cell"] = key
                            if os.environ.get("SHADOW_VERIFY_KEEP"):
                                context["job"] = {k: v for k, v in job.items() if k != "vis"}
                            futures.append((pool.submit(run_job, job), context, vis if args.save_images != "none" else None))
                    if (station in G6_STATIONS) and (light_type == "directional") and not args.no_g6:
                        futures += g6_jobs(session, pool, station, views[0], offset, poses[0], radius, key,
                                           station_info["light"], run_index)
                    pending.append({"cell": cell, "key": key, "futures": futures, "radius": radius,
                                    "technique": technique, "started": started, "render_s": time.time() - started,
                                    "label": f"[run {run_index + 1}/{runs}] {station} | {name} | {light_type}: "
                                             f"{len(poses)} poses x {len(views)} views"})
                    drain(pending, args, image_dir, pending_images, block=False)
    drain(pending, args, image_dir, pending_images, block=True)


def drain(pending, args, image_dir, pending_images, block):
    """Fold the analysis results of every cell whose jobs are all done (all
    cells with block=True) and print its progress line. Rendering of the
    next cells continues while the workers analyze."""
    while pending:
        task = pending[0]
        if (not block) and not all(f.done() for f, _, _ in task["futures"]):
            return
        pending.pop(0)
        cell = task["cell"]
        key = task["key"]
        analysis = []
        for future, context, vis in task["futures"]:
            result = future.result()
            if "G6" in result:
                r = result["G6"]
                if (cell["G6"] is None) or (r["count"] > cell["G6"]["count"]):
                    cell["G6"] = {"count": r["count"], "total": r["total"]}
                if r["count"] > 0:
                    cell["failures"].append(dict(context, gate="G6", count=r["count"], examples=r["examples"]))
                continue
            analysis.append(result.get("analysis_s", 0.0))
            score = fold(cell, result, context, task["radius"], G3_LIMIT[task["technique"]])
            if args.save_images == "all":
                image_name = re.sub(r"[^A-Za-z0-9_.=-]+", "_", f"{key}_r{context['run']}_p{context['pose']}_{context['view']}")
                save_worst_image(os.path.join(image_dir, image_name + ".png"), vis, result["classes"])
                if os.environ.get("SHADOW_VERIFY_KEEP"):
                    np.save(os.path.join(image_dir, image_name + "_vis.npy"), vis)
                    with open(os.path.join(image_dir, image_name + "_job.json"), "w") as handle:
                        json.dump(context, handle, default=str)
            if (args.save_images == "failing") and (score > 0) and (
                    (cell["worst_image"] is None) or (score > cell["worst_image"][0])):
                cell["worst_image"] = (score, context)
                pending_images[key] = (vis, result["classes"], context)
        v = verdicts(cell)
        print(f"{task['label']}: render {task['render_s']:.1f} s, analysis {float(np.mean(analysis)) if analysis else 0.0:.1f} s/image, "
              f"done after {time.time() - task['started']:.1f} s; "
              + " ".join(f"{g}={v[g]}" for g in GATES[:-1] if v[g] != "-"), flush=True)


def g6_jobs(session, pool, station, view, offset, pose, radius, key, light_name, run_index):
    """Base render + eight renders with the camera moved by k/8 texel."""
    rooms.apply_light_pose(session.c, session.c.scene, rooms.STATIONS[station]["light"], pose, offset)
    pose_world = rooms.pose_to_world(pose, offset)
    base_world = session.world(station, view, offset, [0.0, 0.0, 0.0])
    reply, image = session.render(shifted_camera(view, offset, [0.0, 0.0, 0.0]), view, 30, light_name)
    entry = find_shadow_light(reply, light_name, "directional")
    M = np.array(entry["texture_from_world"])
    u_axis = M[0, :3]
    texel = 1.0 / (entry["resolution"][0] * np.linalg.norm(u_axis))
    u_dir = u_axis / np.linalg.norm(u_axis)
    base_vis = image[:, :, 0].astype(np.float32)
    futures = []
    for k in range(1, 9):
        shift = list(u_dir * (texel * k / 8.0))
        world_path = session.world(station, view, offset, shift)
        reply_k, image_k = session.render(shifted_camera(view, offset, shift), view, 30, light_name)
        entry_k = find_shadow_light(reply_k, light_name, "directional")
        job = {"kind": "g6", "key": key, "station": station, "offset": offset, "light": pose_world,
               "cache_dir": session.cache_dir,
               "resolution": entry["resolution"], "band_radius": radius + 1.0,
               "base_world_path": base_world, "base_vis": base_vis,
               "base_texture_from_world": entry["texture_from_world"],
               "moved_world_path": world_path, "moved_vis": image_k[:, :, 0].astype(np.float32),
               "moved_texture_from_world": entry_k["texture_from_world"]}
        futures.append((pool.submit(run_job, job), {"run": run_index, "g6_step": k, "view": view["name"]}, None))
    return futures


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--matrix", default="core", choices=["core", "full"])
    parser.add_argument("--poses", default=None, choices=["full", "short"],
                        help="pose sweep (default: short; the core matrix with full poses does not fit an hour)")
    parser.add_argument("--around", default="Medium", help="preset the core variations are made around, or 'all'")
    parser.add_argument("--station", default="all", help="station name(s), comma separated, or 'all'")
    parser.add_argument("--light", default="all", help="light type(s), comma separated, or 'all'")
    parser.add_argument("--config", default="all", help="config name(s) / fnmatch patterns, comma separated")
    parser.add_argument("--set", nargs="*", default=[], metavar="KEY=VALUE",
                        help="set_graphics_preset fields applied on top of every config")
    parser.add_argument("--root-offset", nargs="*", type=float, default=[0.0], metavar="X",
                        help="station root x offsets in metres (R7: 1000 10000)")
    parser.add_argument("--runs", type=int, default=1, help="full runs; every gate is the worst over the runs")
    parser.add_argument("--enforce", action="store_true", help="exit non-zero when a gate FAILs")
    parser.add_argument("--no-g6", action="store_true", help="skip the G6 camera translation renders")
    parser.add_argument("--save-images", default="none", choices=["none", "failing", "all"],
                        help="failing: the worst visibility image per failing cell; all: every analyzed image (debug)")
    parser.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 4) // 2),
                        help="analysis processes (default: half the CPUs; more slows the editor's renders)")
    parser.add_argument("--list-configs", action="store_true", help="print the matrix and exit (needs an editor)")
    parser.add_argument("--reuse", action="store_true", help="drive the editor already running on --port")
    parser.add_argument("--port", type=int, default=3771, help="MCP port (preferred port of a launched editor)")
    parser.add_argument("--editor", default=DEFAULT_EDITOR)
    args = parser.parse_args()
    if args.poses is None:
        args.poses = "short"
    overrides = parse_overrides(args.set)
    for key in overrides:
        if key not in PRESET_FIELDS + ["use_draw_lists"]:
            raise SystemExit(f"--set {key}: not a set_graphics_preset field")

    from common import Creation  # noqa: E402 - after sys.path

    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    run_dir = os.path.join(OUT_DIR, stamp)
    os.makedirs(run_dir, exist_ok=True)
    backup = None if args.reuse else Config_backup()
    cells = {}
    order = []
    skipped = []
    pending_images = {}
    wall_started = time.time()
    render_seconds = []
    configs_used = []
    server_info = []
    try:
        with concurrent.futures.ProcessPoolExecutor(max_workers=args.workers) as pool:
            phases = [False] if args.reuse else [False, True]
            all_configs = None
            for forward_z in phases:
                if (all_configs is not None) and not any(cfg["forward_z"] == forward_z for cfg in all_configs):
                    continue
                process = None
                c = None
                try:
                    port = args.port
                    if not args.reuse:
                        process, port = launch_editor(args.editor, args.port, forward_z)
                        print(f"launched editor pid {process.pid} on port {port}"
                              + (" (forward-Z)" if forward_z else ""), flush=True)
                    c = Creation("shadow_verify", port=port, pause_s=0.0, reuse=True, manage_windows=False)
                    info = c.call("get_server_info")
                    server_info.append(info)
                    if process is not None and info.get("pid") != process.pid:
                        raise RuntimeError(f"port {port} is served by pid {info.get('pid')}, not {process.pid}")
                    session = Session(c, run_dir, forward_z)
                    supported = session.supported_depth_bits()
                    if all_configs is None:
                        all_configs = build_matrix(args, supported, skipped)
                        names = [cfg["name"] for cfg in all_configs]
                        selected = set(select(names, args.config, "config"))
                        all_configs = [cfg for cfg in all_configs if cfg["name"] in selected]
                        if args.reuse:
                            for cfg in all_configs:
                                if cfg["forward_z"]:
                                    skipped.append(f"{cfg['name']}: forward-Z needs a launched editor (--reuse)")
                        print(f"device depth bits {supported}; {len(all_configs)} configs:", flush=True)
                        for cfg in all_configs:
                            print(f"  {cfg['name']}" + (f"  ({cfg['note']})" if cfg["note"] else ""))
                        for line in skipped:
                            print(f"  skipped: {line}")
                        if args.list_configs:
                            return 0
                    configs = [cfg for cfg in all_configs if cfg["forward_z"] == forward_z]
                    configs_used += [cfg for cfg in configs]
                    if not configs:
                        continue
                    session.environment()
                    for run_index in range(args.runs):
                        measure_editor(session, pool, configs, args, cells, order, overrides, run_index,
                                       args.runs, run_dir, pending_images)
                    render_seconds += session.render_seconds
                    c.close_all_scenes()
                finally:
                    if process is not None:
                        stop_editor(c, process)
    finally:
        if backup is not None:
            backup.restore()
    wall = time.time() - wall_started

    print()
    failures = print_table(cells, order)
    for key, (vis, classes, context) in pending_images.items():
        name = re.sub(r"[^A-Za-z0-9_.=-]+", "_", key)
        save_worst_image(os.path.join(run_dir, f"{name}.png"), vis, classes)
    if not os.environ.get("SHADOW_VERIFY_KEEP"):
        shutil.rmtree(os.path.join(run_dir, "tmp"), ignore_errors=True)
        shutil.rmtree(os.path.join(run_dir, "world"), ignore_errors=True)
        shutil.rmtree(os.path.join(run_dir, "cache"), ignore_errors=True)
    json_path = os.path.join(OUT_DIR, f"{stamp}.json")
    record = {
        "matrix": args.matrix, "poses": args.poses, "around": args.around, "runs": args.runs,
        "overrides": overrides, "root_offsets": args.root_offset, "skipped": skipped,
        "configs": configs_used, "cells": [dict(cells[k], verdicts=verdicts(cells[k])) for k in order],
        "wall_seconds": round(wall, 1), "renders": len(render_seconds),
        "render_seconds_mean": round(float(np.mean(render_seconds)), 3) if render_seconds else None,
        "server": server_info,
    }
    for cell in record["cells"]:
        cell.pop("worst_image", None)
    with open(json_path, "w", encoding="utf-8") as handle:
        json.dump(record, handle, indent=1, default=float)
    print(f"\n{len(render_seconds)} renders, mean {record['render_seconds_mean']} s; wall {wall / 60.0:.1f} min")
    for line in skipped:
        print(f"skipped: {line}")
    print(f"wrote {os.path.relpath(json_path, REPO_ROOT)}"
          + (f"; images under {os.path.relpath(run_dir, REPO_ROOT)}" if pending_images else ""))
    if failures:
        print(f"{failures} gate verdict(s) FAIL" + ("" if args.enforce else " (reported only; --enforce fails the exit code)"))
    else:
        if os.path.isdir(run_dir) and not os.listdir(run_dir):
            os.rmdir(run_dir)
    return 1 if (failures and args.enforce) else 0


if __name__ == "__main__":
    sys.exit(main())
