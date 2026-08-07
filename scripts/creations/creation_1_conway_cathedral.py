#!/usr/bin/env python3
"""Creation 1: Cathedral of Conway.

A circular colonnade of pedestals, each carrying a geometry-node sculpture
built from a different Conway operator chain (gyro, kis, chamfer, truncate,
meta, ambo-dual...), around a large central gyro-sphere centerpiece.
Showcases: geometry node graphs (per-operator conway nodes, subdivision,
transforms), graph-mesh scene binding, material variety, lighting.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from common import Creation, hsv_to_rgb, standard_args  # noqa: E402


# Each pedestal sculpture: (label, source primitive, [(node_type, params), ...])
SCULPTURES = [
    ("Gyro",      "sphere", [("conway_gyro", {"gyro_ratio": 0.6})]),
    ("Kis",       "box",    [("conway_kis", {"kis_height": 0.6}), ("subdivide", {"mode": 1, "iterations": 1})]),
    ("Chamfer",   "box",    [("conway_chamfer", {"chamfer_ratio": 0.4}), ("conway_dual", {})]),
    ("Truncate",  "box",    [("conway_truncate", {"truncate_ratio": 0.35})]),
    ("Meta",      "sphere", [("conway_meta", {})]),
    ("Ambo Dual", "box",    [("conway_ambo", {}), ("conway_dual", {}), ("subdivide", {"mode": 0, "iterations": 1})]),
    ("Join Kis",  "torus",  [("conway_join", {}), ("conway_kis", {"kis_height": 0.25})]),
    ("Gyro Kis",  "box",    [("conway_gyro", {"gyro_ratio": 0.5}), ("conway_kis", {"kis_height": 0.15})]),
]

RING_RADIUS = 7.0
PEDESTAL_H = 1.2


def build_sculpture_graph(c, name, source, chain_ops, material_name):
    g = c.geometry_graph(name)
    if source == "sphere":
        src = g.add("sphere")
    elif source == "torus":
        src = g.add("torus")
    else:
        src = g.add("box", {"size": [1.4, 1.4, 1.4]})
    ids = [src]
    for op_type, params in chain_ops:
        ids.append(g.add(op_type, params or None))
    normalize = g.add("normalize")
    out = g.add("output", {"material": material_name})
    ids += [normalize, out]
    g.chain(ids)
    c.call("get_geometry_graph")  # settle evaluation
    return name


def main():
    args = standard_args("Cathedral of Conway")
    c = Creation("Conway Cathedral", port=args.port, pause_s=args.pause, editor_exe=args.editor_exe, reuse=args.reuse)
    scene = c.new_scene()
    print(f"scene: {scene}")

    c.ambience(ambient=[0.10, 0.10, 0.14], grid=False)

    # Materials: dark polished floor, stone pedestals, one hue per sculpture.
    floor_mat = c.make_material(base_color=[0.10, 0.10, 0.12], roughness=0.35,
                                metallic=0.1, reflectance=0.7)
    stone = c.make_material(base_color=[0.42, 0.40, 0.38], roughness=0.9, metallic=0.0)
    sculpture_mats = []
    for i in range(len(SCULPTURES)):
        rgb = hsv_to_rgb(i / len(SCULPTURES), 0.7, 0.9)
        sculpture_mats.append(c.make_material(
            base_color=list(rgb), roughness=0.35, metallic=0.6, reflectance=0.6))

    # Floor: broad, flat slab.
    c.shape("box", "Cathedral Floor", [0.0, -0.25, 0.0],
            size=[22.0, 0.5, 22.0], material_name=floor_mat)

    # Pedestals + sculptures around the ring.
    for i, (label, source, chain_ops) in enumerate(SCULPTURES):
        angle = (2.0 * math.pi * i) / len(SCULPTURES)
        x = RING_RADIUS * math.cos(angle)
        z = RING_RADIUS * math.sin(angle)
        c.shape("box", f"Pedestal {label}", [x, PEDESTAL_H * 0.5, z],
                size=[1.6, PEDESTAL_H, 1.6], material_name=stone)
        c.shape("box", f"Pedestal Cap {label}", [x, PEDESTAL_H + 0.05, z],
                size=[1.9, 0.1, 1.9], material_name=stone)

        graph_name = f"Sculpture {label}"
        build_sculpture_graph(c, graph_name, source, chain_ops, sculpture_mats[i])
        node_name = c.bind_node_mesh(f"Node {label}", graph_name)
        c.move_node(node_name, translation=[x, PEDESTAL_H + 1.15, z])

    # Centerpiece: big gyro'd, subdivided sphere on a wide plinth.
    c.shape("box", "Central Plinth", [0.0, 0.4, 0.0], size=[3.2, 0.8, 3.2], material_name=stone)
    center_mat = c.make_material(base_color=[0.9, 0.75, 0.3], roughness=0.2,
                                 metallic=1.0, reflectance=0.8)
    g = c.geometry_graph("Centerpiece")
    sphere = g.add("sphere")
    gyro = g.add("conway_gyro", {"gyro_ratio": 0.7})
    kis = g.add("conway_kis", {"kis_height": 0.1})
    norm = g.add("normalize")
    out = g.add("output", {"material": center_mat})
    g.chain([sphere, gyro, kis, norm, out])
    c.call("get_geometry_graph")
    node = c.bind_node_mesh("Centerpiece Node", "Centerpiece")
    c.move_node(node, translation=[0.0, 2.6, 0.0], scale=[1.8, 1.8, 1.8])

    # Lighting: warm key + cool fill points, plus a dim directional.
    c.light("directional", "Moon", [0.0, 20.0, 0.0], [0.6, 0.7, 0.9], 1.6)
    c.light("point", "Warm Key", [4.0, 6.0, 4.0], [1.0, 0.75, 0.45], 400.0, range=50.0)
    c.light("point", "Cool Fill", [-5.0, 4.0, -3.0], [0.4, 0.55, 1.0], 250.0, range=50.0,
            cast_shadow=False)
    c.light("point", "Center Glow", [0.0, 4.5, 0.0], [1.0, 0.9, 0.6], 150.0, range=25.0,
            cast_shadow=False)

    c.settle()
    c.place_camera([13.0, 8.5, 13.0], [0.0, 1.6, 0.0])
    c.screenshot("logs/creations/conway_cathedral.png")
    if not args.no_save:
        c.save("res/editor/scenes/creations/conway_cathedral.glb")
    print("Conway Cathedral complete.")


if __name__ == "__main__":
    main()
