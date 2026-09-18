#!/usr/bin/env python3
"""Acceptance criteria 1-8 of doc/plans/rigging/pole_target.md, driven over MCP.

Runs against an ALREADY RUNNING editor (headless is enough; see AGENTS.md
"In-editor MCP server"). It creates its own scene, imports the tracked
RiggedFigure fixture, authors an Ik_settings attachment and a pole node,
and drives the `ik_drag` tool, computing the chain's bend direction (R11
step 3) and the swivel angles itself from the reported joint positions.

    py -3 scripts/ik_pole_verify.py [--port N] [--effector arm_joint_L_3]

One PASS/FAIL line per criterion with the measured number; exit code 1 when
any criterion fails. The scene is closed again at the end.
"""

import argparse
import math
import sys
import time

from erhe_mcp import DEFAULT_PORT, McpClient, check_true, report, wait_for_server

GLTF_PATH = "res/editor/assets/RiggedFigure/RiggedFigure.glb"

# Doc tolerances (acceptance criteria 3-7).
ANGLE_TOLERANCE_DEG = 2.0
BEND_CHANGE_MIN_DEG = 10.0
POLE_OFFSET_DEG     = 60.0   # >= 30 deg, criterion 5's placement rule
DISTANCE_TOLERANCE  = 1.0e-3


# --- small vector helpers (no numpy dependency) ---------------------------

def sub(a, b):
    return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]


def add(a, b):
    return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]


def scale(a, s):
    return [a[0] * s, a[1] * s, a[2] * s]


def dot(a, b):
    return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2])


def cross(a, b):
    return [
        (a[1] * b[2]) - (a[2] * b[1]),
        (a[2] * b[0]) - (a[0] * b[2]),
        (a[0] * b[1]) - (a[1] * b[0]),
    ]


def length(a):
    return math.sqrt(dot(a, a))


def normalize(a):
    n = length(a)
    if n < 1.0e-12:
        raise RuntimeError("cannot normalize a zero-length vector")
    return scale(a, 1.0 / n)


def rotate_about(v, axis, angle_rad):
    """Right-handed rotation of v about a unit axis (Rodrigues)."""
    c = math.cos(angle_rad)
    s = math.sin(angle_rad)
    return add(
        add(scale(v, c), scale(cross(axis, v), s)),
        scale(axis, dot(axis, v) * (1.0 - c)),
    )


# --- chain measurements (R11 steps 1-6) -----------------------------------

def chain_axis(positions):
    return normalize(sub(positions[-1], positions[0]))


def perp(point, root, axis):
    d = sub(point, root)
    return sub(d, scale(axis, dot(d, axis)))


def bend_direction(positions):
    """R11 step 3: the normalized sum of the intermediate perpendicular offsets."""
    axis = chain_axis(positions)
    root = positions[0]
    total = [0.0, 0.0, 0.0]
    for point in positions[1:-1]:
        total = add(total, perp(point, root, axis))
    return normalize(total)


def signed_angle_deg(from_vec, to_vec, axis):
    """Signed angle about `axis`, right-handed, in degrees."""
    y = dot(cross(from_vec, to_vec), axis)
    x = dot(from_vec, to_vec)
    return math.degrees(math.atan2(y, x))


def segment_lengths(positions):
    return [length(sub(positions[i + 1], positions[i])) for i in range(len(positions) - 1)]


# --- editor driving -------------------------------------------------------

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


def make_scene(client):
    before = {scene["name"] for scene in client.call("list_scenes")["scenes"]}
    client.call("create_scene")
    advance(client, 6)
    after = [scene["name"] for scene in client.call("list_scenes")["scenes"]]
    new = [name for name in after if name not in before]
    if not new:
        raise RuntimeError("create_scene produced no new scene")
    scene = new[0]
    client.call("import_gltf", {"scene_name": scene, "path": GLTF_PATH})
    wait_idle(client)
    return scene


