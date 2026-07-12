#!/usr/bin/env python3
"""Comprehensive geometry nodes smoke test.

Drives the in-editor MCP server (headless build) through the full
geometry nodes feature: every node type, parameter sweeps with undo/redo,
incremental evaluation proof (trace log), multi-link join, structural
churn with deep undo/redo, output node edge cases, multi-link partial
disconnects, invalid connect rejection (type mismatch, self links,
cycles), bad group asset files, out-of-range parameter abuse, output
physics edge cases, the Graph Mesh asset + Geometry Graph Mesh attachment
(create/select/bind/re-bake/shared-bake/physics/unbind + scene v7
save/load round-trip), and stress chains. Prints a structured PASS/FAIL
report.

Geometry graphs only live in Graph_mesh content-library assets (no window
scratch graph, no graph file save/load): every block starts by creating +
selecting a fresh asset (fresh_graph()); persistence is scene save/load,
covered by section_graph_mesh_asset. The output node publishes a bake to
its owning asset - scene content appears only where an explicit
Geometry_graph_mesh attachment binds a scene node to the asset. Group
nodes still read subgraph JSON files; the test authors those files
directly (write_graph_file).
"""

import json
import pathlib
import sys
import time
import urllib.request

PORT = 8080
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


# Heavy graph evaluations (subdivide x6, realize of hundreds of
# instances) block the editor main thread longer than the MCP server's
# per-request wait; the queued request still executes when the main
# thread gets to it, only the HTTP response gives up early.
#
# call(): for QUERIES and other idempotent tools - retries freely until
# the server drains (deadline_s budget).
# mutate(): for mutations - issues the request ONCE; on a server-side
# timeout it polls with a cheap query until the server responds again,
# then treats the mutation as applied (never re-issues, which would
# double-execute and corrupt undo/redo counts).
def call(tool, args=None, timeout=180, deadline_s=900):
    deadline = time.time() + deadline_s
    while True:
        try:
            return call_once(tool, args, timeout=timeout)
        except RuntimeError as error:
            if not is_busy_error(error) or (time.time() > deadline):
                raise
            time.sleep(5.0)


def mutate(tool, args=None, deadline_s=900):
    try:
        return call_once(tool, args)
    except RuntimeError as error:
        if not is_busy_error(error):
            raise
        print(f"  (server busy on {tool}; waiting for the evaluation to finish)")
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


def add_node(type_name):
    return call("geometry_graph_add_node", {"type": type_name})


def connect(src, sslot, dst, dslot):
    return mutate("geometry_graph_connect", {
        "source_node_id": src, "source_slot": sslot, "sink_node_id": dst, "sink_slot": dslot})


# A rejected call returns an MCP error immediately (rejection happens
# before evaluation, so re-issuing on a busy server is harmless).
# Returns the error text, or None when the call unexpectedly succeeded.
# Busy errors are retried, not reported as the expected rejection.
def call_expect_error(tool, args=None):
    try:
        call(tool, args)
        return None
    except RuntimeError as error:
        return str(error)


def connect_expect_error(src, sslot, dst, dslot):
    return call_expect_error("geometry_graph_connect", {
        "source_node_id": src, "source_slot": sslot, "sink_node_id": dst, "sink_slot": dslot})


def disconnect(src, sslot, dst, dslot):
    return mutate("geometry_graph_disconnect", {
        "source_node_id": src, "source_slot": sslot, "sink_node_id": dst, "sink_slot": dslot})


def set_param(node_id, parameters):
    return mutate("geometry_graph_set_parameter", {"node_id": node_id, "parameters": parameters})


def get_graph():
    return call("get_geometry_graph")


def node_by_id(graph, node_id):
    for node in graph["nodes"]:
        if node["id"] == node_id:
            return node
    return None


def out_payload(node_id, slot=0):
    node = node_by_id(get_graph(), node_id)
    if node is None:
        return None
    payloads = node.get("output_payloads", [])
    return payloads[slot] if slot < len(payloads) else None


def geometry_counts(node_id, slot=0):
    payload = out_payload(node_id, slot)
    if payload is None or payload.get("type") != "geometry":
        return None
    return (payload["vertex_count"], payload["facet_count"])


_scene_name_cache = None
_fresh_count = 0
_current_graph_name = None


def scene_name_of_test():
    global _scene_name_cache
    if _scene_name_cache is None:
        _scene_name_cache = call("list_scenes")["scenes"][0]["name"]
    return _scene_name_cache


def fresh_graph():
    """Start the next block on a new, empty Graph_mesh asset (created in
    the content library; create_graph_mesh points the Geometry Graph window
    at it, so the window and the geometry_graph_* tools target the new asset
    - issue #252, no longer via the global selection). Graphs only live in
    the content library - there is no window scratch graph and no
    clear/load tool."""
    global _fresh_count, _current_graph_name
    _fresh_count += 1
    _current_graph_name = f"Smoke {_fresh_count}"
    mutate("create_graph_mesh", {"name": _current_graph_name, "scene_name": scene_name_of_test()})


def current_graph_has_bake():
    entries = call("get_graph_meshes", {"scene_name": scene_name_of_test()})["graph_meshes"]
    for entry in entries:
        if entry["name"] == _current_graph_name:
            return bool(entry.get("has_bake"))
    return False


def write_graph_file(path, nodes, links):
    """Author a group-asset graph JSON file (the v1 format
    Group_node::load_subgraph parses): nodes = [(type, parameters), ...],
    links = [(source_index, source_slot, sink_index, sink_slot), ...]."""
    doc = {
        "version": 1,
        "nodes": [{"type": t, "parameters": p} for (t, p) in nodes],
        "links": [{"source_node": a, "source_slot": b, "sink_node": c, "sink_slot": d} for (a, b, c, d) in links],
    }
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(doc), encoding="utf-8")


def ensure_group_assets():
    """The group-asset files the sections reference (authored directly -
    graph file save no longer exists)."""
    write_graph_file("res/editor/graphs/smoke_group.json",
                     [("group_input", {}), ("subdivide", {"mode": 0, "iterations": 1}), ("group_output", {})],
                     [(0, 0, 1, 0), (1, 0, 2, 0)])
    write_graph_file("res/editor/graphs/smoke_group_no_output.json",
                     [("group_input", {}), ("subdivide", {"mode": 0, "iterations": 1})],
                     [(0, 0, 1, 0)])
    # Asset A contains a group node referencing asset B (smoke_group.json).
    write_graph_file("res/editor/graphs/smoke_group_nested.json",
                     [("group_input", {}), ("group", {"path": "res/editor/graphs/smoke_group.json"}), ("group_output", {})],
                     [(0, 0, 1, 0), (1, 0, 2, 0)])
    # Self-referencing group asset: the evaluation depth guard must break it.
    write_graph_file("res/editor/graphs/smoke_group_self.json",
                     [("group_input", {}), ("group", {"path": "res/editor/graphs/smoke_group_self.json"}), ("group_output", {})],
                     [(0, 0, 1, 0), (1, 0, 2, 0)])


def undo(n=1):
    for _ in range(n):
        mutate("undo")


def redo(n=1):
    for _ in range(n):
        mutate("redo")


def undo_depth():
    stack = call("get_undo_redo_stack")
    return len(stack.get("undo", []))


def log_size():
    return LOG_PATH.stat().st_size if LOG_PATH.is_file() else 0


