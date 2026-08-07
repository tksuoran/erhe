#!/usr/bin/env python3
"""Creation 3: The Texture Atelier.

A gallery of large spheres on plinths, each surfaced live by a different
procedural texture graph (marble, molten lava, ancient bricks with a
normal map, voronoi stained glass, woven fabric, kaleidoscope) bound to
its material through set_material_texture_source - editing a graph would
update the sphere live. Showcases: texture node graphs end-to-end.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from common import Creation, standard_args  # noqa: E402


def grad(stops, interpolation=1):
    return {"interpolation": interpolation,
            "stops": [{"pos": p, "color": list(c)} for p, c in stops]}


def marble(c):
    g = c.texture_graph("Marble")
    noise = g.add("perlin", {"scale_x": 4.0, "scale_y": 4.0, "iterations": 7.0, "persistence": 0.65})
    veins = g.add("tones_range", {"value": 0.5, "width": 0.16, "contrast": 0.85, "invert": True})
    color = g.add("colorize", {"gradient": grad([
        (0.00, [0.93, 0.93, 0.95, 1.0]),
        (0.55, [0.80, 0.81, 0.86, 1.0]),
        (0.85, [0.25, 0.28, 0.38, 1.0]),
        (1.00, [0.10, 0.12, 0.20, 1.0])])})
    out = g.add("output", {"name": "Marble", "size": 1024})
    g.link(noise, veins)
    g.link(veins, color)
    g.link(color, out, dst_slot=2)
    return "Marble"


def lava(c):
    g = c.texture_graph("Lava")
    noise = g.add("fbm", {"noise": 1, "scale_x": 5.0, "scale_y": 5.0, "iterations": 6.0})
    color = g.add("colorize", {"gradient": grad([
        (0.00, [0.02, 0.01, 0.01, 1.0]),
        (0.45, [0.10, 0.02, 0.01, 1.0]),
        (0.62, [0.85, 0.15, 0.02, 1.0]),
        (0.80, [1.00, 0.55, 0.05, 1.0]),
        (1.00, [1.00, 0.95, 0.55, 1.0])])})
    out = g.add("output", {"name": "Lava", "size": 1024})
    g.link(noise, color)
    g.link(color, out, dst_slot=2)
    return "Lava"


def bricks(c):
    g = c.texture_graph("Ancient Bricks")
    pattern = g.add("bricks", {"rows": 8.0, "columns": 4.0})
    dirt = g.add("perlin", {"scale_x": 9.0, "scale_y": 9.0, "iterations": 4.0})
    mix = g.add("math", {"op": 2, "clamp": True})       # multiply brick mask by grime
    color = g.add("colorize", {"gradient": grad([
        (0.00, [0.16, 0.10, 0.08, 1.0]),
        (0.50, [0.45, 0.22, 0.15, 1.0]),
        (1.00, [0.70, 0.45, 0.32, 1.0])])})
    out = g.add("output", {"name": "Bricks Albedo", "size": 1024})
    g.link(pattern, mix)
    g.link(dirt, mix, dst_slot=1)
    g.link(mix, color)
    g.link(color, out, dst_slot=2)
    return "Ancient Bricks"


def bricks_normal(c):
    g = c.texture_graph("Bricks Normal")
    pattern = g.add("bricks", {"rows": 8.0, "columns": 4.0})
    normal = g.add("normal_map", {"amount": 1.0, "size": 10})
    out = g.add("output", {"name": "Bricks Normal", "size": 1024})
    g.link(pattern, normal)       # normal_map takes the grayscale height field
    g.link(normal, out, dst_slot=1)
    return "Bricks Normal"


def stained_glass(c):
    g = c.texture_graph("Stained Glass")
    cells = g.add("voronoi", {"scale_x": 6.0, "scale_y": 6.0, "randomness": 0.9})
    edges = g.add("tones_range", {"value": 0.05, "width": 0.08, "contrast": 0.9, "invert": True})
    color = g.add("colorize", {"gradient": grad([
        (0.00, [0.85, 0.10, 0.10, 1.0]),
        (0.25, [0.95, 0.60, 0.05, 1.0]),
        (0.50, [0.10, 0.60, 0.20, 1.0]),
        (0.75, [0.10, 0.30, 0.85, 1.0]),
        (1.00, [0.55, 0.10, 0.70, 1.0])])})
    lead = g.add("blend", {"blend_type": 2, "amount": 1.0})  # multiply lead lines over color
    out = g.add("output", {"name": "Stained Glass", "size": 1024})
    g.link(cells, edges)          # slot 0: distance field
    g.link(cells, color, src_slot=1)  # slot 1: per-cell random value
    g.link(color, lead)
    edges_rgba = g.add("ensure_rgba")
    g.link(edges, edges_rgba)
    g.link(edges_rgba, lead, dst_slot=1)
    g.link(lead, out, dst_slot=2)
    return "Stained Glass"


def woven(c):
    g = c.texture_graph("Woven Fabric")
    weave = g.add("weave", {"columns": 10.0, "rows": 10.0, "width": 0.85})
    color = g.add("colorize", {"gradient": grad([
        (0.00, [0.06, 0.05, 0.10, 1.0]),
        (0.60, [0.35, 0.12, 0.45, 1.0]),
        (1.00, [0.85, 0.65, 0.25, 1.0])])})
    out = g.add("output", {"name": "Woven", "size": 1024})
    g.link(weave, color)
    g.link(color, out, dst_slot=2)
    return "Woven Fabric"


def kaleido(c):
    g = c.texture_graph("Kaleidoscope")
    cells = g.add("voronoi", {"scale_x": 5.0, "scale_y": 5.0, "randomness": 1.0})
    color = g.add("colorize", {"gradient": grad([
        (0.00, [0.05, 0.00, 0.25, 1.0]),
        (0.25, [0.80, 0.10, 0.55, 1.0]),
        (0.50, [1.00, 0.65, 0.10, 1.0]),
        (0.75, [0.10, 0.85, 0.75, 1.0]),
        (1.00, [0.30, 0.10, 0.90, 1.0])], interpolation=0)})
    swirl = g.add("swirl", {"angle": 150.0, "radius": 0.9})
    kal = g.add("kaleidoscope", {"count": 10.0})
    bright = g.add("brightness_contrast", {"brightness": 0.05, "contrast": 1.3})
    out = g.add("output", {"name": "Kaleidoscope", "size": 1024})
    g.link(cells, color, src_slot=1)
    g.chain([color, swirl, kal, bright])
    g.link(bright, out, dst_slot=2)
    return "Kaleidoscope"


EXHIBITS = [
    # (builder(s), display name, extra material edits, emissive graph?)
    (marble,        "Marble",        {"roughness": 0.15, "metallic": 0.0, "reflectance": 0.7}),
    (lava,          "Lava",          {"roughness": 0.6, "metallic": 0.0}),
    (bricks,        "Ancient Bricks", {"roughness": 0.95, "metallic": 0.0}),
    (stained_glass, "Stained Glass", {"roughness": 0.05, "metallic": 0.0, "reflectance": 0.9}),
    (woven,         "Woven Fabric",  {"roughness": 0.85, "metallic": 0.0}),
    (kaleido,       "Kaleidoscope",  {"roughness": 0.3, "metallic": 0.3}),
]

SPACING = 3.2
SPHERE_R = 1.1
PLINTH_H = 1.0


def main():
    args = standard_args("The Texture Atelier")
    c = Creation("Texture Atelier", port=args.port)
    scene = c.new_scene()
    print(f"scene: {scene}")

    c.ambience(ambient=[0.16, 0.16, 0.18], grid=False)

    hall = c.make_material(base_color=[0.22, 0.22, 0.24], roughness=0.6)
    c.shape("box", "Gallery Floor", [0.0, -0.25, 0.0], size=[26.0, 0.5, 12.0], material_name=hall)
    c.shape("box", "Gallery Wall", [0.0, 3.0, -3.5], size=[26.0, 6.5, 0.4], material_name=hall)

    count = len(EXHIBITS)
    for i, (builder, label, mat_edits) in enumerate(EXHIBITS):
        x = (i - (count - 1) / 2.0) * SPACING
        graph_name = builder(c)

        mat = c.make_material(base_color=[1.0, 1.0, 1.0], **mat_edits)
        c.bind_material_texture(mat, graph_name, slot="base_color")
        if label == "Ancient Bricks":
            normal_graph = bricks_normal(c)
            c.bind_material_texture(mat, normal_graph, slot="normal")
        if label == "Lava":
            c.mutate("edit_material", {"scene_name": c.scene, "material_name": mat,
                                       "emissive": [1.2, 0.35, 0.05]})
            c.bind_material_texture(mat, graph_name, slot="emissive")

        c.shape("box", f"Plinth {label}", [x, PLINTH_H * 0.5, 0.0],
                size=[1.6, PLINTH_H, 1.6], material_name=hall)
        c.shape("uv_sphere", f"Exhibit {label}", [x, PLINTH_H + SPHERE_R, 0.0],
                radius=SPHERE_R, slice_count=48, stack_count=32, material_name=mat)

    c.light("directional", "Skylight", [0.0, 20.0, 5.0], [1.0, 0.98, 0.92], 2.6)
    c.light("point", "Warm Wash", [0.0, 6.0, 7.0], [1.0, 0.9, 0.75], 350.0, range=60.0,
            cast_shadow=False)

    c.settle()
    c.place_camera([0.0, 4.2, 17.0], [0.0, 1.8, 0.0])
    c.screenshot("logs/creations/texture_atelier.png")
    if not args.no_save:
        c.save("res/editor/scenes/creations/texture_atelier.glb")
    print("Texture Atelier complete.")


if __name__ == "__main__":
    main()
