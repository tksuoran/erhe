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

Physics: hierarchical wind rig (glade recipe, two levels) - every trunk
is a REAL-geometry sway spine (dynamic body, gravity 0, wind-receptive,
rest-pose motor joint to the tree's root group) and every MAJOR limb
(pipe-model share >= 12% of the tree) is a second-level spine jointed to
the trunk. Masses and receptivity scale with real height, so the 45 m
spruce lumbers while the shrub stems whip. All spine bodies share one
self-denylisting collision filter (rig_tree_sway) - sibling stems/limbs
no longer jitter from permanently interpenetrating hulls - and the
columnar juniper + shrubs get stiffness-scaled joint settings so they
stir instead of wobbling. Built with the simulation OFF so the joints
capture the authored pose; then wind + physics on.
Each trunk rises from a root flare with surface roots; lower branches
ladder evenly from mid-trunk to the crown as ARCHING sub-cone chains
(some as broken stubs on the old oak/apple/elm); broadleaf crown
segments draw at curve_res 3 and conifer boughs as curved 2-segment
chains, so branches bend instead of reading as straight sticks; the
cluster species (Hieskoivu, Harmaaleppä, Raita, Lehtotuomi) grow 2-3
staggered trunks from a shared root mound.
"""

import math
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from common import Creation, standard_args, quat_mul, probe_tilt  # noqa: E402
from lsystem_trees import (  # noqa: E402
    broadleaf_species_params, grow_columnar, grow_conifer, grow_roots,
    grow_shrub, grow_tree, rig_tree_sway, sway_setting_for_height,
)

def tree_sway_config(c, height):
    """Height-scaled trunk-level rig (glade recipe): trunk mass and
    receptivity grow with the tree, so the 45 m spruce lumbers while the
    17 m apple stirs. branch_sway makes major limbs (pipe share >= 12%)
    second-level spines jointed to the trunk with the ragdoll two-anchor
    pivot (the single-anchor joint put the trunk-side constraint frame at
    the trunk BASE and crowns slumped); their stiffness comes from the
    beam rule keyed by limb radius, so stiffness keeps dropping steeply
    toward thinner branches. trunk_settings comes from
    sway_setting_for_height (thick = stiff and slow, thin = soft and
    lively)."""
    return {
        "trunk_settings": sway_setting_for_height(c, height),
        "trunk_mass": 3.0 * height,
        "trunk_receptivity": 0.9 * height,
        "branch_sway": True,
    }

# (finnish, scientific, height_m, habit, bark, leaf, kwargs)
# "Panmarjakuusi" in the source list is read as Euroopanmarjakuusi
# (Taxus baccata).
SPECIES = [
    ("Metsämänty", "Pinus sylvestris", 42.1, "conifer", "bark_orange", "leaf_pine",
     dict(crown_frac=0.42, droop=0.04, whorl_branches=6, whorl_step_frac=0.045,
          branch_len_frac=0.17, shape=0.8, sparse_lower=3)),
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
     dict(spread=1.25, stubs=0.22)),
    ("Kynäjalava", "Ulmus laevis", 33.2, "broadleaf", "bark_brown", "leaf_mid",
     dict(spread=1.15)),
    ("Metsätammi", "Quercus robur", 32.0, "broadleaf", "bark_dark", "leaf_mid",
     dict(spread=1.35, gnarl=1.8, canopy=1.2, trunk_frac=0.18, curve_res=3,
          stubs=0.35)),
    ("Lehtosaarni", "Fraxinus excelsior", 35.4, "broadleaf", "bark_grey", "leaf_mid",
     dict(canopy=0.9)),
    ("Rauduskoivu", "Betula pendula", 38.5, "broadleaf", "bark_white", "leaf_light",
     dict(spread=0.9, gnarl=0.8, canopy=0.85, trunk_frac=0.30,
          tropism=[0.0, 0.02, 0.0], tip_tropism=[0.0, -0.30, 0.0])),
    ("Hieskoivu", "Betula pubescens", 30.4, "broadleaf", "bark_white", "leaf_mid",
     dict(spread=0.95, canopy=0.9, trunk_frac=0.28, cluster=3)),
    ("Tervaleppä", "Alnus glutinosa", 32.8, "broadleaf", "bark_dark", "leaf_mid",
     dict(spread=0.85, canopy=0.95)),
    ("Harmaaleppä", "Alnus incana", 27.2, "broadleaf", "bark_grey", "leaf_mid",
     dict(spread=0.9, cluster=2)),
    ("Metsähaapa", "Populus tremula", 35.7, "broadleaf", "bark_grey", "leaf_light",
     dict(spread=0.9, trunk_frac=0.30)),
    ("Viitahalava", "Salix pentandra", 22.2, "broadleaf", "bark_brown", "leaf_light",
     dict(spread=1.1)),
    ("Raita", "Salix caprea", 26.2, "broadleaf", "bark_grey", "leaf_mid",
     dict(spread=1.2, canopy=1.15, cluster=2)),
    ("Jokipaju", "Salix triandra", 8.8, "shrub", "bark_grey", "leaf_light",
     dict(stems=5, spread=0.5)),
    ("Mustuvapaju", "Salix myrsinifolia", 15.4, "shrub", "bark_dark", "leaf_dark",
     dict(stems=3, spread=0.3)),
    ("Lehtotuomi", "Prunus padus", 21.8, "broadleaf", "bark_dark", "leaf_mid",
     dict(spread=1.15, canopy=1.1, cluster=3)),
    ("Metsäomenapuu", "Malus sylvestris", 17.0, "broadleaf", "bark_brown", "leaf_mid",
     dict(spread=1.3, gnarl=1.5, canopy=1.15, trunk_frac=0.25, stubs=0.35)),
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

    # Build with the simulation OFF: the sway joints must capture the
    # authored pose as the rest pose (glade bendy-plant recipe).
    c.set_physics(False)

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
    sway_jobs = []
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
        # Bases lifted 0.05 so the dynamic trunk hulls clear the lawn's
        # static body instead of grinding on it (glade recipe).
        base = [x, 0.05, z]
        tag = finnish
        print(f"({x:5.1f},{z:6.1f})  {finnish:<20} {scientific:<28} {height:.1f} m")
        kwargs = dict(kwargs)
        cluster = kwargs.pop("cluster", 0)
        leaf_pair = [m[leaf], m["leaf_mid" if leaf != "leaf_mid" else "leaf_light"]]
        if habit == "conifer":
            kwargs.setdefault("root_count", 5)
            grow_conifer(c, tag, base, height, m[bark], m[leaf], rng,
                         sway_jobs=sway_jobs, sway_mass=3.0 * height,
                         sway_receptivity=0.9 * height,
                         sway_settings=sway_setting_for_height(c, height), **kwargs)
        elif habit == "columnar":
            # A juniper column is a tight bundle of near-vertical stems, far
            # stiffer than a tapered trunk of the same height - and its sway
            # body is only the short inner trunk, so any joint angle is
            # amplified over the full visual column. Stiffness up 4x, range
            # halved, receptivity halved (was: full beam rule + 0.6*h, which
            # parked the trunk at the angular limit and wobbled there).
            kwargs.setdefault("root_count", 3)
            grow_columnar(c, tag, base, height, m[bark], m[leaf], rng,
                          sway_jobs=sway_jobs, sway_mass=2.0 * height,
                          sway_receptivity=0.3 * height,
                          sway_settings=sway_setting_for_height(
                              c, height, stiffness_scale=4.0, range_scale=0.5),
                          **kwargs)
        elif habit == "shrub":
            # Shared root mound: the stems already spread from one point, so
            # with roots underneath the branching reads as starting below
            # ground. Stems are thin - beam scaling by stem height keeps
            # them the softest spines - but the raw bucket-4 setting plus
            # base-overlapping stem hulls made the willows/buckthorns
            # WOBBLE: rig_tree_sway's shared collision filter stops the
            # stem-vs-stem penetration fights, and stiffness x3 with a
            # narrower range calms the spring itself.
            kwargs.setdefault("root_count", 4)
            grow_shrub(c, tag, base, height, m[bark], m[leaf], rng,
                       sway_jobs=sway_jobs, sway_mass=0.15 * height,
                       sway_receptivity=0.25 * height,
                       sway_settings=sway_setting_for_height(
                           c, height * 0.6, stiffness_scale=3.0, range_scale=0.6),
                       **kwargs)
        elif cluster:
            # Multi-stem cluster: a shared root mound and 2-3 full trunks
            # leaning outward from almost the same point, heights staggered -
            # as if the branching happened below ground.
            mound = c.group(f"{tag} Mound", base)
            roots_batch = c.part_batch()
            grow_roots(roots_batch, tag, base, height * 0.020, m[bark], rng,
                       root_count=6, flare=2.2, parent=mound)
            roots_batch.flush()
            for s in range(cluster):
                a = 2.0 * math.pi * s / cluster + rng.uniform(-0.4, 0.4)
                offset = rng.uniform(0.25, 0.6)
                stem_base = [x + math.cos(a) * offset, 0.05, z + math.sin(a) * offset]
                stem_height = height * (1.0 - 0.16 * s)
                lean = 0.10 + 0.10 * rng.random()
                species = broadleaf_species_params(
                    stem_height, root_count=0,
                    tilt=[lean * math.cos(a), 1.0, lean * math.sin(a)],
                    sway=tree_sway_config(c, stem_height),
                    **kwargs)
                grow_tree(c, f"{tag} {s + 1}", stem_base, species, m[bark],
                          leaf_pair, rng, sway_jobs=sway_jobs)
        else:
            species = broadleaf_species_params(height, sway=tree_sway_config(c, height),
                                               **kwargs)
            grow_tree(c, tag, base, species, m[bark], leaf_pair,
                      rng, sway_jobs=sway_jobs)

    # ------------------------------------------------------ physics + wind
    # (Joint settings were created lazily per height bucket during the build.)
    c.settle()
    rig_tree_sway(c, sway_jobs)
    c.settle()
    c.wind(enabled=True, direction=[1.0, 0.0, 0.35], speed=3.0,
           gust_amplitude=2.2, gust_frequency=0.4, turbulence=0.45,
           wavelength=14.0)
    c.set_physics(True)
    c.wake_physics()
    probe_tilt(c, ["Metsäkuusi Trunk", "Rauduskoivu Trunk", "Metsätammi Trunk",
                   "Kotikataja Trunk", "Jokipaju Stem 0", "Orapaatsama Stem 0",
                   "Suippuorapihlaja Stem 0"])

    # shadow_range(160) above also raised the camera far plane to 160 m
    # (the projection default is 64 m - the first framing of this garden
    # far-plane clipped its back row), so the overview can sit at a
    # comfortable distance with a moderate lens.
    cameras = c.call("get_scene_cameras", {"scene_name": c.scene}).get("cameras", [])
    if cameras:
        c.mutate("edit_camera", {
            "scene_name": c.scene,
            "camera_name": cameras[0].get("name") or cameras[0].get("node"),
            "fov_y": 0.9,
        })
    c.place_camera([0.0, 24.0, 58.0], [0.0, 14.0, -12.0])
    c.screenshot("logs/creations/tree_garden.png")
    c.place_camera([-34.0, 8.0, 26.0], [8.0, 12.0, -18.0])
    c.screenshot("logs/creations/tree_garden_low.png")
    if not args.no_save:
        c.save("res/editor/scenes/creations/tree_garden.glb")
    print("Tree Garden complete.")


if __name__ == "__main__":
    main()