def log_tail(start):
    with open(LOG_PATH, "r", encoding="utf-8", errors="replace") as f:
        f.seek(start)
        return f.read()


# ---------------------------------------------------------------- sections

def section_every_node_type():
    S = "node-types"
    fresh_graph()

    # All five primitives into a multi-link join into output.
    prims = {}
    for type_name in ("box", "sphere", "torus", "cone", "disc"):
        prims[type_name] = add_node(type_name)["id"]
    join = add_node("join")["id"]
    output = add_node("output")["id"]
    for node_id in prims.values():
        connect(node_id, 0, join, 0)
    connect(join, 0, output, 0)

    total_v = 0
    for type_name, node_id in prims.items():
        counts = geometry_counts(node_id)
        check(S, f"{type_name} produces geometry", counts is not None and counts[0] > 0, f"counts={counts}")
        if counts:
            total_v += counts[0]
    join_counts = geometry_counts(join)
    check(S, "join (5 inputs) merges all vertices", join_counts is not None and join_counts[0] == total_v,
          f"join={join_counts} expected_v={total_v}")
    check(S, "output publishes a bake to the asset", current_graph_has_bake())

    # Operation chain: box -> subdivide -> conway -> transform ->
    # triangulate -> normalize -> reverse -> repair -> output
    fresh_graph()
    box = add_node("box")["id"]
    ops = {}
    for type_name in ("subdivide", "conway", "transform", "triangulate", "normalize", "reverse", "repair"):
        ops[type_name] = add_node(type_name)["id"]
    output = add_node("output")["id"]
    chain = [box] + list(ops.values()) + [output]
    for a, b in zip(chain, chain[1:]):
        connect(a, 0, b, 0)

    box_counts = geometry_counts(box)
    check(S, "box baseline (default box is 26/24)", box_counts == (26, 24), f"counts={box_counts}")
    sub_counts = geometry_counts(ops["subdivide"])
    check(S, "subdivide grows mesh", sub_counts is not None and sub_counts[0] > box_counts[0], f"counts={sub_counts}")
    for type_name in ("conway", "transform", "triangulate", "normalize", "reverse", "repair"):
        counts = geometry_counts(ops[type_name])
        check(S, f"{type_name} produces geometry", counts is not None and counts[0] > 0, f"counts={counts}")

    # Value nodes and math into a primitive parameter pin.
    fresh_graph()
    fv = add_node("float")["id"]
    iv = add_node("integer")["id"]
    vv = add_node("vector")["id"]
    math1 = add_node("math")["id"]
    math2 = add_node("math")["id"]
    box = add_node("box")["id"]
    tr = add_node("transform")["id"]
    output = add_node("output")["id"]
    set_param(fv, {"value": 2.0})
    connect(fv, 0, math1, 0)
    connect(fv, 0, math1, 1)          # 2 + 2 = 4
    connect(math1, 0, math2, 0)
    connect(math1, 0, math2, 1)       # 4 + 4 = 8
    connect(math2, 0, box, 0)         # x size 8
    set_param(vv, {"value": [0.0, 3.0, 0.0]})
    connect(box, 0, tr, 0)
    connect(vv, 0, tr, 1)             # translation pin driven by vector node
    connect(tr, 0, output, 0)
    m1 = out_payload(math1)
    m2 = out_payload(math2)
    check(S, "float -> math chain", m1 and m1.get("value") == 4.0 and m2 and m2.get("value") == 8.0,
          f"m1={m1} m2={m2}")
    check(S, "integer node outputs int", out_payload(iv) and out_payload(iv).get("type") == "int")
    tr_counts = geometry_counts(tr)
    check(S, "vector drives transform pin", tr_counts == (26, 24), f"counts={tr_counts}")

    # Instances: sphere -> distribute -> instance(box) -> realize -> output
    fresh_graph()
    sphere = add_node("sphere")["id"]
    dist = add_node("distribute")["id"]
    box = add_node("box")["id"]
    inst = add_node("instance")["id"]
    real = add_node("realize")["id"]
    output = add_node("output")["id"]
    connect(sphere, 0, dist, 0)
    connect(dist, 0, inst, 0)
    connect(box, 0, inst, 1)
    connect(inst, 0, real, 0)
    connect(real, 0, output, 0)
    set_param(dist, {"count": 50})
    p = out_payload(dist)
    check(S, "distribute outputs 50 points", p and p.get("type") == "points" and p.get("point_count") == 50, f"p={p}")
    q = out_payload(inst)
    check(S, "instance outputs 50 instances", q and q.get("type") == "instances" and q.get("instance_count") == 50, f"q={q}")
    r = geometry_counts(real)
    check(S, "realize flattens 50 boxes", r == (50 * 26, 50 * 24), f"counts={r}")

    # Boolean of two boxes.
    fresh_graph()
    box_a = add_node("box")["id"]
    box_b = add_node("box")["id"]
    tr = add_node("transform")["id"]
    boolean = add_node("boolean")["id"]
    output = add_node("output")["id"]
    set_param(tr, {"translation": [0.5, 0.5, 0.5]})
    connect(box_a, 0, boolean, 0)
    connect(box_b, 0, tr, 0)
    connect(tr, 0, boolean, 1)
    connect(boolean, 0, output, 0)
    b = geometry_counts(boolean)
    check(S, "boolean produces geometry", b is not None and b[0] > 0, f"counts={b}")

    # Groups: author the asset file directly, then use it.
    ensure_group_assets()
    fresh_graph()
    box = add_node("box")["id"]
    group = add_node("group")["id"]
    output = add_node("output")["id"]
    connect(box, 0, group, 0)
    connect(group, 0, output, 0)
    set_param(group, {"path": "res/editor/graphs/smoke_group.json"})
    g = geometry_counts(group)
    check(S, "group applies asset subgraph (CC on 26/24 box = 98/96)", g == (98, 96), f"counts={g}")

    call("capture_screenshot", {"path": "logs/smoke_nodes.png"})


