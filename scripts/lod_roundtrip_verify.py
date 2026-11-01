#!/usr/bin/env python3
"""Stored LOD chain round trip (doc/plans/lod.md step 6,
doc/gltf_extensions/ERHE_mesh_lod.md, doc/editor/lod.md "Verification").

Launches the headless editor (doc/agents/editor_runs.md), writes a GLB with a
torus and a capsule as plain triangle soups (no ERHE extensions), opens it,
saves it once with prepared mesh data (A) and loads A, so both meshes are
prepared primitives. Then:

1. generate_lods on the torus only and saves (B0): only the torus primitive
   carries ERHE_mesh_lod (chains are made on request only, D1), declared in
   extensionsUsed and not in extensionsRequired;
2. generate_lods on the capsule too, records both chains (get_lod_info) and
   render_scene_image hashes at forced levels 0..5, at four automatic
   selection distances and without Lod, and saves (B);
3. loads B: the chains are identical (levels, index and triangle counts,
   errors, settings; no level edge lines yet) and every image is identical;
   a selection queues the full builds, after which the level edge counts
   equal the ones before the save and the images are still identical;
4. saves the loaded scene again (the stored chains are written back) and
   saves it for Quest (Q), loads Q (stored vertex streams) and checks the
   chains, the images and the edge counts after a selection the same way;
5. requires that no LOD chain was generated after the two requests (the
   "mesh lod" log lines of erhe::primitive's generation).

Usage: python3 scripts/lod_roundtrip_verify.py [--editor PATH] [--out DIR]
Exit code 0 = pass.
"""

import argparse
import hashlib
import json
import math
import os
import re
import signal
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from erhe_mcp import McpClient, check_true, report  # noqa: E402

REPO_ROOT      = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG_PATH       = os.path.join(REPO_ROOT, "logs", "log.txt")
DEFAULT_EDITOR = os.path.join("build_xcode_metal_headless", "bin", "Debug", "editor")
MESHES         = ("torus", "capsule")
LEVEL_COUNT    = 6
TARGET         = [0.0, 1.0, -4.0]


def launch_editor(editor_exe):
    exe = editor_exe if os.path.isabs(editor_exe) else os.path.join(REPO_ROOT, editor_exe)
    if not os.path.isfile(exe):
        raise RuntimeError(f"no editor at {exe}")
    os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
    open(LOG_PATH, "w").close()
    env = dict(os.environ, ERHE_AI_DRIVER="1")
    flags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
    process = subprocess.Popen([exe], cwd=REPO_ROOT, env=env, creationflags=flags,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.monotonic() + 300.0
    pattern = re.compile(r"MCP server: listening on 127\.0\.0\.1:(\d+) \(pid (\d+)")
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"editor exited with {process.returncode} during startup; see logs/log.txt")
        try:
            with open(LOG_PATH, "r", encoding="utf-8", errors="replace") as handle:
                text = handle.read()
            matches = [m for m in pattern.finditer(text) if int(m.group(2)) == process.pid]
            if matches and ("Main loop: completed frame 12" in text):
                return process, int(matches[0].group(1))
        except OSError:
            pass
        time.sleep(1.0)
    process.kill()
    raise RuntimeError("editor did not become ready")


# --- The soup GLB -------------------------------------------------------------

def make_torus(major=1.0, minor=0.35, nu=64, nv=32):
    positions, normals, texcoords, indices = [], [], [], []
    for i in range(nu + 1):
        u = (i / nu) * 2.0 * math.pi
        for j in range(nv + 1):
            v = (j / nv) * 2.0 * math.pi
            cx, cz = math.cos(u), math.sin(u)
            positions.append(((major + minor * math.cos(v)) * cx, minor * math.sin(v), (major + minor * math.cos(v)) * cz))
            normals.append((math.cos(v) * cx, math.sin(v), math.cos(v) * cz))
            texcoords.append((i / nu, j / nv))
    for i in range(nu):
        for j in range(nv):
            a = i * (nv + 1) + j
            b = (i + 1) * (nv + 1) + j
            indices += [a, a + 1, b, b, a + 1, b + 1]
    return positions, normals, texcoords, indices


