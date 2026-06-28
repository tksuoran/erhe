---
name: erhe-renderdoc-gpu-debug
description: GPU-debug the erhe editor by capturing a frame of the windowed Vulkan build with the tksuoran/renderdoc fork MCP server and reading back pipeline state, render targets, per-texel statistics, AND the decoded shader uniform/constant-buffer values feeding any draw. Use this whenever you need to SEE the rendering result or the exact parameters fed to a shader -- "renders nothing / renders wrong / why does X look different from Y / which texture or uniform actually holds what". This is the tool for any "I need to see it" question on the windowed build: save a render target to PNG and Read it, and read the live uniform-buffer values at a specific draw. On macOS the in-editor MCP capture_screenshot only works headless-Vulkan and screencapture is permission-blocked, so RenderDoc is the way to see a Metal-or-windowed-Vulkan frame. Works on macOS (build_xcode_vulkan + build_mac qrenderdoc) and Windows (build_vs2026_vulkan); RenderDoc needs a windowed Vulkan build with a live display (not Metal, not OpenGL, not the headless emulated swapchain) -- so to GPU-debug a bug seen on the Metal build, reproduce it on build_xcode_vulkan.
---

# erhe RenderDoc GPU debugging (windowed Vulkan)

Capture a live frame of the running editor and inspect it at the GPU level: bound
shaders, render-target contents, per-texture min/max/NaN/zero stats, pixel
history, constant-buffer values, and saved PNGs of any render target. This is the
tool for bugs where you can see *that* the output is wrong but not *why* -- the
in-editor MCP `capture_screenshot` and CPU id-buffer readback show the final image
or a decoded value, RenderDoc shows the actual texture/buffer/pipeline state that
produced it.

