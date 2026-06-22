# Debugging erhe with the RenderDoc fork MCP server

This documents a **tested, end-to-end GPU debugging workflow** for the erhe
editor on Windows/Vulkan that combines three live tools driven from Claude Code:

- the **Visual Studio MCP server** -- to build and *debug-launch* the editor;
- the **RenderDoc fork MCP server** (`D:\renderdoc`, branch `mcp-server`) -- an
  MCP server embedded in `qrenderdoc` that exposes the currently open / captured
  frame to an agent (`list_targets`, `connect_to_target`, `trigger_capture`,
  then `get_pipeline_state` / `get_texture_stats` / `get_pixel_history` / ...);
- erhe's own **in-application RenderDoc API**, which makes the running editor a
  RenderDoc *target* the fork can connect to and capture without injecting.

The fork's full tool reference and design notes live in
[`D:\renderdoc\docs\mcp.md`](file:///D:/renderdoc/docs/mcp.md). This file is the
erhe-specific recipe and a worked example (the "black atmosphere sky" bug, found
and fixed with exactly this loop).

> The classic alternative -- run `editor.exe` under the stock qrenderdoc UI by
> hand, click *Capture Frame*, and eyeball the Texture Viewer -- still works.
> What this adds is that the **agent** can drive the capture and read pipeline
> state / texture statistics / pixel history back programmatically, which is what
> makes "edit -> rebuild -> recapture -> diff the numbers" a tight loop.

---

## One-time setup

### 1. Point erhe at the fork's `renderdoc.dll`

In `config/editor/erhe_graphics.json`:

```json
"renderdoc_capture_support": true,
"renderdoc_library_path": "D:\\renderdoc\\x64\\Development\\renderdoc.dll",
```

`renderdoc_capture_support` makes erhe load the RenderDoc in-application API at
startup; `renderdoc_library_path` overrides *which* `renderdoc.dll` is loaded
(default would be `C:\Program Files\RenderDoc\renderdoc.dll`, the stock install).
It **must** be the fork's DLL: a target-control connection is version-sensitive,
so the editor's in-app layer and the connecting `qrenderdoc` have to be the
**same build**.

This path is machine-specific (an absolute path into a local checkout); keep it
as a **local, uncommitted** working-tree change, not something pushed to the
repo.

`initialize_frame_capture()` in
`src/erhe/window/erhe_window/renderdoc_capture.cpp` reads the override and, on
Windows, also sets the Vulkan-loader env vars that let the fork's *implicit
capture layer* win over the stock install (see the next section). Confirm it took
effect in `logs/log.txt`:

```
[erhe.window.renderdoc] RenderDoc: override library 'D:\renderdoc\x64\Development\renderdoc.dll' active, VK_ADD_IMPLICIT_LAYER_PATH='D:\renderdoc\x64\Development'
```

### 2. Layer coexistence (fork 1.45 vs stock 1.44) -- why it doesn't `__debugbreak`

A stock RenderDoc install registers an implicit Vulkan layer system-wide. If the
editor loaded **two** RenderDoc copies it would hit RenderDoc's multi-instance
`__debugbreak()`. erhe avoids this entirely in
`apply_renderdoc_override_env()`:

| env var | value | effect |
|---|---|---|
| `DISABLE_VULKAN_RENDERDOC_CAPTURE_1_44` | `1` | disables the stock 1.44 layer |
| `DISABLE_VULKAN_RENDERDOC_CAPTURE_1_41` | `1` | disables a 1.41 layer if present |
| `VK_ADD_IMPLICIT_LAYER_PATH` | fork dir | lets the loader find the fork layer |
| `ENABLE_VULKAN_RENDERDOC_CAPTURE` | `1` | enables the (now only) fork layer |

The version numbers come from each `renderdoc.json`'s `disable_environment` key:
the stock build is RenderDoc **1.44** (`..._1_44`), the fork is **1.45**
(`..._1_45`). erhe disables 1.44/1.41 but **not** 1.45, so exactly the fork layer
loads. If you upgrade either install, re-check those numbers.

You can leave the stock `qrenderdoc.exe` open at the same time -- it's 1.44 and
cannot connect to the 1.45 editor layer, so it won't grab the target; only the
fork's MCP `qrenderdoc` connects.

### 3. Register the renderdoc MCP server with Claude Code (stdio proxy)

Claude Code connects every MCP server **at session start** and never lets the
model reconnect one mid-session (only the user can, via `/mcp`). So pointing
Claude Code straight at `http://127.0.0.1:7398/` only yields callable
`mcp__renderdoc__*` tools if `qrenderdoc` happened to be running when Claude Code
started -- the opposite of launching it on demand.

To get on-demand launch **and** native tools, register the **stdio proxy**
`scripts/renderdoc_mcp_proxy.py` instead of the raw http endpoint. A stdio
server is always spawned by Claude Code at startup, so its tools are always
registered; the proxy advertises a baked-in copy of the fork's tool schema and
launches `qrenderdoc --mcp-server` lazily on the first tool call that needs the
backend (or eagerly via its `renderdoc_launch` tool). It **leaves `qrenderdoc`
running** when Claude Code exits -- only the proxy's `renderdoc_shutdown` tool
tears down a `qrenderdoc` the proxy itself launched.

In `.mcp.json` (project-local, uncommitted):

```json
"renderdoc": { "command": "py", "args": ["-3", "D:/alt/erhe/scripts/renderdoc_mcp_proxy.py"] }
```

If you previously ran `claude mcp add --transport http renderdoc ...`, remove it
(`claude mcp remove renderdoc`) so the failed http entry does not shadow the
proxy.

**One-time: capture the tool schema.** The proxy serves the fork's tools from
`scripts/renderdoc_tools.json`. Generate it once (with the fork built; this
launches `qrenderdoc` on demand if it is not already running):

```sh
py -3 scripts/capture_renderdoc_tools.py
```

Re-run it whenever the fork's tool set changes. Until the file exists, only the
proxy's own management tools (`renderdoc_launch`, `renderdoc_status`,
`renderdoc_shutdown`) are advertised; the fork's capture/inspection tools appear
once it is generated.

