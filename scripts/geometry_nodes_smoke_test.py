#!/usr/bin/env python3
"""Comprehensive geometry nodes smoke test.

Drives the in-editor MCP server (headless build) through the full
geometry nodes feature: every node type, parameter sweeps with undo/redo,
incremental evaluation proof (trace log), multi-link join, structural
churn with deep undo/redo, serialization round-trip, output node edge
cases, multi-link partial disconnects, invalid connect rejection
(type mismatch, self links, cycles), error-path serialization (malformed
files, bad group assets), out-of-range parameter abuse, output physics
edge cases, and stress chains. Prints a structured PASS/FAIL report.
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


def clear_graph():
    mutate("geometry_graph_clear")


def undo(n=1):
    for _ in range(n):
        mutate("undo")


def redo(n=1):
    for _ in range(n):
        mutate("redo")


def undo_depth():
    stack = call("get_undo_redo_stack")
    return len(stack.get("undo", []))


def scene_nodes():
    # Graph evaluation runs in the background (async-evaluation change);
    # get_geometry_graph waits for it to finish and its results to be
    # applied to the scene, so the scene query below sees the effects of
    # every previously issued graph mutation.
    get_graph()
    return call("get_scene_nodes", {"scene_name": "Default Scene"})


def scene_node_names():
    data = scene_nodes()
    names = []
    def walk(entries):
        for entry in entries:
            names.append(entry.get("name", ""))
            walk(entry.get("children", []))
    if isinstance(data, dict):
        walk(data.get("nodes", []))
    return names


def scene_node_by_name(name):
    data = scene_nodes()
    found = []
    def walk(entries):
        for entry in entries:
            if entry.get("name", "") == name:
                found.append(entry)
            walk(entry.get("children", []))
    if isinstance(data, dict):
        walk(data.get("nodes", []))
    return found


def log_size():
    return LOG_PATH.stat().st_size if LOG_PATH.is_file() else 0


def log_tail(start):
    with open(LOG_PATH, "r", encoding="utf-8", errors="replace") as f:
        f.seek(start)
        return f.read()


# ---------------------------------------------------------------- sections

def section_every_node_type():
    S = "node-types"
    clear_graph()

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
    check(S, "scene node exists", len(scene_node_by_name("Geometry Graph")) == 1)

    # Operation chain: box -> subdivide -> conway -> transform ->
    # triangulate -> normalize -> reverse -> repair -> output
    clear_graph()
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
    clear_graph()
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
    clear_graph()
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
    clear_graph()
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

    # Groups: author asset, then use it.
    clear_graph()
    gi = add_node("group_input")["id"]
    sd = add_node("subdivide")["id"]
    go = add_node("group_output")["id"]
    connect(gi, 0, sd, 0)
    connect(sd, 0, go, 0)
    call("geometry_graph_save", {"path": "res/editor/graphs/smoke_group.json"})
    clear_graph()
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

    clear_graph()
    box = add_node("box")["id"]
    output = add_node("output")["id"]
    connect(box, 0, output, 0)
    sweep(box, {"size": [0.1, 0.1, 0.1], "steps": [1, 1, 1], "power": 1.0},
               {"size": [4.0, 2.0, 1.0], "steps": [3, 2, 1], "power": 0.5},
          verify=lambda: geometry_counts(box)[0] > 26, label="box size/steps/power")

    clear_graph()
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
        clear_graph()
        node = add_node(prim)["id"]
        output = add_node("output")["id"]
        connect(node, 0, output, 0)
        params = node_by_id(get_graph(), node)["parameters"]
        print(f"  ({prim} parameters: {params})")
        a = edge_values(params, 0.3, 3, False)
        b = edge_values(params, 2.5, 12, True)
        sweep(node, a, b, verify=lambda n=node: geometry_counts(n)[0] > 0, label=f"{prim} all")

    # Subdivide: mode 0/1, iterations edge 0 and 6 (on a small box).
    clear_graph()
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
    clear_graph()
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
    clear_graph()
    box = add_node("box")["id"]
    tr = add_node("transform")["id"]
    output = add_node("output")["id"]
    connect(box, 0, tr, 0)
    connect(tr, 0, output, 0)
    sweep(tr, {"translation": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0]},
              {"translation": [5.0, -3.0, 2.0], "rotation": [45.0, 90.0, -30.0], "scale": [2.0, 0.5, 3.0]},
          verify=lambda: geometry_counts(tr) == (26, 24), label="transform trs")

    # Distribute / instance.
    clear_graph()
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
    clear_graph()
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
    clear_graph()
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
    clear_graph()
    box = add_node("box")["id"]
    output = add_node("output")["id"]
    connect(box, 0, output, 0)
    sweep(output, {"name": "Smoke A", "physics": False, "physics_motion": "static"},
                  {"name": "Smoke B", "physics": True, "physics_motion": "dynamic"},
          verify=lambda: len(scene_node_by_name("Smoke B")) == 1, label="output name/physics")
    node_entries = scene_node_by_name("Smoke B")
    check(S, "output physics attachment present", node_entries and "Node_physics" in node_entries[0].get("attachment_types", []),
          f"attachments={node_entries[0].get('attachment_types') if node_entries else None}")

    # Group path parameter round trip already covered in 6e verify; sweep empty/set.
    clear_graph()
    box = add_node("box")["id"]
    group = add_node("group")["id"]
    output = add_node("output")["id"]
    connect(box, 0, group, 0)
    connect(group, 0, output, 0)
    sweep(group, {"path": ""}, {"path": "res/editor/graphs/smoke_group.json"},
          verify=lambda: geometry_counts(group) == (98, 96), label="group path")


def section_incremental():
    S = "incremental"
    clear_graph()
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
    clear_graph()
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
    clear_graph()
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


def section_serialization():
    S = "serialization"
    clear_graph()
    # Build a graph containing EVERY node type.
    ids = {}
    for type_name in ("box", "sphere", "torus", "cone", "disc",
                      "subdivide", "conway", "transform", "triangulate", "normalize", "reverse", "repair",
                      "distribute", "instance", "realize", "join", "boolean",
                      "float", "integer", "vector", "math",
                      "output", "group_input", "group_output", "group"):
        ids[type_name] = add_node(type_name)["id"]
    connect(ids["box"], 0, ids["subdivide"], 0)
    connect(ids["subdivide"], 0, ids["conway"], 0)
    connect(ids["conway"], 0, ids["join"], 0)
    connect(ids["sphere"], 0, ids["join"], 0)
    connect(ids["torus"], 0, ids["transform"], 0)
    connect(ids["transform"], 0, ids["join"], 0)
    connect(ids["join"], 0, ids["output"], 0)
    connect(ids["cone"], 0, ids["triangulate"], 0)
    connect(ids["disc"], 0, ids["normalize"], 0)
    connect(ids["sphere"], 0, ids["distribute"], 0)
    connect(ids["distribute"], 0, ids["instance"], 0)
    connect(ids["box"], 0, ids["instance"], 1)
    connect(ids["instance"], 0, ids["realize"], 0)
    connect(ids["float"], 0, ids["math"], 0)
    connect(ids["box"], 0, ids["boolean"], 0)
    connect(ids["sphere"], 0, ids["boolean"], 1)
    connect(ids["group_input"], 0, ids["reverse"], 0)
    connect(ids["reverse"], 0, ids["group_output"], 0)
    set_param(ids["group"], {"path": "res/editor/graphs/smoke_group.json"})
    set_param(ids["box"], {"size": [1.5, 2.5, 0.5]})
    set_param(ids["distribute"], {"count": 33, "seed": 5})
    set_param(ids["output"], {"name": "Serialized"})

    graph_before = get_graph()
    nodes_before = len(graph_before["nodes"])
    links_before = len(graph_before["links"])

    call("geometry_graph_save", {"path": "res/editor/graphs/smoke_full.json"})
    clear_graph()
    check(S, "clear empties graph", len(get_graph()["nodes"]) == 0)
    call("geometry_graph_load", {"path": "res/editor/graphs/smoke_full.json"})
    graph_after = get_graph()
    check(S, "load restores node count", len(graph_after["nodes"]) == nodes_before,
          f"{len(graph_after['nodes'])}/{nodes_before}")
    check(S, "load restores link count", len(graph_after["links"]) == links_before,
          f"{len(graph_after['links'])}/{links_before}")
    names_before = sorted(node["name"] for node in graph_before["nodes"])
    names_after = sorted(node["name"] for node in graph_after["nodes"])
    check(S, "load restores node types", names_before == names_after)

    def params_of(name_substring):
        for node in graph_after["nodes"]:
            if name_substring in node["name"]:
                return node["parameters"]
        return None
    box_params = params_of("Box")
    dist_params = params_of("Distribute")
    check(S, "load restores parameters",
          box_params and box_params.get("size") == [1.5, 2.5, 0.5] and dist_params and dist_params.get("count") == 33,
          f"box={box_params} dist={dist_params}")
    check(S, "load restores scene mesh", len(scene_node_by_name("Serialized")) == 1)

    undo(1)  # undo load -> empty
    check(S, "undo load -> empty graph", len(get_graph()["nodes"]) == 0)
    undo(1)  # undo clear -> full graph
    check(S, "undo clear -> full graph back", len(get_graph()["nodes"]) == nodes_before)
    redo(2)  # redo clear + redo load -> loaded state
    check(S, "redo x2 -> loaded graph", len(get_graph()["nodes"]) == nodes_before)


def section_output_edge_cases():
    S = "output-node"
    clear_graph()
    box = add_node("box")["id"]
    out1 = add_node("output")["id"]
    out2 = add_node("output")["id"]
    connect(box, 0, out1, 0)
    connect(box, 0, out2, 0)
    set_param(out1, {"name": "Out One"})
    set_param(out2, {"name": "Out Two"})
    check(S, "two output nodes coexist",
          len(scene_node_by_name("Out One")) == 1 and len(scene_node_by_name("Out Two")) == 1)

    # Rename.
    set_param(out1, {"name": "Renamed One"})
    check(S, "rename moves scene node",
          len(scene_node_by_name("Renamed One")) == 1 and len(scene_node_by_name("Out One")) == 0)

    # Removal + undo.
    call("geometry_graph_remove_node", {"node_id": out2})
    gone = len(scene_node_by_name("Out Two")) == 0
    undo(1)
    back = len(scene_node_by_name("Out Two")) == 1
    check(S, "output removal clears scene node; undo restores it", gone and back, f"gone={gone} back={back}")

    # Disconnect input -> mesh primitives clear (scene node remains, no geometry).
    disconnect(box, 0, out1, 0)
    entries = scene_node_by_name("Renamed One")
    check(S, "disconnected output keeps scene node", len(entries) == 1)
    connect(box, 0, out1, 0)


def section_multilink_partial_disconnect():
    """Disconnect ONE link of a multi-link pin; the merged output must
    shrink by exactly that input's contribution, with undo/redo."""
    S = "multilink"

    # Join with 3 inputs.
    clear_graph()
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
    clear_graph()
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
    clear_graph()
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
    clear_graph()
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


