"""Nsight Graphics GPU Trace or Graphics Capture of the windowed editor on a scene
state set up over MCP.

The editor is launched by ngfx with the activity injected and waiting for the
application (GPU Trace: --start-with-ngfx-sdk --stop-with-ngfx-sdk; Graphics
Capture: --hotkey-capture), so nothing is traced or captured until the editor
asks for it (doc/editor/nsight_graphics.md).
The script waits for the editor's MCP server, opens the scene, sets the camera
and import root like scripts/bistro_frame_bench.py, isolates the scene's
viewport, makes the --mcp calls, waits for the deferred loading to settle, and
then calls start_nsight_gpu_trace or start_nsight_graphics_capture. Every wait
is on a condition, not a timer: the editor's ready line, get_async_status idle,
get_nsight_status (trace stopped / capture file written), and for a trace the
exit of ngfx (which saves and exports the report). The editor exits as soon as
the result is written, so a slow configuration costs only its frames plus the
setup.

The editor must be built with ERHE_NSIGHT_GRAPHICS_SDK_DIR (Vulkan backend),
and the NVIDIA driver must allow GPU performance counters for the user
(RmProfilingAdminOnly 0 in /proc/driver/nvidia/params on Linux).

Examples:
  python3 scripts/nsight_graphics.py --ngfx <nsight>/host/linux-desktop-nomad-x64/ngfx \\
      --editor build/Ninja_Vulkan_Release/bin/editor --out-dir logs/gpu_trace_occlusion_off \\
      --mcp 'set_culling_classification={"mode": "gpu"}' --mcp 'set_occlusion_culling={"enabled": false}'
  python3 scripts/nsight_graphics.py --activity capture --out-dir logs/bistro_capture
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import time

import bistro_frame_bench as bench

REPO_ROOT = bench.REPO_ROOT
FINAL_STATES = ("stopped", "failed")
NGFX_ACTIVITY_ARGUMENTS = {
    "trace":   ["--activity", "GPU Trace Profiler", "--start-with-ngfx-sdk", "--stop-with-ngfx-sdk", "--auto-export"],
    "capture": ["--activity", "Graphics Capture", "--hotkey-capture"],
}


def find_ngfx(argument):
    candidates = [argument, os.environ.get("ERHE_NGFX", ""), shutil.which("ngfx") or ""]
    for candidate in candidates:
        if candidate and os.path.isfile(candidate):
            return candidate
    sys.exit("ngfx not found: pass --ngfx or set ERHE_NGFX")


def find_editor_pid(editor, launched_after):
    # ngfx starts the editor itself: the process whose executable is the
    # editor (ngfx's own command line names the editor too, so the command
    # line does not identify it) and that started after the launch.
    editor_path = os.path.realpath(editor)
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        try:
            if os.path.realpath(f"/proc/{entry}/exe") != editor_path:
                continue
            started = os.stat(f"/proc/{entry}").st_mtime
        except OSError:
            continue
        if started >= launched_after - 1.0:
            return int(entry)
    return None


class Editor_process:
    def __init__(self, pid):
        self.pid = pid

    def poll(self):
        return None if os.path.exists(f"/proc/{self.pid}") else 1


def wait_for(predicate, timeout_s, interval_s=0.2):
    start = time.time()
    while (time.time() - start) < timeout_s:
        value = predicate()
        if value:
            return value
        time.sleep(interval_s)
    return None


def print_events(out_dir):
    # The exported per-range GPU times (Vulkan debug labels): one row per
    # label, columns = the trace total then each traced frame.
    path = os.path.join(out_dir, "BASE", "D3DPERF_EVENTS.xls")
    if not os.path.exists(path):
        print(f"no exported events ({path})")
        return
    rows = []
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.read().splitlines()
    for line in lines[1:]:
        fields = line.split("\t")
        try:
            rows.append((fields[0], float(fields[1])))
        except (IndexError, ValueError):
            continue
    rows.sort(key=lambda row: -row[1])
    print("GPU time per debug label over the trace (ms):")
    for name, value in rows[:40]:
        print(f"  {value:10.3f}  {name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ngfx", default="", help="ngfx CLI executable (default ERHE_NGFX, then PATH)")
    parser.add_argument("--editor", default="build/Ninja_Vulkan_Release/bin/editor", help="editor executable (repo-root relative or absolute)")
    parser.add_argument("--scene", default=bench.DEFAULT_SCENE, help="scene file to open (repo-root relative)")
    parser.add_argument("--camera-translation", default=json.dumps(bench.DEFAULT_CAMERA_TRANSLATION))
    parser.add_argument("--camera-rotation", default=json.dumps(bench.DEFAULT_CAMERA_ROTATION))
    parser.add_argument("--root-translation", default=json.dumps(bench.DEFAULT_ROOT_TRANSLATION))
    parser.add_argument("--root-rotation", default=json.dumps(bench.DEFAULT_ROOT_ROTATION))
    parser.add_argument("--mcp", action="append", default=[], metavar="TOOL[=JSON]", help="MCP call made after the camera is set, before the trace (repeatable)")
    parser.add_argument("--activity", choices=("trace", "capture"), default="trace", help="GPU Trace (timings, exported tables) or Graphics Capture (frame debugger capture)")
    parser.add_argument("--default-scene", action="store_true", help="trace / capture the editor's startup scene: no --no-scene, no open_scene, no camera or root transform (--scene and the transform options are ignored)")
    parser.add_argument("--frames", type=int, default=1, help="frames to trace or capture")
    parser.add_argument("--skip-frames", type=int, default=2, help="frames between the start call and the first traced / captured frame")
    parser.add_argument("--spirv-optimization", choices=("on", "off"), default="on", help="ERHE_SPIRV_OPTIMIZATION for the editor: profiling runs use optimized SPIR-V (Graphics_config::spirv_optimization; default on)")
    parser.add_argument("--env", action="append", default=[], metavar="NAME=VALUE", help="extra editor environment variable (repeatable)")
    parser.add_argument("--editor-setting", action="append", default=[], metavar="KEY.PATH=JSON", help="config/editor/editor_settings.json value for this run, e.g. 'load.base_variant=\"on_demand\"' (the file is restored at exit; repeatable)")
    parser.add_argument("--ngfx-arg", action="append", default=[], metavar="ARG", help="extra ngfx activity option, e.g. --ngfx-arg=--time-every-action (repeatable)")
    parser.add_argument("--out-dir", default=os.path.join(REPO_ROOT, "logs", "nsight"), help="report / capture directory (created)")
    parser.add_argument("--timeout", type=float, default=600.0, help="seconds for each wait")
    args = parser.parse_args()

    settings_path   = os.path.join(REPO_ROOT, "config", "editor", "editor_settings.json")
    settings_backup = None
    if args.editor_setting:
        with open(settings_path, encoding="utf-8", newline="") as f:
            settings_backup = f.read()
        values = json.loads(settings_backup)
        for spec in args.editor_setting:
            key_path, _sep, value = spec.partition("=")
            node = values
            keys = key_path.split(".")
            for key in keys[:-1]:
                node = node[key]
            node[keys[-1]] = json.loads(value)
        with open(settings_path, "w", encoding="utf-8", newline="") as f:
            f.write(json.dumps(values, indent=4) + "\n")
    try:
        return run(args)
    finally:
        if settings_backup is not None:
            with open(settings_path, "w", encoding="utf-8", newline="") as f:
                f.write(settings_backup)


def run(args):
    ngfx = find_ngfx(args.ngfx)
    editor = args.editor if os.path.isabs(args.editor) else os.path.join(REPO_ROOT, args.editor)
    out_dir = os.path.abspath(args.out_dir)
    os.makedirs(out_dir, exist_ok=True)
    if os.path.exists(bench.LOG_PATH):
        os.remove(bench.LOG_PATH)

    spirv_optimization = "1" if args.spirv_optimization == "on" else "0"
    env = ";".join(["ERHE_AI_DRIVER=1", f"ERHE_SPIRV_OPTIMIZATION={spirv_optimization}"] + args.env) + ";"
    ngfx_log_path = os.path.join(out_dir, "ngfx.log")
    launched = time.time()
    with open(ngfx_log_path, "w") as ngfx_log:
        ngfx_process = subprocess.Popen(
            [ngfx] + NGFX_ACTIVITY_ARGUMENTS[args.activity] + args.ngfx_arg +
            ["--exe", editor, "--dir", REPO_ROOT] + ([] if args.default_scene else ["--args", "--no-scene"]) +
            ["--env", env, "--output-dir", out_dir, "--no-timeout"],
            stdout=ngfx_log, stderr=subprocess.STDOUT
        )
    pid = wait_for(lambda: find_editor_pid(editor, launched) if ngfx_process.poll() is None else -1, args.timeout)
    if (pid is None) or (pid == -1):
        ngfx_process.kill()
        sys.exit(f"editor did not start under ngfx; see {ngfx_log_path}")
    editor_process = Editor_process(pid)
    port, ready_s = bench.wait_for_ready(editor_process, args.timeout)
    if port is None:
        ngfx_process.kill()
        sys.exit(f"editor (pid {pid}) not ready; see logs/log.txt and {ngfx_log_path}")
    print(f"editor pid {pid} ready in {ready_s:.1f} s, MCP port {port}")

    def call(tool, arguments=None):
        return bench.mcp(port, tool, arguments)

    exit_code = 0
    try:
        if args.default_scene:
            scene_names = bench.scene_names(port) or []
            if not scene_names:
                raise RuntimeError("no startup scene listed")
            scene_name = scene_names[0]
            scene_file = scene_name
            print(f"startup scene '{scene_name}'")
        else:
            scene_file = os.path.basename(args.scene)
            call("open_scene", {"path": args.scene})
            idle_s = bench.wait_until_settled(port, scene_file, args.timeout)
            if idle_s is None:
                raise RuntimeError("open_scene did not settle")
            print(f"open: idle after {idle_s:.1f} s")
            scene_name = next((name for name in (bench.scene_names(port) or []) if scene_file in name), None)
            if scene_name is None:
                raise RuntimeError("scene not listed")
            for node_name, translation, rotation in (
                ("Camera", json.loads(args.camera_translation), json.loads(args.camera_rotation)),
                (scene_name, json.loads(args.root_translation), json.loads(args.root_rotation)),
            ):
                call("set_node_transform", {
                    "scene_name": scene_name, "node_name": node_name, "space": "world",
                    "translation": translation, "rotation_xyzw": rotation,
                })
        isolated = bench.isolate_viewport(lambda tool, arguments: call(tool, arguments), scene_name=scene_name)
        print(f"isolated viewport '{isolated.get('title')}'")
        for spec in args.mcp:
            tool, _, arguments = spec.partition("=")
            print(f"{tool}: {json.dumps(call(tool, json.loads(arguments) if arguments else {}))}")
        if bench.wait_until_settled(port, scene_file, args.timeout) is None:
            raise RuntimeError("scene did not settle after the setup calls")

        tool = "start_nsight_gpu_trace" if (args.activity == "trace") else "start_nsight_graphics_capture"
        reply = call(tool, {"frame_count": args.frames, "skip_frames": args.skip_frames})
        if not isinstance(reply, dict):
            raise RuntimeError(f"{tool}: {reply}")
        if args.activity == "trace":
            status = wait_for(
                lambda: (lambda s: s if (isinstance(s, dict) and (s.get("state") in FINAL_STATES)) else None)(call("get_nsight_status")),
                args.timeout, 0.1
            )
            print(f"trace status: {status}")
            if (status is None) or (status.get("state") != "stopped"):
                raise RuntimeError("trace did not stop")
            # ngfx collects the trace, saves the report, exports it and
            # exits; the editor keeps running until then.
            if wait_for(lambda: ngfx_process.poll() is not None, args.timeout) is None:
                raise RuntimeError("ngfx did not finish")
        else:
            # The capture is written by the injected library inside the
            # editor process; the editor reports the finished file.
            status = wait_for(
                lambda: (lambda s: s if (isinstance(s, dict) and ((s.get("capture_file_count", 0) > 0) or (s.get("state") == "failed"))) else None)(call("get_nsight_status")),
                args.timeout, 0.2
            )
            print(f"capture status: {status}")
            if (status is None) or (status.get("capture_file_count", 0) == 0):
                raise RuntimeError("no capture file was written")
            print(f"capture file: {status.get('capture_file_path')}")
    except RuntimeError as error:
        print(f"error: {error}")
        exit_code = 1
    finally:
        if editor_process.poll() is None:
            call("request_exit")
            if wait_for(lambda: editor_process.poll() is not None, 60.0) is None:
                print(f"editor pid {pid} did not exit; killing")
                os.kill(pid, 9)
        if ngfx_process.poll() is None:
            if wait_for(lambda: ngfx_process.poll() is not None, 60.0) is None:
                ngfx_process.kill()

    with open(ngfx_log_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if ("GPU Trace report saved" in line) or ("ERROR" in line) or ("error" in line):
                print(f"ngfx: {line.rstrip()}")
    if args.activity == "trace":
        print_events(out_dir)
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
