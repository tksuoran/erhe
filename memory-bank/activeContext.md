§MBEL:5.0

[FOCUS]
>F1-scene-close-fixes{856dedd3,2026-07-16-eve}✓{per-part-close_scene-subscriptions}
  design-evolution::user-steered{Editor-centralized→bus-subscribers;considered+rejected:send-and-wait-bus-API{dispatch-already-sync-fan-out;Taskflow-join=concurrency-we-must-not-add}+virtual-Item::handle_item_host_update{wrong-direction:item-cannot-reach-cachers;good-R5-input-for-host-nulling-hardening}}
  parts-subscribe::Brush_tool{active+dragdrop-brush+preview-mesh/node-mid-hover}+Material_paint_tool{m_material}+Material_preview{m_last_material+preview-library-ref-entry+preview-mesh-primitive-material}+Brdf_slice{node-material}+Physics_tool{m_last_target_mesh}+Operations{save-confirm-modal-scene-pin}+Animation_player/window{m_animation;window-set_animation({})-forwards-to-player}
  ctor-bus-arg-added::Animation_player+Animation_window+Material_preview+Brdf_slice{part-ctor-rule}
  watchdog-decoupled::on_close_scene-QUEUES-only{m_scene_roots_pending_close_watch};arming-in-update_scene_close_leak_watches-POST-PUMP{after-all-subscribers;order-independent}
  whitelist::Hotbar+Inventory_window::collect_pinned_items{brush+brush->get_material()+material}→watchdog-info-"intentionally pinned by an inventory slot"¬warn
  clipboard::DECIDED{2026-07-16,user:option-a'-pin-NO-whitelist;zero-code;copy-then-close→"scene-close leak"-warnings=KNOWN-INTENTIONAL{clipboard}until-R5;recorded-in-plan-"Open decisions"}
  verify✓::legA-plain-scene-clean+legB-RiggedFigure-animation-TARGETED+PLAYING-at-close-clean{was-the-bug}+legC-Default-Scene+slot-brush-"cube"→pinned-info+zero-warnings+"all 134 released (1 intentionally pinned)"

[PREV]
>U1-gltf-2.1-unique-ids{577d9f75,2026-07-16}✓{fork-pin-e42e44f2;Item_base::m_gltf_uid;parse-carry+scan-*_uids+export-stamp_uids-store-back;MCP-scan_gltf;verify:roundtrips+validator-0-errors+harness-62/62}
>scene-close-bug-class-session::6-commits{2026-07-15-eve;archive}

[STATE]
@branch::main
prompt_queue.txt::DELETED{U1+F1-done;clipboard-decision→user}
untracked::res/editor/scenes/{user-saved¬touch}
uncommitted-held::doc/gltf_extensions/ERHE_asset_reference.{md,schema.json}+README-row{DRAFT-wire-spec;ask-user-before-committing}
plan::asset-manager-plan.md{OUTSIDE-repo;next:R1-registry/reference/manager-core;R5-input:virtual-handle_item_host_update+host-nulling-on-close}

[OPEN]
?user-verify-INTERACTIVE::
  F1{856dedd3}::close-scene-while{animation-playing|brush-active|material-paint-armed|save-confirm-modal-open}+slot-pins-survive
  graph-mesh-hover/pick{c18b2608}+close-scene-graph-window{dd9022bc}+dragdrop-list{2026-07-15-pm}
?content-library-user-interactive-verify{Copy-to-Scene,texture-combo,prefab}
?content-library-deferred+selection-deferred+animation-editor{#243}+6c-fields+PhaseC+cc-perf-leftovers+#239-per-scene-settings{parked}+geogram-upstream-issue{unfiled}

[BLOCKERS]
none