def section_serialization_errors():
    """Malformed graph files and bad group assets must fail gracefully:
    MCP error, unchanged graph, no undo entries, no crash."""
    S = "serialization-errors"
    graphs_dir = pathlib.Path("res/editor/graphs")
    graphs_dir.mkdir(parents=True, exist_ok=True)

    clear_graph()
    box = add_node("box")["id"]
    output = add_node("output")["id"]
    connect(box, 0, output, 0)
    nodes_before = len(get_graph()["nodes"])
    links_before = len(get_graph()["links"])
    depth_before = undo_depth()

    def expect_load_error(name, path):
        error = call_expect_error("geometry_graph_load", {"path": path})
        graph = get_graph()
        check(S, name,
              (error is not None) and (len(graph["nodes"]) == nodes_before) and (len(graph["links"]) == links_before),
              f"error={error} nodes={len(graph['nodes'])} links={len(graph['links'])}")

    expect_load_error("load nonexistent path fails, graph unchanged", "res/editor/graphs/smoke_does_not_exist.json")

    (graphs_dir / "smoke_bad_json.json").write_text("this is not json {{{", encoding="utf-8")
    expect_load_error("load non-JSON file fails, graph unchanged", "res/editor/graphs/smoke_bad_json.json")

    (graphs_dir / "smoke_bad_version.json").write_text(
        json.dumps({"version": 99, "nodes": [], "links": []}), encoding="utf-8")
    expect_load_error("load unsupported version fails", "res/editor/graphs/smoke_bad_version.json")

    (graphs_dir / "smoke_bad_type.json").write_text(
        json.dumps({"version": 1, "nodes": [{"type": "warp_drive", "parameters": {}}], "links": []}),
        encoding="utf-8")
    expect_load_error("load unknown node type fails", "res/editor/graphs/smoke_bad_type.json")

    (graphs_dir / "smoke_bad_link_index.json").write_text(
        json.dumps({"version": 1, "nodes": [{"type": "box", "parameters": {}}],
                    "links": [{"source_node": 0, "source_slot": 0, "sink_node": 5, "sink_slot": 0}]}),
        encoding="utf-8")
    expect_load_error("load out-of-range link node index fails", "res/editor/graphs/smoke_bad_link_index.json")

    (graphs_dir / "smoke_bad_link_slot.json").write_text(
        json.dumps({"version": 1, "nodes": [{"type": "box", "parameters": {}}, {"type": "output", "parameters": {}}],
                    "links": [{"source_node": 0, "source_slot": 7, "sink_node": 1, "sink_slot": 0}]}),
        encoding="utf-8")
    expect_load_error("load out-of-range pin slot fails", "res/editor/graphs/smoke_bad_link_slot.json")

    (graphs_dir / "smoke_bad_link_key.json").write_text(
        json.dumps({"version": 1, "nodes": [{"type": "float", "parameters": {}}, {"type": "subdivide", "parameters": {}}],
                    "links": [{"source_node": 0, "source_slot": 0, "sink_node": 1, "sink_slot": 0}]}),
        encoding="utf-8")
    expect_load_error("load pin key mismatch link fails", "res/editor/graphs/smoke_bad_link_key.json")

    (graphs_dir / "smoke_bad_cycle.json").write_text(
        json.dumps({"version": 1,
                    "nodes": [{"type": "transform", "parameters": {}}, {"type": "transform", "parameters": {}}],
                    "links": [{"source_node": 0, "source_slot": 0, "sink_node": 1, "sink_slot": 0},
                              {"source_node": 1, "source_slot": 0, "sink_node": 0, "sink_slot": 0}]}),
        encoding="utf-8")
    expect_load_error("load cycle-forming links fails", "res/editor/graphs/smoke_bad_cycle.json")

    error = call_expect_error("geometry_graph_save", {"path": "res/editor/graphs"})
    check(S, "save to a directory path fails", error is not None, f"error={error}")
    check(S, "no undo entries from failed loads/saves", undo_depth() == depth_before,
          f"before={depth_before} after={undo_depth()}")

    # Group node error paths: bad path, non-graph file, no Group Output.
    clear_graph()
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

    # Author an asset without a Group Output node, then reference it.
    clear_graph()
    gi = add_node("group_input")["id"]
    sd = add_node("subdivide")["id"]
    connect(gi, 0, sd, 0)
    call("geometry_graph_save", {"path": "res/editor/graphs/smoke_group_no_output.json"})
    clear_graph()
    box = add_node("box")["id"]
    group = add_node("group")["id"]
    output = add_node("output")["id"]
    connect(box, 0, group, 0)
    connect(group, 0, output, 0)
    set_param(group, {"path": "res/editor/graphs/smoke_group_no_output.json"})
    payload = out_payload(group)
    check(S, "group asset without Group Output evaluates to empty",
          payload is not None and payload.get("type") == "empty", f"payload={payload}")

    # Nested groups: asset A contains a group node referencing asset B.
    clear_graph()
    gi = add_node("group_input")["id"]
    inner = add_node("group")["id"]
    go = add_node("group_output")["id"]
    connect(gi, 0, inner, 0)
    connect(inner, 0, go, 0)
    set_param(inner, {"path": "res/editor/graphs/smoke_group.json"})
    call("geometry_graph_save", {"path": "res/editor/graphs/smoke_group_nested.json"})
    clear_graph()
    box = add_node("box")["id"]
    group = add_node("group")["id"]
    output = add_node("output")["id"]
    connect(box, 0, group, 0)
    connect(group, 0, output, 0)
    set_param(group, {"path": "res/editor/graphs/smoke_group_nested.json"})
    g = geometry_counts(group)
    check(S, "nested group (A references B) applies inner subdivide", g == (98, 96), f"counts={g}")

    # Self-referencing group asset: the evaluation depth guard must break
    # the cycle (warn + empty output), not hang or crash.
    clear_graph()
    gi = add_node("group_input")["id"]
    inner = add_node("group")["id"]
    go = add_node("group_output")["id"]
    connect(gi, 0, inner, 0)
    connect(inner, 0, go, 0)
    set_param(inner, {"path": "res/editor/graphs/smoke_group_self.json"})
    call("geometry_graph_save", {"path": "res/editor/graphs/smoke_group_self.json"})
    clear_graph()
    box = add_node("box")["id"]
    group = add_node("group")["id"]
    output = add_node("output")["id"]
    connect(box, 0, group, 0)
    connect(group, 0, output, 0)
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
    clear_graph()
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
    clear_graph()
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
    clear_graph()
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
    clear_graph()
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

    clear_graph()
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

    clear_graph()
    math_node = add_node("math")["id"]
    set_param(math_node, {"operation": 99, "a": 3.0, "b": 4.0})
    payload = out_payload(math_node)
    check(S, "math operation 99 yields 0.0, no crash",
          payload is not None and payload.get("value") == 0.0, f"payload={payload}")
    check(S, "editor alive after parameter abuse", len(get_graph()["nodes"]) == 1)


