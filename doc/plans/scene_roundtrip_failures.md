# Scene round-trip verification: the three failing checks

Status: in progress

`scripts/scene_roundtrip_verify.py` (`doc/editor/scene_serialization.md`,
run-book) failed three checks. Found while verifying audit item 11; recorded
first in `doc/plans/audit_2026_09_30_followups.md` "Follow-ups found by
review". This document holds the state of each; the queue
(`prompt_queue.txt`) points here.

## How the runs are made

- Windows, windowed Vulkan editor from `build_ninja_win_vulkan` (Debug,
  tests on). Launch with `ERHE_AI_DRIVER=1` from the repo root, then
  `py -3 scripts/scene_roundtrip_verify.py` (port 3743; `--port` selects
  another). One fresh editor per run; the run takes about three minutes.
- The draw-list path is the default (`editor_settings.json`
  `use_draw_lists: true`); the MCP tool `set_draw_lists_enabled`
  (`{"enabled": false}`) turns it off for the session.
- Short device-loss repro (item 2), about one minute:
  `py -3 scripts/device_loss_repro.py --editor build_ninja_win_vulkan/bin/editor.exe`
  (opens `authored.usda` over MCP under the Crash Diagnostic Layer and prints
  `scripts/gpu_fault_report.py`). Pass `--layer-settings` a file with the
  layer options of `doc/agents/debugging.md` "GPU faults".

## 1. `reload-diff: round-trip diff: nodes identical` - FIXED

The fixture's dynamic bodies fell between the snapshot and the reload.
`section_build_scene` now pauses physics (`toggle_physics` enabled=false)
before the bodies exist and `section_prefab_scene` resumes it; the check
passes (verified in several full runs).

## 2. `VK_ERROR_DEVICE_LOST` on the draw-list path - OPEN

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

## 3. Two USD checks

### 3a. `textured round-trip diff: local_property_names identical` - OPEN

- The script's `diff_json` printed its sides inverted (`only in loaded` /
  `only in original` and `only-original=` / `only-loaded=` were swapped).
  Fixed: the parameters are now `loaded, original` and every message names
  the side correctly. The first session's reading of this failure was
  therefore backwards: the ORIGINAL `Gridded` material has the two wrap
  locals (`base_color_texture_wrap_u` / `_v`, 18 names) and the RELOADED one
  lacks them (16 names).
- Why: the importer sets the wrap mode as a local value whenever USD's
  effective wrap (`useMetadata` default = `clamp_to_edge`) differs from
  erhe's default `repeat` (`doc/erhe/usd.md`, deliberate), and the writer
  authors `wrapS` / `wrapT` on every bound texture with a file, so the wrap
  path round-trips. The reloaded material lacks the locals because the saved
  `logs/usd_roundtrip_textured.usda` holds NO `UsdUVTexture` shader at all:
  the save dropped the texture (`Gridded` was written with a plain
  `inputs:diffuseColor`). Every saved file of that run lacked textures,
  including `usd_roundtrip_open_pbr.usda` whose source binds
  `images/auto_rgba.png`.
- Two candidate mechanisms, one log line decides (run the USD leg, or open
  and save `textured.usda` over MCP, and grep `logs/log.txt`): (H1) the
  image never loaded in the original (`[editor.parsers] USD image '...' not
  found` / `could not be decoded` / `is empty`, `src/editor/parsers/usd.cpp`
  `load_usd_image`); (H2) it loaded but the save could not resolve its file
  (`save_scene_usd '...': material 'Gridded' slot 'base_color' has no source
  image file - the slot is not written`, `usd.cpp` `save_scene_usd`, and the
  writer's `has no source file` warning in `usd_export.cpp`). H1 is
  consistent with the `textures` key not having failed. Fix at the named
  function; then add a regression check (assert `UsdUVTexture` in the first
  save when the source has one, in `usd_round_trip_leg` or an `Mcp_test`).
- The wrap import / export logic and `test_usd_texture_inputs.cpp` need no
  change.

### 3b. `references_override: the def below a carrier authored nothing` - FIXED (script)

A typeless `def` below a reference carrier IS an override since ca9b2b651
(unit test `Override_import.a_typed_def_below_a_carrier_is_dropped_and_a_typeless_def_is_an_override`;
`doc/erhe/usd_compatibility_design.md` X2 and "Out of scope"). The script
asserted the older rule. It now checks that the typed `def` added no prim,
that the typeless `def` is the override of `arm` (`visible` local, active),
that nothing else below `DefCarrier` authored a value, and that this
survives save and reload. `doc/erhe/usd.md` says the same. Not yet seen
passing at runtime: the USD leg has not run past item 2.

## Done when

`py -3 scripts/scene_roundtrip_verify.py` exits 0 on the draw-list path
against a fresh editor, with the checks passing for the right reason (not
skipped), `doc/plans/audit_2026_09_30_followups.md` "Follow-ups found by
review" no longer lists them, and the item-2 trigger is recorded in
`doc/erhe/shadows.md`. Delete this document then.
