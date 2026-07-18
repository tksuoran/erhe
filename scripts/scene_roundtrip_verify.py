#!/usr/bin/env python3
"""Phase 6 scene-persistence verification harness (glTF-everything round-trip).

Drives the in-editor MCP server (headless Vulkan build) through the phase 6
verification of doc/gltf-scene-roundtrip-plan.md:

1. Builds a comprehensive scene: shape instances (geometry-normative
   meshes), an imported textured glTF asset (triangle soup + embedded
   images), an imported skinned + animated asset, physics bodies + a joint,
   a brush placement, a graph mesh binding, a graph texture bound to a
   material slot, a layout attachment, tags, a locked node, authored
   animation keys, and an external-asset prefab instance.
2. Saves the scene and validates every ERHE_* extension payload in the
   .glb against its JSON schema (doc/gltf_extensions/schema/), asserting
   full extension coverage (all 11 ERHE_* extensions present) and clean
   extensionsUsed / extensionsRequired conventions. Then asserts the R5
   data-loss tripwire (asset-manager plan step R5.1): every
   library-DEFINED material / brush / animation appears in the file with
   its FULL payload (parameter values, brush geometry, animation
   samplers) - never as a name-only reference stub.
3. Loads the saved scene back and diffs the MCP-visible state: node set
   (name / parent / TRS / tags / attachment types / locked), per-node
   physics and joint fields, materials, animations (channel-level), the
   brush library, and graph assets. Captures screenshots before and after.
4. Round-trips a prefab scene ("res/editor/scenes/Prefab test.glb") when
   present (untracked user scene; the section is skipped when absent).
5. Optional foreign-tool checks: the Khronos glTF validator
   (--gltf-validator or ERHE_GLTF_VALIDATOR) and a Blender headless
   import + render (--blender or ERHE_BLENDER; a Windows default install
   is probed automatically).

Run each invocation against a FRESH headless editor session:

    build_vs2026_vulkan_headless\\src\\editor\\Debug\\editor.exe   (from repo root)
    py -3 scripts/scene_roundtrip_verify.py

Exit code 0 = every executed check passed (skipped sections do not fail).
Artifacts: logs/phase6_*.png screenshots; the saved .glb files under
res/editor/scenes/ are removed on success unless --keep-files is given.
"""

import argparse
import glob
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request

PORT = 8080
SCHEMA_DIR = pathlib.Path("doc/gltf_extensions/schema")
RESULTS = []

ALL_ERHE_EXTENSIONS = [
    "ERHE_brushes",
    "ERHE_camera",
    "ERHE_collections",
    "ERHE_geometry",
    "ERHE_layout",
    "ERHE_light",
    "ERHE_material",
    "ERHE_node",
    "ERHE_node_graphs",
    "ERHE_physics",
    "ERHE_scene",
]


# --------------------------------------------------------------------------
# MCP plumbing (same retry discipline as geometry_nodes_smoke_test.py)
# --------------------------------------------------------------------------

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


def call(tool, args=None, timeout=180, deadline_s=900):
    """Queries / idempotent tools: retry freely until the server drains."""
    deadline = time.time() + deadline_s
    while True:
        try:
            return call_once(tool, args, timeout=timeout)
        except RuntimeError as error:
            if not is_busy_error(error) or (time.time() > deadline):
                raise
            time.sleep(5.0)


def mutate(tool, args=None, deadline_s=900):
    """Mutations: issue ONCE; on server-side timeout poll until drained and
    treat the mutation as applied (re-issuing would double-execute)."""
    try:
        return call_once(tool, args)
    except RuntimeError as error:
        if not is_busy_error(error):
            raise
        print(f"  (server busy on {tool}; waiting for the main thread to drain)")
        deadline = time.time() + deadline_s
        while True:
            try:
                call_once("get_undo_redo_stack")
                return None  # mutation applied; result unavailable
            except RuntimeError as poll_error:
                if not is_busy_error(poll_error) or (time.time() > deadline):
                    raise
                time.sleep(5.0)


def check(section, name, condition, detail=""):
    RESULTS.append((section, name, bool(condition), detail))
    status = "PASS" if condition else "FAIL"
    print(f"[{status}] {section}: {name}" + (f" -- {detail}" if detail and not condition else ""))
    return bool(condition)


def guarded(section, name, block):
    """Run one build block; an exception fails that block's check but lets
    the remaining blocks run (a partial scene still exercises the rest)."""
    try:
        block()
    except Exception as error:
        check(section, name, False, repr(error))


def skip(section, name, reason):
    print(f"[SKIP] {section}: {name} -- {reason}")


# --------------------------------------------------------------------------
# GLB / schema helpers
# --------------------------------------------------------------------------

def read_glb_json(path):
    """Parse the JSON chunk of a .glb file."""
    blob = pathlib.Path(path).read_bytes()
    if blob[0:4] != b"glTF":
        raise RuntimeError(f"not a GLB file: {path}")
    offset = 12
    while offset + 8 <= len(blob):
        chunk_length = int.from_bytes(blob[offset:offset + 4], "little")
        chunk_type = blob[offset + 4:offset + 8]
        if chunk_type == b"JSON":
            return json.loads(blob[offset + 8:offset + 8 + chunk_length].decode("utf-8"))
        offset += 8 + chunk_length
    raise RuntimeError(f"no JSON chunk in GLB: {path}")


# Minimal JSON-schema (draft 2020-12 subset) validator covering exactly the
# keywords the ERHE_* schemas use. Unknown keywords are a validation error so
# schema growth cannot silently skip constraints - extend KNOWN_KEYWORDS (and
# the logic) when a schema legitimately adds one.
KNOWN_KEYWORDS = {
    "$schema", "$id", "$defs", "$ref", "title", "description",
    "type", "properties", "required", "enum", "items",
    "minItems", "maxItems", "minimum", "anyOf",
}

TYPE_CHECKS = {
    "object":  lambda v: isinstance(v, dict),
    "array":   lambda v: isinstance(v, list),
    "string":  lambda v: isinstance(v, str),
    "boolean": lambda v: isinstance(v, bool),
    "number":  lambda v: isinstance(v, (int, float)) and not isinstance(v, bool),
    "integer": lambda v: (isinstance(v, int) and not isinstance(v, bool))
                         or (isinstance(v, float) and float(v).is_integer()),
}


def schema_validate(instance, schema, root_schema, path, errors):
    if "$ref" in schema:
        ref = schema["$ref"]
        prefix = "#/$defs/"
        target = root_schema.get("$defs", {}).get(ref[len(prefix):]) if ref.startswith(prefix) else None
        if target is None:
            errors.append(f"{path}: unresolvable $ref '{ref}'")
        else:
            schema_validate(instance, target, root_schema, path, errors)
        return

    unknown = set(schema) - KNOWN_KEYWORDS
    if unknown:
        errors.append(f"{path}: schema uses unsupported keywords {sorted(unknown)} (extend the validator)")

    expected_type = schema.get("type")
    if expected_type is not None:
        type_check = TYPE_CHECKS.get(expected_type)
        if type_check is None:
            errors.append(f"{path}: schema has unknown type '{expected_type}'")
            return
        if not type_check(instance):
            errors.append(f"{path}: expected {expected_type}, got {type(instance).__name__} ({str(instance)[:60]})")
            return

    if "enum" in schema and instance not in schema["enum"]:
        errors.append(f"{path}: value {instance!r} not in enum {schema['enum']}")

    if "minimum" in schema and isinstance(instance, (int, float)) and not isinstance(instance, bool):
        if instance < schema["minimum"]:
            errors.append(f"{path}: value {instance} < minimum {schema['minimum']}")

    if isinstance(instance, dict):
        for key in schema.get("required", []):
            if key not in instance:
                errors.append(f"{path}: missing required property '{key}'")
        for key, sub_schema in schema.get("properties", {}).items():
            if key in instance:
                schema_validate(instance[key], sub_schema, root_schema, f"{path}.{key}", errors)

    if isinstance(instance, list):
        if "minItems" in schema and len(instance) < schema["minItems"]:
            errors.append(f"{path}: {len(instance)} items < minItems {schema['minItems']}")
        if "maxItems" in schema and len(instance) > schema["maxItems"]:
            errors.append(f"{path}: {len(instance)} items > maxItems {schema['maxItems']}")
        if "items" in schema:
            for index, element in enumerate(instance):
                schema_validate(element, schema["items"], root_schema, f"{path}[{index}]", errors)

    if "anyOf" in schema:
        all_branch_errors = []
        for branch in schema["anyOf"]:
            branch_errors = []
            schema_validate(instance, branch, root_schema, path, branch_errors)
            if not branch_errors:
                break
            all_branch_errors.append(branch_errors)
        else:
            errors.append(f"{path}: no anyOf branch matched: {all_branch_errors}")


