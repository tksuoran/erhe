---
name: erhe-headless-verify
description: Verify editor behavior end-to-end without a display -- build the headless Vulkan editor, launch it, wait for its embedded MCP server, drive it with scripts/mcp_call.py (scene queries and mutations, geometry graph tools, undo/redo, capture_screenshot), read the screenshot PNG to SEE the result, then clean up. Use this whenever a change needs runtime verification and no interactive UI is required or no live display is available -- "does the mesh render", "does save/load round-trip", "does undo restore state", "what does the viewport look like now". This is the standard verification loop for editor changes on Windows; the windowed build cannot capture screenshots (returns "Frame capture not available") and aborts at startup when the display is off/asleep.
---

# erhe headless verification (headless Vulkan build + in-editor MCP)

The condensed run-book for the loop used to verify editor changes headlessly.
Full reference: CLAUDE.md "In-editor MCP server".

## Step 1 -- build

```bat
cmake --build build_vs2026_vulkan_headless --target editor --config Debug
```

If `build_vs2026_vulkan_headless/` does not exist, configure once with
`scripts\configure_vs2026_vulkan_headless.bat`. (The windowed ninja build
`scripts\build_ninja_win_vulkan.bat editor` verifies compilation faster, but
only the headless build supports `capture_screenshot`.)

## Step 2 -- launch and wait for the MCP server (NEVER blind-sleep)

FIRST kill any editor left over from a previous run. A stale editor.exe keeps
port 8080 and SILENTLY eats every MCP call, so you drive an OLD binary and see
impossible results (a fix that "does nothing", node counts that grow across
runs) -- this wastes a lot of time. Then launch from the **repo root** (config/,
res/, logs/ are cwd-relative) and poll `logs/log.txt` for the listening line
(startup takes a variable few seconds; the port can fall back within [8080, 8100)):

```powershell
Get-Process editor -ErrorAction SilentlyContinue | Stop-Process -Force   # no stale server on 8080
Start-Sleep -Milliseconds 800
if (Test-Path logs\log.txt) { Clear-Content logs\log.txt }
$p = Start-Process -FilePath "build_vs2026_vulkan_headless\src\editor\Debug\editor.exe" -WorkingDirectory (Get-Location) -PassThru -WindowStyle Hidden
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Milliseconds 1000
    if ((Test-Path logs\log.txt) -and (Select-String -Path logs\log.txt -Pattern "MCP server: listening" -Quiet)) { break }
}
Select-String -Path logs\log.txt -Pattern "MCP server: listening"   # "... on 127.0.0.1:8080 (pid <pid>, built <ts>)"
```

Then CONFIRM you are talking to the process you just launched (not a stale one):

```bash
py -3 scripts/mcp_call.py get_server_info    # { name, version, pid, build, port }
```

Assert the reported `pid` == `$p.Id` and `build` is your just-built binary; a
mismatch, or a fallback port (8081+) in the listening line, means another
editor.exe owns 8080 -- kill it and relaunch. Remember `$p.Id` for cleanup.

## Step 3 -- drive it with scripts/mcp_call.py

```bash
py -3 scripts/mcp_call.py --list                          # discover tools (--list <substring> filters)
py -3 scripts/mcp_call.py list_scenes
py -3 scripts/mcp_call.py capture_screenshot              # -> logs/mcp_screenshot.png
py -3 scripts/mcp_call.py get_scene_nodes b64:<base64-of-{"scene_name":"Default Scene"}>
```

- Pass tool arguments as `b64:<base64 of the JSON object>` (or `-` + stdin).
  Do NOT inline JSON with spaces on a PowerShell command line -- PowerShell 5.1
  mangles the quotes. In PowerShell:
  `$b64 = "b64:" + [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($json))`.
- Non-default port: `--port <n>` (from the listening line).
- Mutations (create_shape, geometry_graph_*, transform_selection, ...) take
  effect synchronously -- query tools immediately reflect them, and the
  geometry graph tools are undoable (drive `undo` / `redo` as tools, inspect
  with `get_undo_redo_stack`).
- Long evaluations: a mutation that triggers heavy main-thread work makes the
  server reply "Request timed out" (-32000) -- but the queued request STILL
  EXECUTES when the main thread gets to it ("Server busy: request queue full"
  requests are dropped instead). Retry queries freely; NEVER blindly re-issue
  a timed-out mutation (it double-executes and corrupts undo/redo counts) --
  poll a cheap query until the server drains. Reference implementation:
  `call()` / `mutate()` in `scripts/geometry_nodes_smoke_test.py`.
- Geometry nodes regression sweep: `py -3 scripts/geometry_nodes_smoke_test.py`
  (65 checks; exit 0 = pass). Its incremental section needs
  `config/editor/logging.json` `"editor.graph_editor": "trace"` before launch
  -- revert before committing. Clean up `res/editor/graphs/smoke_*.json` after.
- After `capture_screenshot`, `Read` `logs/mcp_screenshot.png` to actually see
  the frame. A useful trick to identify an object visually: select it and
  `transform_selection` it, then re-screenshot and diff by eye.
- Missing capability? Add a new MCP tool to
  `src/editor/mcp/mcp_server.{hpp,cpp}` rather than working around it --
  CLAUDE.md calls this out as first-class debugging infrastructure.

## Step 4 -- clean up (always)

```powershell
Stop-Process -Id <pid> -Force
git checkout -- config/editor/desktop_window_imgui_host_imgui.ini   # editor rewrites it on exit
git status --short    # confirm nothing else got dirtied (never commit the ini)
```

## Gotchas

- `capture_screenshot` works ONLY in the headless build; the windowed build
  returns "Frame capture not available" (use the RenderDoc fork /
  erhe-renderdoc-gpu-debug for windowed GPU inspection instead).
- The windowed editor needs a live, awake display; headless does not.
- The scripted startup scene comes from `config/editor/commands.json` --
  adjust it when a test needs a reproducible scene before init completes.
- One editor instance at a time. A second instance binds the next port in
  [8080, 8100), so your `mcp_call.py` (which defaults to 8080) keeps hitting the
  FIRST, stale one. Always kill all editors before launching (Step 2) and verify
  `get_server_info` reports the pid/build you expect. `get_server_info` + the
  `(pid ..., built ...)` suffix on the listening line exist precisely for this.
