#!/usr/bin/env python3
"""Smoke test: the Transform window's Euler angle row keeps the quaternion sign.

Drives a RUNNING editor over its MCP server (the script does not launch one)
through the same widgets a user types into: the Rotation group's Euler row
of the Transform window (Rotation_inspector, src/editor/transform/).

The inspector derives its Euler angles from the node's quaternion with
erhe::math::quaternion_to_euler_angles (src/erhe/math/erhe_math/
euler_angles.hpp), which gives q and -q different angles, and composes the
quaternion back from the angles directly. Typing one angle therefore
round-trips the quaternion including its sign. A detour through a rotation
matrix cannot tell q from -q and fails these checks.

For each of the 12 Euler orders and for quaternions in both hemispheres
(w > 0 and w < 0):

  1. Set the node's local rotation to q (transform_selection).
  2. Ctrl+click the order's first angle field ("<ORDER>.x"), type the first
     angle this script expects the inspector to show, press Enter.
  3. The node's rotation must be q again - sign included - which holds only
     if the inspector's other two angles reproduce q's hemisphere.

Then one edit that crosses hemispheres: from Z = 270 deg (w < 0) typing
Y = 10 deg must keep w < 0, and typing Z = -90 deg must land on w > 0.

Usage:
  py -3 scripts/rotation_inspector_smoke_test.py [--port 3743] [--wait 30]

Exit code 0 = all checks passed.
"""

import argparse
import math
import sys

from erhe_mcp import McpClient, check_close, check_true, pick_scene, report, wait_for_server

NODE_NAME = "rotation inspector smoke test"
WINDOW    = "Transform"

# Rotation_inspector::c_euler_strings order (the Order combo's entries)
ORDERS = ["XYX", "XZX", "YXY", "YZY", "ZXZ", "ZYZ", "XYZ", "XZY", "YXZ", "YZX", "ZYX", "ZXY"]
AXIS   = {"X": 0, "Y": 1, "Z": 2}


# --- reference math (mirrors erhe_math/euler_angles.hpp), quaternions as (w, x, y, z)

def quat_mul(a, b):
    aw, ax, ay, az = a
    bw, bx, by, bz = b
    return (
        aw * bw - ax * bx - ay * by - az * bz,
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
    )


def axis_quat(axis, angle):
    v = [0.0, 0.0, 0.0]
    v[axis] = math.sin(angle / 2.0)
    return (math.cos(angle / 2.0), v[0], v[1], v[2])


def compose(order, t1, t2, t3):
    a1, a2, a3 = (AXIS[c] for c in order)
    return quat_mul(quat_mul(axis_quat(a1, t1), axis_quat(a2, t2)), axis_quat(a3, t3))


def extract(order, q):
    a1, a2, a3 = (AXIS[c] for c in order)
    w = q[0]
    v = q[1:]
    proper = (a1 == a3)
    other  = 3 - a1 - a2
    parity = 1.0 if (a2 == (a1 + 1) % 3) else -1.0
    q1, q2, qo = v[a1], v[a2], parity * v[other]
    if proper:
        a, b, c, d = w, q1, q2, qo
    else:
        a, b, c, d = w - q2, q1 - qo, w + q2, q1 + qo
    r_ab  = math.hypot(a, b)
    r_cd  = math.hypot(c, d)
    half  = math.atan2(r_cd, r_ab)
    alpha = math.atan2(b, a)
    delta = math.atan2(d, c)
    if proper:
        t2, o1, o3 = 2.0 * half, alpha + delta, alpha - delta
    else:
        t2, o1, o3 = 2.0 * half - math.pi / 2.0, delta + alpha, parity * (delta - alpha)
    wraps = 0
    def wrap(x):
        nonlocal wraps
        if x > math.pi:
            wraps += 1
            return x - 2.0 * math.pi
        if x <= -math.pi:
            wraps += 1
            return x + 2.0 * math.pi
        return x
    o1, o3 = wrap(o1), wrap(o3)
    if wraps % 2 == 1:
        if abs(o3) > abs(o1):
            o3 = o3 - 2.0 * math.pi if o3 > 0.0 else o3 + 2.0 * math.pi
        else:
            o1 = o1 - 2.0 * math.pi if o1 > 0.0 else o1 + 2.0 * math.pi
    return o1, t2, o3


# --- editor driving

def settle(client, frames=3):
    for _ in range(frames):
        client.call("advance_time", {"seconds": 0.016})


def node_rotation_wxyz(client, scene):
    details = client.call("get_node_details", {"scene_name": scene, "node_name": NODE_NAME})
    x, y, z, w = details["local_transform"]["rotation_xyzw"]
    return (w, x, y, z)


def set_node_rotation(client, q_wxyz):
    w, x, y, z = q_wxyz
    client.call("transform_selection", {"space": "local", "rotation_xyzw": [x, y, z, w]})
    settle(client)


def find_item(client, label):
    items = client.call("get_imgui_items", {
        "window": WINDOW, "label_contains": label, "visible_only": False, "limit": 20
    })
    for item in items.get("items", []):
        if item.get("display_label", "") == label:
            return item
    return None