def collect_erhe_payloads(doc):
    """Yield (extension_name, json_path, payload) for every ERHE_* extension
    object in the document, from every attachment site the writers use."""
    found = []

    def take(obj, where):
        for name, payload in obj.get("extensions", {}).items():
            if name.startswith("ERHE_"):
                found.append((name, where, payload))

    take(doc, "asset-root")
    for index, scene in enumerate(doc.get("scenes", [])):
        take(scene, f"scenes[{index}]")
    for index, node in enumerate(doc.get("nodes", [])):
        take(node, f"nodes[{index}]")
    for index, camera in enumerate(doc.get("cameras", [])):
        take(camera, f"cameras[{index}]")
    for index, material in enumerate(doc.get("materials", [])):
        take(material, f"materials[{index}]")
    for index, mesh in enumerate(doc.get("meshes", [])):
        take(mesh, f"meshes[{index}]")
        for prim_index, primitive in enumerate(mesh.get("primitives", [])):
            take(primitive, f"meshes[{index}].primitives[{prim_index}]")
    return found


def validate_erhe_extensions(section, glb_path, expect_all_extensions):
    """Schema-validate every ERHE_* payload in the GLB; check the
    extensionsUsed / extensionsRequired conventions; optionally assert
    full 11-extension coverage."""
    doc = read_glb_json(glb_path)
    payloads = collect_erhe_payloads(doc)
    check(section, f"{glb_path.name}: ERHE_* payloads found", len(payloads) > 0, "no ERHE_* extensions in file")

    schemas = {}
    error_count = 0
    per_extension_counts = {}
    for name, where, payload in payloads:
        per_extension_counts[name] = per_extension_counts.get(name, 0) + 1
        if name not in schemas:
            schema_path = SCHEMA_DIR / f"{name}.schema.json"
            schemas[name] = json.loads(schema_path.read_text(encoding="utf-8")) if schema_path.is_file() else None
        schema = schemas[name]
        if schema is None:
            check(section, f"schema exists for {name}", False, f"missing {SCHEMA_DIR / name}.schema.json")
            error_count += 1
            continue
        errors = []
        schema_validate(payload, schema, schema, where, errors)
        if errors:
            error_count += len(errors)
            for error in errors[:5]:
                print(f"       schema violation [{name}] {error}")
    counts = ", ".join(f"{k}:{v}" for k, v in sorted(per_extension_counts.items()))
    check(section, f"{glb_path.name}: all ERHE_* payloads validate against their schemas",
          error_count == 0, f"{error_count} violations ({counts})")
    print(f"       validated payloads: {counts}")

    extensions_used = doc.get("extensionsUsed", [])
    found_names = sorted(per_extension_counts.keys())
    check(section, f"{glb_path.name}: every present ERHE_* extension is declared in extensionsUsed",
          all(name in extensions_used for name in found_names),
          f"missing: {[n for n in found_names if n not in extensions_used]}")
    required = doc.get("extensionsRequired", [])
    check(section, f"{glb_path.name}: no ERHE_* extension in extensionsRequired",
          not any(name.startswith("ERHE_") for name in required), str(required))

    if expect_all_extensions:
        missing = [name for name in ALL_ERHE_EXTENSIONS if name not in per_extension_counts]
        check(section, f"{glb_path.name}: full ERHE_* coverage (all {len(ALL_ERHE_EXTENSIONS)} extensions present)",
              not missing, f"missing: {missing}")
    return doc


# --------------------------------------------------------------------------
# Scene snapshot + diff
# --------------------------------------------------------------------------

def wait_for_scene(scene_name, tries=100):
    """load_scene is queued; poll list_scenes until the scene appears."""
    for _ in range(tries):
        scenes = call("list_scenes").get("scenes", [])
        if any(s.get("name") == scene_name for s in scenes):
            return True
        time.sleep(0.2)
    return False


def wait_for_scene_node(scene_name, node_name, tries=100):
    for _ in range(tries):
        nodes = call("get_scene_nodes", {"scene_name": scene_name}).get("nodes", [])
        if any(n.get("name") == node_name for n in nodes):
            return True
        time.sleep(0.1)
    return False


def wait_for_node_attachment(scene_name, node_name, attachment_type, tries=100):
    for _ in range(tries):
        nodes = call("get_scene_nodes", {"scene_name": scene_name}).get("nodes", [])
        for node in nodes:
            if node.get("name") == node_name and attachment_type in node.get("attachment_types", []):
                return True
        time.sleep(0.1)
    return False


def round_vec(values, digits=4):
    return [round(float(v), digits) for v in values]


def norm_quat(values, digits=4):
    """q and -q are the same rotation: canonicalize the sign before rounding."""
    q = [float(v) for v in values]
    pivot = max(range(len(q)), key=lambda i: abs(q[i]))
    if q[pivot] < 0.0:
        q = [-v for v in q]
    return [round(v, digits) for v in q]


def norm_node(node, parent_name=None):
    return {
        "name":        node.get("name"),
        "parent":      parent_name if parent_name is not None else node.get("parent"),
        "position":    round_vec(node.get("position", [])),
        "rotation":    norm_quat(node.get("rotation_xyzw", [])),
        "scale":       round_vec(node.get("scale", [])),
        "tags":        sorted(node.get("tags", [])),
        # Brush_placement attachments are session-only (not persisted; see
        # doc/scene_serialization.md "What is not persisted").
        "attachments": sorted(a for a in node.get("attachment_types", []) if a != "Brush_placement"),
        "locked":      node.get("locked"),
    }


def normalize_import_roots(raw_nodes):
    """import_root wrapper nodes are transparent on export (their children
    are written in their place; doc/scene_serialization.md save pipeline
    step 1). Apply the same transparency to the pre-save snapshot: drop
    wrapper nodes and lift their children's parent."""

    def resolve_parent_name(node, by_id):
        parent_id = node.get("parent_id")
        parent_name = node.get("parent")
        while (parent_id is not None) and (parent_id in by_id) and by_id[parent_id].get("import_root"):
            wrapper = by_id[parent_id]
            parent_id = wrapper.get("parent_id")
            parent_name = wrapper.get("parent")
        return parent_name

    by_id = {n.get("id"): n for n in raw_nodes}
    out = []
    for node in raw_nodes:
        if node.get("import_root"):
            continue
        out.append(norm_node(node, parent_name=resolve_parent_name(node, by_id)))
    return out


def node_sort_key(record):
    return (record["name"], record["parent"], json.dumps(record["position"]))