def section_parameter_sweeps():
    """Sweep valid edge values on every parameter of every node type;
    verify re-evaluation and undo/redo of each change."""
    S = "param-sweep"

    def sweep(node_id, params_a, params_b, verify=None, label=""):
        # params_a then params_b; undo -> back to a-state; redo -> b-state
        set_param(node_id, params_a)
        state_a = node_by_id(get_graph(), node_id)["parameters"]
        set_param(node_id, params_b)
        state_b = node_by_id(get_graph(), node_id)["parameters"]
        ok = True
        for key, value in params_b.items():
            if state_b.get(key) != value and not isinstance(value, float):
                ok = False
        undo(1)
        state_after_undo = node_by_id(get_graph(), node_id)["parameters"]
        redo(1)
        state_after_redo = node_by_id(get_graph(), node_id)["parameters"]
        round_trip = (state_after_undo == state_a) and (state_after_redo == state_b)
        extra_ok = verify() if verify else True
        check(S, f"{label} sweep+undo/redo", ok and round_trip and extra_ok,
              f"a={state_a} b={state_b} undo={state_after_undo}")

    fresh_graph()
    box = add_node("box")["id"]
    output = add_node("output")["id"]
    connect(box, 0, output, 0)
    sweep(box, {"size": [0.1, 0.1, 0.1], "steps": [1, 1, 1], "power": 1.0},
               {"size": [4.0, 2.0, 1.0], "steps": [3, 2, 1], "power": 0.5},
          verify=lambda: geometry_counts(box)[0] > 26, label="box size/steps/power")

    fresh_graph()
    sphere = add_node("sphere")["id"]
    output = add_node("output")["id"]
    connect(sphere, 0, output, 0)
    params = node_by_id(get_graph(), sphere)["parameters"]
    print(f"  (sphere parameters: {params})")
    def edge_values(parameters, float_value, int_value, flip_bools):
        swept = {}
        for key, value in parameters.items():
            if isinstance(value, bool):
                swept[key] = (not value) if flip_bools else value
            elif isinstance(value, float):
                swept[key] = float_value
            elif isinstance(value, int):
                swept[key] = int_value
            else:
                swept[key] = value
        return swept

    params = node_by_id(get_graph(), sphere)["parameters"]
    a = edge_values(params, 0.25, 3, False)
    b = edge_values(params, 3.0, 16, True)
    sweep(sphere, a, b, verify=lambda: geometry_counts(sphere)[0] > 0, label="sphere all")

    for prim in ("torus", "cone", "disc"):
        fresh_graph()
        node = add_node(prim)["id"]
        output = add_node("output")["id"]
        connect(node, 0, output, 0)
        params = node_by_id(get_graph(), node)["parameters"]
        print(f"  ({prim} parameters: {params})")
        a = edge_values(params, 0.3, 3, False)
        b = edge_values(params, 2.5, 12, True)
        sweep(node, a, b, verify=lambda n=node: geometry_counts(n)[0] > 0, label=f"{prim} all")

    # Subdivide: mode 0/1, iterations edge 0 and 6 (on a small box).
    fresh_graph()
    box = add_node("box")["id"]
    sub = add_node("subdivide")["id"]
    output = add_node("output")["id"]
    connect(box, 0, sub, 0)
    connect(sub, 0, output, 0)
    sweep(sub, {"mode": 0, "iterations": 0}, {"mode": 1, "iterations": 2},
          verify=lambda: geometry_counts(sub)[0] > 26, label="subdivide mode/iterations")
    set_param(sub, {"mode": 0, "iterations": 0})
    counts_0 = geometry_counts(sub)
    check(S, "subdivide 0 iterations passes through", counts_0 == (26, 24), f"counts={counts_0}")

    # Conway: all 9 operators.
    fresh_graph()
    box = add_node("box")["id"]
    conway = add_node("conway")["id"]
    output = add_node("output")["id"]
    connect(box, 0, conway, 0)
    connect(conway, 0, output, 0)
    params = node_by_id(get_graph(), conway)["parameters"]
    print(f"  (conway parameters: {params})")
    all_ok = True
    for op_index in range(9):
        set_param(conway, {"operation": op_index})
        counts = geometry_counts(conway)
        if counts is None or counts[0] == 0:
            all_ok = False
            print(f"  conway operation {op_index}: FAILED counts={counts}")
        undo(1)
    check(S, "conway all 9 operators produce geometry (each undone)", all_ok)
    sweep(conway, {"kis_height": 0.0, "truncate_ratio": 0.1, "chamfer_ratio": 0.1, "gyro_ratio": 0.1},
                  {"kis_height": 1.0, "truncate_ratio": 0.45, "chamfer_ratio": 0.9, "gyro_ratio": 0.9},
          verify=lambda: geometry_counts(conway)[0] > 0, label="conway ratios")

    # Transform.
    fresh_graph()
    box = add_node("box")["id"]
    tr = add_node("transform")["id"]
    output = add_node("output")["id"]
    connect(box, 0, tr, 0)
    connect(tr, 0, output, 0)
    sweep(tr, {"translation": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]},
              {"translation": [5.0, -3.0, 2.0], "rotation": [45.0, 90.0, -30.0], "scale": [2.0, 0.5, 3.0]},
          verify=lambda: geometry_counts(tr) == (26, 24), label="transform trs")

    # Distribute / instance.
    fresh_graph()
    sphere = add_node("sphere")["id"]
    dist = add_node("distribute")["id"]
    box = add_node("box")["id"]
    inst = add_node("instance")["id"]
    real = add_node("realize")["id"]
    output = add_node("output")["id"]
    connect(sphere, 0, dist, 0)
    connect(dist, 0, inst, 0)
    connect(box, 0, inst, 1)
    connect(inst, 0, real, 0)
    connect(real, 0, output, 0)
    sweep(dist, {"count": 0, "seed": 0}, {"count": 200, "seed": 42},
          verify=lambda: out_payload(dist).get("point_count") == 200, label="distribute count/seed")
    sweep(inst, {"scale": 0.01, "align": True}, {"scale": 2.0, "align": False},
          verify=lambda: out_payload(inst).get("instance_count") == 200, label="instance scale/align")

    # Boolean all modes.
    fresh_graph()
    box_a = add_node("box")["id"]
    box_b = add_node("box")["id"]
    tr = add_node("transform")["id"]
    boolean = add_node("boolean")["id"]
    output = add_node("output")["id"]
    set_param(tr, {"translation": [0.5, 0.5, 0.5]})
    connect(box_a, 0, boolean, 0)
    connect(box_b, 0, tr, 0)
    connect(tr, 0, boolean, 1)
    connect(boolean, 0, output, 0)
    params = node_by_id(get_graph(), boolean)["parameters"]
    print(f"  (boolean parameters: {params})")
    all_ok = True
    for mode in range(3):
        set_param(boolean, {"operation": mode})
        counts = geometry_counts(boolean)
        if counts is None or counts[0] == 0:
            all_ok = False
            print(f"  boolean operation {mode}: FAILED counts={counts}")
    check(S, "boolean all modes produce geometry", all_ok)

    # Value nodes and math.
    fresh_graph()
    fv = add_node("float")["id"]
    iv = add_node("integer")["id"]
    vv = add_node("vector")["id"]
    math_node = add_node("math")["id"]
    connect(fv, 0, math_node, 0)
    sweep(fv, {"value": -100.0}, {"value": 100.0},
          verify=lambda: out_payload(fv).get("value") == 100.0, label="float value")
    sweep(iv, {"value": -5}, {"value": 9}, verify=lambda: out_payload(iv).get("value") == 9, label="integer value")
    sweep(vv, {"value": [-1.0, -2.0, -3.0]}, {"value": [1.0, 2.0, 3.0]}, label="vector value")
    params = node_by_id(get_graph(), math_node)["parameters"]
    print(f"  (math parameters: {params})")
    op_count_ok = True
    for op_index in list(range(0, 5)) + [9, 10]:  # add..power, sine, cosine
        try:
            set_param(math_node, {"operation": op_index, "a": 0.5, "b": 2.0})
            if out_payload(math_node).get("type") != "float":
                op_count_ok = False
                print(f"  math operation {op_index}: no float output")
            undo(1)
        except RuntimeError as error:
            op_count_ok = False
            print(f"  math operation {op_index}: {error}")
            break
    check(S, "math operation sweep (each undone)", op_count_ok)

    # Output node parameters.
    fresh_graph()
    box = add_node("box")["id"]
    output = add_node("output")["id"]
    connect(box, 0, output, 0)
    sweep(output, {"name": "Smoke A", "physics": False, "physics_motion": "static"},
                  {"name": "Smoke B", "physics": True, "physics_motion": "dynamic"},
          verify=current_graph_has_bake, label="output name/physics")

    # Group path parameter round trip already covered in 6e verify; sweep empty/set.
    ensure_group_assets()
    fresh_graph()
    box = add_node("box")["id"]
    group = add_node("group")["id"]
    output = add_node("output")["id"]
    connect(box, 0, group, 0)
    connect(group, 0, output, 0)
    sweep(group, {"path": ""}, {"path": "res/editor/graphs/smoke_group.json"},
          verify=lambda: geometry_counts(group) == (98, 96), label="group path")