def visible_item(client, label):
    client.call("imgui_scroll", {"window": WINDOW, "dy": 100.0})
    for _ in range(60):
        item = find_item(client, label)
        if item is None:
            return None
        if item.get("status", {}).get("visible", False):
            return item
        client.call("imgui_scroll", {"window": WINDOW, "dy": -2.0})
    return None


def select_order(client, order):
    if find_item(client, order + ".x") is not None:
        return True
    # The combo is recorded without the visible flag even while on screen,
    # which imgui_click refuses; click its center instead.
    combo = find_item(client, "Order")
    if combo is None:
        return False
    client.call("mouse_click", {"x": combo["center_x"], "y": combo["center_y"]})
    settle(client)
    # The popup lists 8 of the 12 orders; the ones scrolled out of its view
    # are not submitted at all. Scroll it to the top, then down step by step.
    popup = None
    for window in client.call("get_imgui_windows", {}).get("windows", []):
        if window.get("name", "").startswith("##Combo") and not window.get("hidden", True):
            popup = window["name"]
    if popup is None:
        return False
    client.call("imgui_scroll", {"window": popup, "dy": 20.0})
    for _ in range(12):
        items = client.call("get_imgui_items", {"window": popup, "label_contains": order, "limit": 20})
        entry = next((i for i in items.get("items", []) if i.get("display_label", "") == order), None)
        if entry is not None:
            client.call("mouse_click", {"x": entry["center_x"], "y": entry["center_y"]})
            break
        client.call("imgui_scroll", {"window": popup, "dy": -1.0})
    settle(client)
    return find_item(client, order + ".x") is not None


def type_into(client, label, text):
    item = visible_item(client, label)
    if item is None:
        return False
    client.call("mouse_click", {"x": item["center_x"], "y": item["center_y"], "modifiers": ["ctrl"]})
    client.call("type_text", {"text": text})
    client.call("key_press", {"key": "enter"})
    settle(client)
    return True


def fmt(q):
    return "(" + ", ".join(f"{c:+.5f}" for c in q) + ")"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port",  type=int,   default=None, help="MCP server port (default ERHE_MCP_PORT or 3743)")
    parser.add_argument("--scene", type=str,   default="",   help="Scene name (default: first scene)")
    parser.add_argument("--wait",  type=float, default=30.0, help="Seconds to wait for the MCP server")
    args = parser.parse_args()

    client = McpClient(args.port) if args.port is not None else McpClient()
    wait_for_server(client, args.wait)
    scene = pick_scene(client, args.scene)
    print(f"Scene: {scene}")

    client.call("create_node", {"scene_name": scene, "name": NODE_NAME})
    client.call("select_items", {"scene_name": scene, "paths": [NODE_NAME]})
    client.call("set_window_visibility", {"title": WINDOW, "visible": True, "focus": True})
    settle(client, 6)

    try:
        # Both hemispheres of the same two rotations, away from gimbal lock
        # for every order.
        base = [
            (0.35, 0.55, -0.25, 0.70),
            (0.80, -0.10, 0.45, 0.30),
        ]
        samples = []
        for q in base:
            n = math.sqrt(sum(c * c for c in q))
            q = tuple(c / n for c in q)
            samples.append(q)
            samples.append(tuple(-c for c in q))

        for order in ORDERS:
            print(f"Order {order}")
            if not select_order(client, order):
                check_true(f"{order}: Order combo selects the order", False)
                continue
            for q in samples:
                set_node_rotation(client, q)
                t1, _, _ = extract(order, q)
                if not type_into(client, order + ".x", f"{math.degrees(t1):.6f}"):
                    check_true(f"{order}: '{order}.x' field reachable", False)
                    break
                back = node_rotation_wxyz(client, scene)
                check_close(f"{order} q={fmt(q)} round-trips with its sign", back, q, 2.0e-4)

        print("Hemisphere crossing (ZYX)")
        select_order(client, "ZYX")
        q_270 = compose("ZYX", math.radians(270.0), 0.0, 0.0)
        set_node_rotation(client, q_270)
        check_true("Z 270 deg starts with w < 0", node_rotation_wxyz(client, scene)[0] < 0.0)

        type_into(client, "ZYX.y", "10")
        expected = compose("ZYX", math.radians(270.0), math.radians(10.0), 0.0)
        back = node_rotation_wxyz(client, scene)
        check_close("typing Y = 10 keeps Z = 270 (w < 0)", back, expected, 2.0e-4)

        type_into(client, "ZYX.x", "-90")
        expected = compose("ZYX", math.radians(-90.0), math.radians(10.0), 0.0)
        back = node_rotation_wxyz(client, scene)
        check_close("typing Z = -90 lands on the w > 0 hemisphere", back, expected, 2.0e-4)
        check_true("w > 0 after Z = -90", back[0] > 0.0, f"w = {back[0]:+.5f}")
    finally:
        client.call("select_items", {"scene_name": scene, "paths": []})
        client.call("delete_nodes", {"scene_name": scene, "names": [NODE_NAME]})
        settle(client)

    return report()


if __name__ == "__main__":
    sys.exit(main())
