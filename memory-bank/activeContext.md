§MBEL:5.0

[FOCUS]
>ITEM1-scene-close-leak-root-cause{8c3db108+8df79fa1,2026-07-16-late}✓{was-R1's-BUG-FOUND}
  root-cause-1{8c3db108}::s_item_tasks{items.cpp}retained-COMPLETED-tf::AsyncTask-handles→taskflow-frees-task-lambda+captures-only-at-last-handle-release¬at-completion→raytrace-kickoff-lambda{scene_root+mesh-node-items}pinned-whole-loaded-scene{import-path-leaked-same-way;F1-import-leg-was-masked-by-later-purges}
  fix-1::purge_completed_item_async_tasks{per-frame,Editor::tick-pre-watchdog}¬only-opportunistic-on-next-submission
  root-cause-2{8df79fa1}::5-selection-driven-holders{exposed-once-big-holder-gone}:
    Selection::m_begin_selection_change_state{close-time-clear_selection-snapshot-kept-until-next-change}→clear-post-diff+m_command_target_selection-too
    range-terminators::reset_terminators_for_host-now-unconditional{strong-refs-independent-of-selection}
    Transform_tool::Edit_state::m_first_node{rebuilt-per-draw-but-stale-when-selection-empties}→update_target_nodes-drops-on-empty-entries
    Operations::m_make_mesh_config.material{imgui-stores-get_last_selected<Material>-per-frame;weak-map-lockable-BECAUSE-this-member-pins=self-sustaining}→on_close_scene-drops
    Properties::m_material_candidates{picker-scratch-retained-past-draw}→clear-after-use+NEW-close_scene-subscription{m_target/m_target_items+m_inspected_material;ctor-takes-App_message_bus&}
  watchdog-enhanced::survivor-warns-log-use_count{"N holder(s)"}→single-holder-isolation-was-key-diagnostic
  verify✓::7-headless-legs-all-"all N released"{load-close+select-node-close+select-node-smooth-close+select-material-deselect-close+select-material+pinned-Properties-close+create-close+create-import-close}

[DEBUG-METHOD-THAT-WORKED]
displacement-bisect::same-repro+one-action-diff{close-then-trigger-purge|select-other-material}→leak-vanishes=holder-named;holder-count-log-narrows-to-single-holder;deselect-vs-displace-distinguishes-follows-selection-vs-latch

[PREV]
>R1-asset-manager-core{a5cdda26}✓{registry+reference+manager+MCP;details→archive/2026-07-16}
>F1-scene-close-fixes{856dedd3}✓+U1-gltf-2.1-unique-ids{577d9f75}✓

[STATE]
@branch::main
prompt_queue.txt::UPDATED{ITEM1-removed;next=R2-slots-onto-Asset_reference;read-first-next-session}
untracked::res/editor/scenes/{user-saved¬touch}
uncommitted-held::doc/gltf_extensions/ERHE_asset_reference.{md,schema.json}+README-row{DRAFT-R6-wire-spec;ask-user-before-committing}
plan::asset-manager-plan.md{OUTSIDE-repo;next:R2-slots→Asset_reference}

[OPEN]
?R2-slots{prompt_queue-ITEM1-now}
?user-verify-INTERACTIVE::
  leak-fixes{8c3db108+8df79fa1}::normal-editing-unaffected+interactive-close-legs-leak-free{grep-"scene-close"}
  R1{a5cdda26}::query_asset_manager-live-session
  F1{856dedd3}::close-scene-while{animation|brush|material-paint|save-modal}
  graph-mesh-hover/pick{c18b2608}+close-scene-graph-window{dd9022bc}+dragdrop-list{2026-07-15-pm}
?content-library-user-interactive-verify+selection-deferred+animation-editor{#243}+6c-fields+PhaseC+cc-perf-leftovers+#239-per-scene-settings{parked}+geogram-upstream-issue{unfiled}

[BLOCKERS]
none
