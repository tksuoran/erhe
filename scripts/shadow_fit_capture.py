#!/usr/bin/env python3
"""Drives a running headless editor over MCP and records shadow fit inputs.

Prerequisites: the editor runs the heavy bench scene (config/editor/commands_bench_heavy.json,
scene "Bench Heavy") with shadow_frustum_fit.collect_debug=true. Usage:
    python3 scripts/shadow_fit_capture.py <output dir> static orbit pan drag dragmid sun
The output files feed Shadow_fit_replay.stages in the erhe_math tests
(ERHE_FIT_CAPTURE_DIR=<output dir>); see doc/erhe/shadow_tight_fit.md.


One compact text file per scenario. Floats are written as hex floats
(float.hex) so the C++ replay parses them exactly. Per frame record:

  frame <index>
  shadow_range <f>
  corners <24 floats>
  planes <24 floats>
  receivers <N>   then N lines: min3 max3 in_frustum
  casters <M>     then M lines: min3 max3
  lights <L>      then per light: light <dir3> <flags string of M chars: 1 = survives both culls>
"""
import json
import math
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
OUT = Path(sys.argv[1])
SCENE = "Bench Heavy"
OUT.mkdir(parents=True, exist_ok=True)


def mcp(tool, args):
    result = subprocess.run(
        ["python3", str(REPO / "scripts" / "mcp_call.py"), tool, json.dumps(args)],
        capture_output=True, text=True, cwd=REPO,
    )
    if result.returncode != 0:
        raise SystemExit(f"{tool} failed: {result.stdout[:300]} {result.stderr[:300]}")
    return json.loads(result.stdout)


def hx(values):
    return " ".join(float(v).hex() for v in values)


def quat_look_at(pos, target):
    f = [target[i] - pos[i] for i in range(3)]
    n = math.sqrt(sum(c * c for c in f))
    f = [c / n for c in f]
    up = [0.0, 1.0, 0.0]
    r = [f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]]
    rn = math.sqrt(sum(c * c for c in r))
    r = [c / rn for c in r]
    u = [r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]]
    # columns: right, up, -forward
    m = [[r[0], u[0], -f[0]], [r[1], u[1], -f[1]], [r[2], u[2], -f[2]]]
    tr = m[0][0] + m[1][1] + m[2][2]
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        w = 0.25 * s
        x = (m[2][1] - m[1][2]) / s
        y = (m[0][2] - m[2][0]) / s
        z = (m[1][0] - m[0][1]) / s
    elif m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2
        w = (m[2][1] - m[1][2]) / s
        x = 0.25 * s
        y = (m[0][1] + m[1][0]) / s
        z = (m[0][2] + m[2][0]) / s
    elif m[1][1] > m[2][2]:
        s = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2
        w = (m[0][2] - m[2][0]) / s
        x = (m[0][1] + m[1][0]) / s
        y = 0.25 * s
        z = (m[1][2] + m[2][1]) / s
    else:
        s = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2
        w = (m[1][0] - m[0][1]) / s
        x = (m[0][2] + m[2][0]) / s
        y = (m[1][2] + m[2][1]) / s
        z = 0.25 * s
    return [x, y, z, w]


last_corners = None


def record(handle, index, dedupe=True):
    """Appends the current fit inputs; returns False when the dump repeats the previous frame."""
    global last_corners
    for _attempt in range(20):
        dump = mcp("get_shadow_fit_debug", {})
        node = dump["shadow_nodes"][0]
        lights = [l for l in node["lights"] if l["valid"] and l["receiver_box_count"] > 0]
        corners = [c for p in lights[0]["view_frustum_corners"] for c in p] if lights else []
        key = (tuple(corners), tuple(lights[0]["caster_boxes"][0]["min"]) if lights else ())
        if key != last_corners or index == 0 or not dedupe:
            break
    last_corners = key
    first = lights[0]
    handle.write(f"frame {index}\n")
    handle.write(f"shadow_range {float(node['shadow_range']).hex()}\n")
    handle.write("corners " + hx(corners) + "\n")
    handle.write("planes " + hx([c for p in first["view_frustum_planes"] for c in p]) + "\n")
    handle.write(f"receivers {len(first['receiver_boxes'])}\n")
    for b in first["receiver_boxes"]:
        handle.write(hx(b["min"]) + " " + hx(b["max"]) + f" {1 if b['in_frustum'] else 0}\n")
    handle.write(f"casters {len(first['caster_boxes'])}\n")
    for b in first["caster_boxes"]:
        handle.write(hx(b["min"]) + " " + hx(b["max"]) + "\n")
    handle.write(f"lights {len(lights)}\n")
    for l in lights:
        d = l["receiver_filter_planes"][0][:3]
        flags = "".join("0" if (c["culled_by_shadow_volume"] or c["culled_by_receiver_volume"]) else "1" for c in l["caster_boxes"])
        handle.write("light " + hx(d) + " " + flags + "\n")
    return True


