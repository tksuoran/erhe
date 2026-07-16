§MBEL:5.0

[TASK::R1-asset-manager-core]{DONE-a5cdda26-2026-07-16}
✓asset_paths{normalize_asset_path=weakly_canonical-fallback-raw+asset_path_to_string=generic_string}
✓asset_key{scope+type+path+uid+name;¬index-field{decision-11};traits-table{Item_type-bit+Content_library-folder-member-ptr;mesh=nullptr→scene-node-mesh-attachments}}
✓asset_reference{lazy;file-scope-failed-latch{scene_local/builtin-retryable};copy-re-registers+move-re-points+dtor-releases;user_label;get_as<T>}
✓asset_manager{acquire{builtin|file|scene_local}+register_builtin+get_or_load_container+request_unload+make_key+find_loaded+inspect_*+is_pinned+debug-holds}
✓editor-wiring{App_context::asset_manager;ctor-after-App_scenes;member-decl-after-Prefab_library→destroyed-first;builtin-registration-post-fill_app_context;watchdog-is_pinned-info-branch;log_asset}
✓MCP{query_asset_manager+acquire_asset+release_asset+unload_asset;mcp_server_assets.cpp}
✓verify::headless-isolated{builtins#103+same-object-name-vs-uid{item_id-equal}+refusal-names-holds+clean-unload{all-7-released,0-undeclared}+watchdog-"1 intentionally pinned"+open-as-scene-refusal}
!found::pre-existing-load_scene→close-leak{Scene_root+brushes;control-confirmed;NOT-R1;holder-unknown{undo-stack-empty}}

[TASK::F1-scene-close-fixes]{DONE-856dedd3-2026-07-16}
✓details→activeContext-PREV+archive

[TASK::U1-gltf-2.1-unique-ids]{DONE-577d9f75-2026-07-16}
✓details→activeContext-PREV+archive

[NOTES]
!R1-unload-granularity::container{parsed-Gltf_data-pins-every-contained-object→per-asset-unload-cannot-release;revisit-R5}
!R1-file-scope-types::material+animation-ONLY{Gltf_data-vectors-direct;brush=ERHE_brushes-import-scene-coupled→refuse-with-clear-error-until-R5/R7}
!R1-debug-holds::MCP-test-hooks{acquire_asset{hold_name+scope/type/path/uid/name}→item_id-for-same-object-checks;release_asset{hold_name};unload_asset→users-in-refusal-payload;exit-code-1-on-refusal=isError-expected}
!R1-container-load-needs-current_command_buffer{parse_gltf-texture-upload→MCP-dispatch-in-tick-OK;VERIFYs-like-Prefab_library}
!normalize_asset_path-resolves-8.3-short-paths{TIMO~1.SUO-vs-long-form-match-via-weakly_canonical}
!scene-close-verification::close→wait≈5s→grep-"scene-close"{watchdog-60-frames}
!U1-uid-model+save_scene-timeout+validator-download+headless-gotchas→see-git-history{92270d67,569b0a8c}+archive
!isolated-headless-run::scratchpad-cwd{config-COPY+res-JUNCTION+own-logs/}avoids-clobbering-user-session{CAUTION:res-junction-writes-pass-through→save-scenes-to-ABSOLUTE-scratchpad-paths¬res/}
!stale-editor.exe::locks-exe{LNK1168}+holds-port→kill-before-build+launch;CHECK-ExecutablePath+CreationDate{may-be-USER's-live-session→never-kill/drive}
!MCP-port-since-a32dbbde::Windows-bind-exclusive→2nd-editor-falls-back-8081{mcp_call.py---port-8081};get_server_info-pid-check-before-driving
!PS5.1-embedded-quotes-mangled→mcp_call.py-b64-args+git-commit--F-heredoc-in-bash
!scene-save-exports-only-mesh-referenced-materials{+ERHE_brushes-exports-ALL-library-brushes→loaded-scene-relists-them}
!MCP-load_scene-async{poll-list_scenes}
!commands-target-ACTIVE-scene
!Operation_stack::queue=main-thread-only{workers→queue_from_thread}
