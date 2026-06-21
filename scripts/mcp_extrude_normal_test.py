"""Drive the editor over MCP to verify the mesh-component transform modes.

Exercises the shared component-transform path plus the Extrude / Extrude (Group
Normal) / Extrude (Vertex Normal) modes end to end:

  - set_transform_mode (MCP tool) sets / reads back
    move | extrude | extrude_group_normal | extrude_vertex_normal.
  - A face component selection + transform_selection(end_edit=true) now commits the
    component edit (move queues a vertex move; the extrude modes swap in an extruded
    primitive and grow the geometry).
  - Plain "move" must NOT change the topology (vertex/facet counts stay equal); all
    extrude modes MUST grow it; the undo stack must carry the matching operation.

Usage: launch build_*/src/editor/<Config>/editor.exe (cwd = repo root), then
    py -3 scripts/mcp_extrude_normal_test.py
Env: ERHE_MCP_PORT (default 8080), ERHE_MCP_TIMEOUT_S (default 30).
"""

import os
import sys
import uuid

from erhe_mcp import (
    McpClient,
    check_true,
    pick_scene,
    report,
    wait_for_server,
)


def make_box(client: McpClient, scene: str, name: str) -> tuple[int, str]:
    result = client.call("create_shape", {
        "scene_name": scene,
        "shape":      "box",
        "name":       name,
        "instance":   True,
        "size":       [2.0, 2.0, 2.0],
        "position":   [0.0, 0.0, 0.0],
        "motion_mode": "static",
    })
    return int(result["node_id"]), str(result["node_name"])


def geometry_counts(client: McpClient, scene: str, node_name: str) -> tuple[int, int]:
    details = client.call("get_node_details", {"scene_name": scene, "node_name": node_name})
    for att in details.get("attachments", []):
        if "vertex_count" in att:
            return int(att["vertex_count"]), int(att["facet_count"])
    raise RuntimeError(f"no mesh attachment with counts for node {node_name}: {details}")


def undo_descriptions(client: McpClient) -> list[str]:
    stack = client.call("get_undo_redo_stack")
    return [entry.get("description", "") for entry in stack.get("undo", [])]


def run_case(client: McpClient, scene: str, name: str, mode: str, expect_grows: bool, expect_label: str) -> None:
    print(f"\n--- {mode} (expect topology {'grows' if expect_grows else 'unchanged'}) ---")
    node_id, node_name = make_box(client, scene, name)

    # Select a single face (facet 0) of the fresh box.
    client.call("select_mesh_components", {
        "scene_name": scene,
        "node_id":    node_id,
        "mode":       "face",
        "facets":     [0],
    })
    sel = client.call("get_mesh_component_selection")
    selected_facets = sum(len(e.get("facets", [])) for e in sel.get("entries", []))
    check_true(f"{mode}: one facet selected", selected_facets == 1, f"facets={selected_facets}")

    # Set + read back the transform mode through the new MCP tool.
    set_res = client.call("set_transform_mode", {"mode": mode})
    check_true(f"{mode}: set_transform_mode echoes mode", set_res.get("mode") == mode, str(set_res))
    readback = client.call("get_selection").get("transform_mode")
    check_true(f"{mode}: get_selection reports mode", readback == mode, f"transform_mode={readback}")

    before_v, before_f = geometry_counts(client, scene, node_name)

    # The mesh-component selection alone drives the gizmo (component_mode); apply a
    # large +Y translation in world space and commit (end_edit=true).
    client.call("transform_selection", {
        "space":       "global",
        "translation": [0.0, 8.0, 0.0],
        "end_edit":    True,
    })

    after_v, after_f = geometry_counts(client, scene, node_name)
    print(f"  counts: vertices {before_v}->{after_v}, facets {before_f}->{after_f}")

    if expect_grows:
        check_true(f"{mode}: vertex count grew", after_v > before_v, f"{before_v}->{after_v}")
        check_true(f"{mode}: facet count grew", after_f > before_f, f"{before_f}->{after_f}")
    else:
        check_true(f"{mode}: vertex count unchanged", after_v == before_v, f"{before_v}->{after_v}")
        check_true(f"{mode}: facet count unchanged", after_f == before_f, f"{before_f}->{after_f}")

    descs = undo_descriptions(client)
    matches = [d for d in descs if expect_label in d]
    print(f"  committed op: {matches[:1]}")
    check_true(f"{mode}: committed '{expect_label}' operation", len(matches) > 0, f"not found in {descs[-3:]}")


def main() -> int:
    port = int(os.environ.get("ERHE_MCP_PORT", "8080"))
    timeout_s = float(os.environ.get("ERHE_MCP_TIMEOUT_S", "30"))
    client = McpClient(port)
    wait_for_server(client, timeout_s)

    tools = client.tool_names()
    check_true("set_transform_mode tool registered", "set_transform_mode" in tools)

    scene = pick_scene(client, os.environ.get("ERHE_MCP_SCENE", ""))
    print(f"scene: {scene}")

    # Unique per-run name prefix so re-running against the same live editor never
    # resolves get_node_details to a stale box from a previous run (names are the only
    # lookup key, and create_shape does not uniquify across runs).
    tag = uuid.uuid4().hex[:8]
    run_case(client, scene, f"xn_{tag}_move",     "move",                  expect_grows=False, expect_label="Move")
    run_case(client, scene, f"xn_{tag}_extrude",  "extrude",               expect_grows=True,  expect_label="Extrude")
    run_case(client, scene, f"xn_{tag}_groupn",   "extrude_group_normal",  expect_grows=True,  expect_label="Extrude (Group Normal)")
    run_case(client, scene, f"xn_{tag}_vertexn",  "extrude_vertex_normal", expect_grows=True,  expect_label="Extrude (Vertex Normal)")

    # Leave the editor in the default mode.
    client.call("set_transform_mode", {"mode": "move"})
    return report()


if __name__ == "__main__":
    sys.exit(main())
