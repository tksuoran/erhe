§MBEL:5.0

[TASK::ITEM1-scene-close-leak-root-cause]{DONE-8c3db108+8df79fa1-2026-07-16}
✓repro::save→load_scene→close→17-warns{Scene_root+brushes+nodes;control-confirmed-pre-R1}
✓proof-1::identical-run+any-async_for_nodes_with_mesh-call-post-close→clean{purge-at-entry-released-handles}→holder=s_item_tasks
✓fix-1{8c3db108}::purge_completed_item_async_tasks{items.hpp/cpp+Editor::tick-pre-watchdog};taskflow-verified{.cpm_cache:_tear_down_dependent_async-recycles-node-only-at-use_count-0;callable-lives-in-node->_handle-until-recycle}
✓import-close-leaked-too{pre-fix-confirmed;same-kickoff-capture;F1-leg-was-masked}
✓fix-2{8df79fa1}::5-selection-holders{see-activeContext-FOCUS}+watchdog-use_count-log
✓verify::7-legs-clean{see-activeContext}+clang-cl-editor-builds
✓prompt_queue::ITEM1-removed{R2=next}

[TASK::R1-asset-manager-core]{DONE-a5cdda26-2026-07-16}
✓details→activeContext-PREV+archive{asset_paths+asset_key+asset_reference+asset_manager+MCP-debug-holds;verify-headless-isolated}

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
!scene-close-verification::close→wait≈5s→grep-"scene-close"{watchdog-60-frames;warns-now-carry-"N holder(s)"=use_count}
!leak-hunt-recipe::displacement-bisect{same-repro±one-action;deselect-vs-select-other-distinguishes-follows-vs-latch}+1-holder-log→single-slot-cache
!scratch-retention-pattern::clear-at-point-of-use-KEEPS-contents-between-uses→item-shared_ptr-scratches-must-ALSO-clear-after-use{capacity-kept;precedents:m_material_candidates+m_begin_selection_change_state+m_command_target_selection}
!isolated-headless-run::scratchpad-cwd{config-COPY+res-JUNCTION+own-logs/}avoids-clobbering-user-session{CAUTION:res-junction-writes-pass-through→save-scenes-to-ABSOLUTE-scratchpad-paths¬res/}
!stale-editor.exe::locks-exe{LNK1168}+holds-port→kill-before-build+launch;CHECK-ExecutablePath+CreationDate{may-be-USER's-live-session→never-kill/drive}
!MCP-port-since-a32dbbde::Windows-bind-exclusive→2nd-editor-falls-back-8081{mcp_call.py---port-8081};get_server_info-pid-check-before-driving
!PS5.1-embedded-quotes-mangled→mcp_call.py-b64-args+git-commit--F-heredoc-in-bash
!scene-save-exports-only-mesh-referenced-materials{+ERHE_brushes-exports-ALL-library-brushes→loaded-scene-relists-them}
!MCP-load_scene-async{poll-list_scenes}
!commands-target-ACTIVE-scene
!Operation_stack::queue=main-thread-only{workers→queue_from_thread}
!save_scene-MCP-args::{scene_name+path}both-required{path-alone→"Scene not found"}