NODE_PHYSICS_FIELDS = ["motion_mode", "friction", "restitution", "mass", "gravity_factor", "is_trigger", "collision_shape"]
# joint_settings is intentionally NOT compared: a settings-less (free
# six-dof) joint materializes a Physics_joint_settings item on reload.
NODE_JOINT_FIELDS = ["connected_node", "enable_collision"]


def norm_attachment_details(details):
    out = []
    for attachment in details.get("attachments", []):
        a_type = attachment.get("type")
        if a_type == "Brush_placement":
            continue  # session-only, not persisted (doc/scene_serialization.md)
        if a_type == "Node_physics":
            record = {k: attachment.get(k) for k in NODE_PHYSICS_FIELDS}
            if record.get("motion_mode") == "static":
                # A static body's mass is physically meaningless and not
                # representable in KHR_physics_rigid_bodies (no "motion"
                # object): the reported value is shape-derived noise.
                record.pop("mass", None)
        elif a_type == "Node_joint":
            record = {k: attachment.get(k) for k in NODE_JOINT_FIELDS}
        elif a_type == "Mesh":
            record = {"name": attachment.get("name")}
        else:
            record = {}
        for key, value in list(record.items()):
            if isinstance(value, float):
                record[key] = round(value, 4)
        record["type"] = a_type
        out.append(record)
    return sorted(out, key=lambda r: json.dumps(r, sort_keys=True))


def norm_animation(animation):
    return {
        "name":       animation.get("name"),
        "start_time": round(float(animation.get("start_time", 0.0)), 4),
        "end_time":   round(float(animation.get("end_time", 0.0)), 4),
        "channels":   sorted(
            (
                {
                    "target":        channel.get("target"),
                    "path":          channel.get("path"),
                    "interpolation": channel.get("interpolation"),
                    "keyframes":     channel.get("keyframes"),
                    "components":    channel.get("components"),
                }
                for channel in animation.get("channels", [])
            ),
            key=lambda c: json.dumps(c, sort_keys=True),
        ),
    }


def snapshot_scene(scene_name, material_names, detail_nodes):
    """Collect the diffable MCP-visible state of a scene."""
    snap = {}

    nodes = call("get_scene_nodes", {"scene_name": scene_name}).get("nodes", [])
    snap["nodes"] = sorted(normalize_import_roots(nodes), key=node_sort_key)

    materials = call("get_scene_materials", {"scene_name": scene_name}).get("materials", [])
    snap["materials"] = sorted(
        (
            {
                "name":       m.get("name"),
                "base_color": round_vec(m.get("base_color", [])),
                "metallic":   round(float(m.get("metallic", 0.0)), 4),
                "roughness":  round_vec(m.get("roughness", [])) if isinstance(m.get("roughness"), list) else round(float(m.get("roughness", 0.0)), 4),
                "emissive":   round_vec(m.get("emissive", [])),
            }
            for m in materials
            if m.get("name") in material_names
        ),
        key=lambda m: m["name"],
    )

    animations = call("get_scene_animations", {"scene_name": scene_name}).get("animations", [])
    snap["animations"] = sorted((norm_animation(a) for a in animations), key=lambda a: a["name"])

    brushes = call("get_scene_brushes", {"scene_name": scene_name}).get("brushes", [])
    snap["brushes"] = sorted(
        (
            {
                "name":         b.get("name"),
                "folder_path":  b.get("folder_path"),
                "facet_count":  b.get("facet_count"),
                "vertex_count": b.get("vertex_count"),
            }
            for b in brushes
        ),
        key=lambda b: json.dumps(b, sort_keys=True),
    )

    graph_meshes = call("get_graph_meshes", {"scene_name": scene_name}).get("graph_meshes", [])
    snap["graph_meshes"] = sorted(
        ({"name": g.get("name"), "has_bake": g.get("has_bake")} for g in graph_meshes),
        key=lambda g: g["name"],
    )
    graph_textures = call("get_graph_textures", {"scene_name": scene_name}).get("graph_textures", [])
    snap["graph_textures"] = sorted(
        ({"name": g.get("name"), "has_output": g.get("has_output")} for g in graph_textures),
        key=lambda g: g["name"],
    )

    snap["node_details"] = {}
    for node_name in detail_nodes:
        try:
            details = call("get_node_details", {"scene_name": scene_name, "node_name": node_name})
        except RuntimeError as error:
            snap["node_details"][node_name] = f"lookup failed: {error}"
            continue
        if isinstance(details, dict):
            snap["node_details"][node_name] = norm_attachment_details(details)
    return snap


def diff_json(a, b, path, mismatches, limit=40):
    if len(mismatches) >= limit:
        return
    if type(a) is not type(b):
        mismatches.append(f"{path}: type {type(a).__name__} != {type(b).__name__}")
        return
    if isinstance(a, dict):
        for key in sorted(set(a) | set(b)):
            if key not in a:
                mismatches.append(f"{path}.{key}: only in loaded")
            elif key not in b:
                mismatches.append(f"{path}.{key}: only in original")
            else:
                diff_json(a[key], b[key], f"{path}.{key}", mismatches, limit)
    elif isinstance(a, list):
        if len(a) != len(b):
            mismatches.append(f"{path}: length {len(a)} != {len(b)}")
            preview_a = [e.get("name") if isinstance(e, dict) else e for e in a]
            preview_b = [e.get("name") if isinstance(e, dict) else e for e in b]
            only_a = [x for x in preview_a if x not in preview_b]
            only_b = [x for x in preview_b if x not in preview_a]
            if only_a or only_b:
                mismatches.append(f"{path}: only-original={only_a[:6]} only-loaded={only_b[:6]}")
            return
        for index, (ea, eb) in enumerate(zip(a, b)):
            label = ea.get("name", index) if isinstance(ea, dict) else index
            diff_json(ea, eb, f"{path}[{label}]", mismatches, limit)
    else:
        if a != b:
            mismatches.append(f"{path}: {a!r} != {b!r}")


# --------------------------------------------------------------------------
# Screenshots
# --------------------------------------------------------------------------

def screenshot(path):
    call("capture_screenshot", {"path": str(path)})
    return pathlib.Path(path)


def png_statistics(path):
    """Return (stddev, mean) of the grayscale image, or None without PIL."""
    try:
        from PIL import Image, ImageStat
    except ImportError:
        return None
    with Image.open(path) as image:
        stat = ImageStat.Stat(image.convert("L"))
        return (stat.stddev[0], stat.mean[0])


def compare_screenshots(section, before_path, after_path):
    for label, path in (("before-save", before_path), ("after-load", after_path)):
        exists = pathlib.Path(path).is_file() and pathlib.Path(path).stat().st_size > 1000
        check(section, f"screenshot {label} written ({pathlib.Path(path).name})", exists)
        if not exists:
            return
    stats_before = png_statistics(before_path)
    if stats_before is None:
        skip(section, "screenshot content checks", "PIL not installed")
        return
    stats_after = png_statistics(after_path)
    check(section, "before-save screenshot shows content (not blank)", stats_before[0] > 2.0, f"stddev={stats_before[0]:.2f}")
    check(section, "after-load screenshot shows content (not blank)", stats_after[0] > 2.0, f"stddev={stats_after[0]:.2f}")
    try:
        from PIL import Image, ImageChops, ImageStat
        with Image.open(before_path) as image_a, Image.open(after_path) as image_b:
            small_a = image_a.convert("L").resize((64, 64))
            small_b = image_b.convert("L").resize((64, 64))
            diff_mean = ImageStat.Stat(ImageChops.difference(small_a, small_b)).mean[0]
        # Informational: the loaded scene opens its own viewport, so dock
        # layout differences make a pixel-exact comparison meaningless.
        print(f"       screenshot mean |before - after| (64x64 gray) = {diff_mean:.2f}")
    except ImportError:
        pass


