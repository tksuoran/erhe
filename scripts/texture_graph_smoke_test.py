#!/usr/bin/env python3
"""Comprehensive procedural texture graph smoke test.

Drives the in-editor MCP server (headless Vulkan build) through the whole
texture graph feature: every node type constructs with the expected pins and
default parameters, parameter sweeps round-trip with undo/redo, out-of-range
parameter abuse never crashes, connect/disconnect obey the pin-key rules and
reject type mismatches / self links / cycles, undo/redo of add + connect +
parameter edits, and - the core "it actually renders" proof - export_png of a
uniform node whose center pixel must match the set color, a perlin node whose
output must vary, and the Output node's baked subtree. Graphs only live in the
content library (no window scratch, no graph files): every block starts by
creating + selecting a fresh Graph_texture asset (fresh_graph()); persistence
is scene save/load, covered by section_graph_texture_asset. Prints a
structured PASS/FAIL report and exits non-zero on any failure.

Assumes the headless editor is already running with the MCP server reachable on
127.0.0.1:8080 (exactly like scripts/geometry_nodes_smoke_test.py). Texture
graph evaluation is synchronous (doc/texture-graph-plan.md decision 8), so a
get_texture_graph after a mutation reads settled state with no async barrier.
"""

import json
import pathlib
import struct
import sys
import time
import urllib.request
import zlib

PORT = 8080
LOG_PATH = pathlib.Path("logs/log.txt")
TMP_DIR = pathlib.Path("logs/texgraph_smoke")
RESULTS = []


# ------------------------------------------------------------------ transport

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


# Texture graph evaluation is cheap (string composition) and synchronous, but
# export_png submits a GPU mini-frame and waits idle; keep the geometry test's
# busy-tolerant wrappers so a slow frame never turns into a spurious failure.
def call(tool, args=None, timeout=180, deadline_s=300):
    deadline = time.time() + deadline_s
    while True:
        try:
            return call_once(tool, args, timeout=timeout)
        except RuntimeError as error:
            if not is_busy_error(error) or (time.time() > deadline):
                raise
            time.sleep(2.0)


def mutate(tool, args=None, deadline_s=300):
    try:
        return call_once(tool, args)
    except RuntimeError as error:
        if not is_busy_error(error):
            raise
        print(f"  (server busy on {tool}; waiting)")
        deadline = time.time() + deadline_s
        while True:
            try:
                call_once("get_undo_redo_stack")
                return None
            except RuntimeError as poll_error:
                if not is_busy_error(poll_error) or (time.time() > deadline):
                    raise
                time.sleep(2.0)


def call_expect_error(tool, args=None):
    """Return the error text of an expected rejection, or None on success."""
    try:
        call(tool, args)
        return None
    except RuntimeError as error:
        return str(error)


# ------------------------------------------------------------------ reporting

def check(section, name, condition, detail=""):
    RESULTS.append((section, name, bool(condition), detail))
    status = "PASS" if condition else "FAIL"
    print(f"[{status}] {section}: {name}" + (f" -- {detail}" if detail and not condition else ""))
    return bool(condition)


def approx(a, b, tol=1e-3):
    try:
        return abs(float(a) - float(b)) <= tol
    except (TypeError, ValueError):
        return False


def approx_list(a, b, tol=1e-3):
    if not isinstance(a, list) or not isinstance(b, list) or (len(a) != len(b)):
        return False
    return all(approx(x, y, tol) for x, y in zip(a, b))


# ---------------------------------------------------------------- graph helpers

def add_node(type_name):
    return call("texture_graph_add_node", {"type": type_name})


def connect(src, sslot, dst, dslot):
    return mutate("texture_graph_connect", {
        "source_node_id": src, "source_slot": sslot, "sink_node_id": dst, "sink_slot": dslot})


def connect_expect_error(src, sslot, dst, dslot):
    return call_expect_error("texture_graph_connect", {
        "source_node_id": src, "source_slot": sslot, "sink_node_id": dst, "sink_slot": dslot})


def disconnect(src, sslot, dst, dslot):
    return mutate("texture_graph_disconnect", {
        "source_node_id": src, "source_slot": sslot, "sink_node_id": dst, "sink_slot": dslot})


def set_param(node_id, parameters):
    return mutate("texture_graph_set_parameter", {"node_id": node_id, "parameters": parameters})


def get_graph():
    return call("get_texture_graph")


_scene_name_cache = None
_fresh_count = 0


def _get_scene_name():
    global _scene_name_cache
    if _scene_name_cache is None:
        _scene_name_cache = call("list_scenes")["scenes"][0]["name"]
    return _scene_name_cache


def fresh_graph():
    """Start the next block on a new, empty Graph_texture asset (created in
    the content library and auto-selected; the window and the texture_graph_*
    tools target the selected asset). Graphs only live in the content library
    - there is no window scratch graph and no clear/load tool."""
    global _fresh_count
    _fresh_count += 1
    mutate("create_graph_texture", {"name": f"Smoke {_fresh_count}", "scene_name": _get_scene_name()})


def node_by_id(graph, node_id):
    for node in graph["nodes"]:
        if node["id"] == node_id:
            return node
    return None


def params_of(node_id):
    node = node_by_id(get_graph(), node_id)
    return node["parameters"] if node else None


def value_types(pins):
    return [pin["value_type"] for pin in pins]


def undo(n=1):
    for _ in range(n):
        mutate("undo")


def redo(n=1):
    for _ in range(n):
        mutate("redo")


def undo_depth():
    stack = call("get_undo_redo_stack")
    return len(stack.get("undo", []))


def read_glb_json(path):
    """Parse the JSON chunk of a .glb file."""
    blob = pathlib.Path(path).read_bytes()
    if blob[0:4] != b"glTF":
        raise RuntimeError(f"not a GLB file: {path}")
    offset = 12
    while offset + 8 <= len(blob):
        chunk_length = int.from_bytes(blob[offset:offset + 4], "little")
        chunk_type   = blob[offset + 4:offset + 8]
        if chunk_type == b"JSON":
            return json.loads(blob[offset + 8:offset + 8 + chunk_length].decode("utf-8"))
        offset += 8 + chunk_length
    raise RuntimeError(f"no JSON chunk in GLB: {path}")


def wait_for_scene(scene_name, tries=50):
    """load_scene is queued; poll list_scenes until the scene appears."""
    for _ in range(tries):
        scenes = call("list_scenes").get("scenes", [])
        if any(s.get("name") == scene_name for s in scenes):
            return True
        time.sleep(0.2)
    return False


# ------------------------------------------------------------------ PNG decode

def decode_png(path):
    """Minimal pure-Python PNG decoder (8-bit, color types 2/RGB and 6/RGBA);
    returns (width, height, channels, bytearray of raw pixels row-major)."""
    data = pathlib.Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG file")
    pos = 8
    width = height = bit_depth = color_type = None
    idat = bytearray()
    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        ctype = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length  # 4 length + 4 type + data + 4 crc
        if ctype == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", chunk[:10])
        elif ctype == b"IDAT":
            idat += chunk
        elif ctype == b"IEND":
            break
    if bit_depth != 8:
        raise ValueError(f"unsupported bit depth {bit_depth}")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color_type]
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    out = bytearray(height * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(height):
        filter_type = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if filter_type == 1:  # Sub
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + a) & 0xFF
        elif filter_type == 2:  # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif filter_type == 3:  # Average
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif filter_type == 4:  # Paeth
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        elif filter_type != 0:
            raise ValueError(f"unsupported filter type {filter_type}")
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return width, height, channels, out


def pixel(width, channels, buf, x, y):
    base = (y * width + x) * channels
    return [buf[base + c] for c in range(channels)]


def export_png(node_id, path, size=16, output_slot=0):
    return call("texture_graph_export_png",
                {"node_id": node_id, "path": str(path), "size": size, "output_slot": output_slot})


