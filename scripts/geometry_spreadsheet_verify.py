#!/usr/bin/env python3
"""Verify the Geometry Spreadsheet window (doc/editor/geometry_spreadsheet.md) over MCP.

Launches a headless editor (ERHE_AI_DRIVER=1), creates a box, opens the
Geometry Spreadsheet window and checks, against get_mesh_attribute_values,
that the cells the window formats match the geometry; then exercises the
domain tabs, sorting, scrolling (only visible rows drawn), geometry swaps
(Catmull-Clark + undo), pinning, hiding and target removal.

Usage:
    py -3 scripts/geometry_spreadsheet_verify.py [--editor <path to editor.exe>]

Exit code 0 when every check passes.
"""

import argparse
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from erhe_mcp import McpClient, check_true, report  # noqa: E402

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG_PATH = os.path.join(REPO_ROOT, "logs", "log.txt")
DEFAULT_EDITOR = os.path.join("build_vs2026_vulkan_headless", "bin", "Debug", "editor.exe")
WINDOW = "Geometry Spreadsheet"
BOX = "gs_box"
TOLERANCE = 1.0e-4  # the window shows 4 decimals by default


def launch_editor(editor_exe):
    exe = editor_exe if os.path.isabs(editor_exe) else os.path.join(REPO_ROOT, editor_exe)
    if not os.path.isfile(exe):
        raise RuntimeError(f"no editor at {exe}")
    os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
    open(LOG_PATH, "w").close()
    env = dict(os.environ, ERHE_AI_DRIVER="1")
    flags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
    process = subprocess.Popen([exe], cwd=REPO_ROOT, env=env, creationflags=flags,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.monotonic() + 180.0
    pattern = re.compile(r"MCP server: listening on 127\.0\.0\.1:(\d+)")
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"editor exited with {process.returncode} during startup; see logs/log.txt")
        try:
            with open(LOG_PATH, "r", encoding="utf-8", errors="replace") as handle:
                text = handle.read()
            match = pattern.search(text)
            if match and ("Main loop: completed frame 12" in text):
                return process, int(match.group(1))
        except OSError:
            pass
        time.sleep(1.0)
    process.kill()
    raise RuntimeError("editor did not become ready")


class Editor:
    def __init__(self, client, scene):
        self.c = client
        self.scene = scene

    def call(self, tool, args=None):
        return self.c.call(tool, args or {})

    def advance(self, frames=3):
        for _ in range(frames):
            self.call("advance_time", {"seconds": 0.016})

    def sheet(self, **kwargs):
        return self.call("get_geometry_spreadsheet", kwargs)

    def click(self, label, **kwargs):
        args = {"window": WINDOW, "label": label}
        args.update(kwargs)
        self.call("imgui_click", args)
        self.advance(3)

    def select(self, name):
        self.call("select_items", {"scene_name": self.scene, "paths": [name]})
        self.advance(3)

    def values(self, domain, indices, node=BOX):
        return self.call("get_mesh_attribute_values", {
            "scene_name": self.scene, "node_name": node, "domain": domain, "indices": indices
        })["elements"]


def column_index(sheet, label):
    for i, column in enumerate(sheet["columns"]):
        if column["label"] == label:
            return i
    return -1


def cell_float(text):
    return None if text is None else float(text)


def close(a, b):
    return (a is not None) and (b is not None) and (abs(a - b) <= TOLERANCE)


def compare_attribute(e, sheet, domain, attribute, labels):
    """Every reported row's cells for `attribute` equal the MCP attribute values."""
    rows = sheet["rows"]
    elements = e.values(domain, [row["element"] for row in rows])
    by_index = {element["index"]: element for element in elements}
    columns = [column_index(sheet, label) for label in labels]
    if any(c < 0 for c in columns):
        return False, f"missing column among {labels}"
    for row in rows:
        entry = by_index[row["element"]]
        if attribute == "position":
            value = entry["position"]
            present = True
        else:
            attr = entry["attributes"].get(attribute, {"present": False})
            present = attr["present"]
            value = attr.get("value")
        for component, c in enumerate(columns):
            cell = row["cells"][c]
            if not present:
                if cell is not None:
                    return False, f"element {row['element']} {labels[component]}: expected absent, got {cell}"
                continue
            if not close(cell_float(cell), value[component]):
                return False, f"element {row['element']} {labels[component]}: cell {cell} vs {value[component]}"
    return True, f"{len(rows)} rows"