# --------------------------------------------------------------------------
# Section 1-3: end-to-end round-trip
# --------------------------------------------------------------------------

E2E_GLB = pathlib.Path("res/editor/scenes/phase6_roundtrip.glb")
FOREIGN_GLB = pathlib.Path("res/editor/scenes/phase6_foreign.glb")
PREFAB_RESAVE_GLB = pathlib.Path("res/editor/scenes/phase6_prefab_resave.glb")
# .glb: text .gltf saves do not write their external buffer payload (a
# pre-existing exporter limitation unrelated to R6); read_glb_json gives the
# same JSON view.
R6_GLTF = pathlib.Path("res/editor/scenes/phase6_r6_reference.glb")
R6_RESAVE_GLTF = pathlib.Path("res/editor/scenes/phase6_r6_reference_resave.glb")
DECCER_GLB = "res/editor/assets/SM_Deccer_Cubes_Textured.glb"
RIGGED_GLB = "res/editor/assets/RiggedFigure/RiggedFigure.glb"

E2E_STATE = {}


def create_fresh_scene(section):
    before = {s["name"] for s in call("list_scenes").get("scenes", [])}
    mutate("create_scene")
    for _ in range(100):
        names = {s["name"] for s in call("list_scenes").get("scenes", [])}
        added = names - before
        if added:
            return sorted(added)[0]
        time.sleep(0.1)
    check(section, "create_scene produced a new scene", False)
    return None


def section_build_scene():
    S = "build-scene"
    scene = create_fresh_scene(S)
    if scene is None:
        return
    E2E_STATE["scene"] = scene
    print(f"  (scene: {scene})")
    node_ids = {}

    def block_shapes():
        # Shapes (geometry-normative meshes). "P6 Brush" doubles as the brush
        # library source; the physics pair keeps unique names so name-keyed
        # detail lookups stay unambiguous.
        box = mutate("create_shape", {
            "scene_name": scene, "shape": "box", "name": "P6 Box",
            "position": [0.0, 0.5, 0.0], "motion_mode": "dynamic",
        })
        check(S, "create_shape box", bool(box) and box.get("node_id") is not None, str(box))
        E2E_STATE["material"] = box.get("material") if box else None
        node_ids["P6 Box"] = box.get("node_id")
        sphere = mutate("create_shape", {
            "scene_name": scene, "shape": "uv_sphere", "name": "P6 Sphere",
            "position": [2.0, 0.5, 0.0], "motion_mode": "dynamic",
        })
        check(S, "create_shape uv_sphere", bool(sphere) and sphere.get("node_id") is not None, str(sphere))
        node_ids["P6 Sphere"] = sphere.get("node_id")
        torus = mutate("create_shape", {
            "scene_name": scene, "shape": "torus", "name": "P6 Torus",
            "position": [4.0, 0.5, 0.0], "motion_mode": "static",
        })
        check(S, "create_shape torus (static)", bool(torus) and torus.get("node_id") is not None, str(torus))
        node_ids["P6 Torus"] = torus.get("node_id")
        brush_src = mutate("create_shape", {
            "scene_name": scene, "shape": "box", "name": "P6 Brush", "add_brush": True,
            "position": [0.0, 0.5, -2.0], "motion_mode": "static", "size": [1.0, 0.5, 1.0],
        })
        check(S, "create_shape with add_brush", bool(brush_src) and brush_src.get("brush_id") is not None, str(brush_src))
        E2E_STATE["brush_id"] = brush_src.get("brush_id")

    def block_imports():
        # Imports: textured triangle soup (embedded images) + skinned/animated.
        imported = mutate("import_gltf", {"scene_name": scene, "path": DECCER_GLB})
        check(S, "import_gltf textured asset", bool(imported) and imported.get("imported"), str(imported))
        imported = mutate("import_gltf", {"scene_name": scene, "path": RIGGED_GLB})
        check(S, "import_gltf skinned+animated asset", bool(imported) and imported.get("imported"), str(imported))

    def block_physics():
        # Physics: tune one body's serialized fields, join the two bodies.
        edited = mutate("edit_physics_body", {
            "scene_name": scene, "node_name": "P6 Box",
            "friction": 0.4, "restitution": 0.25, "linear_damping": 0.05, "angular_damping": 0.07,
        })
        check(S, "edit_physics_body (ERHE_physics fields)", bool(edited) and "friction" in edited.get("applied", []), str(edited))
        joint = mutate("create_physics_joint", {
            "scene_name": scene, "node_name": "P6 Sphere", "connected_node_name": "P6 Box",
        })
        check(S, "create_physics_joint", bool(joint) and joint.get("created"), str(joint))

    def block_brush_placement():
        placed = mutate("place_brush", {
            "scene_name": scene, "brush_id": E2E_STATE["brush_id"],
            "position": [-2.0, 0.5, 0.0], "motion_mode": "static",
        })
        check(S, "place_brush", bool(placed) and placed.get("node_id") is not None, str(placed))

    def block_layout():
        # Layout on a node; layout_item on a child of that node (ERHE_layout).
        layout = mutate("add_node_attachment", {"scene_name": scene, "node_name": "P6 Torus", "type": "layout"})
        check(S, "add layout attachment", bool(layout) and layout.get("added"), str(layout))
        mutate("create_node", {
            "scene_name": scene, "name": "P6 Layout Child",
            "parent_node_name": "P6 Torus", "position": [4.0, 1.0, 0.0],
        })
        check(S, "layout child node created", wait_for_scene_node(scene, "P6 Layout Child"))
        layout_item = mutate("add_node_attachment", {"scene_name": scene, "node_name": "P6 Layout Child", "type": "layout_item"})
        check(S, "add layout_item attachment", bool(layout_item) and layout_item.get("added"), str(layout_item))

    def block_tags_locks():
        # Tags (ERHE_collections) + a locked node (ERHE_node flags).
        tagged = mutate("add_tags", {
            "scene_name": scene, "ids": [node_ids["P6 Box"], node_ids["P6 Sphere"]],
            "tags": ["phase6", "roundtrip"],
        })
        check(S, "add_tags", bool(tagged) and tagged.get("tagged_count") == 2, str(tagged))
        locked = mutate("lock_items", {"scene_name": scene, "ids": [node_ids["P6 Torus"]]})
        check(S, "lock_items", bool(locked) and locked.get("locked_count") == 1, str(locked))

    def block_animation():
        # Authored animation keys on top of the imported animation.
        animations = call("get_scene_animations", {"scene_name": scene}).get("animations", [])
        check(S, "imported animation present", len(animations) == 1, str([a.get("name") for a in animations]))
        if not animations:
            return
        animation_name = animations[0]["name"]
        E2E_STATE["animation"] = animation_name
        targeted = mutate("set_animation_target", {"scene_name": scene, "animation": animation_name})
        check(S, "set_animation_target", bool(targeted) and targeted.get("target") == animation_name, str(targeted))
        keyed_a = mutate("animation_create_key", {"nodes": ["P6 Sphere"], "time": 0.0, "paths": ["translation", "rotation"]})
        keyed_b = mutate("animation_create_key", {"nodes": ["P6 Sphere"], "time": 0.5, "paths": ["translation", "rotation"]})
        check(S, "animation_create_key authored keys",
              bool(keyed_a) and keyed_a.get("channels_created") == 2 and bool(keyed_b) and keyed_b.get("keyed_nodes") == 1,
              f"{keyed_a} / {keyed_b}")

    def block_graph_texture():
        # Graph texture bound to a material slot (ERHE_node_graphs material binding).
        created = mutate("create_graph_texture", {"scene_name": scene, "name": "P6 Tex"})
        check(S, "create_graph_texture", bool(created) and created.get("created"), str(created))
        perlin = mutate("texture_graph_add_node", {"type": "perlin"})
        output = mutate("texture_graph_add_node", {"type": "output"})
        connected = mutate("texture_graph_connect", {
            "source_node_id": perlin["id"], "source_slot": 0,
            "sink_node_id": output["id"], "sink_slot": 0,
        })
        check(S, "texture graph perlin->output", bool(connected) and connected.get("connected"), str(connected))
        bound = mutate("set_material_texture_source", {
            "scene_name": scene, "material_name": E2E_STATE.get("material"),
            "graph_texture": "P6 Tex", "slot": "base_color",
        })
        check(S, "set_material_texture_source", bool(bound) and bound.get("bound"), str(bound))

    def block_material():
        # Material fields that serialize through ERHE_material.
        edited = mutate("edit_material", {
            "scene_name": scene, "material_name": E2E_STATE.get("material"),
            "roughness": [0.3, 0.6], "bxdf_model": "anisotropic_brdf",
        })
        check(S, "edit_material anisotropic fields", bool(edited) and edited.get("changed"), str(edited))

    def block_graph_mesh():
        # Graph mesh bound to a scene node (ERHE_node_graphs node binding).
        created = mutate("create_graph_mesh", {"scene_name": scene, "name": "P6 GM"})
        check(S, "create_graph_mesh", bool(created) and created.get("created"), str(created))
        gm_box = mutate("geometry_graph_add_node", {"type": "box"})
        gm_out = mutate("geometry_graph_add_node", {"type": "output"})
        connected = mutate("geometry_graph_connect", {
            "source_node_id": gm_box["id"], "source_slot": 0,
            "sink_node_id": gm_out["id"], "sink_slot": 0,
        })
        check(S, "geometry graph box->output", bool(connected) and connected.get("connected"), str(connected))
        call("get_geometry_graph")  # evaluation barrier
        mutate("create_node", {"scene_name": scene, "name": "P6 GM Node", "position": [0.0, 2.0, 0.0]})
        check(S, "graph mesh carrier node created", wait_for_scene_node(scene, "P6 GM Node"))
        bound = mutate("set_node_graph_mesh", {"scene_name": scene, "node_name": "P6 GM Node", "graph_mesh": "P6 GM"})
        check(S, "set_node_graph_mesh", bool(bound) and bound.get("bound"), str(bound))
        check(S, "graph mesh bake materialized on the carrier", wait_for_node_attachment(scene, "P6 GM Node", "Mesh"))

    def block_light():
        # A light (ERHE_light) - create_scene scenes start with a camera only.
        light = mutate("create_light", {
            "scene_name": scene, "name": "P6 Light", "type": "point",
            "position": [0.0, 4.0, 0.0], "color": [1.0, 0.9, 0.8], "intensity": 20.0, "cast_shadow": True,
        })
        check(S, "create_light", bool(light) and light.get("node_id") is not None, str(light))
        check(S, "light node landed", wait_for_scene_node(scene, "P6 Light"))

    guarded(S, "shapes block", block_shapes)
    guarded(S, "imports block", block_imports)
    guarded(S, "physics block", block_physics)
    guarded(S, "brush placement block", block_brush_placement)
    guarded(S, "layout block", block_layout)
    guarded(S, "tags/locks block", block_tags_locks)
    guarded(S, "animation block", block_animation)
    guarded(S, "graph texture block", block_graph_texture)
    guarded(S, "material block", block_material)
    guarded(S, "graph mesh block", block_graph_mesh)
    guarded(S, "light block", block_light)


