§MBEL:5.0

[TASK::gltf-scene-roundtrip]{active}
✓phases0-4{0c3bd202+72ac5da9+f70143b5+3a4989b6;details→activeContext+history}
?phase5::migration+removal{handoff-written@prompt_queue.txt,run-with-fresh-context}
?phase6::verification{last}

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
