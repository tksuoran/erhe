# Running and driving the editor as an agent

Stability: stable

How an agent session launches the editor, drives it over the in-editor MCP
server, captures screenshots, and hands off to the user. Skill:
**`erhe-headless-verify`** is the condensed run-book for the standard loop
(build headless -> launch -> wait for the MCP server -> drive tools ->
screenshot -> clean up). Tool reference: `doc/agents/mcp_server_usage.md`;
UI driving: `doc/agents/mcp_ui_driving.md`; MCP tool design rules:
`doc/agents/mcp_api_guidelines.md`.

## Launching

- Run the editor with the repo root as working directory (`config/`, `res/`,
  `logs/` are cwd-relative).
- Set `ERHE_AI_DRIVER=1` in the editor's environment (below).
- Kill any editor left over from a previous run before launching (a stale
  editor keeps the MCP port and silently answers every call from an old
  binary) - but only editors you started: the user may be running their own
  editor on 3743. Your editor then falls back to the next free port; read the
  bound port from `logs/log.txt` (`MCP server: listening on 127.0.0.1:<port>`)
  and set `ERHE_MCP_PORT` for the client scripts.
- Windows: the editor is a console-subsystem binary; launch it with a hidden
  console:
  `powershell Start-Process -FilePath <build>\bin\editor.exe -WorkingDirectory <repo> -WindowStyle Hidden`.
- Poll readiness, never blind-sleep: `GET /health` answers 200 once the main
  loop serves requests, or grep `logs/log.txt` for
  `Main loop: completed frame 12`.
- Scene node ids reshuffle on every launch - re-query with `get_scene_nodes`
  before every `select_items`; never reuse an id from a previous run.

### Headless vs windowed