def section_save_and_validate():
    S = "save-validate"
    scene = E2E_STATE.get("scene")
    if scene is None:
        check(S, "scene available", False, "build-scene failed")
        return

    screenshot("logs/phase6_before.png")

    # Foreign-tool variant FIRST: without a prefab instance the asset stays
    # plain glTF 2.0 (stock viewers reject 2.1 / externalAssets).
    saved = mutate("save_scene", {"scene_name": scene, "path": str(FOREIGN_GLB)})
    check(S, "save_scene (foreign-tool variant, pre-prefab)", bool(saved) and saved.get("saved"), str(saved))
    if FOREIGN_GLB.is_file():
        foreign_doc = read_glb_json(FOREIGN_GLB)
        check(S, "foreign-tool variant is plain glTF 2.0",
              foreign_doc.get("asset", {}).get("version") == "2.0", str(foreign_doc.get("asset")))

    # External-asset prefab instance (glTF 2.1 files/externalAssets on save).
    instantiated = mutate("instantiate_prefab", {"scene_name": scene, "path": DECCER_GLB, "position": [0.0, 0.0, -4.0]})
    check(S, "instantiate_prefab", bool(instantiated) and instantiated.get("instantiated"), str(instantiated))

    saved = mutate("save_scene", {"scene_name": scene, "path": str(E2E_GLB)})
    check(S, "save_scene", bool(saved) and saved.get("saved"), str(saved))
    if not E2E_GLB.is_file():
        check(S, "saved file exists", False, str(E2E_GLB))
        return

    doc = validate_erhe_extensions(S, E2E_GLB, expect_all_extensions=True)
    E2E_STATE["doc"] = doc

    check(S, "asset is glTF 2.1 (external assets present)", doc.get("asset", {}).get("version") == "2.1", str(doc.get("asset")))
    check(S, "files + externalAssets arrays present",
          len(doc.get("files", [])) == 1 and len(doc.get("externalAssets", [])) == 1,
          f"files={len(doc.get('files', []))} externalAssets={len(doc.get('externalAssets', []))}")
    check(S, "embedded images survived import->save", len(doc.get("images", [])) == 5, f"images={len(doc.get('images', []))}")
    check(S, "skin exported", len(doc.get("skins", [])) == 1, f"skins={len(doc.get('skins', []))}")
    animations = doc.get("animations", [])
    check(S, "animation exported with authored channels",
          len(animations) == 1 and len(animations[0].get("channels", [])) == 59,
          f"animations={len(animations)} channels={[len(a.get('channels', [])) for a in animations]}")
    check(S, "KHR physics extensions declared",
          all(name in doc.get("extensionsUsed", []) for name in ["KHR_physics_rigid_bodies", "KHR_implicit_shapes"]),
          str(doc.get("extensionsUsed")))