def phase_2(e):
    print("\n[phase 2] read-only window")
    e.select(BOX)
    e.call("set_window_visibility", {"title": WINDOW, "visible": True, "focus": True})
    e.advance(4)

    s = e.sheet()
    check_true("follows the selected mesh", s["node"] == BOX, f"node={s['node']}")
    check_true("target mode follow_selection", s["target_mode"] == "follow_selection")
    check_true("vertex tab is the default", s["domain"] == "Vertex")
    info = e.call("get_mesh_geometry_info", {"scene_name": e.scene, "node_name": BOX})
    counts = info["counts"]
    check_true("vertex row count = geometry vertex count", s["row_count"] == counts["vertices"], f"{s['row_count']} vs {counts['vertices']}")
    ranges = s["drawn_ranges"]
    drawn = sum(last - first for first, last in ranges)
    check_true("only the visible rows are drawn", (0 < drawn < s["row_count"]) and (ranges[0][0] == 0), f"drawn_ranges={ranges}")
    ok, detail = compare_attribute(e, s, "vertex", "position", ["position.x", "position.y", "position.z"])
    check_true("vertex position cells match the geometry", ok, detail)

    e.click("Corner")
    s = e.sheet()
    check_true("Corner tab selected by click", s["domain"] == "Corner", s["domain"])
    check_true("corner row count", s["row_count"] == counts["corners"], f"{s['row_count']} vs {counts['corners']}")
    ok, detail = compare_attribute(e, s, "corner", "corner_normal", ["normal.x", "normal.y", "normal.z"])
    check_true("corner normal cells match the geometry", ok, detail)
    ok, detail = compare_attribute(e, s, "corner", "corner_texcoord_0", ["texcoord_0.u", "texcoord_0.v"])
    check_true("corner texcoord cells match the geometry", ok, detail)
    elements = e.values("corner", [row["element"] for row in s["rows"]])
    vertex_column = column_index(s, "vertex")
    facet_column = column_index(s, "facet")
    ok = all(
        (int(row["cells"][vertex_column]) == element["vertex"]) and (int(row["cells"][facet_column]) == element["facet"])
        for row, element in zip(s["rows"], elements)
    )
    check_true("corner vertex / facet columns match", ok)

    e.click("Facet")
    s = e.sheet()
    check_true("Facet tab", (s["domain"] == "Facet") and (s["row_count"] == counts["facets"]))
    ok, detail = compare_attribute(e, s, "facet", "facet_normal", ["normal.x", "normal.y", "normal.z"])
    check_true("facet normal cells match the geometry", ok, detail)
    elements = e.values("facet", [row["element"] for row in s["rows"]])
    corners_column = column_index(s, "corners")
    first_column = column_index(s, "first_corner")
    ok = all(
        (int(row["cells"][corners_column]) == len(element["corners"])) and (int(row["cells"][first_column]) == element["corners"][0])
        for row, element in zip(s["rows"], elements)
    )
    check_true("facet corner count / first corner columns match", ok)

    e.click("Edge")
    s = e.sheet()
    check_true("Edge tab", (s["domain"] == "Edge") and (s["row_count"] == counts["edges"]), f"{s['row_count']} vs {counts['edges']}")
    elements = e.values("edge", [row["element"] for row in s["rows"]])
    v0 = column_index(s, "v0")
    v1 = column_index(s, "v1")
    ok = all(
        [int(row["cells"][v0]), int(row["cells"][v1])] == element["vertices"]
        for row, element in zip(s["rows"], elements)
    )
    check_true("edge v0 / v1 columns match", ok)

    # Sorting: header click cycles ascending -> descending -> element order.
    e.click("Corner")
    e.click("normal.y")
    s = e.sheet(first_row=0, row_count=256)
    c = column_index(s, "normal.y")
    values = [cell_float(row["cells"][c]) for row in s["rows"]]
    check_true("sort ascending by normal.y", (s["sort_column"] == c) and (values == sorted(values)), f"sort_column={s['sort_column']}")
    e.click("normal.y")
    s = e.sheet(first_row=0, row_count=256)
    values = [cell_float(row["cells"][c]) for row in s["rows"]]
    check_true("sort descending by normal.y", values == sorted(values, reverse=True))
    ok, detail = compare_attribute(e, s, "corner", "corner_normal", ["normal.x", "normal.y", "normal.z"])
    check_true("sorted rows still show their own element's values", ok, detail)
    e.click("normal.y")
    s = e.sheet(first_row=0, row_count=8)
    check_true("third click restores element order", (s["sort_column"] == -1) and ([row["element"] for row in s["rows"]] == list(range(8))))

    # Scrolling moves the drawn range; the drawn row count stays bounded.
    before = e.sheet()["drawn_ranges"]
    e.call("imgui_scroll", {"window": WINDOW, "label": "normal.x", "dy": -20})
    e.advance(4)
    after = e.sheet()["drawn_ranges"]
    drawn_before = sum(last - first for first, last in before)
    drawn_after = sum(last - first for first, last in after)
    check_true("scrolling draws a later row range", after[-1][0] > before[0][0], f"{before} -> {after}")
    check_true("drawn row count stays bounded while scrolling", drawn_after <= drawn_before + 2, f"{drawn_before} -> {drawn_after}")

    # A geometry swap (Catmull-Clark) and its undo.
    e.click("Vertex")
    vertex_count = e.sheet()["row_count"]
    e.call("catmull_clark", {"scene_name": e.scene, "node_name": BOX})
    e.advance(6)
    s = e.sheet()
    new_counts = e.call("get_mesh_geometry_info", {"scene_name": e.scene, "node_name": BOX})["counts"]
    check_true("Catmull-Clark: rows follow the new geometry", (s["row_count"] == new_counts["vertices"]) and (s["row_count"] != vertex_count), f"{vertex_count} -> {s['row_count']}")
    ok, detail = compare_attribute(e, s, "vertex", "position", ["position.x", "position.y", "position.z"])
    check_true("Catmull-Clark: cells match the new geometry", ok, detail)
    e.call("undo", {})
    e.advance(6)
    s = e.sheet()
    check_true("undo: rows back to the original geometry", s["row_count"] == vertex_count, f"{s['row_count']}")

    # Pin keeps the target; unpin follows the selection again.
    e.click("Pin")
    e.select("cube")
    s = e.sheet()
    check_true("pinned: target stays while the selection changes", (s["node"] == BOX) and (s["target_mode"] == "pinned"), f"node={s['node']}")
    e.click("Pin")
    s = e.sheet()
    check_true("unpinned: target follows the selection", (s["node"] == "cube") and (s["target_mode"] == "follow_selection"), f"node={s['node']}")

    # A hidden window draws nothing.
    e.call("set_window_visibility", {"title": WINDOW, "visible": False})
    e.advance(3)
    s = e.sheet()
    check_true("hidden window: nothing drawn", (not s["visible"]) and (s.get("drawn_ranges", []) == []))
    e.call("set_window_visibility", {"title": WINDOW, "visible": True, "focus": True})
    e.advance(3)

    # Removing the target mesh drops it.
    e.select(BOX)
    e.call("delete_nodes", {"scene_name": e.scene, "names": [BOX]})
    e.advance(4)
    s = e.sheet()
    check_true("deleted target is dropped", s["node"] != BOX, f"node={s['node']}")
    e.call("undo", {})
    e.advance(4)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", default=DEFAULT_EDITOR)
    args = parser.parse_args()

    process, port = launch_editor(args.editor)
    try:
        client = McpClient(port)
        scene = client.call("list_scenes")["scenes"][0]["name"]
        e = Editor(client, scene)
        e.call("create_shape", {"scene_name": scene, "shape": "box", "name": BOX})
        e.advance(4)
        phase_2(e)
    finally:
        process.kill()
    return report()


if __name__ == "__main__":
    sys.exit(main())
