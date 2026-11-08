# Scene round-trip verification: the failing check

Status: in progress

`scripts/scene_roundtrip_verify.py` (`doc/editor/scene_serialization.md`,
run-book) fails one check, `VK_ERROR_DEVICE_LOST` on the draw-list path on an
NVIDIA GPU (Vulkan). Found while verifying audit item 11; recorded first in
`doc/plans/audit_2026_09_30_followups.md` "Follow-ups found by review". This
document holds its state; `doc/plans/open_items.md` "Work order" points here.
On macOS (Metal headless, glTF and USD legs) the script passes every check.

## How the runs are made

- Windows, windowed Vulkan editor from `build_ninja_win_vulkan` (Debug,
  tests on). Launch with `ERHE_AI_DRIVER=1` from the repo root, then
  `py -3 scripts/scene_roundtrip_verify.py` (port 3743; `--port` selects
  another). One fresh editor per run; the run takes about three minutes.
- macOS: the headless Metal editor (`build_xcode_metal_headless`, Debug)
  from the repo root with `ERHE_AI_DRIVER=1`, then
  `python3 scripts/scene_roundtrip_verify.py`. The macOS configure scripts
  pass `ERHE_USD_LIBRARY=none` (the Windows ones `lightusd`), which skips the
  USD leg; a tree configured with `-DERHE_USD_LIBRARY=lightusd` runs it.
- The draw-list path is the default (`editor_settings.json`
  `use_draw_lists: true`); the MCP tool `set_draw_lists_enabled`
  (`{"enabled": false}`) turns it off for the session.
- Short device-loss repro, about one minute:
  `py -3 scripts/device_loss_repro.py --editor build_ninja_win_vulkan/bin/editor.exe`
  (opens `authored.usda` over MCP under the Crash Diagnostic Layer and prints
  `scripts/gpu_fault_report.py`). Pass `--layer-settings` a file with the
  layer options of `doc/agents/debugging.md` "GPU faults".

## `VK_ERROR_DEVICE_LOST` on the draw-list path

### Facts established

- The fault is a GPU write (`VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID`,
  one page) and reproduces on every build tried, including the commit
  before the per-subresource layout tracking change (4fc17d988^, built in a
  scratch tree): the layout tracking change is not the cause. The baseline
  check of the first session (7c7c0c0bf) postdates 4fc17d988, so it never
  excluded it; this A/B did.
- It reproduces in a fresh editor with nothing but an MCP `load_scene` of
  `src/erhe/usd/test/data/authored.usda` (one point light with shadow),
  about 25 seconds after the load, on the draw-list path. The first
  session's note that a fresh editor does not fault was wrong.
- With `scripts/device_loss_repro.py` and the Crash Diagnostic Layer
  (`instrument_all_commands` + `sync_after_commands`) the fault is attributed
  to `vkCmdEndRenderPass2` of the first `Point shadow cube 0 face 0` pass of
  a shadow render node that records no draw in that pass ("shadow draw list
  1 dynamic cube layer=1 entries=1", the single entry does not pass the
  shadow filter). Cube passes of another node in the same frame, with draws,
  complete. Skipping the cube passes entirely keeps the editor alive.
