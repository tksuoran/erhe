#!/usr/bin/env python3
"""Leaf material: headless checks of doc/erhe/leaf_material.md.

The scene: two double-sided quads facing +Z, "Plain" (isotropic BRDF) and
"Leaf" (KHR_materials_diffuse_transmission, imported as the leaf BxDF), and a
directional light shining along -Z, so the quads' front faces are lit. A third
double-sided quad, "Blocker", faces -Z (its front face points away from the
light) and moves between the light and the Leaf for the shadow checks.
"Slab" is a closed 2 cm thick leaf (KHR_materials_volume thickness 2.5 cm),
the form of the level kit's leaves.

  - import: the Leaf material is a leaf, double sided, with the file's
    transmission and its back face copying the front;
  - back faces of a double-sided material shade with the reversed normal: the
    Plain quad's back is far darker than its lit front;
  - the Leaf's back face shows the light transmitted through it, tinted by the
    transmission color;
  - the Blocker, whose front face points away from the light, casts a shadow
    that removes the transmitted light;
  - the closed Slab transmits light through its thickness, and does not with
    thickness 0 (its own far face shadows it).

    python3 scripts/leaf_material_verify.py [--editor <path>]

Launches the headless editor (prepared_asset_verify.Editor: deterministic
clock, default layout) and stops it, by its pid, at the end. One PASS/FAIL
line per check, exit code 1 when any check fails.
"""

import argparse
import json
import math
import os
import shutil
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import prepared_asset_verify as pav  # noqa: E402
from prepared_asset_verify import Editor, check  # noqa: E402

DEFAULT_EDITOR = "build_vs2026_vulkan_headless/bin/Debug/editor.exe" if sys.platform == "win32" else "build_ninja_linux_vulkan_headless/bin/editor"
WIDTH        = 480
HEIGHT       = 270
FOV_Y        = 50.0
EYE_DISTANCE = 6.0
PLAIN_X      = -1.5
LEAF_X       = 1.5
SLAB_X       = 4.0
SLAB_HALF_THICKNESS = 0.01
SLAB_THICKNESS      = 0.025
LEAF_COLOR   = [0.1, 0.3, 0.1]
TRANSMISSION = 0.5
TRANSMISSION_COLOR = [0.2, 1.0, 0.2]


def make_quad(half, normal_z):
    """A quad in the XY plane facing +Z (normal_z 1) or -Z (normal_z -1)."""
    positions = [(-half, -half, 0.0), (half, -half, 0.0), (half, half, 0.0), (-half, half, 0.0)]
    normals   = [(0.0, 0.0, normal_z)] * 4
    texcoords = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
    indices   = [0, 1, 2, 0, 2, 3] if normal_z > 0.0 else [0, 2, 1, 0, 3, 2]
    return positions, normals, texcoords, indices


