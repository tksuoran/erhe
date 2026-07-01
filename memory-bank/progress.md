§MBEL:5.0

[TASK::#239-per-scene-settings]
@status::⚡IN-PROGRESS{plan-approved-2026-07-01}

>DONE:
PhaseA::reference-data-captured✓
  doc/editor_settings_codegen_scene_reference.md{settings-map+codegen-how-to+scene-serialization-map}
  auto-memory::reference_settings_codegen_scene+project_239_per_scene_settings{+MEMORY.md-index}
  memory-bank::activeContext+progress-updated
plan::approved{Optional-per-config-group,scene_file-v3→v4,Scene_root-storage}

?PENDING:
PhaseB1::mechanism-end-to-end
  1-scene_settings.py{scene-unit,Optional(StructRef)×6+Optional(Vec4)clear_color+Optional(Bool)post_processing}
  2-CMake::EXTRA_DEFINITIONS_DIRS"${_config_defs}:config/generated/"+DEFINITIONS+_codegen_sources
  3-scene_file.py::+scene_settings-field{StructRef,added_in=4}+version→4
  4-Scene_root::+m_scene_settings+get_scene_settings()
  5-scene_serialization.cpp::save/load-wire
  6-resolvers::get_effective_<x>(Editor_settings_config&,Scene_root&)+rewire-consumers
  7-UI::reflection-driven-override-checkbox/group
PhaseB2::polish

[BLOCKERS]
none

[NOTES]
!enabler::codegen-Optional(StructRef)+is_default()-already-support-"unset=use-default"→¬new-primitive
!build-twice-after-codegen-def-change{stale-.cpp}
!Scene_root-not-erhe::scene::Scene{latter-graphics-agnostic}
!verify-via-headless-MCP{save/load-roundtrip+v3-back-compat+screenshot-effective-value}
!consumer-rewire-grep::editor_settings->{sky,grid,physics,shadow_frustum_fit,viewport,camera_controls,post_processing,clear_color}
