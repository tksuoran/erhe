§MBEL:5.0

[TASK::R2-slots-onto-Asset_reference]{DONE-95ec5eec-2026-07-16}
✓asset_reference_data.py{v1:scope+asset_type+path+uid+name}+asset_reference_config.{hpp,cpp}{to_asset_key/to_asset_reference_data}+CMake{DEFINITIONS+_config_sources+assets-sources}
✓Inventory_slot-v4{brush_asset+material_asset;legacy-names-read-migrated¬written}
✓Slot_entry{Asset_reference-brush/material+get_brush/get_material{get_as};hotbar.hpp-includes-asset_reference.hpp}
✓Asset_reference::adopt{exact-object;drag-drop;¬name-re-resolution}
✓Inventory_window{ctor-sets-keys+labels;resolve_slot_references-per-frame;render_slot-resolved-presence;swap-real-entries;label-restamp-on-change;write_config-writes-keys}
✓Hotbar{set_slots-relabels-"hotbar slot N";handle_slot_update/slot_button-via-get_brush/get_material}
✓collect_pinned_items-slimmed{transitive-brush-material-only;both-Hotbar+Inventory}
✓MCP-set_window_visibility{title+visible;error-lists-titles}
✓verify::seeded-config-legs{see-activeContext-FOCUS}

[TASK::ITEM1-scene-close-leak-root-cause]{DONE-8c3db108+8df79fa1-2026-07-16}
✓details→activeContext-PREV+history{s_item_tasks-AsyncTask-retention+5-selection-holders+watchdog-holder-count}

[TASK::R1-asset-manager-core]{DONE-a5cdda26-2026-07-16}
✓details→archive{asset_paths+asset_key+asset_reference+asset_manager+MCP-debug-holds}

[NOTES]
!R2-headless-recipe::seed-editor_settings.json-inventory-grid_slots{legacy-"_version":2-names|v4-asset-keys}→launch→set_window_visibility-"Inventory"{REQUIRED:default-layout-leaves-it-closed+window-state-autosave-clobbers-desktop_windows.json-seeding}→query_asset_manager-users
!R2-resolution-order::scene_local-key-binds-container-copy-when-container-already-loaded{registry-before-scenes};builtin-shadows-scene-copy-for-brush-names{adopt-preserves-drop-identity}
!R2-transitive-pin::slot-brush's-own-material-has-NO-usership→collect_pinned_items-whitelist-remains-for-exactly-that
!R1-unload-granularity::container{revisit-R5};file-scope-types=material+animation-ONLY{brush→R5/R7}
!R1-debug-holds::MCP{acquire_asset/release_asset/unload_asset;refusal-exit-1=isError-expected}
!R1-container-load-needs-current_command_buffer{MCP-dispatch-in-tick-OK;imgui-draw-in-tick-OK}
!scene-close-verification::close→wait≈5s→grep-"scene-close"{watchdog-60-frames;warns-carry-"N holder(s)"=use_count}
!leak-hunt-recipe::displacement-bisect{same-repro±one-action;deselect-vs-select-other=follows-vs-latch}+1-holder-log→single-slot-cache
!scratch-retention-pattern::clear-at-point-of-use-KEEPS-contents-between-uses→item-shared_ptr-scratches-must-ALSO-clear-after-use
!isolated-headless-run::scratchpad-cwd{config-COPY+res-JUNCTION+own-logs/}{res-junction-writes-pass-through→ABSOLUTE-scratchpad-save-paths}
!stale-editor.exe::kill-before-build+launch;get_server_info-pid-check;2nd-editor→8081
!PS5.1-quotes-mangled→mcp_call.py-b64-args+git-commit--F-heredoc-in-bash
!save_scene-MCP-args::{scene_name+path}both-required
!MCP-load_scene-async{poll-list_scenes};commands-target-ACTIVE-scene
!Operation_stack::queue=main-thread-only{workers→queue_from_thread}
!editor-settings-AUTOSAVE::Editor_settings_store-per-frame-change-detection{collect-callbacks;no-clean-exit-needed-for-config-writeback-tests}
