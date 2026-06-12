#!/usr/bin/env python3
"""Integration test for the editor's create_shape MCP tool.

Drives a running editor instance (MCP server on http://127.0.0.1:<port>/mcp)
through the same shape generators as the Create window:

  1. Tapered capsule instance: world position, mesh, Jolt tapered capsule
     collision shape, static motion mode.
  2. Uniform capsule: regular (non-tapered) Jolt capsule collision shape.
  3. Invalid tapered capsule (length <= |bottom_radius - top_radius|) must be
     rejected with an error.
  4. Box / uv_sphere / torus / cone smoke checks.
  5. add_brush round trip: create_shape with instance=false adds a brush to
     the content library, place_brush instantiates it.
  6. parent_node_id: instance is parented under the given node with its world
     position preserved.

Usage:
  py -3 scripts/mcp_create_shape_test.py [--port 8080] [--scene NAME] [--wait 30]

The editor must already be running. Exit code 0 = all checks passed.
"""

import argparse
import sys

from erhe_mcp import McpClient, check_close, check_true, pick_scene, report, wait_for_server


def get_physics_attachment(details: dict) -> dict:
    for attachment in details.get("attachments", []):
        if attachment.get("type") == "Node_physics":
            return attachment
    return {}


def get_mesh_attachment(details: dict) -> dict:
    for attachment in details.get("attachments", []):
        if attachment.get("type") == "Mesh":
            return attachment
    return {}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port",  type=int,   default=8080, help="MCP server port (default 8080)")
    parser.add_argument("--scene", type=str,   default="",   help="Scene name (default: first scene)")
    parser.add_argument("--wait",  type=float, default=30.0, help="Seconds to wait for the MCP server")
    args = parser.parse_args()

    client = McpClient(args.port)
    print(f"Waiting for MCP server on port {args.port} ...")
    wait_for_server(client, args.wait)

    if "create_shape" not in client.tool_names():
        print("FAIL: create_shape tool is not registered")
        return 1

    scene_name = pick_scene(client, args.scene)
    print(f"Using scene: {scene_name}")

    print("Tapered capsule instance:")
    result = client.call("create_shape", {
        "scene_name":    scene_name,
        "shape":         "capsule",
        "name":          "mcp_tapered_capsule",
        "position":      [2.0, 1.0, 0.0],
        "motion_mode":   "static",
        "length":        1.2,
        "bottom_radius": 0.6,
        "top_radius":    0.3,
    })
    check_true("response reports tapered", result["parameters"]["tapered"] is True)
    check_true("instance node created", "node_id" in result, str(result))
    details = client.call("get_node_details", {"scene_name": scene_name, "node_name": result["node_name"]})
    check_close("world position", details["world_transform"]["translation"], [2.0, 1.0, 0.0])
    mesh = get_mesh_attachment(details)
    check_true("mesh attachment with geometry", mesh.get("vertex_count", 0) > 0, f"vertex_count={mesh.get('vertex_count')}")
    physics = get_physics_attachment(details)
    check_true(
        "tapered capsule collision shape",
        physics.get("collision_shape") == "Jolt_tapered_capsule_shape",
        f"collision_shape={physics.get('collision_shape')}"
    )
    check_true("static motion mode", physics.get("motion_mode") == "static", f"motion_mode={physics.get('motion_mode')}")

    print("Uniform capsule instance:")
    result = client.call("create_shape", {
        "scene_name":    scene_name,
        "shape":         "capsule",
        "name":          "mcp_uniform_capsule",
        "position":      [4.0, 1.0, 0.0],
        "motion_mode":   "static",
        "length":        1.0,
        "bottom_radius": 0.4,
        "top_radius":    0.4,
    })
    check_true("response reports not tapered", result["parameters"]["tapered"] is False)
    details = client.call("get_node_details", {"scene_name": scene_name, "node_name": result["node_name"]})
    physics = get_physics_attachment(details)
    check_true(
        "regular capsule collision shape",
        physics.get("collision_shape") == "Jolt_capsule_shape",
        f"collision_shape={physics.get('collision_shape')}"
    )

    print("Invalid tapered capsule is rejected:")
    try:
        client.call("create_shape", {
            "scene_name":    scene_name,
            "shape":         "capsule",
            "length":        0.5,
            "bottom_radius": 1.0,
            "top_radius":    0.2,
        })
        check_true("length <= |bottom_radius - top_radius| rejected", False, "call unexpectedly succeeded")
    except RuntimeError as error:
        check_true("length <= |bottom_radius - top_radius| rejected", "length" in str(error), str(error))

    print("Other shapes:")
    smoke_shapes = [
        ("box",       {"size": [2.0, 1.0, 3.0]},                       [-2.0, 1.0, 0.0]),
        ("uv_sphere", {"radius": 0.7},                                 [-4.0, 1.0, 0.0]),
        ("torus",     {"major_radius": 1.5, "minor_radius": 0.25},     [-6.0, 1.0, 0.0]),
        ("cone",      {"height": 2.0, "bottom_radius": 0.8},           [-8.0, 1.0, 0.0]),
    ]
    for shape, parameters, position in smoke_shapes:
        arguments = {
            "scene_name":  scene_name,
            "shape":       shape,
            "name":        f"mcp_{shape}",
            "position":    position,
            "motion_mode": "static",
        }
        arguments.update(parameters)
        result = client.call("create_shape", arguments)
        details = client.call("get_node_details", {"scene_name": scene_name, "node_name": result["node_name"]})
        check_close(f"{shape} world position", details["world_transform"]["translation"], position)
        mesh = get_mesh_attachment(details)
        check_true(f"{shape} mesh attachment", mesh.get("vertex_count", 0) > 0, f"vertex_count={mesh.get('vertex_count')}")

    print("add_brush round trip (create_shape -> place_brush):")
    result = client.call("create_shape", {
        "scene_name": scene_name,
        "shape":      "uv_sphere",
        "name":       "mcp_brush_sphere",
        "radius":     0.5,
        "instance":   False,
        "add_brush":  True,
    })
    check_true("brush added to library", "brush_id" in result, str(result))
    check_true("no instance created", "node_id" not in result)
    placed = client.call("place_brush", {
        "scene_name":  scene_name,
        "brush_id":    result["brush_id"],
        "position":    [0.0, 2.0, 5.0],
        "motion_mode": "static",
    })
    details = client.call("get_node_details", {"scene_name": scene_name, "node_name": placed["node_name"]})
    check_close("placed brush world position", details["world_transform"]["translation"], [0.0, 2.0, 5.0])

    print("parent_node_id:")
    parent = client.call("create_shape", {
        "scene_name":  scene_name,
        "shape":       "box",
        "name":        "mcp_parent_box",
        "position":    [3.0, 1.0, 0.0],
        "motion_mode": "static",
    })
    child = client.call("create_shape", {
        "scene_name":     scene_name,
        "shape":          "capsule",
        "name":           "mcp_child_capsule",
        "position":       [3.0, 2.0, 0.0],
        "motion_mode":    "static",
        "parent_node_id": parent["node_id"],
    })
    check_true("parent reported", child.get("parent") == "mcp_parent_box", f"parent={child.get('parent')}")
    details = client.call("get_node_details", {"scene_name": scene_name, "node_name": child["node_name"]})
    check_true("parented under box", details["parent"] == "mcp_parent_box", f"parent={details['parent']}")
    check_close("child world position preserved", details["world_transform"]["translation"], [3.0, 2.0, 0.0])
    check_close("child local translation relative to parent", details["local_transform"]["translation"], [0.0, 1.0, 0.0])

    return report()


if __name__ == "__main__":
    sys.exit(main())