def section_definitions_full_data():
    """R5 data-loss tripwire (asset-manager plan, step R5.1): every asset
    the scene DEFINES must appear in the saved glTF with its full payload,
    never as a name-only stub. Guards the R5 definition-vs-reference
    classification cliff: a migration bug that classifies definitions as
    references would save stubs instead of data - silent data loss on the
    next load. Written pre-R5 (must be green before any R5 change lands);
    stays green through R5/R6 because reference STUBS may only ever be
    written for assets defined in OTHER containers, which this scene has
    none of."""
    S = "definitions-full-data"
    doc = E2E_STATE.get("doc")
    if doc is None or not E2E_GLB.is_file():
        check(S, "prerequisites available", False, "save-validate failed")
        return

    accessors = doc.get("accessors", [])

    def accessor_count(index):
        if not isinstance(index, int) or not (0 <= index < len(accessors)):
            return 0
        return accessors[index].get("count", 0)

    # Materials: every exported material entry is a full definition. A
    # reference stub (R6 wire format, plan D5) is a name-only material
    # carrying ERHE_asset_reference; a definition always writes its PBR
    # block.
    materials = doc.get("materials", [])
    stub_like = [
        m.get("name") for m in materials
        if ("ERHE_asset_reference" in m.get("extensions", {})) or ("pbrMetallicRoughness" not in m)
    ]
    check(S, "every exported material is a full definition (no reference stubs)",
          len(materials) > 0 and not stub_like, f"materials={len(materials)} stub-like={stub_like}")

    # The material edited in build-scene: authored parameter VALUES are in
    # the wire (roughness [0.3, 0.6] -> pbr roughnessFactor + ERHE_material
    # roughness_y; bxdf_model anisotropic_brdf).
    edited_name = E2E_STATE.get("material")
    edited = next((m for m in materials if m.get("name") == edited_name), None)
    edited_erhe = (edited or {}).get("extensions", {}).get("ERHE_material", {})
    edited_pbr = (edited or {}).get("pbrMetallicRoughness", {})
    check(S, "edited material carries its authored parameter values",
          (edited is not None)
          and (edited_erhe.get("bxdf_model") == "anisotropic_brdf")
          and (abs(float(edited_pbr.get("roughnessFactor", -1.0)) - 0.3) < 1e-4)
          and (abs(float(edited_erhe.get("roughness_y", -1.0)) - 0.6) < 1e-4),
          f"material={edited_name!r} pbr={edited_pbr} erhe={edited_erhe}")

    # Brushes: every ERHE_brushes entry points at an extra mesh whose
    # geometry is actually present (primitives with POSITION data).
    brush_payloads = [p for (name, _w, p) in collect_erhe_payloads(doc) if name == "ERHE_brushes"]
    check(S, "ERHE_brushes payload present", len(brush_payloads) == 1, f"found {len(brush_payloads)}")
    brush_entries = brush_payloads[0].get("brushes", []) if brush_payloads else []
    meshes = doc.get("meshes", [])
    geometry_less = []
    for entry in brush_entries:
        mesh_index = entry.get("mesh")
        mesh = meshes[mesh_index] if isinstance(mesh_index, int) and (0 <= mesh_index < len(meshes)) else {}
        primitives = mesh.get("primitives", [])
        position_counts = [accessor_count(p.get("attributes", {}).get("POSITION")) for p in primitives]
        if (not primitives) or (not all(count > 0 for count in position_counts)):
            geometry_less.append(entry.get("name"))
    check(S, "every brush definition carries real geometry (POSITION data on its mesh)",
          len(brush_entries) > 0 and not geometry_less,
          f"entries={len(brush_entries)} geometry-less={geometry_less}")
    check(S, "authored brush 'P6 Brush' is among the brush definitions",
          any(entry.get("name") == "P6 Brush" for entry in brush_entries),
          str([entry.get("name") for entry in brush_entries]))

    # Animations: full sampler data (input/output accessors with keys),
    # not name-only entries.
    animations = doc.get("animations", [])
    hollow = []
    for animation in animations:
        samplers = animation.get("samplers", [])
        channels = animation.get("channels", [])
        samplers_have_data = all(
            (accessor_count(sampler.get("input")) > 0) and (accessor_count(sampler.get("output")) > 0)
            for sampler in samplers
        )
        if (not channels) or (not samplers) or (not samplers_have_data):
            hollow.append(animation.get("name"))
    check(S, "every animation definition carries sampler data",
          len(animations) > 0 and not hollow, f"animations={len(animations)} hollow={hollow}")


def section_reload_and_diff():
    S = "reload-diff"
    scene = E2E_STATE.get("scene")
    doc = E2E_STATE.get("doc")
    if scene is None or doc is None or not E2E_GLB.is_file():
        check(S, "prerequisites available", False, "save-validate failed")
        return

    exported_materials = {m.get("name") for m in doc.get("materials", [])}
    detail_nodes = ["P6 Sphere", "P6 Torus", "P6 GM Node", "P6 Light"]
    original = snapshot_scene(scene, exported_materials, detail_nodes)

    queued = mutate("load_scene", {"path": str(E2E_GLB)})
    check(S, "load_scene queued", bool(queued) and queued.get("queued"), str(queued))
    loaded_scene = E2E_GLB.stem
    check(S, "loaded scene appears in list_scenes", wait_for_scene(loaded_scene))
    check(S, "graph mesh re-baked on load", wait_for_node_attachment(loaded_scene, "P6 GM Node", "Mesh", tries=300))

    loaded = snapshot_scene(loaded_scene, exported_materials, detail_nodes)
    for key in ["nodes", "materials", "animations", "brushes", "graph_meshes", "graph_textures", "node_details"]:
        mismatches = []
        diff_json(loaded[key], original[key], key, mismatches)
        check(S, f"round-trip diff: {key} identical", not mismatches, f"{len(mismatches)} mismatches")
        for mismatch in mismatches[:10]:
            print(f"       {mismatch}")

    screenshot("logs/phase6_after.png")
    compare_screenshots(S, "logs/phase6_before.png", "logs/phase6_after.png")


# --------------------------------------------------------------------------
# Section 3b: R6 asset references (ERHE_asset_reference wire format)
# --------------------------------------------------------------------------

def wait_for_scene_gone(scene_name, tries=100):
    for _ in range(tries):
        scenes = call("list_scenes").get("scenes", [])
        if not any(s.get("name") == scene_name for s in scenes):
            return True
        time.sleep(0.2)
    return False


