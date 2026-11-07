#!/usr/bin/env python3
"""Bistro recipe of the editor and prepared assets (doc/plans/prepared_assets.md
R13.2; doc/editor/prepared_assets.md "Verification", Bistro).

Launches its own headless editors (doc/agents/editor_runs.md; the Editor class
of scripts/prepared_asset_verify.py) and stops them by the pid it launched.
--bistro names a copy of res/editor/assets/bistro_lights_compressed/bistro_lights_c.gltf
outside res/ (the caller's copy: the script writes only the foreign scene's
editor asset beside it, and removes it again). Everything else it writes lives
in a work directory it removes at the end (--keep keeps it).

The source asset is made the way doc/agents/quest.md makes the Quest's
Bistro: the foreign glTF opened, its import root rotated -90 degrees about X,
LOD chains generated for the scene, export settings added (instancing
duplicate, the bake on, the PVS grid of 4 m cells over the content bounds in
x and z and y 0.6 - 4.6) and saved as an erhe scene.

Checks (one PASS / FAIL line each, with numbers):

1. R6.1: the foreign scene's world transforms are bit-identical after the
   save and the reopen, and its World-space mesh items (Bake World-Space
   Meshes before the save) are the reopened scene's: the same keys and, per
   node record, the same payload bytes (the save writes the source soups,
   whose content hashes the reopen computes).
2. The first Export (pc) makes every item the open did not store (the open
   stores the Local-space mesh items and their BVH items); the prepared asset holds exactly
   the items its Scene item names, no Editor data, no Basis texture, no item
   of the other target and no Local-space mesh of a baked node.
3. A second Export in a new session makes nothing (and writes the prepared
   asset anew); the status
   matches; Bake World-Space Meshes makes nothing and every admitted node is
   valid.
4. A texture property edit makes that texture's item and the Scene item
   alone, and setting it back makes nothing; moving one baked node makes its
   World-space mesh, its BVH and the Scene item alone, and after undo an
   Export makes nothing.
5. An Export killed while making World-space mesh items and another killed
   while copying (no prepared asset left, only its temporary file), then run
   again, end with the same keys and per key the same
   kind, target, stamp, meta, info and payload bytes as the uninterrupted
   first Export.
6. With the source moved away the pc prepared asset opens as a prepared run,
   and render_scene_image from the bench camera of scripts/bistro_frame_bench.py
   differs from the source scene's image in at most 0.5 % of the pixels by
   more than 2 / 255 (doc/editor/world_space_bake.md "Verification"; the share
   differing at all is printed).
7. pvs_mode rebake (quest): the Export bakes the PVS, the quest prepared asset
   holds exactly what its Scene item names and nothing of target pc, and its
   prepared run on the host reports the PVS ready.
8. R12.2, R12.3: no .gltf / .glb file of the work directory carries a
   derived-data extension, no glTF file parse reads prepared mesh data, and
   the run creates no cache/mesh_optimizer and no cache/bvh directory (and
   touches no file in one an earlier build left).

Usage: python3 scripts/prepared_asset_bistro_verify.py --editor <headless editor> --bistro <copy>/bistro_lights_c.gltf
       [--work <parent dir>] [--keep] [--skip-kill] [--skip-pvs] [--keep-quest <dir>]
--keep-quest moves the quest prepared asset (with its PVS) into <dir> and
copies the source asset (the saved erhe scene) beside it, the Quest baseline to
compare the prepared run with (doc/agents/quest.md "Prepared assets on the
device"; push the two into different device directories, as a prepared asset
beside its readable source opens the source).
Exit code 0 = every check passed.
"""

import argparse
import hashlib
import json
import math
import os
import shutil
import sqlite3
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import prepared_asset_verify as pav  # noqa: E402
from prepared_asset_verify import check, Editor, ITEM_URI  # noqa: E402

KIND_NAMES = {1: "scene", 2: "local_mesh", 3: "world_mesh", 4: "raytrace_bvh", 5: "texture", 6: "basis_texture", 7: "pvs", 8: "editor_data"}
ROOT_ROTATION = [-0.7071068, 0.0, 0.0, 0.7071068]
PVS_Y = [0.6, 4.6]
PVS_CELL_SIZE = 4.0
# scripts/bistro_frame_bench.py DEFAULT_CAMERA_* (world space, import root rotated).
BENCH_CAMERA_TRANSLATION = [-29.2810115814209, 9.677236557006836, -16.337175369262695]
BENCH_CAMERA_ROTATION = [-0.03749912232160568, -0.9181863069534302, -0.08965181559324265, 0.3840445876121521]
IMAGE_WIDTH, IMAGE_HEIGHT = 960, 540
# doc/editor/world_space_bake.md "Verification": a share of the pixels.
MAX_DIFFERING_SHARE = 0.005
SHADING_LEVELS = 2