The canonical reference (Windows/VS, plus the worked "black atmosphere sky"
example and every gotcha) is [`doc/renderdoc_fork.md`](../../doc/renderdoc_fork.md);
the fork's own tool reference is its
[`docs/mcp.md`](https://github.com/tksuoran/renderdoc/blob/mcp-server/docs/mcp.md).
This skill is the condensed, cross-platform run-book.

## Hard requirements (check first)

- **Windowed Vulkan build with a live display.** RenderDoc reads a real WSI
  swapchain. It does NOT work against Metal, OpenGL, or the headless emulated
  swapchain. If the display is off/asleep the windowed Vulkan editor aborts at
  startup -- there is no headless fallback that RenderDoc can capture.
- **Same fork build on both ends.** The editor must load the fork's
  `renderdoc` library (`renderdoc_library_path_override` in
  `config/editor/erhe_graphics.json`) and the `qrenderdoc` you capture from must be
  the same fork build. A target-control connection is version-sensitive.
- **Connect, don't inject.** The editor already loads the in-app RenderDoc API, so
  it shows up as a *target*. Use `list_targets` + `connect_to_target`.
  `attach_to_process` would inject a conflicting second copy.

## Machine setup (this repo, already wired)

- `.mcp.json` registers the `renderdoc` stdio proxy
  (`scripts/renderdoc_mcp_proxy.py`); the `mcp__renderdoc__*` tools are therefore
  always available and the proxy launches `qrenderdoc --mcp-server` lazily on the
  first tool call. `renderdoc_launch` pre-warms it; `renderdoc_status` confirms.
- `config/editor/erhe_graphics.json` already sets
  `renderdoc_capture_support:true`, `renderdoc_library_path_override_enable:true`,
  and the override path:
  - macOS: `/Users/timosuoranta/git/tksuoran/renderdoc/build_mac/lib/librenderdoc.dylib`
    (qrenderdoc at `.../build_mac/bin/qrenderdoc.app/Contents/MacOS/qrenderdoc`).
  - Windows: the `D:\renderdoc\x64\Development\renderdoc.dll` style path.
- Confirm the override took effect after launch:
  `grep renderdoc logs/log.txt` -> a `RenderDoc: override library '...' active` line.
- Always run `qrenderdoc` with its **visible window** (collaboration); never
  `QT_QPA_PLATFORM=offscreen`.

## Step 1 -- build the windowed Vulkan editor

- macOS: `scripts/configure_xcode_vulkan.sh` (once) then
  `cmake --build build_xcode_vulkan --target editor --config Debug`.
- Windows: `scripts\configure_vs2026_vulkan.bat` then build `editor` (via the VS
  MCP `build_project` on `build_vs2026_vulkan\src\editor\editor.vcxproj`, or the
  IDE).
- Linux: `scripts/configure_ninja_linux_vulkan.sh` then
  `cmake --build build_ninja_linux_vulkan --target editor`.

## Step 2 -- launch the editor (live display required)

- macOS/Linux: launch the built binary directly, backgrounded, and redirect
  output (the spdlog file sink writes `logs/log.txt` regardless):
  `./build_xcode_vulkan/src/editor/Debug/editor > logs/editor_run.out 2>&1 &`
- Windows: debug-launch via the VS MCP `debugger_launch` (so you also get the
  debugger if it crashes).
- macOS Vulkan ICD note: MoltenVK vs KosmicKrisp selection is via the
  `use_kosmickrisp` knob; see the macОS-Vulkan-drivers memory. If the editor hangs
  on frame 1 or aborts at device selection, that memory has the bisection
  playbook.
- Do NOT blind-sleep. Poll for readiness: either `logs/log.txt` showing the editor
  is presenting frames, or poll `mcp__renderdoc__list_targets` until the editor
  appears, or poll the in-editor MCP (`http://127.0.0.1:8080/mcp`, `list_scenes`).

## Step 3 -- set up the repro scene (in-editor MCP)

The in-editor MCP server (`http://127.0.0.1:8080/mcp`, JSON-RPC over POST) drives
the live editor so the frame you capture contains the state you want. Stand up /
mutate the scene and trigger the operation under test *before* capturing. For the
mesh-component selection scan specifically:

```
set_mesh_component_mode {"mode":"face"}
debug_region_select {"x":200,"y":150,"width":1200,"height":800,"replace":true}
```

(`config/editor/commands.json` is the startup script if you need a deterministic
scene before init finishes.)

## Step 4 -- connect and capture

```
mcp__renderdoc__renderdoc_launch                       # optional pre-warm
mcp__renderdoc__list_targets                           # -> {"targets":[{"pid":...,"target":"editor"}]}
mcp__renderdoc__connect_to_target  {"pid": <pid>}
mcp__renderdoc__get_attach_status                      # apiPresenting:true, attached:true
mcp__renderdoc__trigger_capture    {"open": true, "timeoutMs": 25000}
```

`open:true` loads the capture into the live replay session, which is the state the
inspection tools read. To capture the frame that contains an async result (e.g.
the id-buffer scan blit), trigger the editor action a frame or two before
`trigger_capture`, or capture several frames and pick the one whose action tree
contains the draw/marker you need.

## Step 5 -- inspect (read-only)

```
mcp__renderdoc__list_textures                          # resource ids by debug label
mcp__renderdoc__get_texture_stats   {"resourceId": "..."}   # min/max/mean + NaN/Inf/zero/one counts
mcp__renderdoc__save_texture        {"resourceId": "...", "path": "logs/x.png", "overwrite": true}
mcp__renderdoc__pick_pixel          {"resourceId": "...", "x": .., "y": ..}
mcp__renderdoc__search_actions      {"query": "ID"}     # find draws/markers by debug-group label
mcp__renderdoc__set_event           {"eventId": <draw>}
mcp__renderdoc__get_pipeline_state                      # bound shaders, RTs, depth target
mcp__renderdoc__get_viewports_scissors                  # is the scissor where you think?
mcp__renderdoc__get_pixel_history   {"resourceId": "<RT>", "x": .., "y": ..}
mcp__renderdoc__get_constant_buffer_contents {"stage":"fragment","slot":0}
mcp__renderdoc__detach_process                          # when done; editor keeps running
```

`get_texture_stats` and `get_pixel_history` are the highest-signal tools: stats
tell you instantly if a target is all-zero / NaN / uniform; pixel history tells
you whether a fragment was depth/stencil-rejected vs ran-and-wrote-the-wrong-value.

## Step 5a -- SEE the frame (save a render target and Read the PNG)

When the question is "what does it actually look like" (and you cannot screenshot
-- Metal MCP capture is headless-only, macOS `screencapture` is permission-blocked
when driving headless), this is how you see it:

```
mcp__renderdoc__save_texture {"resourceId":"<viewport color>","filename":"frame.png","overwrite":true}
# -> returns a path under <temp>/renderdoc_mcp/frame.png
```
then `Read` that path -- the image tool renders it inline so you literally see the
frame. The viewport color target's debug label is `Viewport window color texture`
(the resolved, non-multisampled sibling of `Viewport window multisampled color
texture`); always save the resolved one. It is `R16G16B16A16F` (HDR) but
`save_texture` applies the Texture-Viewer default tonemap so the PNG is viewable
(judge brightness from `get_texture_stats`, not the PNG, per the HDR gotcha below).
`SendUserFile` the PNG too when the user should see what you see.

## Step 5b -- read the SHADER UNIFORMS feeding a draw

To see the exact parameters a shader received (the highest-signal "why does it
look wrong" answer), navigate to the draw and read its constant buffers:

```
mcp__renderdoc__search_actions {"query":"<marker>"}     # e.g. "grid" -> the "Grid" PushMarker
mcp__renderdoc__set_event {"eventId":<the DRAW eventId, usually marker eventId+1>}
mcp__renderdoc__get_pipeline_state                       # confirm bound shaders (e.g. "grid fragment")
mcp__renderdoc__get_constant_buffers {"stage":"fragment","onlyUsed":true}   # list blocks + their slot index
mcp__renderdoc__get_constant_buffer_contents {"stage":"fragment","slot":<index>}  # decoded named values
```

`get_constant_buffers` gives you the slot index per named block; `get_constant_buffer_contents`
returns the fully decoded variable tree (scalars/vectors/matrices/structs/arrays)
exactly as the shader sees it. In erhe the per-frame `camera` block carries not just
the matrices but the grid params (`grid_color[0..3]`, `grid_label_color`, `grid_size`,
`grid_line_width`), the sky params, and `exposure` -- so reading the `camera` block at
the grid draw tells you the literal grid colors on the GPU. (`light_block` is a
different slot; `onlyUsed:true` keeps the list short and tells you which slot is which.)

## Step 5c -- compare two states (A/B capture)

To answer "why does scene/state A look different from B", capture both and diff the
same numbers -- do not eyeball. Capture A (`save_texture` + read the uniform block),
then relaunch the editor in state B, **`list_targets` + `connect_to_target` again
(the pid changes every launch)**, capture B, and read the identical texture/uniforms.
Byte-identical uniforms prove that subsystem is NOT the cause and redirect the
investigation. Worked example -- the "grid colors wrong in a loaded scene" report:
saving `Viewport window color texture` for the loaded scene vs the default scene
showed the loaded scene rendered unlit (white ground, wireframe meshes) while the
default was lit; reading the `camera` block at the grid draw in BOTH showed
`grid_color` byte-identical (all `(0,0,0,1)`). That proved the grid was fine and the
real bug was the loaded scene rendering with `material_count==0` (its companion
`.glb` failed to parse), not the grid -- a conclusion impossible to reach by staring
at the symptom.

### erhe-specific texture labels worth knowing

The Id_renderer creates several distinct ID render targets, each with its own
`Texture_create_info::debug_label` -- `list_textures` finds them by these names:

- `ID Render color` / `ID Render depth` -- the main picking ID texture
  (`Id_renderer::m_color_texture`), the one the pointer pick and the region scan
  read back. RGBA8, encodes 24-bit `id = triangle_id + id_offset`.
- `Content edge ID color` / `Content edge ID depth` -- the edge-id pass.
- `Content seed face ID color` -- the seed pass (sampled in-frame, NOT read back).

When a readback decodes ids that do not match the `id_ranges` table, the first
thing to confirm in RenderDoc is **which** of these textures actually holds the
values you read, and whether `ID Render color` in the scanned rectangle contains
the expected small ids or something else. `get_texture_stats` on `ID Render color`
(max value, zero-count) and `save_texture` of it (then read pixels at the scan
rect) settle it without any CPU-readback ambiguity.

## Step 6 -- the tight loop (edit -> rebuild -> recapture -> diff)

1. Edit source.
2. Stop the editor (macOS: `pkill -f "Debug/editor"`; Windows: VS MCP
   `debugger_stop`).
3. Rebuild the `editor` target (Step 1's build command). Confirm by the binary's
   mtime, not just a build-status "Done".
4. Relaunch (Step 2) and re-`connect_to_target` (the `pid` changes each launch).
5. `trigger_capture` and re-read the same stats -- now you can diff the numbers
   before/after.

## Gotchas (see doc/renderdoc_fork.md for the full list)

- MSAA viewport color target: pass `sample` to `get_pixel_history`/`pick_pixel`,
  or read the resolved non-`multisampled` sibling. The ID textures are
  single-sample, so this only bites the main viewport color target.
- HDR (`R16G16B16A16F`) targets save dark from `save_texture` (raw linear,
  pre-tonemap) -- read the **stats**, do not judge brightness by the PNG. The ID
  textures are RGBA8/UNORM, so their PNGs are literal.
- `save_texture` path: forward slashes, parent dir must exist, `overwrite:true`.
- The proxy leaves `qrenderdoc` running across Claude Code sessions;
  `renderdoc_shutdown` only kills an instance the proxy itself launched.
