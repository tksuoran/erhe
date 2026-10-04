# Scene round-trip verification: the three failing checks

Status: proposed

`scripts/scene_roundtrip_verify.py` (`doc/editor/scene_serialization.md`,
run-book) fails three checks. All three reproduce on 7c7c0c0bf, the commit
before the 2026-10-04 audit work (verified by building that commit from
`git archive` in a scratch tree and running the script against it), so none
of them was introduced by 55923790f, 414c4e285 or fbbc7bd83. Found while
verifying audit item 11; recorded first in
`doc/plans/audit_2026_09_30_followups.md` "Follow-ups found by review".

## How the runs were made

- Windows, windowed Vulkan editor from `build_ninja_win_vulkan` (Debug,
  tests on; no VS 2026 on that machine, so no headless tree). Launch with
  `ERHE_AI_DRIVER=1` from the repo root, then `py -3 scripts/scene_roundtrip_verify.py`
  (it connects to port 3743; `--port` selects another). One fresh editor
  per run.
- The draw-list path is the default (`editor_settings.json`
  `use_draw_lists: true`); the MCP tool `set_draw_lists_enabled`
  (`{"enabled": false}`) turns it off for the session.
- Result files: the script prints `[PASS]` / `[FAIL]` lines and a summary;
  the editor's `logs/log.txt` holds the device fault report.

## State 2026-10-04 (second session)

- Item 1 FIXED: `section_build_scene` pauses physics (`toggle_physics`
  enabled=false) before the bodies exist and `section_prefab_scene` resumes
  it; `reload-diff: round-trip diff: nodes identical` passes.
- Item 3b FIXED in the script: a typeless `def` below a reference carrier IS
  an override since ca9b2b651 (unit test
  `Override_import.a_typed_def_below_a_carrier_is_dropped_and_a_typeless_def_is_an_override`);
  the check now asserts that rule (typed def dropped, typeless def = override
  of `arm`, survives reload). Not yet seen passing at runtime (the device loss
  below ends the run first); `doc/erhe/usd.md` wording updated to match.
- Item 3a (wrap_u/wrap_v local values) still open; no fix yet.
- Item 2 (device loss): running the script under the validation layers needed
  two fixes/additions: the frame bracket query pool is now one pool per ring
  slot (host reset of a shared pool tripped
  VUID-vkResetQueryPool-firstQuery-02741 and aborted the editor at startup),
  and `erhe_graphics.json` `vulkan.vulkan_gpu_assisted_validation` (new,
  version 2) swaps synchronization validation for GPU-assisted validation.
  Core + sync validation report nothing before the fault. The fault also hit
  once in `section_asset_references` (glTF leg), so it is state-dependent,
  not tied to `authored.usda`. NEXT: set both `vulkan_validation_layers` and
  `vulkan_gpu_assisted_validation` to true (temporarily), launch, run the
  script, read `logs/log.txt` / `logs/device_error.txt` for the GPU-AV
  message; suspects are the draw-list compute writes (indirect commands /
  counters / per-light shadow lists) after scene close or light-set growth.
  The GPU-AV option has been built but not yet exercised at runtime.

## 1. `reload-diff: round-trip diff: nodes identical` (2 mismatches)

`section_reload_and_diff` (glTF leg): `P6 Box` and `P6 Sphere` differ in
`position[1]` between the live snapshot and the reloaded scene (e.g.
-14.27 vs -15.53). Both are `motion_mode: dynamic` bodies the fixture
creates with no ground under them, so they fall while the scene is live;
the snapshot of the original scene and the snapshot of the reloaded one are
taken at different times of that fall. The USD leg pauses physics before
its snapshot (`toggle_physics {"enabled": false}`, script line about 2200,
comment "a simulation step moves what it is about to compare"); the glTF
leg does not. Open question: when did the glTF leg last pass with dynamic
bodies - does the fixture expect a ground, or did the check always depend
on timing? Candidate fix: pause physics in `section_build_scene` after the
bodies exist (and resume for the physics checks that need stepping), the
way the USD leg does.

## 2. `VK_ERROR_DEVICE_LOST` on the USD leg, draw-list path only

After the glTF leg (many scenes created, saved, reloaded, closed), the USD
leg's first step opens `src/erhe/usd/test/data/authored.usda` (5 nodes, one
point light with shadow); the next frame's `vkQueueSubmit2` fails with
`VK_ERROR_DEVICE_LOST` and the `VK_EXT_device_fault` report says
`VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID` (one address, page-sized
range). The editor exits; the remaining sections fail with connection
errors. Facts established:

- Opening `authored.usda` in a fresh editor does not fault; the fault needs
  the preceding glTF-leg session state.
- With draw lists off for the session the whole script completes (the
  USD leg then shows item 3 instead), so the fault is on the draw-list
  path (`Draw_list_scene` shadow or color passes, or the rebuild
  `set_exclude_unlit_from_shadows` queues right before the fault: the log
  shows "Draw_list_scene: exclude unlit from shadows = true; queueing draw
  list rebuild", then `Light_set::resolve` with 1 point light with shadow,
  a `Shader_variant_cache miss`, then the fault).
- Reproduces identically on 7c7c0c0bf with draw lists on.

Next steps: run the script with Vulkan validation layers on
(`config/editor/erhe_graphics.json` `vulkan_validation_layers`, see
`doc/agents/debugging.md`), ideally GPU-assisted validation, to turn the
write fault into a named binding; then narrow the preceding state (which
glTF-leg section is needed before the USD open) by running the sections in
isolation. A GPU write at an invalid address on the draw-list path points at
a buffer or image the pass still references after its owner released it
(a scene close releasing a ring buffer / render target the cached draw-list
pipelines or the shadow cube pass still bind), rather than a CPU-side count
mismatch (those are `ERHE_VERIFY`ed).

## 3. Two USD checks (seen with draw lists off)

- `usd-roundtrip: textured round-trip diff: local_property_names identical`:
  the reloaded `Gridded` material carries `base_color_texture_wrap_u` /
  `base_color_texture_wrap_v` as local values the original did not have
  (16 vs 18 names) - the USD save or load authors the wrap mode where the
  original left it default.
- `usd-roundtrip: references_override: the def below a carrier authored nothing`:
  the check prints `Widget/arm` with `local: ['visible']`, i.e. the def
  below the reference carrier has a local `visible` value after the round
  trip.

Both are in `section_usd_round_trip`; `src/erhe/usd/test/data/*.usda` and
`erhe_usd_tests` (405 pass) are the places to look for what the exporter
and importer author.

## Done when

`py -3 scripts/scene_roundtrip_verify.py` exits 0 on the draw-list path
against a fresh editor, with the three checks passing for the right reason
(not skipped), and `doc/plans/audit_2026_09_30_followups.md` "Follow-ups
found by review" no longer lists them. Delete this document then.
