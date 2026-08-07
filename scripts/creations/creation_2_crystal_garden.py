#!/usr/bin/env python3
"""Creation 2: Crystal Garden at Night.

A dark reflective ground sprouting clusters of emissive crystals. Each
cluster is a geometry node graph: points distributed over a hidden dome,
each instanced with a sharpened cone, realized into one mesh. Clusters get
distinct emissive hues with matching point lights, so the garden glows.
Showcases: geometry-graph instancing (distribute/instance/realize),
emissive materials, mood lighting.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from common import Creation, hsv_to_rgb, standard_args  # noqa: E402


CLUSTERS = [
    # (x, z, hue, count, cluster scale)
    (0.0,  0.0, 0.50, 42, 1.6),   # cyan centerpiece
    (4.5,  1.5, 0.83, 26, 1.0),   # magenta
    (-4.0, 2.5, 0.08, 22, 0.9),   # amber
    (2.0, -4.5, 0.62, 30, 1.1),   # blue-violet
    (-3.0, -3.5, 0.33, 20, 0.8),  # green
    (5.5, -2.0, 0.95, 16, 0.7),   # pink
]


def build_cluster(c, index, count, material_name):
    name = f"Crystal Cluster {index}"
    g = c.geometry_graph(name)
    dome = g.add("sphere")
    flatten = g.add("transform", {"scale": [1.2, 0.45, 1.2]})
    dist = g.add("distribute", {"count": count, "seed": 7 * index + 1})
    spike = g.add("cone", {"height": 1.8, "radius": 0.16, "slices": 6})
    inst = g.add("instance", {"scale": 0.7, "align": True})
    real = g.add("realize")
    out = g.add("output", {"material": material_name})
    g.link(dome, flatten)
    g.link(flatten, dist)
    g.link(dist, inst)
    g.link(spike, inst, dst_slot=1)
    g.link(inst, real)
    g.link(real, out)
    c.call("get_geometry_graph")
    return name


def main():
    args = standard_args("Crystal Garden at Night")
    c = Creation("Crystal Garden", port=args.port)
    scene = c.new_scene()
    print(f"scene: {scene}")

    # Night mood: near-black sky, minimal ambient.
    c.ambience(ambient=[0.015, 0.015, 0.03], clear_color=[0.01, 0.01, 0.03, 1.0])

    # Dark glossy ground.
    ground = c.make_material(base_color=[0.03, 0.03, 0.05], roughness=0.15,
                             metallic=0.4, reflectance=0.9)
    c.shape("box", "Garden Ground", [0.0, -0.25, 0.0], size=[24.0, 0.5, 24.0],
            material_name=ground)

    for i, (x, z, hue, count, scale) in enumerate(CLUSTERS):
        rgb = hsv_to_rgb(hue, 0.85, 1.0)
        crystal_mat = c.make_material(
            base_color=[0.05 + 0.1 * rgb[0], 0.05 + 0.1 * rgb[1], 0.05 + 0.1 * rgb[2]],
            roughness=0.1, metallic=0.0, reflectance=0.9,
            emissive=[rgb[0] * 3.0, rgb[1] * 3.0, rgb[2] * 3.0])

        cluster = build_cluster(c, i, count, crystal_mat)
        node = c.bind_node_mesh(f"Cluster Node {i}", cluster)
        c.move_node(node, translation=[x, 0.0, z], scale=[scale, scale * 1.6, scale])

        # Assign the emissive material to the realized mesh: rebind via
        # edit on the graph output is not material-aware, so instead place
        # a few hand-made crystal shards with the material around the base.
        for k in range(5):
            shard_angle = (2.0 * math.pi * k) / 5.0 + i
            sx = x + (0.9 * scale) * math.cos(shard_angle)
            sz = z + (0.9 * scale) * math.sin(shard_angle)
            height = 0.5 + 0.45 * ((k * 37 + i * 11) % 5) / 4.0
            c.shape("cone", f"Shard {i}-{k}", [sx, 0.0, sz],
                    height=height * scale * 1.8, bottom_radius=0.14 * scale,
                    top_radius=0.0, slice_count=6, use_top=False,
                    material_name=crystal_mat)

        c.light("point", f"Glow {i}", [x, 1.2 * scale, z], list(rgb),
                12.0 * scale, range=10.0 * scale, cast_shadow=False)

    # A faint cool moon so silhouettes read.
    c.light("directional", "Moonlight", [0.0, 30.0, 0.0], [0.35, 0.42, 0.65], 0.35)

    c.settle()
    c.place_camera([9.0, 5.0, 10.5], [0.0, 0.8, 0.0])
    c.screenshot("logs/creations/crystal_garden.png")
    if not args.no_save:
        c.save("res/editor/scenes/creations/crystal_garden.glb")
    print("Crystal Garden complete.")


if __name__ == "__main__":
    main()
