#!/usr/bin/env python3
"""Acceptance criteria of doc/plans/rigging/ik_drag_options.md section 1.4, over MCP.

Runs against an ALREADY RUNNING editor (headless is enough; see AGENTS.md
"In-editor MCP server"). It creates its own scene, imports the tracked
RiggedFigure fixture, and drives the `ik_drag` tool once per effector
orientation mode, measuring the effector's world and local rotation before and
after each drag.

    py -3 scripts/ik_effector_orientation_verify.py [--port N] [--effector arm_joint_L_3]

One PASS/FAIL line per criterion with the measured number; exit code 1 when any
criterion fails. The scene it created is closed again at the end.
"""

import argparse
import math
import sys
import time

from erhe_mcp import DEFAULT_PORT, McpClient, check_true, report, wait_for_server

GLTF_PATH = "res/editor/assets/RiggedFigure/RiggedFigure.glb"

# Doc tolerances (section 1.4).
ROTATION_TOLERANCE      = 1.0e-4   # quaternion distance 1 - |dot|, up to sign
ROTATION_CHANGE_MINIMUM = 1.0e-3   # "its world rotation differs"
PARENT_TURN_MINIMUM_DEG = 10.0     # the premise of criterion 1


def quaternion_distance(a, b):
    """1 - |dot(a, b)|: zero for equal rotations, sign-insensitive."""
    return 1.0 - abs(sum(x * y for x, y in zip(a, b)))


def quaternion_angle_deg(a, b):
    """The rotation angle between two unit quaternions, up to sign."""
    d = min(1.0, abs(sum(x * y for x, y in zip(a, b))))
    return math.degrees(2.0 * math.acos(d))


def distance(a, b):
    return math.sqrt(sum((x - y) * (x - y) for x, y in zip(a, b)))


def advance(client, frames=4):
    for _ in range(frames):
        client.call("advance_time", {"seconds": 0.016})


def wait_idle(client, timeout_s=180.0):
    deadline = time.monotonic() + timeout_s
    idle_reads = 0
    while time.monotonic() < deadline:
        advance(client, 2)
        status = client.call("get_async_status")
        idle = (
            status.get("pending", 1) == 0
            and status.get("running", 1) == 0
            and status.get("queued_operations", 1) == 0
            and status.get("pending_scene_commits", 1) == 0
            and status.get("asset_loads", 1) == 0
        )
        idle_reads = idle_reads + 1 if idle else 0
        if idle_reads >= 2:
            return
    raise RuntimeError("editor did not go idle")


def undo_depth(client):
    return len(client.call("get_undo_redo_stack")["undo"])


def node_state(client, scene, name):
    """The node's local and world rotation, plus its world position."""
    details = client.call("get_node_details", {"scene_name": scene, "node_name": name})
    return {
        "local_rotation": details["local_transform"]["rotation_xyzw"],
        "world_rotation": details["world_transform"]["rotation_xyzw"],
        "world_position": details["world_transform"]["translation"],
    }


def make_scene(client):
    before = {scene["name"] for scene in client.call("list_scenes")["scenes"]}
    client.call("create_scene")
    advance(client, 6)
    new = [scene["name"] for scene in client.call("list_scenes")["scenes"] if scene["name"] not in before]
    if not new:
        raise RuntimeError("create_scene produced no new scene")
    scene = new[0]
    client.call("import_gltf", {"scene_name": scene, "path": GLTF_PATH})
    wait_idle(client)
    return scene


def ik_drag(client, scene, effector, target, orientation=None):
    """One measured gesture: drag, read the undo growth, then undo it.

    Undoing restores every joint's drag-start parent_from_node, so the next
    measured drag starts from the same pose (doc/plans/rigging/pole_target.md
    acceptance section).
    """
    arguments = {
        "scene_name": scene,
        "node_name":  effector,
        "target":     [target[0], target[1], target[2]],
    }
    if orientation is not None:
        arguments["effector_orientation"] = orientation
    depth = undo_depth(client)
    result = client.call("ik_drag", arguments)
    advance(client, 4)
    undo_delta = undo_depth(client) - depth
    return result, undo_delta


