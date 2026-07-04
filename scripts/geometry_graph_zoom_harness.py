#!/usr/bin/env python3
"""Zoom-quality screenshot harness for the node editor (issue #251).

Drives the in-editor MCP server (headless Vulkan build) to stand up a known,
text-rich geometry graph, then captures the Geometry Graph window at a sequence
of zoom levels so before/after (blurry vs native-resolution) renderings can be
compared by eye.

The graph is deliberately small but exercises node titles, parameter widgets,
pins and links - the things that pixelate under the old fake-space vertex
post-transform. Screenshots go to logs/zoom_<label>_<zoom>.png; READ the PNGs
(do not just diff file sizes) to judge text crispness at 2x / 4x.

Usage (headless editor already running, see the erhe-headless-verify skill):
    py -3 scripts/geometry_graph_zoom_harness.py [--label baseline] [--port 8080]

Commit this script; do NOT commit the captured images.

Enabling MCP tool: geometry_graph_set_view {zoom} shows the window and sets the
node-editor view scale immediately (EditorContext::SetZoom, greenfield for #251).
"""

import argparse
import json
import time
import urllib.request

ZOOM_LEVELS = [0.5, 1.0, 2.0, 4.0]


def make_rpc(port):
    def rpc(method, params, timeout=180):
        body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": method, "params": params}).encode("utf-8")
        request = urllib.request.Request(
            f"http://127.0.0.1:{port}/mcp", data=body, headers={"Content-Type": "application/json"}
        )
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.loads(response.read().decode("utf-8"))
    return rpc


def make_call(rpc):
    def call(tool, args=None, timeout=180):
        payload = rpc("tools/call", {"name": tool, "arguments": args or {}}, timeout=timeout)
        if "error" in payload:
            raise RuntimeError(f"{tool}: {payload['error']}")
        result = payload.get("result", {})
        content = result.get("content")
        text = content[0]["text"] if (isinstance(content, list) and content and "text" in content[0]) else ""
        if result.get("isError", False):
            raise RuntimeError(f"{tool}: {text}")
        try:
            return json.loads(text)
        except (json.JSONDecodeError, TypeError):
            return text
    return call


def build_known_graph(call):
    """box -> subdivide -> output, plus a standalone math node. Deterministic
    node types with visible titles/params/pins for crispness comparison."""
    scene_name = call("list_scenes")["scenes"][0]["name"]
    call("create_graph_mesh", {"name": "Zoom Harness", "scene_name": scene_name})

    box = call("geometry_graph_add_node", {"type": "box"})["id"]
    subdivide = call("geometry_graph_add_node", {"type": "subdivide"})["id"]
    output = call("geometry_graph_add_node", {"type": "output"})["id"]
    call("geometry_graph_add_node", {"type": "math"})  # extra widgets on screen

    call("geometry_graph_connect", {"source_node_id": box, "source_slot": 0,
                                     "sink_node_id": subdivide, "sink_slot": 0})
    call("geometry_graph_connect", {"source_node_id": subdivide, "source_slot": 0,
                                     "sink_node_id": output, "sink_slot": 0})
    # Wait for the background evaluation to settle so nodes are laid out.
    call("get_geometry_graph")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", default="baseline", help="filename tag, e.g. baseline / after")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()

    rpc = make_rpc(args.port)
    call = make_call(rpc)

    print(f"[harness] building known graph ...")
    build_known_graph(call)

    # First set_view shows the window and lets it lay out at least one frame so
    # the canvas widget rect (needed to center the content) is valid.
    call("geometry_graph_set_view", {"zoom": 1.0})
    time.sleep(1.0)

    for zoom in ZOOM_LEVELS:
        call("geometry_graph_set_view", {"zoom": zoom})
        time.sleep(0.6)  # let a few frames render at the new zoom
        path = f"logs/zoom_{args.label}_{zoom:g}.png"
        call("capture_screenshot", {"path": path})
        print(f"[harness] zoom {zoom:g} -> {path}")

    print("[harness] done. READ the PNGs to judge text crispness (2x / 4x).")


if __name__ == "__main__":
    main()
