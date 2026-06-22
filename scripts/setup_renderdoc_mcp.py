#!/usr/bin/env python3
"""
setup_renderdoc_mcp.py -- one-shot setup for the RenderDoc fork MCP server + the
erhe stdio proxy.

Clones (or updates) the RenderDoc fork (https://github.com/tksuoran/renderdoc,
branch mcp-server), builds qrenderdoc with its embedded MCP server, points erhe at
the freshly built renderdoc.dll, registers the stdio proxy with Claude Code, and
captures the proxy's baked tool schema. Idempotent: safe to re-run.

Windows only (the fork builds with MSBuild / the v145 toolset). Run with the py
launcher:

    py -3 scripts/setup_renderdoc_mcp.py [options]

Phases (each can be skipped):
  1. Clone/update the fork at --renderdoc-dir on branch --branch.
  2. Build renderdoc.sln (Development|x64) with the v145 toolset override and the
     stdext_compat.h forced-include shim, then verify the artifacts.
  3. Configure erhe: write the machine-specific renderdoc_library_path_override and
     enable it (renderdoc_library_path_override_enable) in
     config/editor/erhe_graphics.json (a deliberately LOCAL, uncommitted change).
  4. Install the proxy into .mcp.json (stdio server, with ERHE_RENDERDOC_QRENDERDOC
     pointing at the built qrenderdoc.exe).
  5. Generate scripts/renderdoc_tools.json (the proxy's baked schema).

BUILD CAVEAT: the verified recipe on this machine is an *incremental* relink of
qrenderdoc_local.vcxproj against already-built libs. A from-scratch full-solution
build with /p:PlatformToolset=v145 is the fragile step (v145's STL dropped APIs the
vendored Qt5 still uses; the shim fixes the one known break, others may surface). If
the build fails, open renderdoc.sln in VS2026, accept the retarget to v145, build
Development|x64 once, then re-run with --skip-build.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys


# flush=True so our phase logs stay correctly ordered relative to the inherited
# stdout/stderr of child processes (git, msbuild, the capture subprocess) when
# this script's stdout is piped/redirected rather than attached to a TTY.
def step(m): print("\n==> " + m, flush=True)
def info(m): print("    " + m, flush=True)
def ok(m):   print("    " + m, flush=True)
def warn(m): print("    WARNING: " + m, flush=True)


class SetupError(Exception):
    pass


# v145's STL dropped stdext::make_(un)checked_array_iterator, which the vendored
# Qt5 qlist.h still uses. Force-include this shim via the CL env var so the build
# links. Untracked, lives under the gitignored x64\; deleted after the build.
SHIM = """\
#pragma once
#include <cstddef>
namespace stdext {
template <class T> inline T *make_checked_array_iterator(T *p, std::size_t, std::size_t i=0){return p+i;}
template <class T> inline T *make_unchecked_array_iterator(T *p){return p;}
}
"""


def require_command(name, hint):
    if shutil.which(name) is None:
        raise SetupError("'%s' not found on PATH. %s" % (name, hint))


def detect_newline(text):
    return "\r\n" if "\r\n" in text else "\n"


def read_text(path):
    with open(path, "r", encoding="utf-8", newline="") as f:
        return f.read()


def write_text(path, content):
    # utf-8, no BOM, embedded newlines preserved verbatim.
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(content)


def find_msbuild():
    pf86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = os.path.join(pf86, "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not os.path.isfile(vswhere):
        raise SetupError("vswhere.exe not found at %s -- is Visual Studio installed?" % vswhere)
    cp = subprocess.run(
        [vswhere, "-latest", "-requires", "Microsoft.Component.MSBuild",
         "-find", r"MSBuild\**\Bin\MSBuild.exe"],
        capture_output=True, text=True,
    )
    lines = [ln.strip() for ln in cp.stdout.splitlines() if ln.strip()]
    if not lines:
        raise SetupError("Could not locate MSBuild.exe via vswhere.")
    return lines[0]


def build(rd_dir, configuration, shim_path, qrenderdoc_exe, renderdoc_dll, renderdoc_json):
    step("Build qrenderdoc (%s|x64, toolset v145)" % configuration)
    msbuild = find_msbuild()
    info("MSBuild: " + msbuild)

    # Stop any running qrenderdoc so its binaries can be relinked.
    subprocess.run(["taskkill", "/IM", "qrenderdoc.exe", "/F"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    os.makedirs(os.path.dirname(shim_path), exist_ok=True)
    write_text(shim_path, SHIM)
    info("wrote forced-include shim: " + shim_path)

    env = os.environ.copy()
    env["CL"] = '/FI"%s"' % shim_path
    sln = os.path.join(rd_dir, "renderdoc.sln")
    msb_args = [
        msbuild, sln,
        "/p:Configuration=%s" % configuration,
        "/p:Platform=x64",
        "/p:PlatformToolset=v145",
        "/m", "/nologo", "/v:minimal", "/clp:Summary",
    ]
    info("msbuild " + " ".join(msb_args[1:]))
    try:
        build_rc = subprocess.run(msb_args, env=env).returncode
    finally:
        try:
            os.remove(shim_path)
        except OSError:
            pass

    if build_rc != 0:
        raise SetupError(
            "MSBuild returned exit code %d.\n\n"
            "This from-scratch full-solution v145 build is the known-fragile step. Fall\n"
            "back to the Visual Studio path:\n"
            "  1. Open %s in VS2026.\n"
            "  2. Accept the prompt to retarget the v140 projects to v145.\n"
            "  3. Build the 'Development' / 'x64' configuration once.\n"
            "  4. Re-run this script with --skip-build." % (build_rc, sln)
        )
    for f in (qrenderdoc_exe, renderdoc_dll, renderdoc_json):
        if not os.path.isfile(f):
            raise SetupError("build reported success but expected artifact is missing: " + f)
    ok("built " + qrenderdoc_exe)


# Matches a JSON scalar value (string / bool / null / number) after a "key":.
_JSON_VALUE = r'(?:"[^"]*"|true|false|null|-?\d+(?:\.\d+)?)'


def _set_json_field(text, key, value_literal, nl, anchor_key):
    """Set "key": value_literal in a strict-JSON text with a minimal-diff edit.
    Replaces the value in place if the key exists, else inserts a new line right
    after the anchor_key line (preserving its indentation). Returns
    (new_text, "updated"|"inserted")."""
    value_pat = r'("' + re.escape(key) + r'"\s*:\s*)' + _JSON_VALUE
    if re.search(value_pat, text):
        # lambda replacement so backslashes in paths are not treated as group refs
        return re.sub(value_pat, lambda m: m.group(1) + value_literal, text), "updated"
    anchor_pat = r'(?m)^(?P<indent>[ \t]*)"' + re.escape(anchor_key) + r'"\s*:\s*' + _JSON_VALUE + r'\s*,?'
    m = re.search(anchor_pat, text)
    if not m:
        raise SetupError("could not find %s to anchor the insert of %s" % (anchor_key, key))
    indent = m.group("indent")
    insert_at = m.end()
    new_line = nl + indent + '"' + key + '": ' + value_literal + ','
    return text[:insert_at] + new_line + text[insert_at:], "inserted"


def configure_erhe(cfg_path, dll_path):
    text = read_text(cfg_path)
    nl = detect_newline(text)

    # Capture support must be on for the in-app RenderDoc API to load at startup.
    text, _ = _set_json_field(text, "renderdoc_capture_support", "true", nl,
                              anchor_key="renderdoc_capture_support")
    # Enable the override and write the machine-specific path (kept after the
    # enable toggle, matching the struct field order). Enabling here makes the
    # freshly built fork actually get used; toggle the enable off to fall back to
    # the system RenderDoc without losing the configured path.
    text, a_en = _set_json_field(text, "renderdoc_library_path_override_enable", "true", nl,
                                 anchor_key="renderdoc_capture_support")
    text, a_path = _set_json_field(text, "renderdoc_library_path_override", json.dumps(dll_path), nl,
                                   anchor_key="renderdoc_library_path_override_enable")
    info("%s renderdoc_library_path_override_enable=true; %s renderdoc_library_path_override" % (a_en, a_path))
    write_text(cfg_path, text)


def install_proxy(mcp_path, proxy_py, qrenderdoc_exe, port):
    if os.path.isfile(mcp_path):
        with open(mcp_path, "r", encoding="utf-8") as f:
            data = json.load(f)
        if not isinstance(data, dict):
            raise SetupError(".mcp.json is not a JSON object: " + mcp_path)
    else:
        data = {}
    servers = data.setdefault("mcpServers", {})
    servers["renderdoc"] = {
        "command": "py",
        "args": ["-3", proxy_py.replace("\\", "/")],
        "env": {
            "ERHE_RENDERDOC_QRENDERDOC": qrenderdoc_exe,
            "ERHE_RENDERDOC_MCP_PORT": str(port),
        },
    }
    write_text(mcp_path, json.dumps(data, indent=2) + "\n")


def main():
    p = argparse.ArgumentParser(
        description="Set up the RenderDoc fork MCP server + erhe stdio proxy (Windows)."
    )
    p.add_argument("--renderdoc-dir", default=r"D:\renderdoc",
                   help="fork checkout / build location (default: D:\\renderdoc)")
    p.add_argument("--repo-url", default="https://github.com/tksuoran/renderdoc.git")
    p.add_argument("--branch", default="mcp-server")
    p.add_argument("--configuration", default="Development")
    p.add_argument("--port", type=int, default=7398)
    p.add_argument("--skip-clone", action="store_true")
    p.add_argument("--skip-build", action="store_true")
    p.add_argument("--skip-schema", action="store_true")
    args = p.parse_args()

    if os.name != "nt":
        raise SetupError("This setup script is Windows-only (the fork builds with MSBuild).")

    script_dir = os.path.dirname(os.path.abspath(__file__))
    erhe_root = os.path.dirname(script_dir)
    proxy_py = os.path.join(script_dir, "renderdoc_mcp_proxy.py")
    capture_py = os.path.join(script_dir, "capture_renderdoc_tools.py")
    schema_json = os.path.join(script_dir, "renderdoc_tools.json")
    erhe_gfx_json = os.path.join(erhe_root, "config", "editor", "erhe_graphics.json")
    mcp_json = os.path.join(erhe_root, ".mcp.json")

    rd_dir = args.renderdoc_dir
    dev_dir = os.path.join(rd_dir, "x64", args.configuration)
    qrenderdoc_exe = os.path.join(dev_dir, "qrenderdoc.exe")
    renderdoc_dll = os.path.join(dev_dir, "renderdoc.dll")
    renderdoc_json = os.path.join(dev_dir, "renderdoc.json")
    shim_path = os.path.join(rd_dir, "x64", "stdext_compat.h")

    step("Preflight")
    require_command("git", "Install Git for Windows.")
    for f in (proxy_py, capture_py):
        if not os.path.isfile(f):
            raise SetupError("required script missing: " + f)
    if not os.path.isfile(erhe_gfx_json):
        raise SetupError("erhe graphics config missing: " + erhe_gfx_json)
    info("erhe root      : " + erhe_root)
    info("renderdoc dir  : " + rd_dir)
    info("dev output dir : " + dev_dir)

    # phase 1: clone / update
    if args.skip_clone:
        step("Clone/update (skipped)")
    else:
        step("Clone/update fork (%s)" % args.branch)
        if os.path.isdir(os.path.join(rd_dir, ".git")):
            info("existing checkout found; fetching and checking out " + args.branch)
            if subprocess.run(["git", "-C", rd_dir, "fetch", "--quiet", "origin"]).returncode != 0:
                raise SetupError("git fetch failed in " + rd_dir)
            if subprocess.run(["git", "-C", rd_dir, "checkout", args.branch]).returncode != 0:
                raise SetupError("git checkout %s failed in %s" % (args.branch, rd_dir))
            if subprocess.run(["git", "-C", rd_dir, "pull", "--ff-only", "--quiet"]).returncode != 0:
                warn("git pull --ff-only did not fast-forward (local changes?); continuing with current checkout")
        elif os.path.exists(rd_dir):
            raise SetupError("%s exists but is not a git checkout. Remove it or pass --renderdoc-dir <other>." % rd_dir)
        else:
            info("cloning %s (branch %s) into %s" % (args.repo_url, args.branch, rd_dir))
            if subprocess.run(["git", "clone", "--branch", args.branch, args.repo_url, rd_dir]).returncode != 0:
                raise SetupError("git clone failed")
        ok("fork checkout ready")

    if not os.path.isfile(os.path.join(rd_dir, "renderdoc.sln")):
        raise SetupError("renderdoc.sln not found under %s -- clone did not produce the expected tree." % rd_dir)

    # phase 2: build
    if args.skip_build:
        step("Build (skipped)")
        if not os.path.isfile(qrenderdoc_exe):
            raise SetupError("--skip-build given but %s does not exist. Build it first (VS2026, Development|x64) or drop --skip-build." % qrenderdoc_exe)
    else:
        build(rd_dir, args.configuration, shim_path, qrenderdoc_exe, renderdoc_dll, renderdoc_json)

    # phase 3: configure erhe
    step("Configure erhe (renderdoc_library_path_override -> built DLL, enabled)")
    configure_erhe(erhe_gfx_json, renderdoc_dll)
    warn("this is a LOCAL change to a tracked file -- keep it uncommitted.")
    ok("erhe graphics config points at " + renderdoc_dll)

    # phase 4: install proxy
    step("Install stdio proxy into .mcp.json")
    install_proxy(mcp_json, proxy_py, qrenderdoc_exe, args.port)
    ok("registered 'renderdoc' stdio proxy (py -3 %s)" % proxy_py)
    info("if you previously ran 'claude mcp add --transport http renderdoc ...', remove it: claude mcp remove renderdoc")

    # phase 5: capture baked schema
    if args.skip_schema:
        step("Capture tool schema (skipped)")
    else:
        step("Capture proxy tool schema -> scripts/renderdoc_tools.json")
        env = os.environ.copy()
        env["ERHE_RENDERDOC_QRENDERDOC"] = qrenderdoc_exe
        env["ERHE_RENDERDOC_MCP_PORT"] = str(args.port)
        rc = subprocess.run([sys.executable, capture_py], env=env).returncode
        if rc != 0 or not os.path.isfile(schema_json):
            warn("schema capture did not complete (qrenderdoc may need a live display). "
                 "Re-run later: py -3 scripts/capture_renderdoc_tools.py")
        else:
            ok("wrote " + schema_json)

    step("Done")
    print("Next steps:")
    print("  - Restart Claude Code so it spawns the stdio proxy (tools register at session start).")
    print("  - The proxy launches qrenderdoc on demand and leaves it running on exit.")
    print("  - See doc/renderdoc_fork.md for the capture/inspection workflow.")


if __name__ == "__main__":
    try:
        main()
    except SetupError as e:
        print("\nERROR: " + str(e), file=sys.stderr)
        sys.exit(1)