The windowed build creates a real WSI swapchain and needs a present-capable
GPU queue on the window surface. When the display is powered off / asleep /
disconnected (common when an agent drives the machine unattended),
`choose_physical_device()` finds no usable GPU and the editor aborts at
startup; the log says so and recommends the fix (look for "Switch to the
HEADLESS build" in `logs/log.txt`). Run the headless build instead
(`build_vs2026_vulkan_headless/bin/Debug/editor.exe` on Windows,
`build_xcode_metal_headless` on macOS): it is surfaceless (emulated
swapchain), needs no display, runs the full render pipeline + MCP server, and
supports `capture_screenshot`. RenderDoc captures need the windowed build and
a live display.

## AI-driven editor runs (`ERHE_AI_DRIVER=1`)

With `ERHE_AI_DRIVER=1` the editor routes error artifacts to files the agent
can read instead of the clipboard (which assumed a human pastes the message
into an AI chat):

- Device / validation errors: full message + callstack appended to
  `logs/device_error.txt` (truncated at each run's first error).
- Shader compile/link errors: `logs/shader_error.txt` (error, source,
  callstack).
- The per-user editor state file (`config/editor/user_state.json`: inventory /
  hotbar slots, per scene view scene, camera and visual style selections) is
  neither read nor written, so an agent run starts from that struct's defaults
  and leaves the user's own state alone. `config/editor/editor_settings.json`
  is read and autosaved as usual.
- Fatal behavior is unchanged (errors still abort); the log line names the
  file to read.

## Logs

The editor writes its spdlog output to `logs/` relative to the working
directory: `logs/log.txt` is the main log, `logs/vulkan.txt` and
`logs/openxr.txt` capture backend-specific traces. Redirecting `stdout` of
`editor.exe` is not enough -- the file sink writes via spdlog and a redirected
stdout stays empty even when the app runs fine. Verify init-time and runtime
behavior by grepping `logs/log.txt`. On Android / Quest the same lines flow
through `adb logcat` (tag `erhe`).

## In-editor MCP server

The `editor` executable embeds an MCP server
(`src/editor/mcp/mcp_server.{hpp,cpp}`), auto-started at launch on
`http://127.0.0.1:3743/mcp` (JSON-RPC 2.0 over `POST`; methods `initialize`,
`tools/list`, `tools/call`). `ERHE_MCP_PORT` overrides the preferred port for
the editor and for `scripts/mcp_call.py` / `scripts/erhe_mcp.py`. If the port
is taken the server falls back to the next free port within `[3743, 3763)`.
It runs on a background thread and dispatches each call to the main thread
(`process_queued_requests()` once per frame), so it is safe to drive a
running editor. Auth is off unless `~/.agents/erhe_mcp_token` exists (mode
0600); when present, every request needs `Authorization: Bearer <token>`.
The server can be registered as native `mcp__erhe__*` tools ("Registering as
an HTTP MCP server" in `doc/agents/mcp_server_usage.md`); registration only
helps sessions that start while an editor already runs on the registered
port, so scripted flows drive it over plain HTTP:

```bash
py -3 scripts/mcp_call.py --list                  # list tool names (optionally: --list <substring>)
py -3 scripts/mcp_call.py get_scene_lights        # call with no arguments
py -3 scripts/mcp_call.py create_shape b64:eyJzaGFwZSI6ICJib3gifQ==   # args as base64 JSON
```

**Drive it with `scripts/mcp_call.py`** -- it handles the JSON-RPC envelope,
the bearer token, and (via the `b64:` argument form or `-` for stdin)
sidesteps PowerShell 5.1's quote mangling of inline JSON.

Use it to set up / inspect / mutate a scene for any debugging need. Tools
fall into:
- **Queries**: `list_scenes`, `get_scene_nodes`, `get_node_details`,
  `get_scene_cameras`, `get_scene_lights`, `get_scene_materials`,
  `get_material_details`, `get_scene_textures`, `get_scene_brushes`,
  `get_brush_geometry_states`, `get_selection`, `get_undo_redo_stack`,
  `get_physics_items`, `get_shadow_fit_debug`, `get_async_status`.
- **Actions**: `create_shape`, `create_node`, `place_brush`,
  `request_brush_geometry`, `select_items`, `transform_selection`,
  `reparent_item`, `create_scope`, `edit_material`,
  `lock_items`/`unlock_items`, `add_tags`/`remove_tags`, `toggle_physics` +
  the `*_physics_*` family, mesh-component editing
  (`set_mesh_component_mode`, `select_mesh_components`,
  `remesh`/`decimate`/`smooth`, ...), `save_scene`,
  `export_gltf`/`import_gltf`, and `capture_screenshot`.
- **ImGui introspection**: `get_imgui_hosts`, `get_imgui_windows`,
  `get_imgui_items`, `get_imgui_item_rect`, plus
  `capture_screenshot.annotate_imgui_items`.
- **Input gestures**: `imgui_click`, `imgui_hover`, `imgui_scroll`,
  `mouse_click`, `mouse_drag`, `mouse_release`, `mouse_wheel`, `key_press`,
  `type_text`, `inject_input_events`, `get_input_state`,
  `get_transform_handles` -- real window input events, so menus, docking,
  property rows, gizmo drags and viewport gestures are driven the way a user
  drives them (`doc/agents/mcp_ui_driving.md`).

**Augment the API when a capability is missing.** If a debugging task needs
an editor action the MCP server does not expose, add a new tool (a
`query_*`/`action_*` handler + the `req->tool_name == "..."` dispatch entry +
its `tools/list` schema in `refresh_tool_list`) rather than working around it
from outside; the MCP surface is first-class debugging infrastructure.

**Scripted startup scene**: `config/editor/commands.json` is a startup script
(`scene.add_cameras` / `add_lights` / `add_room` / `add_platonic_solids` / ...
with arg blocks; see `doc/editor/command_script.md` and
`src/editor/config/definitions/*.py` for arg fields). Adjust it to stand up a
reproducible test scene before the editor finishes init.

## Screenshots

`capture_screenshot` (default `logs/mcp_screenshot.png`) works in both builds.
Headless: `Device::capture_last_frame` reads back the emulated swapchain
synchronously. Windowed: the tool arms a one-shot swapchain capture
(`Device::request_frame_capture`), the MCP server defers the request one frame
while the swapchain render pass records a copy of the composited image, and
the retry returns the pixels. The windowed path needs the surface to grant
`TRANSFER_SRC` image usage (all desktop drivers do) and a supported 4x8-bit
swapchain format; otherwise the tool errors. The macOS Metal build has the
same windowed path. It captures the editor's own frame, so occlusion by other
windows does not matter.

`capture_screenshot` is the screenshot path in every build. Never use
`screencapture` or other system capture tools on macOS (they trigger the
endpoint security agent). OS-level window capture on Windows
(`py -3 scripts/capture_window.py`; PrintWindow by default, `--foreground`
raises the window and blits from the screen) remains only for cases the
in-editor capture cannot serve, and **needs the user's permission every
time**: the user may be using the computer for something else, the editor may
not be topmost, and the user may be live-driving the editor. A granted
permission covers only the capture(s) it was asked for.

## ImGui ini file and default layout

`config/editor/desktop_window_imgui_host_imgui.ini` (window layout state) is
gitignored and rewritten by every editor run on exit -- never add it (or other
erhe_imgui window/ini state files) to the repo, and never delete the user's
copy. When the ini is absent at startup, the default layout is built
procedurally from `config/editor/default_layout.json`, an ordered list of dock
placements (codegen structs; see `src/editor/editor_default_layout.cpp`). To
iterate on the default layout, edit the JSON (no rebuild needed), delete the
ini, and relaunch; a present ini always wins untouched. Restore any tracked
config file (e.g. `config/editor/desktop_windows.json`) that a run rewrote.

## Interactive runs need the user

When a bug can only be reproduced by live mouse/keyboard/controller input
that the MCP gesture tools cannot drive, add `log_*` instrumentation on the
suspected path, build the windowed editor, then have the user perform the
gesture while the file sink records to `logs/log.txt`. Prompt the user to get
ready and wait for their go-ahead before launching the windowed editor; after
launch, tell them the exact gesture to perform and the log prefix you will
grep for, then read `logs/log.txt` once they confirm.

## Once the user starts testing, stop headless testing yourself

For any change with a user-interaction aspect: once you consider the change
complete and the user has started testing it, stop running the headless
verify loop yourself unless the user asks for it. The useful signal is then
the user's verification in a real interactive session; headless runs in
parallel only slow the work down (and re-dirty `logs/` and the ini state).
Headless verification is valuable earlier, while you iterate: drive the
menu- and mouse-driven entry points through the UI-driving tools, the same
path the user's hands would take. The user's verdict still has the final
word, because a headless run exercises only the path you thought to script
(precedent: the #258 headless run docked the MCP-created viewport correctly,
but the menu-driven "Create Scene" viewport was still floating -- the user
found that interactively).
