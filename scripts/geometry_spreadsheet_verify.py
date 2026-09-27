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
import base64
import os
import re
import struct
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

    def click_row(self, element, modifiers=None):
        """Click a row near its left end: a row spans every column, so its
        center lies past the window edge when the table scrolls horizontally."""
        item = self.call("get_imgui_item_rect", {"window": WINDOW, "label": f"row {element}"})
        rect = item.get("rect", item)
        x = rect["x"] + 8.0
        y = item["center_y"] if "center_y" in item else rect["y"] + (rect["height"] * 0.5)
        self.call("mouse_click", {"x": x, "y": y, "modifiers": modifiers or []})
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


def close_all(a, b):
    return (a is not None) and (b is not None) and (len(a) == len(b)) and all(abs(x - y) <= TOLERANCE for x, y in zip(a, b))


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


def selected_components(e, kind, node=BOX):
    """The mesh component selection's `kind` list for the node's live entry."""
    for entry in e.call("get_mesh_component_selection")["entries"]:
        if (entry.get("node_name") == node) and entry["live"]:
            return entry[kind]
    return []


def rows_elements(sheet):
    return [row["element"] for row in sheet["rows"]]


def phase_3(e):
    print("\n[phase 3] selection sync")
    e.select(BOX)
    e.click("Vertex")
    e.call("select_mesh_components", {"scene_name": e.scene, "node_name": BOX, "mode": "vertex", "vertices": [1, 5, 7]})
    e.advance(3)
    s = e.sheet(first_row=0, row_count=10)
    selected = [row["element"] for row in s["rows"] if row["selected"]]
    check_true("component selection shows as selected rows", selected == [1, 5, 7], f"{selected}")

    e.click("Selected Only")
    s = e.sheet(first_row=0, row_count=64)
    check_true("Selected Only: rows are the selected vertices", (s["row_filter"] == "selected") and (rows_elements(s) == [1, 5, 7]), f"{rows_elements(s)}")
    e.call("select_mesh_components", {"scene_name": e.scene, "node_name": BOX, "vertices": [2], "extend": True})
    e.advance(3)
    s = e.sheet(first_row=0, row_count=64)
    check_true("Selected Only follows a selection change (message driven)", rows_elements(s) == [1, 2, 5, 7], f"{rows_elements(s)}")
    e.call("grow_mesh_selection", {})
    e.advance(3)
    s = e.sheet(first_row=0, row_count=256)
    grown = selected_components(e, "vertices")
    check_true("Selected Only follows grow", rows_elements(s) == sorted(grown) and (len(grown) > 4), f"{len(rows_elements(s))} rows vs {len(grown)} selected")

    e.click("Facet")
    e.call("select_mesh_components", {"scene_name": e.scene, "node_name": BOX, "mode": "face", "facets": [3, 2]})
    e.advance(3)
    s = e.sheet(first_row=0, row_count=64)
    check_true("Selected Only, Facet tab", (s["domain"] == "Facet") and (rows_elements(s) == [2, 3]), f"{rows_elements(s)}")

    e.click("Edge")
    edge_values = e.values("edge", [10])
    v0, v1 = edge_values[0]["vertices"]
    e.call("select_mesh_components", {"scene_name": e.scene, "node_name": BOX, "mode": "edge", "edges": [[v1, v0]]})
    e.advance(3)
    s = e.sheet(first_row=0, row_count=64)
    check_true("Selected Only, Edge tab maps the vertex pair to its edge", rows_elements(s) == [10], f"{rows_elements(s)}")

    # Sorting applies within the filtered rows.
    e.click("Vertex")
    e.call("select_mesh_components", {"scene_name": e.scene, "node_name": BOX, "mode": "vertex", "vertices": [3, 8, 20, 40]})
    e.advance(3)
    e.click("position.x")
    e.click("position.x")
    s = e.sheet(first_row=0, row_count=64)
    c = column_index(s, "position.x")
    values = [cell_float(row["cells"][c]) for row in s["rows"]]
    check_true("Selected Only rows sort descending", (sorted(rows_elements(s)) == [3, 8, 20, 40]) and (values == sorted(values, reverse=True)), f"{rows_elements(s)} {values}")
    e.click("position.x")
    e.click("Selected Only")

    # Row clicks edit the component selection.
    e.call("clear_mesh_component_selection", {})
    e.advance(2)
    e.click_row(10)
    check_true("click selects the vertex", selected_components(e, "vertices") == [10])
    mode = e.call("get_mesh_component_selection")["mode"]
    check_true("click switches to vertex component mode", mode == "vertex", mode)
    e.click_row(12, modifiers=["ctrl"])
    check_true("Ctrl+click adds", selected_components(e, "vertices") == [10, 12], f"{selected_components(e, 'vertices')}")
    e.click_row(14, modifiers=["shift"])
    check_true("Shift+click adds the range", selected_components(e, "vertices") == [10, 12, 13, 14], f"{selected_components(e, 'vertices')}")
    e.click_row(12, modifiers=["ctrl"])
    check_true("Ctrl+click toggles off", selected_components(e, "vertices") == [10, 13, 14], f"{selected_components(e, 'vertices')}")
    e.click_row(3)
    check_true("plain click replaces", selected_components(e, "vertices") == [3], f"{selected_components(e, 'vertices')}")

    e.click("Facet")
    e.click_row(5)
    check_true("Facet row click selects the facet", (selected_components(e, "facets") == [5]) and (e.call("get_mesh_component_selection")["mode"] == "face"))
    e.click("Edge")
    e.click_row(7)
    edge = e.values("edge", [7])[0]["vertices"]
    check_true("Edge row click selects the edge", selected_components(e, "edges") == [sorted(edge)], f"{selected_components(e, 'edges')} vs {edge}")

    e.click("Corner")
    e.call("imgui_scroll", {"window": WINDOW, "label": "normal.x", "dy": 100})  # phase 2 scrolled this table down
    e.advance(4)
    e.click_row(4)
    e.click_row(6, modifiers=["ctrl"])
    s = e.sheet(first_row=0, row_count=10)
    selected = [row["element"] for row in s["rows"] if row["selected"]]
    check_true("Corner tab keeps its own row selection", selected == [4, 6], f"{selected}")
    e.click("Selected Only")
    s = e.sheet(first_row=0, row_count=64)
    check_true("Selected Only, Corner tab", rows_elements(s) == [4, 6], f"{rows_elements(s)}")
    e.click("Selected Only")
    e.call("clear_mesh_component_selection", {})
    e.call("set_mesh_component_mode", {"mode": "object"})
    e.click("Vertex")
    e.advance(2)