def section_incremental():
    S = "incremental"
    fresh_graph()
    box = add_node("box")["id"]
    sub = add_node("subdivide")["id"]
    out1 = add_node("output")["id"]
    sphere = add_node("sphere")["id"]
    conway = add_node("conway")["id"]
    out2 = add_node("output")["id"]
    connect(box, 0, sub, 0)
    connect(sub, 0, out1, 0)
    connect(sphere, 0, conway, 0)
    connect(conway, 0, out2, 0)
    set_param(out2, {"name": "Chain Two"})
    time.sleep(0.5)

    mark = log_size()
    set_param(box, {"size": [2.0, 2.0, 2.0]})
    time.sleep(0.5)
    tail = log_tail(mark)
    eval_lines = [line for line in tail.splitlines() if "evaluating node" in line]
    chain1_ids = {str(box), str(sub), str(out1)}
    chain2_ids = {str(sphere), str(conway), str(out2)}
    evaluated_ids = set()
    for line in eval_lines:
        evaluated_ids.add(line.rsplit(" ", 1)[-1])
    touched_chain2 = evaluated_ids & chain2_ids
    check(S, "editing chain 1 re-evaluates only chain 1",
          evaluated_ids and not touched_chain2 and evaluated_ids <= chain1_ids | {"?"},
          f"evaluated={sorted(evaluated_ids)} chain1={sorted(chain1_ids)} chain2={sorted(chain2_ids)}")

    mark = log_size()
    set_param(conway, {"operation": 1})
    time.sleep(0.5)
    tail = log_tail(mark)
    eval_lines = [line for line in tail.splitlines() if "evaluating node" in line]
    evaluated_ids = {line.rsplit(" ", 1)[-1] for line in eval_lines}
    check(S, "editing chain 2 re-evaluates only conway+output",
          evaluated_ids and evaluated_ids <= {str(conway), str(out2)},
          f"evaluated={sorted(evaluated_ids)}")


def section_structural_churn():
    S = "churn"
    fresh_graph()
    box = add_node("box")["id"]
    sub = add_node("subdivide")["id"]
    output = add_node("output")["id"]
    connect(box, 0, sub, 0)
    connect(sub, 0, output, 0)

    # 12 further undoable operations: disconnect, reconnect, remove
    # subdivide, re-add path via direct box->output, parameter edits...
    disconnect(sub, 0, output, 0)                       # 6
    connect(box, 0, output, 0)                          # 7
    set_param(box, {"size": [2.0, 1.0, 1.0]})           # 8
    disconnect(box, 0, output, 0)                       # 9
    call("geometry_graph_remove_node", {"node_id": sub})  # 10
    connect(box, 0, output, 0)                          # 11
    set_param(box, {"size": [3.0, 3.0, 3.0]})           # 12
    tr = add_node("transform")["id"]                    # 13
    disconnect(box, 0, output, 0)                       # 14
    connect(box, 0, tr, 0)                              # 15
    connect(tr, 0, output, 0)                           # 16
    set_param(tr, {"scale": [2.0, 2.0, 2.0]})           # 17

    graph_end = get_graph()
    end_nodes = len(graph_end["nodes"])
    end_links = len(graph_end["links"])
    stack = call("get_undo_redo_stack")
    print(f"  (undo stack: {json.dumps(stack)[:300]})")

    undo(17)
    graph_zero = get_graph()
    check(S, "17 undos back to empty graph", len(graph_zero["nodes"]) == 0 and len(graph_zero["links"]) == 0,
          f"nodes={len(graph_zero['nodes'])} links={len(graph_zero['links'])}")
    redo(17)
    graph_redone = get_graph()
    check(S, "17 redos restore final graph", len(graph_redone["nodes"]) == end_nodes and len(graph_redone["links"]) == end_links,
          f"nodes={len(graph_redone['nodes'])}/{end_nodes} links={len(graph_redone['links'])}/{end_links}")
    tr_counts = geometry_counts(tr)
    check(S, "redone chain evaluates", tr_counts is not None and tr_counts[0] == 26, f"counts={tr_counts}")

    # Removal of a linked middle node restores links on undo.
    fresh_graph()
    box = add_node("box")["id"]
    sub = add_node("subdivide")["id"]
    output = add_node("output")["id"]
    connect(box, 0, sub, 0)
    connect(sub, 0, output, 0)
    links_before = len(get_graph()["links"])
    call("geometry_graph_remove_node", {"node_id": sub})
    links_removed = len(get_graph()["links"])
    undo(1)
    graph_restored = get_graph()
    check(S, "middle node removal + undo restores links",
          links_before == 2 and links_removed == 0 and len(graph_restored["links"]) == 2,
          f"before={links_before} removed={links_removed} restored={len(graph_restored['links'])}")


def section_output_edge_cases():
    """Output node edge cases under the asset model: the bake follows the
    output node's existence and connect state on a BOUND scene node (the
    output itself never creates scene content)."""
    S = "output-node"
    sn = scene_name_of_test()
    fresh_graph()
    asset_name = _current_graph_name
    box = add_node("box")["id"]
    out1 = add_node("output")["id"]
    connect(box, 0, out1, 0)
    get_graph()  # evaluation barrier
    check(S, "connected output publishes a bake", current_graph_has_bake())

    # Adding a graph never touches the scene: only an explicit bind does.
    mutate("create_node", {"name": "Out Edge Node", "scene_name": sn})
    check(S, "bound scene node created", wait_for_scene_node(sn, "Out Edge Node"))
    mutate("set_node_graph_mesh", {"node_name": "Out Edge Node", "graph_mesh": asset_name, "scene_name": sn})
    get_graph()
    mesh_atts = [a for a in node_attachments(sn, "Out Edge Node") if a.get("type") == "Mesh"]
    check(S, "bound node materializes the bake", len(mesh_atts) == 1 and mesh_atts[0].get("facet_count") == 24,
          f"atts={mesh_atts}")

    def bound_mesh_facets():
        mesh_atts = [a for a in node_attachments(sn, "Out Edge Node") if a.get("type") == "Mesh"]
        return mesh_atts[0].get("facet_count") if mesh_atts else None

    # Removing the output node publishes an empty bake -> the bound node's
    # controlled mesh clears (primitives dropped, attachment kept); undo
    # restores it.
    call("geometry_graph_remove_node", {"node_id": out1})
    get_graph()
    gone = not bound_mesh_facets()  # None (no mesh) or 0 (cleared)
    undo(1)
    get_graph()
    back = bound_mesh_facets() == 24
    check(S, "output removal clears the bound mesh; undo restores it", gone and back, f"gone={gone} back={back}")

    # Disconnecting the input publishes an empty bake (attachment stays,
    # controlled mesh clears); reconnect restores.
    disconnect(box, 0, out1, 0)
    get_graph()
    types = [a.get("type") for a in node_attachments(sn, "Out Edge Node")]
    facets = bound_mesh_facets()
    check(S, "disconnected output keeps attachment, clears mesh",
          ("Geometry_graph_mesh" in types) and not facets, f"types={types} facets={facets}")
    connect(box, 0, out1, 0)
    get_graph()
    check(S, "reconnect restores the bound mesh", bound_mesh_facets() == 24, f"facets={bound_mesh_facets()}")

    # Cleanup: unbind so later sections see a plain node.
    mutate("set_node_graph_mesh", {"node_name": "Out Edge Node", "graph_mesh": "", "scene_name": sn})