def ik_drag(client, scene, effector, target):
    """One measured gesture: drag, read the undo growth, then undo it.

    A drag re-solves from the pose the chain is in when it begins (an absolute
    target, doc/plans/rigging/fabrik_ik.md section 5), and FABRIK's unpoled
    solve picks the swivel nearest that pose. So every criterion here drags
    from the SAME pose: the undo restores each joint's drag-start
    parent_from_node exactly, which is what "repeat the drag" means.
    """
    depth = undo_depth(client)
    result = client.call(
        "ik_drag",
        {"scene_name": scene, "node_name": effector, "target": [target[0], target[1], target[2]]},
    )
    positions = [joint["position"] for joint in result["joints"]]
    advance(client, 4)
    undo_delta = undo_depth(client) - depth
    if result["recorded"]:
        client.call("undo")
        advance(client, 4)
    return result, positions, undo_delta


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--effector", default="arm_joint_L_3", help="effector bone name (default arm_joint_L_3)")
    args = parser.parse_args()

    client = McpClient(args.port)
    wait_for_server(client, 60.0)

    # --- criterion 1: fixture scene -------------------------------------
    scene = make_scene(client)
    details = client.call("get_node_details", {"scene_name": scene, "node_name": args.effector})
    start = details["world_transform"]["translation"]
    check_true("1 fixture imported and effector found", True, f"scene={scene} effector={args.effector} at {start}")

    # --- criterion 2: ik_settings attachment (R21) -----------------------
    depth = undo_depth(client)
    client.call("add_node_attachment", {"scene_name": scene, "node_name": args.effector, "type": "ik_settings"})
    advance(client, 6)
    details = client.call("get_node_details", {"scene_name": scene, "node_name": args.effector})
    attachment_id = None
    for attachment in details["attachments"]:
        if attachment.get("type", "") == "Ik_settings":
            attachment_id = attachment["id"]
    undo_delta_attach = undo_depth(client) - depth
    check_true("2 add_node_attachment ik_settings", attachment_id is not None, f"attachment_id={attachment_id}")
    if attachment_id is None:
        close_scene(client, scene)
        return report()

    # --- criterion 3: unpoled drag --------------------------------------
    _, rest_positions, _ = ik_drag(client, scene, args.effector, start)
    rest_lengths = segment_lengths(rest_positions)
    reach = sum(rest_lengths)
    target = [start[0] + (0.15 * reach), start[1], start[2]]

    step3, positions3, undo_delta_drag3 = ik_drag(client, scene, args.effector, target)
    lengths3 = segment_lengths(positions3)
    worst_length_error = max(abs(a - b) for a, b in zip(lengths3, rest_lengths))
    check_true(
        "3 unpoled drag preserves segment lengths and reports no pole",
        (worst_length_error < DISTANCE_TOLERANCE) and (step3["pole"] is None),
        f"worst segment length error={worst_length_error:.3e} pole={step3['pole']} joints={len(positions3)}",
    )
    if len(positions3) < 3:
        check_true("3b chain has an intermediate joint", False, f"chain length {len(positions3)}")
        close_scene(client, scene)
        return report()

    axis = chain_axis(positions3)
    bend3 = bend_direction(positions3)
    distance3 = length(sub(positions3[-1], target))

    # --- criterion 4: poled drag aims the bend at the pole ---------------
    # Placed POLE_OFFSET_DEG about the axis away from the unpoled bend, so
    # criterion 5's "at least 30 degrees away" holds by construction.
    pole_dir = rotate_about(bend3, axis, math.radians(POLE_OFFSET_DEG))
    pole_position = add(positions3[0], scale(pole_dir, reach))
    depth = undo_depth(client)
    client.call(
        "create_node",
        {"scene_name": scene, "name": "ik_pole", "position": [pole_position[0], pole_position[1], pole_position[2]]},
    )
    advance(client, 6)
    pole_details = client.call("get_node_details", {"scene_name": scene, "node_name": "ik_pole"})
    pole_id = pole_details["id"]

    depth = undo_depth(client)
    client.call(
        "set_item_property",
        {"item_id": attachment_id, "property": "pole_target", "reference_id": pole_id},
    )
    advance(client, 6)
    undo_delta_pole = undo_depth(client) - depth

    step4, positions4, undo_delta_drag4 = ik_drag(client, scene, args.effector, target)
    axis4 = chain_axis(positions4)
    bend4 = bend_direction(positions4)
    pole_perp = normalize(perp(pole_position, positions4[0], axis4))
    swivel4 = signed_angle_deg(pole_perp, bend4, axis4)   # pole -> bend, R11 step 5 sign
    distance4 = length(sub(positions4[-1], target))
    check_true(
        "4 poled bend aims at the pole",
        (abs(swivel4) <= ANGLE_TOLERANCE_DEG) and (abs(distance4 - distance3) < DISTANCE_TOLERANCE),
        f"swivel={swivel4:+.3f} deg (limit {ANGLE_TOLERANCE_DEG}), reach error delta={abs(distance4 - distance3):.3e}, pole={step4['pole']}",
    )

    # --- criterion 5: the pole, not the start pose, chose the bend -------
    bend_change = abs(signed_angle_deg(bend3, bend4, axis))
    check_true(
        "5 pole changed the bend by more than 10 degrees",
        bend_change > BEND_CHANGE_MIN_DEG,
        f"bend moved {bend_change:.3f} deg (pole placed {POLE_OFFSET_DEG} deg away)",
    )

    # --- criterion 6: pole_angle ----------------------------------------
    depth = undo_depth(client)
    client.call("set_item_property", {"item_id": attachment_id, "property": "pole_angle", "value": 1.5707963})
    advance(client, 6)
    undo_delta_angle = undo_depth(client) - depth

    step6, positions6, undo_delta_drag6 = ik_drag(client, scene, args.effector, target)
    axis6 = chain_axis(positions6)
    bend6 = bend_direction(positions6)
    pole_perp6 = normalize(perp(pole_position, positions6[0], axis6))
    swivel6 = signed_angle_deg(pole_perp6, bend6, axis6)
    check_true(
        "6 pole_angle of +90 degrees swivels the bend by +90 degrees",
        abs(swivel6 - 90.0) <= ANGLE_TOLERANCE_DEG,
        f"swivel={swivel6:+.3f} deg, error={abs(swivel6 - 90.0):.3f} deg (limit {ANGLE_TOLERANCE_DEG})",
    )

    # --- criterion 7: clearing the pole restores the unpoled solve -------
    depth = undo_depth(client)
    client.call("set_item_property", {"item_id": attachment_id, "property": "pole_target", "value": None})
    advance(client, 6)
    undo_delta_clear = undo_depth(client) - depth

    step7, positions7, undo_delta_drag7 = ik_drag(client, scene, args.effector, target)
    worst_joint_error = max(length(sub(a, b)) for a, b in zip(positions7, positions3))
    check_true(
        "7 clearing pole_target restores the unpoled solve",
        (worst_joint_error < DISTANCE_TOLERANCE) and (step7["pole"] is None),
        f"worst joint error={worst_joint_error:.3e} (limit {DISTANCE_TOLERANCE}), pole={step7['pole']}",
    )

    # --- criterion 8: one undo entry per edit and per drag ---------------
    deltas = {
        "add_node_attachment": undo_delta_attach,
        "set pole_target":     undo_delta_pole,
        "set pole_angle":      undo_delta_angle,
        "clear pole_target":   undo_delta_clear,
        "ik_drag (step 3)":    undo_delta_drag3,
        "ik_drag (step 4)":    undo_delta_drag4,
        "ik_drag (step 6)":    undo_delta_drag6,
        "ik_drag (step 7)":    undo_delta_drag7,
    }
    worst = max(abs(delta - 1) for delta in deltas.values())
    check_true(
        "8 every edit and every drag is exactly one undo step",
        worst == 0,
        ", ".join(f"{name}={delta}" for name, delta in deltas.items()),
    )

    close_scene(client, scene)
    return report()


def close_scene(client, scene):
    try:
        client.call("close_scene", {"scene_name": scene})
        advance(client, 6)
    except RuntimeError as error:
        print(f"  (close_scene failed: {error})")


if __name__ == "__main__":
    sys.exit(main())