def gpu_colors(e, node=BOX):
    """The color_0 vertex attribute of every GPU vertex of the node's base build."""
    info = e.call("get_mesh_buffer_info", {"scene_name": e.scene, "node_name": node, "variant": "original"})
    for stream in info["vertex_streams"]:
        for attribute in stream["attributes"]:
            if (attribute["usage"] == "color") and (attribute["usage_index"] == 0):
                data = e.call("get_mesh_buffer_data", {
                    "scene_name": e.scene, "node_name": node, "variant": "original",
                    "buffer": "vertex", "stream": stream["stream"], "element_count": stream["count"]
                })
                raw = base64.b64decode(data["data"])
                stride = stream["stride"]
                offset = attribute["offset"]
                return [struct.unpack_from("<4f", raw, i * stride + offset) for i in range(stream["count"])]
    return []


def attribute_value(e, domain, attribute, element, node=BOX):
    entry = e.values(domain, [element], node)[0]
    if attribute == "position":
        return entry["position"]
    attr = entry["attributes"].get(attribute, {"present": False})
    return attr.get("value") if attr["present"] else None


def cell_point(e, element, label):
    """Window point of the cell (row `element`, column `label`), both drawn."""
    row = e.call("get_imgui_item_rect", {"window": WINDOW, "label": f"row {element}"})
    header = e.call("get_imgui_item_rect", {"window": WINDOW, "label": label})
    return header["center_x"], row["center_y"]


def undo_top(e):
    return e.call("get_undo_redo_stack")["undo"][-1]["description"]