The proxy honors these environment overrides (defaults shown):
`ERHE_RENDERDOC_QRENDERDOC=D:\renderdoc\x64\Development\qrenderdoc.exe`,
`ERHE_RENDERDOC_MCP_HOST=127.0.0.1`, `ERHE_RENDERDOC_MCP_PORT=7398`,
`ERHE_RENDERDOC_LAUNCH_TIMEOUT=60`.

After editing `.mcp.json`, **restart Claude Code** (the proxy is spawned per
session at startup).

#### Driving the server without the proxy (HTTP fallback)

The fork is a stateless JSON-RPC-over-POST endpoint, so you can always skip the
proxy and drive it directly over HTTP -- see
[HTTP fallback](#http-fallback-driving-the-server-without-native-tools) below.
That path is also on-demand (start `qrenderdoc` from a shell, then POST); it just
lacks the native `mcp__renderdoc__*` tool surface.

---

## The workflow (per debugging session)

### Step 1 -- start the fork qrenderdoc with the MCP server

With the stdio proxy (section 3) this is automatic: the first `mcp__renderdoc__*`
tool call launches `qrenderdoc --mcp-server` on demand, or call
`mcp__renderdoc__renderdoc_launch` to pre-warm it (so the first capture call does
not pay the cold-start delay) and `mcp__renderdoc__renderdoc_status` to confirm.
You only need the manual command below for the HTTP-fallback path:

```powershell
Start-Process "D:\renderdoc\x64\Development\qrenderdoc.exe" -ArgumentList "--mcp-server","--mcp-port","7398"
```

It listens on `127.0.0.1:7398`. Sanity-check the handshake:

```powershell
$body = '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"x","version":"1"}}}'
Invoke-WebRequest http://127.0.0.1:7398/ -Method POST -Body $body -ContentType application/json -UseBasicParsing | Select -Expand Content
```

### Step 2 -- debug-launch the editor via the Visual Studio MCP server

With the erhe solution open in VS (`build_vs2026_vulkan/erhe.slnx`) and `editor`
as the startup project:

- `mcp__visualstudio__debugger_launch` (F5). The editor loads the fork
  `renderdoc.dll` and starts presenting Vulkan frames.

Wait for it to actually be rendering before capturing -- don't blind-sleep. Poll
`logs/log.txt` for a readiness line, e.g. (sky mode 1):

```
[editor.render] Sky_renderer::render_atmosphere first call: supported=true luts_ready=true ... views=1
```

or poll the MCP server until the editor shows up as a target (Step 3).

### Step 3 -- connect RenderDoc to the running editor and capture

The editor is already RenderDoc-enabled (in-app API), so **connect, don't
inject** (`attach_to_process` would add a conflicting second copy):

```
mcp__renderdoc__list_targets            -> {"targets":[{"ident":38920,"pid":1756,"target":"editor"}]}
mcp__renderdoc__connect_to_target  pid=1756
mcp__renderdoc__get_attach_status       -> {"apiPresenting":true, "attached":true, ...}
mcp__renderdoc__trigger_capture    open=true timeoutMs=25000
        -> capture saved to %TEMP%\RenderDoc\editor_<...>_frame<N>.rdc and opened in the replay view
```

`open:true` loads the capture into the live `qrenderdoc` replay session, which is
exactly the state the inspection tools read.

### Step 4 -- inspect

Typical flow (all read-only except `set_event`, which moves the live UI):

```
mcp__renderdoc__search_actions   query="Sky"           -> find the marker/draw event ids
mcp__renderdoc__set_event        eventId=<draw>
mcp__renderdoc__get_pipeline_state                      -> bound shaders, render targets, depth target
mcp__renderdoc__get_depth_stencil_state / get_viewports_scissors
mcp__renderdoc__get_postvs_data                         -> did the (fullscreen) geometry land where expected?
mcp__renderdoc__list_textures                           -> resource ids by debug label
mcp__renderdoc__get_texture_stats  resourceId=...       -> min/max/mean + NaN/Inf/zero/one counts ("is this texture broken?")
mcp__renderdoc__get_constant_buffer_contents stage=fragment slot=0  -> the camera UBO values fed to the shader
mcp__renderdoc__get_pixel_history  resourceId=<RT> x=.. y=..        -> did the fragment pass depth/stencil? what colour did it write?
mcp__renderdoc__save_texture       resourceId=<RT> path=...png      -> a viewable image (HDR targets are pre-tonemap)
```

`get_texture_stats` + `get_pixel_history` are the two highest-signal tools for a
"renders nothing" bug: stats tell you instantly whether a LUT/RT is all-zero or
NaN, and pixel history tells you whether a draw was rejected (depth/stencil) vs
ran-but-wrote-black.

### Step 5 -- fix, rebuild, recapture (the tight loop)

1. Edit the source.
2. `mcp__visualstudio__debugger_stop`
3. `mcp__visualstudio__build_project`
   `projectName=D:\alt\erhe\build_vs2026_vulkan\src\editor\editor.vcxproj`, then
   poll `mcp__visualstudio__build_status` until `State=="Done"` /
   `FailedProjects==0` (confirm with the `editor.exe` mtime -- `build_status` can
   read `Done` from the previous build for a moment).
4. `mcp__visualstudio__debugger_launch` again.
5. Re-`connect_to_target` (the `pid` changes each launch; the `ident` is stable),
   `trigger_capture`, and re-read the same stats -- now you can diff before/after
   numerically.
6. `mcp__renderdoc__detach_process` when done (the editor keeps running).

---

## Worked example: the black atmosphere sky

The procedural-sky atmosphere mode (`Sky_config::mode == 1`, see
[`doc/sky.md`](sky.md)) rendered only the clear colour. The handoff's leading
hypothesis was "the storage-image compute LUT writes aren't landing (LUTs are all
zero)". This loop **disproved** that and found the real cause in minutes.