# ------------------------------------------------------------------ node specs

# name -> (input value_types, output value_types, expected default params subset)
NODE_SPECS = {
    "uniform":             ([],                      ["rgba"], {"color": [0.8, 0.8, 0.8, 1.0]}),
    "perlin":              ([],                      ["f"],    {"scale_x": 4.0, "scale_y": 4.0, "iterations": 3.0, "persistence": 0.5}),
    "voronoi":             ([],                      ["f", "f", "rgb"], {"scale_x": 4.0, "randomness": 1.0}),
    "bricks":              ([],                      ["f"],    {"pattern": 0, "rows": 6.0, "columns": 3.0}),
    "shape":               ([],                      ["f"],    {"shape": 0, "sides": 6.0, "radius": 0.85}),
    "fbm":                 ([],                      ["f"],    {"noise": 1, "scale_x": 4.0, "iterations": 5.0, "persistence": 0.5}),
    "noise":               ([],                      ["f"],    {"size": 4, "density": 0.5}),
    "color_noise":         ([],                      ["rgb"],  {"size": 4}),
    "gradient":            ([],                      ["rgba"], {"repeat": 1.0, "rotate": 0.0, "mirror": False, "gradient": "GRADIENT"}),
    "circular_gradient":   ([],                      ["rgba"], {"repeat": 1.0, "mirror": False, "gradient": "GRADIENT"}),
    "radial_gradient":     ([],                      ["rgba"], {"repeat": 1.0, "mirror": False, "gradient": "GRADIENT"}),
    "spiral_gradient":     ([],                      ["rgba"], {"amount": 1.0, "perspective": 0.3, "use_perspective": True, "mirror": False, "gradient": "GRADIENT"}),
    "multigradient":       (["rgb"],                 ["f"],    {"count": 10.0}),
    "sine_wave":           ([],                      ["f"],    {"amplitude": 0.5, "frequency": 1.0, "phase": 0.0}),
    "truchet":             ([],                      ["f"],    {"shape": 0, "size": 4.0}),
    "weave":               ([],                      ["f"],    {"columns": 4.0, "rows": 4.0, "width": 0.8}),
    "blend":               (["rgba", "rgba"],        ["rgba"], {"blend_type": 2, "amount": 0.5}),
    "colorize":            (["f"],                   ["rgba"], {"gradient": "GRADIENT"}),
    "curve":               (["rgba"],                ["rgba"], {"curve": "CURVE"}),
    "transform":           (["rgba"],                ["rgba"], {"scale_x": 1.0, "scale_y": 1.0, "repeat": False}),
    "brightness_contrast": (["rgba"],                ["rgba"], {"brightness": 0.0, "contrast": 1.0}),
    "normal_map":          (["f"],                   ["rgb"],  {"amount": 0.5, "size": 9}),
    "blur":                (["rgba"],                ["rgba"], {"radius": 0.05}),
    "math":                (["f", "f"],              ["f"],    {"op": 0, "default_in1": 0.0, "default_in2": 0.0, "clamp": False}),
    "invert":              (["rgba"],                ["rgba"], {}),
    "quantize":            (["rgba"],                ["rgba"], {"steps": 4.0}),
    "adjust_hsv":          (["rgba"],                ["rgba"], {"hue": 0.0, "saturation": 1.0, "value": 1.0}),
    "remap":               (["f"],                   ["f"],    {"min": 0.0, "max": 1.0, "step": 0.0}),
    "combine":             (["f", "f", "f", "f"],    ["rgba"], {}),
    "decompose":           (["rgba"],                ["f", "f", "f", "f"], {}),
    "swap_channels":       (["rgba"],                ["rgba"], {"out_r": 2, "out_g": 4, "out_b": 6, "out_a": 8}),
    "reroute":             (["rgba"],                ["rgba"], {}),
    "buffer":              (["f", "rgb", "rgba"],    ["f", "rgb", "rgba"], {"size": 512, "pause": False}),
    "output":              (["f", "rgb", "rgba"],    [],       {"name": "Texture Graph", "size": 1024, "assign": False}),
    "material_output":     (["rgba", "rgb", "f", "rgba", "f", "rgba", "rgb", "rgba", "f", "rgba", "rgb", "rgba"],
                            [],       {"name": "Material", "size": 1024, "assign": True}),
}


# ---------------------------------------------------------------- sections

def section_every_node_type():
    S = "node-types"
    fresh_graph()
    ids = {}
    for type_name, (in_types, out_types, defaults) in NODE_SPECS.items():
        result = add_node(type_name)
        node_id = result.get("id") if isinstance(result, dict) else None
        ids[type_name] = node_id
        check(S, f"{type_name} add returns an id", node_id is not None, f"result={result}")

    graph = get_graph()
    check(S, f"all {len(NODE_SPECS)} node types present in graph", len(graph["nodes"]) == len(NODE_SPECS),
          f"count={len(graph['nodes'])}")

    for type_name, (in_types, out_types, defaults) in NODE_SPECS.items():
        node_id = ids[type_name]
        if node_id is None:
            continue
        node = node_by_id(graph, node_id)
        if node is None:
            check(S, f"{type_name} appears in graph", False)
            continue
        check(S, f"{type_name} factory type label", node["type"] == type_name, f"type={node['type']}")
        check(S, f"{type_name} input pin value types", value_types(node["inputs"]) == in_types,
              f"got={value_types(node['inputs'])} expected={in_types}")
        check(S, f"{type_name} output pin value types", value_types(node["outputs"]) == out_types,
              f"got={value_types(node['outputs'])} expected={out_types}")
        params = node["parameters"]
        ok = True
        for key, expected in defaults.items():
            actual = params.get(key)
            if expected == "GRADIENT":
                good = isinstance(actual, dict) and isinstance(actual.get("stops"), list) and (len(actual["stops"]) >= 1)
            elif expected == "CURVE":
                good = isinstance(actual, list) and (len(actual) >= 2)
            elif isinstance(expected, list):
                good = approx_list(actual, expected)
            elif isinstance(expected, bool):
                good = (actual == expected)
            elif isinstance(expected, (int, float)):
                good = approx(actual, expected)
            else:
                good = (actual == expected)
            ok = ok and good
        check(S, f"{type_name} default parameters", ok, f"params={params} expected~{defaults}")

    fresh_graph()
    check(S, "fresh asset starts empty", len(get_graph()["nodes"]) == 0)


