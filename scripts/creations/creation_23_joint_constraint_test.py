#!/usr/bin/env python3
"""Creation 23 - Joint Constraint Test.

A test ASSET covering the kinds of constraints the editor authors, for
user verification of the joint constraint visualization (Debug
Visualizations > Joint Constraints, doc/editor/tools.md
"Debug_visualizations") and for automated checks
(scripts/joint_constraint_assets_verify.py loads the saved file and
checks it against the tables below - they are the single source of truth
for what each station is).

Two rows:

  - Physics joints (back row, z = 0): thirteen stations, each a static
    post with a dynamic box hanging 0.55 m below it on a Joint between a
    "Hinge" anchor under the box and a "Pivot" anchor under the post
    (glTF cannot store a world-anchored joint, so every joint has a
    body on both sides). One station per limit pattern: limited / free /
    asymmetric hinges, limited and free ball joints, an asymmetric swing
    pyramid, limited and free sliders, a weld, a settings-less free joint,
    a fixed axis authored off zero (enforced differently by the backends),
    a double pendulum (a joint between two dynamic bodies) and an
    inactive joint.
  - IK limits (front row, z = 1.5): eight three-bone chains, each a
    rigidly skinned column of three boxes, with the Ik.* limits of its
    middle bone set per case: twist only, symmetric / asymmetric swing,
    one swing axis, an elbow hinge made of locks, fully locked, all three
    axes, and a pose outside its limits. Each chain ends in a bone tip
    node carrying a small box, so the tip is easy to pick and IK drag.

Saved with save_scene (the erhe-authored glTF carries the Ik.* values on
ERHE_node and the joint prims on ERHE_physics_joint) to
res/editor/assets/joint_constraints/joint_constraints.glb; open it with
File > Load Scene.

Flags: --scene-only builds and saves without screenshots or window
changes; --no-save skips the save.
"""

import contextlib
import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))

from common import (
    Creation,
    standard_args,
    reframe,
    fail_soft,
)

TITLE     = "Joint Constraint Test"
BASE      = "logs/creations/joint_constraint_test"
SAVE_PATH = "res/editor/assets/joint_constraints/joint_constraints.glb"

PI = math.pi

# --- physics stations --------------------------------------------------------

STATION_SPACING = 1.0
POST_Y          = 2.0
PIVOT_Y         = 1.95
ARM             = 0.55          # pivot to bob center
POST_SIZE       = [0.3, 0.1, 0.3]
BOB_SIZE        = [0.15, 0.15, 0.15]
BOB_MASS        = 1.0

LOCK_LINEAR = {"linear_axes": [True, True, True], "min": 0.0, "max": 0.0}


def angular(axes, lo, hi):
    return {"angular_axes": [bool(a) for a in axes], "min": lo, "max": hi}


def linear(axes, lo, hi):
    return {"linear_axes": [bool(a) for a in axes], "min": lo, "max": hi}