**Inspection (capture of frame 1205):**

- LUT textures are fine, not zero. `get_texture_stats` on `Sky transmittance LUT`
  (`R16G16B16A16F`, 256x64): R mean **0.73**, G 0.58, B 0.54, no NaN/Inf -- a
  proper transmittance gradient. Multi-scatter LUT similarly populated. So the new
  storage-image compute path **works**.
- The draw isn't depth/stencil-rejected. `get_pixel_history` on a sky pixel at the
  atmosphere draw: `passed:true`, but `shaderOut.color = [0,0,0,1]`. So the
  fragment shader **runs and writes**, and outputs **black**.
- Why black? `get_constant_buffer_contents` (camera UBO) showed
  `sun_direction = [-0.058, -0.912, -0.406, 37.1]` -- the sun's Y is **-0.91**,
  i.e. ~66 degrees **below the horizon**. A physically-based atmosphere correctly
  integrates a near-black night sky for a below-horizon sun (the shader's
  `horizon_fade`, ground-shadow and `cos_theta < 0` sun-disc test all collapse to
  ~0).

**Root cause:** in `Sky_renderer::render_atmosphere`
(`src/editor/renderers/sky_renderer.cpp`) the sun direction taken from the scene's
directional light was **negated**:

```cpp
const glm::vec3 light_direction = world_from_node * vec4(0,0,1,0);
toward_sun = -glm::normalize(light_direction);   // BUG: double-negates
```

erhe stores a directional light's direction as `world_from_node * +Z` and uses it
**directly** as the toward-the-light vector in shading
(`standard.frag`: `L = normalize(light.direction_and_outer_spot_cos.xyz); dot(N,L)`;
written by `light_buffer.cpp`). So `light_direction` here is already
"toward the sun"; negating it put the sun below the horizon.

