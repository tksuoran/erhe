#!/usr/bin/env python3
"""Editor and prepared assets end to end (doc/editor/prepared_assets.md
"Verification", plan step M5 of doc/plans/prepared_assets_design.md).

Launches its own headless editors (doc/agents/editor_runs.md) and stops them
by the pid it launched. Everything it writes lives in a temporary work
directory that it removes at the end (--keep keeps it). It writes two
hand-written glTF scenes:

- "small": soup meshes, one primitive shared by three nodes (one of them
  mirrored), untextured and textured (PNG) materials, a skinned mesh, an
  animation and a directional light;
- "many": --mesh-count distinct soup meshes, so the Ensure and Copy stages of
  an Export last long enough to be interrupted.

Each is opened as a foreign glTF and saved by the editor, and the
erhe-saved file is the source asset. Every open of a source stores the
Local-space mesh and Raytrace BVH items it derives in the file's editor
asset and loads them on the next open (doc/editor/prepared_assets.md "The
editor's own loading"), so an Export finds them present; Save Scene writes
the source soups and no derived data.

Checks (one PASS / FAIL line each):

0. The editor's own loading: Save Scene writes no derived-data extension;
   the first open of the saved "small" derives and stores its mesh and BVH
   items, a second open (new process) loads them, builds no BVH of them and
   logs no mesh optimizer run of its meshes; the run creates no cache/mesh_optimizer
   and no cache/bvh directory (directories an earlier build left are not
   touched), no .gltf / .glb file a Save or an Export of the run writes
   carries a derived-data extension, and no glTF file parse reads prepared
   mesh data (R12.2, R12.3).
1. Export of "small": counts, the prepared asset holds exactly the items its
   Scene item names (plus the Scene item), records (the stamp records the
   stamps of the items the Scene item names).
2. A new editor process: a second Export makes and copies nothing; an MCP
   edit of one mesh's vertex positions then Exports one mesh item and one
   Scene item; after undo an Export makes none. get_prepared_status after
   the second Export (nothing missing, matches), after the edit (one mesh
   item missing, no match) and after undo (two unreferenced items);
   drop_prepared_items of the unreferenced items (none left, the editor
   asset shrinks) and of the prepared asset (Export copies everything again).
3. With the source in place, opening the prepared asset opens the source.
4. With the source moved away, the prepared asset opens as a prepared run
   and render_scene_image matches the source scene's images exactly
   (decoded pixels, bind pose and a sampled animation pose).
5. The editor's default scene (brush-made, geometry-normative meshes), saved
   and reloaded: its Export writes every mesh as a mesh item (derived
   triangles), no primitive inside the Scene item and no ERHE_geometry,
   brushes or geometry graphs; a second Export makes nothing; its prepared
   run renders the source scene's images (two cameras).
6. Target quest: an Export of "small" for quest writes
   <stem>.quest.erhe_prepared (target quest, Scene item stamp
   glb;textures=astc_4x4) beside the pc one, which it leaves unchanged; the
   mesh items are the pc asset's (one GPU vertex layout serves both targets).
   Opened on the host with the source away it runs, with a notice naming the
   other target, and renders the source images. Copies whose stamp records
   are tweaked with SQLite check the format stamps (doc/editor/prepared_assets.md
   "Format stamps"): a recorded mesh layout stamp and a recorded Scene item
   stamp this build does not read refuse the load naming both stamps and the
   target; a recorded BVH stamp of another library opens with a notice, the
   stored BVHs unused, and a raycast through the torus hole answers as in the
   source scene; item stamps that differ from the records are not read; an
   asset without one of the stamp records is refused naming the record.
7. Export settings: the imported scene has none, and Export, the status and
   Bake World-Space Meshes refuse it, as prepare_scene refuses the
   per-call export_settings and texture_supercompression arguments and the
   container argument of the removed prepared glTF file (R12.1); adding
   them (set_scene_settings) copies the editor's defaults, and the saved
   source carries them. An Export of a scene without a source file is
   refused, and of a USD-backed scene in a build with a USD library. Every
   mesh item has a Raytrace BVH item of its own. raytrace_bvh off, set
   through set_scene_settings, makes no mesh item (they are shared), leaves
   the BVH items out of the prepared asset (unreferenced in the editor
   asset), and a second Export makes nothing; its prepared run renders the
   source image and builds the BVHs in memory (raycast); raytrace_bvh on
   again copies the BVH items back with the first Scene key.
8. Textures (skipped without the Bistro download's Basis Universal images):
   the import derives the texture properties from the uses, a save writes
   them (an MCP set_item_property edit included) and a reload reads them;
   the pc and quest Exports make Texture items of their block formats that
   the Scene item's images name; a transfer function edit makes a new
   Texture item and setting it back finds the first; a second Export makes
   nothing; the pc prepared run transcodes nothing, retains no source image
   bytes and renders the source images exactly; the quest asset runs on the
   host; a recorded Texture item stamp this build does not read refuses the load.
8b. texture_encoding (D8 step 6c, R7.5; --only-encoding runs it alone): a
   PNG-textured scene exported with target makes Basis texture items in the
   editor asset only and block format Texture items the Scene item names; its
   prepared runs (pc, quest on the host) render within a stated bound of the
   source scene and retain no source image bytes; source makes Texture items
   of the exact PNG bytes under other keys (the Basis items stay), its
   prepared run renders pixel for pixel; switching back makes nothing; a
   second Export in a new process makes and copies nothing. The other
   checks export with texture_encoding source (NO_BAKE).
9. Identities (doc/plans/prepared_assets.md R6.1, R6.2): "small" opened as a
   foreign glTF in two processes has the same node uids and bit-identical
   world transforms (get_scene_nodes world_from_node_hex), and its Export in
   the second process has the first Scene key and makes nothing; saved with
   the import root at the identity (no node written for it) and moved (the
   root written as a node), each reopened in a new process has the same uids
   and world transforms.
10. World-space meshes (doc/editor/world_space_bake.md "Verification"; the
   checks before export with bake_static_to_world_space off): the bake's
   hand-written scene, in node order and with children listed first; Bake
   World-Space Meshes makes the World-space mesh items and the world-space
   form, Export writes the Scene item's World Space node against them, a
   node move invalidates and its undo restores, instancing skip, the
   prepared run renders as the source. --only-world runs these alone.
10b. Several files (doc/editor/prepared_assets.md "Verification of several
   files"; --only-multi runs these alone): a host scene with two instances of a
   prefab that instantiates a prefab, and a material of a container file;
   per-file editor assets with the R10.2 split, Ensure deepest prefab first,
   the flat prepared asset and its prepared run with every source away, the
   R10.5 settings log, reload as the change site, the bake with instancing
   duplicate and skip, the R10.8 failures, the R2.2 record and the R2.5
   fallback.
11. "many": an Export killed (SIGKILL) during Ensure (mesh items) and another
   killed during Copy, then run again, leave the editor asset and the
   prepared asset with the same keys, payload bytes and meta as an
   uninterrupted Export of the same source in another directory. Copy writes
   a temporary file renamed over the target: the Copy killed before any
   prepared asset exists leaves none and one temporary file, which the next
   Export removes (logged); a Copy killed over a prepared asset leaves it
   byte-identical. No Export leaves a temporary file.

Usage: python3 scripts/prepared_asset_verify.py --editor <headless editor>
       [--mesh-count N] [--keep] [--negative-control image|items|drop|multi] [--skip-kill] [--only-world] [--only-encoding] [--only-multi]
Exit code 0 = every check passed.
"""

import argparse
import json
import math
import os
import re
import shutil
import signal
import sqlite3
import struct
import subprocess
import sys
import tempfile
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from erhe_mcp import McpClient  # noqa: E402

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG_PATH  = os.path.join(REPO_ROOT, "logs", "log.txt")
ITEM_URI  = "erhe-item:"
KIND_SCENE        = 1
KIND_LOCAL_MESH   = 2
KIND_WORLD_MESH   = 3
KIND_RAYTRACE_BVH = 4
KIND_TEXTURE      = 5
KIND_BASIS_TEXTURE = 6
KIND_PVS          = 7
# Basis Universal KTX2 images of the Bistro download (doc/agents/linux.md); the texture checks skip without them.
BASIS_DIR    = os.path.join(REPO_ROOT, "res", "editor", "assets", "bistro_lights_compressed")
BASIS_IMAGES = ("MASTER_Trim_Cornice_BaseColor-MASTER_Trim_Cornice_BaseColor.ktx2", "MASTER_Trim_Cornice_Normal.ktx2")

# Export settings of the checks that export without the world-space bake. Their
# PNG images are Texture items of the source bytes, so the prepared runs render
# pixel for pixel; the texture_encoding checks export with target.
NO_BAKE = {"bake_static_to_world_space": False, "texture_encoding": "source"}

RESULTS = []
LAUNCHED = []  # every Editor this script started; main() stops any still running


def check(label, condition, detail=""):
    RESULTS.append((label, bool(condition)))
    suffix = f" ({detail})" if detail else ""
    print(f"{'PASS' if condition else 'FAIL'}: {label}{suffix}", flush=True)
    return bool(condition)


# --- Editor processes -----------------------------------------------------------

