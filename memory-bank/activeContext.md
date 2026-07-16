§MBEL:5.0

[FOCUS]
>R2-slots-onto-Asset_reference{95ec5eec,2026-07-16-late}✓
  Slot_entry::brush/material→Asset_reference{resolved-slot=declared-user;labels:"inventory grid slot N"/"inventory hotbar slot N"/hotbar-copies-"hotbar slot N";re-stamped-after-any-slot-change}
  codegen::Asset_reference_data-v1{scope+asset_type+path+uid+name;¬index-per-decision-11}+assets/asset_reference_config.{hpp,cpp}{to_asset_key/to_asset_reference_data}
  Inventory_slot-v4::brush_asset/material_asset{legacy-names-read+migrated-to-scene_local¬written;uid-self-heals-into-written-keys}
  Pending_slot_item-retry-queue→per-frame-resolve_slot_references{imgui;scene_local-misses-don't-latch;unresolved-keys-survive-write_config-by-construction}
  NEW-Asset_reference::adopt{drag-drop-adopts-EXACT-object¬name-re-resolution{builtin-shadows-scene-copy}};swaps-exchange-real-Slot_entries
  collect_pinned_items-SLIMMED{transitive-only:slot-brush-pins-own-material;slot-items-covered-by-is_pinned}
  NEW-MCP::set_window_visibility{title,visible;windows-do-per-frame-work-only-visible;default-layout-leaves-Inventory-closed;desktop_windows.json-seeding-clobbered-by-window-state-autosave}
  verify✓::v2-legacy-migrate+resolve|v4-file-key-loads-container-on-demand|unload-refusal-names-"inventory grid slot 3"|restart-restores-from-autosaved-v4{uid-self-heal-in-written-keys}|close-scene-with-scene-hosted-slot-material→info-"intentionally pinned by the asset manager"+zero-warns{134-released,1-pinned}|MSVC+clang-cl-clean
  observed::scene_local-key-can-bind-loaded-CONTAINER-copy{registry-before-scenes,plan-risk-6;Chromium-leg}=deterministic-accepted

[PREV]
>ITEM1-scene-close-leak-root-cause{8c3db108+8df79fa1}✓{s_item_tasks-completed-AsyncTask-handles+5-selection-holders;watchdog-"N holder(s)";7-legs-clean;details→history-2026-07-16-late}
>R1-asset-manager-core{a5cdda26}✓+F1{856dedd3}✓+U1{577d9f75}✓

[STATE]
@branch::main
prompt_queue.txt::WRITTEN{handoff-for-fresh-context;ITEM1=interactive-verification-redo{windowed-launch+checklist}→ITEM2=R3-tool-state;read-first-next-session}
uncommitted::config/editor/{desktop_windows.json+editor_settings.json}{user's-short-interactive-run:inventory-flag-true+slots-migrated-v2→v4+grid-slot-1-cube-from-real-drag-drop{adopt-proven-live};expected-R2-behavior;ask-user-commit-or-leave}
untracked::res/editor/scenes/{user-saved¬touch}
uncommitted-held::doc/gltf_extensions/ERHE_asset_reference.{md,schema.json}+README-row{DRAFT-R6-wire-spec;ask-user-before-committing}
plan::asset-manager-plan.md{OUTSIDE-repo;R2-AS-LANDED-noted;next:R3-tool-state{brush_tool-m_active_brush+material_paint_tool-m_material→Asset_reference;supersedes-F1-subscriptions-there}}

[OPEN]
?R3-tool-state{next-phase;plan-section-is-spec}
?user-verify-INTERACTIVE::
  R2{95ec5eec}::drag-drop-slots{brush/material/swap/clear}+hotbar-brush-activation+brush-with-material-fork-transitive-pin
  leak-fixes{8c3db108+8df79fa1}::normal-editing-unaffected+interactive-close-legs-leak-free{grep-"scene-close"}
  R1{a5cdda26}::query_asset_manager-live-session
  F1{856dedd3}::close-scene-while{animation|brush|material-paint|save-modal}
  graph-mesh-hover/pick{c18b2608}+close-scene-graph-window{dd9022bc}+dragdrop-list{2026-07-15-pm}
?content-library-user-interactive-verify+selection-deferred+animation-editor{#243}+6c-fields+PhaseC+cc-perf-leftovers+#239-per-scene-settings{parked}+geogram-upstream-issue{unfiled}

[BLOCKERS]
none