# --- Containers ----------------------------------------------------------------

def connect(path):
    return sqlite3.connect(f"file:{path}?mode=ro", uri=True)


def item_count(path):
    try:
        connection = connect(path)
        try:
            return connection.execute("SELECT count(*) FROM item").fetchone()[0]
        finally:
            connection.close()
    except sqlite3.Error:
        return 0


def manifest(path):
    """{key: sha256 of (kind, target, stamp, meta, info, payload)} of a container, read row by row."""
    result = {}
    connection = connect(path)
    try:
        for key, kind, target, stamp, meta, info, payload in connection.execute(
                "SELECT i.key, i.kind, i.target, i.stamp, i.meta, i.info, p.payload FROM item AS i JOIN item_payload AS p ON p.id = i.id"):
            digest = hashlib.sha256()
            for field in (str(kind), target or "", stamp or "", meta or "", info or ""):
                text = field.encode("utf-8")
                digest.update(len(text).to_bytes(8, "little"))
                digest.update(text)
            digest.update(bytes(payload or b""))
            result[bytes(key).hex()] = digest.hexdigest()
    finally:
        connection.close()
    return result


def store_rows(path):
    """{key: (kind, target, stamp)} and the asset records of a container."""
    connection = connect(path)
    try:
        rows = {bytes(key).hex(): (kind, target, stamp) for key, kind, target, stamp in connection.execute(
            "SELECT key, kind, target, stamp FROM item")}
        records = dict(connection.execute("SELECT name, value FROM asset").fetchall())
    finally:
        connection.close()
    return rows, records


def scene_item(path):
    """The Scene item (GLB bytes) a prepared asset records."""
    connection = connect(path)
    try:
        records = dict(connection.execute("SELECT name, value FROM asset").fetchall())
        key = records["prepared.scene_key"]
        row = connection.execute("SELECT p.payload FROM item AS i JOIN item_payload AS p ON p.id = i.id WHERE i.key = ?", (bytes.fromhex(key),)).fetchone()
    finally:
        connection.close()
    return key, bytes(row[0])


def kind_counts_text(rows):
    counts = {}
    for kind, target, _ in rows.values():
        name = f"{KIND_NAMES.get(kind, kind)}:{target or '-'}"
        counts[name] = counts.get(name, 0) + 1
    return " ".join(f"{name}={count}" for name, count in sorted(counts.items()))


def mesh_buffer_uses(document):
    """{item key: set of under-World-Space flags} of the buffers the Scene item's mesh nodes read."""
    nodes = document.get("nodes", [])
    under = [False] * len(nodes)
    for index, node in enumerate(nodes):
        if node.get("name") == "World Space":
            stack = list(node.get("children", []))
            while stack:
                child = stack.pop()
                under[child] = True
                stack.extend(nodes[child].get("children", []))
    uses = {}
    for index, node in enumerate(nodes):
        if "mesh" not in node:
            continue
        for primitive in document["meshes"][node["mesh"]]["primitives"]:
            accessors = list(primitive.get("attributes", {}).values())
            if "indices" in primitive:
                accessors.append(primitive["indices"])
            for accessor_index in accessors:
                view_index = document["accessors"][accessor_index].get("bufferView")
                if view_index is None:
                    continue
                uri = document["buffers"][document["bufferViews"][view_index]["buffer"]].get("uri", "")
                if uri.startswith(ITEM_URI):
                    uses.setdefault(uri[len(ITEM_URI):], set()).add(under[index])
    return uses