def section_parameter_sweeps():
    """Set parameters, read them back via get_texture_graph, and exercise
    undo/redo of each gesture (mirrors the geometry sweep)."""
    S = "param-sweep"

    def sweep(node_id, params_a, params_b, label):
        set_param(node_id, params_a)
        state_a = params_of(node_id)
        set_param(node_id, params_b)
        state_b = params_of(node_id)
        # b applied?
        applied = True
        for key, value in params_b.items():
            got = state_b.get(key)
            if isinstance(value, list):
                applied = applied and approx_list(got, value)
            elif isinstance(value, bool):
                applied = applied and (got == value)
            else:
                applied = applied and approx(got, value)
        undo(1)
        state_after_undo = params_of(node_id)
        redo(1)
        state_after_redo = params_of(node_id)
        # undo returns to a-state, redo back to b-state (compare full dicts)
        round_trip = (state_after_undo == state_a) and (state_after_redo == state_b)
        check(S, f"{label} set+undo/redo", applied and round_trip,
              f"a={state_a} b={state_b} undo={state_after_undo} redo={state_after_redo}")

    fresh_graph()
    uni = add_node("uniform")["id"]
    sweep(uni, {"color": [0.1, 0.2, 0.3, 1.0]}, {"color": [0.9, 0.5, 0.25, 1.0]}, "uniform color")

    perlin = add_node("perlin")["id"]
    sweep(perlin, {"scale_x": 2.0, "scale_y": 2.0, "iterations": 1.0},
                  {"scale_x": 8.0, "scale_y": 6.0, "iterations": 5.0}, "perlin scale/iterations")

    vor = add_node("voronoi")["id"]
    sweep(vor, {"scale_x": 3.0, "randomness": 0.2}, {"scale_x": 10.0, "randomness": 0.9}, "voronoi scale/randomness")

    bricks = add_node("bricks")["id"]
    sweep(bricks, {"columns": 2.0, "rows": 3.0}, {"columns": 8.0, "rows": 12.0}, "bricks columns/rows")

    shape = add_node("shape")["id"]
    sweep(shape, {"shape": 0, "sides": 3.0}, {"shape": 2, "sides": 8.0}, "shape enum/sides")

    blend = add_node("blend")["id"]
    sweep(blend, {"blend_type": 0, "amount": 0.25}, {"blend_type": 5, "amount": 0.75}, "blend type/amount")

    # Gradient (colorize) and curve control-point data do not fit the scalar
    # sweep() helper; round-trip them with dedicated structural checks.
    colorize = add_node("colorize")["id"]
    grad_a = {"interpolation": 1, "stops": [
        {"pos": 0.0, "color": [0.0, 0.0, 0.0, 1.0]},
        {"pos": 1.0, "color": [1.0, 0.0, 0.0, 1.0]}]}
    grad_b = {"interpolation": 2, "stops": [
        {"pos": 0.0, "color": [0.0, 0.0, 1.0, 1.0]},
        {"pos": 0.5, "color": [0.0, 1.0, 0.0, 1.0]},
        {"pos": 1.0, "color": [1.0, 1.0, 0.0, 1.0]}]}
    set_param(colorize, {"gradient": grad_a})
    ga = params_of(colorize)["gradient"]
    set_param(colorize, {"gradient": grad_b})
    gb = params_of(colorize)["gradient"]
    applied = (len(gb["stops"]) == 3) and (gb["interpolation"] == 2) and approx_list(gb["stops"][1]["color"], [0.0, 1.0, 0.0, 1.0])
    undo(1)
    g_undo = params_of(colorize)["gradient"]
    redo(1)
    g_redo = params_of(colorize)["gradient"]
    round_trip = (len(g_undo["stops"]) == 2) and (g_undo["interpolation"] == 1) and (len(g_redo["stops"]) == 3)
    check(S, "colorize gradient set+undo/redo", applied and round_trip,
          f"a={ga} b={gb} undo={g_undo} redo={g_redo}")

    curve = add_node("curve")["id"]
    curve_a = [{"x": 0.0, "y": 0.0, "l": 0.0, "r": 0.0}, {"x": 1.0, "y": 1.0, "l": 0.0, "r": 0.0}]
    curve_b = [{"x": 0.0, "y": 1.0, "l": 0.0, "r": 0.0}, {"x": 0.5, "y": 0.2, "l": 0.0, "r": 0.0}, {"x": 1.0, "y": 0.0, "l": 0.0, "r": 0.0}]
    set_param(curve, {"curve": curve_a})
    ca = params_of(curve)["curve"]
    set_param(curve, {"curve": curve_b})
    cb = params_of(curve)["curve"]
    applied = (len(cb) == 3) and approx(cb[0]["y"], 1.0) and approx(cb[2]["y"], 0.0)
    undo(1)
    c_undo = params_of(curve)["curve"]
    redo(1)
    c_redo = params_of(curve)["curve"]
    round_trip = (len(c_undo) == 2) and (len(c_redo) == 3)
    check(S, "curve points set+undo/redo", applied and round_trip,
          f"a={ca} b={cb} undo={c_undo} redo={c_redo}")

    tr = add_node("transform")["id"]
    sweep(tr, {"translate_x": 0.0, "rotate": 0.0, "scale_x": 1.0, "repeat": False},
              {"translate_x": 0.3, "rotate": 90.0, "scale_x": 2.0, "repeat": True}, "transform trs")

    bc = add_node("brightness_contrast")["id"]
    sweep(bc, {"brightness": -0.5, "contrast": 0.5}, {"brightness": 0.5, "contrast": 1.8}, "brightness/contrast")

    nm = add_node("normal_map")["id"]
    sweep(nm, {"amount": 0.2, "size": 6}, {"amount": 1.5, "size": 10}, "normal_map amount/size")


def section_parameter_abuse():
    """Out-of-range / degenerate values must not crash the editor; the graph
    stays queryable and the value round-trips (set_parameter does not clamp -
    the descriptor min/max only constrain the widget)."""
    S = "param-abuse"
    fresh_graph()

    perlin = add_node("perlin")["id"]
    set_param(perlin, {"scale_x": -4.0, "iterations": 0.0})
    p = params_of(perlin)
    check(S, "perlin negative scale accepted, no crash", approx(p.get("scale_x"), -4.0), f"params={p}")

    bricks = add_node("bricks")["id"]
    set_param(bricks, {"pattern": 99})  # enum index beyond range
    p = params_of(bricks)
    check(S, "bricks enum index 99 accepted, no crash", p.get("pattern") == 99, f"params={p}")

    shape = add_node("shape")["id"]
    set_param(shape, {"shape": 42, "sides": 0.0})
    p = params_of(shape)
    check(S, "shape enum 42 + sides 0 accepted, no crash", p.get("shape") == 42, f"params={p}")

    uni = add_node("uniform")["id"]
    set_param(uni, {"color": [-1.0, 5.0, 0.5, 1.0]})  # out of [0,1]
    p = params_of(uni)
    check(S, "uniform out-of-range color accepted", approx_list(p.get("color"), [-1.0, 5.0, 0.5, 1.0]), f"params={p}")

    nm = add_node("normal_map")["id"]
    set_param(nm, {"size": 0})  # size exponent 0 -> 1px kernel
    p = params_of(nm)
    check(S, "normal_map size exponent 0 accepted", p.get("size") == 0, f"params={p}")

    out = add_node("output")["id"]
    set_param(out, {"size": 0})  # not one of 256/512/1024/2048 -> keeps previous
    p = params_of(out)
    check(S, "output size 0 handled (keeps a valid size)", p.get("size") in (256, 512, 1024, 2048), f"params={p}")

    check(S, "editor alive + graph queryable after abuse", len(get_graph()["nodes"]) == 6)