**Fix:** drop the negation -- `toward_sun = glm::normalize(light_direction)`.

**Verification (rebuild + recapture, frame 1408), same tools, numbers diffed:**

| | before | after |
|---|---|---|
| `sun_direction.xyz` | `[-0.058,-0.912,-0.406]` (below horizon) | `[+0.058,+0.912,+0.406]` (above horizon) |
| sky-pixel `shaderOut.color` | `[0,0,0,1]` (black) | `[2.20,3.34,4.22,1]` (blue, B>G>R = Rayleigh) |
| resolve-target exactly-zero pixels | 88,424 | 872 |
| resolve-target mean R/G/B | 0.06/0.07/0.06 | 0.61/0.70/0.78 |

The atmosphere now renders a bright blue daytime sky. The whole identify ->
fix -> prove cycle was driven from the agent via these two MCP servers.

---

## Gotchas learned doing this

- **MCP tools need the server up at session start.** This is exactly what the
  stdio proxy (section 3) removes: the proxy is the thing up at session start, so
  its tools are always registered and it launches `qrenderdoc` on demand. The
  caveat only bites the **raw http registration / HTTP fallback** -- there,
  `claude mcp list` showing `Connected` is necessary but not sufficient for the
  `mcp__renderdoc__*` tools to be callable in an already-running session; use
  `/mcp` to reconnect or the HTTP fallback below.
- **Connect, don't inject.** The editor already has the in-app RenderDoc, so use
  `list_targets` + `connect_to_target`. `attach_to_process` would inject a second
  copy and conflict.
- **Same build on both ends.** The editor's `renderdoc_library_path` DLL and the
  `qrenderdoc.exe` you run `--mcp-server` from must be the same fork build.
- **Once-at-startup work isn't in a mid-run frame capture.** The sky LUTs are
  generated once (`ensure_luts` sets a ready flag), so a frame-1408 capture has no
  `Sky LUT generation` dispatch -- inspect the **LUT textures** as persistent
  resources instead (they keep their `Texture_create_info::debug_label`, e.g.
  `Sky transmittance LUT`, which is how `list_textures` finds them). Debug-group
  markers (`erhe::graphics::Scoped_debug_group`, e.g. the `Sky atmosphere` marker)
  are what make draws findable via `search_actions` -- keep them.
- **MSAA render targets:** the viewport colour target is multisampled; pass
  `sample` to `get_pixel_history` / `pick_pixel`, or use the resolved
  (`...color texture`, non-`multisampled`) sibling. `get_texture_stats` /
  `save_texture` resolve-average by default.
- **HDR targets save dark.** `save_texture` on a pre-tonemap `R16G16B16A16F`
  viewport target writes the raw linear values; an LDR PNG of it looks dim
  relative to what the editor shows after bloom+tonemap. Read the **stats**, don't
  judge brightness by the PNG.
- **`save_texture` path escaping.** Pass the `path` with forward slashes
  (`D:/alt/erhe/logs/x.png`); back-to-back backslashes in a hand-built JSON string
  can collapse and write to a mangled path. The parent directory must already
  exist and `overwrite:true` is required to replace a file.
- **`build_status` can read stale `Done`.** The VS build is async; confirm the
  rebuild by the `editor.exe` mtime, not just `build_status`.

---

## HTTP fallback: driving the server without native tools

The server is stateless JSON-RPC over HTTP POST (no session handshake required per
call), so any HTTP client drives it. Minimal PowerShell helper:

```powershell
$Uri = "http://127.0.0.1:7398/"
function Rd-Tool([string]$Name, $Arguments = @{}) {
    $payload = @{ jsonrpc="2.0"; id=1; method="tools/call";
                  params=@{ name=$Name; arguments=$Arguments } } | ConvertTo-Json -Depth 20 -Compress
    $r = Invoke-WebRequest $Uri -Method POST -Body $payload -ContentType application/json `
         -Headers @{ Accept = "application/json, text/event-stream" } -UseBasicParsing
    ($r.Content | ConvertFrom-Json).result.content[0].text   # tool result is JSON-in-text
}

Rd-Tool list_targets
Rd-Tool connect_to_target @{ pid = 1756 }
Rd-Tool trigger_capture   @{ open = $true; timeoutMs = 25000 }
Rd-Tool get_texture_stats @{ resourceId = "ResourceId::436" }
```

Tool arguments are exactly the schemas in
[`D:\renderdoc\docs\mcp.md`](file:///D:/renderdoc/docs/mcp.md); the result of each
`tools/call` is a single text block containing the tool's JSON payload.