def check_prepared_asset(label, prepared_path, other_target):
    """The prepared asset holds exactly what its Scene item names, nothing of another target, no
    Editor data, no Basis texture and no Local-space mesh of a baked node (R9.3, R13.2)."""
    rows, _ = store_rows(prepared_path)
    scene_key, glb = scene_item(prepared_path)
    document = pav.scene_item_document(glb)
    named = pav.scene_item_keys(glb) | {scene_key}
    check(f"{label}: the prepared asset holds exactly the items its Scene item names plus the Scene item",
          set(rows) == named, f"{len(rows)} items, {len(named)} named; {kind_counts_text(rows)}; {os.path.getsize(prepared_path)} bytes")
    others = [key for key, (kind, target, _) in rows.items() if (target == other_target) or (kind in (6, 8))]
    check(f"{label}: no item of target {other_target}, no Basis texture item and no Editor data item", not others, f"{len(others)} such items")
    uses = mesh_buffer_uses(document)
    local_baked = [key for key, (kind, _, _) in rows.items() if (kind == 2) and (True in uses.get(key, set()))]
    world_outside = [key for key, (kind, _, _) in rows.items() if (kind == 3) and (False in uses.get(key, set()))]
    local_count = sum(1 for kind, _, _ in rows.values() if kind == 2)
    check(f"{label}: no Local-space mesh item of a baked node; World-space mesh items only under World Space",
          (not local_baked) and (not world_outside), f"{local_count} Local-space mesh items, {len(local_baked)} under World Space, {len(world_outside)} World-space outside it")
    return rows, document


# --- Editor helpers -------------------------------------------------------------

def made_counts(status):
    return {kind: value.get("made", 0) for kind, value in status.get("export", {}).get("counts", {}).items() if value.get("made", 0)}


def written_counts(status):
    return {kind: value.get("written", 0) for kind, value in status.get("export", {}).get("counts", {}).items() if value.get("written", 0)}


def export_text(status):
    export = status.get("export", {})
    return (f"{pav.counts_text(status)}; ensure {export.get('ensure_ms', 0):.0f} ms, scene {export.get('scene_ms', 0):.0f} ms, "
            f"copy {export.get('copy_ms', 0):.0f} ms, pvs {export.get('pvs_ms', 0):.0f} ms; prepared {export.get('prepared_item_count')} items "
            f"{export.get('prepared_asset_bytes')} bytes; inline primitives {export.get('inline_primitive_count')} images {export.get('inline_image_count')}")


def timed_export(editor, scene, target="pc"):
    start = time.monotonic()
    status = editor.export(scene, target)
    status["wall_s"] = time.monotonic() - start
    return status


def bench_camera():
    """eye / target of the bench camera: its translation and its -Z axis."""
    x, y, z, w = BENCH_CAMERA_ROTATION
    # Rotation of (0, 0, -1) by the quaternion.
    forward = [-(2.0 * (x * z + w * y)), -(2.0 * (y * z - w * x)), -(1.0 - 2.0 * (x * x + y * y))]
    length = math.sqrt(sum(c * c for c in forward))
    eye = BENCH_CAMERA_TRANSLATION
    return eye, [eye[i] + 10.0 * forward[i] / length for i in range(3)]


def render(editor, scene, path, mesh_form="source"):
    eye, target = bench_camera()
    camera = {"eye": eye, "target": target, "fov_y_degrees": 50.0, "near": 0.1, "far": 1000.0}
    editor.call("render_scene_image", {"scene": scene, "camera": camera, "width": IMAGE_WIDTH, "height": IMAGE_HEIGHT,
                                       "output": "png", "path": path, "mesh_form": mesh_form})
    return path


def image_statistics(path_a, path_b):
    """(pixels differing, pixels differing by more than SHADING_LEVELS, largest channel difference)."""
    wa, ha, a = pav.decode_png(path_a)
    wb, hb, b = pav.decode_png(path_b)
    if (wa, ha) != (wb, hb):
        return -1, -1, 255
    differing = beyond = largest = 0
    for i in range(0, len(a), 4):
        if a[i:i + 4] != b[i:i + 4]:
            differing += 1
            difference = max(abs(a[i + c] - b[i + c]) for c in range(3))
            largest = max(largest, difference)
            if difference > SHADING_LEVELS:
                beyond += 1
    return differing, beyond, largest


def check_image(label, path_a, path_b):
    differing, beyond, largest = image_statistics(path_a, path_b)
    pixels = IMAGE_WIDTH * IMAGE_HEIGHT
    check(f"{label}: at most {MAX_DIFFERING_SHARE * 100:.1f}% of the pixels differ by more than {SHADING_LEVELS}/255",
          (beyond >= 0) and (beyond <= MAX_DIFFERING_SHARE * pixels),
          f"{differing} px ({100.0 * differing / pixels:.2f}%) differ, {beyond} px ({100.0 * beyond / pixels:.3f}%) by more than {SHADING_LEVELS}/255, max {largest}/255")


def content_world_transforms(editor, scene):
    """{gltf_uid: world_from_node_hex} of the content nodes."""
    result = {}
    for node in editor.call("get_scene_nodes", {"scene_name": scene})["nodes"]:
        if node.get("content", False) and (not node.get("render_proxy", False)) and node.get("gltf_uid"):
            result[node["gltf_uid"]] = node.get("world_from_node_hex")
    return result


