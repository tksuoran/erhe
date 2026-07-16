§MBEL:5.0

[FOCUS]
>R1-asset-manager-core{a5cdda26,2026-07-16-late}✓{registry+reference+manager+MCP}
  new::src/editor/assets/{asset_paths+asset_key+asset_reference+asset_manager}+mcp/mcp_server_assets.cpp
  model::single-loader-axiom{acquire=only-materialization;uid-wins→unique-name-fallback→ambiguous=loud-error{decision-11};key-self-heals-name→uid}
  usership::holding-RESOLVED-Asset_reference=registered-user{copy-re-registers+move-re-points+dtor-releases;manager-tracks-by-reference-pointer}
  unload::container-granularity{Gltf_data-pins-all→per-asset-unload-meaningless-until-R5};refuse-naming-users;success→weak_ptr-exclusivity-verify→"undeclared asset user"-warn
  containers::parse_gltf-once{free-root-node-NO-holding-scene;refuse:missing|empty|open-as-scene{two-loader-hazard-until-R5}};file-scope-types=material+animation-only{brush-import-scene-coupled→R5/R7}
  builtins::Scene_builder-palette-brushes-registered-post-fill_app_context{#103;names=persistence-contract{comment-in-make_brushes}}
  watchdog::is_pinned{manager-strong-ref|declared-usership}→info-"intentionally pinned by the asset manager"¬warn
  debug-holds::manager-owned-named-Asset_references{MCP:acquire_asset/release_asset/unload_asset;=declared-users;dtor-clears-before-registry-maps}
  ctor::(App_context&,App_message_bus&,App_scenes&){context-stored-only;bus-reserved-R5};main-thread-ERHE_VERIFY-on-mutating-entries
  verify✓::builtins-103-listed+acquire-name-vs-uid→same-item_id+unload-refusals-name-holds+clean-unload-"all 7 released"+0-undeclared+scene_local-hold-close→"1 intentionally pinned"+zero-leak-warns+open-as-scene-refusal

[BUG-FOUND]
!pre-existing::load_scene(.glb)→close_scene→"scene-close leak"{Scene_root+library-brushes-survive;#17-warns;undo-stack-EMPTY;holder-unidentified;control-run-confirmed-sans-asset-manager}
  F1-legs-closed-created/imported-scenes{clean}→load-close-path-never-exercised;needs-own-root-cause-session

[PREV]
>F1-scene-close-fixes{856dedd3}✓{per-part-close_scene-subscriptions;details→archive/2026-07-16}
>U1-gltf-2.1-unique-ids{577d9f75}✓{fork-pin-e42e44f2;details→archive/2026-07-16}

[STATE]
@branch::main
prompt_queue.txt::WRITTEN{handoff-2026-07-16-late;ITEM1=load-close-leak-root-cause{fixes-before-machinery}→ITEM2=R2-slots;read-first-next-session}
untracked::res/editor/scenes/{user-saved¬touch}
uncommitted-held::doc/gltf_extensions/ERHE_asset_reference.{md,schema.json}+README-row{DRAFT-R6-wire-spec;ask-user-before-committing}
plan::asset-manager-plan.md{OUTSIDE-repo;R1-AS-LANDED-noted;next:R2-slots→Asset_reference}

[OPEN]
?load-close-leak-investigation{BUG-FOUND-above}
?user-verify-INTERACTIVE::
  R1{a5cdda26}::normal-editing-unaffected{manager-passive-until-R2+};query_asset_manager-in-live-session
  F1{856dedd3}::close-scene-while{animation-playing|brush-active|material-paint-armed|save-confirm-modal-open}+slot-pins-survive
  graph-mesh-hover/pick{c18b2608}+close-scene-graph-window{dd9022bc}+dragdrop-list{2026-07-15-pm}
?content-library-user-interactive-verify{Copy-to-Scene,texture-combo,prefab}
?content-library-deferred+selection-deferred+animation-editor{#243}+6c-fields+PhaseC+cc-perf-leftovers+#239-per-scene-settings{parked}+geogram-upstream-issue{unfiled}

[BLOCKERS]
none
