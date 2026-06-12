#!/usr/bin/env python3
"""Integration test for the editor's transform_selection MCP tool.

Drives a running editor instance (MCP server on http://127.0.0.1:<port>/mcp)
through the same Transform tool code path as the Transform window numeric
entry:

  1. Places two static brush instances and reparents one under the other, so
     the child node has a non-identity parent transform.
  2. Local-space translation: the value must land in parent space.
  3. Drift regression: re-applying the same local value with end_edit=false
     (equivalent to an active drag, where the edit baselines stay frozen)
     must be idempotent. Before the 2026-06 fix the child ran away by the
     parent's world offset on every application.
  4. Global-space translation: the value must land in world space.
  5. Local-space rotation and scale spot checks.

Usage:
  py -3 scripts/mcp_transform_test.py [--port 8080] [--scene NAME] [--wait 30]

The editor must already be running. Exit code 0 = all checks passed.
"""

import argparse
import math
import sys

from erhe_mcp import McpClient, check_close, pick_scene, report, wait_for_server


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port",  type=int,   default=8080, help="MCP server port (default 8080)")
    parser.add_argument("--scene", type=str,   default="",   help="Scene name (default: first scene)")
    parser.add_argument("--wait",  type=float, default=30.0, help="Seconds to wait for the MCP server")
    args = parser.parse_args()

    client = McpClient(args.port)
    print(f"Waiting for MCP server on port {args.port} ...")
    wait_for_server(client, args.wait)

    if "transform_selection" not in client.tool_names():
        print("FAIL: transform_selection tool is not registered")
        return 1

    scene_name = pick_scene(client, args.scene)
    print(f"Using scene: {scene_name}")

    brushes = client.call("get_scene_brushes", {"scene_name": scene_name}).get("brushes", [])
    if not brushes:
        print("FAIL: no brushes available in scene")
        return 1
    brush_id = brushes[0]["id"]

    # Parent away from the origin so a local/world space mix-up cannot hide.
    parent_pos = [5.0, 1.0, 0.0]
    parent = client.call("place_brush", {
        "scene_name": scene_name, "brush_id": brush_id, "position": parent_pos, "motion_mode": "static",
    })
    child = client.call("place_brush", {
        "scene_name": scene_name, "brush_id": brush_id, "position": [0.0, 1.0, 0.0], "motion_mode": "static",
    })
    print(f"Placed parent '{parent['node_name']}' (id {parent['node_id']}) and child '{child['node_name']}' (id {child['node_id']})")

    client.call("reparent_node", {
        "scene_name": scene_name, "node_id": child["node_id"], "parent_node_id": parent["node_id"],
    })

    parent_details = client.call("get_node_details", {"scene_name": scene_name, "node_name": parent["node_name"]})
    parent_world_t = parent_details["world_transform"]["translation"]
    check_close("parent world translation (setup)", parent_world_t, parent_pos)

    client.call("select_items", {"scene_name": scene_name, "ids": [child["node_id"]]})

    print("Local space translation:")
    local_t = [1.0, 2.0, 3.0]
    result = client.call("transform_selection", {"space": "local", "translation": local_t})
    node = result["nodes"][0]
    check_close("child local translation", node["local_transform"]["translation"], local_t)
    expected_world = [p + l for p, l in zip(parent_world_t, local_t)]
    check_close("child world translation", node["world_transform"]["translation"], expected_world)

    print("Drift regression (repeated application with frozen baselines):")
    for i in range(3):
        result = client.call("transform_selection", {"space": "local", "translation": local_t, "end_edit": False})
        node = result["nodes"][0]
        check_close(f"local translation stable, application {i + 1}", node["local_transform"]["translation"], local_t)
    client.call("transform_selection", {"space": "local", "translation": local_t})  # close the edit session

    print("Global space translation:")
    world_t = [7.0, 2.0, -1.0]
    result = client.call("transform_selection", {"space": "global", "translation": world_t})
    node = result["nodes"][0]
    check_close("child world translation", node["world_transform"]["translation"], world_t)
    expected_local = [w - p for w, p in zip(world_t, parent_world_t)]
    check_close("child local translation", node["local_transform"]["translation"], expected_local)

    print("Local space rotation (90 degrees about Y):")
    half = math.sin(math.radians(45.0))
    rotation = [0.0, half, 0.0, math.cos(math.radians(45.0))]
    result = client.call("transform_selection", {"space": "local", "rotation_xyzw": rotation})
    node = result["nodes"][0]
    actual_q = node["local_transform"]["rotation_xyzw"]
    if all(math.copysign(1.0, a) != math.copysign(1.0, e) for a, e in zip(actual_q, rotation) if abs(e) > 1.0e-6):
        actual_q = [-c for c in actual_q]  # q and -q are the same rotation
    check_close("child local rotation", actual_q, rotation)

    print("Local space scale:")
    scale = [2.0, 3.0, 4.0]
    result = client.call("transform_selection", {"space": "local", "scale": scale})
    node = result["nodes"][0]
    check_close("child local scale", node["local_transform"]["scale"], scale)

    return report()


if __name__ == "__main__":
    sys.exit(main())
