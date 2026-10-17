#!/usr/bin/env python3
"""Node -> body gate for Node_physics_system::apply_pending_node_poses().

usage: physics_node_pose_gate.py <editor binary> <out.json>

Runs one headless editor on the default scene with the manual simulation clock
(deterministic) and records where a dropped ball and its box end up after each
node -> body path: a static box carried by its parent node's edit (propagation,
no teleport of the child), a selected ball (kinematic physical) held still for a
second and then dragged, and an edit made while the simulation is paused. Run
it on two builds and compare the two files: the positions must be identical.
The run directories go under build_ninja_linux_vulkan_headless/physics_gate.
"""
import argparse, json, os, signal, subprocess, sys, time
from pathlib import Path
REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "scripts"))
import render_thread_bench as rtb  # noqa: E402

binary, out_path = Path(sys.argv[1]), Path(sys.argv[2])
root = REPO / "build_ninja_linux_vulkan_headless" / "physics_gate"
root.mkdir(parents=True, exist_ok=True)
run_dir = rtb.make_run_dir(argparse.Namespace(overrides=[], viewport_size=""), root, "gate_" + binary.name, ["erhe_graphics.json:force_disable_vsync=true"])
env = dict(os.environ)
env.update({"ERHE_AI_DRIVER": "1", "VK_DRIVER_FILES": "/usr/share/vulkan/icd.d/intel_icd.json", "VK_LOADER_LAYERS_DISABLE": "~implicit~"})
proc = subprocess.Popen([str(binary), "--commands", "config/editor/commands.json"], cwd=run_dir, env=env,
                        stdout=(run_dir / "editor_stdout.log").open("wb"), stderr=subprocess.STDOUT, start_new_session=True)
result = {"steps": []}
try:
    log = run_dir / "logs" / "log.txt"
    for _ in range(600):
        if proc.poll() is not None:
            raise SystemExit("editor exited early")
        if log.is_file() and "Main loop: completed frame 12" in log.read_text(errors="replace"):
            break
        time.sleep(0.5)
    else:
        raise SystemExit("editor not ready")

    def mcp(tool, args=None):
        r = subprocess.run(["python3", str(REPO / "scripts" / "mcp_call.py"), tool, json.dumps(args or {})], capture_output=True, text=True, cwd=REPO)
        if r.returncode != 0:
            raise SystemExit(f"{tool} {args} failed: {r.stdout[:400]} {r.stderr[:400]}")
        try:
            return json.loads(r.stdout)
        except json.JSONDecodeError:
            return {"raw": r.stdout.strip()}

    scenes = mcp("list_scenes")
    scene = (scenes["scenes"][0]["name"] if isinstance(scenes, dict) else scenes[0]["name"])
    def node_id(name):
        for _ in range(100):
            nodes = mcp("get_scene_nodes", {"scene_name": scene})["nodes"]
            hit = [n for n in nodes if n["name"] == name]
            if hit:
                return hit[0]["id"]
            time.sleep(0.05)
        raise SystemExit("node not found " + name)
    def advance(seconds):
        mcp("advance_time", {"seconds": seconds, "max_step_ms": 50})
        for _ in range(2000):
            if mcp("advance_time", {}).get("pending_seconds", 0) == 0:
                return
            time.sleep(0.02)
        raise SystemExit("advance did not drain")
    def state(nid, label):
        s = mcp("get_physics_state", {"scene_name": scene, "node_id": nid})
        pos = s.get("position") or s.get("world_position")
        d = mcp("get_node_details", {"scene_name": scene, "node_id": nid})
        node_pos = d.get("world_translation") or d.get("world_position") or d.get("position") or (d.get("transform") or {}).get("world_translation")
        rec = {"label": label, "body": pos, "node": node_pos, "node_keys": sorted(d.keys())[:12], "motion_mode": s.get("motion_mode"), "active": s.get("is_active", s.get("active"))}
        result["steps"].append(rec)
        print(label, json.dumps(rec), flush=True)
        return rec

    mcp("toggle_physics", {"enabled": True})
    mcp("advance_time", {"mode": "manual"})
    mcp("create_node", {"scene_name": scene, "name": "phys_parent", "position": [0, 0, 20]})
    parent = node_id("phys_parent")
    mcp("create_shape", {"scene_name": scene, "shape": "box", "name": "phys_box", "size": [2, 1, 2], "motion_mode": "static", "parent_node_id": parent, "position": [0, 0.5, 20]})
    box = node_id("phys_box")
    mcp("create_shape", {"scene_name": scene, "shape": "uv_sphere", "name": "phys_ball", "radius": 0.5, "motion_mode": "dynamic", "mass": 1.0, "position": [0, 4, 20]})
    ball = node_id("phys_ball")
    mcp("wake_physics_bodies", {"scene_name": scene, "node_ids": [ball]})
    advance(2.0)
    state(ball, "1 ball on box (expect y~1.5 x~0)")
    state(box, "1 box")

    # 2. carry: the parent's edit moves the box node through propagation; the box
    # body must follow (no teleport on the child), so the ball loses its support.
    mcp("set_node_transform", {"scene_name": scene, "node_id": parent, "translation": [3, 0, 20]})
    mcp("wake_physics_bodies", {"scene_name": scene, "node_ids": [ball]})
    advance(2.0)
    state(ball, "2a ball after box carried away (expect fallen: y << 1)")
    state(box, "2a box (node x 3)")
    mcp("set_node_transform", {"scene_name": scene, "node_id": ball, "translation": [3, 4, 20]})
    mcp("wake_physics_bodies", {"scene_name": scene, "node_ids": [ball]})
    advance(2.0)
    state(ball, "2b ball dropped on carried box (expect x~3 y~1.5)")

    # 3. selected = kinematic physical: held still, then dragged, then released.
    mcp("select_items", {"scene_name": scene, "ids": [ball]})
    advance(1.0)
    state(ball, "3a selected ball held (expect unchanged x~3 y~1.5, kinematic)")
    mcp("drag_selection", {"translation": [2, 0, 0], "frames": 10, "release": True})
    advance(0.5)
    state(ball, "3b selected ball dragged (expect x~5)")
    mcp("select_items", {"scene_name": scene, "ids": []})
    mcp("wake_physics_bodies", {"scene_name": scene, "node_ids": [ball]})
    advance(2.0)
    state(ball, "3c released ball off the box (expect fallen: y << 1)")

    # 4. edit while paused, applied on resume.
    mcp("toggle_physics", {"enabled": False})
    mcp("set_node_transform", {"scene_name": scene, "node_id": parent, "translation": [8, 0, 20]})
    mcp("set_node_transform", {"scene_name": scene, "node_id": ball, "translation": [8, 4, 20]})
    mcp("toggle_physics", {"enabled": True})
    mcp("wake_physics_bodies", {"scene_name": scene, "node_ids": [ball]})
    advance(2.0)
    state(ball, "4 ball on box moved while paused (expect x~8 y~1.5)")
    state(box, "4 box (node x 8)")
    out_path.write_text(json.dumps(result, indent=1, sort_keys=True))
    print("wrote", out_path)
finally:
    if proc.poll() is None:
        os.kill(proc.pid, signal.SIGKILL)
        proc.wait()
