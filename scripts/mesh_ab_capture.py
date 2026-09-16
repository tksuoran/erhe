"""Deterministic A/B screenshot capture through the editor's MCP server.

    scripts/mesh_ab_capture.py <off|on|cache> <out.png>
    ERHE_SHOT_SCENE=<path|->      scene to load ("-" = default startup scene)
    ERHE_SHOT_EXPOSURE=<float>    camera exposure (default 1.0)
    ERHE_SHOT_EXE=<path>          editor build to run (default: ninja vulkan Debug)
    ERHE_SHOT_FRAME=0             keep the asset's authored camera placement

Built for verifying the meshoptimizer work (doc/meshoptimizer-integration.md),
but nothing here is specific to it: it is the general recipe for comparing two
editor renders and having the difference mean something.

Everything it pins exists because leaving it unpinned produced a false result:

  - DDGI accumulates across frames. Unpinned, two IDENTICAL runs differed by 36%.
  - The simulation clock keeps animations and temporal effects moving.
  - The default camera framing depends on load timing.
  - The editor REWRITES config/editor/desktop_windows.json on exit, and a
    different window layout moves the viewport - that reads as a ~96% pixel
    difference which is not a rendering difference at all.

So: always capture a same-setting control pair first and confirm it is 0
differing pixels before believing any A/B number. Crop the ImGui panels out when
comparing (the viewport starts near x=350, y=75 at 2304x1200).

The MCP server is on port 3743 (not 8080). Scene choice matters: use
res/editor/assets/ABeautifulGame.glb (no cameras, animations or skins), not
VirtualCity.glb, whose 14 camera helper boxes clutter the centre of view.

CHECK THE SHOT, not just the number. Bistro's light setup blows the viewport out
to near-solid white at the default exposure, and one saturated viewport compares
equal to another saturated viewport - a meaningless 0 that reads exactly like a
passing A/B. ERHE_SHOT_EXPOSURE=0.001 makes it readable; 0.02 is still almost
entirely clipped. Sanity-check any shot by the fraction of saturated pixels
before believing a diff taken from it.

Bistro in the Debug build is painfully slow, so point ERHE_SHOT_EXE at
build_ninja_win_vulkan_release for it. Both sides of one comparison must of
course use the same executable.

Leaves config/editor/mesh_memory.json modified - restore it with git checkout
when done.
"""
import json, math, os, subprocess, sys, time, urllib.request

REPO  = r"D:\erhe"
# A large scene (Bistro) is painfully slow in the Debug build; point this at
# build_ninja_win_vulkan_release for those. Both builds render the same, so an
# A/B is valid as long as BOTH sides of it use the same executable.
EXE   = os.environ.get("ERHE_SHOT_EXE") or os.path.join(REPO, r"build_ninja_win_vulkan\bin\editor.exe")
CFG   = os.path.join(REPO, r"config\editor\mesh_memory.json")
SCENE = os.environ.get("ERHE_SHOT_SCENE", "res/editor/assets/ABeautifulGame.glb")
# Bistro needs about 0.02; the default is neutral.
EXPOSURE = float(os.environ.get("ERHE_SHOT_EXPOSURE", "1.0"))
# frame_scene() costs ONE RPC PER NODE, which on a scene the size of Bistro is
# thousands of round trips and minutes per run. Set ERHE_SHOT_FRAME=0 there: the
# asset's authored camera is just as deterministic (it is the file's own
# transform, identical on every run) and usually a better shot than a generic
# 3/4 overview.
FRAME = os.environ.get("ERHE_SHOT_FRAME", "1") != "0"
BASE  = "http://127.0.0.1:3743"


def set_flag(key, value):
    text = open(CFG, "r", encoding="utf-8").read()
    want, other = ("true", "false") if value else ("false", "true")
    text = text.replace('"%s": %s' % (key, other), '"%s": %s' % (key, want))
    open(CFG, "w", encoding="utf-8", newline="\n").write(text)
    assert '"%s": %s' % (key, want) in text


def set_optimize(value):
    set_flag("optimize_meshes", value)