# One entry per station, left to right. `limits` is the
# create_physics_joint_settings limit list (None = a joint with no settings,
# the free six-DOF joint). `check` names what the automated verification
# asserts beyond "reported, in range at rest":
#   exact_jolt / exact_box3d: Joint_limit_shape::is_exact per backend
#   stress: an impulse on the bob must not push the joint past its limits
#   live: False for the inactive joint
STATIONS = [
    {"key": "P01", "label": "Hinge limited",
     "limits": [LOCK_LINEAR, angular([1, 1, 0], 0.0, 0.0), angular([0, 0, 1], -0.785, 0.785)],
     "exact_jolt": True, "exact_box3d": True, "stress": True},
    {"key": "P02", "label": "Hinge free",
     "limits": [LOCK_LINEAR, angular([1, 1, 0], 0.0, 0.0)],
     "exact_jolt": True, "exact_box3d": True, "stress": False},
    {"key": "P03", "label": "Hinge asymmetric",
     "limits": [LOCK_LINEAR, angular([1, 1, 0], 0.0, 0.0), angular([0, 0, 1], -0.3, 1.2)],
     "exact_jolt": True, "exact_box3d": True, "stress": True},
    {"key": "P04", "label": "Ball limited",
     "limits": [LOCK_LINEAR, angular([1, 1, 1], -0.5, 0.5)],
     "exact_jolt": True, "exact_box3d": False, "stress": True},
    {"key": "P05", "label": "Swing pyramid asymmetric",
     "limits": [LOCK_LINEAR, angular([1, 0, 0], 0.0, 0.0), angular([0, 1, 0], -0.2, 0.8), angular([0, 0, 1], -0.6, 0.3)],
     "exact_jolt": True, "exact_box3d": False, "stress": True},
    {"key": "P06", "label": "Ball free",
     "limits": [LOCK_LINEAR],
     "exact_jolt": True, "exact_box3d": True, "stress": False},
    {"key": "P07", "label": "Slider Y limited",
     "limits": [linear([1, 0, 1], 0.0, 0.0), linear([0, 1, 0], -0.3, 0.1), angular([1, 1, 1], 0.0, 0.0)],
     "exact_jolt": True, "exact_box3d": True, "stress": True},
    {"key": "P08", "label": "Slider X free",
     "limits": [linear([0, 1, 1], 0.0, 0.0), angular([1, 1, 1], 0.0, 0.0)],
     "exact_jolt": True, "exact_box3d": True, "stress": False},
    {"key": "P09", "label": "Weld",
     "limits": [LOCK_LINEAR, angular([1, 1, 1], 0.0, 0.0)],
     "exact_jolt": True, "exact_box3d": True, "stress": True},
    {"key": "P10", "label": "Free six-DOF",
     "limits": None,
     "exact_jolt": True, "exact_box3d": True, "stress": False},
    {"key": "P11", "label": "Fixed off zero",
     "limits": [linear([1, 0, 1], 0.0, 0.0), linear([0, 1, 0], 0.1, 0.1), angular([1, 1, 1], 0.0, 0.0)],
     "exact_jolt": False, "exact_box3d": False, "stress": False},
    {"key": "P12", "label": "Double pendulum",
     "limits": [LOCK_LINEAR, angular([1, 1, 0], 0.0, 0.0), angular([0, 0, 1], -0.8, 0.8)],
     "lower_limits": [LOCK_LINEAR, angular([1, 1, 0], 0.0, 0.0), angular([0, 0, 1], -1.0, 1.0)],
     "exact_jolt": True, "exact_box3d": True, "stress": True},
    {"key": "P13", "label": "Inactive joint",
     "limits": [LOCK_LINEAR, angular([1, 1, 0], 0.0, 0.0), angular([0, 0, 1], -0.785, 0.785)],
     "exact_jolt": True, "exact_box3d": True, "stress": False, "live": False},
]


def station_x(index):
    return (index - (len(STATIONS) - 1) * 0.5) * STATION_SPACING


def joint_name(station, lower=False):
    return f"{station['key']} {station['label']}{' lower' if lower else ''} joint"


def bob_name(station, lower=False):
    return f"{station['key']} {'Lower Bob' if lower else 'Bob'}"


# --- IK chains ---------------------------------------------------------------

CHAIN_SPACING = 1.0
CHAIN_Z       = 1.5
BONE_LENGTH   = 0.4
BONE_WIDTH    = 0.08
TIP_SIZE      = 0.1


def limit_triplet(x=None, y=None, z=None):
    """Ik.limit_min / Ik.limit_max text for per-axis (lo, hi) pairs; an axis
    given as None keeps the full range."""
    lo = [-PI, -PI, -PI]
    hi = [PI, PI, PI]
    for axis, pair in enumerate((x, y, z)):
        if pair is not None:
            lo[axis], hi[axis] = pair
    return " ".join(f"{v:.7f}" for v in lo), " ".join(f"{v:.7f}" for v in hi)


