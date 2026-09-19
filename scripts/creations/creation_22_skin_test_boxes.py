#!/usr/bin/env python3
"""Creation 22 - Skin Test (three boxes).

A deliberately minimal RIGGED test asset: a 3 m tall, 0.2 m square column
that is three separate closed boxes (each 0.2 x 1.0 x 0.2, stacked y 0..1,
1..2, 2..3) merged into ONE skinned mesh with ONE primitive, driven by a
three-bone chain.

The point of the asset is that every check on it is exact. The weights are
rigid - every vertex of box k is driven by bone k alone, weight 1 - so the
posed bounds after rotating a bone are a closed-form number rather than
something to eyeball: rotate bone_1 by 90 degrees about Z and boxes 1 and 2
swing out 2 m along -X while box 0 does not move at all. Each box keeps its
own end caps, so the merged mesh has interior faces where the boxes touch;
that is intentional - a skinning test wants to see the seam bend.

This is the first creation built with the `create_skin` MCP tool, which
merges mesh prims that are already in the scene into one skinned mesh
(world transforms baked in) and takes the part meshes back out.

Flags: --scene-only builds the scene and stops (no screenshots, no window
changes, no recording pause); --keep-windows leaves the editor's window
layout alone; --no-save skips the export.
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

BASE = "logs/creations/skin_test_boxes"
SAVE_PATH = "res/editor/assets/skin_test/skin_test_3_boxes.glb"

BOX_COUNT = 3
BOX_WIDTH = 0.2      # x and z
BOX_HEIGHT = 1.0     # y, one per bone
ROOT_NAME = "skin_test_3_boxes"
MESH_NAME = "skin_test_3_boxes_mesh"

SHOTS = [
    ("front", [4.2, 2.6, 6.4], [-0.6, 1.5, 0.0]),
    ("side", [7.4, 2.2, 1.2], [-0.6, 1.5, 0.0]),
]

# The pose the catalog screenshots are taken in: a skinning test asset is
# worth looking at bent, not straight. Restored before the export, so the
# saved asset is in its bind pose.
SHOWCASE_POSE = [(1, 34.0), (2, 34.0)]


def build_column(c, material_name):
    """The root, the bone chain and the three boxes; returns
    (root_id, bone_ids, box_ids)."""
    root = c.group(ROOT_NAME, [0.0, 0.0, 0.0])

    # A parent/child chain, each bone one box-height above its parent, so a
    # rotation on bone k carries every bone above it.
    bones = []
    parent = root
    for index in range(BOX_COUNT):
        parent = c.anchor(f"bone_{index}", parent,
                          [0.0, float(index) * BOX_HEIGHT, 0.0])
        bones.append(parent)

    # Separate closed boxes, centered on their own segment. motion_mode
    # "none" keeps them purely visual - create_skin merges the geometry and
    # a rigid body on a part would be left behind.
    boxes = []
    for index in range(BOX_COUNT):
        result = c.shape("box", f"box_{index}",
                         [0.0, (index + 0.5) * BOX_HEIGHT, 0.0],
                         size=[BOX_WIDTH, BOX_HEIGHT, BOX_WIDTH],
                         steps=[1, 1, 1],
                         material_name=material_name,
                         motion_mode="none",
                         parent_node_id=root,
                         reuse=False)
        boxes.append(result["node_id"])

    return root, bones, boxes


def posed_bounds(c):
    """World AABB of the skinned mesh - the POSED bounds, computed from the
    joint transforms (the mesh node's own transform is ignored by
    skinning)."""
    details = c.call("get_node_details",
                     {"scene_name": c.scene, "node_name": MESH_NAME})
    aabb = details["mesh"]["world_aabb"]
    return ([round(v, 4) for v in aabb["min"]],
            [round(v, 4) for v in aabb["max"]])


def z_rotation(degrees):
    half = math.radians(degrees) * 0.5
    return [0.0, 0.0, math.sin(half), math.cos(half)]


def report_pose(c, bones):
    """Rotate one bone at a time and print the posed bounds, then restore
    the bind pose. Rigid weights make every number here exact."""
    print(f"bind pose bounds:          {posed_bounds(c)}")
    for index in range(1, BOX_COUNT):
        set_bone_rotation(c, bones[index], 90.0)
        print(f"bone_{index} rotated 90 deg Z: {posed_bounds(c)}")
        set_bone_rotation(c, bones[index], 0.0)
    print(f"restored bind pose:        {posed_bounds(c)}")


def set_bone_rotation(c, bone_id, degrees):
    c.mutate("set_node_transform", {
        "scene_name": c.scene, "node_id": int(bone_id),
        "rotation_xyzw": z_rotation(degrees), "space": "local",
    })


def add_script_arguments(parser):
    parser.add_argument("--scene-only", action="store_true",
                        help="build the scene only: no screenshots, pose "
                             "report, window changes or recording pause")


def main():
    args = standard_args("Skin Test (three boxes)", add_script_arguments)
    if reframe(args, "Skin Test (three boxes)", BASE, SHOTS):
        return
    scene_only = args.scene_only
    c = Creation("Skin Test (three boxes)", port=args.port,
                 pause_s=0.0 if scene_only else args.pause,
                 editor_exe=args.editor_exe, reuse=args.reuse,
                 keep_scenes=args.keep_scenes,
                 manage_windows=not (args.keep_windows or scene_only))
    print(f"scene: {c.new_scene()}")

    guard = contextlib.nullcontext() if scene_only else fail_soft(c, BASE)
    with guard:
        c.set_physics(False)
        material = c.ensure_material("skin test", base_color=[0.62, 0.64, 0.68],
                                     roughness=0.35, metallic=0.9)
        c.light("directional", "Key", [4.0, 6.0, 4.0], [1.0, 0.98, 0.94], 3.0)
        c.light("point", "Fill", [-2.5, 1.6, 2.5], [0.8, 0.85, 1.0], 8.0,
                range=8.0, cast_shadow=False)
        c.shadow_range(6.0, z_far=40.0)

        root, bones, boxes = build_column(c, material)
        c.settle()

        result = c.skin(MESH_NAME, list(zip(boxes, bones)),
                        parent_node_id=root, material=material)
        c.settle()
        print(f"skin: {result['skin_name']}, {result['joint_count']} joints, "
              f"{result['vertex_count']} vertices")
        for joint in result["joints"]:
            print(f"  joint {joint['joint_index']} {joint['node_name']}: "
                  f"{joint['vertex_count']} vertices")

        if scene_only:
            c.place_camera(*SHOTS[0][1:])
            if not args.no_save:
                c.export(SAVE_PATH)
            print("Skin test scene built.")
            return

        report_pose(c, bones)

        for bone_index, degrees in SHOWCASE_POSE:
            set_bone_rotation(c, bones[bone_index], degrees)
        c.screenshot_views(BASE, SHOTS)
        for bone_index, _degrees in SHOWCASE_POSE:
            set_bone_rotation(c, bones[bone_index], 0.0)

        if not args.no_save:
            c.export(SAVE_PATH)
        c.place_camera(*SHOTS[0][1:])
        print("Skin test complete.")


if __name__ == "__main__":
    main()
