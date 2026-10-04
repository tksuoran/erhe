"""Short device-loss repro: launch the editor under the Crash Diagnostic Layer,
open one scene over MCP, wait for the editor to die (or not), then print
scripts/gpu_fault_report.py. Run from the repository root.

    py -3 scripts/device_loss_repro.py --editor build_ninja_win_vulkan/bin/editor.exe
        [--scene src/erhe/usd/test/data/authored.usda] [--wait 90] [--no-cdl]
        [--layer-settings <vk_layer_settings.txt>]

The layer settings file holds the Crash Diagnostic Layer options
(doc/agents/debugging.md "GPU faults"); without it the layer runs with its
defaults. The dumps land under ~/cdl, which is emptied first.
"""
import argparse
import base64
import glob
import os
import shutil
import subprocess
import sys
import time


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", required=True)
    parser.add_argument("--scene", default="src/erhe/usd/test/data/authored.usda")
    parser.add_argument("--wait", type=int, default=90, help="seconds to wait for a fault after the load")
    parser.add_argument("--no-cdl", action="store_true", help="do not enable the Crash Diagnostic Layer")
    parser.add_argument("--layer-settings", help="VK_LAYER_SETTINGS_PATH file for the layer")
    arguments = parser.parse_args()

    repo = os.getcwd()
    for directory in glob.glob(os.path.join(os.path.expanduser("~"), "cdl", "*")):
        shutil.rmtree(directory, ignore_errors=True)

    environment = dict(os.environ)
    environment["ERHE_AI_DRIVER"] = "1"
    if not arguments.no_cdl:
        environment["VK_LOADER_LAYERS_ENABLE"] = "VK_LAYER_LUNARG_crash_diagnostic"
        if arguments.layer_settings:
            environment["VK_LAYER_SETTINGS_PATH"] = os.path.abspath(arguments.layer_settings)
    log_path = os.path.join(repo, "logs", "log.txt")
    try:
        os.remove(log_path)
    except OSError:
        pass
    creation_flags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
    process = subprocess.Popen([arguments.editor], cwd=repo, env=environment, creationflags=creation_flags)
    started = time.time()
    while time.time() - started < 120:
        if process.poll() is not None:
            print("editor exited during startup, code", process.returncode)
            return 2
        try:
            if "Main loop: completed frame 12" in open(log_path, encoding="utf-8", errors="replace").read():
                break
        except OSError:
            pass
        time.sleep(0.5)
    print(f"editor ready after {time.time() - started:.1f} s")

    payload = base64.b64encode(('{"path": "%s"}' % arguments.scene).encode()).decode()
    result = subprocess.run(
        [sys.executable, os.path.join("scripts", "mcp_call.py"), "load_scene", "b64:" + payload],
        cwd=repo, capture_output=True, text=True
    )
    print("load_scene:", result.stdout.strip().replace("\n", " ")[:120])
    loaded = time.time()
    while time.time() - loaded < arguments.wait:
        if process.poll() is not None:
            print(f"EDITOR DIED {time.time() - loaded:.1f} s after the load (exit {process.returncode})")
            break
        time.sleep(1.0)
    else:
        print(f"editor still alive {arguments.wait} s after the load")
        process.kill()
        process.wait()
    subprocess.run([sys.executable, os.path.join("scripts", "gpu_fault_report.py")], cwd=repo)
    return 0


if __name__ == "__main__":
    sys.exit(main())