# The Ik.* values of each chain's middle bone ("<key> bone_1"), which also
# carries IK Lock so a drag of bone_2 turns that bone alone. `pose_x`
# rotates that bone about its local X (radians) in the saved file;
# `within` is what is_ik_rotation_within_limits reports for the saved pose.
IK_CASES = [
    {"key": "I01", "label": "Twist limited",
     "limit": [False, True, False], "range": limit_triplet(y=(-0.5, 0.5)),
     "within": True},
    {"key": "I02", "label": "Swing symmetric",
     "limit": [True, False, True], "range": limit_triplet(x=(-0.6, 0.6), z=(-0.6, 0.6)),
     "within": True},
    {"key": "I03", "label": "Swing asymmetric",
     "limit": [True, False, True], "range": limit_triplet(x=(-0.2, 0.9), z=(-0.7, 0.3)),
     "within": True},
    {"key": "I04", "label": "One swing axis",
     "limit": [True, False, False], "range": limit_triplet(x=(-0.3, 0.8)),
     "within": True},
    {"key": "I05", "label": "Elbow hinge",
     "lock": [False, True, True],
     "limit": [True, False, False], "range": limit_triplet(x=(0.0, 2.0)),
     "within": True},
    {"key": "I06", "label": "Locked",
     "lock": [True, True, True],
     "limit": [False, False, False], "range": None,
     "within": True},
    {"key": "I07", "label": "All axes limited",
     "limit": [True, True, True], "range": limit_triplet(x=(-0.5, 0.5), y=(-0.3, 0.3), z=(-0.4, 0.4)),
     "within": True},
    {"key": "I08", "label": "Posed outside",
     "limit": [True, False, True], "range": limit_triplet(x=(-0.6, 0.6), z=(-0.6, 0.6)),
     "pose_x": 1.0, "within": False},
]


def chain_x(index):
    return (index - (len(IK_CASES) - 1) * 0.5) * CHAIN_SPACING


def ik_bone_name(case, index):
    return f"{case['key']} bone_{index}"


def ik_tip_name(case):
    return f"{ik_bone_name(case, 2)} tip"


def ik_mesh_name(case):
    return f"{case['key']} {case['label']}"


SHOTS = [
    ("front", [0.0, 3.2, 17.0], [0.0, 1.0, 0.5]),
    ("physics", [-3.2, 2.0, 4.2], [-3.2, 1.5, 0.0]),
    ("ik", [0.0, 1.3, 5.2], [0.0, 0.6, 1.5]),
]


# --- builders ----------------------------------------------------------------

def box_node(c, name, position, size, material, parent_node_id):
    return c.shape("box", name, position, size=size, steps=[0, 0, 0],
                   material_name=material, motion_mode="none",
                   parent_node_id=parent_node_id, reuse=False)["node_id"]


def half(size):
    return [0.5 * v for v in size]


def make_settings(c, name, limits):
    if limits is None:
        return None
    return c.joint_settings(name, limits)


def rename(c, item_id, name):
    c.mutate("set_item_property", {"item_id": int(item_id), "property": "name", "value": name})


def build_station(c, index, station, materials, root):
    x    = station_x(index)
    key  = station["key"]
    group = c.group(f"{key} {station['label']}", [x, 0.0, 0.0], parent_node_id=root)

    post = box_node(c, f"{key} Post", [x, POST_Y, 0.0], POST_SIZE, materials["post"], group)
    c.body(post, shape="box", half_extents=half(POST_SIZE), motion_mode="static")
    bob = box_node(c, bob_name(station), [x, PIVOT_Y - ARM, 0.0], BOB_SIZE, materials["bob"], group)
    c.body(bob, shape="box", half_extents=half(BOB_SIZE), motion_mode="dynamic", mass=BOB_MASS)

    pivot = c.anchor(f"{key} Pivot", post, [x, PIVOT_Y, 0.0])
    hinge = c.anchor(f"{key} Hinge", bob, [x, PIVOT_Y, 0.0])
    settings = make_settings(c, f"{key} Limits", station["limits"])
    joint = c.joint(hinge, connected_node_id=pivot, settings_name=settings)
    rename(c, joint["joint_id"], joint_name(station))

    if "lower_limits" in station:
        lower_y = PIVOT_Y - ARM - 0.5 * BOB_SIZE[1]
        lower = box_node(c, bob_name(station, lower=True), [x, lower_y - ARM, 0.0], BOB_SIZE, materials["bob"], group)
        c.body(lower, shape="box", half_extents=half(BOB_SIZE), motion_mode="dynamic", mass=BOB_MASS)
        lower_pivot = c.anchor(f"{key} Lower Pivot", bob, [x, lower_y, 0.0])
        lower_hinge = c.anchor(f"{key} Lower Hinge", lower, [x, lower_y, 0.0])
        lower_settings = make_settings(c, f"{key} Lower Limits", station["lower_limits"])
        lower_joint = c.joint(lower_hinge, connected_node_id=lower_pivot, settings_name=lower_settings)
        rename(c, lower_joint["joint_id"], joint_name(station, lower=True))

    if station.get("live", True) is False:
        c.mutate("set_item_property", {"item_id": int(joint["joint_id"]), "property": "active", "value": "false"})


