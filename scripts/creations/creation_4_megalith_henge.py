#!/usr/bin/env python3
"""Creation 4: Megalith Henge at Dusk.

A weathered stone circle built from brushes: a monolith brush and a
capstone brush are authored once with create_shape (add_brush), then
place_brush erects a ring of trilithons around a central altar. The stone
surface is a procedural texture graph (noise-grimed granite) bound to the
megalith material. Low orange sun + cold ambient = dusk.
Showcases: brush authoring + placement, texture graph materials, mood.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from common import Creation, standard_args  # noqa: E402


TRILITHONS = 7          # pairs of uprights + capstone
RING_RADIUS = 8.0
UPRIGHT_H = 3.4
UPRIGHT_W = 1.1
UPRIGHT_D = 0.8
CAP_L = 2.6


def grad(stops, interpolation=1):
    return {"interpolation": interpolation,
            "stops": [{"pos": p, "color": list(c)} for p, c in stops]}


def granite_graph(c):
    g = c.texture_graph("Granite")
    base = g.add("fbm", {"noise": 1, "scale_x": 7.0, "scale_y": 7.0, "iterations": 6.0})
    speck = g.add("noise", {"size": 7, "density": 0.4})
    mix = g.add("math", {"op": 0, "clamp": True})
    color = g.add("colorize", {"gradient": grad([
        (0.00, [0.13, 0.13, 0.14, 1.0]),
        (0.40, [0.32, 0.31, 0.30, 1.0]),
        (0.70, [0.45, 0.44, 0.42, 1.0]),
        (1.00, [0.58, 0.57, 0.53, 1.0])])})
    out = g.add("output", {"name": "Granite", "size": 1024})
    g.link(base, mix)
    g.link(speck, mix, dst_slot=1)
    g.link(mix, color)
    g.link(color, out, dst_slot=2)
    return "Granite"


def main():
    args = standard_args("Megalith Henge at Dusk")
    c = Creation("Megalith Henge", port=args.port)
    scene = c.new_scene()
    print(f"scene: {scene}")

    # Dusk mood.
    c.ambience(ambient=[0.09, 0.08, 0.13], grid=False)

    granite = granite_graph(c)
    stone = c.make_material(base_color=[0.9, 0.9, 0.9], roughness=0.95, metallic=0.0)
    c.bind_material_texture(stone, granite, slot="base_color")
    earth = c.make_material(base_color=[0.16, 0.13, 0.09], roughness=1.0)

    # Ground.
    c.shape("box", "Moor", [0.0, -0.3, 0.0], size=[34.0, 0.6, 34.0], material_name=earth)

    # Author the brushes once (no instance), then place them.
    c.shape("box", "Monolith", [0.0, -100.0, 0.0], instance=False, add_brush=True,
            size=[UPRIGHT_W, UPRIGHT_H, UPRIGHT_D], steps=[2, 4, 2], power=0.85)
    c.shape("box", "Capstone", [0.0, -100.0, 0.0], instance=False, add_brush=True,
            size=[CAP_L, 0.7, UPRIGHT_D + 0.2], steps=[3, 1, 2], power=0.8)
    c.shape("box", "Altar Slab", [0.0, -100.0, 0.0], instance=False, add_brush=True,
            size=[2.4, 0.6, 1.3], steps=[2, 1, 2], power=0.75)
    c.settle()

    monolith = c.brush_id("Monolith")
    capstone = c.brush_id("Capstone")
    altar = c.brush_id("Altar Slab")
    if None in (monolith, capstone, altar):
        raise RuntimeError(f"brushes missing: {[b['name'] for b in c.brushes()]}")

    def yaw_quat(angle_rad):
        return [0.0, math.sin(angle_rad / 2.0), 0.0, math.cos(angle_rad / 2.0)]

    def place_rotated(brush, position, yaw_rad, scale=1.0):
        result = c.place(brush, position, material_name=stone, scale=scale)
        node_id = result.get("node_id") if isinstance(result, dict) else None
        if node_id is not None:
            c.move_node_id(node_id, rotation_xyzw=yaw_quat(yaw_rad))

    gap = UPRIGHT_W * 1.15
    for i in range(TRILITHONS):
        angle = (2.0 * math.pi * i) / TRILITHONS
        cx = RING_RADIUS * math.cos(angle)
        cz = RING_RADIUS * math.sin(angle)
        # tangent direction for the pair offset; stones face the center
        tx, tz = -math.sin(angle), math.cos(angle)
        yaw = -angle + math.pi / 2.0  # local X axis along the tangent
        for side in (-1.0, 1.0):
            px = cx + side * (gap * 0.5) * tx
            pz = cz + side * (gap * 0.5) * tz
            place_rotated(monolith, [px, UPRIGHT_H * 0.5, pz], yaw)
        place_rotated(capstone, [cx, UPRIGHT_H + 0.35, cz], yaw)

    # Inner ring of shorter stones.
    for i in range(9):
        angle = (2.0 * math.pi * i) / 9.0 + 0.3
        px = 4.4 * math.cos(angle)
        pz = 4.4 * math.sin(angle)
        c.place(monolith, [px, UPRIGHT_H * 0.3, pz], material_name=stone, scale=0.6)

    # Central altar.
    c.place(altar, [0.0, 0.3, 0.0], material_name=stone)

    # Dusk lighting: low warm sun + cold counter light + ember glow on altar.
    sun = c.light("directional", "Setting Sun", [0.0, 10.0, 0.0], [1.0, 0.45, 0.18], 2.2)
    # Aim the sun low across the henge: rotate its node.
    sun_node = None
    for node in c.nodes():
        if node.get("name") == "Setting Sun":
            sun_node = node
            break
    if sun_node is not None:
        # pitch ~ -12 degrees from horizon, yaw toward the ring
        import math as m
        pitch = m.radians(-168.0)  # light -Z points down-ish toward scene
        # Build a simple pitch quaternion about X after a yaw about Y.
        yaw = m.radians(35.0)
        qy = [0.0, m.sin(yaw / 2), 0.0, m.cos(yaw / 2)]
        qx = [m.sin(pitch / 2), 0.0, 0.0, m.cos(pitch / 2)]
        q = [
            qy[3] * qx[0] + qy[0] * qx[3] + qy[1] * qx[2] - qy[2] * qx[1],
            qy[3] * qx[1] - qy[0] * qx[2] + qy[1] * qx[3] + qy[2] * qx[0],
            qy[3] * qx[2] + qy[0] * qx[1] - qy[1] * qx[0] + qy[2] * qx[3],
            qy[3] * qx[3] - qy[0] * qx[0] - qy[1] * qx[1] - qy[2] * qx[2],
        ]
        c.select(sun_node["id"])
        c.mutate("transform_selection", {"space": "global", "rotation_xyzw": q})
        c.clear_selection()

    c.light("point", "Altar Embers", [0.0, 1.2, 0.0], [1.0, 0.5, 0.15], 90.0,
            range=16.0)
    c.light("point", "Cold North", [-10.0, 6.0, -8.0], [0.25, 0.35, 0.7], 150.0,
            range=40.0, cast_shadow=False)

    c.settle()
    c.place_camera([17.0, 7.5, 13.0], [0.0, 1.5, 0.0])
    c.screenshot("logs/creations/megalith_henge.png")
    if not args.no_save:
        c.save("res/editor/scenes/creations/megalith_henge.glb")
    print("Megalith Henge complete.")


if __name__ == "__main__":
    main()