def world_mesh_records(path):
    """{key: sha256 of the payload} of the World-space mesh items of a container (one item per
    primitive: the primitives of one mesh share the node record, so the key names an item)."""
    connection = connect(path)
    try:
        return {bytes(key).hex(): hashlib.sha256(bytes(payload)).hexdigest() for key, payload in connection.execute(
            "SELECT i.key, p.payload FROM item AS i JOIN item_payload AS p ON p.id = i.id WHERE i.kind = 3")}
    finally:
        connection.close()


def wait_stage_with_items(editor, job_id, stage, store_path):
    """Polls until the job reports stage and the store holds an item; False when the job ended first."""
    while True:
        status = editor.job(job_id)
        if status.get("state") in ("done", "failed"):
            return False, status
        progress = status.get("progress", {})
        if (progress.get("stage") == stage) and (progress.get("done", 0) > 0):
            if (store_path is None) or (item_count(store_path) > 0):
                return True, status
        time.sleep(0.002)


def move_aside(paths, directory):
    moved = []
    for path in paths:
        if os.path.exists(path):
            destination = os.path.join(directory, os.path.basename(path))
            shutil.move(path, destination)
            moved.append((destination, path))
    return moved


def remove_store(path):
    for suffix in ("", "-wal", "-shm", "-journal"):
        if os.path.exists(path + suffix):
            os.remove(path + suffix)


# --- Phases -----------------------------------------------------------------------

def phase_source(args, work, state):
    """Foreign Bistro -> rotated, LOD chains, export settings, Bake World-Space Meshes, erhe save."""
    editor = Editor(args.editor)
    try:
        start = time.monotonic()
        name = editor.open_foreign(args.bistro)
        print(f"opened the foreign Bistro in {time.monotonic() - start:.1f} s", flush=True)
        editor.call("set_node_transform", {"scene_name": name, "node_name": name, "space": "world",
                                           "translation": [0.0, 0.0, 0.0], "rotation_xyzw": ROOT_ROTATION})
        editor.wait_idle()
        start = time.monotonic()
        lods = editor.call("generate_lods", {"scene_name": name, "scope": "scene"})
        editor.wait_idle(timeout=3600.0)
        print(f"generate_lods: {lods.get('primitive_count')} primitives of {lods.get('mesh_count')} meshes in {time.monotonic() - start:.1f} s", flush=True)
        bounds = editor.call("get_node_details", {"scene_name": name, "node_name": name})["subtree_world_aabb"]
        region_min = [bounds["min"][0], PVS_Y[0], bounds["min"][2]]
        region_max = [bounds["max"][0], PVS_Y[1], bounds["max"][2]]
        state["pvs_region"] = (region_min, region_max)
        cells = [math.ceil((region_max[i] - region_min[i]) / PVS_CELL_SIZE) for i in range(3)]
        print(f"PVS region {region_min} .. {region_max}, {cells} = {cells[0] * cells[1] * cells[2]} cells of {PVS_CELL_SIZE} m", flush=True)
        editor.set_export_settings(name, {"bake_static_to_world_space": True, "instancing": "duplicate", "pvs_mode": "off",
                                          "pvs_region_min": region_min, "pvs_region_max": region_max, "pvs_cell_size": PVS_CELL_SIZE})
        bake = pav.bake_world(editor, name)
        status = pav.world_status(editor, name)
        check("foreign Bistro: Bake World-Space Meshes makes the World-space mesh items and every admitted node is valid",
              (bake.get("state") == "done") and (status.get("valid_count") == status.get("admitted_count")) and (status.get("admitted_count", 0) > 0),
              f"{pav.counts_text(bake)} in {bake.get('save_ms', 0):.0f} ms; admitted {status.get('admitted_count')} valid {status.get('valid_count')}")
        state["foreign_transforms"] = content_world_transforms(editor, name)
        editor.save(name, state["source"])
        print(f"saved {state['source']} ({os.path.getsize(state['source'])} bytes)", flush=True)
        editor.close(name)
    finally:
        editor.stop()
    state["foreign_world_records"] = world_mesh_records(state["foreign_editor_asset"])
    remove_store(state["foreign_editor_asset"])