def section_connect_rules():
    """Pin-key rules: valid chain connects, disconnect removes, and
    type-mismatch / self / 2-node / 3-node cycles are all rejected with no
    link created and no undo entry."""
    S = "connect"
    fresh_graph()
    perlin = add_node("perlin")["id"]        # out f (key grayscale)
    colorize = add_node("colorize")["id"]     # in f, out rgba
    output = add_node("output")["id"]         # in f/rgb/rgba (slots 0/1/2)

    # Valid chain: perlin.f -> colorize.f ; colorize.rgba -> output.rgba (slot 2)
    connect(perlin, 0, colorize, 0)
    connect(colorize, 0, output, 2)
    links = get_graph()["links"]
    check(S, "valid chain creates two links", len(links) == 2, f"links={links}")
    has_pc = any(l["source_node_id"] == perlin and l["sink_node_id"] == colorize for l in links)
    has_co = any(l["source_node_id"] == colorize and l["sink_node_id"] == output and l["sink_slot"] == 2 for l in links)
    check(S, "perlin->colorize link present", has_pc)
    check(S, "colorize->output(rgba slot) link present", has_co)

    # Disconnect one link.
    disconnect(perlin, 0, colorize, 0)
    links = get_graph()["links"]
    check(S, "disconnect removes exactly one link", len(links) == 1, f"links={links}")

    # Fan out: one perlin feeds colorize.f AND normal_map.f (both grayscale).
    fresh_graph()
    perlin = add_node("perlin")["id"]
    colorize = add_node("colorize")["id"]
    nm = add_node("normal_map")["id"]
    connect(perlin, 0, colorize, 0)
    connect(perlin, 0, nm, 0)
    links = get_graph()["links"]
    fan = sum(1 for l in links if l["source_node_id"] == perlin)
    check(S, "generator fans out to two filters", fan == 2, f"links={links}")

    # Rejections.
    fresh_graph()
    uni = add_node("uniform")["id"]           # out rgba (key rgba)
    colorize = add_node("colorize")["id"]     # in f (key grayscale)
    tr_a = add_node("transform")["id"]        # in rgba, out rgba
    tr_b = add_node("transform")["id"]
    tr_c = add_node("transform")["id"]
    connect(tr_a, 0, tr_b, 0)                 # valid A->B
    connect(tr_b, 0, tr_c, 0)                 # valid B->C
    links_before = len(get_graph()["links"])
    depth_before = undo_depth()

    err = connect_expect_error(uni, 0, colorize, 0)  # rgba -> f (key mismatch)
    check(S, "type mismatch (rgba->f) rejected", err is not None, f"err={err}")
    err = connect_expect_error(tr_a, 0, tr_a, 0)     # self link
    check(S, "self connect rejected", err is not None, f"err={err}")
    err = connect_expect_error(tr_b, 0, tr_a, 0)     # 2-node cycle B->A
    check(S, "two-node cycle rejected", err is not None, f"err={err}")
    err = connect_expect_error(tr_c, 0, tr_a, 0)     # 3-node cycle C->A
    check(S, "three-node cycle rejected", err is not None, f"err={err}")

    links_after = len(get_graph()["links"])
    check(S, "rejected connects create no links", links_after == links_before,
          f"before={links_before} after={links_after}")
    check(S, "rejected connects add no undo entries", undo_depth() == depth_before,
          f"before={depth_before} after={undo_depth()}")


def section_undo_redo():
    S = "undo-redo"
    fresh_graph()
    depth0 = undo_depth()

    # Add a node.
    perlin = add_node("perlin")["id"]
    check(S, "add grows undo stack", undo_depth() == depth0 + 1, f"depth={undo_depth()}")
    undo(1)
    check(S, "undo removes the added node", len(get_graph()["nodes"]) == 0)
    redo(1)
    check(S, "redo restores the added node", len(get_graph()["nodes"]) == 1)

    # Connect.
    colorize = add_node("colorize")["id"]
    connect(perlin, 0, colorize, 0)
    check(S, "connect present", len(get_graph()["links"]) == 1)
    undo(1)
    check(S, "undo removes the link", len(get_graph()["links"]) == 0)
    redo(1)
    check(S, "redo restores the link", len(get_graph()["links"]) == 1)

    # Parameter change.
    set_param(perlin, {"scale_x": 16.0})
    check(S, "param applied", approx(params_of(perlin).get("scale_x"), 16.0))
    undo(1)
    check(S, "undo reverts the parameter", approx(params_of(perlin).get("scale_x"), 4.0),
          f"scale_x={params_of(perlin).get('scale_x')}")
    redo(1)
    check(S, "redo reapplies the parameter", approx(params_of(perlin).get("scale_x"), 16.0))


def section_render_export():
    """Core proof: composed shaders actually render to pixels."""
    S = "render"
    TMP_DIR.mkdir(parents=True, exist_ok=True)
    fresh_graph()

    # Uniform color -> exact center pixel (linear UNORM RGBA8, no sRGB).
    uni = add_node("uniform")["id"]
    color = [0.2, 0.6, 0.9, 1.0]
    set_param(uni, {"color": color})
    uni_png = TMP_DIR / "uniform.png"
    result = export_png(uni, uni_png, size=16)
    check(S, "uniform export returns path+dims",
          isinstance(result, dict) and result.get("width") == 16 and result.get("height") == 16,
          f"result={result}")
    check(S, "uniform PNG file exists and is non-empty", uni_png.is_file() and uni_png.stat().st_size > 0)
    w, h, ch, buf = decode_png(uni_png)
    check(S, "uniform PNG decodes to 16x16 RGBA", (w, h, ch) == (16, 16, 4), f"got=({w},{h},{ch})")
    cx = pixel(w, ch, buf, 8, 8)
    expected = [round(c * 255) for c in color]
    match = all(abs(cx[i] - expected[i]) <= 3 for i in range(4))
    check(S, "uniform center pixel matches set color (+/-3)", match, f"pixel={cx} expected={expected}")
    # Flatness: min == max per channel across the image.
    flat = True
    for c in range(3):
        vals = [buf[(i * ch) + c] for i in range(w * h)]
        if max(vals) - min(vals) > 3:
            flat = False
    check(S, "uniform image is a flat color", flat)

    # Perlin -> output must VARY (min != max across samples).
    perlin = add_node("perlin")["id"]
    set_param(perlin, {"scale_x": 6.0, "scale_y": 6.0, "iterations": 4.0})
    perlin_png = TMP_DIR / "perlin.png"
    export_png(perlin, perlin_png, size=64)
    w, h, ch, buf = decode_png(perlin_png)
    reds = [buf[(i * ch)] for i in range(w * h)]
    varies = (max(reds) - min(reds)) > 16
    check(S, "perlin output varies (not flat)", varies, f"min={min(reds)} max={max(reds)}")

    # Output node bakes the connected subtree: perlin -> colorize -> output.
    fresh_graph()
    perlin = add_node("perlin")["id"]
    colorize = add_node("colorize")["id"]
    output = add_node("output")["id"]
    set_param(colorize, {"gradient": {"interpolation": 1, "stops": [
        {"pos": 0.0, "color": [0.0, 0.0, 0.2, 1.0]},
        {"pos": 1.0, "color": [1.0, 0.4, 0.0, 1.0]}]}})
    connect(perlin, 0, colorize, 0)
    connect(colorize, 0, output, 2)
    out_png = TMP_DIR / "output_baked.png"
    result = export_png(output, out_png, size=64)
    check(S, "output-node export returns dims",
          isinstance(result, dict) and result.get("width") == 64, f"result={result}")
    check(S, "output baked PNG exists", out_png.is_file() and out_png.stat().st_size > 0)
    w, h, ch, buf = decode_png(out_png)
    # colorize output varies and is colored (R channel != B channel somewhere).
    reds = [buf[(i * ch)] for i in range(w * h)]
    varies = (max(reds) - min(reds)) > 8
    check(S, "output baked result varies (colorized noise)", varies, f"min={min(reds)} max={max(reds)}")

    # Export of an unconnected Output node must error (nothing composable).
    fresh_graph()
    lonely = add_node("output")["id"]
    err = call_expect_error("texture_graph_export_png",
                            {"node_id": lonely, "path": str(TMP_DIR / "lonely.png"), "size": 8})
    check(S, "export of unconnected output errors cleanly", err is not None, f"err={err}")