def undo(client, result):
    if result["recorded"]:
        client.call("undo")
        advance(client, 4)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--effector", default="arm_joint_L_3", help="effector bone name (default arm_joint_L_3)")
    args = parser.parse_args()

    client = McpClient(args.port)
    wait_for_server(client, 60.0)

    scene = make_scene(client)
    try:
        # A drag toward the effector's own start position reports the
        # drag-start chain geometry without moving anything.
        before_effector = node_state(client, scene, args.effector)
        start = before_effector["world_position"]
        rest, _ = ik_drag(client, scene, args.effector, start)
        undo(client, rest)
        joints = rest["joints"]
        if len(joints) < 3:
            check_true("0 chain has an intermediate joint", False, f"chain length {len(joints)}")
            return report()
        parent_name = joints[-2]["name"]
        positions = [joint["position"] for joint in joints]
        reach = sum(distance(positions[i], positions[i + 1]) for i in range(len(positions) - 1))

        # Sideways by half the chain's reach: far enough to swing the parent
        # joint well past criterion 1's 10 degrees.
        target = [start[0] + (0.5 * reach), start[1], start[2]]
        before_parent = node_state(client, scene, parent_name)

        # --- criterion 2: keep_world holds the effector's world rotation ----
        keep, undo_delta_keep = ik_drag(client, scene, args.effector, target, "keep_world")
        keep_effector = node_state(client, scene, args.effector)
        keep_parent   = node_state(client, scene, parent_name)
        undo(client, keep)

        parent_turn_deg = quaternion_angle_deg(before_parent["world_rotation"], keep_parent["world_rotation"])
        check_true(
            "1 the measured drag turns the effector's parent joint by more than 10 degrees",
            parent_turn_deg > PARENT_TURN_MINIMUM_DEG,
            f"parent '{parent_name}' turned {parent_turn_deg:.3f} deg (minimum {PARENT_TURN_MINIMUM_DEG})",
        )

        keep_world_error = quaternion_distance(before_effector["world_rotation"], keep_effector["world_rotation"])
        check_true(
            "2 keep_world leaves the effector's world rotation unchanged",
            (keep_world_error < ROTATION_TOLERANCE) and (keep["effector_orientation"] == "keep_world"),
            f"world rotation distance={keep_world_error:.3e} (limit {ROTATION_TOLERANCE}), "
            f"echoed={keep['effector_orientation']!r}",
        )

        # --- criterion 3: follow_last_segment holds the local rotation ------
        follow, undo_delta_follow = ik_drag(client, scene, args.effector, target, "follow_last_segment")
        follow_effector = node_state(client, scene, args.effector)
        undo(client, follow)

        follow_local_error  = quaternion_distance(before_effector["local_rotation"], follow_effector["local_rotation"])
        follow_world_change = quaternion_distance(before_effector["world_rotation"], follow_effector["world_rotation"])
        check_true(
            "3 follow_last_segment leaves the effector's local rotation unchanged and moves its world rotation",
            (follow_local_error < ROTATION_TOLERANCE)
            and (follow_world_change > ROTATION_CHANGE_MINIMUM)
            and (follow["effector_orientation"] == "follow_last_segment"),
            f"local rotation distance={follow_local_error:.3e} (limit {ROTATION_TOLERANCE}), "
            f"world rotation distance={follow_world_change:.3e} (minimum {ROTATION_CHANGE_MINIMUM}), "
            f"echoed={follow['effector_orientation']!r}",
        )

        # --- criterion 4: an unrecognized value is refused ------------------
        refused = False
        try:
            client.call(
                "ik_drag",
                {
                    "scene_name":           scene,
                    "node_name":            args.effector,
                    "target":               [target[0], target[1], target[2]],
                    "effector_orientation": "sideways",
                },
            )
        except RuntimeError as error:
            refused = "effector_orientation" in str(error)
        advance(client, 4)
        after_refusal = node_state(client, scene, args.effector)
        unchanged = quaternion_distance(before_effector["world_rotation"], after_refusal["world_rotation"])
        moved = distance(before_effector["world_position"], after_refusal["world_position"])
        check_true(
            "4 an unrecognized effector_orientation is refused and moves nothing",
            refused and (unchanged < ROTATION_TOLERANCE) and (moved < 1.0e-4),
            f"refused={refused}, rotation distance={unchanged:.3e}, position moved={moved:.3e}",
        )

        # --- criterion 5: one undo step per drag ----------------------------
        check_true(
            "5 each measured drag is exactly one undo step",
            (undo_delta_keep == 1) and (undo_delta_follow == 1),
            f"keep_world={undo_delta_keep}, follow_last_segment={undo_delta_follow}",
        )
    finally:
        try:
            client.call("close_scene", {"scene_name": scene})
            advance(client, 6)
        except RuntimeError as error:
            print(f"  (close_scene failed: {error})")
    return report()


if __name__ == "__main__":
    sys.exit(main())
