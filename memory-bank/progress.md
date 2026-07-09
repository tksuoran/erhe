§MBEL:5.0

[TASK::uniform-scale-gizmo]{impl✓c162eb69,awaiting-user-verify}
?drag-feel::windowed-interactive-only{headless-cannot-drive-pointer-drags};s=2^(drive/gizmo_radius)→tune-in-Scale_tool::update_uniform

[TASK::#239-per-scene-settings]{parked}
?PENDING::viewport+post_processing{init-consumed¬applied→needs-per-scene-refactor}+clear_color{editor-global-never-read→decide-wire||drop}+sky/grid-override-visual-verify{needs-runtime-override-setter-MCP-tool}

[NOTES]
!¬get_type_name-in-Item-derived{clashes-erhe::Item-virtual→C2555}→factory_type_name
!¬ImGui-popups-inside-ax-NodeEditor-canvas→steppers
!¬mutate-upstream-shared-Geometry→copy-first{copy_with_transform+identity}
!ax-GetNodePosition{never-drawn→ImVec2{FLT_MAX}}→is_valid_node_position-filter
!editor-run-dirties-desktop_window_imgui_host_imgui.ini→git-checkout-after-runs
!clangd-new-file-diagnostics::false-positives-until-reconfigure{ninja-build=truth}
!MCP-execute_command-fallback::any-registered-command-callable-by-name{BUT-Hotbar.rotate-executes-without-switching-tool-headless}→gizmo-handle-visibility-via-set_gizmo_visibility-tool¬hotbar
!MCP-node-ids-differ-per-run{create_shape-returns-node_id→always-use-returned-id,¬cached-from-previous-run}