- The faulting page is always inside the address range of a Default
  Viewport texture freed earlier (its post-processing `upsample level 0` or
  the overlay MSAA color, freed when the viewport resized), never rebound,
  and immediately after the live mapping of the faulting node's `Point
  shadow cube depth scratch` (which reused part of that freed range). The
  write starts inside the scratch range and crosses its end: a write "one
  layer past" the depth attachment. With a 2- or 8-layer scratch, or with the
  cube face store set to `Dont_care`, the fault moves to address 0 instead,
  so the pass performs two bad writes and the color store masks the second.
- Not the cause (each tested with the full script, fault unchanged): the
  depth clear (load `Dont_care`), the depth format (D32 instead of D24S8),
  the creation-time layout transitions of the cube array and scratch, the
  implicit layout transitions of the cube face (color attachment layout
  before and after), the pipeline map (now invalidated per handle, see
  below), stale buffers (every `vkCmdCopyBuffer` of the faulting frame is
  in range), image barriers (the frame's only image barriers name the
  `Rendertarget_mesh` texture).
- Short-repro results that did NOT transfer to the full script: in the short
  repro the editor survived 90 seconds with (a) the cube array created as
  `texture_2d_array` (no `VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT`), (b) the
  cube face color load set to `Dont_care`, (c) the color load set to `Load`,
  and (d) a transfer clear (`Command_buffer::clear_texture` on a six-layer
  view per cube) before the faces with the faces loading it. Variant (d)
  still faulted in the full script at the same address, so the short repro
  is necessary but not sufficient as the acceptance test; (a) to (c) were
  not tried in the full script.
- The fresh-editor repro and the full script fault in the same way (same
  command, same address pattern), so the glTF leg's scene churn is not
  needed, only a shadow node created after startup and a point light whose
  casters are culled from a face.

### Instrumentation added (all tracked)

- `doc/agents/debugging.md` "GPU faults": the run-book (device fault report,
  validation, Crash Diagnostic Layer with the loader's explicit enable, the
  debug-level traces, one change per run).
- Traces at `debug` level (`config/editor/logging.json`, default `info`):
  `erhe.graphics.texture` / `erhe.graphics.buffer` (create, destroy, free
  with `VkImage` / `VkBuffer`, `VkDeviceMemory`, offset, size),
  `erhe.graphics.render_pass` (render pass and framebuffer handles plus each
  attachment's image and layer), `erhe.graphics.debug` `[VA]` (GPU virtual
  address bind / unbind through `VK_EXT_device_address_binding_report`).
- `scripts/gpu_fault_report.py`, `scripts/device_loss_repro.py`.
- `erhe_graphics.json` `vulkan.vulkan_gpu_assisted_validation` (GPU-assisted
  validation; on this fault it hangs the GPU instead of reporting).
- Messages from the Crash Diagnostic Layer (`CDL`) stay at warning so the
  dump gets written.

### Fixed on the way (real defects, not the cause)

- Frame bracket query pool: one pool per ring slot (a host reset of the
  shared pool aborted every validation-layer run at startup).
- Vulkan graphics pipeline map keyed on raw handle values: pipelines are now
  retired when their render pass, shader module or pipeline layout is
  destroyed (`Device_impl::retire_pipelines_using`), before the driver can
  reuse the handle value.

### Next steps

1. Rebuild the short repro's variants against the FULL script, one per run,
   starting with (a) `texture_2d_array` for the cube array (if it passes, the
   cube-compatible flag is the trigger and the shader would sample a 2D
   array: `erhe_point_shadow.glsl` face math replaces the cube lookup) and
   (b) skipping a face pass that would record no draw (determine emptiness
   before `Scoped_render_pass`; clear the face by transfer instead). If (d)
   fails and (b) passes, the empty render pass itself is the trigger
   regardless of the clear.
2. The workaround, once one is verified by the full script, is a graphics
   config option (`Vulkan_config`, version 3) with `auto | on | off`,
   `auto` enabling it for `VkPhysicalDeviceProperties::vendorID == 0x10DE`
   (NVIDIA) on Vulkan; log the decision at startup. User decision
   2026-10-04.
3. When the fault no longer reproduces under the full script, re-check with
   the validation layers on (`vulkan_validation_layers`), then record the
   trigger in `doc/erhe/shadows.md` "Point-light cube-map shadows" (it holds
   a pointer here now) and `doc/erhe/vulkan_backend.md`.
4. Consider reporting the driver behaviour to NVIDIA with the short repro
   (RTX 50 laptop, driver 0x94004000 / 580 series, Vulkan SDK 1.4.363).

## Done when

`py -3 scripts/scene_roundtrip_verify.py` exits 0 on the draw-list path
against a fresh editor on the NVIDIA machine, with the USD leg run (not
skipped), `doc/plans/audit_2026_09_30_followups.md` "Follow-ups found by
review" no longer lists it, and the device-loss trigger is recorded in
`doc/erhe/shadows.md`. Delete this document then.
