#!/usr/bin/env python3
"""Joint constraint visualization checks (doc/editor/tools.md "Debug_visualizations"), over MCP.

Runs against an ALREADY RUNNING editor (headless is enough; see
doc/agents/editor_runs.md). Builds its content in the "Default Scene" (the
scene the default viewport shows, so the screenshots see it):

  - a dynamic box "Hinge Bob" hung from a world-anchored joint at "Hinge Pivot"
    0.5 m above it, rotation limited to +-0.8 rad about Z, everything else
    fixed;
  - the tracked RiggedFigure fixture with Ik limits on its left elbow.

Checks (PASS / FAIL per line, exit code 1 on any failure):
  1. the joint reports live, with the hinge range as the enforced limit;
  2. a pose inside the range reads in range, one past it (1.2 rad) reads the
     rotation coordinate past 0.8 and flags it;
  3. the elbow reports its IK limits, within limits at rest and outside after
     a local rotation past them;
  4. the Joint Constraints filter: `all` draws the joint and the elbow;
     `hovered_mesh` / `hovered_bone` with nothing hovered draw nothing;
     hovering Hinge Bob in the item tree draws only the hinge joint under
     `hovered_mesh` and nothing under `hovered_bone`; hovering the elbow bone
     draws only the elbow under `hovered_bone`; hovering the skinned mesh draws
     the elbow (a bone of its skin) under `hovered_mesh`.
Screenshots of the `all` state (in range, violated, IK) are written to
logs/joint_visualization_*.png for a visual look; READ them.

    py -3 scripts/joint_visualization_verify.py [--port N]
"""

import argparse
import math
import sys

from erhe_mcp import DEFAULT_PORT, McpClient, check_true, report, wait_for_server

SCENE      = "Default Scene"
TREE       = "Scene Hierarchy [1]"
GLTF_PATH  = "res/editor/assets/RiggedFigure/RiggedFigure.glb"
ELBOW      = "arm_joint_L_2"
SKINNED_MESH = "Proxy"
PIVOT      = [-0.6, 1.2, 1.0]
ARM        = 0.5    # pivot to bob center
HINGE_MAX  = 0.8    # rad
ANGLE_TOL  = 0.02   # rad


def advance(client, frames=3):
    for _ in range(frames):
        client.call("advance_time", {"seconds": 0.016})


def joint_state(client):
    advance(client)
    return client.call("get_joint_constraint_state", {"scene_name": SCENE})


def drawn(client):
    viewport = joint_state(client)["viewports"][0]
    return viewport["drawn_physics_joints"], viewport["drawn_ik_bones"]


def pose_bob(client, angle):
    """Places the bob on its arm, turned by angle about Z at the pivot."""
    position = [PIVOT[0] + (ARM * math.sin(angle)), PIVOT[1] - (ARM * math.cos(angle)), PIVOT[2]]
    client.call("set_node_transform", {
        "scene_name":    SCENE,
        "node_name":     "Hinge Bob",
        "translation":   position,
        "rotation_xyzw": [0.0, 0.0, math.sin(0.5 * angle), math.cos(0.5 * angle)],
    })


def hinge_angle(joint):
    """The hinge's rotation coordinate: the backend puts it in the twist (Box3D
    revolute) or in a pyramid swing axis (Jolt)."""
    coordinates = joint["contract"]["coordinates"]
    return max([abs(coordinates["twist"])] + [abs(value) for value in coordinates["swing"]])


def tree_items(client):
    return client.call("get_imgui_items", {"window": TREE})["items"]


def expand_to(client, names):
    """Opens the item tree rows along names (root first) by clicking their arrows."""
    for name in names:
        for item in tree_items(client):
            if (item.get("label") == name) and item["status"].get("openable") and not item["status"].get("opened"):
                client.call("mouse_click", {"x": item["x"] + 10.0, "y": item["center_y"]})
                advance(client)
                break