def section_multilink_partial_disconnect():
    """Disconnect ONE link of a multi-link pin; the merged output must
    shrink by exactly that input's contribution, with undo/redo."""
    S = "multilink"

    # Join with 3 inputs.
    fresh_graph()
    box = add_node("box")["id"]
    sphere = add_node("sphere")["id"]
    torus = add_node("torus")["id"]
    join = add_node("join")["id"]
    output = add_node("output")["id"]
    for node_id in (box, sphere, torus):
        connect(node_id, 0, join, 0)
    connect(join, 0, output, 0)
    box_counts = geometry_counts(box)
    sphere_counts = geometry_counts(sphere)
    torus_counts = geometry_counts(torus)
    full = (box_counts[0] + sphere_counts[0] + torus_counts[0],
            box_counts[1] + sphere_counts[1] + torus_counts[1])
    check(S, "join (3 inputs) merges all", geometry_counts(join) == full, f"join={geometry_counts(join)} expected={full}")
    disconnect(sphere, 0, join, 0)
    partial = (full[0] - sphere_counts[0], full[1] - sphere_counts[1])
    check(S, "disconnecting one join input shrinks by exactly that primitive",
          geometry_counts(join) == partial, f"join={geometry_counts(join)} expected={partial}")
    undo(1)
    check(S, "undo restores merged counts", geometry_counts(join) == full, f"join={geometry_counts(join)}")
    redo(1)
    check(S, "redo shrinks again", geometry_counts(join) == partial, f"join={geometry_counts(join)}")

    # Instance points pin: two distribute nodes into one instance node.
    fresh_graph()
    sphere = add_node("sphere")["id"]
    dist_a = add_node("distribute")["id"]
    dist_b = add_node("distribute")["id"]
    box = add_node("box")["id"]
    inst = add_node("instance")["id"]
    real = add_node("realize")["id"]
    output = add_node("output")["id"]
    connect(sphere, 0, dist_a, 0)
    connect(sphere, 0, dist_b, 0)
    set_param(dist_a, {"count": 20, "seed": 1})
    set_param(dist_b, {"count": 30, "seed": 2})
    connect(dist_a, 0, inst, 0)
    connect(dist_b, 0, inst, 0)
    connect(box, 0, inst, 1)
    connect(inst, 0, real, 0)
    connect(real, 0, output, 0)
    check(S, "instance points pin accumulates two distributes (20+30)",
          out_payload(inst).get("instance_count") == 50, f"inst={out_payload(inst)}")
    disconnect(dist_b, 0, inst, 0)
    check(S, "disconnecting one distribute shrinks instances to 20",
          out_payload(inst).get("instance_count") == 20, f"inst={out_payload(inst)}")
    undo(1)
    check(S, "undo restores 50 instances", out_payload(inst).get("instance_count") == 50, f"inst={out_payload(inst)}")

    # Realize instances pin: two instance nodes into one realize.
    fresh_graph()
    sphere = add_node("sphere")["id"]
    dist_a = add_node("distribute")["id"]
    dist_b = add_node("distribute")["id"]
    box = add_node("box")["id"]
    inst_a = add_node("instance")["id"]
    inst_b = add_node("instance")["id"]
    real = add_node("realize")["id"]
    output = add_node("output")["id"]
    connect(sphere, 0, dist_a, 0)
    connect(sphere, 0, dist_b, 0)
    set_param(dist_a, {"count": 20, "seed": 1})
    set_param(dist_b, {"count": 30, "seed": 2})
    connect(dist_a, 0, inst_a, 0)
    connect(dist_b, 0, inst_b, 0)
    connect(box, 0, inst_a, 1)
    connect(box, 0, inst_b, 1)
    connect(inst_a, 0, real, 0)
    connect(inst_b, 0, real, 0)
    connect(real, 0, output, 0)
    check(S, "realize pin accumulates two instance nodes (50 boxes)",
          geometry_counts(real) == (50 * 26, 50 * 24), f"real={geometry_counts(real)}")
    disconnect(inst_b, 0, real, 0)
    check(S, "disconnecting one instance node shrinks realize to 20 boxes",
          geometry_counts(real) == (20 * 26, 20 * 24), f"real={geometry_counts(real)}")
    undo(1)
    check(S, "undo restores 50 realized boxes",
          geometry_counts(real) == (50 * 26, 50 * 24), f"real={geometry_counts(real)}")


def section_invalid_connects():
    """Type-mismatched, self, and cycle-forming connects must be
    rejected: MCP error, no link created, no undo stack entry, and the
    evaluation must settle (no perpetual re-evaluation)."""
    S = "invalid-connect"
    fresh_graph()
    fv = add_node("float")["id"]
    box = add_node("box")["id"]
    sub = add_node("subdivide")["id"]
    math_node = add_node("math")["id"]
    tr_a = add_node("transform")["id"]
    tr_b = add_node("transform")["id"]
    tr_c = add_node("transform")["id"]
    connect(box, 0, sub, 0)          # valid baseline link
    connect(tr_a, 0, tr_b, 0)        # A -> B
    connect(tr_b, 0, tr_c, 0)        # B -> C
    links_before = len(get_graph()["links"])
    depth_before = undo_depth()

    error = connect_expect_error(fv, 0, sub, 0)  # float -> geometry
    check(S, "float -> geometry input rejected", error is not None, f"error={error}")
    error = connect_expect_error(box, 0, math_node, 0)  # geometry -> float
    check(S, "geometry -> float input rejected", error is not None, f"error={error}")
    error = connect_expect_error(box, 0, sub, 1)  # geometry -> int (iterations pin)
    check(S, "geometry -> int input rejected", error is not None, f"error={error}")
    error = connect_expect_error(sub, 0, sub, 0)  # self link
    check(S, "self connect rejected", error is not None, f"error={error}")
    error = connect_expect_error(tr_b, 0, tr_a, 0)  # 2-node cycle
    check(S, "two node cycle (B -> A) rejected", error is not None, f"error={error}")
    error = connect_expect_error(tr_c, 0, tr_a, 0)  # 3-node cycle
    check(S, "three node cycle (C -> A) rejected", error is not None, f"error={error}")

    links_after = len(get_graph()["links"])
    check(S, "no links created by rejected connects", links_after == links_before,
          f"before={links_before} after={links_after}")
    check(S, "no undo entries from rejected connects", undo_depth() == depth_before,
          f"before={depth_before} after={undo_depth()}")

    # The graph must settle: no sort failures, no per-frame re-evaluation.
    time.sleep(1.0)
    mark = log_size()
    time.sleep(1.5)
    tail = log_tail(mark)
    check(S, "evaluation settles after rejected connects",
          ("evaluating node" not in tail) and ("not acyclic" not in tail),
          f"tail={tail[-300:]}")