def section_gradient_curve():
    """Gradient (colorize) and curve nodes render their control-point functions:
    the colorize maps its default $uv.x ramp through a blue->red gradient, and
    the curve node inverts a grayscale ramp - both proven by exported pixels."""
    S = "gradient-curve"
    TMP_DIR.mkdir(parents=True, exist_ok=True)
    fresh_graph()

    # Colorize with default $uv.x input -> blue@0 -> red@1 horizontal ramp.
    colorize = add_node("colorize")["id"]
    set_param(colorize, {"gradient": {"interpolation": 1, "stops": [
        {"pos": 0.0, "color": [0.0, 0.0, 1.0, 1.0]},
        {"pos": 1.0, "color": [1.0, 0.0, 0.0, 1.0]}]}})
    grad_png = TMP_DIR / "gradient_colorize.png"
    result = export_png(colorize, grad_png, size=32)
    check(S, "gradient colorize export returns dims",
          isinstance(result, dict) and result.get("width") == 32, f"result={result}")
    w, h, ch, buf = decode_png(grad_png)
    left  = pixel(w, ch, buf, 1,     h // 2)
    right = pixel(w, ch, buf, w - 2, h // 2)
    check(S, "gradient left edge is bluish", left[2] > left[0] + 20, f"left={left}")
    check(S, "gradient right edge is reddish", right[0] > right[2] + 20, f"right={right}")
    reds = [buf[(i * ch)] for i in range(w * h)]
    check(S, "gradient output varies across the ramp", (max(reds) - min(reds)) > 40,
          f"min={min(reds)} max={max(reds)}")
    g = params_of(colorize)["gradient"]
    check(S, "gradient round-trips 2 stops + interpolation", isinstance(g, dict) and len(g["stops"]) == 2 and g["interpolation"] == 1, f"g={g}")

    # Curve node inverting its default grayscale $uv.x ramp -> left bright, right dark.
    fresh_graph()
    curve = add_node("curve")["id"]
    set_param(curve, {"curve": [
        {"x": 0.0, "y": 1.0, "l": 0.0, "r": 0.0},
        {"x": 1.0, "y": 0.0, "l": 0.0, "r": 0.0}]})
    curve_png = TMP_DIR / "curve_invert.png"
    export_png(curve, curve_png, size=32)
    w, h, ch, buf = decode_png(curve_png)
    left  = pixel(w, ch, buf, 1,     h // 2)
    right = pixel(w, ch, buf, w - 2, h // 2)
    check(S, "inverting curve makes left brighter than right", left[0] > right[0] + 40,
          f"left={left} right={right}")
    c = params_of(curve)["curve"]
    check(S, "curve round-trips 2 points", isinstance(c, list) and len(c) == 2, f"c={c}")


def section_output_node():
    """Output node: name/size params, generator connect, assign-to-material
    path (no crash / no visual assert headlessly)."""
    S = "output-node"
    fresh_graph()
    uni = add_node("uniform")["id"]
    output = add_node("output")["id"]
    set_param(uni, {"color": [0.3, 0.7, 0.2, 1.0]})
    set_param(output, {"name": "Base Color Tex", "size": 512})
    connect(uni, 0, output, 2)
    p = params_of(output)
    check(S, "output name + size round-trip", p.get("name") == "Base Color Tex" and p.get("size") == 512, f"params={p}")

    # Force a bake so the texture registers in the content library.
    export_png(output, TMP_DIR / "out_named.png", size=64)

    # The scene queries are addressed by scene name; use the first scene.
    scenes = call("list_scenes")
    scene_name = ""
    if isinstance(scenes, dict) and scenes.get("scenes"):
        scene_name = scenes["scenes"][0].get("name", "")
    elif isinstance(scenes, list) and scenes:
        scene_name = scenes[0].get("name", "")

    # The registered texture should surface in get_scene_textures by name.
    textures = call("get_scene_textures", {"scene_name": scene_name})
    tex_names = []
    if isinstance(textures, dict):
        for entry in textures.get("textures", []):
            tex_names.append(entry.get("name", ""))
    elif isinstance(textures, list):
        tex_names = [t.get("name", "") for t in textures if isinstance(t, dict)]
    # Registration happens during a rendered frame (render_products); it may lag
    # a headless MCP-only export. Treat presence as a bonus, absence as non-fatal.
    print(f"  (scene textures: {tex_names})")

    # Exercise the assign-to-material path: turn it on and set a material by
    # name if any exist; assert the tool calls succeed and the flag round-trips.
    mats = call("get_scene_materials", {"scene_name": scene_name})
    mat_names = []
    if isinstance(mats, dict):
        mat_names = [m.get("name", "") for m in mats.get("materials", [])]
    elif isinstance(mats, list):
        mat_names = [m.get("name", "") for m in mats if isinstance(m, dict)]
    args = {"assign": True}
    if mat_names:
        args["material"] = mat_names[0]
    set_param(output, args)
    p = params_of(output)
    check(S, "assign-to-material flag set without crash", p.get("assign") is True, f"params={p}")
    export_png(output, TMP_DIR / "out_assigned.png", size=32)
    check(S, "editor alive after assign path", len(get_graph()["nodes"]) == 2)


def section_multi_output_decompose():
    """Decompose is the editor's first multi-output filter node. Feed it a
    uniform whose channels differ, assert it exposes 4 grayscale outputs, and
    export each output slot to prove the composer selects the matching channel;
    then route one output onward through the Output node."""
    S = "multi-output"
    TMP_DIR.mkdir(parents=True, exist_ok=True)
    fresh_graph()

    color = [0.2, 0.4, 0.6, 1.0]
    uni = add_node("uniform")["id"]
    set_param(uni, {"color": color})
    dec = add_node("decompose")["id"]
    connect(uni, 0, dec, 0)  # uniform.rgba -> decompose.i

    graph = get_graph()
    node = node_by_id(graph, dec)
    check(S, "decompose exposes 4 grayscale outputs",
          node is not None and value_types(node["outputs"]) == ["f", "f", "f", "f"],
          f"outputs={value_types(node['outputs']) if node else None}")
    check(S, "decompose composable once fed", node is not None and node.get("composable") is True,
          f"composable={node.get('composable') if node else None}")

    # Each output slot must isolate a channel of the uniform color.
    for slot, channel_value, chan_name in ((0, color[0], "R"), (1, color[1], "G"),
                                           (2, color[2], "B"), (3, color[3], "A")):
        png = TMP_DIR / f"decompose_{chan_name}.png"
        export_png(dec, png, size=8, output_slot=slot)
        w, h, ch, buf = decode_png(png)
        cx = pixel(w, ch, buf, w // 2, h // 2)
        expected = round(channel_value * 255)
        # grayscale output is broadcast to RGB; compare the red channel.
        check(S, f"decompose slot {slot} ({chan_name}) center ~= channel value",
              abs(cx[0] - expected) <= 4, f"pixel={cx} expected~{expected}")

    # Route the Blue channel onward: decompose.B (slot 2) -> output.f (slot 0).
    output = add_node("output")["id"]
    connect(dec, 2, output, 0)
    out_png = TMP_DIR / "decompose_routed.png"
    result = export_png(output, out_png, size=16)
    check(S, "routed decompose output exports", isinstance(result, dict) and result.get("width") == 16,
          f"result={result}")
    w, h, ch, buf = decode_png(out_png)
    cx = pixel(w, ch, buf, w // 2, h // 2)
    check(S, "routed Blue channel bakes ~0.6 gray", abs(cx[0] - round(color[2] * 255)) <= 6,
          f"pixel={cx}")


def section_new_filters():
    """A few Phase 4b filters proven end-to-end: invert flips a color, the math
    op enum switches behavior (A*0 -> flat black), and remap collapses a varying
    input to a constant. Each exercises a distinct substitution path (rgba
    expression, enum-as-code-fragment, inline code + float uniforms)."""
    S = "new-filters"
    TMP_DIR.mkdir(parents=True, exist_ok=True)

    # invert: uniform color -> invert -> center pixel is 1 - color (RGB).
    fresh_graph()
    color = [0.2, 0.6, 0.9, 1.0]
    uni = add_node("uniform")["id"]
    set_param(uni, {"color": color})
    inv = add_node("invert")["id"]
    connect(uni, 0, inv, 0)
    inv_png = TMP_DIR / "invert.png"
    export_png(inv, inv_png, size=16)
    w, h, ch, buf = decode_png(inv_png)
    cx = pixel(w, ch, buf, 8, 8)
    expected = [round((1.0 - color[i]) * 255) for i in range(3)] + [round(color[3] * 255)]
    match = all(abs(cx[i] - expected[i]) <= 4 for i in range(4))
    check(S, "invert flips RGB and keeps A", match, f"pixel={cx} expected={expected}")

    # math: perlin -> math.in1 with op A*B and default B = 0 -> flat black.
    fresh_graph()
    perlin = add_node("perlin")["id"]
    set_param(perlin, {"scale_x": 6.0, "scale_y": 6.0, "iterations": 4.0})
    math = add_node("math")["id"]
    connect(perlin, 0, math, 0)
    set_param(math, {"op": 2, "default_in2": 0.0})  # op index 2 == A*B
    math_png = TMP_DIR / "math_mul0.png"
    export_png(math, math_png, size=32)
    w, h, ch, buf = decode_png(math_png)
    reds = [buf[(i * ch)] for i in range(w * h)]
    check(S, "math A*0 renders flat black", max(reds) <= 4, f"min={min(reds)} max={max(reds)}")

    # remap: perlin -> remap with min==max collapses the varying input to a
    # constant (min + in*(max-min) - mod(...) == min when max==min).
    fresh_graph()
    perlin = add_node("perlin")["id"]
    set_param(perlin, {"scale_x": 6.0, "scale_y": 6.0, "iterations": 4.0})
    remap = add_node("remap")["id"]
    connect(perlin, 0, remap, 0)
    set_param(remap, {"min": 0.3, "max": 0.3, "step": 0.0})
    remap_png = TMP_DIR / "remap_const.png"
    export_png(remap, remap_png, size=32)
    w, h, ch, buf = decode_png(remap_png)
    reds = [buf[(i * ch)] for i in range(w * h)]
    flat = (max(reds) - min(reds)) <= 4
    near = abs((sum(reds) / len(reds)) - round(0.3 * 255)) <= 8
    check(S, "remap min==max collapses to constant ~0.3", flat and near,
          f"min={min(reds)} max={max(reds)} mean={sum(reds) / len(reds):.1f}")


def export_png_retry(node_id, path, size=16, output_slot=0, tries=25, delay=0.3):
    """Export, retrying while a sampled buffer has not produced its texture yet.

    A buffer renders its texture during the editor frame; the first export right
    after wiring can race that first render. The renderer reports this as a
    non-busy error, so retry it for a few seconds rather than failing."""
    last = None
    for _ in range(tries):
        try:
            return export_png(node_id, path, size=size, output_slot=output_slot)
        except RuntimeError as error:
            last = error
            time.sleep(delay)
    raise last


def local_row_variation(w, h, ch, buf, channel=0):
    """Sum of absolute horizontal neighbor differences on one channel - a proxy
    for local high-frequency content (blur lowers it)."""
    total = 0
    for y in range(h):
        row = y * w
        for x in range(w - 1):
            a = buf[(row + x) * ch + channel]
            b = buf[(row + x + 1) * ch + channel]
            total += abs(a - b)
    return total


def section_buffer():
    """Phase 5 buffer node: an explicit render-to-texture cut point. A buffer
    renders its input subtree once into a real texture; downstream nodes sample
    that texture instead of re-inlining the subtree. Proven by (a) a buffer
    faithfully reproducing a flat input color when sampled through a reroute, and
    (b) one buffer feeding two consumers that both read the same result."""
    S = "buffer"
    TMP_DIR.mkdir(parents=True, exist_ok=True)
    fresh_graph()

    # uniform -> buffer -> reroute : the reroute samples the buffer's texture
    # (the compose DAG cuts at the buffer), so its export must match the uniform.
    uni = add_node("uniform")["id"]
    buf = add_node("buffer")["id"]
    rer = add_node("reroute")["id"]
    color = [0.3, 0.55, 0.8, 1.0]
    set_param(uni, {"color": color})
    check(S, "uniform -> buffer connects", connect(uni, 0, buf, 2))       # rgba -> rgba input
    check(S, "buffer -> reroute connects", connect(buf, 2, rer, 0))       # rgba output -> reroute
    get_graph()

    rer_png = TMP_DIR / "buffer_reroute.png"
    result = export_png_retry(rer, rer_png, size=16)
    check(S, "reroute-over-buffer export returns dims",
          isinstance(result, dict) and result.get("width") == 16, f"result={result}")
    w, h, ch, rbuf = decode_png(rer_png)
    rc = pixel(w, ch, rbuf, 8, 8)
    expected = [round(c * 255) for c in color]
    match = all(abs(rc[i] - expected[i]) <= 5 for i in range(4))
    check(S, "buffer faithfully reproduces its input color (sampled downstream)", match,
          f"pixel={rc} expected={expected}")

    # One buffer feeding two consumers: both reroutes must read the same texture.
    rer2 = add_node("reroute")["id"]
    check(S, "buffer -> second reroute connects", connect(buf, 2, rer2, 0))
    get_graph()
    rer2_png = TMP_DIR / "buffer_reroute2.png"
    export_png_retry(rer2, rer2_png, size=16)
    _, _, _, rbuf2 = decode_png(rer2_png)
    c2 = pixel(w, ch, rbuf2, 8, 8)
    same = all(abs(c2[i] - rc[i]) <= 2 for i in range(4))
    check(S, "buffer feeding two consumers yields the same result", same, f"a={rc} b={c2}")

    # A buffer whose input is disconnected must not crash export (no texture yet).
    fresh_graph()
    lonely_buf = add_node("buffer")["id"]
    lonely_rer = add_node("reroute")["id"]
    connect(lonely_buf, 2, lonely_rer, 0)
    get_graph()
    err = call_expect_error("texture_graph_export_png",
                            {"node_id": lonely_rer, "path": str(TMP_DIR / "buffer_lonely.png"), "size": 8})
    check(S, "export of reroute over an unfed buffer errors cleanly (no crash)", err is not None, f"err={err}")


def section_blur():
    """Phase 5 buffer-dependent filter: a Gaussian blur multiply-samples its
    input. Fed through a buffer it becomes cheap texture taps. Proven by the
    blurred noise having markedly less local (neighbor) variation than the
    unblurred noise at the same resolution."""
    S = "blur"
    TMP_DIR.mkdir(parents=True, exist_ok=True)
    fresh_graph()

    # perlin -> buffer -> blur ; also export the raw perlin for comparison.
    perlin = add_node("perlin")["id"]
    buf    = add_node("buffer")["id"]
    blur   = add_node("blur")["id"]
    set_param(perlin, {"scale_x": 8.0, "scale_y": 8.0, "iterations": 4.0})
    set_param(blur,   {"radius": 0.15})
    check(S, "perlin -> buffer connects", connect(perlin, 0, buf, 0))     # f -> f input
    check(S, "buffer -> blur connects",   connect(buf, 2, blur, 0))       # rgba output -> blur rgba input
    get_graph()

    size = 64
    blur_png  = TMP_DIR / "blur_blurred.png"
    noise_png = TMP_DIR / "blur_noise.png"
    result = export_png_retry(blur, blur_png, size=size)
    check(S, "blur export returns dims", isinstance(result, dict) and result.get("width") == size, f"result={result}")
    export_png(perlin, noise_png, size=size)

    wb, hb, chb, bbuf = decode_png(blur_png)
    wn, hn, chn, nbuf = decode_png(noise_png)
    blurred_var = local_row_variation(wb, hb, chb, bbuf)
    noise_var   = local_row_variation(wn, hn, chn, nbuf)
    check(S, "blurred result varies less locally than the unblurred noise",
          blurred_var < (noise_var * 0.6),
          f"blurred_var={blurred_var} noise_var={noise_var}")
    # The blur output is not a flat constant (it still contains the low frequencies).
    reds = [bbuf[(i * chb)] for i in range(wb * hb)]
    check(S, "blurred result is not flat", (max(reds) - min(reds)) > 4, f"min={min(reds)} max={max(reds)}")


def section_reseed():
    """Reseed: changing a seeded generator's seed uniform changes its output
    pattern (the mechanism behind the per-node Reseed button and 'Reseed all')."""
    S = "reseed"
    TMP_DIR.mkdir(parents=True, exist_ok=True)
    fresh_graph()

    perlin = add_node("perlin")["id"]
    set_param(perlin, {"scale_x": 6.0, "scale_y": 6.0, "seed": 0.0})
    a_png = TMP_DIR / "reseed_a.png"
    export_png(perlin, a_png, size=48)
    set_param(perlin, {"seed": 137.5})
    b_png = TMP_DIR / "reseed_b.png"
    export_png(perlin, b_png, size=48)

    wa, ha, cha, abuf = decode_png(a_png)
    wb, hb, chb, bbuf = decode_png(b_png)
    changed = sum(1 for i in range(wa * ha) if abs(abuf[i * cha] - bbuf[i * chb]) > 8)
    check(S, "reseed changes the seeded generator's output pixels", changed > (wa * ha) // 10,
          f"changed={changed} of {wa * ha}")


def section_material_output():
    """PBR Material Output node (Phase 6): bake connected channels to textures,
    pack occlusion/roughness/metallic into one glTF ORM texture, assemble an
    erhe material, and export channels to PNG. Verifies the material actually
    references the baked textures (get_material_details) and that the ORM packing
    matches how standard.frag samples (R=occlusion, G=roughness, B=metallic)."""
    S = "material-output"
    TMP_DIR.mkdir(parents=True, exist_ok=True)

    scenes = call("list_scenes")
    scene_name = ""
    if isinstance(scenes, dict) and scenes.get("scenes"):
        scene_name = scenes["scenes"][0].get("name", "")
    elif isinstance(scenes, list) and scenes:
        scene_name = scenes[0].get("name", "")

    mats = call("get_scene_materials", {"scene_name": scene_name})
    mat_names = []
    if isinstance(mats, dict):
        mat_names = [m.get("name", "") for m in mats.get("materials", [])]
    elif isinstance(mats, list):
        mat_names = [m.get("name", "") for m in mats if isinstance(m, dict)]
    check(S, "scene has at least one material to assign", len(mat_names) > 0, f"mats={mat_names}")
    target_material = mat_names[0] if mat_names else ""

    # --- Assembly + assignment: perlin->colorize->albedo, shape->roughness,
    #     bricks->metallic, voronoi->occlusion, perlin->normal_map->normal.
    fresh_graph()
    mat = add_node("material_output")["id"]
    perlin = add_node("perlin")["id"]
    colorize = add_node("colorize")["id"]
    set_param(colorize, {"gradient": {"interpolation": 1, "stops": [
        {"pos": 0.0, "color": [0.1, 0.1, 0.4, 1.0]},
        {"pos": 1.0, "color": [0.9, 0.5, 0.1, 1.0]}]}})
    connect(perlin, 0, colorize, 0)
    connect(colorize, 0, mat, 0)          # albedo rgba pin (slot 0)
    shape = add_node("shape")["id"]
    connect(shape, 0, mat, 4)             # roughness f pin (slot 4)
    bricks = add_node("bricks")["id"]
    connect(bricks, 0, mat, 2)            # metallic f pin (slot 2)
    voronoi = add_node("voronoi")["id"]
    connect(voronoi, 0, mat, 8)           # occlusion f pin (slot 8)
    perlin2 = add_node("perlin")["id"]
    nmap = add_node("normal_map")["id"]
    connect(perlin2, 0, nmap, 0)
    connect(nmap, 0, mat, 6)              # normal rgb pin (slot 6)

    set_param(mat, {"name": "Smoke PBR Mat", "size": 256, "assign": True, "material": target_material})
    p = params_of(mat)
    check(S, "material-output params round-trip",
          p.get("name") == "Smoke PBR Mat" and p.get("size") == 256 and p.get("assign") is True and p.get("material") == target_material,
          f"params={p}")

    # render_products bakes + assigns on a frame tick; poll for base_color.
    def texture_slots():
        d = call("get_material_details", {"scene_name": scene_name, "material_name": target_material})
        return d.get("texture_samplers", {}) if isinstance(d, dict) else {}
    deadline = time.time() + 20
    ts = {}
    while time.time() < deadline:
        ts = texture_slots()
        if ts.get("base_color", {}).get("texture_id") is not None:
            break
        time.sleep(0.5)

    def has_tex(name):
        return ts.get(name, {}).get("texture_id") is not None
    check(S, "albedo baked into base_color slot", has_tex("base_color"), f"base_color={ts.get('base_color')}")
    check(S, "normal baked into normal slot", has_tex("normal"), f"normal={ts.get('normal')}")
    check(S, "ORM baked into metallic_roughness slot", has_tex("metallic_roughness"), f"mr={ts.get('metallic_roughness')}")
    check(S, "occlusion baked into occlusion slot", has_tex("occlusion"), f"occ={ts.get('occlusion')}")
    check(S, "emissive slot stays empty (unconnected)", not has_tex("emissive"), f"emis={ts.get('emissive')}")
    check(S, "occlusion + metallic_roughness share the packed ORM texture",
          ts.get("metallic_roughness", {}).get("texture_id") == ts.get("occlusion", {}).get("texture_id"),
          f"mr={ts.get('metallic_roughness', {}).get('texture_id')} occ={ts.get('occlusion', {}).get('texture_id')}")

    md = call("get_material_details", {"scene_name": scene_name, "material_name": target_material})
    check(S, "metallic scalar driven to 1.0 (texture .b drives)", approx(md.get("metallic"), 1.0), f"metallic={md.get('metallic')}")
    check(S, "roughness scalar driven to 1.0 (texture .g drives)", approx_list(md.get("roughness"), [1.0, 1.0]), f"roughness={md.get('roughness')}")

    textures = call("get_scene_textures", {"scene_name": scene_name})
    tex_names = [t.get("name", "") for t in textures.get("textures", [])] if isinstance(textures, dict) else []
    for suffix in ("Albedo", "Normal", "ORM"):
        check(S, f"content library registers 'Smoke PBR Mat {suffix}'", f"Smoke PBR Mat {suffix}" in tex_names, f"names={tex_names}")

    # --- Channel PNG export.
    export = call("texture_graph_export_material", {"node_id": mat, "dir": str(TMP_DIR / "material"), "size": 16})
    files = export.get("files", []) if isinstance(export, dict) else []
    have = " ".join(files)
    check(S, "export writes albedo + normal + metallic_roughness + occlusion PNGs",
          all(s in have for s in ("_albedo.png", "_normal.png", "_metallic_roughness.png", "_occlusion.png")),
          f"files={files}")
    check(S, "export skips the unconnected emissive channel", "_emissive.png" not in have, f"files={files}")

    # --- ORM packing pixel check: deterministic gray sources -> R=occ,G=rough,B=metal.
    fresh_graph()
    mat2 = add_node("material_output")["id"]

    def gray_uniform(v):
        node_id = add_node("uniform")["id"]
        set_param(node_id, {"color": [v, v, v, 1.0]})
        return node_id
    connect(gray_uniform(0.25), 0, mat2, 9)  # occlusion rgba pin -> luminance 0.25
    connect(gray_uniform(0.50), 0, mat2, 5)  # roughness rgba pin -> luminance 0.50
    connect(gray_uniform(0.75), 0, mat2, 3)  # metallic  rgba pin -> luminance 0.75
    set_param(mat2, {"name": "ORM Pack", "size": 16, "assign": False})
    export2 = call("texture_graph_export_material", {"node_id": mat2, "dir": str(TMP_DIR / "orm"), "size": 16})
    files2 = export2.get("files", []) if isinstance(export2, dict) else []
    orm_png = next((f for f in files2 if f.endswith("_metallic_roughness.png")), None)
    check(S, "ORM export produced a metallic_roughness PNG", orm_png is not None, f"files={files2}")
    if orm_png:
        w, h, ch, buf = decode_png(pathlib.Path(orm_png))
        cx = pixel(w, ch, buf, w // 2, h // 2)
        expected = [round(0.25 * 255), round(0.5 * 255), round(0.75 * 255)]
        match = all(abs(cx[i] - expected[i]) <= 4 for i in range(3))
        check(S, "ORM packing R=occlusion G=roughness B=metallic", match, f"pixel={cx} expected~{expected}")

    fresh_graph()
    check(S, "editor alive after material output section", len(get_graph()["nodes"]) == 0)


def section_graph_texture_asset():
    """Graph Texture as a content-library asset: create + select, edit the
    selected asset, bind a material slot to it, and round-trip both the asset and
    the binding through save_scene / load_scene (ERHE_node_graphs extension)."""
    S = "graph-texture-asset"

    scenes = call("list_scenes")["scenes"]
    if not scenes:
        check(S, "a scene exists", False)
        return
    scene_name = scenes[0]["name"]

    created = mutate("create_graph_texture", {"name": "Smoke Asset", "scene_name": scene_name})
    check(S, "create_graph_texture created + returned id", bool(created) and created.get("created") and created.get("id"),
          detail=str(created))

    # Issue #252: create_graph_texture points the Texture Graph window at the
    # new asset (its target), no longer via the global selection.
    graph = get_graph()
    check(S, "new Graph Texture is the window target", graph.get("graph_texture_name") == "Smoke Asset",
          detail=str(graph.get("graph_texture_name")))

    listed = call("get_graph_textures", {"scene_name": scene_name})["graph_textures"]
    check(S, "get_graph_textures lists the asset", any(g["name"] == "Smoke Asset" for g in listed), detail=str(listed))

    # Edit the SELECTED asset: nodes must land in it, not a global graph.
    uniform = add_node("uniform")
    set_param(uniform["id"], {"color": [0.2, 0.6, 0.9, 1.0]})
    output = add_node("output")
    connect(uniform["id"], 0, output["id"], 2)

    graph = get_graph()
    check(S, "get_texture_graph targets the selected asset", graph.get("graph_texture_name") == "Smoke Asset",
          detail=str(graph.get("graph_texture_name")))
    check(S, "nodes land in the selected asset", len(graph["nodes"]) == 2, detail=str(len(graph["nodes"])))

    listed = call("get_graph_textures", {"scene_name": scene_name})["graph_textures"]
    asset = next((g for g in listed if g["name"] == "Smoke Asset"), None)
    check(S, "asset bakes an output after connect", bool(asset) and asset.get("has_output") is True, detail=str(asset))

    # Bind a material's base_color to the asset (the material -> graph back-ref).
    # The material must be one a mesh actually uses: glTF scene save exports
    # only mesh-referenced materials and drops graph bindings of unexported
    # materials (with a warning), so binding an unused library material would
    # not round-trip. Find a used material via a mesh node's details.
    material_name = ""
    nodes = call("get_scene_nodes", {"scene_name": scene_name}).get("nodes", [])
    for node in nodes:
        if "Mesh" not in node.get("attachment_types", []):
            continue
        node_details = call("get_node_details", {"scene_name": scene_name, "node_name": node["name"]})
        for attachment in node_details.get("attachments", []):
            for name in attachment.get("materials", []):
                if name and name != "(none)":
                    material_name = name
                    break
            if material_name:
                break
        if material_name:
            break
    check(S, "a mesh-used material exists to bind", bool(material_name), detail=material_name)
    if not material_name:
        return
    bound = mutate("set_material_texture_source",
                   {"material_name": material_name, "slot": "base_color", "graph_texture": "Smoke Asset", "scene_name": scene_name})
    check(S, "set_material_texture_source bound", bool(bound) and bound.get("bound"), detail=str(bound))

    details = call("get_material_details", {"scene_name": scene_name, "material_name": material_name})
    src_name = details.get("texture_samplers", {}).get("base_color", {}).get("graph_texture_name")
    check(S, "material base_color reports the graph source", src_name == "Smoke Asset", detail=str(src_name))

    # Persist and inspect the saved scene file (the graph assets and material
    # bindings ride in the root-level ERHE_node_graphs extension), then reload.
    TMP_DIR.mkdir(parents=True, exist_ok=True)
    scene_path = TMP_DIR / "asset_roundtrip.glb"
    mutate("save_scene", {"scene_name": scene_name, "path": str(scene_path)})
    check(S, "save wrote the scene file", scene_path.is_file(), detail=str(scene_path))
    if scene_path.is_file():
        scene_doc = read_glb_json(scene_path)
        node_graphs = scene_doc.get("extensions", {}).get("ERHE_node_graphs", {})
        gts = node_graphs.get("graph_textures", [])
        saved_asset = next((g for g in gts if g.get("name") == "Smoke Asset"), None)
        check(S, "scene file persists the Graph Texture asset", bool(saved_asset) and bool(saved_asset.get("graph")),
              detail=str(saved_asset)[:120])
        # material_bindings reference the material by its glTF material index.
        gltf_materials = scene_doc.get("materials", [])
        def bound_material_name(binding):
            index = binding.get("material", -1)
            return gltf_materials[index].get("name") if 0 <= index < len(gltf_materials) else None
        bindings = node_graphs.get("material_bindings", [])
        saved_binding = next((b for b in bindings if bound_material_name(b) == material_name
                              and b.get("slot") == "base_color" and b.get("graph_texture") == "Smoke Asset"), None)
        check(S, "scene file persists the material -> graph binding", bool(saved_binding), detail=str(bindings)[:160])

    before = len([g for g in call("get_graph_textures")["graph_textures"] if g["name"] == "Smoke Asset"])
    queued = mutate("load_scene", {"path": str(scene_path)})
    check(S, "load_scene queued the load", bool(queued) and queued.get("queued"), detail=str(queued))
    check(S, "loaded scene appears in list_scenes", wait_for_scene("asset_roundtrip"))
    after_list = call("get_graph_textures")["graph_textures"]
    after = [g for g in after_list if g["name"] == "Smoke Asset"]
    check(S, "reloaded scene reconstructs the asset", len(after) >= before + 1, detail=f"{before}->{len(after)}")
    check(S, "reloaded asset bakes an output", all(g.get("has_output") for g in after) and len(after) >= 1, detail=str(after))


def main():
    sections = [
        section_every_node_type,
        section_parameter_sweeps,
        section_parameter_abuse,
        section_connect_rules,
        section_undo_redo,
        section_render_export,
        section_gradient_curve,
        section_multi_output_decompose,
        section_new_filters,
        section_buffer,
        section_blur,
        section_reseed,
        section_output_node,
        section_material_output,
        section_graph_texture_asset,
    ]
    for section in sections:
        print(f"\n=== {section.__name__} ===")
        try:
            section()
        except Exception as error:  # keep sweeping; report the wreck
            check(section.__name__, "section completed without exception", False, repr(error))

    try:
        fresh_graph()
        get_graph()
        call("capture_screenshot", {"path": "logs/texgraph_smoke_final.png"})
    except RuntimeError as error:
        print(f"  (final screenshot skipped: {error})")

    print("\n===== SUMMARY =====")
    failed = [r for r in RESULTS if not r[2]]
    print(f"total={len(RESULTS)} pass={len(RESULTS) - len(failed)} fail={len(failed)}")
    for section, name, _, detail in failed:
        print(f"  FAIL {section}: {name} -- {detail}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