def ancestor_names(client, node_name):
    nodes = {node["id"]: node for node in client.call("get_scene_nodes", {"scene_name": SCENE})["nodes"]}
    by_name = {node["name"]: node for node in nodes.values()}
    chain = []
    node = by_name[node_name]
    while node.get("parent_id") in nodes:
        node = nodes[node["parent_id"]]
        chain.append(node["name"])
    return list(reversed(chain))


def hover_row(client, label):
    client.call("imgui_hover", {"window": TREE, "label": label})
    advance(client)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    args = parser.parse_args()

    client = McpClient(args.port)
    wait_for_server(client, 60.0)

    # --- content --------------------------------------------------------
    client.call("create_shape", {"scene_name": SCENE, "shape": "box", "size": [0.2, 0.2, 0.2], "name": "Hinge Bob",
                                 "position": [PIVOT[0], PIVOT[1] - ARM, PIVOT[2]]})
    client.call("create_node", {"scene_name": SCENE, "name": "Hinge Pivot", "parent_node_name": "Hinge Bob", "position": PIVOT})
    client.call("create_physics_joint_settings", {"scene_name": SCENE, "name": "Hinge Limits", "limits": [
        {"linear_axes":  [True, True, True],   "min": 0.0,        "max": 0.0},
        {"angular_axes": [True, True, False],  "min": 0.0,        "max": 0.0},
        {"angular_axes": [False, False, True], "min": -HINGE_MAX, "max": HINGE_MAX},
    ]})
    client.call("create_joint", {"scene_name": SCENE, "node_name": "Hinge Pivot", "settings_name": "Hinge Limits"})
    client.call("import_gltf", {"scene_name": SCENE, "path": GLTF_PATH})
    advance(client, 30)
    client.call("toggle_physics", {"enabled": False})
    advance(client)

    # --- 1. the joint ---------------------------------------------------
    joints = joint_state(client)["physics_joints"]
    check_true("1 one physics joint reported", len(joints) == 1, f"{len(joints)} joints")
    joint = joints[0]
    joint_id = joint["id"]
    check_true("1 joint is live", joint["live"])
    limits = joint["contract"]["limits"]
    ranges = [limits["twist"]] + limits["swing"]
    hinge = [r for r in ranges if r.get("limited") and (r["max"] > r["min"])]
    check_true("1 exactly one ranged rotation axis, +-0.8 rad",
               (len(hinge) == 1) and math.isclose(hinge[0]["max"], HINGE_MAX, abs_tol=1e-5) and math.isclose(hinge[0]["min"], -HINGE_MAX, abs_tol=1e-5),
               str(ranges))

    # --- 2. in range / past the range -------------------------------------
    pose_bob(client, 0.5)
    joint = joint_state(client)["physics_joints"][0]
    check_true("2 at 0.5 rad: coordinate 0.5, in range",
               math.isclose(hinge_angle(joint), 0.5, abs_tol=ANGLE_TOL) and joint["contract"]["in_range"]["all"],
               f"angle {hinge_angle(joint):.4f}, in_range {joint['contract']['in_range']}")
    client.call("set_joint_constraint_visualization", {"filter": "all"})
    advance(client)
    client.call("capture_screenshot", {"path": "logs/joint_visualization_in_range.png"})

    pose_bob(client, 1.2)
    joint = joint_state(client)["physics_joints"][0]
    check_true("2 at 1.2 rad: coordinate 1.2, flagged out of range",
               math.isclose(hinge_angle(joint), 1.2, abs_tol=ANGLE_TOL) and not joint["contract"]["in_range"]["all"],
               f"angle {hinge_angle(joint):.4f}, in_range {joint['contract']['in_range']}")
    client.call("capture_screenshot", {"path": "logs/joint_visualization_violated.png"})
    pose_bob(client, 0.0)

    # --- 3. IK limits ----------------------------------------------------
    elbow_id = client.call("get_node_details", {"scene_name": SCENE, "node_name": ELBOW})["id"]
    for prop, value in (("Ik.limit_x", True), ("Ik.limit_y", True), ("Ik.limit_z", True),
                        ("Ik.limit_min", "-0.6 -0.4 -0.3"), ("Ik.limit_max", "0.9 0.4 0.5")):
        client.call("set_item_property", {"item_id": elbow_id, "property": prop, "value": value})
    bones = {bone["name"]: bone for bone in joint_state(client)["ik_bones"]}
    check_true("3 elbow reported with its limits", (ELBOW in bones) and (bones[ELBOW]["limit"] == [True, True, True]),
               str(bones.get(ELBOW)))
    check_true("3 elbow within limits at rest", bones.get(ELBOW, {}).get("within_limits") is True)
    client.call("capture_screenshot", {"path": "logs/joint_visualization_ik.png"})
    rest = client.call("get_node_details", {"scene_name": SCENE, "node_name": ELBOW})["local_transform"]["rotation_xyzw"]
    # 1.2 rad about the elbow's local X on top of the rest: past the 0.9 limit.
    s, c = math.sin(0.6), math.cos(0.6)
    x, y, z, w = rest
    turned = [w * s + x * c, y * c + z * s, z * c - y * s, w * c - x * s]
    client.call("set_node_transform", {"scene_name": SCENE, "node_name": ELBOW, "space": "local", "rotation_xyzw": turned})
    bones = {bone["name"]: bone for bone in joint_state(client)["ik_bones"]}
    check_true("3 elbow outside limits after turning past them", bones.get(ELBOW, {}).get("within_limits") is False,
               str(bones.get(ELBOW, {}).get("swing")))
    client.call("set_node_transform", {"scene_name": SCENE, "node_name": ELBOW, "space": "local", "rotation_xyzw": rest})

    # --- 4. the filter ---------------------------------------------------
    physics, ik = drawn(client)
    check_true("4 all: the joint and the elbow are drawn", (joint_id in physics) and (elbow_id in ik), f"{physics} {ik}")
    for mode in ("hovered_mesh", "hovered_bone"):
        client.call("set_joint_constraint_visualization", {"filter": mode})
        physics, ik = drawn(client)
        check_true(f"4 {mode}, nothing hovered: nothing drawn", (physics == []) and (ik == []), f"{physics} {ik}")

    hover_row(client, "Hinge Bob")
    client.call("set_joint_constraint_visualization", {"filter": "hovered_mesh"})
    physics, ik = drawn(client)
    check_true("4 hovered_mesh, Hinge Bob hovered: only the hinge joint", (physics == [joint_id]) and (ik == []), f"{physics} {ik}")
    client.call("set_joint_constraint_visualization", {"filter": "hovered_bone"})
    physics, ik = drawn(client)
    check_true("4 hovered_bone, Hinge Bob hovered: nothing", (physics == []) and (ik == []), f"{physics} {ik}")

    expand_to(client, ancestor_names(client, ELBOW))
    hover_row(client, ELBOW)
    physics, ik = drawn(client)
    check_true("4 hovered_bone, elbow hovered: only the elbow", (physics == []) and (ik == [elbow_id]), f"{physics} {ik}")

    # The skinned mesh of the figure: its skin's bones move it.
    client.call("set_joint_constraint_visualization", {"filter": "hovered_mesh"})
    expand_to(client, ancestor_names(client, SKINNED_MESH))
    hover_row(client, SKINNED_MESH)
    physics, ik = drawn(client)
    check_true("4 hovered_mesh, skinned mesh hovered: its limited bone, no joint", (physics == []) and (ik == [elbow_id]), f"{physics} {ik}")

    client.call("set_joint_constraint_visualization", {"filter": "off"})
    physics, ik = drawn(client)
    check_true("4 off: nothing drawn", (physics == []) and (ik == []), f"{physics} {ik}")
    return report()


if __name__ == "__main__":
    sys.exit(main())
