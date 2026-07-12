§MBEL:5.0

[TASK::gltf-scene-roundtrip]{active}
✓phases0-5{0c3bd202+72ac5da9+f70143b5+3a4989b6;phase5:f5a58c5b+1c643354+77ca784b;details→activeContext+history}
✓race-fix::Scene::update_node_transforms-locks-item_host_mutex{async-raytrace-kickoff-vs-main-thread-AV}
✓geogram-wedge-fix::make_convex_hull-PDEL→BDEL+ASSERT_THROW-explicit{doc/geogram.md=upstream-issue-draft,¬yet-filed}
✓smoke-suites-green-post-removal{geometry_nodes:130/130+texture_graph:268/268;fresh-session-per-suite}
✓doc::scene_serialization.md{d4188760,process+parts+limitations-reference}
?phase6::verification{last,handoff-written@prompt_queue.txt,run-with-fresh-context}

[TASK::#239-per-scene-settings]{parked}
✓runtime-setter-MCP-tool{set_scene_settings+get_scene_settings@phase4,3a4989b6}→sky/grid-override-visual-verify-unblocked
?PENDING::viewport+post_processing{init-consumed¬applied→needs-per-scene-refactor}+clear_color{editor-global-never-read→decide-wire||drop}+sky/grid-override-visual-verify

[NOTES]
!¬get_type_name-in-Item-derived{clashes-erhe::Item-virtual→C2555}→factory_type_name
!¬ImGui-popups-inside-ax-NodeEditor-canvas→steppers
!¬mutate-upstream-shared-Geometry→copy-first{copy_with_transform+identity}
!ax-GetNodePosition{never-drawn→ImVec2{FLT_MAX}}→is_valid_node_position-filter
!editor-run-dirties-desktop_window_imgui_host_imgui.ini→git-checkout-after-runs
!clangd-new-file-diagnostics::false-positives-until-reconfigure{ninja-build=truth}
!MCP-execute_command-fallback::any-registered-command-callable-by-name{BUT-Hotbar.rotate-executes-without-switching-tool-headless}→gizmo-handle-visibility-via-set_gizmo_visibility-tool¬hotbar
!MCP-node-ids-differ-per-run{create_shape-returns-node_id→always-use-returned-id,¬cached-from-previous-run}
!MCP-load_scene-async-since-phase4{queued→poll-list_scenes}
!stale-editor.exe::locks-exe{headless-relink-LNK1168}+holds-port-8080→kill-before-build+launch
!incremental-build-stamp-stale{get_server_info-"built"-from-unchanged-TU}→check-exe-LastWriteTime
!PS5.1-embedded-double-quotes-in-native-args-mangled{git-commit--m-heredoc-splits-at-quote}→write-msg-to-file+git-commit--F
!smoke-suites::one-FRESH-editor-session-each{texture-material_output-resolves-material-via-get_single_scene_root→null-when->1-scene-open→section-fails}
!scene-save-exports-only-mesh-referenced-materials{graph-binding-on-unused-material=dropped+warn}→tests/users-bind-mesh-used-materials