def write_scene(directory):
    builder = pav.Gltf_builder()
    document = {
        "asset": {"version": "2.0", "generator": "leaf_material_verify.py"},
        "extensionsUsed": ["KHR_materials_diffuse_transmission", "KHR_materials_volume"],
        "scene": 0, "scenes": [{"nodes": [0, 1, 2, 3]}],
        "nodes": [
            {"name": "Plain",   "mesh": 0, "translation": [PLAIN_X, 0.0, 0.0]},
            {"name": "Leaf",    "mesh": 1, "translation": [LEAF_X, 0.0, 0.0]},
            {"name": "Blocker", "mesh": 2, "translation": [50.0, 0.0, 0.0]},
            {"name": "Slab",    "mesh": 3, "translation": [SLAB_X, 0.0, 0.0]}
        ],
        "meshes": [
            {"name": "plain",   "primitives": [builder.primitive(make_quad(1.0,  1.0), 0)]},
            {"name": "leaf",    "primitives": [builder.primitive(make_quad(1.0,  1.0), 1)]},
            {"name": "blocker", "primitives": [builder.primitive(make_quad(1.5, -1.0), 0)]},
            {"name": "slab",    "primitives": [builder.primitive(pav.make_box(0.7, 0.7, SLAB_HALF_THICKNESS), 2)]}
        ],
        "materials": [
            {"name": "Plain Material", "doubleSided": True,
             "pbrMetallicRoughness": {"baseColorFactor": [0.8, 0.8, 0.8, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.6}},
            {"name": "Leaf Material", "doubleSided": True,
             "pbrMetallicRoughness": {"baseColorFactor": LEAF_COLOR + [1.0], "metallicFactor": 0.0, "roughnessFactor": 0.5},
             "extensions": {"KHR_materials_diffuse_transmission": {
                 "diffuseTransmissionFactor": TRANSMISSION, "diffuseTransmissionColorFactor": TRANSMISSION_COLOR}}},
            {"name": "Slab Material",
             "pbrMetallicRoughness": {"baseColorFactor": LEAF_COLOR + [1.0], "metallicFactor": 0.0, "roughnessFactor": 0.5},
             "extensions": {
                 "KHR_materials_diffuse_transmission": {"diffuseTransmissionFactor": TRANSMISSION, "diffuseTransmissionColorFactor": TRANSMISSION_COLOR},
                 "KHR_materials_volume": {"thicknessFactor": SLAB_THICKNESS}}}
        ],
        "accessors": builder.accessors, "bufferViews": builder.views
    }
    binary = builder.padded_binary()
    document["buffers"] = [{"uri": "leaf_scene.bin", "byteLength": len(binary)}]
    with open(os.path.join(directory, "leaf_scene.bin"), "wb") as handle:
        handle.write(binary)
    path = os.path.join(directory, "leaf_scene.gltf")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(document, handle)
    return path


def read_pfm(path):
    """rows[y][x] = (r, g, b) of an RGB float PFM, y = 0 the top row."""
    with open(path, "rb") as handle:
        data = handle.read()
    parts = data.split(b"\n", 3)
    width, height = (int(t) for t in parts[1].split())
    endian = "<" if float(parts[2]) < 0.0 else ">"
    floats = struct.unpack(f"{endian}{width * height * 3}f", parts[3][:width * height * 12])
    return [[floats[(height - 1 - y) * width * 3 + 3 * x:(height - 1 - y) * width * 3 + 3 * x + 3] for x in range(width)] for y in range(height)]


def pixel_column(world_x, eye_z):
    """The image column of a point (world_x, 0, 0) seen from (0, 0, eye_z) looking at the origin."""
    half_height = EYE_DISTANCE * math.tan(math.radians(FOV_Y * 0.5))
    half_width  = half_height * WIDTH / HEIGHT
    screen_x    = world_x if eye_z > 0.0 else -world_x  # the back view mirrors x
    return int(round((WIDTH * 0.5) + (screen_x / half_width) * (WIDTH * 0.5)))


class Session:
    def __init__(self, editor, work_dir, scene):
        self.e = editor
        self.work_dir = work_dir
        self.scene = scene
        self.index = 0

    def render(self, eye_z):
        """The mean color of a 9 x 9 patch at the centre of each quad, seen from (0, 0, eye_z)."""
        self.index += 1
        path = os.path.join(self.work_dir, f"leaf_{self.index}.pfm")
        camera = {"eye": [0.0, 0.0, eye_z], "target": [0.0, 0.0, 0.0], "fov_y_degrees": FOV_Y, "near": 0.05, "far": 100.0}
        self.e.call("render_scene_image", {"scene": self.scene, "camera": camera, "width": WIDTH, "height": HEIGHT,
                                           "output": "linear", "color_format": "rgba32f", "msaa_samples": 0, "path": path})
        rows = read_pfm(path)
        result = {}
        for name, world_x in (("plain", PLAIN_X), ("leaf", LEAF_X), ("slab", SLAB_X)):
            cx = pixel_column(world_x, eye_z)
            cy = HEIGHT // 2
            patch = [rows[y][x] for y in range(cy - 4, cy + 5) for x in range(cx - 4, cx + 5)]
            result[name] = [sum(p[c] for p in patch) / len(patch) for c in range(3)]
        return result

    def move(self, node, translation):
        self.e.call("set_node_transform", {"scene_name": self.scene, "node_name": node, "translation": translation})
        self.e.wait_idle()


def fmt(color):
    return "(" + ", ".join(f"{c:.4f}" for c in color) + ")"


def run(s):
    # Import (step 5).
    materials = {m["name"]: m for m in s.e.call("get_scene_materials", {"scene_name": s.scene}).get("materials", [])}
    check("the leaf material imported", "Leaf Material" in materials, str(list(materials)))
    details = s.e.call("get_material_details", {"scene_name": s.scene, "material_name": "Leaf Material"})
    check("import: KHR_materials_diffuse_transmission makes a leaf material", details.get("bxdf_model") == "leaf", str(details.get("bxdf_model")))
    check("import: the leaf material is double sided", details.get("double_sided") is True)
    check("import: the transmission factor", abs(details.get("diffuse_transmission", -1.0) - TRANSMISSION) < 1.0e-5, str(details.get("diffuse_transmission")))
    check("import: the transmission color", all(abs(a - b) < 1.0e-5 for a, b in zip(details.get("diffuse_transmission_color", []), TRANSMISSION_COLOR)),
          str(details.get("diffuse_transmission_color")))
    check("import: the back face copies the front base color", all(abs(a - b) < 1.0e-5 for a, b in zip(details.get("back_base_color", []), LEAF_COLOR)),
          str(details.get("back_base_color")))

    front = s.render(EYE_DISTANCE)
    back  = s.render(-EYE_DISTANCE)
    print(f"  front: plain {fmt(front['plain'])} leaf {fmt(front['leaf'])}")
    print(f"  back:  plain {fmt(back['plain'])} leaf {fmt(back['leaf'])}")

    # Step 1: the Plain quad's back face, facing away from the light, shows the ambient light only.
    check("double-sided back face is not lit by a light behind it", sum(back["plain"]) < 0.5 * sum(front["plain"]),
          f"back {fmt(back['plain'])}, front {fmt(front['plain'])}")

    # Step 4: the Leaf's back face shows the transmitted light, tinted green.
    check("leaf back face transmits the light (brighter than the plain back face)", back["leaf"][1] > 2.0 * back["plain"][1],
          f"leaf {fmt(back['leaf'])}, plain {fmt(back['plain'])}")
    check("leaf transmission takes the transmission color", back["leaf"][1] > 2.0 * back["leaf"][0], fmt(back["leaf"]))

    # Steps 2 and 4: the Blocker faces away from the light; its shadow removes the transmitted light.
    s.move("Blocker", [LEAF_X, 0.0, 1.5])
    blocked = s.render(-EYE_DISTANCE)
    print(f"  back, blocked: leaf {fmt(blocked['leaf'])}")
    check("a double-sided caster facing away from the light shadows the leaf", blocked["leaf"][1] < 0.5 * back["leaf"][1],
          f"blocked {fmt(blocked['leaf'])}, open {fmt(back['leaf'])}")
    s.move("Blocker", [50.0, 0.0, 0.0])
    reopened = s.render(-EYE_DISTANCE)
    check("the transmitted light returns when the blocker moves away", abs(reopened["leaf"][1] - back["leaf"][1]) < 1.0e-3 * max(1.0, back["leaf"][1]),
          f"{fmt(reopened['leaf'])} vs {fmt(back['leaf'])}")

    # A closed leaf: transmitted light enters through its far face, a thickness behind the visible one.
    slab = s.e.call("get_material_details", {"scene_name": s.scene, "material_name": "Slab Material"})
    check("import: KHR_materials_volume gives the thickness", abs(slab.get("thickness", -1.0) - SLAB_THICKNESS) < 1.0e-5, str(slab.get("thickness")))
    print(f"  back: slab {fmt(back['slab'])}")
    check("a closed leaf with its thickness transmits the light", back["slab"][1] > 0.8 * back["leaf"][1],
          f"slab {fmt(back['slab'])}, single-layer leaf {fmt(back['leaf'])}")
    s.e.call("edit_material", {"scene_name": s.scene, "material_name": "Slab Material", "thickness": 0.0})
    s.e.wait_idle()
    thin = s.render(-EYE_DISTANCE)
    check("a closed leaf of thickness 0 shadows its own transmitted light", thin["slab"][1] < 0.25 * back["slab"][1],
          f"thickness 0 {fmt(thin['slab'])}, thickness {SLAB_THICKNESS} {fmt(back['slab'])}")

    # Transmission 0 turns the back face into the plain diffuse back.
    s.e.call("edit_material", {"scene_name": s.scene, "material_name": "Leaf Material", "diffuse_transmission": 0.0})
    s.e.wait_idle()
    opaque = s.render(-EYE_DISTANCE)
    check("diffuse_transmission 0 removes the transmitted light", opaque["leaf"][1] < 0.5 * back["leaf"][1],
          f"{fmt(opaque['leaf'])} vs {fmt(back['leaf'])}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", default=DEFAULT_EDITOR)
    args = parser.parse_args()

    work_dir = tempfile.mkdtemp(prefix="leaf_material_verify_")
    editor = Editor(args.editor)
    try:
        editor.call("reset_editor_state")
        editor.wait_idle()
        scene = editor.open_foreign(write_scene(work_dir))
        editor.call("create_light", {"scene_name": scene, "name": "Sun", "type": "directional", "intensity": 3.0, "position": [0.0, 0.0, 10.0]})
        editor.wait_idle()
        editor.call("set_node_transform", {"scene_name": scene, "node_name": "Sun", "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]})
        editor.wait_idle()
        run(Session(editor, work_dir, scene))
    finally:
        editor.stop()
        shutil.rmtree(work_dir, ignore_errors=True)
    failed = [label for label, ok in pav.RESULTS if not ok]
    print(f"\n{len(pav.RESULTS) - len(failed)}/{len(pav.RESULTS)} checks passed")
    for label in failed:
        print(f"  FAILED: {label}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