def rpc(name, args=None, timeout=90):
    body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                       "params": {"name": name, "arguments": args or {}}}).encode()
    req = urllib.request.Request(BASE + "/mcp", data=body,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        res = json.loads(r.read().decode())
    txt = res.get("result", {}).get("content", [{}])[0].get("text", "")
    try:
        return json.loads(txt)
    except Exception:
        return txt


def wait_health(deadline):
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(BASE + "/health", timeout=2) as r:
                if r.status == 200:
                    return True
        except Exception:
            time.sleep(0.5)
    return False


def look_at_quat(eye, center, up=(0.0, 1.0, 0.0)):
    def sub(a, b):   return [a[i] - b[i] for i in range(3)]
    def cross(a, b): return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
    def norm(a):
        l = math.sqrt(sum(c*c for c in a)) or 1.0
        return [c/l for c in a]
    # erhe / glTF cameras look down -Z.
    z = norm(sub(eye, center))
    x = norm(cross(up, z))
    y = cross(z, x)
    m = [[x[0], y[0], z[0]], [x[1], y[1], z[1]], [x[2], y[2], z[2]]]
    t = m[0][0] + m[1][1] + m[2][2]
    if t > 0.0:
        s = math.sqrt(t + 1.0) * 2.0
        qw = 0.25 * s; qx = (m[2][1]-m[1][2])/s; qy = (m[0][2]-m[2][0])/s; qz = (m[1][0]-m[0][1])/s
    elif m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2.0
        qw = (m[2][1]-m[1][2])/s; qx = 0.25*s; qy = (m[0][1]+m[1][0])/s; qz = (m[0][2]+m[2][0])/s
    elif m[1][1] > m[2][2]:
        s = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2.0
        qw = (m[0][2]-m[2][0])/s; qx = (m[0][1]+m[1][0])/s; qy = 0.25*s; qz = (m[1][2]+m[2][1])/s
    else:
        s = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2.0
        qw = (m[1][0]-m[0][1])/s; qx = (m[0][2]+m[2][0])/s; qy = (m[1][2]+m[2][1])/s; qz = 0.25*s
    return [qx, qy, qz, qw]


def viewport_camera(scene):
    """The camera the viewport is actually rendering with."""
    vp = rpc("get_viewports", {})
    for v in (vp.get("viewports", []) if isinstance(vp, dict) else []):
        for key in ("camera_name", "camera"):  # get_viewports reports "camera"
            if isinstance(v.get(key), str) and v[key]:
                return v[key]
    cams = rpc("get_scene_cameras", {"scene_name": scene})
    lst = cams.get("cameras", cams) if isinstance(cams, dict) else cams
    for entry in (lst or []):
        name = entry if isinstance(entry, str) else (entry.get("name") or entry.get("camera_name"))
        if name:
            return name
    return None


def set_exposure(scene):
    cam = viewport_camera(scene)
    print("  viewport camera:", cam)
    if cam is None:
        return
    print("  exposure:", rpc("edit_camera", {"scene_name": scene, "camera_name": cam, "exposure": EXPOSURE}))


def frame_scene(scene):
    """Point the viewport camera at the whole scene from a fixed 3/4 view."""
    nodes = rpc("get_scene_nodes", {"scene_name": scene})
    names = nodes.get("nodes", nodes) if isinstance(nodes, dict) else nodes
    names = [n if isinstance(n, str) else n.get("name") for n in names]
    lo = [1e30]*3; hi = [-1e30]*3
    for nm in names:
        d = rpc("get_node_details", {"scene_name": scene, "node_name": nm})
        if not isinstance(d, dict):
            continue
        ab = d.get("subtree_world_aabb") or {}
        mn, mx = ab.get("min"), ab.get("max")
        if not mn or not mx:
            continue
        for i in range(3):
            lo[i] = min(lo[i], mn[i]); hi[i] = max(hi[i], mx[i])
    if lo[0] > hi[0]:
        print("  no bounds found; leaving camera alone")
        return None
    c = [(lo[i]+hi[i])/2.0 for i in range(3)]
    diag = math.sqrt(sum((hi[i]-lo[i])**2 for i in range(3)))
    print("  scene bounds min %s max %s diag %.3f" % ([round(v,3) for v in lo], [round(v,3) for v in hi], diag))
    eye = [c[0] + diag*0.55, c[1] + diag*0.45, c[2] + diag*0.55]
    vp = rpc("get_viewports", {})
    print("  get_viewports:", json.dumps(vp)[:400])
    cams = rpc("get_scene_cameras", {"scene_name": scene})
    print("  get_scene_cameras:", json.dumps(cams)[:400])
    cam = None
    for v in (vp.get("viewports", []) if isinstance(vp, dict) else []):
        for k in ("camera_name", "camera"):
            if isinstance(v.get(k), str) and v[k]:
                cam = v[k]; break
        if cam:
            break
    if not cam:
        lst = cams.get("cameras", cams) if isinstance(cams, dict) else cams
        for entry in (lst or []):
            nm = entry if isinstance(entry, str) else (entry.get("name") or entry.get("camera_name"))
            if nm:
                cam = nm; break
    print("  camera to move:", cam)
    if cam:
        print("  edit_camera:", rpc("edit_camera", {"scene_name": scene, "camera_name": cam,
                                                    "z_near": max(diag*0.001, 0.01), "z_far": diag*8.0}))
        print("  cameras after edit:", json.dumps(rpc("get_scene_cameras", {"scene_name": scene}))[:400])
        print("  set_node_transform:", rpc("set_node_transform", {
            "scene_name": scene, "node_name": cam, "space": "world",
            "translation": eye, "rotation_xyzw": look_at_quat(eye, c)}))
    return cam


if __name__ == "__main__":
    mode = sys.argv[1]
    out  = sys.argv[2]
    set_optimize(mode in ("on", "cache"))
    set_flag("mesh_optimize_cache", mode == "cache")
    tgt = os.path.join(REPO, out)
    if os.path.exists(tgt):
        os.remove(tgt)
    # The editor rewrites its window layout on exit, and a different layout
    # moves the viewport - which shows up as a ~96% pixel difference that has
    # nothing to do with what is being measured. Pin the layout for every run.
    import shutil
    live   = os.path.join(REPO, "config", "editor", "desktop_windows.json")
    layout = os.path.join(REPO, "cache", "desktop_windows.pinned.json")
    if os.path.exists(layout):
        shutil.copyfile(layout, live)
    elif os.path.exists(live):
        # First run: snapshot the current layout and reuse it from here on, so
        # every later run in the comparison sees the same viewport rectangle.
        os.makedirs(os.path.dirname(layout), exist_ok=True)
        shutil.copyfile(live, layout)
    # Refuse to start if something is already listening: a leftover editor keeps
    # port 3743, the new one fails to bind, and every RPC below then silently
    # drives the OLD process - with the OLD config. That produces a
    # plausible-looking measurement of the wrong thing.
    try:
        with urllib.request.urlopen(BASE + "/health", timeout=2) as _r:
            raise SystemExit("an editor is already serving %s - kill it first" % BASE)
    except SystemExit:
        raise
    except Exception:
        pass

    args = [EXE] if SCENE == "-" else [EXE, "--scene", SCENE]
    proc = subprocess.Popen(args, cwd=REPO)
    try:
        if not wait_health(time.time() + 150):
            raise SystemExit("MCP never came up")
        time.sleep(30)
        act = rpc("get_active_scene", {})
        scene = act.get("active_scene") or "ABeautifulGame.glb"
        print("scene:", scene)
        # Exposure first, and regardless of framing: a saturated viewport
        # compares equal to another saturated one, so a shot taken at the wrong
        # exposure yields a 0 that means nothing.
        set_exposure(scene)
        if FRAME:
            frame_scene(scene)
        else:
            print("  framing skipped; using the asset's authored camera")
        rpc("set_ddgi", {"enabled": False})
        rpc("advance_time", {"mode": "paused"})
        time.sleep(6)
        print("capture:", rpc("capture_screenshot", {"path": out}))
    finally:
        proc.kill(); proc.wait()
    print("done", mode, out)
