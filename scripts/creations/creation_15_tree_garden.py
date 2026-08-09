#!/usr/bin/env python3
"""Creation 15: Tree Garden.

An arboretum of the 28 tree species native to (or long naturalized in)
Finland, each grown to its REAL recorded height - from the 45.0 m
Metsäkuusi (Picea abies) down to the 5.5 m Suippuorapihlaja (Crataegus
rhipidophylla) - arranged in four height-sorted rows, tallest at the
back. Showcases the shared L-system tree module (lsystem_trees.py):
four growth habits (excurrent conifer whorls, stochastic bracket
L-system broadleaf, columnar evergreen, multi-stem shrub), all built
from unit-geometry brush instances batched one place_brush_instances
call per tree.

Static display: no physics rig, every tree part is motion_mode "none".
"""

import math
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from common import Creation, standard_args, quat_mul  # noqa: E402
from lsystem_trees import (  # noqa: E402
    broadleaf_species_params, grow_columnar, grow_conifer, grow_shrub,
    grow_tree,
)

# (finnish, scientific, height_m, habit, bark, leaf, kwargs)
# "Panmarjakuusi" in the source list is read as Euroopanmarjakuusi
# (Taxus baccata).
SPECIES = [
    ("Metsämänty", "Pinus sylvestris", 42.1, "conifer", "bark_orange", "leaf_pine",
     dict(crown_frac=0.42, droop=0.04, whorl_branches=6, whorl_step_frac=0.045,
          branch_len_frac=0.17, shape=0.8)),
    ("Metsäkuusi", "Picea abies", 45.0, "conifer", "bark_dark", "leaf_dark",
     dict(crown_frac=0.93, droop=-0.35, whorl_branches=7, whorl_step_frac=0.038,
          branch_len_frac=0.15, shape=1.1, tip_rise=0.45)),
    ("Kotikataja", "Juniperus communis", 15.4, "columnar", "bark_grey", "leaf_grey",
     dict(width_frac=0.10, lobes=6)),
    ("Euroopanmarjakuusi", "Taxus baccata", 8.4, "conifer", "bark_dark", "leaf_dark",
     dict(crown_frac=0.90, droop=-0.05, whorl_branches=6, whorl_step_frac=0.11,
          branch_len_frac=0.30, shape=0.7)),
    ("Metsälehmus", "Tilia cordata", 36.5, "broadleaf", "bark_brown", "leaf_mid",
     dict(canopy=1.15)),
    ("Metsävaahtera", "Acer platanoides", 30.8, "broadleaf", "bark_grey", "leaf_light",
     dict(spread=1.1, canopy=1.2)),
    ("Vuorijalava", "Ulmus glabra", 35.6, "broadleaf", "bark_grey", "leaf_mid",
     dict(spread=1.25)),
    ("Kynäjalava", "Ulmus laevis", 33.2, "broadleaf", "bark_brown", "leaf_mid",
     dict(spread=1.15)),
    ("Metsätammi", "Quercus robur", 32.0, "broadleaf", "bark_dark", "leaf_mid",
     dict(spread=1.35, gnarl=1.8, canopy=1.2, trunk_frac=0.18, curve_res=3)),
    ("Lehtosaarni", "Fraxinus excelsior", 35.4, "broadleaf", "bark_grey", "leaf_mid",
     dict(canopy=0.9)),
    ("Rauduskoivu", "Betula pendula", 38.5, "broadleaf", "bark_white", "leaf_light",
     dict(spread=0.9, gnarl=0.8, canopy=0.85, trunk_frac=0.30,
          tropism=[0.0, 0.02, 0.0], tip_tropism=[0.0, -0.30, 0.0])),
    ("Hieskoivu", "Betula pubescens", 30.4, "broadleaf", "bark_white", "leaf_mid",
     dict(spread=0.95, canopy=0.9, trunk_frac=0.28)),
    ("Tervaleppä", "Alnus glutinosa", 32.8, "broadleaf", "bark_dark", "leaf_mid",
     dict(spread=0.85, canopy=0.95)),
    ("Harmaaleppä", "Alnus incana", 27.2, "broadleaf", "bark_grey", "leaf_mid",
     dict(spread=0.9)),
    ("Metsähaapa", "Populus tremula", 35.7, "broadleaf", "bark_grey", "leaf_light",
     dict(spread=0.9, trunk_frac=0.30)),
    ("Viitahalava", "Salix pentandra", 22.2, "broadleaf", "bark_brown", "leaf_light",
     dict(spread=1.1)),
    ("Raita", "Salix caprea", 26.2, "broadleaf", "bark_grey", "leaf_mid",
     dict(spread=1.2, canopy=1.15)),
    ("Jokipaju", "Salix triandra", 8.8, "shrub", "bark_grey", "leaf_light",
     dict(stems=5, spread=0.5)),
    ("Mustuvapaju", "Salix myrsinifolia", 15.4, "shrub", "bark_dark", "leaf_dark",
     dict(stems=3, spread=0.3)),
    ("Lehtotuomi", "Prunus padus", 21.8, "broadleaf", "bark_dark", "leaf_mid",
     dict(spread=1.15, canopy=1.1)),
    ("Metsäomenapuu", "Malus sylvestris", 17.0, "broadleaf", "bark_brown", "leaf_mid",
     dict(spread=1.3, gnarl=1.5, canopy=1.15, trunk_frac=0.25)),
    ("Kotipihlaja", "Sorbus aucuparia", 22.8, "broadleaf", "bark_grey", "leaf_mid",
     dict(spread=1.1, canopy=0.95)),
    ("Suomenpihlaja", "Hedlundia hybrida", 11.8, "broadleaf", "bark_grey", "leaf_mid",
     dict(spread=1.1)),
    ("Ruotsinpihlaja", "Scandosorbus intermedia", 19.1, "broadleaf", "bark_grey", "leaf_mid",
     dict(spread=1.05, canopy=1.1)),
    ("Tylppöorapihlaja", "Crataegus monogyna", 8.1, "shrub", "bark_brown", "leaf_mid",
     dict(stems=4, spread=0.4)),
    ("Suippuorapihlaja", "Crataegus rhipidophylla", 5.5, "shrub", "bark_brown", "leaf_mid",
     dict(stems=4, spread=0.45)),
    ("Orapaatsama", "Rhamnus cathartica", 7.0, "shrub", "bark_dark", "leaf_mid",
     dict(stems=5, spread=0.5)),
    ("Korpipaatsama", "Frangula alnus", 8.0, "shrub", "bark_dark", "leaf_light",
     dict(stems=4, spread=0.45)),
]