def phase_4(e):
    print("\n[phase 4] editing")
    e.select(BOX)
    e.click("Corner")
    e.call("imgui_scroll", {"window": WINDOW, "label": "normal.x", "dy": 100})
    e.advance(3)
    red = [1.0, 0.0, 0.0, 1.0]
    before_gpu = gpu_colors(e)
    check_true("GPU colors before: no red vertex", not any(close_all(c, red) for c in before_gpu), f"{len(before_gpu)} vertices")

    # MCP edit of a corner color: geometry, window layout and GPU buffer.
    result = e.call("set_mesh_attribute_values", {"scene_name": e.scene, "node_name": BOX, "attribute": "corner_color_0", "elements": [0, 1, 2, 3], "value": red})
    e.advance(4)
    check_true("set_mesh_attribute_values queues one operation", result.get("queued") is True, result.get("operation", ""))
    check_true("corner color written to the geometry", close_all(attribute_value(e, "corner", "corner_color_0", 2) or [], red))
    s = e.sheet(first_row=0, row_count=4)
    c = column_index(s, "color_0.r")
    check_true("new attribute column appears in the window", (c >= 0) and all(row["cells"][c] == "1.0000" for row in s["rows"]), f"column {c}")
    red_count = sum(1 for color in gpu_colors(e) if close_all(color, red))
    check_true("GPU vertex buffer carries the new color", red_count >= 4, f"{red_count} red vertices")
    e.call("undo", {})
    e.advance(4)
    check_true("undo removes the color", attribute_value(e, "corner", "corner_color_0", 2) is None)
    check_true("undo: column gone again", column_index(e.sheet(first_row=0, row_count=1), "color_0.r") < 0)
    check_true("undo: GPU buffer back", not any(close_all(color, red) for color in gpu_colors(e)))
    e.call("redo", {})
    e.advance(4)
    check_true("redo restores the color", close_all(attribute_value(e, "corner", "corner_color_0", 2) or [], red))
    e.call("undo", {})
    e.advance(4)

    # Position edit: Move_mesh_vertices_operation refreshes the facet normals.
    e.click("Vertex")
    e.call("imgui_scroll", {"window": WINDOW, "label": "position.x", "dy": 100})
    e.advance(3)
    p0 = attribute_value(e, "vertex", "position", 0)
    facets_of_0 = [element["facet"] for element in e.values("corner", list(range(384))) if element["vertex"] == 0]
    normal_before = attribute_value(e, "facet", "facet_normal", facets_of_0[0])
    e.call("set_mesh_attribute_values", {"scene_name": e.scene, "node_name": BOX, "attribute": "position", "elements": [0], "value": [p0[0] + 0.25, p0[1], p0[2]]})
    e.advance(4)
    p0_after = attribute_value(e, "vertex", "position", 0)
    normal_after = attribute_value(e, "facet", "facet_normal", facets_of_0[0])
    check_true("position edit moves the vertex", close(p0_after[0], p0[0] + 0.25), f"{p0} -> {p0_after}")
    check_true("position edit refreshes the facet normal", not close_all(normal_before, normal_after), f"{normal_before} -> {normal_after}")
    e.call("undo", {})
    e.advance(4)
    check_true("undo restores the position", close_all(attribute_value(e, "vertex", "position", 0), p0))

    # UI edit: double-click the cell, type, Enter.
    x, y = cell_point(e, 5, "position.y")
    e.call("mouse_click", {"x": x, "y": y, "double": True})
    e.advance(3)
    e.call("key_press", {"key": "a", "modifiers": ["ctrl"]})
    e.call("type_text", {"text": "0.125"})
    e.advance(3)
    e.call("key_press", {"key": "enter"})
    e.advance(5)
    p5 = attribute_value(e, "vertex", "position", 5)
    check_true("double-click + type + Enter edits the cell", close(p5[1], 0.125), f"{p5}")
    s = e.sheet(first_row=5, row_count=1)
    shown = s["rows"][0]["cells"][column_index(s, "position.y")]
    check_true("the window shows the edited value", shown == "0.1250", shown)
    check_true("the UI edit is one undoable operation", "Move 1 mesh vertices" in undo_top(e), undo_top(e))
    e.call("undo", {})
    e.advance(4)

    # Escape cancels an edit.
    x, y = cell_point(e, 6, "position.x")
    before6 = attribute_value(e, "vertex", "position", 6)
    depth = len(e.call("get_undo_redo_stack")["undo"])
    e.call("mouse_click", {"x": x, "y": y, "double": True})
    e.advance(3)
    e.call("key_press", {"key": "a", "modifiers": ["ctrl"]})
    e.call("type_text", {"text": "9"})
    e.call("key_press", {"key": "escape"})
    e.advance(4)
    unchanged = close_all(attribute_value(e, "vertex", "position", 6), before6) and (len(e.call("get_undo_redo_stack")["undo"]) == depth)
    check_true("Escape cancels the edit", unchanged)

    # Fill down: select rows, right-click a source cell, Set Selected Rows.
    e.click_row(1)
    e.click_row(3, modifiers=["shift"])
    source_y = attribute_value(e, "vertex", "position", 20)[1]
    before_ys = [attribute_value(e, "vertex", "position", v)[1] for v in (1, 2, 3)]
    check_true("fill down source differs from the targets", not any(close(v, source_y) for v in before_ys), f"{before_ys} vs {source_y}")
    x, y = cell_point(e, 20, "position.y")
    e.call("mouse_click", {"x": x, "y": y, "button": "right"})
    e.advance(3)
    e.call("imgui_click", {"label": "Set Selected Rows To This Value"})
    e.advance(4)
    ys = [attribute_value(e, "vertex", "position", v)[1] for v in (1, 2, 3)]
    check_true("fill down sets the column on every selected row", all(close(v, source_y) for v in ys), f"{ys} vs {source_y}")
    check_true("fill down is one operation", "Move 3 mesh vertices" in undo_top(e), undo_top(e))
    e.call("undo", {})
    e.call("clear_mesh_component_selection", {})
    e.call("set_mesh_component_mode", {"mode": "object"})
    e.advance(4)

    # Edge sharpness: in place, no rebuild; the Edge tab gains the column.
    e.click("Edge")
    e.call("set_mesh_attribute_values", {"scene_name": e.scene, "node_name": BOX, "attribute": "edge_sharpness", "elements": [3], "value": [2.0]})
    e.advance(4)
    s = e.sheet(first_row=3, row_count=1)
    c = column_index(s, "sharpness")
    check_true("edge sharpness edit shows in the Edge tab", (c >= 0) and (s["rows"][0]["cells"][c] == "2.0000"), f"column {c}")
    e.call("undo", {})
    e.advance(4)

    # Derived attributes are refused.
    try:
        e.call("set_mesh_attribute_values", {"scene_name": e.scene, "node_name": BOX, "attribute": "facet_centroid", "elements": [0], "value": [0, 0, 0]})
        refused = False
    except RuntimeError:
        refused = True
    check_true("derived attribute edit is refused", refused)
    e.click("Vertex")


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
        phase_3(e)
        phase_4(e)
    finally:
        process.kill()
    return report()


if __name__ == "__main__":
    sys.exit(main())