def build_chain(c, index, case, material, tip_material, root):
    x = chain_x(index)
    group = c.group(ik_mesh_name(case) + " rig", [x, 0.0, CHAIN_Z], parent_node_id=root)
    bones = []
    parent = group
    for bone_index in range(3):
        parent = c.anchor(ik_bone_name(case, bone_index), parent, [x, bone_index * BONE_LENGTH, CHAIN_Z])
        bones.append(parent)
    boxes = []
    for bone_index in range(3):
        boxes.append(box_node(c, f"{case['key']} box_{bone_index}",
                              [x, (bone_index + 0.5) * BONE_LENGTH, CHAIN_Z],
                              [BONE_WIDTH, BONE_LENGTH, BONE_WIDTH], material, group))
    c.settle()
    c.skin(ik_mesh_name(case), list(zip(boxes, bones)), parent_node_id=group, material=material)
    c.settle()

    # A bone tip node at the end of the last bone, named "<bone> tip" at the
    # leaf bone's Rig.tail - here the top of its box - as Hierarchy > Add Bone
    # Tip Nodes names it. Dragged with IK it is the end of the chain, so the
    # last bone aims at it and turns too. Unlike Add Bone Tip Nodes it
    # carries a small box mesh of its own, so a click in the viewport selects
    # the tip.
    box_node(c, ik_tip_name(case), [x, 3.0 * BONE_LENGTH, CHAIN_Z],
             [TIP_SIZE, TIP_SIZE, TIP_SIZE], tip_material, bones[2])

    middle = bones[1]
    # IK Lock on the limited bone: an IK drag of bone_2 then stops the chain
    # there, so the drag turns this bone alone and runs it straight into its
    # limits instead of letting bone_0 take up the motion.
    c.mutate("set_item_flags", {"scene_name": c.scene, "ids": [int(middle)], "flags": ["ik_lock"], "enabled": True})

    def set_prop(prop, value):
        c.mutate("set_item_property", {"item_id": int(middle), "property": prop, "value": value})

    for axis, name in enumerate("xyz"):
        if case.get("lock", [False, False, False])[axis]:
            set_prop(f"Ik.lock_{name}", True)
        if case["limit"][axis]:
            set_prop(f"Ik.limit_{name}", True)
    if case["range"] is not None:
        set_prop("Ik.limit_min", case["range"][0])
        set_prop("Ik.limit_max", case["range"][1])
    if "pose_x" in case:
        angle = case["pose_x"]
        c.mutate("set_node_transform", {
            "scene_name": c.scene, "node_id": int(middle), "space": "local",
            "rotation_xyzw": [math.sin(0.5 * angle), 0.0, 0.0, math.cos(0.5 * angle)],
        })


def add_script_arguments(parser):
    parser.add_argument("--scene-only", action="store_true",
                        help="build and save the scene only: no screenshots, window changes or recording pause")