def make_capsule(radius=0.5, half=0.75, nu=48, nh=24):
    positions, normals, texcoords, indices = [], [], [], []
    for k in range(nh + 1):
        t = k / nh
        theta = t * math.pi
        y = math.cos(theta) * radius + (half if t < 0.5 else -half)
        r = math.sin(theta) * radius
        for i in range(nu + 1):
            u = (i / nu) * 2.0 * math.pi
            positions.append((r * math.cos(u), y, r * math.sin(u)))
            normals.append((math.sin(theta) * math.cos(u), math.cos(theta), math.sin(theta) * math.sin(u)))
            texcoords.append((i / nu, k / nh))
    for k in range(nh):
        for i in range(nu):
            a = k * (nu + 1) + i
            b = (k + 1) * (nu + 1) + i
            if k != 0:
                indices += [a, a + 1, b]
            if k != nh - 1:
                indices += [b, a + 1, b + 1]
    return positions, normals, texcoords, indices


def write_soup_glb(path):
    meshes = [("torus", make_torus(), [-1.5, 0.8, -4.0]), ("capsule", make_capsule(), [1.5, 1.4, -4.0])]
    binary = bytearray()
    views, accessors, gltf_meshes, nodes = [], [], [], []

    def add_view(data):
        while len(binary) % 4:
            binary.append(0)
        views.append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(data)})
        binary.extend(data)
        return len(views) - 1

    def add_accessor(data, component_type, count, kind, extra=None):
        accessor = {"bufferView": add_view(data), "componentType": component_type, "count": count, "type": kind}
        accessor.update(extra or {})
        accessors.append(accessor)
        return len(accessors) - 1

    for name, (positions, normals, texcoords, indices), translation in meshes:
        bounds = {"min": [min(v[c] for v in positions) for c in range(3)], "max": [max(v[c] for v in positions) for c in range(3)]}
        p = add_accessor(b"".join(struct.pack("<3f", *v) for v in positions), 5126, len(positions), "VEC3", bounds)
        n = add_accessor(b"".join(struct.pack("<3f", *v) for v in normals),   5126, len(normals),   "VEC3")
        t = add_accessor(b"".join(struct.pack("<2f", *v) for v in texcoords), 5126, len(texcoords), "VEC2")
        i = add_accessor(b"".join(struct.pack("<I", v) for v in indices),     5125, len(indices),   "SCALAR")
        gltf_meshes.append({"name": name, "primitives": [{"attributes": {"POSITION": p, "NORMAL": n, "TEXCOORD_0": t}, "indices": i, "mode": 4}]})
        nodes.append({"name": name, "mesh": len(gltf_meshes) - 1, "translation": translation})
    while len(binary) % 4:
        binary.append(0)
    document = {
        "asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": list(range(len(nodes)))}],
        "nodes": nodes, "meshes": gltf_meshes, "accessors": accessors, "bufferViews": views,
        "buffers": [{"byteLength": len(binary)}],
    }
    json_bytes = json.dumps(document).encode()
    while len(json_bytes) % 4:
        json_bytes += b" "
    with open(path, "wb") as handle:
        handle.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(json_bytes) + 8 + len(binary)))
        handle.write(struct.pack("<II", len(json_bytes), 0x4E4F534A))
        handle.write(json_bytes)
        handle.write(struct.pack("<II", len(binary), 0x004E4942))
        handle.write(binary)


def glb_lod_primitives(path):
    with open(path, "rb") as handle:
        data = handle.read()
    length, = struct.unpack_from("<I", data, 12)
    document = json.loads(data[20:20 + length])
    used = {mesh.get("name", "?"): [("ERHE_mesh_lod" in p.get("extensions", {})) for p in mesh["primitives"]] for mesh in document["meshes"]}
    return used, ("ERHE_mesh_lod" in document.get("extensionsUsed", [])), ("ERHE_mesh_lod" in document.get("extensionsRequired", []))


# --- The checks ---------------------------------------------------------------