def phase_first_export(args, work, state):
    editor = Editor(args.editor)
    try:
        start = time.monotonic()
        scene = editor.load(state["source"])
        print(f"loaded the erhe-saved source in {time.monotonic() - start:.1f} s", flush=True)
        transforms = content_world_transforms(editor, scene)
        # The nodes the save writes (the editor's injected default camera is session-only, never saved).
        saved_uids = {node["uid"] for node in pav.glb_document(state["source"]).get("nodes", []) if "uid" in node}
        foreign = {uid: value for uid, value in state["foreign_transforms"].items() if uid in saved_uids}
        reopened = {uid: value for uid, value in transforms.items() if uid in saved_uids}
        differing = [uid for uid, hex_value in foreign.items() if reopened.get(uid) != hex_value]
        check("R6.1: every saved content node of the foreign Bistro has its uid and a bit-identical world transform after the save and the reopen",
              (not differing) and (len(reopened) == len(foreign)) and (len(foreign) > 0),
              f"{len(foreign)} saved nodes before, {len(reopened)} after, {len(differing)} differ; "
              f"{len(state['foreign_transforms']) - len(foreign)} unsaved before, {len(transforms) - len(reopened)} after")
        first = timed_export(editor, scene)
        counts = first.get("export", {}).get("counts", {})
        # R8.3: the open stored the Local-space mesh items and their BVH items; the Export finds those present.
        check("first Export (pc) makes every item the open did not store: only mesh and BVH items present, every made item written",
              (first.get("state") == "done") and
              all(value.get("present", 0) == 0 for kind, value in counts.items() if kind not in ("local_mesh", "raytrace_bvh")) and
              (counts.get("local_mesh", {}).get("made", -1) == 0) and
              all((value.get("made", 0) + value.get("present", 0)) == value.get("written", 0) for value in counts.values() if value.get("made", 0) > 0) and
              (first.get("export", {}).get("inline_primitive_count", -1) == 0) and (first.get("export", {}).get("inline_image_count", -1) == 0),
              f"{export_text(first)}; {first['wall_s']:.1f} s")
        state["first"] = first
        records = world_mesh_records(state["editor_asset"])
        foreign_records = state["foreign_world_records"]
        common = set(records) & set(foreign_records)
        same_payload = sum(1 for key in common if records[key] == foreign_records[key])
        # R3.5: the save writes the source soups (authoring soups), so the reopened scene keys as the import did.
        check("R6.1: the World-space mesh items of the foreign Bistro's bake are the reopened erhe scene's: the same keys and payload bytes",
              (len(records) == len(foreign_records)) and (len(common) == len(records)) and (same_payload == len(records)),
              f"{len(foreign_records)} foreign, {len(records)} reopened, {len(common)} keys in common; "
              f"{same_payload} of {len(records)} items with the same payload bytes")
        check_prepared_asset("first Export (pc)", state["prepared_pc"], "quest")
        state["editor_manifest"] = manifest(state["editor_asset"])
        state["prepared_manifest"] = manifest(state["prepared_pc"])
        render(editor, scene, os.path.join(state["images"], "source.png"))
        bake = pav.bake_world(editor, scene)
        status = pav.world_status(editor, scene)
        check("Bake World-Space Meshes after the Export makes nothing and loads the world-space form",
              (bake.get("state") == "done") and (sum(made_counts(bake).values()) == 0) and (status.get("available") is True),
              f"{pav.counts_text(bake)}; world-space form load {bake.get('save_ms', 0):.0f} ms; admitted {status.get('admitted_count')} valid {status.get('valid_count')}")
        render(editor, scene, os.path.join(state["images"], "world_space.png"), "world_space")
        check_image("world-space form vs source meshes", os.path.join(state["images"], "source.png"), os.path.join(state["images"], "world_space.png"))
        editor.close(scene)
    finally:
        editor.stop()


