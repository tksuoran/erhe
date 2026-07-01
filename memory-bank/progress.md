§MBEL:5.0

[TASK::#240-selectable-scene+scene-properties]
@status::✓DONE{2026-07-01}
>DONE:
1fd65017::PART1-hierarchy+MCP✓
  erhe::scene::Scene-is-Item_base→selectable-row-top-of-Hierarchy{Scene_root::m_scene-unique→shared_ptr+get_scene_item()+show_in_ui-flag}
  Item_tree::set_header_item(){flatten-before-root@indent0}+Scene-special-case-nests-content-library{via-scene->get_item_host()->Scene_root::get_content_library()->root}
  should_show::Content_library_node-bypasses-type-filter{else-hidden-under-show_in_ui-scene-tree}
  App_scenes::unregister-drops-scene-item-from-selection{¬dangling-Scene_host}
  MCP::find_items_by_ids-matches-scene-item+list_scenes-returns-scene-id{→scene-selectable-headlessly}
faacc975::PART2+3-scene-properties✓
  #237-ambient::Light_layer::ambient_light→erhe::scene::Scene::ambient_light{serialized,scene_file-v4→v5,old=0-default}+consumers{composition_pass+brush_preview+scene_builder+Ambient_light_operation→Scene*}
  config_ui.{hpp,cpp}::extract-imgui_field+add_config_section-template{shared-Settings+Properties,via-Property_editor-base}
  Properties::scene_properties(){Ambient-ColorEdit3+Scene-Overrides-group}+dispatch-if(scene)
  settings_window::Scene-Overrides-REMOVED{moved-to-Properties,was-single-scene-only}+use-shared-add_config_section
LIVE-VERIFIED✓::headless-MCP{Scene-row+Content-Library-nested-screenshot;save→v5+ambient[0.04];load-roundtrip;select-scene(id-676)→Properties-Ambient-Light+Scene-Overrides-screenshot;node-props-regression-ok}
!build✓::ninja-vulkan{windowed}+vs-vulkan-headless{both-editor.exe-clean}

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

abd6f838::GRID+FLY-CAMERA-per-scene✓
  grid::Grid::render-resolves-get_effective_grid-for-viewport-scene{appearance/labels/colors/widths/cell-sizes;placement-stays-per-grid}
  camera_controls::Fly_camera_tool::apply_camera_controls_from_scene()-in-on_hover_viewport_change{adopt-on-scene-switch,live-edits-persist}
  verify::headless-screenshot-default-scene→grid-renders-normal{no-regression}

?PENDING::remaining-consumers→see-next_prompt.txt
  WIRED-per-scene✓::physics(stepping+debug)+shadow_frustum_fit+sky(all-paths)+grid+camera_controls
  init-consumed-NOT-applied::viewport(Viewport_config_data)+post_processing{viewport-creation-time→needs-per-scene-refactor}
  clear_color::NO-consumer-anywhere{editor-global-clear_color-written-never-read;per-view-Viewport_config.clear_color-is-real-one}→decide-wire||drop
  sky/grid-visual-override-verify::needs-runtime-override-setter-MCP-tool→capture_screenshot-before/after

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