class Roundtrip:
    def __init__(self, client, out_dir):
        self.client  = client
        self.out_dir = out_dir

    def call(self, tool, arguments=None):
        return self.client.call(tool, arguments or {})

    def wait_idle(self, timeout=300.0):
        deadline = time.monotonic() + timeout
        idle_count = 0
        while time.monotonic() < deadline:
            status = self.call("get_async_status")
            if status.get("idle", False) and (status.get("pending_scene_commits", 0) == 0):
                idle_count += 1
                if idle_count >= 3:
                    return
            else:
                idle_count = 0
            time.sleep(0.3)
        raise RuntimeError("editor not idle")

    def wait_scene(self, name, timeout=120.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if any(scene["name"] == name for scene in self.call("list_scenes")["scenes"]):
                return
            time.sleep(0.3)
        raise RuntimeError(f"no scene {name}")

    def load(self, path):
        reply = self.call("load_scene", {"path": path})
        scene = reply.get("scene_name") or os.path.splitext(os.path.basename(path))[0]
        self.wait_scene(scene)
        self.wait_idle()
        return scene

    def close(self, scene):
        self.call("close_scene", {"scene_name": scene})
        self.wait_idle()

    def save(self, tool, scene, path):
        job = self.client.save(tool, {"scene_name": scene, "path": path})
        check_true(f"{tool} {os.path.basename(path)}", job.get("state") == "done", json.dumps(job)[:300])
        return job

    def node_id(self, scene, name):
        for node in self.call("get_scene_nodes", {"scene_name": scene})["nodes"]:
            if node["name"] == name:
                return node["id"]
        raise RuntimeError(f"no node {name}")

    def chain(self, scene, name):
        primitive = self.call("get_lod_info", {"scene_name": scene, "node_name": name})["primitives"][0]
        return {
            "lod_settings": primitive.get("lod_settings"),
            "levels": [(l["level"], l["index_count"], l["triangle_count"], l["edge_count"], l["error"]) for l in primitive.get("levels", [])]
        }

    def set_lod(self, scene, name, mode, level=0):
        item = self.node_id(scene, name)
        if mode is None:
            self.call("set_item_property", {"item_id": item, "property": "Lod.apply_lod", "value": "false"})
            return
        self.call("set_item_property", {"item_id": item, "property": "Lod.apply_lod", "value": "true"})
        self.call("set_item_property", {"item_id": item, "property": "Lod.mode", "value": mode})
        if mode == "Forced":
            self.call("set_item_property", {"item_id": item, "property": "Lod.forced_level", "value": str(level)})

    def image(self, scene, tag, eye):
        path = os.path.join(self.out_dir, f"{tag}.png")
        camera = {"eye": eye, "target": TARGET, "fov_y_degrees": 50.0, "near": 0.03, "far": 2000.0}
        self.call("render_scene_image", {"scene": scene, "camera": camera, "width": 640, "height": 360, "output": "png", "path": path})
        with open(path, "rb") as handle:
            return hashlib.md5(handle.read()).hexdigest()

    def images(self, scene, tag):
        hashes = {}
        near = [0.0, 1.5, 1.0]
        for level in range(LEVEL_COUNT):
            for name in MESHES:
                self.set_lod(scene, name, "Forced", level)
            hashes[f"forced level {level}"] = self.image(scene, f"{tag}_forced{level}", near)
        for name in MESHES:
            self.set_lod(scene, name, "Automatic")
        for index, distance in enumerate((3.0, 10.0, 30.0, 90.0)):
            hashes[f"automatic at {distance} m"] = self.image(scene, f"{tag}_auto{index}", [0.0, 1.5, TARGET[2] + distance])
        for name in MESHES:
            self.set_lod(scene, name, None)
        hashes["without Lod"] = self.image(scene, f"{tag}_none", near)
        return hashes

    def check_reload(self, scene, tag, chains_before, images_before):
        for name in MESHES:
            chain = self.chain(scene, name)
            without_edges = [(l[0], l[1], l[2], l[4]) for l in chain["levels"]]
            expected      = [(l[0], l[1], l[2], l[4]) for l in chains_before[name]["levels"]]
            check_true(f"{tag}: {name} chain identical", (without_edges == expected) and (chain["lod_settings"] == chains_before[name]["lod_settings"]), f"{chain}")
            check_true(f"{tag}: {name} has no level edge lines before its full build", all(l[3] == 0 for l in chain["levels"]))
        images = self.images(scene, tag)
        for key, value in images_before.items():
            check_true(f"{tag}: image {key} identical", images[key] == value, f"{value} {images[key]}")
        # The selection queues the full builds: level edge lines from the stored indices.
        self.call("select_items", {"scene_name": scene, "paths": list(MESHES)})
        self.wait_idle()
        for name in MESHES:
            chain = self.chain(scene, name)
            check_true(f"{tag}: {name} chain with edge counts identical after the full build", chain["levels"] == chains_before[name]["levels"], f"{chain}")
        images = self.images(scene, f"{tag}_full_build")
        for key, value in images_before.items():
            if key != "without Lod":
                check_true(f"{tag}: image {key} identical after the full build", images[key] == value, f"{value} {images[key]}")
        self.call("select_items", {"scene_name": scene, "ids": []})
        self.wait_idle()

    def run(self):
        out = self.out_dir
        soup_path = os.path.join(out, "lod_roundtrip_soup.glb")
        write_soup_glb(soup_path)
        self.call("set_graphics_preset", {"lod_enabled": True, "lod_threshold_pixels": 4.0, "lod_bias": 0, "lod_hysteresis_band": 0.0})

        # Foreign soups -> prepared save A -> load A (prepared primitives).
        self.call("open_scene", {"path": soup_path})
        soup_scene = "lod_roundtrip_soup.glb"
        self.wait_scene(soup_scene)
        self.wait_idle()
        path_a = os.path.join(out, "lod_roundtrip_a.glb")
        self.save("save_scene", soup_scene, path_a)
        used, declared, _ = glb_lod_primitives(path_a)
        check_true("A: no ERHE_mesh_lod without a request", (not declared) and not any(any(v) for v in used.values()), f"{used}")
        self.close(soup_scene)
        scene_a = self.load(path_a)

        # D1: only the requested primitive carries a chain.
        self.call("generate_lods", {"scene_name": scene_a, "node_name": "torus", "settings": {"level_count": LEVEL_COUNT}})
        self.wait_idle()
        path_b0 = os.path.join(out, "lod_roundtrip_b0.glb")
        self.save("save_scene", scene_a, path_b0)
        used, declared, required = glb_lod_primitives(path_b0)
        check_true("B0: the torus carries ERHE_mesh_lod, the capsule does not", (used.get("torus") == [True]) and (used.get("capsule") == [False]), f"{used}")
        check_true("B0: ERHE_mesh_lod in extensionsUsed, not in extensionsRequired", declared and not required)

        self.call("generate_lods", {"scene_name": scene_a, "node_name": "capsule", "settings": {"level_count": LEVEL_COUNT}})
        self.wait_idle()
        chains_before = {name: self.chain(scene_a, name) for name in MESHES}
        for name in MESHES:
            check_true(f"{name} has a chain before the save", len(chains_before[name]["levels"]) > 1, f"{chains_before[name]}")
        images_before = self.images(scene_a, "before")
        check_true("negative control: forced levels 0 and 3 differ", images_before["forced level 0"] != images_before["forced level 3"])
        path_b = os.path.join(out, "lod_roundtrip_b.glb")
        job = self.save("save_scene", scene_a, path_b)
        check_true("B: 2 chains written", job.get("prepared", {}).get("lod_generated_count", 0) + job.get("prepared", {}).get("lod_stored_count", 0) == 2, json.dumps(job.get("prepared", {})))
        self.close(scene_a)

        scene_b = self.load(path_b)
        self.check_reload(scene_b, "B", chains_before, images_before)
        job = self.save("save_scene", scene_b, os.path.join(out, "lod_roundtrip_b2.glb"))
        check_true("B2: the stored chains are written back", job.get("prepared", {}).get("lod_stored_count") == 2, json.dumps(job.get("prepared", {})))
        path_q = os.path.join(out, "lod_roundtrip_q.glb")
        job = self.save("save_scene_for_quest", scene_b, path_q)
        check_true("Q: the stored chains are written back", job.get("prepared", {}).get("lod_stored_count") == 2, json.dumps(job.get("prepared", {})))
        self.close(scene_b)

        scene_q = self.load(path_q)
        self.check_reload(scene_q, "Q", chains_before, images_before)
        self.close(scene_q)

        with open(LOG_PATH, "r", encoding="utf-8", errors="replace") as handle:
            generations = sum(1 for line in handle if "mesh lod " in line)
        check_true("only the two requests generated chains (no generation at load, save or full build)", generations == 2, f"{generations} 'mesh lod' lines")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", default=DEFAULT_EDITOR)
    parser.add_argument("--out", default=os.path.join(REPO_ROOT, "logs", "lod_roundtrip"))
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)

    process, port = launch_editor(args.editor)
    try:
        Roundtrip(McpClient(port), args.out).run()
    finally:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(60)
        except subprocess.TimeoutExpired:
            process.kill()
    return report()


if __name__ == "__main__":
    sys.exit(main())