def phase_second_export_and_edits(args, work, state):
    editor = Editor(args.editor)
    try:
        scene = editor.load(state["source"])
        first_key = state["first"]["export"]["scene_key"]
        second = timed_export(editor, scene)
        check("second Export in a new session makes nothing and writes the prepared asset anew (R9.3, R9.4)",
              (second.get("state") == "done") and (not made_counts(second)) and pav.rewritten(second) and
              (second.get("export", {}).get("scene_key") == first_key), f"{export_text(second)}; {second['wall_s']:.1f} s")
        status = editor.status(scene)
        world = pav.status_kind(status, "world_mesh")
        check("status after the second Export: nothing missing, the prepared asset matches",
              (status.get("matches") is True) and all(counts.get("missing", 0) == 0 for counts in status.get("kinds", {}).values()),
              pav.status_text(status) + f"; world-space meshes {json.dumps({k: world.get(k) for k in ('present', 'missing')})}")
        bake = pav.bake_world(editor, scene)
        world_state = pav.world_status(editor, scene)
        check("Bake World-Space Meshes in the new session makes nothing; every admitted node is valid",
              (sum(made_counts(bake).values()) == 0) and (world_state.get("valid_count") == world_state.get("admitted_count")),
              f"{pav.counts_text(bake)}; admitted {world_state.get('admitted_count')} valid {world_state.get('valid_count')}")

        # One texture's transfer function.
        textures = editor.call("get_scene_textures", {"scene_name": scene})["textures"]
        texture = next(entry for entry in textures if entry.get("format") == "format_bc7_srgb" and entry.get("width", 0) >= 256)
        before = pav.texture_properties(editor, texture["id"])
        pav.set_texture_property(editor, texture["id"], "transfer_function", "Linear")
        edited = timed_export(editor, scene)
        check(f"texture edit ('{texture['name']}' transfer function Linear): Export makes one Texture item and the Scene item alone",
              (edited.get("state") == "done") and (made_counts(edited) == {"texture": 1, "scene": 1}), export_text(edited))
        pav.set_texture_property(editor, texture["id"], "transfer_function", before.get("transfer_function", "sRGB"))
        back = timed_export(editor, scene)
        check("texture set back: Export makes nothing and has the first Scene key",
              (back.get("state") == "done") and (not made_counts(back)) and (back.get("export", {}).get("scene_key") == first_key), export_text(back))

        # One baked node moved, then undone.
        _, glb = scene_item(state["prepared_pc"])
        document = pav.scene_item_document(glb)
        nodes = document["nodes"]
        world_node = next(node for node in nodes if node.get("name") == "World Space")
        baked = {}
        for child in world_node.get("children", []):
            node = nodes[child]
            if "mesh" in node:
                baked.setdefault(node.get("name"), []).append(len(document["meshes"][node["mesh"]]["primitives"]))
        scene_nodes = editor.call("get_scene_nodes", {"scene_name": scene})["nodes"]
        name_counts = {}
        for node in scene_nodes:
            name_counts[node["name"]] = name_counts.get(node["name"], 0) + 1
        candidate = next(node for node in scene_nodes
                         if (name_counts[node["name"]] == 1) and (baked.get(node["name"]) is not None) and (len(baked[node["name"]]) == 1))
        primitive_count = baked[candidate["name"]][0]
        position = candidate["position"]
        editor.call("set_node_transform", {"scene_name": scene, "node_id": candidate["id"], "space": "local",
                                           "translation": [position[0], position[1] + 1.0, position[2]]})
        editor.wait_idle()
        moved_state = pav.world_status(editor, scene)
        moved = timed_export(editor, scene)
        check(f"node move ('{candidate['name']}', {primitive_count} primitive(s)): Export makes its World-space mesh items, their BVH items and the Scene item alone",
              (moved.get("state") == "done") and (made_counts(moved) == {"world_mesh": primitive_count, "raytrace_bvh": primitive_count, "scene": 1}),
              f"{export_text(moved)}; before the Export valid {moved_state.get('valid_count')} of {moved_state.get('admitted_count')}")
        editor.call("undo", {})
        editor.wait_idle()
        undone_state = pav.world_status(editor, scene)
        undone = timed_export(editor, scene)
        check("undo of the move: every node valid again and Export makes nothing, with the first Scene key",
              (undone.get("state") == "done") and (not made_counts(undone)) and (undone.get("export", {}).get("scene_key") == first_key) and
              (undone_state.get("valid_count") == undone_state.get("admitted_count")),
              f"{export_text(undone)}; valid {undone_state.get('valid_count')} of {undone_state.get('admitted_count')}")
        editor.close(scene)
    finally:
        editor.stop()


