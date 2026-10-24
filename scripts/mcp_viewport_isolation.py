"""Makes one viewport window the only rendering viewport of a running editor.

A measurement or gate that drives "the viewport" over MCP must not depend on
the window state the working copy's config/editor/desktop_windows.json and
the ImGui ini restore: a second viewport window doubles the frame's GPU work,
and a target viewport docked behind another tab (or hidden) renders nothing,
so a check waiting for its frame never completes. isolate_viewport() hides
every other viewport window, shows and focuses the target (focus also selects
its dock tab) and returns once the editor reports the target visible with a
non-empty viewport rectangle and every other viewport window hidden.

call(tool, arguments) is the caller's MCP helper, returning the tool's
parsed JSON result.
"""

import time


def _viewports(call):
    reply = call("get_viewports", {})
    if not isinstance(reply, dict) or "viewports" not in reply:
        raise RuntimeError(f"get_viewports: {str(reply)[:300]}")
    return reply["viewports"]


def _imgui_window_hidden(call):
    """ImGui window name -> hidden. A viewport window's ImGui name is its
    get_viewports title ("Default Scene###Viewport_window 1"); the display
    names of two viewports can be equal. get_viewports keeps reporting the
    rectangle of a viewport behind another dock tab, so its size does not
    tell whether it renders - the ImGui flags do: a window is shown when it
    was submitted last frame (active) and is not a hidden dock tab."""
    reply = call("get_imgui_windows", {})
    windows = reply.get("windows", []) if isinstance(reply, dict) else []
    return {w.get("name", ""): (not w.get("active", False)) or bool(w.get("hidden", True)) for w in windows}


def isolate_viewport(call, scene_name=None, title=None, timeout_s=20.0):
    """Returns the get_viewports entry of the target viewport: the one named
    title, else the first one showing scene_name, else the first one.
    Raises RuntimeError when no such viewport exists or the state is not
    reached within timeout_s."""
    viewports = _viewports(call)
    if not viewports:
        raise RuntimeError("no viewport windows")
    if title is not None:
        target = next((v for v in viewports if v.get("title") == title), None)
    elif scene_name is not None:
        target = next((v for v in viewports if v.get("scene") == scene_name), None)
    else:
        target = viewports[0]
    if target is None:
        listed = [(v.get("title"), v.get("scene")) for v in viewports]
        raise RuntimeError(f"no viewport for title {title!r} / scene {scene_name!r}; viewports: {listed}")
    target_title = target["title"]
    others = [v["title"] for v in viewports if v["title"] != target_title]
    for other in others:
        call("set_window_visibility", {"title": other, "visible": False})
    call("set_window_visibility", {"title": target_title, "visible": True, "focus": True})

    deadline = time.time() + timeout_s
    state = ""
    while time.time() < deadline:
        time.sleep(0.25)
        current = next((v for v in _viewports(call) if v.get("title") == target_title), None)
        hidden = _imgui_window_hidden(call)
        target_shown = (target_title in hidden) and not hidden[target_title]
        others_hidden = all(hidden.get(other, True) for other in others)
        sized = (current is not None) and (current.get("width", 0) > 0) and (current.get("height", 0) > 0)
        state = (f"target {target_title!r} submitted {target_title in hidden} hidden {hidden.get(target_title)} "
                 f"size {current.get('width') if current else None}x{current.get('height') if current else None}; "
                 f"others shown {[o for o in others if not hidden.get(o, True)]}")
        if target_shown and others_hidden and sized:
            return current
    raise RuntimeError(f"viewport isolation not reached in {timeout_s:.0f} s: {state}")