def main():
    args = standard_args("Tree Garden")
    c = Creation("Tree Garden", port=args.port, pause_s=args.pause,
                 editor_exe=args.editor_exe, reuse=args.reuse)
    scene = c.new_scene()
    print(f"scene: {scene}")

    c.ambience(ambient=[0.18, 0.22, 0.20],
               clear_color=[0.55, 0.70, 0.88, 1.0], grid=False,
               sky={"_version": 3, "enabled": True, "mode": 1})

    def mat(**edits):
        return c.make_material(**edits)

    m = {
        "grass":       mat(base_color=[0.16, 0.30, 0.10], roughness=1.0, metallic=0.0),
        "bark_brown":  mat(base_color=[0.28, 0.19, 0.11], roughness=0.95, metallic=0.0),
        "bark_grey":   mat(base_color=[0.45, 0.44, 0.42], roughness=0.9, metallic=0.0),
        "bark_dark":   mat(base_color=[0.16, 0.13, 0.10], roughness=0.95, metallic=0.0),
        "bark_white":  mat(base_color=[0.85, 0.84, 0.80], roughness=0.8, metallic=0.0),
        "bark_orange": mat(base_color=[0.55, 0.32, 0.15], roughness=0.9, metallic=0.0),
        "leaf_dark":   mat(base_color=[0.06, 0.17, 0.07], roughness=0.9, metallic=0.0),
        "leaf_pine":   mat(base_color=[0.10, 0.24, 0.14], roughness=0.9, metallic=0.0),
        "leaf_mid":    mat(base_color=[0.15, 0.31, 0.09], roughness=0.9, metallic=0.0),
        "leaf_light":  mat(base_color=[0.27, 0.42, 0.12], roughness=0.9, metallic=0.0),
        "leaf_grey":   mat(base_color=[0.19, 0.29, 0.17], roughness=0.9, metallic=0.0),
    }

    c.light("directional", "Afternoon Sun", [0.0, 60.0, 0.0],
            [1.0, 0.94, 0.80], 2.4)
    pitch = math.radians(-135.0)
    yaw = math.radians(-40.0)
    qy = [0.0, math.sin(yaw / 2), 0.0, math.cos(yaw / 2)]
    qx = [math.sin(pitch / 2), 0.0, 0.0, math.cos(pitch / 2)]
    c.set_node_transform("Afternoon Sun", rotation_xyzw=quat_mul(qy, qx))
    c.light("point", "Sky Fill", [0.0, 30.0, 20.0], [0.6, 0.7, 0.9],
            300.0, range=120.0, cast_shadow=False)
    c.shadow_range(160.0)

    c.shape("box", "Lawn", [0.0, -0.25, 0.0], size=[180.0, 0.5, 130.0],
            material_name=m["grass"])

    # Four height-sorted rows of seven, tallest at the back; within a row
    # tallest at the left, so height reads across and into the picture.
    rng = random.Random(15)
    ordered = sorted(SPECIES, key=lambda s: -s[2])
    # Rows compressed so the whole garden stays inside the viewport draw
    # distance (~80 m) from the overview camera.
    rows_z = [-30.0, -19.0, -8.0, 3.0]
    spacing = 12.0
    print(f"{'pos':>8}  {'finnish':<20} {'scientific':<28} height")
    for index, (finnish, scientific, height, habit, bark, leaf, kwargs) in enumerate(ordered):
        row = index // 7
        col = index % 7
        x = (col - 3.0) * spacing + rng.uniform(-1.2, 1.2)
        z = rows_z[row] + rng.uniform(-1.5, 1.5)
        base = [x, 0.0, z]
        tag = finnish
        print(f"({x:5.1f},{z:6.1f})  {finnish:<20} {scientific:<28} {height:.1f} m")
        if habit == "conifer":
            grow_conifer(c, tag, base, height, m[bark], m[leaf], rng, **kwargs)
        elif habit == "columnar":
            grow_columnar(c, tag, base, height, m[bark], m[leaf], rng, **kwargs)
        elif habit == "shrub":
            grow_shrub(c, tag, base, height, m[bark], m[leaf], rng, **kwargs)
        else:
            species = broadleaf_species_params(height, **kwargs)
            grow_tree(c, tag, base, species, m[bark],
                      [m[leaf], m["leaf_mid" if leaf != "leaf_mid" else "leaf_light"]],
                      rng, sway_jobs=None)

    c.settle()
    # Wide lens + close eye: the far row must stay inside the ~80 m draw
    # distance (the first framing at 100 m lost the back row entirely).
    cameras = c.call("get_scene_cameras", {"scene_name": c.scene}).get("cameras", [])
    if cameras:
        c.mutate("edit_camera", {
            "scene_name": c.scene,
            "camera_name": cameras[0].get("name") or cameras[0].get("node"),
            "fov_y": 1.15,
        })
    c.place_camera([0.0, 20.0, 36.0], [0.0, 13.0, -10.0])
    c.screenshot("logs/creations/tree_garden.png")
    c.place_camera([-34.0, 8.0, 26.0], [8.0, 12.0, -18.0])
    c.screenshot("logs/creations/tree_garden_low.png")
    if not args.no_save:
        c.save("res/editor/scenes/creations/tree_garden.glb")
    print("Tree Garden complete.")


if __name__ == "__main__":
    main()
