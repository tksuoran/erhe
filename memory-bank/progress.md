§MBEL:5.0

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
!¬persisted::Brush_placement-attachments{brush-LIBRARY-persists-via-ERHE_brushes}+static-body-mass{KHR-no-motion-object-for-statics}→doc/scene_serialization.md-limitations
!import_root-wrappers-transparent-on-export{children-written-in-their-place}→roundtrip-diffs-need-parent_id+import_root-from-get_scene_nodes
!settings-less-joint-reload-materializes-Physics_joint_settings-item{"Physics joint 0"}→joint_settings-name-not-comparable-across-roundtrip
!animation-authoring-headless::animation_create_key-needs-EXISTING-target{set_animation_target;new-animation-only-via-ImGui-+Key-button}→import-animated-gltf-then-key
!MCP-create_shape-with-motion_mode-already-creates-rigid-body→edit_physics_body-for-field-tweaks{create_physics_body-errors-"already has"}
!Blender-stock-gltf-importer-rejects-2.1{externalAssets-scene}→foreign-tool-checks-use-prefab-free-2.0-save
!MCP-save_prefab-REMOVED{b1eecef0}→save_scene{path-optional:omitted→source-path||res/editor/scenes/<name>.glb;written-path=loaded-prefab→auto-reload-instances;explicit-path¬re-associates-scene}
!scene-with-source_path-saves-back-SILENTLY{¬overwrite-modal;open/load-sets-source_path;first-default-dir-save-associates}
!MCP-select_items-SCOPED-since-per-scene-selection{clears+selects-within-target-scene-only;ids=[]-clears-that-scene-only;activates-target-scene;materials=non-hosted→persist-across-scene-scoped-clears}
!commands-target-ACTIVE-scene{transform_selection/mesh-ops/Selection.delete→active-scene-selection-only;switch-via-set_active_scene||select_items;get_selection-reports-active_scene+per-item-scene_name}
!Operation_stack::queue=main-thread-only{async-worker-completions→queue_from_thread;drained-in-update}
!get_hosted_selection-returns-host-owned-scratch{cleared+refilled-per-call→copy-before-next-call-with-same-host}