def phase_kill(args, work, state):
    editor_asset, prepared = state["editor_asset"], state["prepared_pc"]
    remove_store(editor_asset)
    remove_store(prepared)

    editor = Editor(args.editor)
    scene = editor.load(state["source"])
    hit, status = wait_stage_with_items(editor, editor.begin_export(scene), "making world-space mesh items", editor_asset)
    editor.kill()
    made = item_count(editor_asset)
    total = len(state["editor_manifest"])
    check("Export killed while making World-space mesh items, with some items made",
          hit and (0 < made < total) and not os.path.isfile(prepared),
          f"{made}/{total} items in the editor asset at the kill; progress {json.dumps(status.get('progress', {}))}")

    editor = Editor(args.editor)
    scene = editor.load(state["source"])
    hit, status = wait_stage_with_items(editor, editor.begin_export(scene), "copying items", None)
    editor.kill()
    temporaries = [name for name in os.listdir(os.path.dirname(prepared)) if name.startswith(os.path.basename(prepared) + ".tmp-")]
    check("Export killed while copying leaves no prepared asset, only its temporary file", hit and not os.path.exists(prepared) and bool(temporaries),
          f"progress {json.dumps(status.get('progress', {}))}; temporaries {temporaries}")

    editor = Editor(args.editor)
    try:
        scene = editor.load(state["source"])
        rerun = timed_export(editor, scene)
        written = written_counts(rerun)
        check("Export after the kills writes every item, removes the temporary file and has the first Scene key",
              (rerun.get("state") == "done") and (sum(written.values()) == len(state["prepared_manifest"])) and
              not any(name.startswith(os.path.basename(prepared) + ".tmp-") for name in os.listdir(os.path.dirname(prepared))) and
              (rerun.get("export", {}).get("scene_key") == state["first"]["export"]["scene_key"]), export_text(rerun))
        editor.close(scene)
    finally:
        editor.stop()
    for label, path, expected in (("editor asset", editor_asset, state["editor_manifest"]), ("prepared asset", prepared, state["prepared_manifest"])):
        actual = manifest(path)
        different = [key for key in expected if (key in actual) and (actual[key] != expected[key])]
        check(f"{label} after the kills vs the uninterrupted first Export: same keys, and per key the same kind, target, stamp, meta, info and payload bytes",
              (set(actual) == set(expected)) and not different,
              f"{len(actual)} vs {len(expected)} items, {len(set(actual) ^ set(expected))} keys in one only, {len(different)} differ")
    check("the prepared asset after the kills is one file at rest", not any(os.path.exists(prepared + suffix) for suffix in ("-wal", "-shm", "-journal")))


def phase_prepared_run(args, work, state):
    away = os.path.join(work, "away")
    os.makedirs(away, exist_ok=True)
    stem = os.path.splitext(state["source"])[0]
    moved = move_aside([state["source"]] + [state["editor_asset"] + suffix for suffix in ("", "-wal", "-shm")], away)
    editor = Editor(args.editor)
    try:
        start = time.monotonic()
        reply = editor.call("load_scene", {"path": state["prepared_pc"]})
        listed = None
        while reply["scene_name"] not in editor.scene_names():
            time.sleep(0.05)
        listed = time.monotonic() - start
        editor.wait_idle()
        idle = time.monotonic() - start
        run = reply["scene_name"]
        memory = editor.call("get_memory_usage", {})
        process = memory.get("process", {})
        check("the pc prepared asset opens as a prepared run with the source away",
              run == os.path.basename(stem) + ".pc", f"scene {run}; listed {listed:.1f} s, idle {idle:.1f} s; "
              f"VmHWM {process.get('VmHWM_bytes', 0) / 1048576.0:.0f} MB, retained image source bytes {memory.get('cpu_images', {}).get('retained_source_bytes')}")
        render(editor, run, os.path.join(state["images"], "prepared_run.png"))
        check_image("pc prepared run vs source meshes", os.path.join(state["images"], "source.png"), os.path.join(state["images"], "prepared_run.png"))
        differing, beyond, largest = image_statistics(os.path.join(state["images"], "world_space.png"), os.path.join(state["images"], "prepared_run.png"))
        print(f"      pc prepared run vs the open scene's world-space form: {differing} px differ, {beyond} by more than {SHADING_LEVELS}/255, max {largest}/255", flush=True)
        editor.close(run)
    finally:
        editor.stop()
        for destination, path in moved:
            shutil.move(destination, path)


