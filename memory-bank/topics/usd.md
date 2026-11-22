# USD

Working notes for USD work. What USD support does is in `doc/erhe/usd.md`,
`doc/erhe/usd_compatibility_design.md` and `doc/erhe/usd_compatibility.md`;
the remaining work and the pending user checks are in
`doc/plans/usd_compatibility.md`; the survey loop is in
`doc/agents/usd_survey_gap_loop.md`.

## Traps

- MCP `set_animation_target` applies the clip on the next update
  (`Animation_player::set_animation` sets `m_apply_requested`), so the pose
  changes before playback starts. A verify script that snapshots values
  after selecting an animation stops playback first
  (`scripts/scene_roundtrip_verify.py` does).
- `erhe_scene_renderer_gpu_tests` exits with code 3 after setting up its
  environment when the display is asleep; that is not a code failure. It
  passed 16/16 with the display on (2026-09-14).

## Measurement baselines

- `full_assets/Teapot/DrawModes.usd` imported in the editor (2026-09-14,
  Tracy capture): the frame tick after the import was 128-150 ms before the
  material change-serial fixes and 6.1 ms median after
  (`update_material_sets` 0.47 ms, `check_material_changes` 0.04 ms); the
  import tick went from 30.5 s to 12.5 s.
