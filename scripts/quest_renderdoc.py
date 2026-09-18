#!/usr/bin/env python3
"""Launch the erhe editor on a Quest under RenderDoc Meta Fork, and capture frames.

Subcommands:
    status    Report everything the workflow depends on, change nothing.
    launch    Force-stop the app, then launch it with RenderDoc injection.
              Records the capture ident so `capture` can be run separately.
    capture   Capture N frames from the running injected app and download them.
    stop      Force-stop the app and stop the on-device RenderDoc server.

Typical use:

    py -3 scripts/quest_renderdoc.py launch
    # ... put the headset on, get the scene into the state you want ...
    py -3 scripts/quest_renderdoc.py capture --frames 1

Why a script rather than raw commands: this workflow has several failure modes
that are silent or actively misleading, and each one costs a headset cycle to
rediscover. They are all checked in `preflight()`; see the comments there.

Requires RenderDoc Meta Fork (68.16+ for the bundled Python API package).
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

# The editor's Quest package / activity, matching android-project.
PACKAGE = "org.libsdl.app.quest"
ACTIVITY = "org.libsdl.app.ErheActivity"

# RenderDoc's on-device component, installed from the fork's plugins/android.
RENDERDOC_DEVICE_PACKAGE = "com.oculus.renderdoccmd.arm64"

# Where the ident from `launch` is remembered so `capture` can run as a separate
# invocation. Gitignored along with the rest of logs/.
IDENT_FILE = Path("logs/.renderdoc_ident.json")
CAPTURE_DIR = Path("logs/rdc")

# Where the capture layer writes .rdc files on the device.
REMOTE_CAPTURE_DIR = f"/storage/emulated/0/Android/media/{PACKAGE}/files/RenderDoc"

# The manual Android GPU-debug-layer settings. RenderDoc's own launcher performs
# injection itself, and these must NOT also be set - see preflight().
GPU_DEBUG_SETTINGS = (
    "enable_gpu_debug_layers",
    "gpu_debug_app",
    "gpu_debug_layer_app",
    "gpu_debug_layers",
    "gpu_debug_layers_gles",
)


def renderdoc_root() -> Path:
    env = os.environ.get("RENDERDOC_META_FORK_DIR")
    if env:
        return Path(env)
    return Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "RenderDocForMetaQuest"


def android_sdk() -> Path:
    env = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
    if env:
        return Path(env)
    return Path(os.environ["LOCALAPPDATA"]) / "Android" / "Sdk"


def adb_path() -> Path:
    # Always the Android SDK's adb, never the copy bundled with RenderDoc.
    #
    # RenderDoc ships adb 1.0.40 (2018) while the Android SDK has 1.0.41. adb
    # refuses to talk to a server of a different version and KILLS it, so any
    # interleaving of the two clients restarts the daemon underneath whatever is
    # running - which silently drops RenderDoc's target-control port forwards and
    # breaks an in-progress capture. Pinning one adb for everything is the fix;
    # `status` additionally checks that RenderDoc is pointed at this same SDK.
    return android_sdk() / "platform-tools" / "adb.exe"


def run(args: list[str], **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run(args, capture_output=True, text=True, **kwargs)


def adb(*args: str) -> str:
    result = run([str(adb_path()), *args])
    return result.stdout.strip()


def remote_captures() -> set[str]:
    """Names of the .rdc files currently on the device."""
    listing = adb("shell", f"ls -1 {REMOTE_CAPTURE_DIR}")
    return {line.strip() for line in listing.splitlines() if line.strip().endswith(".rdc")}


def import_renderdoc_api():
    """Import the fork's bundled Python API without installing it.

    The package only needs to be importable; `renderdoc_api` shells out to
    renderdoccmd rather than binding the native module, so its fastmcp/pydantic
    dependencies (needed only by the MCP server itself) are not required.
    """
    mcp_dir = renderdoc_root() / "MCP"
    if not mcp_dir.is_dir():
        sys.exit(
            f"RenderDoc Meta Fork MCP package not found at {mcp_dir}.\n"
            "Install RenderDoc Meta Fork 68.16 or later, or set RENDERDOC_META_FORK_DIR."
        )
    sys.path.insert(0, str(mcp_dir))
    os.environ.setdefault("RENDERDOC_CMD_PATH", str(renderdoc_root() / "renderdoccmd.exe"))
    try:
        from renderdoc_mcp import renderdoc_api  # type: ignore
    except ImportError as exc:
        sys.exit(f"Could not import renderdoc_mcp.renderdoc_api from {mcp_dir}: {exc}")
    return renderdoc_api


def preflight(require_device_package: bool = True) -> str:
    """Check every precondition that otherwise fails silently. Returns the serial."""
    problems: list[str] = []

    if not adb_path().is_file():
        sys.exit(f"adb not found at {adb_path()}. Set ANDROID_HOME.")

    cmd = renderdoc_root() / "renderdoccmd.exe"
    if not cmd.is_file():
        sys.exit(f"renderdoccmd not found at {cmd}. Set RENDERDOC_META_FORK_DIR.")

    devices = [
        line.split()[0]
        for line in adb("devices").splitlines()[1:]
        if line.strip() and line.split()[1] == "device"
    ]
    if not devices:
        sys.exit("No adb device. Connect the Quest and check `adb devices`.")
    serial = devices[0]

    # RenderDoc must drive the SAME adb as this script, or the two clients fight
    # over the server (see adb_path()). RenderDoc uses its bundled 1.0.40 unless
    # Android.SDKDirPath is set in its config.
    conf = Path(os.environ["APPDATA"]) / "RenderDocMetaFork" / "renderdoc.conf"
    if conf.is_file():
        text = conf.read_text(encoding="utf-8", errors="replace")
        marker = "<SDKDirPath type=\"String\">"
        start = text.find(marker)
        value = text[start + len(marker):text.find("<", start + len(marker))] if start >= 0 else ""
        if not value.strip():
            problems.append(
                f"RenderDoc's Android.SDKDirPath is empty in {conf}, so it will use its own\n"
                f"    adb 1.0.40 instead of the SDK's 1.0.41 and the two will kill each other's\n"
                f"    daemon mid-capture. Set it to: {android_sdk()}"
            )

    # Leftover manual layer injection is actively harmful: with the layer forced
    # in this way the editor SIGSEGVs inside RenderDoc's own
    # WrappedVulkan::vkBindImageMemory during xrCreateSwapchain, because Meta's XR
    # runtime creates swapchain images the layer never saw created. RenderDoc's
    # launcher injects differently and does not hit this, so these settings must
    # be clear. See doc/agents/quest_renderdoc_capture.md.
    leftovers = [k for k in GPU_DEBUG_SETTINGS if adb("shell", "settings", "get", "global", k) not in ("null", "")]
    if leftovers:
        problems.append(
            "Manual GPU debug layer settings are still set: " + ", ".join(leftovers) + "\n"
            "    These crash the editor at xrCreateSwapchain. Clear them with:\n"
            "        py -3 scripts/quest_renderdoc.py stop --clear-layer-settings"
        )

    if require_device_package:
        installed = adb("shell", "pm", "list", "packages", RENDERDOC_DEVICE_PACKAGE)
        if RENDERDOC_DEVICE_PACKAGE not in installed:
            apk = renderdoc_root() / "plugins" / "android" / f"{RENDERDOC_DEVICE_PACKAGE}.apk"
            problems.append(
                f"{RENDERDOC_DEVICE_PACKAGE} is not installed on the device.\n"
                f"    Install it with: adb install -r -g \"{apk}\""
            )

    # The app must be a development build; RenderDoc refuses store builds.
    flags = adb("shell", "dumpsys", "package", PACKAGE)
    if not flags:
        problems.append(f"{PACKAGE} is not installed. Build and install it first (scripts/install_android.bat quest).")
    elif "DEBUGGABLE" not in flags:
        problems.append(f"{PACKAGE} is not DEBUGGABLE. RenderDoc can only capture development builds.")

    if problems:
        sys.exit("Preflight failed:\n\n" + "\n\n".join("  - " + p for p in problems))

    return serial


def cmd_status(_args: argparse.Namespace) -> int:
    serial = preflight(require_device_package=False)
    print(f"device                 : {serial}")
    print(f"adb                    : {adb_path()} ({adb('version').splitlines()[0] if adb('version') else '?'})")
    print(f"renderdoc              : {renderdoc_root()}")
    editor_pid = adb("shell", "pidof", PACKAGE)
    print(f"{PACKAGE:<23}: {'running pid ' + editor_pid if editor_pid else 'not running'}")
    server_pid = adb("shell", "pidof", RENDERDOC_DEVICE_PACKAGE)
    print(f"{RENDERDOC_DEVICE_PACKAGE:<23}: {'running pid ' + server_pid if server_pid else 'not running'}")
    if editor_pid:
        # Whether the capture layer actually made it into the process. The editor
        # also logs this ("RenderDoc: attaching to injected capture layer").
        maps = run([str(adb_path()), "shell", f"run-as {PACKAGE} cat /proc/{editor_pid}/maps"])
        attached = "renderdoc" in maps.stdout.lower()
        print(f"capture layer in process: {'yes' if attached else 'no'}")
    print(f"recorded ident         : {read_ident() if IDENT_FILE.is_file() else '<none>'}")
    return 0


def read_ident() -> int | None:
    try:
        return int(json.loads(IDENT_FILE.read_text(encoding="utf-8"))["ident"])
    except Exception:
        return None


def cmd_launch(args: argparse.Namespace) -> int:
    serial = preflight()
    api = import_renderdoc_api()

    print(f"Force-stopping {PACKAGE} ...")
    adb("shell", "am", "force-stop", PACKAGE)

    print("Launching with RenderDoc injection (this also starts the on-device server) ...")
    result = api.adb_launch(
        PACKAGE,
        device=serial,
        activity=ACTIVITY,
        pair_controllers=not args.no_pair_controllers,
    )
    if not result.get("success"):
        print(json.dumps(result, indent=2)[:4000], file=sys.stderr)
        sys.exit("adb-launch failed.")

    ident = result.get("ident")
    IDENT_FILE.parent.mkdir(parents=True, exist_ok=True)
    IDENT_FILE.write_text(json.dumps({"ident": ident, "device": serial}, indent=2), encoding="utf-8")
    print(f"Launched. ident={ident} (recorded in {IDENT_FILE})")
    print("The editor takes ~15-20 s to initialize. Then:")
    print("    py -3 scripts/quest_renderdoc.py capture --frames 1")
    return 0


def cmd_capture(args: argparse.Namespace) -> int:
    serial = preflight()
    api = import_renderdoc_api()

    ident = args.ident if args.ident is not None else read_ident()
    if ident is None:
        sys.exit(f"No ident recorded in {IDENT_FILE}; run `launch` first, or pass --ident.")

    if not adb("shell", "pidof", PACKAGE):
        sys.exit(f"{PACKAGE} is not running - the ident refers to a launch that has exited. Run `launch` again.")

    CAPTURE_DIR.mkdir(parents=True, exist_ok=True)

    # Snapshot the device's capture directory first, so the new .rdc can be
    # identified by difference. Neither of renderdoccmd's own answers can be
    # trusted here: on a copy failure it reports "COPY_FAILED:<path>" naming a
    # capture from an EARLIER run rather than the one just taken, so following
    # that path silently downloads a stale frame - which looks like a successful
    # capture until the contents are examined.
    before = remote_captures()

    print(f"Capturing {args.frames} frame(s) with ident {ident} ...")
    result = api.adb_capture(serial, ident=ident, frames=args.frames, output_dir=str(CAPTURE_DIR))
    produced = sorted(remote_captures() - before)

    if not produced:
        print(json.dumps(result, indent=2)[:4000], file=sys.stderr)
        sys.exit(
            "Capture failed - no new .rdc appeared on the device.\n"
            "If the app died during capture, suspect device memory: the editor, the RenderDoc\n"
            "capture layer and the .rdc must all fit at once (see doc/agents/quest_renderdoc_capture.md)."
        )

    # Whether renderdoccmd managed its own copy or not, pull by the name that is
    # actually new. adb pull works even when renderdoccmd's copy reports failure.
    local_files = []
    for name in produced:
        local = CAPTURE_DIR / name
        if not local.is_file():
            pull = run([str(adb_path()), "pull", f"{REMOTE_CAPTURE_DIR}/{name}", str(local)])
            if pull.returncode != 0 or not local.is_file():
                print(pull.stdout + pull.stderr, file=sys.stderr)
                sys.exit(f"adb pull of {name} failed; the capture is still on the device.")
        print(f"capture: {local}")
        local_files.append(local)

    # An embedded thumbnail is the cheapest proof the capture holds a real frame,
    # and it is the only part of a capture that can be looked at without starting
    # a replay session, so extract it by default.
    if not args.no_thumb:
        for local in local_files:
            thumb = local.with_suffix(".thumb.png")
            extract = run([str(renderdoc_root() / "renderdoccmd.exe"), "thumb", str(local), "--out", str(thumb)])
            if extract.returncode == 0 and thumb.is_file():
                print(f"thumbnail: {thumb}")
            else:
                print(f"(could not extract a thumbnail from {local.name})")
    return 0


def cmd_stop(args: argparse.Namespace) -> int:
    if not adb_path().is_file():
        sys.exit(f"adb not found at {adb_path()}.")
    adb("shell", "am", "force-stop", PACKAGE)
    adb("shell", "am", "force-stop", RENDERDOC_DEVICE_PACKAGE)
    print(f"Force-stopped {PACKAGE} and {RENDERDOC_DEVICE_PACKAGE}.")
    if args.clear_layer_settings:
        for key in GPU_DEBUG_SETTINGS:
            adb("shell", "settings", "delete", "global", key)
        adb("shell", "setprop", "debug.vr.profiler", "0")
        adb("shell", "setprop", "debug.vulkan.layers", ":")
        print("Cleared manual GPU debug layer settings.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("status", help="report device / injection state, change nothing").set_defaults(func=cmd_status)

    p_launch = sub.add_parser("launch", help="force-stop then launch with RenderDoc injection")
    p_launch.add_argument(
        "--no-pair-controllers",
        action="store_true",
        help="skip RenderDoc's controller pairing step",
    )
    p_launch.set_defaults(func=cmd_launch)

    p_capture = sub.add_parser("capture", help="capture frames from the running injected app")
    p_capture.add_argument("--frames", type=int, default=1, help="frames to capture (1-10, default 1)")
    p_capture.add_argument("--ident", type=int, default=None, help="override the recorded ident")
    p_capture.add_argument("--no-thumb", action="store_true", help="skip extracting the embedded thumbnail")
    p_capture.set_defaults(func=cmd_capture)

    p_stop = sub.add_parser("stop", help="force-stop the app and the on-device RenderDoc server")
    p_stop.add_argument(
        "--clear-layer-settings",
        action="store_true",
        help="also clear the manual gpu_debug_* settings (they crash the editor at xrCreateSwapchain)",
    )
    p_stop.set_defaults(func=cmd_stop)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