def phase_pvs(args, work, state):
    editor = Editor(args.editor)
    try:
        scene = editor.load(state["source"])
        editor.set_export_settings(scene, {"pvs_mode": "rebake"})
        rebake = timed_export(editor, scene, "quest")
        export = rebake.get("export", {})
        pvs = pav.export_counts(rebake, "pvs")
        check("quest Export with pvs_mode rebake bakes the PVS and writes the PVS item",
              (rebake.get("state") == "done") and (pvs.get("made") == 1) and (pvs.get("written") == 1),
              f"{export_text(rebake)}; {export.get('pvs_cell_count')} cells; {rebake['wall_s']:.1f} s")
        rows, document = check_prepared_asset("quest Export", state["prepared_quest"], "pc")
        texture_stamps = sorted({stamp for kind, _, stamp in rows.values() if kind == 5})
        print(f"      quest Texture item stamps {texture_stamps}", flush=True)
        status = pav.pvs_status(editor, scene)
        print(f"      open scene PVS after the rebake: {status.get('state')} {status.get('form')} bits_hash {status.get('bits_hash')}", flush=True)
        state["pvs_bits_hash"] = status.get("bits_hash")
        editor.close(scene)
    finally:
        editor.stop()

    away = os.path.join(work, "away")
    os.makedirs(away, exist_ok=True)
    moved = move_aside([state["source"]] + [state["editor_asset"] + suffix for suffix in ("", "-wal", "-shm")], away)
    editor = Editor(args.editor)
    try:
        start = time.monotonic()
        run = editor.load(state["prepared_quest"])
        status = pav.pvs_status(editor, run)
        check("the quest prepared run (on the host) reports its PVS ready",
              status.get("state") == "ready", f"scene {run} in {time.monotonic() - start:.1f} s; {status.get('state')} {status.get('form')} "
              f"columns {status.get('column_count')} bits_hash {status.get('bits_hash')}")
        editor.close(run)
    finally:
        editor.stop()
        for destination, path in moved:
            shutil.move(destination, path)
    if args.keep_quest:
        os.makedirs(args.keep_quest, exist_ok=True)
        shutil.move(state["prepared_quest"], os.path.join(args.keep_quest, os.path.basename(state["prepared_quest"])))
        shutil.copy2(state["source"], os.path.join(args.keep_quest, os.path.basename(state["source"])))
        print(f"kept {os.path.join(args.keep_quest, os.path.basename(state['prepared_quest']))} and the source {os.path.basename(state['source'])}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", required=True, help="headless editor binary (relative to the repository root or absolute)")
    parser.add_argument("--bistro", required=True, help="a copy of bistro_lights_c.gltf outside res/")
    parser.add_argument("--work", default=None, help="parent directory of the work directory (default: the system temporary directory)")
    parser.add_argument("--keep", action="store_true", help="keep the work directory")
    parser.add_argument("--skip-kill", action="store_true", help="skip the kill checks")
    parser.add_argument("--skip-pvs", action="store_true", help="skip the PVS rebake (about 200 s)")
    parser.add_argument("--keep-quest", default=None, metavar="DIR", help="move the quest prepared asset and copy the source asset into DIR")
    args = parser.parse_args()
    args.bistro = os.path.abspath(args.bistro)
    if args.keep_quest:
        args.keep_quest = os.path.abspath(args.keep_quest)

    foreign_editor_asset = os.path.splitext(args.bistro)[0] + ".erhe_editor"
    if not os.path.isfile(args.bistro):
        print(f"no Bistro at {args.bistro}")
        return 2
    if os.path.exists(foreign_editor_asset):
        print(f"{foreign_editor_asset} exists: remove it first (the foreign scene's bake must start from an empty editor asset)")
        return 2
    start = time.monotonic()
    cache_before = pav.cache_directory_state()
    work = tempfile.mkdtemp(prefix="prepared_asset_bistro_verify_", dir=args.work)
    print(f"work directory {work}", flush=True)
    images = os.path.join(work, "images")
    os.makedirs(images, exist_ok=True)
    source = os.path.join(work, "bistro_src.glb")
    state = {
        "source": source, "images": images,
        "editor_asset": os.path.join(work, "bistro_src.erhe_editor"),
        "prepared_pc": os.path.join(work, "bistro_src.pc.erhe_prepared"),
        "prepared_quest": os.path.join(work, "bistro_src.quest.erhe_prepared"),
        "foreign_editor_asset": foreign_editor_asset,
    }
    try:
        phase_source(args, work, state)
        phase_first_export(args, work, state)
        phase_second_export_and_edits(args, work, state)
        if not args.skip_kill:
            phase_kill(args, work, state)
        phase_prepared_run(args, work, state)
        if not args.skip_pvs:
            phase_pvs(args, work, state)
    except Exception as error:  # noqa: BLE001 - a failed step is a failed check
        check("run completed without an error", False, f"{type(error).__name__}: {error}")
    finally:
        for editor in pav.LAUNCHED:
            editor.stop()
        remove_store(foreign_editor_asset)
        pav.check_cache_directories(cache_before)
        pav.check_no_derived_data_files(work)
        pav.check_no_file_parse_reads()
        if args.keep:
            print(f"kept {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)
    failed = [label for label, ok in pav.RESULTS if not ok]
    print(f"\n{len(pav.RESULTS) - len(failed)}/{len(pav.RESULTS)} checks passed, {len(failed)} failed in {time.monotonic() - start:.0f} s")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