class Editor:
    """One headless editor launched by this script; stopped by its own pid."""

    def __init__(self, exe):
        self.exe = exe if os.path.isabs(exe) else os.path.join(REPO_ROOT, exe)
        if not os.path.isfile(self.exe):
            raise RuntimeError(f"no editor at {self.exe}")
        os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
        open(LOG_PATH, "w").close()
        env = dict(os.environ, ERHE_AI_DRIVER="1")
        flags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
        self.process = subprocess.Popen([self.exe], cwd=REPO_ROOT, env=env, creationflags=flags,
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        LAUNCHED.append(self)
        pattern = re.compile(r"MCP server: listening on 127\.0\.0\.1:(\d+) \(pid (\d+)")
        deadline = time.monotonic() + 300.0
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError(f"editor exited with {self.process.returncode} during startup; see logs/log.txt")
            text = read_log()
            ports = [int(m.group(1)) for m in pattern.finditer(text) if int(m.group(2)) == self.process.pid]
            if ports and ("Main loop: completed frame 12" in text):
                self.client = McpClient(ports[0])
                # The image checks compare forms and runs at full detail: the LOD chains of a
                # World-space mesh item are simplified from baked vertices, so coarse levels differ.
                self.call("set_graphics_preset", {"lod_enabled": False})
                return
            time.sleep(0.25)
        self.process.kill()
        raise RuntimeError("editor did not become ready")

    def call(self, tool, arguments=None):
        return self.client.call(tool, arguments or {})

    def stop(self):
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGTERM)
            try:
                self.process.wait(120)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
            scan_file_parse_reads(read_log())

    def kill(self):
        self.process.send_signal(signal.SIGKILL)
        self.process.wait()
        scan_file_parse_reads(read_log())

    # Scenes

    def wait_idle(self, timeout=600.0):
        deadline = time.monotonic() + timeout
        idle_count = 0
        while time.monotonic() < deadline:
            status = self.call("get_async_status")
            settled = status.get("idle", False) and all(
                status.get(field, 0) == 0 for field in ("pending_scene_commits", "queued_operations", "asset_loads")
            )
            idle_count = (idle_count + 1) if settled else 0
            if idle_count >= 3:
                return
            time.sleep(0.1)
        raise RuntimeError("editor not idle")

    def scene_names(self):
        return [scene["name"] for scene in self.call("list_scenes")["scenes"]]

    def wait_scene(self, name, timeout=600.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if name in self.scene_names():
                self.wait_idle()
                return
            time.sleep(0.1)
        raise RuntimeError(f"no scene {name}; scenes {self.scene_names()}")

    def load(self, path):
        reply = self.call("load_scene", {"path": path})
        self.wait_scene(reply["scene_name"])
        return reply["scene_name"]

    def open_foreign(self, path):
        self.call("open_scene", {"path": path})
        name = os.path.basename(path)
        self.wait_scene(name)
        return name

    def close(self, scene):
        # A selected mesh during the close trips a known VERIFY (Lod_drawn_levels::prepare).
        self.call("select_items", {"scene_name": scene, "ids": []})
        self.wait_idle()
        self.call("close_scene", {"scene_name": scene})
        deadline = time.monotonic() + 120.0
        while scene in self.scene_names():
            if time.monotonic() > deadline:
                raise RuntimeError(f"scene {scene} did not close")
            time.sleep(0.1)
        self.wait_idle()

    def save(self, scene, path):
        job = self.client.save("save_scene", {"scene_name": scene, "path": path})
        if job.get("state") != "done":
            raise RuntimeError(f"save_scene {path} failed: {json.dumps(job)[:400]}")

    def scene_settings(self, scene):
        """The scene's Scene_settings (get_scene_settings; {} when every field is at its default)."""
        return self.call("get_scene_settings", {"scene_name": scene}).get("settings") or {}

    def set_export_settings(self, scene, fields):
        """set_scene_settings merge: fields over the scene's export settings, or over a copy of the
        editor's defaults when it has none (adding them); None removes them."""
        self.call("set_scene_settings", {"scene_name": scene, "merge": True, "settings": {"_version": 3, "export_settings": fields}})

    def refusal(self, tool, arguments):
        """The error text of a call that must fail; None when it succeeded."""
        try:
            self.call(tool, arguments)
        except RuntimeError as error:
            return str(error)
        return None

    def begin_export(self, scene, target="pc"):
        return self.call("prepare_scene", {"scene_name": scene, "target": target})["job_id"]

    def job(self, job_id):
        return self.call("get_save_status", {"job_id": job_id})

    def finish_export(self, job_id, timeout=1800.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            status = self.job(job_id)
            if status.get("state") in ("done", "failed"):
                return status
            time.sleep(0.05)
        raise RuntimeError(f"export job {job_id} did not finish")

    def export(self, scene, target="pc"):
        status = self.finish_export(self.begin_export(scene, target))
        self.wait_idle()
        return status

    def status(self, scene, drop=None, target="pc", refresh=False, timeout=600.0):
        """The status computed on request (get_prepared_status with update, polled until it is not
        pending; Refresh only with refresh), or a drop_prepared_items job polled to its end and its prepared_status."""
        if drop is not None:
            job_id = self.call("drop_prepared_items", {"scene_name": scene, "drop": drop, "target": target})["job_id"]
            job = self.finish_export(job_id)
            self.wait_idle()
            if job.get("state") != "done":
                raise RuntimeError(f"status job {job_id} failed: {json.dumps(job)[:400]}")
            return job.get("prepared_status", {})
        self.wait_idle()
        arguments = {"scene_name": scene, "target": target}
        arguments["refresh" if refresh else "update"] = True
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            reply = self.call("get_prepared_status", arguments)
            arguments.pop("refresh", None)
            arguments.pop("update", None)
            if not reply.get("pending"):
                if "prepared_status" not in reply:
                    raise RuntimeError(f"no status of {scene}: {reply.get('error', '')}")
                return reply["prepared_status"]
            time.sleep(0.05)
        raise RuntimeError(f"the status of {scene} stayed pending")

    def render(self, scene, path, eye, target):
        camera = {"eye": eye, "target": target, "fov_y_degrees": 50.0, "near": 0.05, "far": 200.0}
        self.call("render_scene_image", {"scene": scene, "camera": camera, "width": 480, "height": 270, "output": "png", "path": path})


# R12.2: log lines of a glTF file parse that read derived data (prepared mesh data names its document).
FILE_PARSE_READS = []
FILE_PARSE_READ_PATTERN = re.compile(r"prepared mesh data: ([1-9]\d*) used, .* in (\S+\.(?:glb|gltf))\s*$", re.MULTILINE)


def scan_file_parse_reads(text):
    FILE_PARSE_READS.extend(f"{m.group(2)}: {m.group(1)} used" for m in FILE_PARSE_READ_PATTERN.finditer(text))


def check_no_file_parse_reads():
    check("R12.2: no glTF file parse of the run reads prepared mesh data (every 'prepared mesh data: N used' line names an item document)",
          FILE_PARSE_READS == [], "; ".join(FILE_PARSE_READS[:5]))


def read_log():
    try:
        with open(LOG_PATH, "r", encoding="utf-8", errors="replace") as handle:
            return handle.read()
    except OSError:
        return ""


# --- glTF writing ----------------------------------------------------------------

class Gltf_builder:
    def __init__(self):
        self.binary    = bytearray()
        self.views     = []
        self.accessors = []

    def view(self, data):
        while len(self.binary) % 4:
            self.binary.append(0)
        self.views.append({"buffer": 0, "byteOffset": len(self.binary), "byteLength": len(data)})
        self.binary.extend(data)
        return len(self.views) - 1

    def accessor(self, data, component_type, count, kind, extra=None):
        accessor = {"bufferView": self.view(data), "componentType": component_type, "count": count, "type": kind}
        accessor.update(extra or {})
        self.accessors.append(accessor)
        return len(self.accessors) - 1

    def floats(self, values, kind, with_bounds=False):
        width = len(values[0])
        extra = None
        if with_bounds:
            extra = {"min": [min(v[c] for v in values) for c in range(width)], "max": [max(v[c] for v in values) for c in range(width)]}
        return self.accessor(b"".join(struct.pack(f"<{width}f", *v) for v in values), 5126, len(values), kind, extra)

    def indices(self, values):
        return self.accessor(b"".join(struct.pack("<I", v) for v in values), 5125, len(values), "SCALAR")

    def primitive(self, mesh, material, extra_attributes=None):
        positions, normals, texcoords, indices = mesh
        attributes = {
            "POSITION":   self.floats(positions, "VEC3", True),
            "NORMAL":     self.floats(normals, "VEC3"),
            "TEXCOORD_0": self.floats(texcoords, "VEC2")
        }
        attributes.update(extra_attributes or {})
        return {"attributes": attributes, "indices": self.indices(indices), "material": material, "mode": 4}

    def padded_binary(self):
        while len(self.binary) % 4:
            self.binary.append(0)
        return bytes(self.binary)


def make_box(sx, sy, sz):
    positions, normals, texcoords, indices = [], [], [], []
    for axis in range(3):
        for sign in (-1.0, 1.0):
            u_axis, v_axis = [(1, 2), (2, 0), (0, 1)][axis]
            base = len(positions)
            for (a, b) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                p = [0.0, 0.0, 0.0]
                p[axis] = sign
                p[u_axis] = a * sign
                p[v_axis] = b
                positions.append((p[0] * sx, p[1] * sy, p[2] * sz))
                n = [0.0, 0.0, 0.0]
                n[axis] = sign
                normals.append(tuple(n))
                texcoords.append(((a + 1) * 0.5, (b + 1) * 0.5))
            indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    return positions, normals, texcoords, indices


def make_torus(major, minor, nu, nv, wobble=0.0, phase=0.0):
    positions, normals, texcoords, indices = [], [], [], []
    for i in range(nu + 1):
        u = (i / nu) * 2.0 * math.pi
        for j in range(nv + 1):
            v = (j / nv) * 2.0 * math.pi
            r = minor * (1.0 + wobble * math.sin(3.0 * u + phase))
            cx, cz = math.cos(u), math.sin(u)
            positions.append(((major + r * math.cos(v)) * cx, r * math.sin(v), (major + r * math.cos(v)) * cz))
            normals.append((math.cos(v) * cx, math.sin(v), math.cos(v) * cz))
            texcoords.append((i / nu, j / nv))
    for i in range(nu):
        for j in range(nv):
            a = i * (nv + 1) + j
            b = (i + 1) * (nv + 1) + j
            indices += [a, a + 1, b, b, a + 1, b + 1]
    return positions, normals, texcoords, indices


def make_plane(size):
    positions = [(-size, 0.0, -size), (size, 0.0, -size), (size, 0.0, size), (-size, 0.0, size)]
    normals   = [(0.0, 1.0, 0.0)] * 4
    texcoords = [(0.0, 0.0), (4.0, 0.0), (4.0, 4.0), (0.0, 4.0)]
    return positions, normals, texcoords, [0, 2, 1, 0, 3, 2]


def make_bar(segments, height, half_width):
    """A column along +y with rings of four vertices; joint 0 at y=0, joint 1 at y=height/2."""
    positions, normals, texcoords, indices, joints, weights = [], [], [], [], [], []
    corners = [(-1, -1), (1, -1), (1, 1), (-1, 1)]
    for k in range(segments + 1):
        y = height * k / segments
        w1 = min(1.0, max(0.0, (y - 0.35 * height) / (0.3 * height)))
        for (x, z) in corners:
            positions.append((x * half_width, y, z * half_width))
            length = math.sqrt(2.0)
            normals.append((x / length, 0.0, z / length))
            texcoords.append(((x + 1) * 0.5, k / segments))
            joints.append((0, 1, 0, 0))
            weights.append((1.0 - w1, w1, 0.0, 0.0))
    for k in range(segments):
        for c in range(4):
            a = k * 4 + c
            b = k * 4 + ((c + 1) % 4)
            indices += [a, b + 4, b, a, a + 4, b + 4]
    return (positions, normals, texcoords, indices), joints, weights


def png_bytes(width, height, rgba_rows):
    raw = b"".join(b"\x00" + bytes(row) for row in rgba_rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def quat_axis_angle(axis, degrees):
    half = math.radians(degrees) * 0.5
    s = math.sin(half)
    return [axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half)]


def write_small_scene(directory):
    """small.gltf + small.bin + checker.png (the features listed in the docstring)."""
    builder = Gltf_builder()
    checker = [[c for x in range(8) for c in ((230, 200, 40, 255) if ((x // 2 + y // 2) % 2) == 0 else (40, 60, 160, 255))] for y in range(8)]
    with open(os.path.join(directory, "checker.png"), "wb") as handle:
        handle.write(png_bytes(8, 8, checker))

    bar, joints, weights = make_bar(8, 2.0, 0.2)
    joints_accessor  = builder.accessor(b"".join(struct.pack("<4B", *j) for j in joints), 5121, len(joints), "VEC4")
    weights_accessor = builder.floats(weights, "VEC4")
    meshes = [
        {"name": "box",   "primitives": [builder.primitive(make_box(0.4, 0.4, 0.4), 0)]},
        {"name": "floor", "primitives": [builder.primitive(make_plane(4.0), 2)]},
        {"name": "torus", "primitives": [builder.primitive(make_torus(0.6, 0.22, 48, 24), 1)]},
        {"name": "bar",   "primitives": [builder.primitive(bar, 1, {"JOINTS_0": joints_accessor, "WEIGHTS_0": weights_accessor})]}
    ]
    inverse_bind = [
        (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.0, 0.0, 1.5, 1),
        (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.0, -1.0, 1.5, 1)
    ]
    inverse_bind_accessor = builder.accessor(b"".join(struct.pack("<16f", *m) for m in inverse_bind), 5126, 2, "MAT4")
    times    = builder.floats([(0.0,), (1.0,)], "SCALAR", True)
    rotation = builder.floats([tuple(quat_axis_angle((0, 0, 1), 0.0)), tuple(quat_axis_angle((0, 0, 1), 60.0))], "VEC4")
    nodes = [
        {"name": "box_a",      "mesh": 0, "translation": [-2.2, 0.4, 0.6]},
        {"name": "box_b",      "mesh": 0, "translation": [-1.1, 0.4, 0.6], "rotation": quat_axis_angle((0, 1, 0), 30.0)},
        {"name": "box_mirror", "mesh": 0, "translation": [0.0, 0.5, 0.8], "rotation": quat_axis_angle((0, 1, 0), 20.0), "scale": [-1.0, 1.25, 1.0]},
        {"name": "floor",      "mesh": 1},
        {"name": "torus",      "mesh": 2, "translation": [1.8, 0.6, 0.4], "rotation": quat_axis_angle((1, 0, 0), 60.0)},
        {"name": "bar",        "mesh": 3, "skin": 0},
        {"name": "joint_0",    "translation": [0.0, 0.0, -1.5], "children": [7]},
        {"name": "joint_1",    "translation": [0.0, 1.0, 0.0]},
        {"name": "sun",        "rotation": quat_axis_angle((1, 0, 0), -55.0), "extensions": {"KHR_lights_punctual": {"light": 0}}}
    ]
    document = {
        "asset": {"version": "2.0", "generator": "prepared_asset_verify.py"},
        "extensionsUsed": ["KHR_lights_punctual"],
        "extensions": {"KHR_lights_punctual": {"lights": [{"type": "directional", "intensity": 3.0, "color": [1.0, 0.95, 0.9]}]}},
        "scene": 0, "scenes": [{"nodes": [0, 1, 2, 3, 4, 5, 6, 8]}],
        "nodes": nodes, "meshes": meshes,
        "skins": [{"joints": [6, 7], "inverseBindMatrices": inverse_bind_accessor, "skeleton": 6}],
        "animations": [{"name": "bend", "channels": [{"sampler": 0, "target": {"node": 7, "path": "rotation"}}],
                        "samplers": [{"input": times, "output": rotation, "interpolation": "LINEAR"}]}],
        "materials": [
            {"name": "red",     "pbrMetallicRoughness": {"baseColorFactor": [0.8, 0.15, 0.1, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.6}},
            {"name": "blue",    "pbrMetallicRoughness": {"baseColorFactor": [0.15, 0.3, 0.85, 1.0], "metallicFactor": 0.2, "roughnessFactor": 0.4}},
            {"name": "checker", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0, "roughnessFactor": 0.9}}
        ],
        "textures": [{"source": 0, "sampler": 0}],
        "samplers": [{"magFilter": 9728, "minFilter": 9986, "wrapS": 10497, "wrapT": 10497}],
        "images": [{"uri": "checker.png"}],
        "accessors": builder.accessors, "bufferViews": builder.views
    }
    binary = builder.padded_binary()
    document["buffers"] = [{"uri": "small.bin", "byteLength": len(binary)}]
    with open(os.path.join(directory, "small.bin"), "wb") as handle:
        handle.write(binary)
    path = os.path.join(directory, "small.gltf")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(document, handle)
    return path


def write_many_scene(directory, mesh_count):
    """many.gltf + many.bin: mesh_count distinct tori in a grid."""
    builder = Gltf_builder()
    meshes, nodes = [], []
    side = int(math.ceil(math.sqrt(mesh_count)))
    for i in range(mesh_count):
        mesh = make_torus(0.35, 0.12, 40, 20, wobble=0.15, phase=i * 0.01)
        meshes.append({"name": f"torus_{i}", "primitives": [builder.primitive(mesh, 0)]})
        nodes.append({"name": f"torus_{i}", "mesh": i, "translation": [(i % side) * 1.0, 0.0, (i // side) * 1.0]})
    document = {
        "asset": {"version": "2.0", "generator": "prepared_asset_verify.py"},
        "scene": 0, "scenes": [{"nodes": list(range(len(nodes)))}],
        "nodes": nodes, "meshes": meshes,
        "materials": [{"name": "grey", "pbrMetallicRoughness": {"baseColorFactor": [0.6, 0.6, 0.6, 1.0]}}],
        "accessors": builder.accessors, "bufferViews": builder.views
    }
    binary = builder.padded_binary()
    document["buffers"] = [{"uri": "many.bin", "byteLength": len(binary)}]
    with open(os.path.join(directory, "many.bin"), "wb") as handle:
        handle.write(binary)
    path = os.path.join(directory, "many.gltf")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(document, handle)
    return path


# --- Containers and images ---------------------------------------------------------

def read_store(path):
    """{key hex: (kind, target, stamp, meta, payload, info)} and the asset records of a container."""
    connection = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    try:
        items = {bytes(row[0]).hex(): tuple(row[1:]) for row in connection.execute(
            "SELECT i.key, i.kind, i.target, i.stamp, i.meta, p.payload, i.info FROM item AS i JOIN item_payload AS p ON p.id = i.id")}
        records = dict(connection.execute("SELECT name, value FROM asset").fetchall())
    finally:
        connection.close()
    return items, records


# R12.2: what a .gltf / .glb file of this build never carries.
DERIVED_DATA_NAMES = ("ERHE_mesh_optimized", "ERHE_mesh_topology", "ERHE_raytrace_bvh", "ERHE_gpu_vertex_streams",
                      "ERHE_texture_ktx2", "ERHE_pvs", "source_content_hash", "\"levels\"")


# R12.3: the editor asset is the only persistent derived-data store.
CACHE_DIRECTORIES = (os.path.join(REPO_ROOT, "cache", "mesh_optimizer"), os.path.join(REPO_ROOT, "cache", "bvh"))


def cache_directory_state():
    """Which of CACHE_DIRECTORIES exist, and the mtime of every file in them."""
    files = {}
    for directory in CACHE_DIRECTORIES:
        for root, _, names in os.walk(directory):
            for name in names:
                path = os.path.join(root, name)
                files[path] = os.stat(path).st_mtime_ns
    return {"existed": [os.path.exists(directory) for directory in CACHE_DIRECTORIES], "files": files}


def check_cache_directories(before):
    """R12.3: no cache directory created, none an earlier build left touched."""
    after = cache_directory_state()
    created = [d for d, existed in zip(CACHE_DIRECTORIES, before["existed"]) if (not existed) and os.path.exists(d)]
    touched = sorted(path for path in set(before["files"]) | set(after["files"]) if before["files"].get(path) != after["files"].get(path))
    left = [d for d, existed in zip(CACHE_DIRECTORIES, before["existed"]) if existed]
    if left:
        print(f"note: {left} left by an earlier build; this build neither reads nor writes them", flush=True)
    check("R12.3: the run creates no cache/mesh_optimizer and no cache/bvh directory and touches no file in one an earlier build left",
          (created == []) and (touched == []), f"created {created}; touched {len(touched)}: {touched[:3]}")


def check_no_derived_data_files(directory):
    """R12.2: no .gltf / .glb file under directory carries a derived-data extension."""
    carriers = []
    for root, _, names in os.walk(directory):
        for name in names:
            if not name.endswith((".glb", ".gltf")):
                continue
            path = os.path.join(root, name)
            try:
                text = json.dumps(gltf_document(path))
            except (OSError, ValueError) as error:
                carriers.append(f"{os.path.relpath(path, directory)} unreadable: {error}")
                continue
            found = [derived for derived in DERIVED_DATA_NAMES if derived in text]
            if found:
                carriers.append(f"{os.path.relpath(path, directory)}: {found}")
    check("R12.2: no .gltf / .glb file of the run (Save, Export, inputs) carries a derived-data extension", carriers == [], "; ".join(carriers[:5]))


def gltf_document(path):
    """The JSON of a .glb or .gltf file."""
    if path.endswith(".gltf"):
        with open(path, "r", encoding="utf-8") as handle:
            return json.load(handle)
    return glb_document(path)


def glb_document(path):
    """The glTF JSON of a GLB file."""
    with open(path, "rb") as handle:
        return scene_item_document(handle.read())


def file_export_settings(path):
    """The export_settings the ERHE_scene extension of a saved scene carries (None when absent)."""
    for scene in glb_document(path).get("scenes", []):
        settings = scene.get("extensions", {}).get("ERHE_scene", {}).get("settings") or {}
        if settings.get("export_settings") is not None:
            return settings["export_settings"]
    return None


def kind_items(store_path, kind):
    """{key: (meta, payload, stamp)} of the items of one kind a container holds."""
    items, _ = read_store(store_path)
    return {key: (value[3], value[4], value[2]) for key, value in items.items() if value[0] == kind}


def mesh_items(prepared_path):
    return kind_items(prepared_path, KIND_LOCAL_MESH)


def bvh_items(prepared_path):
    return kind_items(prepared_path, KIND_RAYTRACE_BVH)


def scene_item_bvh_buffers(glb):
    """Per primitive of a Scene item, the uris of the buffers its ERHE_raytrace_bvh views lie in (empty: no BVH)."""
    document = scene_item_document(glb)
    result = []
    for mesh in document.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            extension = primitive.get("extensions", {}).get("ERHE_raytrace_bvh")
            uris = set()
            if extension is not None:
                for build in extension.get("builds", []):
                    for member in ("nodes", "primitive_ids"):
                        view = document["bufferViews"][build[member]]
                        uris.add(document["buffers"][view["buffer"]].get("uri", ""))
            result.append(uris)
    return result


def check_bvh_items(label, prepared_path, expected_count):
    """The mesh items carry no BVH; each primitive's ERHE_raytrace_bvh reads one BVH item of stamp bvh=<backend>/<library>."""
    items, records = read_store(prepared_path)
    meshes = mesh_items(prepared_path)
    bvhs   = bvh_items(prepared_path)
    buffers = scene_item_bvh_buffers(items[records["prepared.scene_key"]][4])
    bvh_uris = {ITEM_URI + key for key in bvhs}
    check(f"{label}: {expected_count} BVH items, no mesh item carries ERHE_raytrace_bvh",
          (len(bvhs) == expected_count) and not any("ERHE_raytrace_bvh" in meta for meta, _, _ in meshes.values()),
          f"{len(meshes)} mesh items, {len(bvhs)} BVH items")
    if expected_count > 0:
        check(f"{label}: every primitive's ERHE_raytrace_bvh reads one BVH item; BVH stamps are bvh=<backend>/<library>",
              all((len(uris) == 1) and (next(iter(uris)) in bvh_uris) for uris in buffers) and
              all(stamp.startswith("bvh=") and ('"extension"' in meta) for meta, _, stamp in bvhs.values()),
              f"{len(buffers)} primitives; stamps {sorted({stamp for _, _, stamp in bvhs.values()})}")
    else:
        check(f"{label}: no primitive of the Scene item has ERHE_raytrace_bvh", all(not uris for uris in buffers), f"{len(buffers)} primitives")


# A ray along the torus axis through its hole: the triangle BVH misses the
# torus and hits the floor; a bounding box proxy would hit the torus.
TORUS_RAY = {"origin": [1.8, 2.1, 2.9980762], "direction": [0.0, -0.5, -0.8660254]}


def ray_hit(editor, scene):
    reply = editor.call("raycast", dict(TORUS_RAY, scene_name=scene))
    return (reply.get("node_name"), round(reply.get("distance", -1.0), 4)) if reply.get("hit") else (None, None)


def wait_ray_hit(editor, scene, expected, timeout=60.0):
    """The ray's hit once the scene's raytrace is built (BVHs a load does not read are built after it);
    expected is a (node, distance) hit or a node name."""
    def matches(hit):
        return (hit == expected) if isinstance(expected, tuple) else (hit[0] == expected)
    deadline = time.monotonic() + timeout
    hit = ray_hit(editor, scene)
    while (not matches(hit)) and (time.monotonic() < deadline):
        time.sleep(0.2)
        hit = ray_hit(editor, scene)
    return hit


def scene_item_document(glb):
    """The glTF JSON of a Scene item (GLB)."""
    magic, _, _ = struct.unpack_from("<III", glb, 0)
    if magic != 0x46546C67:
        raise RuntimeError("the Scene item is not a GLB")
    length, = struct.unpack_from("<I", glb, 12)
    return json.loads(glb[20:20 + length])


def scene_item_keys(glb):
    """The keys of the erhe-item: buffers of a Scene item (GLB)."""
    document = scene_item_document(glb)
    return {buffer["uri"][len(ITEM_URI):] for buffer in document.get("buffers", []) if buffer.get("uri", "").startswith(ITEM_URI)}


def scene_item_mesh_forms(glb):
    """{mesh name: 'mesh item' | 'ERHE_geometry' | 'inside'}: where the Scene item's first primitive of each mesh lives."""
    document = scene_item_document(glb)
    forms = {}
    for mesh in document.get("meshes", []):
        primitive = mesh["primitives"][0]
        if "ERHE_geometry" in primitive.get("extensions", {}):
            forms[mesh.get("name", "?")] = "ERHE_geometry"
            continue
        accessor = document["accessors"][primitive["attributes"]["POSITION"]]
        buffer = document["buffers"][document["bufferViews"][accessor["bufferView"]]["buffer"]]
        forms[mesh.get("name", "?")] = "mesh item" if buffer.get("uri", "").startswith(ITEM_URI) else "inside"
    return forms


def check_mesh_forms(label, prepared_path):
    """Every mesh of the prepared asset's Scene item reads a mesh item."""
    items, records = read_store(prepared_path)
    forms = scene_item_mesh_forms(items[records["prepared.scene_key"]][4])
    others = {name: form for name, form in forms.items() if form != "mesh item"}
    check(f"{label}: every mesh of the Scene item is a mesh item", not others, json.dumps(others) if others else f"{len(forms)} meshes")


def decode_png(path):
    """(width, height, RGBA8 pixel bytes) of an 8-bit RGB / RGBA PNG."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise RuntimeError(f"{path} is not a PNG")
    offset, idat = 8, bytearray()
    width = height = channels = 0
    while offset < len(data):
        length, tag = struct.unpack_from(">I4s", data, offset)
        body = data[offset + 8:offset + 8 + length]
        if tag == b"IHDR":
            width, height, depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if (depth != 8) or (color_type not in (2, 6)) or (interlace != 0):
                raise RuntimeError(f"{path}: unsupported PNG format")
            channels = 4 if color_type == 6 else 3
        elif tag == b"IDAT":
            idat.extend(body)
        offset += 12 + length
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    pixels = bytearray()
    previous = bytearray(stride)
    position = 0
    for _ in range(height):
        filter_type = raw[position]
        line = bytearray(raw[position + 1:position + 1 + stride])
        position += 1 + stride
        for i in range(stride):
            left  = line[i - channels] if i >= channels else 0
            up    = previous[i]
            upper_left = previous[i - channels] if i >= channels else 0
            if filter_type == 1:
                line[i] = (line[i] + left) & 0xff
            elif filter_type == 2:
                line[i] = (line[i] + up) & 0xff
            elif filter_type == 3:
                line[i] = (line[i] + ((left + up) >> 1)) & 0xff
            elif filter_type == 4:
                p = left + up - upper_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - upper_left)
                predictor = left if (pa <= pb and pa <= pc) else (up if pb <= pc else upper_left)
                line[i] = (line[i] + predictor) & 0xff
        if channels == 4:
            pixels.extend(line)
        else:
            for i in range(0, stride, 3):
                pixels.extend(line[i:i + 3] + b"\xff")
        previous = line
    return width, height, bytes(pixels)


def compare_images(path_a, path_b):
    """(identical, differing pixel count, total pixel count) on decoded pixels."""
    wa, ha, a = decode_png(path_a)
    wb, hb, b = decode_png(path_b)
    if (wa, ha) != (wb, hb):
        return False, -1, wa * ha
    differing = sum(1 for i in range(0, len(a), 4) if a[i:i + 4] != b[i:i + 4])
    return differing == 0, differing, wa * ha


def distinct_colors(path):
    _, _, pixels = decode_png(path)
    return len({pixels[i:i + 4] for i in range(0, len(pixels), 4)})


# --- The checks --------------------------------------------------------------------

CAMERAS = [
    ("front", [0.5, 2.6, 6.5], [0.0, 0.6, 0.0]),
    ("side",  [-5.0, 2.0, -3.0], [0.0, 0.8, -0.5])
]
ANIMATION_TIME = 0.75


def status_kind(status, kind):
    return status.get("kinds", {}).get(kind, {})


def status_text(status):
    kinds = {kind: {k: v for k, v in counts.items() if k in ("present", "missing", "unreferenced_count")} for kind, counts in status.get("kinds", {}).items()}
    return (f"kinds {json.dumps(kinds)}; matches {status.get('matches')} ({status.get('mismatch', '')}); "
            f"editor asset {status.get('editor_asset_bytes')} bytes; drop {status.get('drop')} {status.get('dropped_count')}")


def export_counts(status, kind):
    return status.get("export", {}).get("counts", {}).get(kind, {})


def rewritten(status):
    """Copy wrote the whole prepared asset anew: the written counts add up to its item count."""
    export = status.get("export", {})
    written = sum(value.get("written", 0) for value in export.get("counts", {}).values())
    return (written > 0) and (written == export.get("prepared_item_count"))


def counts_text(status):
    counts = status.get("export", {}).get("counts", {})
    return " ".join(f"{kind}={value}" for kind, value in sorted(counts.items()))


def render_set(editor, scene, directory, tag):
    paths = {}
    for pose in ("bind", "animated"):
        editor.call("animation_playback", {"scene_name": scene, "animation": "bend", "action": "pause",
                                           "time": 0.0 if pose == "bind" else ANIMATION_TIME})
        editor.wait_idle()
        for name, eye, target in CAMERAS:
            path = os.path.join(directory, f"{tag}_{pose}_{name}.png")
            editor.render(scene, path, eye, target)
            paths[(pose, name)] = path
    return paths


def check_holds_exactly(label, prepared_path, status, negative_control):
    items, records = read_store(prepared_path)
    if negative_control == "items":
        drop = sorted(key for key, value in items.items() if value[0] == KIND_LOCAL_MESH)[0]
        del items[drop]
        print(f"(negative control: item {drop} left out of the prepared asset's item set)")
    scene_key = records.get("prepared.scene_key", "")
    check(f"{label}: prepared.scene_key is the Export's Scene key", scene_key == status.get("export", {}).get("scene_key"), scene_key)
    check(f"{label}: prepared.target is pc", records.get("prepared.target") == "pc", records.get("prepared.target", ""))
    if scene_key not in items:
        check(f"{label}: the prepared asset holds its Scene item", False)
        return items
    scene_item = items[scene_key]
    named = scene_item_keys(scene_item[4])
    expected = named | {scene_key}
    check(
        f"{label}: the prepared asset holds exactly the items its Scene item names plus the Scene item",
        set(items.keys()) == expected,
        f"{len(items)} held, {len(named)} named, missing {len(expected - set(items.keys()))}, extra {len(set(items.keys()) - expected)}"
    )
    kinds_ok = (scene_item[0] == KIND_SCENE) and all(items[key][0] in (KIND_LOCAL_MESH, KIND_RAYTRACE_BVH, KIND_TEXTURE) for key in named if key in items)
    check(f"{label}: the Scene item is kind scene, every named item kind local_mesh, raytrace_bvh or texture", kinds_ok)
    check_stamp_records(label, items, records)
    directory, name = os.path.split(prepared_path)
    left = [entry for entry in os.listdir(directory) if entry.startswith(name + ".tmp-")]
    check(f"{label}: no temporary file of the prepared asset remains", not left, json.dumps(left))
    return items


STAMP_RECORDS = ("prepared.mesh_stamp", "prepared.bvh_stamp", "prepared.scene_stamp", "prepared.texture_stamps")


def check_stamp_records(label, items, records):
    """Copy's stamp records (doc/editor/prepared_assets.md "Format stamps") are the stamps of the items the Scene item names."""
    scene_key = records.get("prepared.scene_key", "")
    named = scene_item_keys(items[scene_key][4]) if scene_key in items else set()
    def stamps(*kinds):
        return {items[key][2] for key in named if (key in items) and (items[key][0] in kinds)}
    mesh, bvh, texture = stamps(KIND_LOCAL_MESH, KIND_WORLD_MESH), stamps(KIND_RAYTRACE_BVH), stamps(KIND_TEXTURE)
    expected = {
        "prepared.mesh_stamp":     records.get("prepared.mesh_stamp") if not mesh else (mesh.pop() if len(mesh) == 1 else None),
        "prepared.bvh_stamp":      "none" if not bvh else (bvh.pop() if len(bvh) == 1 else None),
        "prepared.scene_stamp":    items[scene_key][2] if scene_key in items else None,
        "prepared.texture_stamps": " | ".join(sorted(texture)),
    }
    actual = {name: records.get(name) for name in STAMP_RECORDS}
    check(f"{label}: the stamp records are the stamps of the items the Scene item names ('none' without BVH items)",
          actual == expected, json.dumps(actual))


def phase_export(args, work):
    """Process 1: foreign glTF -> erhe-saved source -> Export; source images."""
    foreign_dir = os.path.join(work, "foreign")
    source_dir  = os.path.join(work, "source")
    images_dir  = os.path.join(work, "images")
    for directory in (foreign_dir, source_dir, images_dir):
        os.makedirs(directory, exist_ok=True)
    foreign = write_small_scene(foreign_dir)
    source = os.path.join(source_dir, "small.glb")
    editor = Editor(args.editor)
    try:
        name = editor.open_foreign(foreign)
        # Export settings (doc/editor/prepared_assets.md "Export settings"): an imported scene has none.
        check("an imported scene has no export settings", "export_settings" not in editor.scene_settings(name),
              json.dumps(editor.scene_settings(name).get("export_settings")))
        refusal = editor.refusal("prepare_scene", {"scene_name": name, "target": "pc"})
        check("Export of a scene without export settings is refused, saying to add them",
              (refusal is not None) and ("has no export settings - add them" in refusal), (refusal or "")[:200])
        refusal = editor.refusal("get_prepared_status", {"scene_name": name})
        check("the status of a scene without export settings is refused", (refusal is not None) and ("has no export settings" in refusal), (refusal or "")[:200])
        refusal = editor.refusal("bake_world_space_meshes", {"scene_name": name})
        check("Bake World-Space Meshes of a scene without export settings is refused",
              (refusal is not None) and ("has no export settings" in refusal), (refusal or "")[:200])
        editor.save(name, source)
        editor.close(name)
        # R1.1, R8.4: Save Scene writes authoring data alone: the soups, no derived-data extension.
        saved_doc = glb_document(source)
        saved_text = json.dumps(saved_doc)
        derived = [name for name in DERIVED_DATA_NAMES if name in saved_text]
        check("Save Scene writes no derived-data extension (ERHE_mesh_optimized, _topology, ERHE_raytrace_bvh, ERHE_gpu_vertex_streams, ERHE_texture_ktx2, ERHE_pvs, LOD chains)",
              (derived == []) and (len(saved_doc.get("meshes", [])) > 0), f"{derived}; extensionsUsed {saved_doc.get('extensionsUsed')}")
        scene = editor.load(source)
        check("load of the erhe-saved source names the scene by its stem", scene == "small", scene)
        # An erhe scene saved without export settings gets a copy of the editor's defaults at load; the file keeps none until a Save.
        check("an erhe scene saved without export settings opens with the editor's default export settings",
              ("export_settings" in editor.scene_settings(scene)) and (file_export_settings(source) is None),
              json.dumps(editor.scene_settings(scene).get("export_settings"))[:200])
        editor.wait_idle()
        deadline = time.monotonic() + 60.0
        reply = editor.call("get_prepared_status", {"scene_name": scene, "update": True})
        while reply.get("pending") and (time.monotonic() < deadline):
            time.sleep(0.05)
            reply = editor.call("get_prepared_status", {"scene_name": scene})
        # R8.1 - R8.3: the open made the editor asset and stored the mesh and BVH items it derived.
        load_line = re.search(r"small\.glb: (\d+) primitives from items, (\d+) derived and stored, (\d+) BVH from items, (\d+) built and stored", read_log())
        check("the open of the saved source derives and stores its four mesh items and four BVH items (one log line)",
              (load_line is not None) and (load_line.groups() == ("0", "4", "0", "4")), load_line.group(0) if load_line else "no line")
        loaded_status = reply.get("prepared_status", {})
        loaded_asset = os.path.join(source_dir, "small.erhe_editor")
        check("the status of that scene runs without Add Export Settings and finds the mesh items the open stored present",
              (reply.get("pending") is False) and os.path.isfile(loaded_asset) and
              (status_kind(loaded_status, "local_mesh").get("missing") == 0) and
              (store_kind_counts(loaded_asset) == {KIND_LOCAL_MESH: 4, KIND_RAYTRACE_BVH: 4}),
              status_text(loaded_status) + " " + reply.get("error", ""))
        nodes = {node["name"] for node in editor.call("get_scene_nodes", {"scene_name": scene})["nodes"]}
        check("the source scene holds the hand-written nodes", {"box_a", "box_b", "box_mirror", "floor", "torus", "bar", "joint_0", "joint_1"} <= nodes, f"{len(nodes)} nodes")
        animations = editor.call("get_scene_animations", {"scene_name": scene})
        check("the source scene holds the animation", "bend" in json.dumps(animations))

        for argument, value in (("export_settings", {"raytrace_bvh": False}), ("texture_supercompression", "none"), ("container", "store")):
            refusal = editor.refusal("prepare_scene", {"scene_name": scene, "target": "pc", argument: value})
            check(f"prepare_scene refuses the per-call argument {argument}",
                  (refusal is not None) and (f"'{argument}' is not an argument" in refusal), (refusal or "")[:200])
        editor.set_export_settings(scene, {})
        added = editor.scene_settings(scene).get("export_settings") or {}
        with open(os.path.join(REPO_ROOT, "config", "editor", "editor_settings.json")) as handle:
            editor_lod = json.load(handle).get("lod", {})
        lod_fields = ("level_count", "ratio", "attributes", "border", "seams", "fallback")
        check("adding export settings copies the editor's defaults (LOD request included)",
              (added.get("raytrace_bvh") is True) and (added.get("texture_supercompression") == "zstd") and
              (added.get("bake_static_to_world_space") is True) and (added.get("texture_encoding") == "target") and
              all(added.get("lod", {}).get(field) == editor_lod.get(field, added.get("lod", {}).get(field)) for field in lod_fields) and
              (added.get("lod", {}).get("level_count") is not None),
              json.dumps(added)[:300])
        editor.save(scene, source)
        check("the saved source carries the export settings", file_export_settings(source) == added,
              json.dumps(file_export_settings(source))[:200])
        editor.close(scene)
        scene = editor.load(source)
        check("the reloaded source has the export settings", editor.scene_settings(scene).get("export_settings") == added)
        # The checks before the world-space phase export without the bake (doc/editor/world_space_bake.md).
        editor.set_export_settings(scene, NO_BAKE)
        editor.save(scene, source)
        editor.close(scene)
        scene = editor.load(source)

        status = editor.export(scene)
        mesh = export_counts(status, "local_mesh")
        bvh  = export_counts(status, "raytrace_bvh")
        scene_counts = export_counts(status, "scene")
        check("first Export is done", status.get("state") == "done", status.get("error", ""))
        # box (shared by three nodes), floor, torus, bar: four unique mesh items, each with its BVH item,
        # stored by the open (R8.3): Ensure finds them present.
        check(
            "first Export finds the four mesh items and four BVH items the open stored, makes one Scene item and copies them",
            (mesh.get("made") == 0) and (mesh.get("present") == 4) and (mesh.get("written") == 4) and
            (bvh.get("made") == 0) and (bvh.get("present") == 4) and (bvh.get("written") == 4) and
            (scene_counts.get("made") == 1) and (scene_counts.get("written") == 1),
            counts_text(status)
        )
        check("first Export writes no primitive inside the Scene item", status.get("export", {}).get("inline_primitive_count") == 0,
              str(status.get("export", {}).get("inline_primitive_count")))
        editor_asset = os.path.join(source_dir, "small.erhe_editor")
        prepared     = os.path.join(source_dir, "small.pc.erhe_prepared")
        check("the editor asset and the prepared asset lie beside the source",
              os.path.isfile(editor_asset) and os.path.isfile(prepared) and (os.path.normpath(status.get("path", "")) == os.path.normpath(prepared)))
        check("the prepared asset is one file at rest", not any(os.path.exists(prepared + suffix) for suffix in ("-wal", "-shm", "-journal")))
        _, editor_records = read_store(editor_asset)
        check("editor asset records the source and the prepared asset",
              (editor_records.get("source.relative_path") == "small.glb") and (editor_records.get("prepared_path.pc") == "small.pc.erhe_prepared"),
              json.dumps({k: v for k, v in editor_records.items() if k != "format_version"}))
        items = check_holds_exactly("first Export", prepared, status, args.negative_control)
        check_mesh_forms("first Export", prepared)
        check_bvh_items("first Export", prepared, 4)
        check("prepared asset records the source relative to itself", read_store(prepared)[1].get("source.relative_path") == "small.glb")
        images = render_set(editor, scene, images_dir, "source")
        check("the source images are not blank", all(distinct_colors(path) > 16 for path in images.values()))
        bind, animated = images[("bind", "front")], images[("animated", "front")]
        check("the animation pose changes the image (the skinned mesh moves)", not compare_images(bind, animated)[0])
        editor.close(scene)

        # R3.5, R6.1: the foreign glTF exported with the same settings makes the items of the
        # erhe scene saved from it: the saved file holds the source soups, so the reopen hashes them alike.
        name = editor.open_foreign(foreign)
        editor.set_export_settings(name, {})
        editor.set_export_settings(name, NO_BAKE)
        foreign_status = editor.export(name)
        check("Export of the foreign glTF is done", foreign_status.get("state") == "done", foreign_status.get("error", ""))
        editor.close(name)
        foreign_asset = os.path.join(foreign_dir, "small.erhe_editor")
        # The foreign editor asset also holds the items its open stored without export settings (no LOD request).
        foreign_prepared = os.path.join(foreign_dir, "small.pc.erhe_prepared")
        for kind, label in ((KIND_LOCAL_MESH, "mesh"), (KIND_RAYTRACE_BVH, "BVH"), (KIND_TEXTURE, "Texture")):
            foreign_items = kind_items(foreign_asset, kind)
            source_items  = kind_items(editor_asset, kind)
            exported      = set(kind_items(foreign_prepared, kind))
            check(f"R6.1: the foreign import's exported {label} items are the saved erhe scene's: the same keys and the same bytes",
                  (len(exported) > 0) and (exported == set(source_items)) and all(foreign_items.get(key) == source_items[key] for key in source_items),
                  f"{len(exported)} exported from the foreign, {len(source_items)} saved, {len(exported & set(source_items))} shared keys")
    finally:
        editor.stop()
    return source, images, status, items


def phase_reexport_and_open(args, work, source, source_images, first_status):
    """Process 2: second Export, edit / undo Exports, open with the source in place and moved away."""
    source_dir = os.path.dirname(source)
    prepared   = os.path.join(source_dir, "small.pc.erhe_prepared")
    images_dir = os.path.join(work, "images")
    editor = Editor(args.editor)
    try:
        scene = editor.load(source)
        # R8.2, R13.3 in small: a second open builds every primitive from the items the first stored.
        log = read_log()
        log = log[max(0, log.find("async parse_gltf 'small.glb'")):]  # the open, not the startup scene's brushes
        load_line = re.search(r"small\.glb: (\d+) primitives from items, (\d+) derived and stored, (\d+) BVH from items, (\d+) built and stored", log)
        # The scene's own meshes; the open's helper geometry (light and camera gizmos) is procedural.
        optimized = re.findall(r"mesh optimize (box\w*|floor|torus|bar)\b", log)
        # The helper geometry's BVHs are built (no BVH disk cache, R12.3); the scene's come from items.
        check("a second open of the saved source loads its four mesh items and four BVH items and derives nothing",
              (load_line is not None) and (load_line.groups() == ("4", "0", "4", "0")) and (optimized == []),
              (load_line.group(0) if load_line else "no line") + f"; BVH builds of helper geometry {log.count('BVH build ')}, scene mesh optimize lines {optimized}")
        status = editor.export(scene)
        mesh, scene_counts = export_counts(status, "local_mesh"), export_counts(status, "scene")
        bvh = export_counts(status, "raytrace_bvh")
        check(
            "second Export in a new process makes no item and writes the whole prepared asset anew",
            (status.get("state") == "done") and (mesh.get("made") == 0) and (mesh.get("present") == 4) and (mesh.get("written") == 4) and
            (bvh.get("made") == 0) and (bvh.get("present") == 4) and (bvh.get("written") == 4) and
            (scene_counts.get("made") == 0) and (scene_counts.get("present") == 1) and (scene_counts.get("written") == 1) and rewritten(status),
            counts_text(status)
        )
        check("second Export has the first Export's Scene key", status.get("export", {}).get("scene_key") == first_status.get("export", {}).get("scene_key"))
        prepared_status = editor.status(scene)
        check(
            "status after Export: nothing missing, the prepared asset matches, the Export's Scene key",
            (status_kind(prepared_status, "local_mesh").get("present") == 4) and (status_kind(prepared_status, "local_mesh").get("missing") == 0) and
            (status_kind(prepared_status, "raytrace_bvh").get("present") == 4) and (status_kind(prepared_status, "raytrace_bvh").get("missing") == 0) and
            (status_kind(prepared_status, "scene").get("present") == 1) and (status_kind(prepared_status, "scene").get("missing") == 0) and
            prepared_status.get("matches") is True and prepared_status.get("prepared_asset_exists") is True and
            (prepared_status.get("scene_key") == first_status.get("export", {}).get("scene_key")) and
            (prepared_status.get("prepared_scene_key") == prepared_status.get("scene_key")),
            status_text(prepared_status)
        )
        check(
            "status names both assets with their sizes",
            (os.path.normpath(prepared_status.get("prepared_asset", "")) == os.path.normpath(prepared)) and
            (os.path.normpath(prepared_status.get("editor_asset", "")) == os.path.normpath(os.path.join(source_dir, "small.erhe_editor"))) and
            (prepared_status.get("prepared_asset_bytes", 0) > 0) and (prepared_status.get("editor_asset_bytes", 0) > 0) and
            (prepared_status.get("prepared_item_count") == 10),
            json.dumps({k: prepared_status.get(k) for k in ("editor_asset", "editor_asset_bytes", "prepared_asset", "prepared_asset_bytes", "prepared_item_count")})
        )

        # One vertex of the torus moves (an undoable Geometry Spreadsheet edit).
        position = editor.call("get_mesh_attribute_values", {"scene_name": scene, "node_name": "torus", "domain": "vertex", "indices": [0], "attributes": ["position"]})
        edit = editor.call("set_mesh_attribute_values", {"scene_name": scene, "node_name": "torus", "attribute": "position", "elements": [0], "value": [0.9, 0.05, 0.0]})
        editor.wait_idle()
        after = editor.call("get_mesh_attribute_values", {"scene_name": scene, "node_name": "torus", "domain": "vertex", "indices": [0], "attributes": ["position"]})
        check("the MCP edit moved the torus vertex", json.dumps(position) != json.dumps(after), json.dumps(edit)[:200])
        # The edit marks the status stale and computes nothing until an update is requested.
        time.sleep(0.5)
        unrequested = editor.call("get_prepared_status", {"scene_name": scene})
        check(
            "status after the vertex edit, before an update is requested: stale, not pending, the earlier status still shown",
            (unrequested.get("stale") is True) and (unrequested.get("pending") is False) and
            (status_kind(unrequested.get("prepared_status", {}), "local_mesh").get("missing") == 0),
            json.dumps({k: unrequested.get(k) for k in ("stale", "pending", "error")})
        )
        prepared_status = editor.status(scene)
        check(
            "status after the vertex edit, with an update and no Refresh: one mesh item and its BVH item missing, the prepared asset does not match",
            (status_kind(prepared_status, "local_mesh").get("missing") == 1) and (status_kind(prepared_status, "local_mesh").get("present") == 3) and
            (status_kind(prepared_status, "raytrace_bvh").get("missing") == 1) and (status_kind(prepared_status, "raytrace_bvh").get("present") == 3) and
            (status_kind(prepared_status, "scene").get("missing") == 1) and prepared_status.get("matches") is False,
            status_text(prepared_status)
        )
        status = editor.export(scene)
        mesh, scene_counts = export_counts(status, "local_mesh"), export_counts(status, "scene")
        bvh = export_counts(status, "raytrace_bvh")
        check(
            "Export after the vertex edit makes one mesh item, its BVH item and one Scene item",
            (status.get("state") == "done") and (mesh.get("made") == 1) and (mesh.get("present") == 3) and
            (bvh.get("made") == 1) and (bvh.get("present") == 3) and (bvh.get("written") == 4) and
            (scene_counts.get("made") == 1) and (scene_counts.get("present") == 0) and
            (mesh.get("written") == 4) and (scene_counts.get("written") == 1) and rewritten(status),
            counts_text(status)
        )
        check_holds_exactly("Export after the edit", prepared, status, None)
        check_mesh_forms("Export after the edit", prepared)

        undo = editor.call("undo")
        editor.wait_idle()
        status = editor.export(scene)
        mesh, scene_counts = export_counts(status, "local_mesh"), export_counts(status, "scene")
        bvh = export_counts(status, "raytrace_bvh")
        check(
            "Export after undo makes no item (the original items come back)",
            (status.get("state") == "done") and (mesh.get("made") == 0) and (mesh.get("present") == 4) and
            (bvh.get("made") == 0) and (bvh.get("present") == 4) and (bvh.get("written") == 4) and
            (scene_counts.get("made") == 0) and (scene_counts.get("present") == 1) and
            (mesh.get("written") == 4) and (scene_counts.get("written") == 1) and rewritten(status),
            f"{counts_text(status)}; undo {json.dumps(undo.get('performed', ''))}"
        )
        check("Export after undo has the first Export's Scene key", status.get("export", {}).get("scene_key") == first_status.get("export", {}).get("scene_key"))
        check_holds_exactly("Export after undo", prepared, status, None)
        check_mesh_forms("Export after undo", prepared)
        prepared_status = editor.status(scene)
        check(
            "status after undo and Export: matches, the edit's mesh item, BVH item and Scene item unreferenced",
            prepared_status.get("matches") is True and (status_kind(prepared_status, "local_mesh").get("missing") == 0) and
            (status_kind(prepared_status, "local_mesh").get("unreferenced_count") == 1) and (status_kind(prepared_status, "scene").get("unreferenced_count") == 1) and
            (status_kind(prepared_status, "raytrace_bvh").get("unreferenced_count") == 1),
            status_text(prepared_status)
        )
        undo_scene_key = status.get("export", {}).get("scene_key")
        # R3.7: a settings edit changes the Texture keys at the edit (no Refresh), and setting it back asks for the earlier ones (R3.3).
        editor.set_export_settings(scene, {"texture_encoding": "target"})
        encoded_status = editor.status(scene)
        check("a texture_encoding edit changes the Texture key and asks for a Basis texture item, with no Refresh",
              (status_kind(encoded_status, "texture").get("missing") == 1) and (status_kind(encoded_status, "basis_texture").get("missing") == 1) and
              (status_kind(encoded_status, "local_mesh").get("missing") == 0) and encoded_status.get("matches") is False,
              status_text(encoded_status))
        editor.set_export_settings(scene, {"texture_encoding": "source"})
        restored_status = editor.status(scene)
        check("setting texture_encoding back asks for the earlier Texture key and matches with the last Scene key",
              (status_kind(restored_status, "texture").get("missing") == 0) and ("basis_texture" not in restored_status.get("kinds", {})) and
              restored_status.get("matches") is True and (restored_status.get("scene_key") == undo_scene_key),
              status_text(restored_status))
        if args.negative_control == "drop":
            print("(negative control: the drop of unreferenced items is not run)")
            dropped = editor.status(scene)
        else:
            dropped = editor.status(scene, "unreferenced")
        check(
            "drop of unreferenced items: none left, the editor asset shrinks, the scene's items stay",
            (dropped.get("drop") == "unreferenced") and (dropped.get("dropped_count") == 3) and
            (status_kind(dropped, "local_mesh").get("unreferenced_count") == 0) and (status_kind(dropped, "scene").get("unreferenced_count") == 0) and
            (status_kind(dropped, "raytrace_bvh").get("unreferenced_count") == 0) and
            (dropped.get("editor_asset_bytes", 0) < dropped.get("bytes_before_drop", 0)) and
            (status_kind(dropped, "local_mesh").get("missing") == 0) and (status_kind(dropped, "scene").get("missing") == 0) and dropped.get("matches") is True,
            f"{status_text(dropped)}; bytes before {dropped.get('bytes_before_drop')}"
        )
        status = editor.export(scene)
        check("Export after the drop makes nothing and writes the prepared asset anew",
              (status.get("state") == "done") and (export_counts(status, "local_mesh").get("made") == 0) and (export_counts(status, "scene").get("made") == 0) and
              rewritten(status),
              counts_text(status))
        dropped = editor.status(scene, "prepared_asset")
        check("drop of the prepared asset deletes the file; status: no prepared asset, no match",
              (not os.path.exists(prepared)) and (dropped.get("prepared_asset_exists") is False) and (dropped.get("matches") is False),
              status_text(dropped))
        status = editor.export(scene)
        check("Export after the prepared asset drop writes every item again",
              (status.get("state") == "done") and (export_counts(status, "local_mesh").get("written") == 4) and
              (export_counts(status, "raytrace_bvh").get("written") == 4) and (export_counts(status, "scene").get("written") == 1),
              counts_text(status))
        editor.close(scene)

        # The source is readable: the prepared asset opens the source.
        log_before = len(read_log())
        reply = editor.call("load_scene", {"path": prepared})
        check("load_scene of the prepared asset names the source scene", reply.get("scene_name") == "small", reply.get("scene_name", ""))
        editor.wait_scene("small")
        log_tail = read_log()[log_before:]
        check("with the source in place the prepared asset opens the source",
              ("its source" in log_tail) and ("is readable - opening the source instead" in log_tail) and ("small.pc" not in editor.scene_names()),
              f"scenes {editor.scene_names()}")
        status = editor.export("small")
        check("the source opened through the prepared asset Exports normally", (status.get("state") == "done") and (export_counts(status, "local_mesh").get("made") == 0), counts_text(status))
        source_hit = wait_ray_hit(editor, "small", "floor")
        check("the ray through the torus hole hits the floor in the source scene", source_hit[0] == "floor", json.dumps(source_hit))
        editor.close("small")

        # The source moved away: a prepared run.
        moved_dir = os.path.join(work, "moved")
        os.makedirs(moved_dir, exist_ok=True)
        shutil.move(source, os.path.join(moved_dir, "small.glb"))
        log_before = len(read_log())
        reply = editor.call("load_scene", {"path": prepared})
        check("load_scene of the prepared asset names the prepared run", reply.get("scene_name") == "small.pc", reply.get("scene_name", ""))
        editor.wait_scene("small.pc")
        log_tail = read_log()[log_before:]
        check("with the source moved away the prepared asset opens as a prepared run", "Prepared run of" in log_tail)
        check("the open checks the stamp records alone: logged once, with no per-item stamp line",
              (log_tail.count("stamp records checked in") == 1) and ("item(s) of" not in log_tail) and ("Prepared run: " not in log_tail),
              next((line.strip() for line in log_tail.splitlines() if "stamp records checked in" in line), ""))
        images = render_set(editor, "small.pc", images_dir, "prepared")
        if args.negative_control == "image":
            path = images[("bind", "front")]
            width, height, pixels = decode_png(path)
            rows = [pixels[y * width * 4:(y + 1) * width * 4] for y in range(height)]
            first = bytearray(rows[0])
            first[0:4] = bytes(((first[0] + 1) & 0xff, first[1], first[2], first[3]))
            rows[0] = bytes(first)
            with open(path, "wb") as handle:
                handle.write(png_bytes(width, height, rows))
            print("(negative control: one pixel of the prepared bind/front image altered)")
        for key, path in sorted(images.items()):
            identical, differing, total = compare_images(source_images[key], path)
            check(f"prepared run image {key[0]}/{key[1]} identical to the source scene's", identical, f"{differing}/{total} pixels differ")
        check("the prepared run's stored BVHs answer the ray as the source does", ray_hit(editor, "small.pc") == source_hit,
              json.dumps(ray_hit(editor, "small.pc")))
        refusal = None
        try:
            editor.call("prepare_scene", {"scene_name": "small.pc", "target": "pc"})
        except RuntimeError as error:
            refusal = str(error)
        check("Export of the prepared run is refused", (refusal is not None) and ("prepared run" in refusal), (refusal or "")[:120])
        editor.close("small.pc")
        shutil.move(os.path.join(moved_dir, "small.glb"), source)
    finally:
        editor.stop()
    return source_hit


RECORD_UPDATE = "UPDATE asset SET value = ? WHERE name = ?"


def tweak_store(source_path, destination_path, sql, parameters):
    shutil.copyfile(source_path, destination_path)
    with sqlite3.connect(destination_path) as connection:
        connection.execute(sql, parameters)
        connection.commit()


def expect_refused(editor, path):
    try:
        editor.call("load_scene", {"path": path})
    except RuntimeError as error:
        return str(error)
    return None


def phase_quest_target(args, work, source, source_images, source_hit):
    """Process 3: Export for target quest beside the pc asset; the format stamp checks on the host."""
    source_dir   = os.path.dirname(source)
    pc_prepared  = os.path.join(source_dir, "small.pc.erhe_prepared")
    quest        = os.path.join(source_dir, "small.quest.erhe_prepared")
    editor_asset = os.path.join(source_dir, "small.erhe_editor")
    images_dir   = os.path.join(work, "images")
    pc_before    = read_store(pc_prepared)
    editor = Editor(args.editor)
    try:
        scene = editor.load(source)
        status = editor.export(scene, "quest")
        mesh, scene_counts = export_counts(status, "local_mesh"), export_counts(status, "scene")
        bvh = export_counts(status, "raytrace_bvh")
        check("quest Export is done", status.get("state") == "done", status.get("error", ""))
        check("quest Export makes no mesh or BVH item (the pc ones serve), copies four of each, makes and copies one Scene item",
              (mesh.get("made") == 0) and (mesh.get("present") == 4) and (mesh.get("written") == 4) and
              (bvh.get("made") == 0) and (bvh.get("present") == 4) and (bvh.get("written") == 4) and
              (scene_counts.get("made") == 1) and (scene_counts.get("written") == 1),
              counts_text(status))
        check("quest Export writes <stem>.quest.erhe_prepared beside the source",
              os.path.isfile(quest) and (os.path.normpath(status.get("path", "")) == os.path.normpath(quest)), status.get("path", ""))
        items, records = read_store(quest)
        scene_key = records.get("prepared.scene_key", "")
        check("quest prepared asset records target quest", records.get("prepared.target") == "quest", records.get("prepared.target", ""))
        scene_item = items.get(scene_key)
        check("quest Scene item is of target quest with stamp glb;textures=astc_4x4",
              (scene_item is not None) and (scene_item[1] == "quest") and (scene_item[2] == "glb;textures=astc_4x4"),
              "" if scene_item is None else f"{scene_item[1]} {scene_item[2]}")
        pc_items, pc_records = pc_before
        pc_named  = scene_item_keys(pc_items[pc_records["prepared.scene_key"]][4])
        quest_named = scene_item_keys(scene_item[4]) if scene_item is not None else set()
        check("quest and pc Scene items name the same mesh items (shared: one vertex layout)", quest_named == pc_named,
              f"{len(quest_named)} vs {len(pc_named)}")
        check("quest and pc Scene items have different keys", scene_key != pc_records["prepared.scene_key"])
        check("the pc prepared asset is unchanged by the quest Export", read_store(pc_prepared) == pc_before)
        _, editor_records = read_store(editor_asset)
        check("editor asset records both prepared assets",
              (editor_records.get("prepared_path.pc") == "small.pc.erhe_prepared") and (editor_records.get("prepared_path.quest") == "small.quest.erhe_prepared"),
              json.dumps({k: v for k, v in editor_records.items() if k.startswith("prepared_path")}))
        editor.close(scene)

        # Another directory, without the source: prepared runs.
        run_dir = os.path.join(work, "quest_run")
        os.makedirs(run_dir, exist_ok=True)
        run = os.path.join(run_dir, "small.quest.erhe_prepared")
        shutil.copyfile(quest, run)
        log_before = len(read_log())
        reply = editor.call("load_scene", {"path": run})
        notices = reply.get("format_notices", [])
        check("the quest asset opens on the host as a prepared run with a notice naming the target",
              (reply.get("scene_name") == "small.quest") and any("target quest" in notice for notice in notices), json.dumps(notices))
        editor.wait_scene("small.quest")
        log_tail = read_log()[log_before:]
        check("the prepared run notice names target quest", "(target quest)" in log_tail)
        images = render_set(editor, "small.quest", images_dir, "quest")
        for key, path in sorted(images.items()):
            identical, differing, total = compare_images(source_images[key], path)
            check(f"quest prepared run image {key[0]}/{key[1]} identical to the source scene's", identical, f"{differing}/{total} pixels differ")
        editor.close("small.quest")

        layout_path = os.path.join(run_dir, "layout.quest.erhe_prepared")
        tweak_store(quest, layout_path, RECORD_UPDATE, ("layout=00000000000000000000000000000000", "prepared.mesh_stamp"))
        log_before = len(read_log())
        refusal = expect_refused(editor, layout_path)
        check("a recorded mesh stamp of another vertex layout refuses the load naming the kinds and both stamps",
              (refusal is not None) and ("the local_mesh and world_mesh items" in refusal) and ("layout=00000000000000000000000000000000" in refusal) and
              ("(recorded at Export) where this build has 'layout=" in refusal),
              (refusal or "")[:300])
        print(f"  refusal: {refusal}")

        bvh_path = os.path.join(run_dir, "bvh.quest.erhe_prepared")
        tweak_store(quest, bvh_path, RECORD_UPDATE, ("bvh=other library", "prepared.bvh_stamp"))
        log_before = len(read_log())
        reply = editor.call("load_scene", {"path": bvh_path})
        notices = reply.get("format_notices", [])
        check("a recorded BVH stamp of another library opens with a notice that the BVHs are built in memory",
              any(("the raytrace_bvh items" in notice) and ("bvh=other library" in notice) and ("built in memory" in notice) for notice in notices),
              json.dumps(notices))
        run_name = reply.get("scene_name", "bvh.quest")
        editor.wait_scene(run_name)
        log_tail = read_log()[log_before:]
        bvh_lines = [line for line in log_tail.splitlines() if "Prepared run: " in line and "raytrace_bvh" in line]
        check("the BVH stamp notice is logged", bool(bvh_lines))
        if bvh_lines:
            print(f"  logged: {bvh_lines[0].strip()}")
        check("the load leaves the stored BVHs of the other stamp unused", "stored BVHs are not used" in log_tail)
        hit = wait_ray_hit(editor, run_name, source_hit)
        check("with the BVHs built in memory the ray answers as in the source scene", hit == source_hit, json.dumps(hit))
        editor.close(run_name)

        scene_path = os.path.join(run_dir, "scene.quest.erhe_prepared")
        tweak_store(quest, scene_path, RECORD_UPDATE, ("glb;textures=etc2", "prepared.scene_stamp"))
        refusal = expect_refused(editor, scene_path)
        check("a recorded Scene item stamp this build does not read refuses the load naming the target",
              (refusal is not None) and ("of target quest" in refusal) and ("glb;textures=etc2" in refusal), (refusal or "")[:300])
        print(f"  refusal: {refusal}")

        # The records decide: item stamps are not read at the open.
        items_path = os.path.join(run_dir, "items.quest.erhe_prepared")
        tweak_store(quest, items_path, "UPDATE item SET stamp = ? WHERE kind = ?", ("layout=00000000000000000000000000000000", KIND_LOCAL_MESH))
        reply = editor.call("load_scene", {"path": items_path})
        check("item stamps differing from the records are not read at the open (it runs)", bool(reply.get("scene_name")), json.dumps(reply)[:200])
        editor.wait_scene(reply["scene_name"])
        editor.close(reply["scene_name"])

        # An asset without the stamp records (made before them) is refused, naming the record.
        for name in STAMP_RECORDS:
            missing_path = os.path.join(run_dir, f"missing_{name.split('.')[1]}.quest.erhe_prepared")
            tweak_store(quest, missing_path, "DELETE FROM asset WHERE name = ?", (name,))
            refusal = expect_refused(editor, missing_path)
            check(f"a prepared asset without the {name} record is refused naming it",
                  (refusal is not None) and (f"it has no {name} record" in refusal) and ("export it again" in refusal), (refusal or "")[:300])
    finally:
        editor.stop()


DEFAULT_CAMERAS = [
    ("near", [0.0, 2.2, 3.8], [0.0, 0.4, 0.0]),
    ("high", [-3.0, 4.0, -2.5], [0.0, 0.3, 0.0])
]


def phase_default_scene(args, work):
    """The editor's default scene (brush-made, geometry-normative meshes): every mesh a mesh item."""
    default_dir = os.path.join(work, "default")
    moved_dir   = os.path.join(work, "default_moved")
    images_dir  = os.path.join(work, "images")
    for directory in (default_dir, moved_dir, images_dir):
        os.makedirs(directory, exist_ok=True)
    source   = os.path.join(default_dir, "default_scene.glb")
    prepared = os.path.join(default_dir, "default_scene.pc.erhe_prepared")
    editor = Editor(args.editor)
    try:
        default_scene = "Default Scene"
        check("the editor starts with its default scene", default_scene in editor.scene_names(), json.dumps(editor.scene_names()))
        editor.set_export_settings(default_scene, NO_BAKE)
        refusal = editor.refusal("prepare_scene", {"scene_name": default_scene, "target": "pc"})
        check("Export of a scene without a source file is refused, saying to save it first",
              (refusal is not None) and ("has no source file yet - save it first" in refusal), (refusal or "")[:200])
        editor.save(default_scene, source)
        editor.close(default_scene)
        scene = editor.load(source)
        source_images = {}
        for name, eye, target in DEFAULT_CAMERAS:
            path = os.path.join(images_dir, f"default_source_{name}.png")
            editor.render(scene, path, eye, target)
            source_images[name] = path
        check("the default scene images are not blank", all(distinct_colors(path) > 16 for path in source_images.values()))
        status = editor.export(scene)
        mesh, scene_counts = export_counts(status, "local_mesh"), export_counts(status, "scene")
        check("Export of the default scene is done and makes mesh items",
              (status.get("state") == "done") and (mesh.get("made", 0) > 0) and (mesh.get("written") == mesh.get("made")) and (scene_counts.get("made") == 1),
              counts_text(status))
        check("Export of the default scene writes no primitive inside the Scene item",
              status.get("export", {}).get("inline_primitive_count") == 0, str(status.get("export", {}).get("inline_primitive_count")))
        check_holds_exactly("default scene Export", prepared, status, None)
        check_mesh_forms("default scene Export", prepared)
        items, records = read_store(prepared)
        document = scene_item_document(items[records["prepared.scene_key"]][4])
        text = json.dumps(document)
        node_graphs = document.get("extensions", {}).get("ERHE_node_graphs", {})
        check("the default scene's Scene item carries no ERHE_geometry, brushes or geometry graphs",
              ("ERHE_geometry" not in text) and ("ERHE_brushes" not in text) and
              ("graph_meshes" not in node_graphs) and ("node_bindings" not in node_graphs),
              f"extensionsUsed {document.get('extensionsUsed', [])}")
        again = editor.export(scene)
        check("a second Export of the default scene makes nothing and writes the prepared asset anew",
              (export_counts(again, "local_mesh").get("made") == 0) and (export_counts(again, "scene").get("made") == 0) and rewritten(again),
              counts_text(again))
        editor.close(scene)

        shutil.move(source, os.path.join(moved_dir, "default_scene.glb"))
        reply = editor.call("load_scene", {"path": prepared})
        run = reply.get("scene_name", "")
        check("the default scene's prepared asset opens as a prepared run", run == "default_scene.pc", run)
        editor.wait_scene(run)
        for name, eye, target in DEFAULT_CAMERAS:
            path = os.path.join(images_dir, f"default_prepared_{name}.png")
            editor.render(run, path, eye, target)
            identical, differing, total = compare_images(source_images[name], path)
            check(f"default scene prepared run image {name} identical to the source scene's", identical, f"{differing}/{total} pixels differ")
        editor.close(run)
    finally:
        editor.stop()


def phase_settings(args, work, source, source_images, source_hit):
    """An MCP edit of the scene's export settings (raytrace_bvh off) changes what Export writes; USD is refused."""
    settings_dir = os.path.join(work, "settings")
    moved_dir    = os.path.join(work, "settings_moved")
    images_dir   = os.path.join(work, "images")
    for directory in (settings_dir, moved_dir):
        os.makedirs(directory, exist_ok=True)
    copy     = os.path.join(settings_dir, "small.glb")
    prepared = os.path.join(settings_dir, "small.pc.erhe_prepared")
    shutil.copyfile(source, copy)
    editor = Editor(args.editor)
    try:
        scene = editor.load(copy)
        with_bvh = editor.export(scene)
        items_with = mesh_items(prepared)
        check("Export with raytrace_bvh on: four mesh items and four BVH items (the open stored them)",
              (export_counts(with_bvh, "local_mesh").get("present") == 4) and (len(items_with) == 4) and
              (export_counts(with_bvh, "raytrace_bvh").get("present") == 4) and (len(bvh_items(prepared)) == 4),
              counts_text(with_bvh))
        editor.set_export_settings(scene, {"raytrace_bvh": False})
        check("set_scene_settings merges raytrace_bvh off into the scene's export settings",
              editor.scene_settings(scene).get("export_settings", {}).get("raytrace_bvh") is False)
        without_bvh = editor.export(scene)
        items_without = mesh_items(prepared)
        bvh_counts = export_counts(without_bvh, "raytrace_bvh")
        check("Export with raytrace_bvh off makes no mesh item (shared with raytrace_bvh on), no BVH item and a new Scene item; Copy writes no BVH item",
              (export_counts(without_bvh, "local_mesh").get("made") == 0) and (export_counts(without_bvh, "local_mesh").get("present") == 4) and
              (export_counts(without_bvh, "local_mesh").get("written") == 4) and
              (bvh_counts.get("made", 0) == 0) and (bvh_counts.get("written", 0) == 0) and
              (export_counts(without_bvh, "scene").get("made") == 1) and (set(items_with) == set(items_without)) and
              (without_bvh.get("export", {}).get("scene_key") != with_bvh.get("export", {}).get("scene_key")),
              counts_text(without_bvh))
        check_bvh_items("Export with raytrace_bvh off", prepared, 0)
        check_holds_exactly("Export with raytrace_bvh off", prepared, without_bvh, None)
        again = editor.export(scene)
        check("a second Export with raytrace_bvh off makes nothing and writes the prepared asset anew",
              all(export_counts(again, kind).get("made", 0) == 0 for kind in ("local_mesh", "raytrace_bvh", "scene")) and rewritten(again) and
              (again.get("export", {}).get("scene_key") == without_bvh.get("export", {}).get("scene_key")),
              counts_text(again))
        off_status = editor.status(scene)
        check("status with raytrace_bvh off: matches, asks for no BVH item, the four BVH items unreferenced in the editor asset",
              (off_status.get("matches") is True) and (status_kind(off_status, "raytrace_bvh").get("present", 0) == 0) and
              (status_kind(off_status, "raytrace_bvh").get("missing", 0) == 0) and (status_kind(off_status, "raytrace_bvh").get("unreferenced_count") == 4),
              status_text(off_status))
        editor.close(scene)
        shutil.move(copy, os.path.join(moved_dir, "small.glb"))
        log_before = len(read_log())
        reply = editor.call("load_scene", {"path": prepared})
        run = reply.get("scene_name", "")
        notices = reply.get("format_notices", [])
        check("the prepared run without BVH items has a notice that the BVHs are built in memory",
              any(("the local_mesh and world_mesh items" in notice) and ("raytrace_bvh off" in notice) and ("built in memory" in notice) for notice in notices),
              json.dumps(notices))
        editor.wait_scene(run)
        check("the notice of the BVHs built in memory is logged",
              any(("Prepared run: " in line) and ("raytrace_bvh off" in line) for line in read_log()[log_before:].splitlines()))
        hit = wait_ray_hit(editor, run, source_hit)
        check("raycast in the prepared run without BVH items answers as in the source scene (BVHs built in memory)", hit == source_hit, json.dumps(hit))
        path = os.path.join(images_dir, "settings_no_bvh_bind_front.png")
        editor.call("animation_playback", {"scene_name": run, "animation": "bend", "action": "pause", "time": 0.0})
        editor.wait_idle()
        name, eye, target = CAMERAS[0]
        editor.render(run, path, eye, target)
        identical, differing, total = compare_images(source_images[("bind", name)], path)
        check("the prepared run without stored BVHs renders the source scene's image", (run == "small.pc") and identical,
              f"{run}: {differing}/{total} pixels differ")
        editor.close(run)
        shutil.move(os.path.join(moved_dir, "small.glb"), copy)
        scene = editor.load(copy)
        editor.set_export_settings(scene, {"raytrace_bvh": True})
        back_on = editor.export(scene)
        check("Export with raytrace_bvh on again makes nothing and writes the four BVH items back, with the first Scene key",
              (export_counts(back_on, "local_mesh").get("made") == 0) and (export_counts(back_on, "raytrace_bvh").get("made") == 0) and
              (export_counts(back_on, "raytrace_bvh").get("present") == 4) and (export_counts(back_on, "raytrace_bvh").get("written") == 4) and
              (export_counts(back_on, "scene").get("made") == 0) and
              (back_on.get("export", {}).get("scene_key") == with_bvh.get("export", {}).get("scene_key")),
              counts_text(back_on))
        editor.close(scene)

        usd_path = os.path.join(settings_dir, "usd_scene.usda")
        with open(usd_path, "w") as handle:
            handle.write('#usda 1.0\n\ndef Xform "root"\n{\n}\n')
        # A build without a USD library (ERHE_USD_LIBRARY none) opens no USD scene.
        opened = editor.refusal("open_scene", {"path": usd_path}) is None
        deadline = time.monotonic() + 30.0
        while opened and ("usd_scene.usda" not in editor.scene_names()) and (time.monotonic() < deadline):
            time.sleep(0.1)
        if opened and ("usd_scene.usda" in editor.scene_names()):
            editor.wait_idle()
            refusal = editor.refusal("prepare_scene", {"scene_name": "usd_scene.usda", "target": "pc"})
            check("Export of a USD-backed scene is refused", (refusal is not None) and ("is USD-backed" in refusal), (refusal or "")[:200])
            editor.close("usd_scene.usda")
        else:
            print("(skipped: this build opens no USD scene, so the USD refusal is not checked)", flush=True)
    finally:
        editor.stop()


def wait_for_stage(editor, job_id, stage, store_path=None, store_count=0):
    """Polls until the running job reports stage with progress done > 0 (and, with store_path, the store holds more than store_count items); False when the job ended first."""
    while True:
        status = editor.job(job_id)
        if status.get("state") in ("done", "failed"):
            return False, status
        progress = status.get("progress", {})
        if (progress.get("stage") == stage) and (progress.get("done", 0) > 0):
            if store_path is None:
                return True, status
            try:
                if len(read_store(store_path)[0]) > store_count:
                    return True, status
            except sqlite3.Error:
                pass
        time.sleep(0.002)


def compare_stores(label, path_a, path_b):
    items_a, _ = read_store(path_a)
    items_b, _ = read_store(path_b)
    same_keys = set(items_a.keys()) == set(items_b.keys())
    different = [key for key in items_a if key in items_b and items_a[key] != items_b[key]]
    check(f"{label}: same keys, and per key the same kind, stamp, meta and payload bytes",
          same_keys and not different, f"{len(items_a)} vs {len(items_b)} items, {len(different)} differ")


def write_basis_scene(directory):
    """basis.gltf + basis.bin: a plane and a box with a Basis Universal base color and normal map."""
    builder = Gltf_builder()
    for name in BASIS_IMAGES:
        shutil.copyfile(os.path.join(BASIS_DIR, name), os.path.join(directory, name))
    meshes = [{"name": "plane", "primitives": [builder.primitive(make_plane(2.0), 0)]},
              {"name": "cube",  "primitives": [builder.primitive(make_box(0.5, 0.5, 0.5), 0)]}]
    document = {
        "asset": {"version": "2.0", "generator": "prepared_asset_verify.py"},
        "extensionsUsed": ["KHR_texture_basisu", "KHR_lights_punctual"],
        "extensionsRequired": ["KHR_texture_basisu"],
        "extensions": {"KHR_lights_punctual": {"lights": [{"type": "directional", "intensity": 3.0}]}},
        "scene": 0, "scenes": [{"nodes": [0, 1, 2]}],
        "nodes": [{"name": "plane", "mesh": 0}, {"name": "cube", "mesh": 1, "translation": [0.0, 0.5, 0.0], "rotation": quat_axis_angle((0, 1, 0), 30.0)},
                  {"name": "sun", "rotation": quat_axis_angle((1, 0, 0), -55.0), "extensions": {"KHR_lights_punctual": {"light": 0}}}],
        "meshes": meshes,
        "materials": [{"name": "cornice", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0, "roughnessFactor": 0.7},
                       "normalTexture": {"index": 1}}],
        "textures": [{"sampler": 0, "extensions": {"KHR_texture_basisu": {"source": 0}}},
                     {"sampler": 0, "extensions": {"KHR_texture_basisu": {"source": 1}}}],
        "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 33071}],
        "images": [{"name": "cornice_color", "uri": BASIS_IMAGES[0], "mimeType": "image/ktx2"},
                   {"name": "cornice_normal", "uri": BASIS_IMAGES[1], "mimeType": "image/ktx2"}],
        "accessors": builder.accessors, "bufferViews": builder.views
    }
    binary = builder.padded_binary()
    document["buffers"] = [{"uri": "basis.bin", "byteLength": len(binary)}]
    with open(os.path.join(directory, "basis.bin"), "wb") as handle:
        handle.write(binary)
    path = os.path.join(directory, "basis.gltf")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(document, handle)
    return path


TEXTURE_CAMERAS = [
    ("high", [0.0, 2.4, 2.6], [0.0, 0.3, 0.0]),
    ("low",  [2.2, 0.9, -1.6], [0.0, 0.4, 0.0])
]


def texture_ids(editor, scene):
    return {texture["name"]: texture["id"] for texture in editor.call("get_scene_textures", {"scene_name": scene})["textures"]}


def texture_properties(editor, texture_id):
    """{name: value} of the texture properties of one Texture item (get_item_properties)."""
    names = ("transfer_function", "normal_encoding", "generate_mipmaps", "wrap_u", "wrap_v")
    return {entry["name"]: entry["value"] for entry in editor.call("get_item_properties", {"item_id": texture_id})["properties"] if entry["name"] in names}


def file_texture_properties(path):
    """{image name: entry} of the ERHE_texture_properties of a saved scene (GLB)."""
    document = glb_document(path)
    images = document.get("images", [])
    entries = document.get("extensions", {}).get("ERHE_texture_properties", {}).get("images", [])
    return {images[entry["image"]].get("name", str(entry["image"])): entry for entry in entries}


def render_textures(editor, scene, directory, tag):
    paths = {}
    for name, eye, target in TEXTURE_CAMERAS:
        path = os.path.join(directory, f"{tag}_{name}.png")
        editor.render(scene, path, eye, target)
        paths[name] = path
    return paths


def scene_item_image_buffers(glb):
    """Per image of a Scene item, the uri of the buffer its view lies in ('' for the Scene item's own)."""
    document = scene_item_document(glb)
    result = []
    for image in document.get("images", []):
        view = document["bufferViews"][image["bufferView"]]
        result.append(document["buffers"][view["buffer"]].get("uri", ""))
    return result


def set_texture_property(editor, texture_id, name, value):
    editor.call("set_item_property", {"item_id": texture_id, "property": name, "value": value})
    editor.wait_idle()


def phase_textures(args, work):
    """Texture properties (D8 step 6a) and Texture items for Basis Universal sources (6b)."""
    if not all(os.path.isfile(os.path.join(BASIS_DIR, name)) for name in BASIS_IMAGES):
        print(f"SKIP: texture checks: the Basis Universal images are not in {BASIS_DIR} (the Bistro download)", flush=True)
        return
    foreign_dir = os.path.join(work, "basis_foreign")
    source_dir  = os.path.join(work, "basis_source")
    run_dir     = os.path.join(work, "basis_run")
    images_dir  = os.path.join(work, "basis_images")
    for directory in (foreign_dir, source_dir, run_dir, images_dir):
        os.makedirs(directory, exist_ok=True)
    foreign = write_basis_scene(foreign_dir)
    source  = os.path.join(source_dir, "basis.glb")
    pc_prepared    = os.path.join(source_dir, "basis.pc.erhe_prepared")
    quest_prepared = os.path.join(source_dir, "basis.quest.erhe_prepared")

    editor = Editor(args.editor)
    try:
        # 6a: the import derives the properties from the uses, once.
        scene = editor.open_foreign(foreign)
        ids = texture_ids(editor, scene)
        color, normal = texture_properties(editor, ids.get("cornice_color")), texture_properties(editor, ids.get("cornice_normal"))
        check("import: the base color texture takes sRGB, not a normal map, and the sampler's wrap",
              color == {"transfer_function": "sRGB", "normal_encoding": "Not A Normal Map", "generate_mipmaps": "true", "wrap_u": "Repeat", "wrap_v": "Clamp"},
              json.dumps(color))
        check("import: the normal map takes linear and the image's two component layout",
              (normal.get("transfer_function") == "Linear") and (normal.get("normal_encoding") == "Two Component"), json.dumps(normal))
        editor.set_export_settings(scene, NO_BAKE)
        set_texture_property(editor, ids["cornice_normal"], "generate_mipmaps", "false")
        editor.save(scene, source)
        saved = file_texture_properties(source)
        check("save: ERHE_texture_properties carries both images' properties, the MCP edit included",
              (saved.get("cornice_color", {}).get("transfer_function") == "srgb") and (saved.get("cornice_color", {}).get("wrap_v") == "clamp_to_edge") and
              (saved.get("cornice_normal", {}).get("normal_encoding") == "two_component") and (saved.get("cornice_normal", {}).get("generate_mipmaps") is False),
              json.dumps(saved))
        editor.close(scene)
        scene = editor.load(source)
        ids = texture_ids(editor, scene)
        reloaded = texture_properties(editor, ids["cornice_normal"])
        check("reload: the saved properties are the texture's (generate_mipmaps false kept)",
              (reloaded.get("generate_mipmaps") == "false") and (reloaded.get("normal_encoding") == "Two Component"), json.dumps(reloaded))
        source_images = render_textures(editor, scene, images_dir, "source")

        # 6b: Texture items for pc and quest.
        status = editor.export(scene, "pc")
        textures = export_counts(status, "texture")
        check("pc Export makes and copies two Texture items, no image inside the Scene item",
              (status.get("state") == "done") and (textures.get("made") == 2) and (textures.get("written") == 2) and
              (status.get("export", {}).get("inline_image_count") == 0), counts_text(status))
        items, records = read_store(pc_prepared)
        texture_items = {key: value for key, value in items.items() if value[0] == KIND_TEXTURE}
        metas = [json.loads(value[3]) for value in texture_items.values()]
        check("pc prepared asset holds the two Texture items: target pc, stamp ktx2;blocks=bc, BC7 sRGB and BC5 metas",
              (len(texture_items) == 2) and all((value[1] == "pc") and (value[2] == "ktx2;blocks=bc") for value in texture_items.values()) and
              (sorted(meta.get("format") for meta in metas) == ["format_bc5_unorm", "format_bc7_srgb"]) and
              all((meta.get("mimeType") == "image/ktx2") and (meta.get("byteLength") == len(value[4])) for meta, value in zip(metas, texture_items.values())),
              json.dumps([(value[1], value[2], meta.get("format")) for meta, value in zip(metas, texture_items.values())]))
        scene_item = items[records["prepared.scene_key"]][4]
        buffers = scene_item_image_buffers(scene_item)
        check("the pc Scene item's images are views of the Texture items' erhe-item: buffers",
              (len(buffers) == 2) and all(uri.startswith(ITEM_URI) and (uri[len(ITEM_URI):] in texture_items) for uri in buffers), json.dumps(buffers))
        pc_scene_key = records["prepared.scene_key"]

        status = editor.export(scene, "quest")
        textures = export_counts(status, "texture")
        check("quest Export makes two Texture items of its own and no mesh item",
              (textures.get("made") == 2) and (textures.get("written") == 2) and (export_counts(status, "local_mesh").get("made") == 0), counts_text(status))
        quest_textures = {key: value for key, value in read_store(quest_prepared)[0].items() if value[0] == KIND_TEXTURE}
        check("quest Texture items: target quest, stamp ktx2;blocks=astc_4x4, keys other than pc's",
              (len(quest_textures) == 2) and all((value[1] == "quest") and (value[2] == "ktx2;blocks=astc_4x4") for value in quest_textures.values()) and
              not (set(quest_textures) & set(texture_items)), json.dumps([(value[1], value[2]) for value in quest_textures.values()]))

        # An edit of a property the transcode reads asks for a new item; setting it back finds the first.
        set_texture_property(editor, ids["cornice_color"], "transfer_function", "Linear")
        status = editor.export(scene, "pc")
        check("transfer_function linear: Export makes one Texture item and one Scene item",
              (export_counts(status, "texture").get("made") == 1) and (export_counts(status, "scene").get("made") == 1), counts_text(status))
        set_texture_property(editor, ids["cornice_color"], "transfer_function", "sRGB")
        status = editor.export(scene, "pc")
        check("transfer_function back to sRGB: Export makes nothing and has the first Scene key",
              (export_counts(status, "texture").get("made") == 0) and (export_counts(status, "scene").get("made") == 0) and
              (status.get("export", {}).get("scene_key") == pc_scene_key), counts_text(status))
        prepared_status = editor.status(scene)
        texture_status = status_kind(prepared_status, "texture")
        check("status: both Texture items present, the linear one unreferenced, the prepared asset matches",
              prepared_status.get("matches") and (texture_status.get("present") == 2) and (texture_status.get("missing") == 0) and
              (texture_status.get("unreferenced_count", 0) >= 1), status_text(prepared_status))
        editor.close(scene)
    finally:
        editor.stop()

    editor = Editor(args.editor)
    try:
        scene = editor.load(source)
        status = editor.export(scene, "pc")
        made   = {kind: counts.get("made") for kind, counts in status.get("export", {}).get("counts", {}).items()}
        check("a second Export in a new process makes nothing and writes the prepared asset anew", all(v == 0 for v in made.values()) and rewritten(status), counts_text(status))
        editor.close(scene)

        # Prepared runs with the source away.
        for name in ("basis.pc.erhe_prepared", "basis.quest.erhe_prepared"):
            shutil.copyfile(os.path.join(source_dir, name), os.path.join(run_dir, name))
        log_before = len(read_log())
        run = editor.load(os.path.join(run_dir, "basis.pc.erhe_prepared"))
        log_tail = read_log()[log_before:]
        check("the pc asset opens as the prepared run basis.pc", run == "basis.pc", run)
        transcode_lines = [line for line in log_tail.splitlines() if "images: " in line and "Basis KTX2 transcoded" in line]
        check("the pc prepared run transcodes nothing: the Texture items upload as stored",
              bool(transcode_lines) and ("images: 0 Basis KTX2 transcoded, 2 raw block KTX2 uploaded as stored" in transcode_lines[-1]),
              transcode_lines[-1].strip() if transcode_lines else "no image summary line")
        retained = editor.call("get_memory_usage").get("cpu_images", {}).get("retained_source_bytes")
        check("the pc prepared run retains no source image bytes", retained == 0, f"retained_source_bytes {retained}")
        run_images = render_textures(editor, run, images_dir, "pc_run")
        for key, path in sorted(run_images.items()):
            identical, differing, total = compare_images(source_images[key], path)
            check(f"pc prepared run image {key} identical to the source scene's", identical, f"{differing}/{total} pixels differ")
        editor.close(run)

        run = editor.load(os.path.join(run_dir, "basis.quest.erhe_prepared"))
        quest_images = render_textures(editor, run, images_dir, "quest_run")
        check("the quest asset runs on the host (ASTC decoded) and renders the textured scene",
              all(distinct_colors(path) > 100 for path in quest_images.values()),
              json.dumps({key: distinct_colors(path) for key, path in quest_images.items()}))
        editor.close(run)

        etc2_path = os.path.join(run_dir, "etc2.pc.erhe_prepared")
        tweak_store(os.path.join(run_dir, "basis.pc.erhe_prepared"), etc2_path, RECORD_UPDATE, ("ktx2;blocks=etc2", "prepared.texture_stamps"))
        refusal = expect_refused(editor, etc2_path)
        check("a recorded Texture item stamp this build does not read refuses the load naming the kind and the target",
              (refusal is not None) and ("the texture items" in refusal) and ("ktx2;blocks=etc2" in refusal) and ("of target pc" in refusal), (refusal or "")[:300])
    finally:
        editor.stop()


# --- texture_encoding (D8 step 6c, R7.5) ---------------------------------------

# Prepared run of texture_encoding target against the source scene: the
# images go through UASTC and then BC7 (pc) or ASTC 4x4 (quest), both lossy,
# and the Basis item carries mips made by basisu's filter where the source
# load builds its mips on the GPU. The encoder test bounds level 0 at 8/255
# per channel on a smooth gradient; shading scales a texel error by the
# light (intensity 3, so up to ~3x where a surface faces the sun), so a pixel
# may move by 24/255 and on average by much less.
ENCODED_MAX_CHANNEL_DIFFERENCE = 24
ENCODED_MAX_MEAN_DIFFERENCE    = 1.5


def pattern_rgba(width, height):
    """A smooth gradient with soft 8 texel checks: the base color PNG."""
    rows = []
    for y in range(height):
        row = []
        for x in range(width):
            check_cell = ((x // 8) + (y // 8)) % 2
            row += [40 + (x * 3) % 200, 60 + (y * 2) % 180, 110 + 70 * check_cell, 255]
        rows.append(row)
    return rows


def bump_normal_rgba(width, height):
    """A three component tangent-space normal map of a sine bump field."""
    rows = []
    for y in range(height):
        row = []
        for x in range(width):
            dx = 0.6 * math.cos(2.0 * math.pi * x / width)
            dy = 0.6 * math.cos(2.0 * math.pi * y / height)
            length = math.sqrt(dx * dx + dy * dy + 1.0)
            n = (-dx / length, -dy / length, 1.0 / length)
            row += [int(round((c * 0.5 + 0.5) * 255)) for c in n] + [255]
        rows.append(row)
    return rows


def write_png_scene(directory):
    """png.gltf + png.bin: a plane and a box with a PNG base color and a PNG normal map."""
    builder = Gltf_builder()
    images = {"pattern.png": png_bytes(64, 64, pattern_rgba(64, 64)), "bumps.png": png_bytes(32, 32, bump_normal_rgba(32, 32))}
    for name, data in images.items():
        with open(os.path.join(directory, name), "wb") as handle:
            handle.write(data)
    meshes = [{"name": "plane", "primitives": [builder.primitive(make_plane(2.0), 0)]},
              {"name": "cube",  "primitives": [builder.primitive(make_box(0.5, 0.5, 0.5), 0)]}]
    document = {
        "asset": {"version": "2.0", "generator": "prepared_asset_verify.py"},
        "extensionsUsed": ["KHR_lights_punctual"],
        "extensions": {"KHR_lights_punctual": {"lights": [{"type": "directional", "intensity": 3.0}]}},
        "scene": 0, "scenes": [{"nodes": [0, 1, 2]}],
        "nodes": [{"name": "plane", "mesh": 0}, {"name": "cube", "mesh": 1, "translation": [0.0, 0.5, 0.0], "rotation": quat_axis_angle((0, 1, 0), 30.0)},
                  {"name": "sun", "rotation": quat_axis_angle((1, 0, 0), -55.0), "extensions": {"KHR_lights_punctual": {"light": 0}}}],
        "meshes": meshes,
        "materials": [{"name": "patterned", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0, "roughnessFactor": 0.7},
                       "normalTexture": {"index": 1}}],
        "textures": [{"sampler": 0, "source": 0}, {"sampler": 0, "source": 1}],
        "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
        "images": [{"name": "pattern", "uri": "pattern.png"}, {"name": "bumps", "uri": "bumps.png"}],
        "accessors": builder.accessors, "bufferViews": builder.views
    }
    binary = builder.padded_binary()
    document["buffers"] = [{"uri": "png.bin", "byteLength": len(binary)}]
    with open(os.path.join(directory, "png.bin"), "wb") as handle:
        handle.write(binary)
    path = os.path.join(directory, "png.gltf")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(document, handle)
    return path, images


def image_statistics(path_a, path_b):
    """(differing pixel count, largest channel difference, mean channel difference) on decoded RGB."""
    wa, ha, a = decode_png(path_a)
    wb, hb, b = decode_png(path_b)
    if (wa, ha) != (wb, hb):
        return -1, 255, 255.0
    differing = 0
    largest = 0
    total = 0
    for i in range(0, len(a), 4):
        d = [abs(a[i + c] - b[i + c]) for c in range(3)]
        if any(d):
            differing += 1
            largest = max(largest, max(d))
            total += sum(d)
    return differing, largest, total / (3.0 * wa * ha)


def check_images_within_bound(label, source_images, run_images):
    results = {key: image_statistics(source_images[key], path) for key, path in run_images.items()}
    check(f"{label}: every pixel within {ENCODED_MAX_CHANNEL_DIFFERENCE}/255 and the mean within {ENCODED_MAX_MEAN_DIFFERENCE}/255 of the source scene's (block compression)",
          all((d >= 0) and (m <= ENCODED_MAX_CHANNEL_DIFFERENCE) and (mean <= ENCODED_MAX_MEAN_DIFFERENCE) for d, m, mean in results.values()) and
          any(d > 0 for d, _, _ in results.values()),
          "; ".join(f"{key} {d} px differ, max {m}/255, mean {mean:.3f}/255" for key, (d, m, mean) in sorted(results.items())))


def kind_counts(status):
    return {kind: (counts.get("made"), counts.get("present"), counts.get("written"))
            for kind, counts in status.get("export", {}).get("counts", {}).items()}


def phase_texture_encoding(args, work):
    """texture_encoding (D8 step 6c, R7.5): PNG images through Basis texture items (target) or as they are (source)."""
    foreign_dir = os.path.join(work, "png_foreign")
    source_dir  = os.path.join(work, "png_source")
    moved_dir   = os.path.join(work, "png_moved")
    images_dir  = os.path.join(work, "png_images")
    for directory in (foreign_dir, source_dir, moved_dir, images_dir):
        os.makedirs(directory, exist_ok=True)
    foreign, png_files = write_png_scene(foreign_dir)
    source       = os.path.join(source_dir, "png.glb")
    editor_asset = os.path.join(source_dir, "png.erhe_editor")
    pc_prepared  = os.path.join(source_dir, "png.pc.erhe_prepared")
    quest        = os.path.join(source_dir, "png.quest.erhe_prepared")
    settings     = {"bake_static_to_world_space": False}

    editor = Editor(args.editor)
    try:
        name = editor.open_foreign(foreign)
        editor.set_export_settings(name, settings)
        editor.save(name, source)
        editor.close(name)
        scene = editor.load(source)
        check("texture_encoding: the scene's export settings default to target",
              (editor.scene_settings(scene).get("export_settings") or {}).get("texture_encoding") == "target",
              json.dumps(editor.scene_settings(scene).get("export_settings"))[:200])
        source_images = render_textures(editor, scene, images_dir, "png_source")
        source_retained = editor.call("get_memory_usage").get("cpu_images", {}).get("retained_source_bytes")
        print(f"  source scene retained_source_bytes {source_retained}")

        # target: Basis texture items in the editor asset, block format Texture items in the prepared asset.
        job_id = editor.begin_export(scene, "pc")
        stages = set()
        deadline = time.monotonic() + 600.0
        status = {}
        while time.monotonic() < deadline:
            status = editor.job(job_id)
            stage = status.get("progress", {}).get("stage")
            if stage:
                stages.add(stage)
            if status.get("state") in ("done", "failed"):
                break
            time.sleep(0.01)
        editor.wait_idle()
        basis, textures = export_counts(status, "basis_texture"), export_counts(status, "texture")
        check("target Export makes two Basis texture items and two Texture items, copies the Texture items only, no image inside the Scene item",
              (status.get("state") == "done") and (basis.get("made") == 2) and (basis.get("written", 0) == 0) and
              (textures.get("made") == 2) and (textures.get("written") == 2) and (status.get("export", {}).get("inline_image_count") == 0),
              counts_text(status))
        print(f"  progress stages seen: {sorted(stages)}")
        editor_items, _ = read_store(editor_asset)
        basis_items = {key: value for key, value in editor_items.items() if value[0] == KIND_BASIS_TEXTURE}
        check("the editor asset holds the two Basis texture items: no target, stamp ktx2;basis=uastc_ldr_4x4;basisu=<version>, Basis Universal KTX2 payloads",
              (len(basis_items) == 2) and all((value[1] == "") and value[2].startswith("ktx2;basis=uastc_ldr_4x4;basisu=") and
                                              (value[4][:12] == b"\xabKTX 20\xbb\r\n\x1a\n") and (struct.unpack_from("<I", value[4], 12)[0] == 0)
                                              for value in basis_items.values()),
              json.dumps([(value[1], value[2], len(value[4])) for value in basis_items.values()]))
        items, records = read_store(pc_prepared)
        check("the prepared asset holds no Basis texture item", not any(value[0] == KIND_BASIS_TEXTURE for value in items.values()))
        texture_items = {key: value for key, value in items.items() if value[0] == KIND_TEXTURE}
        metas = {key: json.loads(value[3]) for key, value in texture_items.items()}
        check("the prepared asset holds two Texture items of target pc, stamp ktx2;blocks=bc: BC7 sRGB (base color) and BC7 linear (three component normal map), with mips",
              (len(texture_items) == 2) and all((value[1] == "pc") and (value[2] == "ktx2;blocks=bc") for value in texture_items.values()) and
              (sorted(meta.get("format") for meta in metas.values()) == ["format_bc7_srgb", "format_bc7_unorm"]) and
              all(meta.get("levelCount", 0) > 1 for meta in metas.values()),
              json.dumps([(meta.get("format"), meta.get("width"), meta.get("levelCount")) for meta in metas.values()]))
        target_scene_key = records["prepared.scene_key"]
        buffers = scene_item_image_buffers(items[target_scene_key][4])
        check("the Scene item's images are views of the Texture items' erhe-item: buffers",
              (len(buffers) == 2) and all(uri.startswith(ITEM_URI) and (uri[len(ITEM_URI):] in texture_items) for uri in buffers), json.dumps(buffers))
        target_texture_keys = set(texture_items)
        prepared_status = editor.status(scene)
        check("status after the target Export: the Basis texture items present, the prepared asset matches (they are not compared with it)",
              prepared_status.get("matches") is True and (status_kind(prepared_status, "basis_texture").get("present") == 2) and
              (status_kind(prepared_status, "basis_texture").get("missing") == 0) and (status_kind(prepared_status, "texture").get("present") == 2),
              status_text(prepared_status))

        # source: the Texture items hold the PNG bytes; the Basis items stay.
        editor.set_export_settings(scene, {"texture_encoding": "source"})
        status = editor.export(scene, "pc")
        basis, textures = export_counts(status, "basis_texture"), export_counts(status, "texture")
        check("source Export makes two Texture items and no Basis texture item, writes the two",
              (status.get("state") == "done") and (basis.get("made", 0) == 0) and (textures.get("made") == 2) and (textures.get("written") == 2) and
              (status.get("export", {}).get("inline_image_count") == 0),
              counts_text(status))
        items, records = read_store(pc_prepared)
        source_textures = {key: value for key, value in items.items() if value[0] == KIND_TEXTURE}
        check("source Texture items: no target (shared by the targets), stamp source, the exact PNG bytes, image/png metas",
              (len(source_textures) == 2) and all((value[1] == "") and (value[2] == "source") and (json.loads(value[3]).get("mimeType") == "image/png")
                                                  for value in source_textures.values()) and
              (sorted(bytes(value[4]) for value in source_textures.values()) == sorted(png_files.values())),
              json.dumps([(value[1], value[2], len(value[4])) for value in source_textures.values()]))
        check("changing texture_encoding changes the Texture item keys",
              not (set(source_textures) & target_texture_keys), f"{sorted(source_textures)} vs {sorted(target_texture_keys)}")
        check("changing texture_encoding leaves the Basis texture items in the editor asset",
              {key for key, value in read_store(editor_asset)[0].items() if value[0] == KIND_BASIS_TEXTURE} == set(basis_items))

        # target again: everything is present (R9.2), the first Scene key.
        editor.set_export_settings(scene, {"texture_encoding": "target"})
        status = editor.export(scene, "pc")
        basis, textures = export_counts(status, "basis_texture"), export_counts(status, "texture")
        check("target again: no item made, the Texture items present and written back, the first Scene key",
              (textures.get("made") == 0) and (textures.get("present") == 2) and (textures.get("written") == 2) and
              (basis.get("made", 0) == 0) and (export_counts(status, "scene").get("made") == 0) and
              (status.get("export", {}).get("scene_key") == target_scene_key),
              counts_text(status))

        # quest: ASTC 4x4 Texture items from the same Basis texture items.
        status = editor.export(scene, "quest")
        basis, textures = export_counts(status, "basis_texture"), export_counts(status, "texture")
        quest_textures = {key: value for key, value in read_store(quest)[0].items() if value[0] == KIND_TEXTURE}
        check("quest target Export: two ASTC 4x4 Texture items of its own, the pc Basis texture items serve (none encoded)",
              (textures.get("made") == 2) and (basis.get("made", 0) == 0) and (len(quest_textures) == 2) and
              all((value[1] == "quest") and (value[2] == "ktx2;blocks=astc_4x4") for value in quest_textures.values()) and
              not (set(quest_textures) & target_texture_keys),
              counts_text(status))
        editor.save(scene, source)
        editor.close(scene)
    finally:
        editor.stop()

    editor = Editor(args.editor)
    try:
        scene = editor.load(source)
        status = editor.export(scene, "pc")
        check("a second target Export in a new process makes nothing and writes the prepared asset anew",
              (status.get("state") == "done") and all(made == 0 for made, _, _ in kind_counts(status).values()) and rewritten(status),
              counts_text(status))
        editor.close(scene)

        shutil.copyfile(pc_prepared, os.path.join(moved_dir, "png.pc.erhe_prepared"))
        shutil.copyfile(quest, os.path.join(moved_dir, "png.quest.erhe_prepared"))
        log_before = len(read_log())
        run = editor.load(os.path.join(moved_dir, "png.pc.erhe_prepared"))
        log_tail = read_log()[log_before:]
        transcode_lines = [line for line in log_tail.splitlines() if "images: " in line and "Basis KTX2 transcoded" in line]
        check("the target prepared run uploads the two BC7 Texture items as stored",
              bool(transcode_lines) and ("images: 0 Basis KTX2 transcoded, 2 raw block KTX2 uploaded as stored" in transcode_lines[-1]),
              transcode_lines[-1].strip() if transcode_lines else "no image summary line")
        retained = editor.call("get_memory_usage").get("cpu_images", {}).get("retained_source_bytes")
        check("the target prepared run retains no source image bytes", retained == 0, f"retained_source_bytes {retained}")
        check_images_within_bound("target pc prepared run", source_images, render_textures(editor, run, images_dir, "png_pc_run"))
        editor.close(run)
        run = editor.load(os.path.join(moved_dir, "png.quest.erhe_prepared"))
        check_images_within_bound("target quest prepared run on the host (ASTC decoded)", source_images, render_textures(editor, run, images_dir, "png_quest_run"))
        editor.close(run)

        scene = editor.load(source)
        editor.set_export_settings(scene, {"texture_encoding": "source"})
        status = editor.export(scene, "pc")
        check("source Export in a new process makes nothing (its Texture items are present)",
              (status.get("state") == "done") and all(made == 0 for made, _, _ in kind_counts(status).values()), counts_text(status))
        editor.close(scene)
        shutil.copyfile(pc_prepared, os.path.join(moved_dir, "png.pc.erhe_prepared"))
        run = editor.load(os.path.join(moved_dir, "png.pc.erhe_prepared"))
        retained = editor.call("get_memory_usage").get("cpu_images", {}).get("retained_source_bytes")
        check("the source prepared run retains no PNG bytes (the images are read from their Texture items)", retained == 0, f"retained_source_bytes {retained}")
        for key, path in sorted(render_textures(editor, run, images_dir, "png_source_run").items()):
            identical, differing, total = compare_images(source_images[key], path)
            check(f"source prepared run image {key} identical to the source scene's", identical, f"{differing}/{total} pixels differ")
        editor.close(run)
    finally:
        editor.stop()


def scene_identities(editor, scene):
    """name -> get_scene_nodes entry of every content prim but the session-only default camera, the animation paused at 0.
    The other prims (bone proxies, library scopes) are editor furniture that is never saved."""
    editor.call("animation_playback", {"scene_name": scene, "animation": "bend", "action": "pause", "time": 0.0})
    editor.wait_idle()
    nodes = {}
    for node in editor.call("get_scene_nodes", {"scene_name": scene})["nodes"]:
        if (not node.get("content", False)) or ((node["type"] == "Camera") and (node["name"] == "Camera")):
            continue
        if node["name"] in nodes:
            raise RuntimeError(f"node name {node['name']!r} is not unique in {scene}")
        nodes[node["name"]] = node
    return nodes


def identity_differences(expected, actual, names):
    """The names whose uid or world transform (hexfloat) differ, or that are missing."""
    differences = []
    for name in names:
        if name not in actual:
            differences.append(f"{name}: missing")
        elif actual[name].get("gltf_uid") != expected[name].get("gltf_uid"):
            differences.append(f"{name}: uid {expected[name].get('gltf_uid')} -> {actual[name].get('gltf_uid')}")
        elif actual[name].get("world_from_node_hex") != expected[name].get("world_from_node_hex"):
            differences.append(f"{name}: world transform")
    return differences


def phase_identities(args, work):
    """R6.1 and R6.2: uids and world transforms across sessions, saves with and without a transformed import root."""
    foreign_dir = os.path.join(work, "identities_foreign")
    plain_dir   = os.path.join(work, "identities_plain")
    moved_dir   = os.path.join(work, "identities_moved")
    for directory in (foreign_dir, plain_dir, moved_dir):
        os.makedirs(directory, exist_ok=True)
    foreign = write_small_scene(foreign_dir)
    plain   = os.path.join(plain_dir, "small.glb")
    moved   = os.path.join(moved_dir, "small.glb")
    root    = os.path.basename(foreign)

    editor = Editor(args.editor)
    try:
        scene = editor.open_foreign(foreign)
        first = scene_identities(editor, scene)
        check("identities: every node of an imported glTF has a uid, all distinct",
              (len(first) > 0) and all(len(node.get("gltf_uid", "")) == 16 for node in first.values()) and
              (len({node["gltf_uid"] for node in first.values()}) == len(first)) and first.get(root, {}).get("import_root", False),
              f"{len(first)} nodes")
        editor.set_export_settings(scene, NO_BAKE)
        first_export = editor.export(scene)
        editor.save(scene, plain)
        editor.call("set_node_transform", {"scene_name": scene, "node_name": root, "space": "local",
                                           "translation": [0.75, -0.25, 1.5], "rotation_xyzw": list(quat_axis_angle((0, 1, 0), 35.0)),
                                           "scale": [1.5, 1.5, 1.5]})
        editor.wait_idle()
        moved_first = scene_identities(editor, scene)
        editor.save(scene, moved)
        editor.close(scene)
    finally:
        editor.stop()

    plain_root_written = any(node.get("name") == root for node in glb_document(plain).get("nodes", []))
    moved_root_written = any(node.get("name") == root for node in glb_document(moved).get("nodes", []))
    check("identities: the save writes the import root on the identity path as no node, a transformed one as a node",
          (not plain_root_written) and moved_root_written,
          f"identity save {'writes' if plain_root_written else 'omits'} it, transformed save {'writes' if moved_root_written else 'omits'} it")

    editor = Editor(args.editor)
    try:
        scene = editor.open_foreign(foreign)
        second = scene_identities(editor, scene)
        differences = identity_differences(first, second, first.keys()) + [f"{name}: extra" for name in second.keys() - first.keys()]
        check("identities: a second import of the glTF in a new process has the same uids and bit-identical world transforms",
              not differences, "; ".join(differences[:4]) or f"{len(second)} nodes")
        editor.set_export_settings(scene, NO_BAKE)
        second_export = editor.export(scene)
        scene_counts = export_counts(second_export, "scene")
        check("identities: an Export of the unchanged imported scene in a new process has the first Scene key and makes nothing",
              (second_export.get("export", {}).get("scene_key") == first_export.get("export", {}).get("scene_key")) and
              (scene_counts.get("made") == 0) and (export_counts(second_export, "local_mesh").get("made") == 0),
              counts_text(second_export))
        editor.close(scene)

        scene = editor.load(plain)
        reopened = scene_identities(editor, scene)
        names = [name for name in first if name != root]
        differences = identity_differences(first, reopened, names) + [f"{name}: extra" for name in reopened.keys() - set(names)]
        check("identities: the save without a transformed import root, reopened, has the same uids and bit-identical world transforms",
              not differences, "; ".join(differences[:4]) or f"{len(reopened)} nodes")
        editor.close(scene)

        scene = editor.load(moved)
        reopened = scene_identities(editor, scene)
        differences = identity_differences(moved_first, reopened, moved_first.keys()) + [f"{name}: extra" for name in reopened.keys() - moved_first.keys()]
        check("identities: the save with a transformed import root, reopened, has the root as a plain node, the same uids and bit-identical world transforms",
              (not differences) and (root in reopened) and (not reopened[root].get("import_root", True)),
              "; ".join(differences[:4]) or f"{len(reopened)} nodes")
        editor.close(scene)
    finally:
        editor.stop()


def phase_kill(args, work):
    """Kill an Export during Ensure and during Copy; the rerun ends like an uninterrupted Export."""
    foreign_dir = os.path.join(work, "many_foreign")
    kill_dir    = os.path.join(work, "many_kill")
    ref_dir     = os.path.join(work, "many_reference")
    for directory in (foreign_dir, kill_dir, ref_dir):
        os.makedirs(directory, exist_ok=True)
    foreign = write_many_scene(foreign_dir, args.mesh_count)
    kill_source = os.path.join(kill_dir, "many.glb")
    ref_source  = os.path.join(ref_dir, "many.glb")

    # The open stores the items it derives (R8.3), so Ensure has mesh items to make only
    # for an LOD request the open did not ask: every load here sets one (Export's keys change).
    def load(editor, path):
        scene = editor.load(path)
        editor.set_export_settings(scene, {"lod": KILL_LOD})
        return scene

    with open(os.path.join(REPO_ROOT, "config", "editor", "editor_settings.json")) as handle:
        KILL_LOD = dict(json.load(handle).get("lod", {}))
    KILL_LOD["level_count"] = 3

    editor = Editor(args.editor)
    try:
        name = editor.open_foreign(foreign)
        editor.set_export_settings(name, NO_BAKE)
        editor.save(name, kill_source)
        editor.close(name)
        shutil.copyfile(kill_source, ref_source)
        scene = load(editor, ref_source)
        reference = editor.export(scene)
        check("uninterrupted Export of 'many'", (reference.get("state") == "done") and (export_counts(reference, "local_mesh").get("made") == args.mesh_count),
              f"{counts_text(reference)} ensure {reference.get('export', {}).get('ensure_ms', 0):.0f} ms copy {reference.get('export', {}).get('copy_ms', 0):.0f} ms")
        editor.close(scene)
    finally:
        editor.stop()

    editor_asset = os.path.join(kill_dir, "many.erhe_editor")
    prepared     = os.path.join(kill_dir, "many.pc.erhe_prepared")

    # Killed while making mesh items.
    editor = Editor(args.editor)
    scene = load(editor, kill_source)
    stored = len(read_store(editor_asset)[0]) if os.path.isfile(editor_asset) else 0
    hit, status = wait_for_stage(editor, editor.begin_export(scene), "making mesh items", editor_asset, stored)
    editor.kill()
    made = (len(read_store(editor_asset)[0]) if os.path.isfile(editor_asset) else 0) - stored
    check("Export killed during Ensure (making mesh items) with some items made",
          hit and (0 < made < (2 * args.mesh_count)) and not os.path.isfile(prepared),
          f"{made}/{2 * args.mesh_count} items made beside the {stored} the open stored at the kill; progress {json.dumps(status.get('progress', {}))}")

    # Killed while copying, with no prepared asset yet and then over the one a finished Export wrote.
    def temporaries():
        prefix = os.path.basename(prepared) + ".tmp-"
        return sorted(name for name in os.listdir(kill_dir) if name.startswith(prefix) and not name.endswith("-journal"))

    def file_bytes(path):
        with open(path, "rb") as handle:
            return handle.read()

    editor = Editor(args.editor)
    scene = load(editor, kill_source)
    hit, status = wait_for_stage(editor, editor.begin_export(scene), "copying items")
    editor.kill()
    left = temporaries()
    check("Export killed during Copy leaves no prepared asset and one temporary file beside it",
          hit and not os.path.exists(prepared) and (len(left) == 1),
          f"progress {json.dumps(status.get('progress', {}))}; temporaries {left}")

    editor = Editor(args.editor)
    try:
        mark = len(read_log())
        scene = load(editor, kill_source)
        rerun = editor.export(scene)
        mesh, scene_counts = export_counts(rerun, "local_mesh"), export_counts(rerun, "scene")
        check("Export after the killed Copy makes nothing and writes every item",
              (rerun.get("state") == "done") and (mesh.get("made") == 0) and (mesh.get("written") == args.mesh_count) and
              (scene_counts.get("made") == 0) and (scene_counts.get("written") == 1) and rewritten(rerun),
              counts_text(rerun))
        check("the Export after the kill removes the temporary file the killed Copy left, logged, and leaves none",
              (temporaries() == []) and ("left by an Export that did not finish" in read_log()[mark:]), json.dumps(temporaries()))
        check("Export after the kills has the uninterrupted Export's Scene key",
              rerun.get("export", {}).get("scene_key") == reference.get("export", {}).get("scene_key"))
        editor.close(scene)
    finally:
        editor.stop()

    before = file_bytes(prepared)
    editor = Editor(args.editor)
    scene = load(editor, kill_source)
    hit, status = wait_for_stage(editor, editor.begin_export(scene), "copying items")
    editor.kill()
    left = temporaries()
    check("Export killed during Copy over a prepared asset leaves it byte-identical, with at most one temporary file",
          hit and (file_bytes(prepared) == before) and (len(left) <= 1),
          f"progress {json.dumps(status.get('progress', {}))}; temporaries {left}")

    editor = Editor(args.editor)
    try:
        scene = load(editor, kill_source)
        rerun = editor.export(scene)
        check("Export after the second killed Copy is done and leaves no temporary file",
              (rerun.get("state") == "done") and rewritten(rerun) and (temporaries() == []), counts_text(rerun))
        editor.close(scene)
    finally:
        editor.stop()
    compare_stores("editor asset after the kills vs uninterrupted", editor_asset, os.path.join(ref_dir, "many.erhe_editor"))
    compare_stores("prepared asset after the kills vs uninterrupted", prepared, os.path.join(ref_dir, "many.pc.erhe_prepared"))
    killed_records, reference_records = read_store(prepared)[1], read_store(os.path.join(ref_dir, "many.pc.erhe_prepared"))[1]
    check("the prepared asset after the kills has the uninterrupted Export's Scene key and stamp records",
          all(killed_records.get(name) == reference_records.get(name) for name in ("prepared.scene_key",) + STAMP_RECORDS) and
          all(killed_records.get(name) for name in ("prepared.scene_key", "prepared.mesh_stamp", "prepared.bvh_stamp", "prepared.scene_stamp")),
          json.dumps({name: killed_records.get(name) for name in STAMP_RECORDS}))
    check("the prepared asset after the kills is one file at rest", not any(os.path.exists(prepared + suffix) for suffix in ("-wal", "-shm", "-journal")))

# --- World-space meshes (doc/editor/world_space_bake.md "Verification") ------------

WORLD_NODES = ("floor", "mirror", "under_mirror", "parent_mesh", "child_mesh", "held_mesh", "shared_a", "shared_b", "shared_c")
WORLD_CAMERAS = [
    ("front", [0.5, 3.0, 7.0], [0.0, 0.6, 0.0]),
    ("side",  [-6.0, 2.5, -3.5], [0.0, 0.8, -0.5])
]
# Rounding of world-space vertices: shading differs by a few levels at single
# pixels, coverage at single edge pixels by more; the bound is a share of the image.
WORLD_MAX_DIFFERING_SHARE = 0.005


def write_world_scene(directory, name, children_first):
    """The bake's hand-written scene: a mirrored node, a mirrored parent, a baked mesh with a baked
    child and a non-mesh child holding a baked mesh, and three nodes sharing one mesh; with
    children_first the node array lists every child before its parent."""
    builder = Gltf_builder()
    meshes = [
        {"name": "floor_mesh",  "primitives": [builder.primitive(make_plane(4.0), 0)]},
        {"name": "mirror_mesh", "primitives": [builder.primitive(make_box(0.4, 0.3, 0.5), 1)]},
        {"name": "torus_mesh",  "primitives": [builder.primitive(make_torus(0.5, 0.18, 40, 20), 2)]},
        {"name": "parent_box",  "primitives": [builder.primitive(make_box(0.3, 0.3, 0.3), 0)]},
        {"name": "child_torus", "primitives": [builder.primitive(make_torus(0.25, 0.08, 32, 16, wobble=0.1), 1)]},
        {"name": "held_box",    "primitives": [builder.primitive(make_box(0.2, 0.25, 0.2), 2)]},
        {"name": "shared_torus", "primitives": [builder.primitive(make_torus(0.3, 0.1, 32, 16, wobble=0.2, phase=0.5), 0)]}
    ]
    # Parents first; children_first reverses the array and remaps the indices.
    nodes = [
        {"name": "floor",         "mesh": 0},
        {"name": "mirror",        "mesh": 1, "translation": [-2.0, 0.6, 1.0], "rotation": quat_axis_angle((0, 1, 0), 20.0), "scale": [-1.5, 1.0, 0.7]},
        {"name": "mirror_parent", "translation": [2.0, 1.4, 0.5], "scale": [1.0, -1.2, 1.0], "children": [3]},
        {"name": "under_mirror",  "mesh": 2, "translation": [0.0, 0.2, 0.0], "rotation": quat_axis_angle((1, 0, 0), 30.0)},
        {"name": "parent_mesh",   "mesh": 3, "translation": [0.0, 0.4, -1.5], "rotation": quat_axis_angle((0, 1, 0), 25.0), "children": [5, 6]},
        {"name": "child_mesh",    "mesh": 4, "translation": [0.0, 0.8, 0.0]},
        {"name": "holder",        "translation": [0.9, 0.0, 0.0], "children": [7]},
        {"name": "held_mesh",     "mesh": 5, "translation": [0.0, 0.5, 0.0], "scale": [1.3, 1.0, 1.0]},
        {"name": "shared_a",      "mesh": 6, "translation": [-1.0, 0.3, 2.2]},
        {"name": "shared_b",      "mesh": 6, "translation": [0.0, 0.3, 2.4], "rotation": quat_axis_angle((0, 0, 1), 15.0)},
        {"name": "shared_c",      "mesh": 6, "translation": [1.0, 0.45, 2.2], "scale": [0.8, 0.8, 0.8]},
        {"name": "sun",           "rotation": quat_axis_angle((1, 0, 0), -55.0), "extensions": {"KHR_lights_punctual": {"light": 0}}}
    ]
    roots = [0, 1, 2, 4, 8, 9, 10, 11]
    if children_first:
        count = len(nodes)
        remap = {i: count - 1 - i for i in range(count)}
        reordered = [None] * count
        for i, node in enumerate(nodes):
            node = dict(node)
            if "children" in node:
                node["children"] = [remap[c] for c in node["children"]]
            reordered[remap[i]] = node
        nodes = reordered
        roots = sorted(remap[r] for r in roots)
    document = {
        "asset": {"version": "2.0", "generator": "prepared_asset_verify.py"},
        "extensionsUsed": ["KHR_lights_punctual"],
        "extensions": {"KHR_lights_punctual": {"lights": [{"type": "directional", "intensity": 3.0, "color": [1.0, 0.95, 0.9]}]}},
        "scene": 0, "scenes": [{"nodes": roots}],
        "nodes": nodes, "meshes": meshes,
        "materials": [
            {"name": "grey",  "pbrMetallicRoughness": {"baseColorFactor": [0.7, 0.7, 0.68, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.7}},
            {"name": "red",   "pbrMetallicRoughness": {"baseColorFactor": [0.8, 0.2, 0.1, 1.0], "metallicFactor": 0.1, "roughnessFactor": 0.5}},
            {"name": "green", "pbrMetallicRoughness": {"baseColorFactor": [0.2, 0.7, 0.3, 1.0], "metallicFactor": 0.3, "roughnessFactor": 0.4}}
        ],
        "accessors": builder.accessors, "bufferViews": builder.views
    }
    binary = builder.padded_binary()
    document["buffers"] = [{"uri": f"{name}.bin", "byteLength": len(binary)}]
    with open(os.path.join(directory, f"{name}.bin"), "wb") as handle:
        handle.write(binary)
    path = os.path.join(directory, f"{name}.gltf")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(document, handle)
    return path


def image_difference(path_a, path_b):
    """(differing pixel count, largest channel difference) on decoded pixels; (-1, 255) for other sizes."""
    wa, ha, a = decode_png(path_a)
    wb, hb, b = decode_png(path_b)
    if (wa, ha) != (wb, hb):
        return -1, 255
    differing = 0
    largest = 0
    for i in range(0, len(a), 4):
        if a[i:i + 4] != b[i:i + 4]:
            differing += 1
            largest = max(largest, max(abs(a[i + c] - b[i + c]) for c in range(4)))
    return differing, largest


def render_world(editor, scene, directory, tag, mesh_form="source"):
    paths = {}
    for name, eye, target in WORLD_CAMERAS:
        path = os.path.join(directory, f"{tag}_{name}.png")
        camera = {"eye": eye, "target": target, "fov_y_degrees": 50.0, "near": 0.05, "far": 200.0}
        editor.call("render_scene_image", {"scene": scene, "camera": camera, "width": 480, "height": 270,
                                           "output": "png", "path": path, "mesh_form": mesh_form})
        paths[name] = path
    return paths


def check_capture_lod_levels(editor, scene, images_dir):
    """render_scene_image lod_nodes, LOD on: the levels of the rendered frame, its shadow pass drawing each caster at its color level."""
    editor.call("set_graphics_preset", {"lod_enabled": True})
    try:
        name, eye, target = WORLD_CAMERAS[0]
        camera = {"eye": eye, "target": target, "fov_y_degrees": 50.0, "near": 0.05, "far": 200.0}
        reply = editor.call("render_scene_image", {"scene": scene, "camera": camera, "width": 480, "height": 270, "output": "png",
                                                   "path": os.path.join(images_dir, "lod_levels.png"), "lod_nodes": ["shared_a", "shared_b", "shared_c"]})
    finally:
        editor.call("set_graphics_preset", {"lod_enabled": False})
    selection = reply.get("lod_selection", {})
    columns = selection.get("columns", [])
    # The capture's color column is the one without history; its shadow column samples it.
    fresh = [c["column"] for c in columns if (c["purpose"] == "color") and (c["previous_column"] is None)]
    capture_color = fresh[0] if len(fresh) == 1 else None
    capture_shadow = [c["column"] for c in columns if (c["purpose"] == "shadow") and (capture_color is not None) and ((c["color_column_mask"] >> capture_color) & 1)]
    entries = selection.get("entries", [])
    color_levels  = {e["node_name"]: e["levels"][capture_color] for e in entries if (e["purpose"] == "color") and (capture_color is not None)}
    shadow_levels = {e["node_name"]: [e["levels"][c] for c in capture_shadow] for e in entries if e["purpose"] == "shadow"}
    check("world: render_scene_image lod_nodes reports the rendered frame's levels; its shadow pass draws each caster at the capture's color level",
          (capture_color is not None) and (len(capture_shadow) > 0) and (len(color_levels) == 3) and
          all(levels == [color_levels[node]] * len(levels) for node, levels in shadow_levels.items()) and (len(shadow_levels) == 3),
          json.dumps({"frame": selection.get("frame_index"), "color": color_levels, "shadow": shadow_levels}))


def check_images_close(label, images_a, images_b):
    results = {name: image_difference(images_a[name], images_b[name]) for name in images_a}
    pixel_count = 480 * 270
    check(f"{label}: at most {WORLD_MAX_DIFFERING_SHARE * 100:.1f}% of the pixels differ (vertex rounding)",
          all((d >= 0) and (d <= (WORLD_MAX_DIFFERING_SHARE * pixel_count)) for d, _ in results.values()),
          "; ".join(f"{name} {d} px differ, max {m}/255" for name, (d, m) in results.items()))


def world_status(editor, scene):
    return editor.call("get_world_space_meshes", {"scene_name": scene})


def bake_world(editor, scene):
    job_id = editor.call("bake_world_space_meshes", {"scene_name": scene})["job_id"]
    status = editor.finish_export(job_id)
    editor.wait_idle()
    return status


def node_tree(editor, scene):
    """name -> (parent, local hexfloat transform) of the scene's content nodes."""
    tree = {}
    for node in editor.call("get_scene_nodes", {"scene_name": scene})["nodes"]:
        if node.get("content", False) and not node.get("render_proxy", False):
            tree[node["name"]] = (node.get("parent"), node.get("world_from_node_hex"))
    return tree


def scene_item_world_space(glb):
    """The children names of the Scene item's World Space node, and the placeholder names with their children."""
    document = scene_item_document(glb)
    nodes = document.get("nodes", [])
    world = [node for node in nodes if node.get("name") == "World Space"]
    children = [nodes[c].get("name") for c in world[0].get("children", [])] if world else []
    placeholders = {node.get("name"): [nodes[c].get("name") for c in node.get("children", [])]
                    for node in nodes if ("mesh" not in node) and node.get("children") and (node.get("name") != "World Space")}
    return children, placeholders


def world_mesh_buffers_ok(glb, items):
    """Every mesh primitive under the World Space node reads a World-space mesh item."""
    document = scene_item_document(glb)
    nodes = document.get("nodes", [])
    world = [node for node in nodes if node.get("name") == "World Space"]
    if not world:
        return False
    for c in world[0].get("children", []):
        node = nodes[c]
        if "mesh" not in node:
            return False
        for primitive in document["meshes"][node["mesh"]]["primitives"]:
            accessor = document["accessors"][primitive["attributes"]["POSITION"]]
            uri = document["buffers"][document["bufferViews"][accessor["bufferView"]]["buffer"]].get("uri", "")
            key = uri[len(ITEM_URI):] if uri.startswith(ITEM_URI) else ""
            if (key not in items) or (items[key][0] != KIND_WORLD_MESH):
                return False
    return True


def phase_world_space(args, work):
    """D8 step 8: Bake World-Space Meshes, the two forms, the Scene item's World Space node."""
    foreign_dir = os.path.join(work, "world_foreign")
    source_dir  = os.path.join(work, "world_source")
    away_dir    = os.path.join(work, "world_away")
    images_dir  = os.path.join(work, "world_images")
    for directory in (foreign_dir, source_dir, away_dir, images_dir):
        os.makedirs(directory, exist_ok=True)
    source = os.path.join(source_dir, "world.glb")
    prepared = os.path.join(source_dir, "world.pc.erhe_prepared")
    editor_asset = os.path.join(source_dir, "world.erhe_editor")
    editor = Editor(args.editor)
    try:
        name = editor.open_foreign(write_world_scene(foreign_dir, "world", False))
        editor.save(name, source)
        editor.close(name)
        scene = editor.load(source)
        editor.set_export_settings(scene, {})
        settings = editor.scene_settings(scene).get("export_settings") or {}
        check("world: the default instancing of added export settings is duplicate_within_budget (R4.6)",
              (settings.get("instancing") == "duplicate_within_budget") and (settings.get("bake_static_to_world_space") is True),
              json.dumps({k: settings.get(k) for k in ("instancing", "bake_static_to_world_space")}))
        editor.save(scene, source)
        tree_before = node_tree(editor, scene)
        source_images = render_world(editor, scene, images_dir, "source")
        check("world: no world-space form before a bake", not world_status(editor, scene).get("baked"))
        check_capture_lod_levels(editor, scene, images_dir)
        refusal = editor.refusal("render_scene_image", {"scene": scene, "camera": {"eye": [0, 1, 5], "target": [0, 0, 0]}, "width": 64, "height": 64,
                                                         "output": "png", "path": os.path.join(images_dir, "refused.png"), "mesh_form": "world_space"})
        check("world: render_scene_image refuses the world-space form before a bake", (refusal is not None) and ("not available" in refusal), (refusal or "")[:160])

        bake = bake_world(editor, scene)
        worlds = export_counts(bake, "world_mesh")
        bvhs = export_counts(bake, "raytrace_bvh")
        check("world: Bake World-Space Meshes makes nine World-space mesh items and their BVH items",
              (bake.get("state") == "done") and (worlds.get("made") == 9) and (worlds.get("present") == 0) and (bvhs.get("made") == 9) and
              (bake.get("export", {}).get("world_mesh_node_count") == 9) and (bake.get("export", {}).get("world_form_mesh_count") == 9),
              counts_text(bake) + f" nodes {bake.get('export', {}).get('world_mesh_node_count')} form {bake.get('export', {}).get('world_form_mesh_count')}")
        status = world_status(editor, scene)
        check("world: after the bake every admitted node's world-space mesh is valid and the world-space form is available",
              status.get("baked") and (status.get("admitted_count") == 9) and (status.get("valid_count") == 9) and status.get("available"), json.dumps(status))
        check("world: the bake leaves the node tree as authored", node_tree(editor, scene) == tree_before,
              f"{len(tree_before)} content nodes")
        items, _ = read_store(editor_asset)
        world_items = {key: value for key, value in items.items() if value[0] == KIND_WORLD_MESH}
        named = set()
        for value in world_items.values():
            for record in json.loads(value[5] or "{}").get("nodes", []):
                if record.get("path") and (len(record.get("uids", [])) == len(record["path"])):
                    named.add(record["path"][-1])
        check("world: each World-space mesh item records its node's path of names and chain of uids (R4.8)",
              (len(world_items) == 9) and (named == set(WORLD_NODES)), f"{len(world_items)} items; nodes {sorted(named)}")
        world_images = render_world(editor, scene, images_dir, "world_form", "world_space")
        check_images_close("world: the world-space form renders as the source meshes", world_images, source_images)
        # A view's shadow passes draw the casters of the form it draws: sources that stop casting change the source form alone.
        for node in ("shared_a", "shared_b", "shared_c"):
            editor.call("set_item_property", {"scene_name": scene, "item_name": node, "property": "shadow_cast", "value": False})
        editor.wait_idle()
        source_uncast = render_world(editor, scene, images_dir, "source_uncast")
        world_uncast  = render_world(editor, scene, images_dir, "world_form_uncast", "world_space")
        for _ in range(3):
            editor.call("undo", {})
        editor.wait_idle()
        check("world: the world-space form's shadows are cast by its own meshes (sources without shadow_cast change only the source form)",
              all(image_difference(world_uncast[n], world_images[n])[0] == 0 for n in world_images) and
              all(image_difference(source_uncast[n], source_images[n])[0] > 0 for n in source_images),
              json.dumps({n: [image_difference(world_uncast[n], world_images[n])[0], image_difference(source_uncast[n], source_images[n])[0]] for n in world_images}))
        bake_again = bake_world(editor, scene)
        check("world: a second bake makes nothing", export_counts(bake_again, "world_mesh").get("made") == 0, counts_text(bake_again))

        export = editor.export(scene)
        worlds = export_counts(export, "world_mesh")
        meshes = export_counts(export, "local_mesh")
        check("world: Export with the bake on uses the nine World-space mesh items and asks for no Local-space mesh item",
              (export.get("state") == "done") and (worlds.get("made") == 0) and (worlds.get("present") == 9) and (worlds.get("written") == 9) and
              (meshes.get("made", 0) == 0) and (meshes.get("written", 0) == 0) and (export.get("export", {}).get("inline_primitive_count") == 0),
              counts_text(export))
        first_scene_key = export.get("export", {}).get("scene_key")
        prepared_items, records = read_store(prepared)
        scene_glb = prepared_items[records["prepared.scene_key"]][4]
        named_keys = scene_item_keys(scene_glb)
        check("world: the prepared asset holds exactly the items its Scene item names plus the Scene item",
              set(prepared_items.keys()) == (named_keys | {records["prepared.scene_key"]}), f"{len(prepared_items)} held, {len(named_keys)} named")
        children, placeholders = scene_item_world_space(scene_glb)
        check("world: the Scene item writes the nine baked nodes under the World Space node",
              sorted(children) == sorted(WORLD_NODES), json.dumps(children))
        check("world: a placeholder keeps the baked parent's place only where a child stays (parent_mesh holds holder)",
              placeholders.get("parent_mesh") == ["holder"] and ("mirror_parent" not in placeholders), json.dumps(placeholders))
        check("world: every World Space mesh reads a World-space mesh item", world_mesh_buffers_ok(scene_glb, prepared_items))
        check("world: Export leaves the node tree as authored and the form valid",
              (node_tree(editor, scene) == tree_before) and world_status(editor, scene).get("available"))

        # R8.12: right after the Export an update needs no status job (the Export's Scene key).
        exported_status = editor.status(scene)
        check("world: the status after the Export matches with its Scene key and no status job",
              exported_status.get("matches") is True and (exported_status.get("scene_key") == first_scene_key) and
              (status_kind(exported_status, "world_mesh").get("missing") == 0), status_text(exported_status))

        # R8.6: a move invalidates at the edit; the undo makes it valid again; a bake after the move makes one item.
        editor.call("set_node_transform", {"scene_name": scene, "node_name": "mirror", "space": "local", "translation": [-2.0, 0.9, 1.0]})
        editor.wait_idle()
        moved = world_status(editor, scene)
        check("world: moving a baked node invalidates its world-space mesh and the world-space form is unavailable",
              (moved.get("missing_count") == 1) and (not moved.get("available")), json.dumps(moved))
        refusal = editor.refusal("render_scene_image", {"scene": scene, "camera": {"eye": [0, 1, 5], "target": [0, 0, 0]}, "width": 64, "height": 64,
                                                         "output": "png", "path": os.path.join(images_dir, "refused.png"), "mesh_form": "world_space"})
        check("world: the world-space form is refused while a world-space mesh is missing", refusal is not None, (refusal or "")[:120])
        moved_status = editor.status(scene)
        check("world: the status after the move counts one World-space mesh item missing",
              status_kind(moved_status, "world_mesh").get("missing") == 1, status_text(moved_status))
        # R3.7: the move changes exactly the World-space keys (and BVH keys) of the moved node.
        check("world: the move changes the World-space key of the moved node alone: as many item keys asked (the Scene key open), every other item present",
              (moved_status.get("asked_key_count") == exported_status.get("asked_key_count") - 1) and
              (status_kind(moved_status, "world_mesh").get("present") == status_kind(exported_status, "world_mesh").get("present") - 1) and
              (status_kind(moved_status, "raytrace_bvh").get("missing") == 1) and
              (status_kind(moved_status, "local_mesh") == status_kind(exported_status, "local_mesh")) and
              (moved_status.get("computed_hash_count") == 0) and (moved_status.get("matches") is False),
              f"{status_text(moved_status)} asked {moved_status.get('asked_key_count')} vs {exported_status.get('asked_key_count')}")
        editor.call("undo")
        editor.wait_idle()
        undone = world_status(editor, scene)
        check("world: undoing the move makes the world-space mesh valid again", (undone.get("missing_count") == 0) and undone.get("available"), json.dumps(undone))
        undone_status = editor.status(scene)
        check("world: after the undo the status asks for the earlier keys again and matches with the first Scene key (R3.3)",
              (status_kind(undone_status, "world_mesh").get("missing") == 0) and (status_kind(undone_status, "raytrace_bvh").get("missing") == 0) and
              undone_status.get("matches") is True and (undone_status.get("scene_key") == first_scene_key), status_text(undone_status))
        # R3.3, D8 step 8 open item: a mesh removed after the bake, and the removal undone, leaves the form available.
        editor.call("delete_nodes", {"scene_name": scene, "names": ["floor"]})
        editor.wait_idle()
        removed = world_status(editor, scene)
        editor.call("undo")
        editor.wait_idle()
        restored = world_status(editor, scene)
        check("world: removing a baked mesh ends the form's availability and undoing the removal restores it",
              removed.get("scene_changed") and (not removed.get("available")) and (not restored.get("scene_changed")) and restored.get("available"),
              f"{json.dumps(removed)} -> {json.dumps(restored)}")
        editor.call("set_node_transform", {"scene_name": scene, "node_name": "mirror", "space": "local", "translation": [-2.0, 0.9, 1.0]})
        editor.wait_idle()
        rebake = bake_world(editor, scene)
        check("world: a bake after the move makes one World-space mesh item",
              (export_counts(rebake, "world_mesh").get("made") == 1) and (export_counts(rebake, "world_mesh").get("present") == 8) and
              world_status(editor, scene).get("available"), counts_text(rebake))
        editor.call("undo")
        editor.wait_idle()
        check("world: undoing the move after the bake invalidates the moved form", world_status(editor, scene).get("missing_count") == 1)
        rebake = bake_world(editor, scene)
        check("world: baking again finds the first item", export_counts(rebake, "world_mesh").get("made") == 0, counts_text(rebake))

        # Instancing skip: the shared torus keeps its transform and asks for its Local-space mesh item.
        editor.set_export_settings(scene, {"instancing": "skip"})
        settings_status = world_status(editor, scene)
        check("world: an edit of the export settings ends the world-space form's availability",
              settings_status.get("settings_changed") and not settings_status.get("available"), json.dumps(settings_status))
        skip = editor.export(scene)
        check("world: instancing skip exports the shared mesh as one Local-space mesh item and six World-space mesh items",
              ((export_counts(skip, "local_mesh").get("made", 0) + export_counts(skip, "local_mesh").get("present", 0)) == 1) and
              (export_counts(skip, "world_mesh").get("present") == 6) and
              (export_counts(skip, "world_mesh").get("written") == 6), counts_text(skip))
        editor.set_export_settings(scene, {"instancing": "duplicate"})
        duplicate = editor.export(scene)
        check("world: instancing duplicate makes no World-space mesh item (the keys do not depend on the instancing)",
              (export_counts(duplicate, "world_mesh").get("made") == 0) and (export_counts(duplicate, "world_mesh").get("present") == 9),
              counts_text(duplicate))
        editor.set_export_settings(scene, {"instancing": "duplicate_within_budget"})
        back = editor.export(scene)
        check("world: back at duplicate_within_budget the Export has the first Scene key",
              back.get("export", {}).get("scene_key") == first_scene_key, back.get("export", {}).get("scene_key", ""))
        editor.close(scene)
    finally:
        editor.stop()

    editor = Editor(args.editor)
    try:
        scene = editor.load(source)
        second = editor.export(scene)
        made = sum(value.get("made", 0) for value in second.get("export", {}).get("counts", {}).values())
        check("world: a second Export in a new process makes nothing and writes the prepared asset anew",
              (made == 0) and rewritten(second) and (second.get("export", {}).get("scene_key") == first_scene_key), counts_text(second))
        editor.close(scene)
        for suffix in (".glb", ".erhe_editor"):
            shutil.move(os.path.join(source_dir, "world" + suffix), os.path.join(away_dir, "world" + suffix))
        for leftover in ("-wal", "-shm"):
            if os.path.exists(editor_asset + leftover):
                shutil.move(editor_asset + leftover, os.path.join(away_dir, "world.erhe_editor" + leftover))
        run = editor.load(prepared)
        check("world: with the source away the prepared asset opens as the prepared run", run == "world.pc", run)
        run_images = render_world(editor, run, images_dir, "prepared_run")
        # The two draw the same items; the shadows are cast by the source meshes in the open scene.
        check_images_close("world: the prepared run renders as the open scene's world-space form", run_images, world_images)
        check_images_close("world: the prepared run renders as the unbaked source scene", run_images, source_images)
        nodes = {node["name"]: node for node in editor.call("get_scene_nodes", {"scene_name": run})["nodes"]}
        check("world: the prepared run holds the World Space node with the baked nodes under it",
              ("World Space" in nodes) and all(nodes.get(name, {}).get("parent") == "World Space" for name in WORLD_NODES),
              json.dumps({name: nodes.get(name, {}).get("parent") for name in WORLD_NODES}))
        editor.close(run)
    finally:
        editor.stop()

    # The same scene with every child listed before its parent.
    child_dir = os.path.join(work, "world_children_first")
    os.makedirs(child_dir, exist_ok=True)
    editor = Editor(args.editor)
    try:
        name = editor.open_foreign(write_world_scene(child_dir, "world_cf", True))
        child_source = os.path.join(child_dir, "world_cf.glb")
        editor.save(name, child_source)
        editor.close(name)
        scene = editor.load(child_source)
        editor.set_export_settings(scene, {})
        editor.save(scene, child_source)
        child_images = render_world(editor, scene, images_dir, "children_first_source")
        bake = bake_world(editor, scene)
        export = editor.export(scene)
        child_items, child_records = read_store(os.path.join(child_dir, "world_cf.pc.erhe_prepared"))
        children, placeholders = scene_item_world_space(child_items[child_records["prepared.scene_key"]][4])
        check("world (children first): the bake and the Export write the nine baked nodes under World Space with the parent_mesh placeholder",
              (export_counts(bake, "world_mesh").get("made") == 9) and (sorted(children) == sorted(WORLD_NODES)) and
              (placeholders.get("parent_mesh") == ["holder"]), counts_text(bake) + " / " + json.dumps(placeholders))
        child_world = render_world(editor, scene, images_dir, "children_first_world", "world_space")
        check_images_close("world (children first): the world-space form renders as the source meshes", child_world, child_images)
        editor.close(scene)
    finally:
        editor.stop()


def pvs_status(editor, scene):
    return editor.call("get_pvs_status", {"scene_name": scene})


def wait_pvs_views(editor, scene, predicate, timeout=60.0):
    """get_pvs_status once its views satisfy predicate (the views are the last rendered frame's)."""
    deadline = time.monotonic() + timeout
    status = pvs_status(editor, scene)
    while time.monotonic() < deadline:
        status = pvs_status(editor, scene)
        if status.get("views") and predicate(status["views"]):
            return status
        time.sleep(0.2)
    return status


def scene_item_pvs(glb):
    """The ERHE_pvs extension of a Scene item and the uri of the buffer its bits view reads (None, None without one)."""
    document = scene_item_document(glb)
    extension = document.get("extensions", {}).get("ERHE_pvs")
    if extension is None:
        return None, None
    view = document["bufferViews"][extension["bits"]]
    return extension, document["buffers"][view["buffer"]].get("uri")


def bake_pvs(editor, scene, timeout=1800.0):
    """MCP pvs_bake (disocclusion) polled to its end; the final get_pvs_bake_status."""
    editor.call("pvs_bake", {"scene_name": scene, "method": "disocclusion"})
    deadline = time.monotonic() + timeout
    stages = set()
    while time.monotonic() < deadline:
        status = editor.call("get_pvs_bake_status")
        stages.add(status.get("stage"))
        if status.get("state") in ("done", "failed", "cancelled"):
            status["stages_seen"] = sorted(s for s in stages if s)
            editor.wait_idle()
            return status
        time.sleep(0.1)
    raise RuntimeError("pvs_bake did not finish")


def phase_pvs(args, work):
    """D8 step 9: the PVS item, the bake on the world-space form, pvs_mode, R8.10, R9.6."""
    foreign_dir = os.path.join(work, "pvs_foreign")
    source_dir  = os.path.join(work, "pvs_source")
    away_dir    = os.path.join(work, "pvs_away")
    for directory in (foreign_dir, source_dir, away_dir):
        os.makedirs(directory, exist_ok=True)
    source = os.path.join(source_dir, "pvs.glb")
    prepared = os.path.join(source_dir, "pvs.pc.erhe_prepared")
    editor_asset = os.path.join(source_dir, "pvs.erhe_editor")
    editor = Editor(args.editor)
    try:
        name = editor.open_foreign(write_world_scene(foreign_dir, "pvs", False))
        editor.save(name, source)
        editor.close(name)
        scene = editor.load(source)
        editor.set_export_settings(scene, {})
        settings = editor.scene_settings(scene).get("export_settings") or {}
        check("pvs: added export settings have pvs_mode off (R7.6)", settings.get("pvs_mode") == "off", json.dumps(settings.get("pvs_mode")))
        editor.save(scene, source)

        off = editor.export(scene)
        check("pvs: pvs_mode off exports no PVS item",
              (off.get("state") == "done") and ("pvs" not in off.get("export", {}).get("counts", {})) and (len(kind_items(prepared, KIND_PVS)) == 0) and
              (scene_item_pvs(read_store(prepared)[0][read_store(prepared)[1]["prepared.scene_key"]][4])[0] is None), counts_text(off))

        editor.set_export_settings(scene, {"pvs_mode": "reuse"})
        log_start = len(read_log())
        reuse_none = editor.export(scene)
        # The Scene item records the scene's export settings, so the setting's edit makes a Scene item.
        check("pvs: pvs_mode reuse without a PVS item writes none and logs it",
              (reuse_none.get("state") == "done") and ("pvs" not in reuse_none.get("export", {}).get("counts", {})) and
              (export_counts(reuse_none, "world_mesh").get("made") == 0) and (len(kind_items(prepared, KIND_PVS)) == 0) and
              ("holds no PVS item" in read_log()[log_start:]), counts_text(reuse_none))

        editor.set_export_settings(scene, {"pvs_mode": "rebake"})
        items_before, _ = read_store(editor_asset)
        log_start = len(read_log())
        refused = editor.export(scene)
        items_after, _ = read_store(editor_asset)
        check("pvs: pvs_mode rebake with the PVS region unset fails before making anything (R7.6)",
              (refused.get("state") == "failed") and ("PVS region is unset" in read_log()[log_start:]) and (set(items_after) == set(items_before)),
              (refused.get("error") or "")[:160])

        # The region around the content and the viewport camera, so the camera stands in a cell.
        camera = pvs_status(editor, scene).get("camera", {}).get("position") or [0.0, 1.0, 5.0]
        region_min = [min(-3.5, camera[0] - 1.0), min(-0.5, camera[1] - 1.0), min(-3.0, camera[2] - 1.0)]
        region_max = [max(3.5, camera[0] + 1.0), max(3.0, camera[1] + 1.0), max(3.5, camera[2] + 1.0)]
        extent = max(region_max[i] - region_min[i] for i in range(3))
        editor.set_export_settings(scene, {"pvs_region_min": region_min, "pvs_region_max": region_max, "pvs_cell_size": max(2.0, extent / 4.0)})
        job_id = editor.begin_export(scene)
        refused_during = None
        progress_seen = None
        deadline = time.monotonic() + 1800.0
        while time.monotonic() < deadline:
            job = editor.job(job_id)
            if job.get("state") in ("done", "failed"):
                break
            progress = job.get("progress") or {}
            if (progress.get("stage") == "baking the PVS") and (refused_during is None):
                progress_seen = progress
                refused_during = editor.refusal("set_node_transform", {"scene_name": scene, "node_name": "floor", "space": "local", "translation": [0.0, 0.1, 0.0]})
            time.sleep(0.05)
        rebake = editor.finish_export(job_id)
        editor.wait_idle()
        check("pvs: Export with pvs_mode rebake reports the PVS bake stage with its cell progress and holds the scene read-only meanwhile (R9.6, R9.7)",
              (progress_seen is not None) and ("total" in progress_seen) and (refused_during is not None) and ("save is in progress" in refused_during),
              json.dumps(progress_seen) + " / " + (refused_during or "")[:80])
        pvs_counts = export_counts(rebake, "pvs")
        editor_pvs = kind_items(editor_asset, KIND_PVS)
        prepared_pvs = kind_items(prepared, KIND_PVS)
        _, editor_records = read_store(editor_asset)
        check("pvs: Export with pvs_mode rebake bakes the PVS, stores one PVS item in the editor asset and writes it (R4.10, R9.2, R9.3)",
              (rebake.get("state") == "done") and (pvs_counts.get("made") == 1) and (pvs_counts.get("written") == 1) and
              (len(editor_pvs) == 1) and (set(prepared_pvs) == set(editor_pvs)) and (editor_records.get("pvs.key") in editor_pvs) and
              (rebake.get("export", {}).get("pvs_cell_count", 0) > 0),
              counts_text(rebake) + f" cells {rebake.get('export', {}).get('pvs_cell_count')} pvs_ms {rebake.get('export', {}).get('pvs_ms', 0):.0f}")
        pvs_key = editor_records.get("pvs.key")
        prepared_items, prepared_records = read_store(prepared)
        scene_glb = prepared_items[prepared_records["prepared.scene_key"]][4]
        extension, bits_uri = scene_item_pvs(scene_glb)
        uids = {node["name"]: node.get("gltf_uid") for node in editor.call("get_scene_nodes", {"scene_name": scene})["nodes"]}
        column_uids = [uid for uid, _ in json.loads(editor_pvs[pvs_key][0])["column_uids"]] if pvs_key in editor_pvs else []
        check("pvs: the Scene item names the PVS item and its columns are the baked nodes' uids (R4.10)",
              (extension is not None) and (bits_uri == ITEM_URI + (pvs_key or "")) and
              (set(column_uids) == {uids.get(node) for node in WORLD_NODES}),
              f"{len(column_uids)} columns, uri {bits_uri}")
        holds = scene_item_keys(scene_glb) | {prepared_records["prepared.scene_key"]}
        check("pvs: the prepared asset holds exactly the items its Scene item names plus the Scene item", set(prepared_items) == holds)
        status = pvs_status(editor, scene)
        check("pvs: the open scene's PVS is ready and baked on the world-space form",
              (status.get("state") == "ready") and (status.get("form") == "world_space"), f"{status.get('state')} {status.get('form')}")

        # R8.10: the PVS culls only in a viewport that draws the world-space form.
        editor.call("set_viewport_mesh_form", {"scene_name": scene, "mesh_form": "source"})
        source_views = wait_pvs_views(editor, scene, lambda views: all(view.get("tested", 0) == 0 for view in views))
        editor.call("set_viewport_mesh_form", {"scene_name": scene, "mesh_form": "world_space"})
        world_views = wait_pvs_views(editor, scene, lambda views: any(view.get("tested", 0) > 0 for view in views))
        check("pvs: PVS culling tests no entry in a viewport drawing source meshes and tests entries in one drawing the world-space form (R8.10)",
              bool(source_views.get("views")) and all(view.get("tested", 0) == 0 for view in source_views["views"]) and
              any(view.get("tested", 0) > 0 for view in world_views.get("views", [])),
              json.dumps(source_views.get("views")) + " / " + json.dumps(world_views.get("views")))
        editor.call("set_viewport_mesh_form", {"scene_name": scene, "mesh_form": "source"})

        again = editor.export(scene)
        made = sum(value.get("made", 0) for value in again.get("export", {}).get("counts", {}).values())
        check("pvs: a second rebake Export of the unchanged scene bakes the same PVS, makes nothing and writes the prepared asset anew",
              (made == 0) and rewritten(again) and (export_counts(again, "pvs").get("present") == 1) and
              (again.get("export", {}).get("scene_key") == rebake.get("export", {}).get("scene_key")), counts_text(again))
        standalone = bake_pvs(editor, scene)
        check("pvs: pvs_bake runs Bake World-Space Meshes first and replaces the PVS item (R8.9)",
              (standalone.get("state") == "done") and ("baking world-space meshes" in standalone.get("stages_seen", [])) and
              (standalone.get("pvs_item", {}).get("key") in kind_items(editor_asset, KIND_PVS)) and (len(kind_items(editor_asset, KIND_PVS)) == 1),
              json.dumps({k: standalone.get(k) for k in ("state", "stages_seen", "pvs_item", "error")}))

        editor.set_export_settings(scene, {"pvs_mode": "reuse"})
        log_start = len(read_log())
        reuse = editor.export(scene)
        made = sum(value.get("made", 0) for kind, value in reuse.get("export", {}).get("counts", {}).items() if kind != "scene")
        check("pvs: pvs_mode reuse copies the editor asset's PVS item as it is, logged as unsafe (R7.6)",
              (reuse.get("state") == "done") and (made == 0) and (export_counts(reuse, "pvs").get("present") == 1) and
              ("with no check that it matches" in read_log()[log_start:]) and (set(kind_items(prepared, KIND_PVS)) == set(kind_items(editor_asset, KIND_PVS))),
              counts_text(reuse))
        reuse_key = reuse.get("export", {}).get("scene_key")
        reuse_status = editor.status(scene)
        check("pvs: the status of pvs_mode reuse matches the prepared asset", reuse_status.get("matches") is True, status_text(reuse_status))
        editor.save(scene, source)
        editor.close(scene)
    finally:
        editor.stop()

    editor = Editor(args.editor)
    try:
        scene = editor.load(source)
        second = editor.export(scene)
        made = sum(value.get("made", 0) for value in second.get("export", {}).get("counts", {}).values())
        check("pvs: a second Export (reuse) in a new process makes nothing and writes the prepared asset anew (R9.4)",
              (made == 0) and rewritten(second) and (second.get("export", {}).get("scene_key") == reuse_key), counts_text(second))
        editor.close(scene)
        for suffix in (".glb", ".erhe_editor"):
            shutil.move(os.path.join(source_dir, "pvs" + suffix), os.path.join(away_dir, "pvs" + suffix))
        for leftover in ("-wal", "-shm"):
            if os.path.exists(editor_asset + leftover):
                shutil.move(editor_asset + leftover, os.path.join(away_dir, "pvs.erhe_editor" + leftover))
        run = editor.load(prepared)
        status = pvs_status(editor, run)
        run_views = wait_pvs_views(editor, run, lambda views: any(view.get("tested", 0) > 0 for view in views))
        check("pvs: the prepared run loads the PVS from its item and reports it ready (R4.10)",
              (run == "pvs.pc") and (status.get("state") == "ready") and (status.get("form") == "source"),
              f"{run} {status.get('state')} {status.get('form')} columns {status.get('column_count')}")
        check("pvs: the prepared run's viewport is PVS culled (its nodes are the world-space form)",
              any(view.get("tested", 0) > 0 for view in run_views.get("views", [])), json.dumps(run_views.get("views")))
        mismatch = [line for line in read_log().splitlines() if "was baked for scene id" in line]
        check("pvs: the prepared run's PVS carries the Scene item's stored scene id (no mismatch warning)",
              mismatch == [], "; ".join(mismatch[:2]))
        editor.close(run)
    finally:
        editor.stop()

    for suffix in (".glb", ".erhe_editor", ".erhe_editor-wal", ".erhe_editor-shm"):
        if os.path.exists(os.path.join(away_dir, "pvs" + suffix)):
            shutil.move(os.path.join(away_dir, "pvs" + suffix), os.path.join(source_dir, "pvs" + suffix))
    editor = Editor(args.editor)
    try:
        scene = editor.load(source)
        editor.set_export_settings(scene, {"pvs_mode": "off"})
        off_again = editor.export(scene)
        check("pvs: pvs_mode off again writes no PVS item into the prepared asset and keeps it in the editor asset",
              (export_counts(off_again, "pvs").get("written", 0) == 0) and (len(kind_items(prepared, KIND_PVS)) == 0) and
              (len(kind_items(editor_asset, KIND_PVS)) == 1),
              counts_text(off_again))
        editor.close(scene)
    finally:
        editor.stop()


# --- Several files (doc/editor/prepared_assets.md "Several files", R10) ---------

MULTI_CAMERAS = [
    ("front", [0.0, 3.5, 7.0], [0.0, 0.6, 0.0]),
    ("side",  [6.0, 2.5, -3.0], [0.0, 0.6, 0.0])
]


def solid_png(path, rgba_a, rgba_b, size=8):
    rows = [[c for x in range(size) for c in (rgba_a if ((x // 2 + y // 2) % 2) == 0 else rgba_b)] for y in range(size)]
    with open(path, "wb") as handle:
        handle.write(png_bytes(size, size, rows))


def write_gltf(directory, stem, document, builder):
    binary = builder.padded_binary()
    document["accessors"]   = builder.accessors
    document["bufferViews"] = builder.views
    document["buffers"]     = [{"uri": f"{stem}.bin", "byteLength": len(binary)}]
    with open(os.path.join(directory, f"{stem}.bin"), "wb") as handle:
        handle.write(binary)
    path = os.path.join(directory, f"{stem}.gltf")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(document, handle)
    return path


def write_multi_sources(directory):
    """The foreign files of the several-files scene: prefab B (a textured torus), prefab A (a box), the
    material container (a textured material) and the host (a floor of the container's material)."""
    solid_png(os.path.join(directory, "tile.png"),  (200, 180, 60, 255), (60, 120, 200, 255))
    solid_png(os.path.join(directory, "brick.png"), (170, 70, 50, 255), (220, 210, 190, 255))
    builder = Gltf_builder()
    prefab_b = write_gltf(directory, "prefab_b_foreign", {
        "asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "torus_b", "mesh": 0, "rotation": quat_axis_angle((1, 0, 0), 70.0)}],
        "meshes": [{"name": "torus_b", "primitives": [builder.primitive(make_torus(0.45, 0.15, 40, 20), 0)]}],
        "materials": [{"name": "tile", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0, "roughnessFactor": 0.8}}],
        "textures": [{"source": 0, "sampler": 0}],
        "samplers": [{"magFilter": 9728, "minFilter": 9986, "wrapS": 10497, "wrapT": 10497}],
        "images": [{"uri": "tile.png"}]
    }, builder)
    builder = Gltf_builder()
    prefab_a = write_gltf(directory, "prefab_a_foreign", {
        "asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "box_a", "mesh": 0, "translation": [0.0, 0.3, 0.0]}],
        "meshes": [{"name": "box_a", "primitives": [builder.primitive(make_box(0.5, 0.3, 0.5), 0)]}],
        "materials": [{"name": "red", "pbrMetallicRoughness": {"baseColorFactor": [0.8, 0.15, 0.1, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.6}}]
    }, builder)
    with open(os.path.join(directory, "materials.gltf"), "w", encoding="utf-8") as handle:
        json.dump({
            "asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": []}],
            "materials": [{"name": "brick", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0, "roughnessFactor": 0.9}}],
            "textures": [{"source": 0, "sampler": 0}],
            "samplers": [{"magFilter": 9728, "minFilter": 9986, "wrapS": 10497, "wrapT": 10497}],
            "images": [{"uri": "brick.png"}]
        }, handle)
    builder = Gltf_builder()
    host = write_gltf(directory, "host_foreign", {
        "asset": {"version": "2.1", "minVersion": "2.1"},
        "extensionsUsed": ["KHR_lights_punctual", "ERHE_asset_reference"],
        "extensions": {"KHR_lights_punctual": {"lights": [{"type": "directional", "intensity": 3.0, "color": [1.0, 0.95, 0.9]}]}},
        "files": [{"uri": "materials.gltf", "mimeType": "model/gltf+json"}],
        "scene": 0, "scenes": [{"nodes": [0, 1, 2]}],
        "nodes": [
            {"name": "floor", "mesh": 0},
            {"name": "pillar", "mesh": 1, "translation": [0.0, 0.8, -1.5]},
            {"name": "sun", "rotation": quat_axis_angle((1, 0, 0), -55.0), "extensions": {"KHR_lights_punctual": {"light": 0}}}
        ],
        "meshes": [
            {"name": "floor",  "primitives": [builder.primitive(make_plane(4.0), 0)]},
            {"name": "pillar", "primitives": [builder.primitive(make_box(0.25, 0.8, 0.25), 1)]}
        ],
        "materials": [
            {"name": "brick", "extensions": {"ERHE_asset_reference": {"file": 0}}},
            {"name": "grey", "pbrMetallicRoughness": {"baseColorFactor": [0.5, 0.5, 0.55, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.7}}
        ]
    }, builder)
    return prefab_b, prefab_a, host


def instantiate(editor, scene, path, position):
    editor.call("instantiate_prefab", {"scene_name": scene, "path": path, "position": position})
    editor.wait_idle()


def build_multi_scene(editor, directory):
    """prefab_b.glb, prefab_a.glb (an instance of prefab_b inside) and host.glb (two instances of prefab_a,
    a floor of the container's material), each erhe-saved; the paths of the saved files."""
    prefab_b_foreign, prefab_a_foreign, host_foreign = write_multi_sources(directory)
    prefab_b = os.path.join(directory, "prefab_b.glb")
    prefab_a = os.path.join(directory, "prefab_a.glb")
    host     = os.path.join(directory, "host.glb")
    name = editor.open_foreign(prefab_b_foreign)
    editor.save(name, prefab_b)
    editor.close(name)
    name = editor.open_foreign(prefab_a_foreign)
    instantiate(editor, name, prefab_b, [0.0, 0.95, 0.0])
    editor.save(name, prefab_a)
    editor.close(name)
    name = editor.open_foreign(host_foreign)
    instantiate(editor, name, prefab_a, [-1.6, 0.0, 0.4])
    instantiate(editor, name, prefab_a, [1.6, 0.0, 0.4])
    editor.save(name, host)
    editor.close(name)
    scene = editor.load(host)
    editor.set_export_settings(scene, NO_BAKE)
    editor.save(scene, host)
    editor.close(scene)
    return prefab_b, prefab_a, host


def render_multi(editor, scene, directory, tag):
    paths = {}
    for name, eye, target in MULTI_CAMERAS:
        path = os.path.join(directory, f"{tag}_{name}.png")
        editor.render(scene, path, eye, target)
        paths[name] = path
    return paths


def store_kind_counts(path):
    counts = {}
    for value in read_store(path)[0].values():
        counts[value[0]] = counts.get(value[0], 0) + 1
    return counts


def export_files(status):
    return [(os.path.basename(entry.get("source", "")), entry.get("role"), entry.get("depth"), entry.get("location"))
            for entry in status.get("export", {}).get("files", [])]


def file_counts(status, stem, kind):
    for entry in status.get("export", {}).get("files", []):
        if os.path.basename(entry.get("source", "")) == stem:
            return entry.get("counts", {}).get(kind, {})
    return {}


def sha256_of(path):
    import hashlib
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


def log_since(mark):
    return read_log()[mark:]


def phase_several_files(args, work):
    """R10: a host scene with two instances of a prefab that instantiates a prefab, and a material of a
    container file; per-file editor assets, the flat prepared asset, the bake, R10.5 - R10.8, R2.2, R2.5."""
    directory = os.path.join(work, "multi")
    images_dir = os.path.join(work, "multi_images")
    for path in (directory, images_dir):
        os.makedirs(path, exist_ok=True)
    editor = Editor(args.editor)
    try:
        prefab_b, prefab_a, host = build_multi_scene(editor, directory)
        doc = glb_document(host)
        check("several files: the saved host references prefab_a as an external asset and brick as an asset reference",
              ("prefab_a.glb" in json.dumps(doc.get("files", []))) and ("materials.gltf" in json.dumps(doc.get("files", []))) and
              any("ERHE_asset_reference" in m.get("extensions", {}) for m in doc.get("materials", [])),
              json.dumps(doc.get("files", [])))
        records = {os.path.basename(path): glb_document(path).get("scenes", [{}])[0].get("extensions", {}).get("ERHE_scene", {}).get("editor_asset")
                   for path in (host, prefab_a, prefab_b)}
        check("R2.2: Save Scene records each source's editor asset in ERHE_scene",
              records == {"host.glb": "host.erhe_editor", "prefab_a.glb": "prefab_a.erhe_editor", "prefab_b.glb": "prefab_b.erhe_editor"},
              json.dumps(records))
        scene = editor.load(host)
        nodes = {node["name"] for node in editor.call("get_scene_nodes", {"scene_name": scene})["nodes"]}
        check("several files: the host holds two instances of prefab_a, each with prefab_b nested inside",
              {"floor", "pillar", "box_a", "torus_b"} <= nodes, f"{len(nodes)} nodes")
        source_images = render_multi(editor, scene, images_dir, "source")
        check("several files: the source images are not blank", all(distinct_colors(path) > 16 for path in source_images.values()))

        mark = len(read_log())
        status = editor.export(scene)
        log = log_since(mark)
        check("several files: Export is done", status.get("state") == "done", status.get("error", ""))
        files = export_files(status)
        check("R10.1, R10.7: the Export names four files, deepest prefab first, the scene last",
              [entry[:3] for entry in files] == [("prefab_b.glb", "prefab", 2), ("prefab_a.glb", "prefab", 1), ("materials.gltf", "material_container", 1), ("host.glb", "scene", 0)],
              json.dumps(files))
        order = [log.find(f"Ensure of the {role} '{os.path.join(directory, stem)}'") for stem, role in
                 (("prefab_b.glb", "prefab"), ("prefab_a.glb", "prefab"), ("materials.gltf", "material_container"), ("host.glb", "scene"))]
        check("R10.7: the log shows Ensure deepest prefab first, the scene last", all(i >= 0 for i in order) and (order == sorted(order)), str(order))
        assets = {stem: os.path.join(directory, stem + ".erhe_editor") for stem in ("host", "prefab_a", "prefab_b", "materials")}
        check("R10.1: each file has its editor asset beside it", all(os.path.isfile(path) for path in assets.values()))
        kinds = {stem: store_kind_counts(path) for stem, path in assets.items()}
        check("R10.2: prefab_b's editor asset holds its mesh item, BVH item and Texture item, nothing use-dependent",
              kinds["prefab_b"] == {KIND_LOCAL_MESH: 1, KIND_RAYTRACE_BVH: 1, KIND_TEXTURE: 1}, json.dumps(kinds["prefab_b"]))
        check("R10.2: prefab_a's editor asset holds its mesh item and BVH item", kinds["prefab_a"] == {KIND_LOCAL_MESH: 1, KIND_RAYTRACE_BVH: 1}, json.dumps(kinds["prefab_a"]))
        check("R10.2: the material container's editor asset holds the Texture item of its material", kinds["materials"] == {KIND_TEXTURE: 1}, json.dumps(kinds["materials"]))
        check("R10.2: the host's editor asset holds its own meshes and the Scene item",
              kinds["host"] == {KIND_LOCAL_MESH: 2, KIND_RAYTRACE_BVH: 2, KIND_SCENE: 1}, json.dumps(kinds["host"]))
        # The host's two mesh items were stored by its open; prefab templates load without items.
        check("R10.2: the prefab's mesh items are made once for both instances",
              (export_counts(status, "local_mesh").get("made") == 2) and (export_counts(status, "local_mesh").get("present") == 2) and
              (file_counts(status, "prefab_b.glb", "local_mesh").get("made") == 1),
              counts_text(status))
        records_b = read_store(assets["prefab_b"])[1]
        check("R2.1: a prefab's editor asset records its source", records_b.get("source.relative_path") == "prefab_b.glb", json.dumps(records_b))
        prepared = os.path.join(directory, "host.pc.erhe_prepared")
        check_holds_exactly("several files", prepared, status, None)
        items, prepared_records = read_store(prepared)
        scene_doc = scene_item_document(items[prepared_records["prepared.scene_key"]][4])
        check("R10.3, R10.9: the Scene item names no other file and carries both instances' content",
              ("files" not in scene_doc) and ("externalAssets" not in scene_doc) and ("ERHE_asset_reference" not in json.dumps(scene_doc)) and
              (sum(1 for node in scene_doc.get("nodes", []) if node.get("name") == "torus_b") == 2) and
              (sum(1 for node in scene_doc.get("nodes", []) if node.get("name") == "box_a") == 2),
              f"{len(scene_doc.get('nodes', []))} nodes, files {'files' in scene_doc}")
        check("R10.3: the prepared asset gathers the items of every editor asset",
              store_kind_counts(prepared) == {KIND_LOCAL_MESH: 4, KIND_RAYTRACE_BVH: 4, KIND_TEXTURE: 2, KIND_SCENE: 1}, json.dumps(store_kind_counts(prepared)))
        state = editor.status(scene)
        check("several files: the status matches, nothing missing, four files",
              state.get("matches") and all(k.get("missing", 0) == 0 for k in state.get("kinds", {}).values()) and (len(state.get("files", [])) == 4),
              status_text(state))
        prepared_sha = sha256_of(prepared)

        # R10.8: an unreadable prefab source fails the Export naming it; the prepared asset is unchanged.
        os.chmod(prefab_b, 0)
        try:
            failed = editor.export(scene)
        finally:
            os.chmod(prefab_b, 0o644)
        check("R10.8: Export fails naming the unreadable prefab source",
              (failed.get("state") == "failed") and ("prefab_b.glb" in failed.get("error", "")) and ("cannot be read" in failed.get("error", "")),
              failed.get("error", "")[:200])
        check("R10.8: the failed Export leaves the prepared asset unchanged", sha256_of(prepared) == prepared_sha)
        container = os.path.join(directory, "materials.gltf")
        os.chmod(container, 0)
        try:
            failed = editor.export(scene)
        finally:
            os.chmod(container, 0o644)
        check("R10.8: Export fails naming the unreadable material container",
              (failed.get("state") == "failed") and ("material container" in failed.get("error", "")) and ("materials.gltf" in failed.get("error", "")),
              failed.get("error", "")[:200])
        check("R10.8: the prepared asset is still unchanged", sha256_of(prepared) == prepared_sha)
        if args.negative_control == "multi":
            print("(negative control: the prefab source stays readable)")
            failed = editor.export(scene)
            check("R10.8 negative control: Export fails naming the unreadable prefab source", failed.get("state") == "failed", failed.get("error", "")[:200])
        editor.close(scene)
    finally:
        editor.stop()

    # A new process: nothing is made or copied; a prefab's own differing export setting is logged (R10.5).
    editor = Editor(args.editor)
    try:
        name = editor.load(prefab_a)
        editor.set_export_settings(name, {"raytrace_bvh": False, "texture_encoding": "source"})
        editor.save(name, prefab_a)
        editor.close(name)
        scene = editor.load(host)
        mark = len(read_log())
        status = editor.export(scene)
        log = log_since(mark)
        check("several files: a second Export in a new process makes nothing and writes the prepared asset anew",
              all(counts.get("made", 0) == 0 for counts in status.get("export", {}).get("counts", {}).values()) and rewritten(status),
              counts_text(status))
        check("R10.5: Export logs the prefab's differing setting with both values; the scene's value applies",
              (f"the prefab '{os.path.join(directory, 'prefab_a.glb')}' has export setting raytrace_bvh = false where the scene has true - the scene's value applies" in log) and
              (export_counts(status, "raytrace_bvh").get("present") == 4),
              counts_text(status))
        check("R10.5: only the differing setting is logged", log.count("has export setting") == 1, str(log.count("has export setting")))

        # R10.6: a Save of the prefab source reloads the instances and marks the host's status stale.
        state = editor.status(scene)
        check("R10.6: the host's status matches before the prefab edit", state.get("matches"), status_text(state))
        original_b = os.path.join(work, "prefab_b_original.glb")
        shutil.copyfile(prefab_b, original_b)
        name_b = editor.load(prefab_b)
        mark = len(read_log())
        # The read builds the torus's Geometry the edit needs.
        editor.call("get_mesh_attribute_values", {"scene_name": name_b, "node_name": "torus_b", "domain": "vertex", "indices": [0], "attributes": ["position"]})
        editor.wait_idle()
        editor.call("set_mesh_attribute_values", {"scene_name": name_b, "node_name": "torus_b", "attribute": "position", "elements": [0], "value": [0.7, 0.05, 0.0]})
        editor.wait_idle()
        editor.save(name_b, prefab_b)
        editor.wait_idle()
        time.sleep(0.5)
        log = log_since(mark)
        stale = editor.call("get_prepared_status", {"scene_name": scene})
        check("R10.6: the Save of the prefab source marks the status of the scene that instantiates it stale, computing nothing",
              ("Prefab instances of 'host' reloaded: its status is out of date" in log) and ("Key check of 'host' (pc)" not in log) and
              (stale.get("stale") is True) and (stale.get("pending") is False),
              json.dumps({k: stale.get(k) for k in ("stale", "pending")}))
        state = editor.status(scene)
        check("R10.6: after the prefab edit the host's status misses the prefab's new mesh item and does not match",
              (status_kind(state, "local_mesh").get("missing") == 1) and not state.get("matches"), status_text(state))
        editor.close(name_b)
        shutil.copyfile(original_b, prefab_b)
        editor.call("reload_prefab", {"path": prefab_b})
        editor.wait_idle()
        state = editor.status(scene)
        check("R10.6: MCP reload_prefab of the restored source finds the first items again", state.get("matches"), status_text(state))
        editor.close(scene)
    finally:
        editor.stop()

    # The world-space bake covers the instances' static meshes (R10.4).
    editor = Editor(args.editor)
    try:
        scene = editor.load(host)
        editor.set_export_settings(scene, {"bake_static_to_world_space": True, "instancing": "duplicate"})
        status = editor.export(scene)
        world = export_counts(status, "world_mesh")
        check("R10.4: with instancing duplicate each instance gets its own World-space mesh items, in the host's editor asset",
              (world.get("made") == 6) and (export_counts(status, "local_mesh").get("written", 0) == 0) and
              (store_kind_counts(os.path.join(directory, "host.erhe_editor")).get(KIND_WORLD_MESH) == 6) and
              (KIND_WORLD_MESH not in store_kind_counts(os.path.join(directory, "prefab_b.erhe_editor"))),
              counts_text(status))
        editor.set_export_settings(scene, {"instancing": "skip"})
        status = editor.export(scene)
        local = export_counts(status, "local_mesh")
        check("R10.4: with instancing skip the instances share the prefabs' Local-space mesh items",
              (export_counts(status, "world_mesh").get("present") == 2) and (local.get("present") == 2) and (local.get("made") == 0),
              counts_text(status))
        editor.set_export_settings(scene, NO_BAKE)
        editor.save(scene, host)
        editor.close(scene)
    finally:
        editor.stop()

    # R10.3: the prepared run opens with every source moved away and renders as the source.
    away = os.path.join(work, "multi_away")
    os.makedirs(away, exist_ok=True)
    for stem in ("host.glb", "prefab_a.glb", "prefab_b.glb", "materials.gltf", "brick.png", "tile.png"):
        shutil.move(os.path.join(directory, stem), os.path.join(away, stem))
    editor = Editor(args.editor)
    try:
        run = editor.load(os.path.join(directory, "host.pc.erhe_prepared"))
        check("R10.3: the prepared asset opens as a prepared run with every source file away", run == "host.pc", run)
        run_images = render_multi(editor, run, images_dir, "run")
        for name in source_images:
            same, differing, total = compare_images(source_images[name], run_images[name])
            check(f"R10.3: the prepared run renders the source image ({name}) pixel for pixel", same, f"{differing} of {total} pixels differ")
        editor.close(run)
    finally:
        editor.stop()
    for stem in ("host.glb", "prefab_a.glb", "prefab_b.glb", "materials.gltf", "brick.png", "tile.png"):
        shutil.move(os.path.join(away, stem), os.path.join(directory, stem))

    # R2.2, R2.6: moved together and the scene renamed, the editor asset is found through the record.
    moved = os.path.join(work, "multi_moved")
    shutil.copytree(directory, moved)
    os.rename(os.path.join(moved, "host.glb"), os.path.join(moved, "renamed.glb"))
    editor = Editor(args.editor)
    try:
        scene = editor.load(os.path.join(moved, "renamed.glb"))
        status = editor.export(scene)
        host_entry = [entry for entry in status.get("export", {}).get("files", []) if entry.get("role") == "scene"]
        check("R2.2, R2.6: after moving the files together and renaming the scene, its editor asset is found through the record",
              bool(host_entry) and (os.path.basename(host_entry[0].get("editor_asset", "")) == "host.erhe_editor") and (host_entry[0].get("location") == "record") and
              (export_counts(status, "local_mesh").get("made") == 0) and (export_counts(status, "texture").get("made") == 0),
              json.dumps(host_entry)[:300] + " " + counts_text(status))
        editor.close(scene)
    finally:
        editor.stop()
    # R10.8: references the open could not resolve fail the Export naming the file (a stub material, an empty carrier).
    for missing, expected in (("materials.gltf", "holds a stub of the material"), ("prefab_b.glb", "could not be read")):
        os.rename(os.path.join(moved, missing), os.path.join(moved, missing + ".away"))
        editor = Editor(args.editor)
        try:
            scene = editor.load(os.path.join(moved, "renamed.glb"))
            failed = editor.export(scene)
            check(f"R10.8: with {missing} missing at the open the Export fails naming it",
                  (failed.get("state") == "failed") and (missing in failed.get("error", "")) and (expected in failed.get("error", "")),
                  failed.get("error", "")[:240])
            editor.close(scene)
        finally:
            editor.stop()
        os.rename(os.path.join(moved, missing + ".away"), os.path.join(moved, missing))

    # R2.5: a read-only directory: fallback editor assets under the cache directory, logged once per file.
    readonly = os.path.join(work, "multi_readonly")
    os.makedirs(readonly)
    for stem in ("host.glb", "prefab_a.glb", "prefab_b.glb", "materials.gltf", "brick.png", "tile.png"):
        shutil.copyfile(os.path.join(directory, stem), os.path.join(readonly, stem))
    os.chmod(readonly, 0o555)
    probe = os.path.join(readonly, "probe")
    writable = True
    try:
        open(probe, "w").close()
        os.remove(probe)
    except OSError:
        writable = False
    fallbacks = []
    if writable:
        print("SKIP: R2.5 fallback checks (a read-only directory is writable for this user)")
    else:
        editor = Editor(args.editor)
        try:
            scene = editor.load(os.path.join(readonly, "host.glb"))
            out_path = os.path.join(work, "readonly_host.pc.erhe_prepared")
            first = editor.finish_export(editor.call("prepare_scene", {"scene_name": scene, "target": "pc", "path": out_path})["job_id"])
            second = editor.finish_export(editor.call("prepare_scene", {"scene_name": scene, "target": "pc", "path": out_path})["job_id"])
            fallbacks = [entry.get("editor_asset", "") for entry in first.get("export", {}).get("files", [])]
            cache = os.path.join(REPO_ROOT, "cache", "editor_assets")
            check("R2.5: in a read-only directory every file's editor asset is a fallback under the cache directory",
                  (first.get("state") == "done") and (len(fallbacks) == 4) and
                  all(entry.get("location") == "fallback" for entry in first.get("export", {}).get("files", [])) and
                  all(os.path.dirname(os.path.abspath(path)) == cache for path in fallbacks),
                  first.get("error", "") + " " + json.dumps(export_files(first)))
            check("R2.5: the fallback is logged once per file", read_log().count("cannot be written - using the fallback") == 4,
                  str(read_log().count("cannot be written - using the fallback")))
            check("R2.5: a second Export uses the fallback editor assets and makes nothing",
                  (second.get("state") == "done") and all(counts.get("made", 0) == 0 for counts in second.get("export", {}).get("counts", {}).values()),
                  counts_text(second))
            editor.close(scene)
            # R10.8, R2.5: with the fallback location unwritable too, the Export fails naming the file and both locations.
            readonly_second = os.path.join(work, "multi_readonly_2")
            shutil.copytree(readonly, readonly_second)
            os.chmod(readonly_second, 0o555)
            os.chmod(cache, 0o555)
            try:
                scene = editor.load(os.path.join(readonly_second, "host.glb"))
                failed = editor.finish_export(editor.call("prepare_scene", {"scene_name": scene, "target": "pc", "path": out_path + "_2"})["job_id"])
            finally:
                os.chmod(cache, 0o755)
                os.chmod(readonly_second, 0o755)
            error = failed.get("error", "")
            check("R10.8: with no writable editor asset location the Export fails naming the file and both locations",
                  (failed.get("state") == "failed") and ("host.glb" in error) and ("host.erhe_editor" in error) and ("nor at the fallback" in error) and
                  not os.path.exists(out_path + "_2"),
                  error[:300])
            editor.close(scene)
        finally:
            editor.stop()
    os.chmod(readonly, 0o755)
    for path in fallbacks:
        for suffix in ("", "-wal", "-shm"):
            if os.path.exists(path + suffix):
                os.remove(path + suffix)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", required=True, help="headless editor binary (relative to the repository root or absolute)")
    parser.add_argument("--mesh-count", type=int, default=600, help="meshes of the scene the kill checks interrupt")
    parser.add_argument("--keep", action="store_true", help="keep the work directory")
    parser.add_argument("--negative-control", choices=("image", "items", "drop", "multi"), help="break one check on purpose: it must FAIL")
    parser.add_argument("--skip-kill", action="store_true", help="skip the kill checks")
    parser.add_argument("--only-world", action="store_true", help="run the world-space checks alone")
    parser.add_argument("--only-pvs", action="store_true", help="run the PVS checks alone")
    parser.add_argument("--only-encoding", action="store_true", help="run the texture_encoding checks alone")
    parser.add_argument("--only-multi", action="store_true", help="run the several-files checks alone")
    args = parser.parse_args()

    start = time.monotonic()
    cache_before = cache_directory_state()
    # Canonical, as the editor reports paths (macOS /var is a link to /private/var).
    work = os.path.realpath(tempfile.mkdtemp(prefix="prepared_asset_verify_"))
    print(f"work directory {work}", flush=True)
    try:
        if args.only_world:
            phase_world_space(args, work)
            raise SystemExit
        if args.only_pvs:
            phase_pvs(args, work)
            raise SystemExit
        if args.only_encoding:
            phase_texture_encoding(args, work)
            raise SystemExit
        if args.only_multi:
            phase_several_files(args, work)
            raise SystemExit
        source, images, status, _ = phase_export(args, work)
        source_hit = phase_reexport_and_open(args, work, source, images, status)
        phase_quest_target(args, work, source, images, source_hit)
        phase_settings(args, work, source, images, source_hit)
        phase_default_scene(args, work)
        phase_textures(args, work)
        phase_texture_encoding(args, work)
        phase_identities(args, work)
        phase_world_space(args, work)
        phase_pvs(args, work)
        phase_several_files(args, work)
        if not args.skip_kill:
            phase_kill(args, work)
    except SystemExit:
        pass
    except Exception as error:  # noqa: BLE001 - a failed step is a failed check
        check("run completed without an error", False, f"{type(error).__name__}: {error}")
    finally:
        for editor in LAUNCHED:
            editor.stop()
        check_cache_directories(cache_before)
        check_no_derived_data_files(work)
        check_no_file_parse_reads()
        if args.keep:
            print(f"kept {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)
    failed = [label for label, ok in RESULTS if not ok]
    print(f"\n{len(RESULTS) - len(failed)}/{len(RESULTS)} checks passed, {len(failed)} failed in {time.monotonic() - start:.0f} s")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