def section_asset_references():
    """R6 wire format (asset-manager plan phase R6 / design D5): a scene
    using another container's material saves a name-only proxy carrying
    ERHE_asset_reference {file, uid} plus a glTF 2.1 files entry; loading
    resolves the proxy through the Asset_manager to THE container object
    (open-scene and loaded-container directions both); a missing container
    falls back to the stub while KEEPING the key across re-save, and
    resolves again once the container returns."""
    S = "asset-references"
    if not E2E_GLB.is_file():
        check(S, "container scene file available", False, "save-validate failed")
        return
    # reload-diff left the saved scene OPEN, path-bound to E2E_GLB - the
    # cross-scene source whose make_key produces durable file-scope keys.
    container_scene = E2E_GLB.stem
    material_name = E2E_STATE.get("material")
    materials = call("get_scene_materials", {"scene_name": container_scene}).get("materials", [])
    source = next((m for m in materials if m.get("name") == material_name), None)
    if not check(S, "container scene provides the source material", source is not None,
                 f"scene={container_scene} material={material_name!r}"):
        return
    source_id = source["id"]

    # Reference scene: a fresh scene whose only tie to the container is the
    # cross-scene material on one shape (register_mesh lists it as a
    # reference entry carrying a file-scope key).
    scene = create_fresh_scene(S)
    if scene is None:
        return
    created = mutate("create_shape", {
        "scene_name": scene, "shape": "box", "name": "R6 Box",
        "material_id": source_id, "motion_mode": "static",
    })
    check(S, "create_shape with the container material", created is None or bool(created), str(created))

    saved = mutate("save_scene", {"scene_name": scene, "path": str(R6_GLTF)})
    check(S, "save_scene", bool(saved) and saved.get("saved"), str(saved))
    if not R6_GLTF.is_file():
        check(S, "saved reference file exists", False, str(R6_GLTF))
        return
    doc = read_glb_json(R6_GLTF)
    proxy = next(
        (m for m in doc.get("materials", [])
         if (m.get("name") == material_name) and ("ERHE_asset_reference" in m.get("extensions", {}))),
        None,
    )
    check(S, "referenced material saved as an ERHE_asset_reference proxy", proxy is not None,
          str([(m.get("name"), sorted(m.get("extensions", {}))) for m in doc.get("materials", [])]))
    reference = (proxy or {}).get("extensions", {}).get("ERHE_asset_reference", {})
    uid = reference.get("uid")
    if proxy is not None:
        # The serializer writes an empty pbrMetallicRoughness object for a
        # default material - equivalent to absent; only real data fails.
        check(S, "proxy is a name-only stub (no PBR data, no ERHE_material, no uid property)",
              (not proxy.get("pbrMetallicRoughness")) and ("ERHE_material" not in proxy.get("extensions", {})) and ("uid" not in proxy),
              str(proxy))
        file_index = reference.get("file")
        files = doc.get("files", [])
        file_ok = isinstance(file_index, int) and (0 <= file_index < len(files))
        check(S, "proxy file index resolves to a files entry", file_ok, str(reference))
        if file_ok:
            uri = files[file_index].get("uri", "")
            check(S, "files entry names the container", pathlib.Path(uri).name == E2E_GLB.name, uri)
        check(S, "proxy carries the target uid", isinstance(uid, str) and bool(uid), str(reference))
        check(S, "asset declares glTF 2.1 (files array present)",
              doc.get("asset", {}).get("version") == "2.1", str(doc.get("asset")))
        check(S, "ERHE_asset_reference declared in extensionsUsed",
              "ERHE_asset_reference" in doc.get("extensionsUsed", []), str(doc.get("extensionsUsed")))

    # Resolve leg, open-scene direction: reload the reference scene while the
    # container is OPEN as a scene - the proxy resolves to THE scene's object.
    mutate("close_scene", {"scene_name": scene})
    check(S, "reference scene closed", wait_for_scene_gone(scene))
    mutate("load_scene", {"path": str(R6_GLTF)})
    loaded_scene = R6_GLTF.stem
    check(S, "reference scene reloaded", wait_for_scene(loaded_scene))
    loaded_materials = call("get_scene_materials", {"scene_name": loaded_scene}).get("materials", [])
    loaded = next((m for m in loaded_materials if m.get("name") == material_name), None)
    check(S, "proxy resolved to THE open container scene's object (same item id)",
          (loaded is not None) and (loaded.get("id") == source_id),
          f"source id={source_id} loaded={loaded}")

    # Missing-container negative: hide the container file - the stub is kept
    # and the key survives a re-save (never silently dropped).
    mutate("close_scene", {"scene_name": loaded_scene})
    check(S, "loaded reference scene closed", wait_for_scene_gone(loaded_scene))
    mutate("clear_undo_history")  # undo entries pin assets and would refuse the courtesy unload
    mutate("close_scene", {"scene_name": container_scene})
    check(S, "container scene closed", wait_for_scene_gone(container_scene))
    hidden = E2E_GLB.with_suffix(".hidden")
    E2E_GLB.rename(hidden)
    try:
        mutate("load_scene", {"path": str(R6_GLTF)})
        check(S, "reference scene loads with the container missing", wait_for_scene(loaded_scene))
        stub_materials = call("get_scene_materials", {"scene_name": loaded_scene}).get("materials", [])
        stub = next((m for m in stub_materials if m.get("name") == material_name), None)
        check(S, "stub fallback material present (a new object, not the container's)",
              (stub is not None) and (stub.get("id") != source_id), f"source id={source_id} stub={stub}")
        saved = mutate("save_scene", {"scene_name": loaded_scene, "path": str(R6_RESAVE_GLTF)})
        check(S, "re-save with the container missing", bool(saved) and saved.get("saved"), str(saved))
        re_doc = read_glb_json(R6_RESAVE_GLTF) if R6_RESAVE_GLTF.is_file() else {}
        re_proxy = next(
            (m for m in re_doc.get("materials", [])
             if (m.get("name") == material_name) and ("ERHE_asset_reference" in m.get("extensions", {}))),
            None,
        )
        re_reference = (re_proxy or {}).get("extensions", {}).get("ERHE_asset_reference", {})
        check(S, "asset key survives the re-save (proxy + uid intact)",
              (re_proxy is not None) and (re_reference.get("uid") == uid),
              f"expected uid={uid!r} got={re_reference}")
        mutate("close_scene", {"scene_name": loaded_scene})
        check(S, "stub scene closed", wait_for_scene_gone(loaded_scene))
    finally:
        hidden.rename(E2E_GLB)

    # Restore leg, loaded-container direction: with the container back (and
    # no scene open on that path), the re-saved file resolves by LOADING the
    # container; a direct acquire of the same key returns the same object.
    mutate("load_scene", {"path": str(R6_RESAVE_GLTF)})
    resaved_scene = R6_RESAVE_GLTF.stem
    check(S, "re-saved reference scene loads after restore", wait_for_scene(resaved_scene))
    resolved_materials = call("get_scene_materials", {"scene_name": resaved_scene}).get("materials", [])
    resolved = next((m for m in resolved_materials if m.get("name") == material_name), None)
    hold = None
    if (resolved is not None) and uid:
        hold = call("acquire_asset", {
            "hold_name": "r6-verify", "scope": "file", "type": "material",
            "path": str(E2E_GLB), "uid": uid,
        })
    check(S, "proxy resolves against the loaded container (same object as a direct acquire)",
          (resolved is not None) and (hold is not None) and (hold.get("item_id") == resolved.get("id")),
          f"resolved={resolved} hold={hold}")
    if hold is not None:
        call("release_asset", {"hold_name": "r6-verify"})
    mutate("close_scene", {"scene_name": resaved_scene})
    check(S, "restored reference scene closed", wait_for_scene_gone(resaved_scene))


# --------------------------------------------------------------------------
# Section 4: prefab scene round-trip (user scene, skipped when absent)
# --------------------------------------------------------------------------

def section_prefab_scene():
    S = "prefab-scene"
    source = pathlib.Path("res/editor/scenes/Prefab test.glb")
    if not source.is_file():
        skip(S, "prefab scene round-trip", f"{source} not present (untracked user scene)")
        return

    queued = mutate("load_scene", {"path": str(source)})
    check(S, "load_scene queued", bool(queued) and queued.get("queued"), str(queued))
    scene = source.stem
    check(S, "prefab scene appears in list_scenes", wait_for_scene(scene))

    source_doc = read_glb_json(source)
    exported_materials = {m.get("name") for m in source_doc.get("materials", [])}
    original = snapshot_scene(scene, exported_materials, [])

    saved = mutate("save_scene", {"scene_name": scene, "path": str(PREFAB_RESAVE_GLB)})
    check(S, "save_scene (resave)", bool(saved) and saved.get("saved"), str(saved))
    if not PREFAB_RESAVE_GLB.is_file():
        return
    resaved_doc = validate_erhe_extensions(S, PREFAB_RESAVE_GLB, expect_all_extensions=False)

    def external_asset_summary(doc):
        files = [f.get("uri") for f in doc.get("files", [])]
        assets = [(a.get("name"), a.get("file")) for a in doc.get("externalAssets", [])]
        return {"files": files, "externalAssets": assets}

    check(S, "files / externalAssets preserved on resave",
          external_asset_summary(source_doc) == external_asset_summary(resaved_doc),
          f"{external_asset_summary(source_doc)} != {external_asset_summary(resaved_doc)}")

    queued = mutate("load_scene", {"path": str(PREFAB_RESAVE_GLB)})
    check(S, "resaved scene load queued", bool(queued) and queued.get("queued"), str(queued))
    reloaded_scene = PREFAB_RESAVE_GLB.stem
    check(S, "resaved scene appears in list_scenes", wait_for_scene(reloaded_scene))

    reloaded = snapshot_scene(reloaded_scene, exported_materials, [])
    for key in ["nodes", "materials", "brushes"]:
        mismatches = []
        diff_json(reloaded[key], original[key], key, mismatches)
        check(S, f"prefab round-trip diff: {key} identical", not mismatches, f"{len(mismatches)} mismatches")
        for mismatch in mismatches[:10]:
            print(f"       {mismatch}")