def section_group_asset_errors():
    """Bad group-asset files must fail gracefully: the group node
    evaluates to empty (warn in the log), never crashes; nested and
    self-referencing assets behave."""
    S = "group-asset-errors"
    graphs_dir = pathlib.Path("res/editor/graphs")
    graphs_dir.mkdir(parents=True, exist_ok=True)
    ensure_group_assets()
    (graphs_dir / "smoke_bad_json.json").write_text("this is not json {{{", encoding="utf-8")

    # Group node error paths: bad path, non-graph file, no Group Output.
    fresh_graph()
    box = add_node("box")["id"]
    group = add_node("group")["id"]
    output = add_node("output")["id"]
    connect(box, 0, group, 0)
    connect(group, 0, output, 0)
    set_param(group, {"path": "res/editor/graphs/smoke_does_not_exist.json"})
    payload = out_payload(group)
    check(S, "group with nonexistent asset evaluates to empty",
          payload is not None and payload.get("type") == "empty", f"payload={payload}")
    set_param(group, {"path": "res/editor/graphs/smoke_bad_json.json"})
    payload = out_payload(group)
    check(S, "group with non-graph asset evaluates to empty",
          payload is not None and payload.get("type") == "empty", f"payload={payload}")

    # An asset without a Group Output node produces no output.
    set_param(group, {"path": "res/editor/graphs/smoke_group_no_output.json"})
    payload = out_payload(group)
    check(S, "group asset without Group Output evaluates to empty",
          payload is not None and payload.get("type") == "empty", f"payload={payload}")

    # Nested groups: asset A contains a group node referencing asset B.
    set_param(group, {"path": "res/editor/graphs/smoke_group_nested.json"})
    g = geometry_counts(group)
    check(S, "nested group (A references B) applies inner subdivide", g == (98, 96), f"counts={g}")

    # Self-referencing group asset: the evaluation depth guard must break
    # the cycle (warn + empty output), not hang or crash.
    mark = log_size()
    set_param(group, {"path": "res/editor/graphs/smoke_group_self.json"})
    payload = out_payload(group)
    tail = log_tail(mark)
    check(S, "self-referencing group breaks at depth guard (warn + empty)",
          payload is not None and payload.get("type") == "empty" and ("nesting depth exceeds" in tail),
          f"payload={payload} warned={'nesting depth exceeds' in tail}")
    check(S, "editor alive after group error paths", len(get_graph()["nodes"]) == 3)


def section_parameter_abuse():
    """Out-of-range and degenerate parameter values must clamp or behave
    harmlessly - never crash."""
    S = "param-abuse"

    # Subdivide iterations clamp to [0, 6].
    fresh_graph()
    box = add_node("box")["id"]
    sub = add_node("subdivide")["id"]
    output = add_node("output")["id"]
    connect(box, 0, sub, 0)
    connect(sub, 0, output, 0)
    set_param(sub, {"iterations": -5})
    counts = geometry_counts(sub)
    check(S, "subdivide iterations -5 clamps to 0 (pass-through)", counts == (26, 24), f"counts={counts}")
    set_param(sub, {"iterations": 99})
    counts = geometry_counts(sub)
    check(S, "subdivide iterations 99 clamps to 6", counts is not None and counts[1] == 24 * (4 ** 6),
          f"counts={counts}")
    set_param(sub, {"iterations": 0})

    # Distribute count clamps to >= 0; empty points flow downstream.
    fresh_graph()
    sphere = add_node("sphere")["id"]
    dist = add_node("distribute")["id"]
    box = add_node("box")["id"]
    inst = add_node("instance")["id"]
    real = add_node("realize")["id"]
    output = add_node("output")["id"]
    connect(sphere, 0, dist, 0)
    connect(dist, 0, inst, 0)
    connect(box, 0, inst, 1)
    connect(inst, 0, real, 0)
    connect(real, 0, output, 0)
    set_param(dist, {"count": -10})
    payload = out_payload(dist)
    check(S, "distribute count -10 clamps to 0 points",
          payload is not None and payload.get("point_count") == 0, f"payload={payload}")
    payload = out_payload(real)
    check(S, "realize of empty instances evaluates to empty",
          payload is not None and payload.get("type") == "empty", f"payload={payload}")

    # Instance scale 0 produces degenerate but valid geometry.
    set_param(dist, {"count": 10})
    set_param(inst, {"scale": 0.0})
    counts = geometry_counts(real)
    check(S, "instance scale 0 realizes degenerate but valid geometry",
          counts == (10 * 26, 10 * 24), f"counts={counts}")

    # Box size 0 / negative.
    fresh_graph()
    box = add_node("box")["id"]
    output = add_node("output")["id"]
    connect(box, 0, output, 0)
    set_param(box, {"size": [0.0, 0.0, 0.0]})
    counts = geometry_counts(box)
    check(S, "box size 0 survives (degenerate geometry)", counts == (26, 24), f"counts={counts}")
    set_param(box, {"size": [-2.0, 1.0, 1.0]})
    counts = geometry_counts(box)
    check(S, "box negative size survives", counts == (26, 24), f"counts={counts}")

    # Out-of-range operation indices: no enum case matches; the node
    # must produce an empty/zero result, not crash.
    fresh_graph()
    box = add_node("box")["id"]
    conway = add_node("conway")["id"]
    output = add_node("output")["id"]
    connect(box, 0, conway, 0)
    connect(conway, 0, output, 0)
    set_param(conway, {"operation": 42})
    counts = geometry_counts(conway)
    check(S, "conway operation 42 yields empty geometry, no crash", counts == (0, 0), f"counts={counts}")
    set_param(conway, {"operation": 0})
    counts = geometry_counts(conway)
    check(S, "conway recovers after out-of-range operation", counts is not None and counts[0] > 0, f"counts={counts}")

    fresh_graph()
    box_a = add_node("box")["id"]
    box_b = add_node("box")["id"]
    boolean = add_node("boolean")["id"]
    output = add_node("output")["id"]
    connect(box_a, 0, boolean, 0)
    connect(box_b, 0, boolean, 1)
    connect(boolean, 0, output, 0)
    set_param(boolean, {"operation": 99})
    counts = geometry_counts(boolean)
    check(S, "boolean operation 99 yields empty geometry, no crash", counts == (0, 0), f"counts={counts}")

    fresh_graph()
    math_node = add_node("math")["id"]
    set_param(math_node, {"operation": 99, "a": 3.0, "b": 4.0})
    payload = out_payload(math_node)
    check(S, "math operation 99 yields 0.0, no crash",
          payload is not None and payload.get("value") == 0.0, f"payload={payload}")
    check(S, "editor alive after parameter abuse", len(get_graph()["nodes"]) == 1)


