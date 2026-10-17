#!/usr/bin/env python3
"""Runs one editor binary headless on the heavy bench scene and records the shadow fit results.

usage: shadow_fit_ab.py <editor binary> <output jsonl> [scene commands file] [scene name]
Compare two builds of the fit with cmp on the two output files; the build directory for run
directories is build_ninja_linux_vulkan_headless. Everything is deterministic (physics off, scripted camera / light / mesh moves), so two builds of the
fit must produce byte-identical output files.
"""
import argparse
import json
import math
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "scripts"))
import render_thread_bench as rtb  # noqa: E402

binary = Path(sys.argv[1])
out_path = Path(sys.argv[2])
commands = sys.argv[3] if len(sys.argv) > 3 else "config/editor/commands_bench_heavy.json"
SCENE = sys.argv[4] if len(sys.argv) > 4 else "Bench Heavy"

root = REPO / "build_ninja_linux_vulkan_headless" / "shadow_fit_capture"
run_dir = rtb.make_run_dir(
    argparse.Namespace(overrides=[], viewport_size=""), root, "ab_" + binary.name,
    ["editor_settings.json:shadow_frustum_fit.collect_debug=true", "erhe_graphics.json:force_disable_vsync=true"],
)
env = dict(os.environ)
env.update({"ERHE_AI_DRIVER": "1", "VK_DRIVER_FILES": "/usr/share/vulkan/icd.d/intel_icd.json", "VK_LOADER_LAYERS_DISABLE": "~implicit~"})
proc = subprocess.Popen([str(binary), "--commands", commands], cwd=run_dir, env=env,
                        stdout=(run_dir / "editor_stdout.log").open("wb"), stderr=subprocess.STDOUT, start_new_session=True)
try:
    log = run_dir / "logs" / "log.txt"
    for _ in range(600):
        if proc.poll() is not None:
            raise SystemExit("editor exited early")
        if log.is_file() and "Main loop: completed frame 12" in log.read_text(errors="replace"):
            break
        time.sleep(0.5)
    else:
        raise SystemExit("editor not ready")

    def mcp(tool, args):
        r = subprocess.run(["python3", str(REPO / "scripts" / "mcp_call.py"), tool, json.dumps(args)], capture_output=True, text=True, cwd=REPO)
        if r.returncode != 0:
            raise SystemExit(f"{tool} failed: {r.stdout[:300]} {r.stderr[:300]}")
        return json.loads(r.stdout)

    def quat_look_at(pos, target):
        f = [target[i] - pos[i] for i in range(3)]
        n = math.sqrt(sum(c * c for c in f)); f = [c / n for c in f]
        r = [f[1] * 0 - f[2] * 1, f[2] * 0 - f[0] * 0, f[0] * 1 - f[1] * 0]
        rn = math.sqrt(sum(c * c for c in r)); r = [c / rn for c in r]
        u = [r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]]
        m = [[r[0], u[0], -f[0]], [r[1], u[1], -f[1]], [r[2], u[2], -f[2]]]
        tr = m[0][0] + m[1][1] + m[2][2]
        if tr > 0:
            s = math.sqrt(tr + 1.0) * 2; return [(m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25 * s]
        if m[0][0] > m[1][1] and m[0][0] > m[2][2]:
            s = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2; return [0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s]
        if m[1][1] > m[2][2]:
            s = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2; return [(m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s]
        s = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2; return [(m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s]

    def set_camera(pos, rot):
        mcp("set_node_transform", {"scene_name": SCENE, "node_name": "Camera A", "translation": pos, "rotation_xyzw": rot})

    last = [None]

    def record(handle, label, index, dedupe=True):
        for _ in range(20):
            dump = mcp("get_shadow_fit_debug", {})
            node = dump["shadow_nodes"][0]
            lights = [l for l in node["lights"] if l["valid"] and l["receiver_box_count"] > 0]
            # Everything a scripted move changes: the camera (frustum corners), the
            # sun (receiver cull planes) and the dragged mesh (some caster box).
            key = json.dumps([
                lights[0]["view_frustum_corners"],
                lights[0]["receiver_filter_planes"],
                [c["min"] for c in lights[0]["caster_boxes"]],
            ]) if lights else ""
            if not dedupe or key != last[0]:
                break
        last[0] = key
        rec = {"scenario": label, "frame": index, "lights": []}
        for l in lights:
            rec["lights"].append({
                "name": l["name"],
                "step_boxes": l["step_boxes"],
                "obb": [l["obb_edge_from_original"], l["obb_aabb_min"], l["obb_aabb_max"]],
                "hull": l["light_plane_hull"],
                "fit_point_count": l["fit_point_count"],
                "receiver_filter_planes": l["receiver_filter_planes"],
                "verdicts": "".join("0" if (c["culled_by_shadow_volume"] or c["culled_by_receiver_volume"]) else "1" for c in l["caster_boxes"]),
            })
        handle.write(json.dumps(rec, sort_keys=True) + "\n")

    mcp("toggle_physics", {"enabled": False})
    rot0 = [-0.215206116437912, 0.0, 0.0, 0.9765686392784119]
    with out_path.open("w") as handle:
        cams = mcp("get_scene_cameras", {"scene_name": SCENE})
        for i in range(6):
            record(handle, "static", i, dedupe=False)
        center = [17.0, 0.0, 0.0]
        for i in range(36):
            a = 2.0 * math.pi * i / 36
            pos = [center[0] + 45.0 * math.cos(a), 18.0, center[2] + 45.0 * math.sin(a)]
            set_camera(pos, quat_look_at(pos, center))
            record(handle, "orbit", i)
        for i in range(40):
            set_camera([-20.0 + 80.0 * i / 39, 16.0, 34.0], rot0)
            record(handle, "pan", i)
        set_camera([10.0, 16.0, 34.0], rot0)
        nodes = mcp("get_scene_nodes", {"scene_name": SCENE})["nodes"]
        meshes = [n for n in nodes if n["type"] == "Mesh"]
        target = min(meshes, key=lambda n: (n["position"][0] - 17.0) ** 2 + n["position"][2] ** 2)
        base = target["position"]
        for i in range(20):
            mcp("set_node_transform", {"scene_name": SCENE, "node_name": target["name"], "translation": [base[0] + 0.05 * i, base[1], base[2] + 0.05 * i]})
            record(handle, "dragmid", i)
        mcp("set_node_transform", {"scene_name": SCENE, "node_name": target["name"], "translation": base})
        base_q = [0.0309157557785511, 0.8990049958229065, 0.4320841431617737, -0.06432408839464188]
        for i in range(30):
            a = 0.05 * i
            cy, sy = math.cos(a / 2), math.sin(a / 2)
            q = [cy * base_q[0] + sy * base_q[2], cy * base_q[1] + sy * base_q[3], cy * base_q[2] - sy * base_q[0], cy * base_q[3] - sy * base_q[1]]
            n = math.sqrt(sum(c * c for c in q)); q = [c / n for c in q]
            mcp("set_node_transform", {"scene_name": SCENE, "node_name": "Directional light 0", "rotation_xyzw": q})
            record(handle, "sun", i)
    print("recorded", out_path)
finally:
    if proc.poll() is None:
        os.kill(proc.pid, signal.SIGKILL)
        proc.wait()