def section_output_physics_edges():
    """Output node physics: attachment follows connect state, all motion
    modes work, duplicate scene node names coexist."""
    S = "output-physics"
    clear_graph()
    box = add_node("box")["id"]
    output = add_node("output")["id"]
    connect(box, 0, output, 0)
    set_param(output, {"name": "Phys Edge", "physics": True, "physics_motion": "dynamic"})

    def has_physics(name):
        entries = scene_node_by_name(name)
        return (len(entries) == 1) and ("Node_physics" in entries[0].get("attachment_types", []))

    check(S, "physics attachment present after enable", has_physics("Phys Edge"))

    # All three motion modes.
    for motion in ("static", "kinematic", "dynamic"):
        set_param(output, {"physics_motion": motion})
        params = node_by_id(get_graph(), output)["parameters"]
        check(S, f"physics motion mode '{motion}' applies and keeps attachment",
              (params.get("physics_motion") == motion) and has_physics("Phys Edge"), f"params={params}")

    # Disconnecting the input must remove the physics attachment (the
    # scene node itself remains).
    disconnect(box, 0, output, 0)
    entries = scene_node_by_name("Phys Edge")
    check(S, "disconnect removes physics attachment, keeps scene node",
          (len(entries) == 1) and ("Node_physics" not in entries[0].get("attachment_types", [])),
          f"attachments={entries[0].get('attachment_types') if entries else None}")
    connect(box, 0, output, 0)
    check(S, "reconnect restores physics attachment", has_physics("Phys Edge"))

    # Two output nodes with the SAME scene node name coexist.
    out2 = add_node("output")["id"]
    connect(box, 0, out2, 0)
    set_param(out2, {"name": "Phys Edge"})
    entries = scene_node_by_name("Phys Edge")
    check(S, "two outputs with the same name coexist", len(entries) == 2, f"count={len(entries)}")
    set_param(out2, {"name": "Phys Edge B"})
    check(S, "renaming one resolves the collision",
          len(scene_node_by_name("Phys Edge")) == 1 and len(scene_node_by_name("Phys Edge B")) == 1)


def section_stress():
    S = "stress"
    clear_graph()
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
    clear_graph()
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


def main():
    sections = [
        section_every_node_type,
        section_parameter_sweeps,
        section_incremental,
        section_structural_churn,
        section_serialization,
        section_output_edge_cases,
        section_multilink_partial_disconnect,
        section_invalid_connects,
        section_serialization_errors,
        section_parameter_abuse,
        section_output_physics_edges,
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
