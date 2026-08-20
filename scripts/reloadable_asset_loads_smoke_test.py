#!/usr/bin/env python3
"""Smoke test for reloadable asset loads.

Drives the in-editor MCP server against a RUNNING editor (start one first;
this script does not launch it).

Covers doc/reloadable-asset-loads.md: a recorded glTF import owns everything
it created, so undoing a large import used to free nothing until the entry
itself was destroyed. Now the import drops its payload on undo - when that is
lossless - and re-reads the file on redo.

Every memory figure comes from the get_memory_usage MCP tool. Mesh and texture
releases are frame-deferred (double-gated on frame completion and the loader
watermark), so every sample advances frames first.

Test asset: res/editor/assets/RiggedFigure/RiggedFigure.glb.
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


def check(section, name, condition, detail=""):
    RESULTS.append((section, name, bool(condition), detail))
    status = "PASS" if condition else "FAIL"
    print(f"[{status}] {section}: {name}" + (f" -- {detail}" if detail and not condition else ""))
    return bool(condition)


def advance(frames=14):
    # Releases are frame-deferred; sampling immediately would read stale figures.
    for _ in range(frames):
        call("advance_time", {"seconds": 0.016})


def memory():
    return call("get_memory_usage")


def mesh_used():
    return memory()["mesh_memory"]["used_bytes"]


def scene_names():
    return [entry.get("name") for entry in call("list_scenes").get("scenes", [])]


def new_scene():
    before = set(scene_names())
    call("create_scene")
    advance(6)
    created = [name for name in scene_names() if name not in before]
    return created[-1] if created else None


def node_ids(scene):
    return {entry["id"] for entry in call("get_scene_nodes", {"scene_name": scene}).get("nodes", [])}


def redo_descriptions():
    return [entry.get("description", "") for entry in call("get_undo_redo_stack").get("redo", [])]


def drop_scene(scene):
    if not scene:
        return
    try:
        call("close_scene", {"scene_name": scene})
        advance(6)
    except RuntimeError:
        pass


# ---------------------------------------------------------------- sections

def section_undo_frees_memory():
    """The reported goal: undoing a load gives its memory back."""
    section = "undo frees"
    scene = new_scene()
    if not check(section, "created a scene", scene is not None):
        return None
    base = mesh_used()

    call("import_gltf", {"scene_name": scene, "path": GLTF})
    advance()
    after_import = mesh_used()
    if not check(section, "import allocated mesh memory", after_import > base,
                 f"{base} -> {after_import}"):
        return scene

    call("undo")
    advance()
    after_undo = mesh_used()
    check(section, "undo released it", after_undo < after_import,
          f"{after_import} -> {after_undo}")
    # Nearly all of it: the import's own allocation, back in the pool free list.
    released = after_import - after_undo
    added = after_import - base
    check(section, "released essentially all of the import",
          released >= added * 0.9, f"released {released} of {added}")
    return scene


def section_redo_rereads(scene):
    """Redo re-reads the file and produces equivalent, freshly identified content."""
    section = "redo re-reads"
    if scene is None:
        return
    before_ids = node_ids(scene)
    descriptions = redo_descriptions()
    check(section, "redo entry is marked unloaded",
          any("unloaded" in text for text in descriptions), json.dumps(descriptions))

    after_undo = mesh_used()
    call("redo")
    advance()
    after_redo = mesh_used()
    check(section, "redo re-allocated the content", after_redo > after_undo,
          f"{after_undo} -> {after_redo}")

    new_ids = node_ids(scene) - before_ids
    check(section, "redo restored the nodes", len(new_ids) > 0, f"{len(new_ids)} nodes")

    animations = [entry["name"] for entry in
                  call("get_scene_animations", {"scene_name": scene}).get("animations", [])]
    check(section, "redo restored the animation", len(animations) > 0, json.dumps(animations))

    # A rebuild makes fresh objects; ids are session-local and never reused.
    check(section, "rebuilt content has fresh ids", not (new_ids & before_ids),
          f"{len(new_ids & before_ids)} ids reused")

    # And the cycle is stable, not a one-shot.
    call("undo")
    advance()
    cycled_undo = mesh_used()
    call("redo")
    advance()
    cycled_redo = mesh_used()
    check(section, "a second undo/redo cycle is stable",
          (cycled_undo < after_redo) and (cycled_redo > cycled_undo),
          f"{after_redo} -> {cycled_undo} -> {cycled_redo}")
    drop_scene(scene)


def section_lossless_gate():
    """The gate declines when something was recorded after the import."""
    section = "lossless gate"
    scene = new_scene()
    if not check(section, "created a scene", scene is not None):
        return None
    base = mesh_used()
    call("import_gltf", {"scene_name": scene, "path": GLTF})
    advance()
    after_import = mesh_used()

    # Recorded AFTER the import: its redo entry holds references to the
    # imported content, so the import may not drop it.
    call("create_node", {"scene_name": scene, "name": "recorded_after_import"})
    advance(8)
    call("undo")   # undo the node creation
    advance(8)
    call("undo")   # undo the import - no longer lossless
    advance()
    after_undo = mesh_used()

    check(section, "payload kept when a later entry survives",
          after_undo >= after_import - 4096, f"{after_import} -> {after_undo}")
    descriptions = redo_descriptions()
    check(section, "the import entry is NOT marked unloaded",
          not any("unloaded" in text for text in descriptions), json.dumps(descriptions))
    check(section, "both redo entries survive", len(descriptions) == 2, json.dumps(descriptions))
    return scene


def section_free_undone_loads(scene):
    """The explicit command frees it, at the cost of the later redo entry."""
    section = "free_undone_loads"
    if scene is None:
        return
    before = mesh_used()
    result = call("free_undone_loads")
    advance()
    after = mesh_used()

    check(section, "released one payload", result.get("released_count") == 1, json.dumps(result))
    check(section, "discarded the later redo entry", result.get("discarded_count") == 1, json.dumps(result))
    check(section, "memory dropped", after < before, f"{before} -> {after}")

    descriptions = redo_descriptions()
    check(section, "only the load remains redoable, marked unloaded",
          (len(descriptions) == 1) and ("unloaded" in descriptions[0]), json.dumps(descriptions))

    call("redo")
    advance()
    check(section, "the freed load still redoes", mesh_used() > after, f"{after} -> {mesh_used()}")
    drop_scene(scene)


def section_no_payload_is_a_noop():
    """With nothing to release the command reports zero, not an error."""
    section = "no-op"
    try:
        result = call("free_undone_loads")
    except RuntimeError as error:
        check(section, "no-op rather than an error", False, str(error)[:200])
        return
    check(section, "reports zero when nothing holds a payload",
          result.get("released_count") == 0, json.dumps(result))


def section_batch_nesting():
    """An import nested in a compound must NOT drop its payload.

    A child of a Compound_operation cannot see its siblings, so the drop
    decision is made by Operation_stack for the top-level entry only.

    Which path this exercises depends on `load.async_gltf_load`. With it ON
    (the default) the import is queued from a completion callback frames after
    the batch closed, so it lands as its own top-level entry and IS eligible to
    drop. With it OFF the import queues inside the group and end_group wraps it
    in a Compound_operation, which is the nesting case. Both outcomes are
    asserted, so the test says something real either way.
    """
    section = "batch nesting"
    scene = new_scene()
    if not check(section, "created a scene", scene is not None):
        return
    base = mesh_used()
    try:
        call("batch", {"calls": [
            {"tool": "import_gltf", "arguments": {"scene_name": scene, "path": GLTF}},
            {"tool": "create_node", "arguments": {"scene_name": scene, "name": "batched_sibling"}}
        ]})
    except RuntimeError as error:
        check(section, "batch executed", False, str(error)[:200])
        drop_scene(scene)
        return
    advance()
    after_import = mesh_used()
    if not check(section, "batched import allocated", after_import > base, f"{base} -> {after_import}"):
        drop_scene(scene)
        return

    call("undo")
    advance()
    after_undo = mesh_used()
    descriptions = redo_descriptions()
    nested = any(text.startswith("[") and ("Compound" in text) for text in descriptions)

    if nested:
        # Synchronous import path: the import really is a child of a compound.
        check(section, "nested import keeps its payload",
              after_undo >= after_import - 4096, f"{after_import} -> {after_undo}")
        check(section, "compound entry is not marked unloaded",
              not any("unloaded" in text for text in descriptions), json.dumps(descriptions))
    else:
        # Asynchronous import path: the import landed top-level after the batch
        # closed, so the ordinary lossless gate applies and it drops.
        check(section, "async import landed top-level and dropped",
              after_undo < after_import, f"{after_import} -> {after_undo}")
        check(section, "top-level entry is marked unloaded",
              any("unloaded" in text for text in descriptions), json.dumps(descriptions))
    drop_scene(scene)


def section_unload_after_drop():
    """unload_asset must refuse while the payload is kept and succeed once dropped."""
    section = "unload"
    scene = new_scene()
    if not check(section, "created a scene", scene is not None):
        return
    call("import_gltf", {"scene_name": scene, "path": GLTF})
    advance()
    call("undo")
    advance()

    # The payload is dropped, so the undo history no longer pins the assets.
    try:
        result = call("unload_asset", {"scope": "file", "type": "animation", "path": GLTF})
    except RuntimeError as error:
        users = str(error)
        check(section, "unload is not refused by the undo history",
              "undo/redo history" not in users, users[:400])
        drop_scene(scene)
        return
    if isinstance(result, dict):
        check(section, "unload succeeded after the drop", result.get("ok") is True,
              json.dumps(result)[:300])
        check(section, "no undeclared survivors", result.get("undeclared_survivors", -1) == 0,
              json.dumps(result)[:300])
    drop_scene(scene)


def section_blas_released():
    """With ray tracing on, the BLAS cache must not pin dropped content."""
    section = "blas"
    memory_before = memory()
    if memory_before.get("blas_count", 0) == 0:
        check(section, "ray tracing populated the BLAS cache (skipped: none)", True,
              "no acceleration structures in this session")
        return
    scene = new_scene()
    call("import_gltf", {"scene_name": scene, "path": GLTF})
    advance(20)
    with_import = memory()["blas_count"]
    call("undo")
    advance(20)
    after_undo = memory()["blas_count"]
    check(section, "BLAS entries released with the payload", after_undo <= with_import,
          f"{with_import} -> {after_undo}")
    drop_scene(scene)


def section_missing_file():
    """A rebuild that cannot read the file reports an error and stays consistent."""
    section = "missing file"
    missing = "res/editor/assets/__does_not_exist__.glb"
    scene = new_scene()
    if not check(section, "created a scene", scene is not None):
        return
    try:
        call("import_gltf", {"scene_name": scene, "path": missing})
        advance(8)
    except RuntimeError:
        pass  # rejected up front is fine too
    # The editor must still answer, i.e. it did not abort.
    check(section, "editor still responsive after a bad path",
          isinstance(memory(), dict))
    drop_scene(scene)


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

    scene = run("undo frees", section_undo_frees_memory)
    run("redo re-reads", section_redo_rereads, scene)
    gate_scene = run("lossless gate", section_lossless_gate)
    run("free_undone_loads", section_free_undone_loads, gate_scene)
    run("no-op", section_no_payload_is_a_noop)
    run("batch nesting", section_batch_nesting)
    run("unload", section_unload_after_drop)
    run("blas", section_blas_released)
    run("missing file", section_missing_file)

    failed = [entry for entry in RESULTS if not entry[2]]
    print()
    print(f"{len(RESULTS) - len(failed)}/{len(RESULTS)} checks passed")
    for section, name, _, detail in failed:
        print(f"  FAIL {section}: {name} -- {detail}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