def section_output_physics_edges():
    """Output node physics under the asset model: the physics flags travel
    with the bake to a BOUND scene node's Node_physics attachment and
    follow the graph's connect state."""
    S = "output-physics"
    sn = scene_name_of_test()
    fresh_graph()
    asset_name = _current_graph_name
    box = add_node("box")["id"]
    output = add_node("output")["id"]
    connect(box, 0, output, 0)
    set_param(output, {"name": "Phys Edge", "physics": True, "physics_motion": "dynamic"})

    mutate("create_node", {"name": "Phys Edge Node", "scene_name": sn})
    check(S, "bound scene node created", wait_for_scene_node(sn, "Phys Edge Node"))
    mutate("set_node_graph_mesh", {"node_name": "Phys Edge Node", "graph_mesh": asset_name, "scene_name": sn})
    get_graph()

    def has_physics():
        types = [a.get("type") for a in node_attachments(sn, "Phys Edge Node")]
        return "Node_physics" in types

    check(S, "physics attachment present after enable", has_physics())

    # All three motion modes.
    for motion in ("static", "kinematic", "dynamic"):
        set_param(output, {"physics_motion": motion})
        get_graph()
        params = node_by_id(get_graph(), output)["parameters"]
        check(S, f"physics motion mode '{motion}' applies and keeps attachment",
              (params.get("physics_motion") == motion) and has_physics(), f"params={params}")

    # Disconnecting the input publishes an empty bake -> the physics
    # attachment is removed (the bound scene node itself remains).
    disconnect(box, 0, output, 0)
    get_graph()
    check(S, "disconnect removes physics attachment, keeps scene node",
          wait_for_scene_node(sn, "Phys Edge Node") and not has_physics(),
          f"attachments={node_attachments(sn, 'Phys Edge Node')}")
    connect(box, 0, output, 0)
    get_graph()
    check(S, "reconnect restores physics attachment", has_physics())

    # Disabling physics removes the attachment from the bound node.
    set_param(output, {"physics": False})
    get_graph()
    check(S, "physics disable removes the attachment", not has_physics())

    # Cleanup: unbind so later sections see a plain node.
    mutate("set_node_graph_mesh", {"node_name": "Phys Edge Node", "graph_mesh": "", "scene_name": sn})


def section_stress():
    S = "stress"
    fresh_graph()
    box = add_node("box")["id"]
    sub = add_node("subdivide")["id"]
    output = add_node("output")["id"]
    connect(box, 0, sub, 0)
    connect(sub, 0, output, 0)
    start = time.time()
    set_param(sub, {"iterations": 5})
    counts_5 = geometry_counts(sub)
    t5 = time.time() - start
    check(S, "subdivide x5 on box", counts_5 is not None and counts_5[1] == 24 * (4 ** 5), f"counts={counts_5} t={t5:.1f}s")
    start = time.time()
    set_param(sub, {"iterations": 6})
    counts_6 = geometry_counts(sub)
    t6 = time.time() - start
    check(S, "subdivide x6 on box", counts_6 is not None and counts_6[1] == 24 * (4 ** 6), f"counts={counts_6} t={t6:.1f}s")
    print(f"  (subdivide timings: x5 {t5:.1f}s, x6 {t6:.1f}s)")

    # Boolean of two subdivided meshes.
    fresh_graph()
    box_a = add_node("box")["id"]
    sub_a = add_node("subdivide")["id"]
    box_b = add_node("box")["id"]
    sub_b = add_node("subdivide")["id"]
    tr = add_node("transform")["id"]
    boolean = add_node("boolean")["id"]
    output = add_node("output")["id"]
    connect(box_a, 0, sub_a, 0)
    connect(box_b, 0, sub_b, 0)
    connect(sub_b, 0, tr, 0)
    connect(sub_a, 0, boolean, 0)
    connect(tr, 0, boolean, 1)
    connect(boolean, 0, output, 0)
    set_param(tr, {"translation": [0.6, 0.4, 0.5]})
    start = time.time()
    set_param(sub_a, {"iterations": 3})
    set_param(sub_b, {"iterations": 3})
    counts = geometry_counts(boolean)
    tb = time.time() - start
    check(S, "boolean of two subdivided (x3) meshes", counts is not None and counts[0] > 0, f"counts={counts} t={tb:.1f}s")
    print(f"  (boolean of two subdiv3 timing: {tb:.1f}s)")

    call("capture_screenshot", {"path": "logs/smoke_stress.png"})


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


def node_attachments(scene_name, node_name):
    details = call("get_node_details", {"scene_name": scene_name, "node_name": node_name})
    return details.get("attachments", []) if isinstance(details, dict) else []


def wait_for_scene_node(scene_name, node_name, tries=50):
    # create_node queues the insert operation; it lands on a later frame's
    # request batch, so poll rather than assuming same-batch visibility.
    for _ in range(tries):
        data = call("get_scene_nodes", {"scene_name": scene_name})
        names = []
        def walk(entries):
            for entry in entries:
                names.append(entry.get("name", ""))
                walk(entry.get("children", []))
        if isinstance(data, dict):
            walk(data.get("nodes", []))
        if node_name in names:
            return True
        time.sleep(0.1)
    return False