def set_camera(pos, rot):
    mcp("set_node_transform", {"scene_name": SCENE, "node_name": "Camera A", "translation": pos, "rotation_xyzw": rot})


def scenario_static(handle):
    for i in range(15):
        record(handle, i, dedupe=False)


def scenario_orbit(handle):
    center = [17.0, 0.0, 0.0]
    steps = 72
    for i in range(steps):
        a = 2.0 * math.pi * i / steps
        pos = [center[0] + 45.0 * math.cos(a), 18.0, center[2] + 45.0 * math.sin(a)]
        set_camera(pos, quat_look_at(pos, center))
        record(handle, i)


def scenario_pan(handle):
    rot = [-0.215206116437912, 0.0, 0.0, 0.9765686392784119]
    steps = 80
    for i in range(steps):
        x = -20.0 + 80.0 * i / (steps - 1)
        set_camera([x, 16.0, 34.0], rot)
        record(handle, i)


def scenario_drag(handle):
    rot = [-0.215206116437912, 0.0, 0.0, 0.9765686392784119]
    set_camera([10.0, 16.0, 34.0], rot)
    for i in range(40):
        x = 58.5 - 1.5 * i
        mcp("set_node_transform", {"scene_name": SCENE, "node_name": "torus.40_1", "translation": [x, 0.625, 3.0 * math.sin(i * 0.3)]})
        record(handle, i)


def scenario_sun(handle):
    rot = [-0.215206116437912, 0.0, 0.0, 0.9765686392784119]
    set_camera([10.0, 16.0, 34.0], rot)
    for i in range(60):
        a = 0.03 * i
        # yaw around world Y applied to the light's base rotation (light 0)
        base = [0.0309157557785511, 0.8990049958229065, 0.4320841431617737, -0.06432408839464188]
        cy, sy = math.cos(a / 2), math.sin(a / 2)
        q = [cy * base[0] + sy * base[2], cy * base[1] + sy * base[3], cy * base[2] - sy * base[0], cy * base[3] - sy * base[1]]
        n = math.sqrt(sum(c * c for c in q))
        q = [c / n for c in q]
        mcp("set_node_transform", {"scene_name": SCENE, "node_name": "Directional light 0", "rotation_xyzw": q})
        record(handle, i)


def scenario_dragmid(handle):
    nodes = mcp("get_scene_nodes", {"scene_name": SCENE})["nodes"]
    meshes = [n for n in nodes if n["type"] == "Mesh"]
    target = min(meshes, key=lambda n: (n["position"][0] - 17.0) ** 2 + (n["position"][2]) ** 2)
    base = target["position"]
    print("dragging", target["name"], base, flush=True)
    rot = [-0.215206116437912, 0.0, 0.0, 0.9765686392784119]
    set_camera([10.0, 16.0, 34.0], rot)
    for i in range(40):
        mcp("set_node_transform", {"scene_name": SCENE, "node_name": target["name"], "translation": [base[0] + 0.05 * i, base[1], base[2] + 0.05 * i]})
        record(handle, i)


SCENARIOS = {"dragmid": scenario_dragmid, "static": scenario_static, "orbit": scenario_orbit, "pan": scenario_pan, "drag": scenario_drag, "sun": scenario_sun}

mcp("toggle_physics", {"enabled": False})
for name in sys.argv[2:]:
    last_corners = None
    with (OUT / f"heavy_{name}.txt").open("w") as handle:
        SCENARIOS[name](handle)
    print("done", name, flush=True)
