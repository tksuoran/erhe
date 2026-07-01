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

?PENDING::consumer-wiring+UI+live-verify→see-next_prompt.txt
  UI::settings_window"Scene Overrides"{current-scene_root+Override-checkbox/group+reuse-add_config_section-reflection}
  consumers::NOT-mechanical-read-swap{per-subsystem-wrinkles}
  live-verify::rebuild-headless→MCP-save/load-v4+screenshot-effective-sky

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