# --------------------------------------------------------------------------
# Section 5: foreign tools (Khronos validator, Blender)
# --------------------------------------------------------------------------

def find_gltf_validator(explicit):
    for candidate in [explicit, os.environ.get("ERHE_GLTF_VALIDATOR"), shutil.which("gltf_validator")]:
        if candidate and pathlib.Path(candidate).is_file():
            return candidate
    return None


def find_blender(explicit):
    candidates = [explicit, os.environ.get("ERHE_BLENDER"), shutil.which("blender")]
    program_files = os.environ.get("ProgramFiles", r"C:\Program Files")
    candidates += sorted(glob.glob(os.path.join(program_files, "Blender Foundation", "*", "blender.exe")), reverse=True)
    for candidate in candidates:
        if candidate and pathlib.Path(candidate).is_file():
            return candidate
    return None


def run_khronos_validator(section, validator, glb_path):
    with tempfile.TemporaryDirectory() as tmp:
        result = subprocess.run(
            [validator, "-o", "-a", str(glb_path.resolve())],
            capture_output=True, text=True, timeout=600, cwd=tmp,
        )
        report = None
        try:
            report = json.loads(result.stdout)
        except json.JSONDecodeError:
            report_file = pathlib.Path(tmp) / (glb_path.name + ".report.json")
            if report_file.is_file():
                report = json.loads(report_file.read_text(encoding="utf-8"))
    if report is None:
        check(section, f"validator produced a report for {glb_path.name}", False,
              f"exit={result.returncode} stdout={result.stdout[:200]} stderr={result.stderr[:200]}")
        return
    issues = report.get("issues", {})
    errors = issues.get("numErrors", -1)
    warnings = issues.get("numWarnings", -1)
    check(section, f"Khronos validator: {glb_path.name} has 0 errors", errors == 0,
          f"errors={errors} warnings={warnings} messages={[m.get('code') for m in issues.get('messages', [])[:8]]}")
    print(f"       validator: errors={errors} warnings={warnings} infos={issues.get('numInfos')} hints={issues.get('numHints')}")


BLENDER_RENDER_SCRIPT = r"""
import sys
import bpy
import mathutils

argv = sys.argv[sys.argv.index("--") + 1:]
glb_path, png_path = argv

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=glb_path)
mesh_objects = [o for o in bpy.data.objects if o.type == "MESH"]
print("BLENDER_IMPORT_OK objects=%d meshes=%d" % (len(bpy.data.objects), len(mesh_objects)))
assert mesh_objects, "no mesh objects imported"

corners = []
for obj in mesh_objects:
    for corner in obj.bound_box:
        corners.append(obj.matrix_world @ mathutils.Vector(corner))
minimum = mathutils.Vector((min(c[i] for c in corners) for i in range(3)))
maximum = mathutils.Vector((max(c[i] for c in corners) for i in range(3)))
center = (minimum + maximum) * 0.5
diagonal = max((maximum - minimum).length, 1.0)

camera_data = bpy.data.cameras.new("VerifyCamera")
camera = bpy.data.objects.new("VerifyCamera", camera_data)
bpy.context.scene.collection.objects.link(camera)
camera.location = center + mathutils.Vector((1.0, -1.0, 0.6)).normalized() * diagonal * 1.6
direction = center - camera.location
camera.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
camera_data.clip_end = diagonal * 10.0
bpy.context.scene.camera = camera

scene = bpy.context.scene
scene.render.engine = "BLENDER_WORKBENCH"
scene.render.resolution_x = 640
scene.render.resolution_y = 480
scene.render.filepath = png_path
bpy.ops.render.render(write_still=True)
print("BLENDER_RENDER_OK")
"""


def run_blender_check(section, blender, glb_path):
    render_png = pathlib.Path("logs/phase6_blender_render.png").resolve()
    with tempfile.TemporaryDirectory() as tmp:
        script_path = pathlib.Path(tmp) / "blender_render.py"
        script_path.write_text(BLENDER_RENDER_SCRIPT, encoding="utf-8")
        result = subprocess.run(
            [blender, "--background", "--factory-startup", "--python", str(script_path),
             "--", str(glb_path.resolve()), str(render_png)],
            capture_output=True, text=True, timeout=600,
        )
    imported = "BLENDER_IMPORT_OK" in result.stdout
    rendered = "BLENDER_RENDER_OK" in result.stdout and render_png.is_file()
    check(section, f"Blender imports {glb_path.name} (stock glTF importer)", imported,
          f"exit={result.returncode} tail={result.stdout[-300:]} err={result.stderr[-200:]}")
    check(section, "Blender renders the imported scene", rendered, f"exit={result.returncode}")
    if rendered:
        stats = png_statistics(render_png)
        if stats is not None:
            check(section, "Blender render shows content (not blank)", stats[0] > 2.0, f"stddev={stats[0]:.2f}")
        print(f"       render: {render_png}")


def section_foreign_tools(gltf_validator_arg, blender_arg):
    S = "foreign-tools"
    if not FOREIGN_GLB.is_file():
        check(S, "saved scene available", False, "save-validate failed")
        return

    validator = find_gltf_validator(gltf_validator_arg)
    if validator is None:
        skip(S, "Khronos glTF validator", "not found (pass --gltf-validator or set ERHE_GLTF_VALIDATOR; "
             "download from github.com/KhronosGroup/glTF-Validator releases)")
    else:
        run_khronos_validator(S, validator, FOREIGN_GLB)
        if E2E_GLB.is_file():
            # The full scene is glTF 2.1 (externalAssets): version and
            # files/externalAssets warnings are expected, errors are not.
            run_khronos_validator(S, validator, E2E_GLB)

    # Blender's stock importer requires glTF 2.0: run it on the
    # prefab-free variant.
    blender = find_blender(blender_arg)
    if blender is None:
        skip(S, "Blender import+render", "not found (pass --blender or set ERHE_BLENDER)")
    else:
        run_blender_check(S, blender, FOREIGN_GLB)


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=8080, help="MCP server port (default 8080)")
    parser.add_argument("--gltf-validator", help="path to the Khronos gltf_validator executable")
    parser.add_argument("--blender", help="path to the Blender executable")
    parser.add_argument("--keep-files", action="store_true", help="keep the saved .glb test artifacts")
    arguments = parser.parse_args()

    global PORT
    PORT = arguments.port

    sections = [
        section_build_scene,
        section_save_and_validate,
        section_definitions_full_data,
        section_reload_and_diff,
        section_asset_references,
        section_prefab_scene,
        lambda: section_foreign_tools(arguments.gltf_validator, arguments.blender),
    ]
    for section in sections:
        name = getattr(section, "__name__", "section_foreign_tools")
        print(f"\n=== {name} ===")
        try:
            section()
        except Exception as error:  # keep sweeping; report the wreck
            check(name, "section completed without exception", False, repr(error))

    print("\n===== SUMMARY =====")
    failed = [r for r in RESULTS if not r[2]]
    print(f"total={len(RESULTS)} pass={len(RESULTS) - len(failed)} fail={len(failed)}")
    for section, name, _, detail in failed:
        print(f"  FAIL {section}: {name} -- {detail}")

    if not failed and not arguments.keep_files:
        for artifact in [E2E_GLB, FOREIGN_GLB, PREFAB_RESAVE_GLB, R6_GLTF, R6_RESAVE_GLTF]:
            artifact.unlink(missing_ok=True)
    elif arguments.keep_files:
        print(f"(kept {E2E_GLB}, {FOREIGN_GLB}, {PREFAB_RESAVE_GLB}, {R6_GLTF} and {R6_RESAVE_GLTF})")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
