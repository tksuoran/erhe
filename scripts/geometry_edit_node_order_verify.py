#!/usr/bin/env python3
"""Verify that geometry edits keep the mesh node's place in the hierarchy.

doc/editor/operations.md "Primitive swaps keep the node in place": the
operations that swap a mesh's primitives (Mesh_operation,
Set_geometry_attribute, Move_mesh_vertices, Fork_geometry, Merge) leave the
Scene Hierarchy's sibling order as it was, on execute and on undo.

Launches a headless editor (ERHE_AI_DRIVER=1), creates three boxes and after
every operation and every undo compares the get_scene_nodes order against the
baseline.

Usage:
    py -3 scripts/geometry_edit_node_order_verify.py [--editor <path to editor.exe>]

Exit code 0 when every check passes.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from erhe_mcp import McpClient, check_true, report  # noqa: E402
from geometry_spreadsheet_verify import DEFAULT_EDITOR, launch_editor  # noqa: E402

BOXES = ["order_a", "order_b", "order_c"]


class Editor:
    def __init__(self, client, scene):
        self.c = client
        self.scene = scene

    def call(self, tool, args=None):
        return self.c.call(tool, args or {})

    def advance(self, frames=3):
        for _ in range(frames):
            self.call("advance_time", {"seconds": 0.016})

    def order(self):
        """(parent id, name) of every node, in scene tree walk order."""
        nodes = self.call("get_scene_nodes", {"scene_name": self.scene})["nodes"]
        return [(node.get("parent_id"), node["name"]) for node in nodes]

    def top_undo(self):
        undo = self.call("get_undo_redo_stack")["undo"]
        return undo[-1]["description"] if undo else ""

    def undo(self):
        self.call("undo", {})
        self.advance(3)

    def select(self, names):
        self.call("select_items", {"scene_name": self.scene, "paths": names})
        self.advance(3)


def check_order(e, label, expected):
    actual = e.order()
    detail = "" if actual == expected else f"expected {[n for _, n in expected]}, got {[n for _, n in actual]}"
    check_true(label, actual == expected, detail)


def check_operation(e, label, baseline, run):
    """Run one edit, then its undo; the order must match the baseline after each."""
    run()
    e.advance(3)
    check_order(e, f"{label}: order kept ({e.top_undo()})", baseline)
    e.undo()
    check_order(e, f"{label}: order kept after undo", baseline)


def run_checks(e):
    for name in BOXES:
        e.call("create_shape", {"scene_name": e.scene, "shape": "box", "name": name})
    e.advance(3)
    baseline = e.order()
    check_true("Boxes created", all(any(n == name for _, n in baseline) for name in BOXES))

    middle = BOXES[1]

    check_operation(e, "Catmull-Clark (Mesh_operation)", baseline,
                    lambda: e.call("catmull_clark", {"scene_name": e.scene, "node_name": middle}))

    check_operation(e, "Corner color (Set_geometry_attribute_operation)", baseline,
                    lambda: e.call("set_mesh_attribute_values", {
                        "scene_name": e.scene, "node_name": middle, "attribute": "corner_color_0",
                        "elements": [0, 1, 2, 3], "value": [1.0, 0.0, 0.0, 1.0]}))

    check_operation(e, "Vertex position cell edit", baseline,
                    lambda: e.call("set_mesh_attribute_values", {
                        "scene_name": e.scene, "node_name": middle, "attribute": "position",
                        "elements": [0], "value": [0.75, 0.5, 0.5]}))

    def move_vertices():
        e.call("select_mesh_components", {"scene_name": e.scene, "node_name": middle, "mode": "vertex", "vertices": [0, 1]})
        e.advance(3)
        e.call("transform_selection", {"translation": [0.0, 0.25, 0.0]})

    check_operation(e, "Vertex selection transform (Mesh_component_transform)", baseline, move_vertices)
    e.call("clear_mesh_component_selection", {})
    e.call("set_mesh_component_mode", {"mode": "object"})
    e.advance(3)

    # A pasted copy shares the Geometry, so a vertex move on it forks first.
    e.call("clipboard_copy_nodes", {"scene_name": e.scene, "node_ids": [
        node["id"] for node in e.call("get_scene_nodes", {"scene_name": e.scene})["nodes"] if node["name"] == middle
    ]})
    e.select([BOXES[0]])
    e.call("clipboard_paste", {"scene_name": e.scene})
    e.advance(5)
    shared_baseline = e.order()
    pasted = [name for _, name in shared_baseline if name not in {n for _, n in baseline}]
    check_true("Paste created one copy", len(pasted) == 1, f"new nodes {pasted}")
    if len(pasted) == 1:
        def fork_move():
            e.call("select_mesh_components", {"scene_name": e.scene, "node_name": pasted[0], "mode": "vertex", "vertices": [0]})
            e.advance(3)
            e.call("transform_selection", {"translation": [0.0, 0.25, 0.0]})

        check_operation(e, "Vertex selection transform on shared geometry (fork)", shared_baseline, fork_move)
        e.call("clear_mesh_component_selection", {})
        e.call("set_mesh_component_mode", {"mode": "object"})
        e.advance(3)
    e.undo()  # the paste
    check_order(e, "Paste undone", baseline)

    # Merge the outer two boxes: the middle one stays between the positions
    # the merged sources held.
    e.call("set_window_visibility", {"title": "Operations", "visible": True, "focus": True})
    e.advance(3)
    e.select([BOXES[0], BOXES[2]])
    e.call("imgui_click", {"window": "Operations", "label": "Merge"})
    e.advance(5)
    merged = e.order()
    remaining = [entry for entry in merged if entry[1] in BOXES]
    check_true(f"Merge removed one source ({e.top_undo()})", len(remaining) == 2, f"{remaining}")
    kept = [entry for entry in baseline if entry in merged] == merged
    check_true("Merge keeps the survivor's place", kept,
               "" if kept else f"baseline {[n for _, n in baseline]}, merged {[n for _, n in merged]}")
    e.undo()
    check_order(e, "Merge undo restores the sources at their places", baseline)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", default=DEFAULT_EDITOR)
    args = parser.parse_args()

    process, port = launch_editor(args.editor)
    try:
        client = McpClient(port)
        scene = client.call("list_scenes")["scenes"][0]["name"]
        run_checks(Editor(client, scene))
    finally:
        try:
            McpClient(port).call("request_exit", {})
            process.wait(timeout=30)
        except Exception:
            process.kill()
    return report()


if __name__ == "__main__":
    sys.exit(main())