def main():
    args = standard_args(TITLE, add_script_arguments)
    if reframe(args, TITLE, BASE, SHOTS):
        return
    scene_only = args.scene_only
    c = Creation(TITLE, port=args.port,
                 pause_s=0.0 if scene_only else args.pause,
                 editor_exe=args.editor_exe, reuse=args.reuse,
                 keep_scenes=args.keep_scenes,
                 manage_windows=not (args.keep_windows or scene_only))
    print(f"scene: {c.new_scene()}")

    guard = contextlib.nullcontext() if scene_only else fail_soft(c, BASE)
    with guard:
        # Built with the simulation paused, so every body is saved at its
        # authored rest pose.
        c.set_physics(False)
        c.light("directional", "Key", [4.0, 6.0, 6.0], [1.0, 0.98, 0.94], 3.0)
        # Aim the key down onto the rows: pitch 55 degrees below the
        # horizon, then a slight yaw so the posts cast readable shadows.
        pitch = math.radians(-55.0)
        yaw   = math.radians(20.0)
        qx = [math.sin(0.5 * pitch), 0.0, 0.0, math.cos(0.5 * pitch)]
        qy = [0.0, math.sin(0.5 * yaw), 0.0, math.cos(0.5 * yaw)]
        key_rotation = [
            qy[3] * qx[0] + qy[0] * qx[3] + qy[1] * qx[2] - qy[2] * qx[1],
            qy[3] * qx[1] - qy[0] * qx[2] + qy[1] * qx[3] + qy[2] * qx[0],
            qy[3] * qx[2] + qy[0] * qx[1] - qy[1] * qx[0] + qy[2] * qx[3],
            qy[3] * qx[3] - qy[0] * qx[0] - qy[1] * qx[1] - qy[2] * qx[2],
        ]
        c.set_node_transform("Key", rotation_xyzw=key_rotation)
        c.light("point", "Fill", [-4.0, 3.0, 5.0], [0.8, 0.85, 1.0], 20.0,
                range=14.0, cast_shadow=False)
        c.shadow_range(16.0, z_far=60.0)

        materials = {
            "post":  c.ensure_material("constraint test post",  base_color=[0.35, 0.36, 0.38], roughness=0.6, metallic=0.0),
            "bob":   c.ensure_material("constraint test bob",   base_color=[0.85, 0.45, 0.12], roughness=0.45, metallic=0.0),
            "ik":    c.ensure_material("constraint test bone",  base_color=[0.30, 0.55, 0.85], roughness=0.4, metallic=0.0),
            "tip":   c.ensure_material("constraint test tip",   base_color=[0.95, 0.80, 0.20], roughness=0.4, metallic=0.0),
            "floor": c.ensure_material("constraint test floor", base_color=[0.55, 0.55, 0.52], roughness=0.9, metallic=0.0),
        }
        # Top face 1 cm below the grid plane (y = 0), so the two do not
        # z-fight.
        floor_size = [16.0, 0.1, 6.0]
        floor = box_node(c, "Floor", [0.0, -0.06, 0.5], floor_size, materials["floor"], None)
        c.body(floor, shape="box", half_extents=half(floor_size), motion_mode="static")

        physics_root = c.group("Physics joints", [0.0, 0.0, 0.0])
        for index, station in enumerate(STATIONS):
            build_station(c, index, station, materials, physics_root)
        c.settle()

        ik_root = c.group("IK limits", [0.0, 0.0, CHAIN_Z])
        for index, case in enumerate(IK_CASES):
            build_chain(c, index, case, materials["ik"], materials["tip"], ik_root)
        c.settle()

        c.place_camera(*SHOTS[0][1:])
        if not scene_only:
            c.mutate("set_joint_constraint_visualization", {"filter": "all"})
            c.screenshot_views(BASE, SHOTS)
            c.place_camera(*SHOTS[0][1:])
        if not args.no_save:
            # Every part is a private mesh (reuse=False), so the scene's
            # brush library holds only the defaults a new scene starts with -
            # about 5.6 MB of geometry the asset does not use.
            c.mutate("delete_nodes", {"scene_name": c.scene, "names": ["Brushes"]})
            c.settle()
            c.save(SAVE_PATH)
        print(f"{TITLE} complete.")


if __name__ == "__main__":
    main()
