#!/usr/bin/env python3
"""Smoke test for clearing editor references to content removed by an undo.

Drives the in-editor MCP server against a RUNNING editor (this script does
not launch one - start the editor first, or use scripts/run_mcp_tests.ps1
for the C++ suite).

Covers doc/import-undo-reference-clearing.md: undoing a glTF import, or
undoing "open glTF as a scene", must make every editor part drop the
cached shared_ptr it holds to the removed content - otherwise the window
keeps showing dead content and the asset can never be unloaded
(Asset_manager::unload_record reports it as an undeclared survivor).

Test asset: res/editor/assets/RiggedFigure/RiggedFigure.glb (tracked;
22 nodes, 1 skin, 1 animation with 13 channels).
"""

import json
import pathlib
import sys
import time
import urllib.request

PORT = 8080
GLTF = "res/editor/assets/RiggedFigure/RiggedFigure.glb"
LOG_PATH = pathlib.Path("logs/log.txt")
RESULTS = []


def rpc(method, params, timeout=180):
    body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": method, "params": params}).encode("utf-8")
    request = urllib.request.Request(
        f"http://127.0.0.1:{PORT}/mcp", data=body, headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def call_once(tool, args=None, timeout=180):
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


def is_busy_error(error):
    text = str(error)
    return ("Request timed out" in text) or ("Server busy" in text)


def call(tool, args=None, timeout=180, deadline_s=600):
    deadline = time.time() + deadline_s
    while True:
        try:
            return call_once(tool, args, timeout=timeout)
        except RuntimeError as error:
            if not is_busy_error(error) or (time.time() > deadline):
                raise
            time.sleep(5.0)


def call_expect_error(tool, args=None):
    try:
        call(tool, args)
        return None
    except RuntimeError as error:
        return str(error)


def check(section, name, condition, detail=""):
    RESULTS.append((section, name, bool(condition), detail))
    status = "PASS" if condition else "FAIL"
    print(f"[{status}] {section}: {name}" + (f" -- {detail}" if detail and not condition else ""))
    return bool(condition)


def refs():
    return call("get_editor_references")


def undo():
    return call("undo")


def redo():
    return call("redo")


def advance(frames=3):
    # The removal announcement is published once per frame, just before the
    # message bus pump, so give the editor a frame after every mutation.
    for _ in range(frames):
        call("advance_time", {"seconds": 0.016})


def name_of(reference):
    return reference["name"] if isinstance(reference, dict) else None


def uid_of(reference):
    return reference["uid"] if isinstance(reference, dict) else None


def scene_names():
    return [entry.get("name") for entry in call("list_scenes").get("scenes", [])]


def import_into_new_scene():
    """Create a scene, import the glTF, and report what the import ADDED.

    Diffing matters: a fresh scene already has a default camera and the
    standard materials, and undoing the import is not supposed to remove
    those - a test that picked one would assert the wrong thing.
    """
    before_scenes = set(scene_names())
    call("create_scene")
    advance(6)
    created = [name for name in scene_names() if name not in before_scenes]
    scene = created[-1] if created else (scene_names() or [None])[-1]

    nodes_before = {
        entry.get("id")
        for entry in call("get_scene_nodes", {"scene_name": scene}).get("nodes", [])
    }
    materials_before = {
        entry.get("name")
        for entry in call("get_scene_materials", {"scene_name": scene}).get("materials", [])
    }

    call("import_gltf", {"scene_name": scene, "path": GLTF})
    advance(10)

    nodes = [
        entry
        for entry in call("get_scene_nodes", {"scene_name": scene}).get("nodes", [])
        if entry.get("id") not in nodes_before and entry.get("name") not in (None, "")
    ]
    materials = [
        entry.get("name")
        for entry in call("get_scene_materials", {"scene_name": scene}).get("materials", [])
        if entry.get("name") not in materials_before
    ]
    return scene, nodes, materials


def first_animation_name(scene_name):
    animations = call("get_scene_animations", {"scene_name": scene_name}).get("animations", [])
    return animations[0]["name"] if animations else None


def drop_scene(scene_name):
    """Close a section's scene; a leftover scene keeps its own library alive."""
    if not scene_name:
        return
    try:
        call("close_scene", {"scene_name": scene_name})
        advance(4)
    except RuntimeError:
        pass


def properties_targets(reference_dump):
    return [name_of(entry.get("target")) for entry in reference_dump.get("properties", [])]


def find_tree_holding(item_name, item_uid):
    """The item tree that lists THIS item.

    Trees are labelled by window title ("Scene Hierarchy [1]"), which does not
    name the scene, so ask each tree in turn. The uid check matters: the
    imported root node is named after the glTF file, and the Asset Browser
    lists an Asset_file_gltf entry with exactly that name - a name-only match
    pins an item the undo is not supposed to remove.
    """
    for tree in refs().get("item_trees", []):
        label = tree.get("label")
        if not label:
            continue
        try:
            result = call("debug_set_item_tree_hover", {"tree": label, "item_name": item_name})
        except RuntimeError:
            continue
        if isinstance(result, dict) and (result.get("uid") == item_uid):
            return label
        call("debug_set_item_tree_hover", {"tree": label, "clear": True})
    return None


# ---------------------------------------------------------------- sections

def section_route_1_import():
    """The reported bug: import into an open scene, target an animation, undo."""
    section = "route 1 (import)"
    scene, imported_nodes, imported_materials = import_into_new_scene()

    animation = first_animation_name(scene)
    if not check(section, "imported glTF has an animation", animation is not None):
        return
    call("set_animation_target", {"animation": animation, "scene_name": scene})
    advance()

    before = refs()
    check(section, "animation window holds the animation",
          name_of(before["animation_window"]) == animation, json.dumps(before["animation_window"]))
    check(section, "animation player holds the animation",
          name_of(before["animation_player"]) == animation, json.dumps(before["animation_player"]))

    undo()
    advance()
    after = refs()
    check(section, "animation window cleared by undo",
          after["animation_window"] is None, json.dumps(after["animation_window"]))
    check(section, "animation player cleared by undo",
          after["animation_player"] is None, json.dumps(after["animation_player"]))
    return scene


def section_redo_is_not_resurrection(scene):
    """Redo brings the content back, but must not re-target the window."""
    section = "redo"
    if scene is None:
        return
    redo()
    advance(6)
    animations = call("get_scene_animations", {"scene_name": scene}).get("animations", [])
    check(section, "redo restores the imported animation", len(animations) > 0,
          f"{len(animations)} animations")
    after = refs()
    check(section, "window target stays cleared after redo",
          after["animation_window"] is None, json.dumps(after["animation_window"]))


def section_route_2_open_scene():
    """Open the glTF as its own scene, target an animation, undo."""
    section = "route 2 (open scene)"
    call("open_scene", {"path": GLTF})
    advance(10)
    scenes = call("list_scenes").get("scenes", [])
    scene = None
    for entry in scenes:
        if "RiggedFigure" in entry.get("name", ""):
            scene = entry["name"]
    if not check(section, "glTF opened as a scene", scene is not None, json.dumps(scenes)):
        return
    animation = first_animation_name(scene)
    if not check(section, "opened scene has an animation", animation is not None):
        return
    call("set_animation_target", {"animation": animation, "scene_name": scene})
    advance()
    before = refs()
    check(section, "animation window holds the animation",
          name_of(before["animation_window"]) == animation)

    undo()
    advance(4)
    after = refs()
    check(section, "animation window cleared by undo of the scene open",
          after["animation_window"] is None, json.dumps(after["animation_window"]))
    check(section, "animation player cleared by undo of the scene open",
          after["animation_player"] is None, json.dumps(after["animation_player"]))


def section_properties_pin():
    """A pinned Properties window (#252) must drop its target on undo.

    Properties::m_target is the PIN, not a selection follow: an unpinned
    window shows the selection and holds nothing across frames, so the pin is
    the reference that can outlive the content.
    """
    section = "properties pin"
    scene, imported_nodes, imported_materials = import_into_new_scene()
    if not check(section, "import added a material", len(imported_materials) > 0):
        return
    material = imported_materials[0]

    call("open_properties_window", {"scene_name": scene, "material": material})
    advance(4)
    before = refs()
    if not check(section, "properties window pinned to the imported material",
                 material in properties_targets(before), json.dumps(before.get("properties"))):
        return  # do not let the assertion below pass vacuously

    undo()
    advance(4)
    after = refs()
    check(section, "pinned target cleared by undo",
          material not in properties_targets(after), json.dumps(after.get("properties")))


def section_announcement_content():
    """last_announced_uids pins the producer, independent of any subscriber."""
    section = "announcement"
    scene, imported_nodes, imported_materials = import_into_new_scene()
    animation = first_animation_name(scene)
    before = refs()
    count_before = before.get("items_removed_announcement_count", 0)

    undo()
    advance(4)
    after = refs()
    count_after = after.get("items_removed_announcement_count", 0)
    check(section, "an announcement was published", count_after > count_before,
          f"{count_before} -> {count_after}")
    check(section, "announcement is not empty", len(after.get("last_announced_uids", [])) > 0,
          json.dumps(after.get("last_announced_uids", []))[:200])
    return animation


def section_false_positive_move():
    """A library folder move is a detach + attach: it must not read as removal."""
    section = "false positive (move)"
    scene, imported_nodes, imported_materials = import_into_new_scene()
    if not check(section, "import added a material", len(imported_materials) > 0):
        return
    material = imported_materials[0]
    call("set_tool_asset", {"tool": "material_paint", "scene_name": scene, "name": material})
    advance()
    before = refs()
    if not check(section, "material paint tool holds the material",
                 name_of(before["material_paint_tool"]) == material,
                 json.dumps(before["material_paint_tool"])):
        return
    count_before = before.get("items_removed_announcement_count", 0)

    call("move_library_item", {"scene_name": scene, "item_name": material, "folder_name": "Moved"})
    advance(4)
    after = refs()
    check(section, "material paint tool keeps the moved material",
          name_of(after["material_paint_tool"]) == material, json.dumps(after["material_paint_tool"]))
    check(section, "a move announces no removal",
          after.get("items_removed_announcement_count", 0) == count_before,
          f"{count_before} -> {after.get('items_removed_announcement_count')}")


def section_tool_references():
    """Every remaining subscriber drops its reference."""
    section = "tool references"
    scene, imported_nodes, imported_materials = import_into_new_scene()
    if not check(section, "import added a material", len(imported_materials) > 0):
        return
    material = imported_materials[0]
    material_id = next(
        (entry["id"] for entry in call("get_scene_materials", {"scene_name": scene}).get("materials", [])
         if entry.get("name") == material),
        None
    )
    if not check(section, "imported material has an id", material_id is not None):
        return

    call("set_tool_asset", {"tool": "material_paint", "scene_name": scene, "name": material})
    # Operations, Material_preview and Brdf_slice resolve the last-selected
    # material in their render path, so a selection is what arms them. Without
    # this the assertions below would pass on an empty implementation.
    call("set_window_visibility", {"title": "Operations", "visible": True})
    advance()
    call("select_items", {"scene_name": scene, "ids": [material_id]})
    advance(8)

    before = refs()
    armed = {
        "material_paint_tool":           name_of(before["material_paint_tool"]) == material,
        "material_preview":              name_of(before["material_preview"]) == material,
        "brdf_slice":                    name_of(before["brdf_slice"]) == material,
        "operations_make_mesh_material": name_of(before["operations_make_mesh_material"]) == material,
    }
    for key, ok in armed.items():
        check(section, f"{key} armed", ok, json.dumps(before.get(key)))

    undo()
    advance(4)
    after = refs()
    for key in armed:
        if not armed[key]:
            continue  # not armed, so "cleared" would be vacuous
        check(section, f"{key} cleared by undo", after[key] is None, json.dumps(after[key]))


def section_tree_window_pin():
    """The hover / popup pin only persists on a tree that stops rendering."""
    section = "tree window pin"
    scene, imported_nodes, imported_materials = import_into_new_scene()
    if not check(section, "import added nodes", len(imported_nodes) > 0):
        return
    node = imported_nodes[0]["name"]

    label = find_tree_holding(node, imported_nodes[0]["id"])
    if not check(section, "an item tree lists the imported node", label is not None,
                 json.dumps(refs().get("item_trees"))):
        return

    # Hide FIRST: a rendering tree resets the hover every frame
    # (item_tree_window.cpp), so hovering before hiding would let the next
    # rendered frame wipe it and the assertion would pass either way.
    call("set_window_visibility", {"title": label, "visible": False})
    advance()
    error = call_expect_error("debug_set_item_tree_hover", {"tree": label, "item_name": node})
    if not check(section, "hover pin can be set on the hidden tree", error is None, str(error)):
        return
    before = refs()
    pinned = any(
        (tree.get("label") == label) and (name_of(tree.get("hovered_item")) == node)
        for tree in before.get("item_trees", [])
    )
    check(section, "tree holds the hover pin", pinned, json.dumps(before.get("item_trees")))

    undo()
    advance(4)
    after = refs()
    cleared = all(
        (tree.get("label") != label) or (tree.get("hovered_item") is None and tree.get("popup_item") is None)
        for tree in after.get("item_trees", [])
    )
    check(section, "hover / popup pins cleared by undo", cleared, json.dumps(after.get("item_trees")))
    call("set_window_visibility", {"title": label, "visible": True})


def section_selection_pruning():
    """Selection prunes removed items, in ONE batched change."""
    section = "selection"
    scene, imported_nodes, imported_materials = import_into_new_scene()
    picked = imported_nodes[:3]
    names = [entry["name"] for entry in picked]
    if not check(section, "import added selectable nodes", len(picked) > 0):
        return
    call("select_items", {"scene_name": scene, "ids": [e["id"] for e in picked]})
    advance()
    before = refs()
    check(section, "nodes are selected", len(before.get("selection", [])) > 0)
    changes_before = before.get("selection_change_count", 0)

    undo()
    advance(4)
    after = refs()
    selected_uids = [uid_of(entry) for entry in after.get("selection", [])]
    still = [entry["name"] for entry in picked if entry["id"] in selected_uids]
    check(section, "removed items pruned from the selection", not still, json.dumps(still))
    changes_after = after.get("selection_change_count", 0)
    # The undo restores the pre-import selection snapshot AND the prune may
    # run; what must never happen is one dispatch per removed item.
    check(section, "pruning is batched, not one dispatch per item",
          (changes_after - changes_before) <= 3,
          f"{changes_before} -> {changes_after} for {len(names)} items")


def section_truly_released():
    """undo + clear history must leave no window / tool / history holder.

    The user's criterion is "the assets are truly unloaded". Undo alone can
    never achieve that - the undo entry deliberately retains everything for
    redo - so this runs the full sequence: undo, clear the history, then ask
    the asset manager to unload the container.

    A refusal can still be legitimate: a container's assets are declared-used
    by other scenes' library entries (Bone_visualization's shared 'bone'
    material is referenced by every scene's library), and that is designed
    behaviour, not a stale reference. What must never appear is a window, a
    tool, or the undo history - those are what this change fixes.
    """
    section = "truly released"
    scene, imported_nodes, imported_materials = import_into_new_scene()
    animation = first_animation_name(scene)
    material = imported_materials[0] if imported_materials else None
    if material is not None:
        call("set_tool_asset", {"tool": "material_paint", "scene_name": scene, "name": material})
    if animation is not None:
        call("set_animation_target", {"animation": animation, "scene_name": scene})
    advance()

    undo()
    advance(4)
    call("clear_undo_history")
    advance(2)

    stale = refs()
    check(section, "no window still holds the removed animation",
          stale["animation_window"] is None and stale["animation_player"] is None,
          json.dumps({"window": stale["animation_window"], "player": stale["animation_player"]}))
    check(section, "no tool still holds a removed material",
          stale["material_paint_tool"] is None, json.dumps(stale["material_paint_tool"]))
    check(section, "no tool still holds a removed brush",
          stale["brush_tool"].get("active_brush") is None,
          json.dumps(stale["brush_tool"]))

    try:
        result = call("unload_asset", {"scope": "file", "type": "animation", "path": GLTF})
    except RuntimeError as error:
        users = str(error)
        offenders = [
            label for label in
            ("undo stack", "brush tool", "material paint tool", "animation window", "properties")
            if label in users
        ]
        check(section, "unload is not refused by a window / tool / the history",
              not offenders, f"{offenders} in: {users[:400]}")
        return

    if isinstance(result, dict):
        check(section, "unload succeeded", result.get("ok") is True, json.dumps(result)[:400])
        check(section, "no undeclared survivors", result.get("undeclared_survivors", -1) == 0,
              json.dumps(result)[:400])
    else:
        check(section, "unload returned a result", False, str(result)[:200])


def section_no_close_regression():
    """Closing a scene must not regress the scene-close leak watchdog."""
    section = "scene close"
    scene, imported_nodes, imported_materials = import_into_new_scene()
    baseline = LOG_PATH.stat().st_size if LOG_PATH.exists() else 0
    call("close_scene", {"scene_name": scene})
    # The watchdog is frame-delayed; give it more than its check window.
    advance(90)
    tail = ""
    if LOG_PATH.exists():
        with LOG_PATH.open("rb") as handle:
            handle.seek(baseline)
            tail = handle.read().decode("utf-8", errors="replace")
    check(section, "no scene-close leak reported", "scene-close leak" not in tail,
          tail[-500:] if "scene-close leak" in tail else "")


def main():
    try:
        call("list_scenes")
    except Exception as error:  # noqa: BLE001 - the editor is simply not up
        print(f"Cannot reach the editor MCP server on port {PORT}: {error}")
        print("Start the editor first (it serves MCP on 127.0.0.1:8080).")
        return 2

    def run(section_name, function, *args):
        try:
            return function(*args)
        except Exception as error:  # noqa: BLE001 - report, do not abort the run
            check(section_name, "section completed", False, f"{type(error).__name__}: {error}"[:400])
            return None

    scene = run("route 1 (import)", section_route_1_import)
    run("redo", section_redo_is_not_resurrection, scene)
    run("route 2 (open scene)", section_route_2_open_scene)
    run("properties pin", section_properties_pin)
    run("announcement", section_announcement_content)
    run("false positive (move)", section_false_positive_move)
    run("tool references", section_tool_references)
    run("tree window pin", section_tree_window_pin)
    run("selection", section_selection_pruning)
    run("truly released", section_truly_released)
    run("scene close", section_no_close_regression)

    failed = [entry for entry in RESULTS if not entry[2]]
    print()
    print(f"{len(RESULTS) - len(failed)}/{len(RESULTS)} checks passed")
    for section, name, _, detail in failed:
        print(f"  FAIL {section}: {name} -- {detail}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
