§MBEL:5.0

[TASK::#239-per-scene-settings]
@status::⚡IN-PROGRESS{plan-approved-2026-07-01}

>DONE:
PhaseA::reference-data-captured✓{doc+auto-memory+memory-bank}
30ca9659::docs(#239)-reference+memory-bank-record
f2a3a31e::DATA-LAYER+RESOLVERS✓{builds-clean-ninja-vulkan}
  scene_settings.py{scene-unit,Optional×8:6-StructRef+clear_color-Vec4+post_processing-Bool}
  CMake::scene-unit-EXTRA_DEFINITIONS_DIRS{config+gfx+renderer+scene_renderer+xr}
  scene_file.py::+scene_settings-field-v3→v4{old-v3=all-null=defaults}
  Scene_root::+m_scene_settings+get_scene_settings()const+mutable
  scene_serialization.cpp::save/load-wired
  scene_settings_resolve.{hpp,cpp}::get_effective_<x>(Editor_settings_config&,Scene_root&)-API-ready
verify-codegen::ran-generate.py-manually✓{Optional(StructRef)-serialize/deserialize/is_default-correct}
eefd4801::UI+PHYSICS-STEPPING-GATE✓
  UI::settings_window"Scene Overrides"{single-scene,Override-checkbox/group,reuse-add_config_section}
  physics::app_scenes-before/update/after-resolve-get_effective_physics-per-scene_root
f5e45397::shadow_frustum_fit(shadow_render_node)+physics-debug_draw(debug_visualizations)+load_scene-MCP-tool✓
0c4857b7::SKY-per-scene-all-render-paths✓{app_rendering-update_sky_parameters(context)+Sky_composition_pass-gate,sky_renderer-render_atmosphere,viewport_scene_view-LUT-gate,headset_view-XR-passthrough+2-LUT-gates}
  !insight::sky-pass-refreshed-per-viewport-in-render_composer(context)→per-scene-works-w/o-new-pass
LIVE-VERIFIED✓::headless-MCP{save_scene→v4+scene_settings;load_scene-round-trips-all-null-AND-engaged-physics-override-values-preserved;v3-scene-loads→all-null-defaults}

?PENDING::remaining-consumers→see-next_prompt.txt
  WIRED-per-scene✓::physics(stepping+debug)+shadow_frustum_fit+sky(all-paths)
  init-consumed-NOT-applied::grid+viewport+camera_controls+post_processing{subsystem-holds-own-config→needs-per-frame-re-read-refactor,¬read-swap}
  clear_color::NO-consumer-anywhere{editor-global-clear_color-only-written-never-read;per-view-Viewport_config.clear_color-is-real-one}→decide-wire||drop
  sky-visual-verify::needs-runtime-override-setter-MCP-tool→capture_screenshot-before/after

[BLOCKERS]
none{data-layer-complete;consumer-wiring-is-integration-work}

[NOTES]
!enabler::codegen-Optional(StructRef)+is_default()-already-support-"unset=use-default"→¬new-primitive
!FINDING::SKY-is-GLOBAL-composition-pass{app_rendering.update_sky_parameters-no-scene-ctx;only-atmosphere-subpath-has-per-viewport-scene_root}→per-scene-sky-needs-per-viewport-pass¬read-swap
!FINDING::5/8-settings-consumed-at-INIT{editor.cpp-passes-camera_controls/post_processing/grid;grid_tool.write_config}→need-per-frame-re-read-w-scene_root
!FINDING::physics-reads-gate-simulation{app_scenes+operations+tools}¬redirect-WRITE-sites{physics_window/mcp-edit-editor-global}
!piecemeal-consumer-swap=band-aid{inconsistent-half-per-scene}→wire-per-subsystem-w-UI-together
!build-twice-after-codegen-def-change{stale-.cpp}
!windowed-vulkan-needs-live-display{ninja-build-cant-run-unattended}→headless-build-for-MCP-verify