def section_graph_mesh_asset():
    """Graph Mesh as a content-library asset + Geometry Graph Mesh node
    attachment: create + select, edit the selected asset, bind scene nodes,
    live re-bake propagation, shared bake, physics-through-bake, unbind, and
    scene save/load round-trip (scene file v7) without duplicating the baked
    products."""
    S = "graph-mesh-asset"

    scenes = call("list_scenes")["scenes"]
    if not scenes:
        check(S, "a scene exists", False)
        return
    scene_name = scenes[0]["name"]

    created = mutate("create_graph_mesh", {"name": "Smoke GM", "scene_name": scene_name})
    check(S, "create_graph_mesh creates the asset", bool(created) and created.get("created"), detail=str(created))
    asset_id = created.get("id") if created else None
    graph = get_graph()
    check(S, "geometry_graph tools target the created asset",
          (graph.get("graph_mesh_name") == "Smoke GM") and (graph.get("selected") is True))

    box = add_node("box")
    out = add_node("output")
    connect(box["id"], 0, out["id"], 0)
    get_graph()  # evaluation barrier
    entries = [e for e in call("get_graph_meshes", {"scene_name": scene_name})["graph_meshes"] if e["name"] == "Smoke GM"]
    check(S, "asset publishes a bake after evaluation", len(entries) == 1 and entries[0].get("has_bake"), detail=str(entries))

    mutate("create_node", {"name": "Smoke GM Node", "scene_name": scene_name})
    check(S, "bound scene node created", wait_for_scene_node(scene_name, "Smoke GM Node"))
    bound = mutate("set_node_graph_mesh", {"node_name": "Smoke GM Node", "graph_mesh": "Smoke GM", "scene_name": scene_name})
    check(S, "set_node_graph_mesh binds", bool(bound) and bound.get("bound"), detail=str(bound))
    get_graph()
    atts = node_attachments(scene_name, "Smoke GM Node")
    gm_atts   = [a for a in atts if a.get("type") == "Geometry_graph_mesh"]
    mesh_atts = [a for a in atts if a.get("type") == "Mesh"]
    check(S, "attachment back-references the asset", len(gm_atts) == 1 and gm_atts[0].get("graph_mesh") == "Smoke GM", detail=str(gm_atts))
    check(S, "controlled mesh carries the box geometry", len(mesh_atts) == 1 and mesh_atts[0].get("facet_count") == 24, detail=str(mesh_atts))

    # Live link: a graph edit re-bakes and re-renders every bound node.
    sub = add_node("subdivide")
    disconnect(box["id"], 0, out["id"], 0)
    connect(box["id"], 0, sub["id"], 0)
    connect(sub["id"], 0, out["id"], 0)
    get_graph()
    mesh_atts = [a for a in node_attachments(scene_name, "Smoke GM Node") if a.get("type") == "Mesh"]
    check(S, "graph edit re-bakes the bound node's mesh", len(mesh_atts) == 1 and mesh_atts[0].get("facet_count") == 96, detail=str(mesh_atts))

    # A second node bound to the same asset picks up the existing bake at
    # bind time (no re-evaluation) and shares the GPU primitive.
    mutate("create_node", {"name": "Smoke GM Node 2", "scene_name": scene_name})
    check(S, "second scene node created", wait_for_scene_node(scene_name, "Smoke GM Node 2"))
    mutate("set_node_graph_mesh", {"node_name": "Smoke GM Node 2", "graph_mesh": "Smoke GM", "scene_name": scene_name})
    mesh_atts = [a for a in node_attachments(scene_name, "Smoke GM Node 2") if a.get("type") == "Mesh"]
    check(S, "second node shares the existing bake at bind time", len(mesh_atts) == 1 and mesh_atts[0].get("facet_count") == 96, detail=str(mesh_atts))

    # Binding a node that ALREADY has a Mesh (and Node_physics, from brush
    # placement) must ADOPT them - a node has exactly one attachment of
    # each type - not attach duplicates. The graph's bake replaces the
    # adopted mesh's primitives; with graph physics off, the adopted
    # physics is removed (the bake dictates the physics state).
    mutate("create_shape", {"shape": "box", "name": "Smoke GM Shape", "scene_name": scene_name})
    check(S, "shape node created", wait_for_scene_node(scene_name, "Smoke GM Shape"))
    mutate("set_node_graph_mesh", {"node_name": "Smoke GM Shape", "graph_mesh": "Smoke GM", "scene_name": scene_name})
    get_graph()
    atts = node_attachments(scene_name, "Smoke GM Shape")
    mesh_atts = [a for a in atts if a.get("type") == "Mesh"]
    phys_atts = [a for a in atts if a.get("type") == "Node_physics"]
    check(S, "binding adopts the existing mesh (exactly one Mesh attachment, re-baked)",
          len(mesh_atts) == 1 and mesh_atts[0].get("facet_count") == 96, detail=str(atts))
    check(S, "graph without physics removes the adopted physics", len(phys_atts) == 0, detail=str(phys_atts))
    mutate("set_node_graph_mesh", {"node_name": "Smoke GM Shape", "graph_mesh": "", "scene_name": scene_name})

    # Issue #252: the window TARGET (not the global selection) drives which
    # graph the tools edit. Clearing / setting the target is explicit now.
    mutate("set_geometry_graph_target", {"graph_mesh": ""})
    check(S, "cleared target -> tools report the empty state", get_graph().get("selected") is False)
    mutate("set_geometry_graph_target", {"graph_mesh": "Smoke GM", "scene_name": scene_name})
    check(S, "asset re-targetable by name", get_graph().get("graph_mesh_name") == "Smoke GM")

    # Physics travels with the bake to every bound node.
    set_param(out["id"], {"physics": True})
    get_graph()
    types = [a.get("type") for a in node_attachments(scene_name, "Smoke GM Node 2")]
    check(S, "physics enable materializes Node_physics on bound nodes", "Node_physics" in types, detail=str(types))
    set_param(out["id"], {"physics": False})
    get_graph()
    types = [a.get("type") for a in node_attachments(scene_name, "Smoke GM Node 2")]
    check(S, "physics disable removes Node_physics from bound nodes", "Node_physics" not in types, detail=str(types))

    # Unbind removes the attachment and its controlled products.
    mutate("set_node_graph_mesh", {"node_name": "Smoke GM Node 2", "graph_mesh": "", "scene_name": scene_name})
    types = [a.get("type") for a in node_attachments(scene_name, "Smoke GM Node 2")]
    check(S, "unbind removes attachment and controlled mesh", ("Geometry_graph_mesh" not in types) and ("Mesh" not in types), detail=str(types))

    # Scene save/load round-trip (erhe-authored glTF scene file; the graph
    # assets and bindings ride in the root-level ERHE_node_graphs extension).
    # The baked products must NOT be double-persisted as ordinary mesh /
    # physics records on the bound node.
    scene_path = pathlib.Path("res/editor/scenes/smoke_graph_mesh.glb")
    mutate("save_scene", {"scene_name": scene_name, "path": str(scene_path)})
    check(S, "save wrote the scene file", scene_path.is_file(), detail=str(scene_path))
    if scene_path.is_file():
        doc = read_glb_json(scene_path)
        node_graphs = doc.get("extensions", {}).get("ERHE_node_graphs", {})
        saved_assets = [g for g in node_graphs.get("graph_meshes", []) if g.get("name") == "Smoke GM" and g.get("graph")]
        check(S, "ERHE_node_graphs persists the Graph Mesh asset", len(saved_assets) == 1, detail=str(saved_assets)[:120])
        bindings = node_graphs.get("node_bindings", [])
        check(S, "ERHE_node_graphs persists the node binding", len(bindings) == 1 and bindings[0].get("graph_mesh") == "Smoke GM", detail=str(bindings))
        if len(bindings) == 1:
            gltf_nodes = doc.get("nodes", [])
            node_index = bindings[0].get("node", -1)
            bound_node = gltf_nodes[node_index] if 0 <= node_index < len(gltf_nodes) else {}
            node_extensions = bound_node.get("extensions", {})
            check(S, "controlled mesh is not double-persisted", "mesh" not in bound_node, detail=str(bound_node)[:120])
            has_physics = ("KHR_physics_rigid_bodies" in node_extensions) or ("ERHE_physics" in node_extensions)
            check(S, "controlled physics is not double-persisted", not has_physics, detail=str(sorted(node_extensions.keys())))

    before = len([g for g in call("get_graph_meshes")["graph_meshes"] if g["name"] == "Smoke GM"])
    queued = mutate("load_scene", {"path": str(scene_path)})
    check(S, "load_scene queued the load", bool(queued) and queued.get("queued"), detail=str(queued))
    check(S, "loaded scene appears in list_scenes", wait_for_scene("smoke_graph_mesh"))
    get_graph()  # barrier: the loaded graph evaluates and pushes to its binding
    after = [g for g in call("get_graph_meshes")["graph_meshes"] if g["name"] == "Smoke GM"]
    check(S, "reloaded scene reconstructs the asset", len(after) >= before + 1, detail=f"{before}->{len(after)}")
    check(S, "reloaded asset re-bakes", len(after) >= 1 and all(g.get("has_bake") for g in after), detail=str(after))

    # Cleanup: unbind the remaining node, drop the scene file, clear the target.
    mutate("set_node_graph_mesh", {"node_name": "Smoke GM Node", "graph_mesh": "", "scene_name": scene_name})
    mutate("set_geometry_graph_target", {"graph_mesh": ""})
    scene_path.unlink(missing_ok=True)
    check(S, "empty state after cleanup", get_graph().get("selected") is False)


def main():
    sections = [
        section_every_node_type,
        section_parameter_sweeps,
        section_incremental,
        section_structural_churn,
        section_output_edge_cases,
        section_multilink_partial_disconnect,
        section_invalid_connects,
        section_group_asset_errors,
        section_parameter_abuse,
        section_output_physics_edges,
        section_graph_mesh_asset,
        section_stress,
    ]
    for section in sections:
        print(f"\n=== {section.__name__} ===")
        try:
            section()
        except Exception as error:  # keep sweeping; report the wreck
            check(section.__name__, "section completed without exception", False, repr(error))

    call("capture_screenshot", {"path": "logs/smoke_final.png"})

    print("\n===== SUMMARY =====")
    failed = [r for r in RESULTS if not r[2]]
    print(f"total={len(RESULTS)} pass={len(RESULTS) - len(failed)} fail={len(failed)}")
    for section, name, _, detail in failed:
        print(f"  FAIL {section}: {name} -- {detail}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
