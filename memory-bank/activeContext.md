§MBEL:5.0

[FOCUS]
>graph-editor-dragdrop+inventory-session::6-commits{2026-07-15-pm,follow-up-to-UX-sprint}
  resize-shrink-fix{6c4dc32e}::bottom-edge-shrink-stuck-after-1-grid-step
    root-cause::SizeAction-m_MinimumSize-latch-EXACT-float-equality{requested-vs-adopted}✗ImCeil-quantized-adoption{NodeBuilder::End-ceils-size/zoom→adopted=requested+1-when-fp-lands-above-integer;bottom-edge-deterministic{node-top=measurement-base-fixed-during-gesture}}
    fix::latch-only-when-adopted>requested+1.5{quantum+fp-margin}+trace-log-on-latch{erhe.imgui.node_editor}
  brush-drop-ghost{9d3002ce}::content-library-brush-drag-over-geometry-canvas→ghost-rect{AcceptBeforeDelivery-peek+AcceptNoDrawDefaultRect;IsDelivery-separates-drop}
  palette-drag{ddc2a75f}::palette-entries-drag-sources{Graph_node_drag_payload:kind/type/label-char-arrays-POD}
    →canvas{both-editors,ghost,kind-checked-vs-clipboard_kind}
    →inventory/hotbar{graph-node-slot-kind:Inventory_slot-v3{graph_node_kind/type/label};click→spawn_node_from_slot{show_window+spawn-grid};find_window_by_kind{App_context-routing}}
  inventory→canvas{d4ef5d1b}::canvas-targets-also-accept-"Inventory_Slot"-payload{graph-node-slots+brush-slots{Brush*→shared_from_this};slot-keeps-content=copy-semantics}
  brush-slot-persistence-fix{5cedd865}::brush-in-slot-mutated-to-Brush-tool-across-restart
    root-cause::collect_tools-resolved-tool-name-only{brush/material-resolution="deferred-for-now"-TODO}→next-autosave-collected-brush_name-from-null→PERMANENT-config-degradation
    fix::resolve-by-name-vs-ALL-scene-content-libraries{collect_tools+per-frame-retry-while-window-renders}+write_config-preserves-unresolved-names-verbatim+user-slot-change-drops-pending
  conway-per-op{4fea814b}::"Conway"-palette-GROUP+9-node-types{conway_ambo..conway_gyro}
    Conway_node-single-class-fixed-op-at-ctor{combo-REMOVED;name=operator;only-own-param-editable}
    c_operation_infos-table=single-source{factory+palette-built-from-it};"Conway Join"/"Conway Subdivide"-labels{plain-Join/Subdivide-exist}
    legacy-"conway"::still-constructs;read_parameters-adopts-"operation"→set_name+set_factory_type_name{migrates-on-load,re-saves-as-specific-type};out-of-range-op→empty-geometry+recoverable{smoke-abuse-path-preserved}
    smoke::+per-op-section→132/132✓{fresh-headless-session}

[PREV]
>graph-editor-UX-sprint::6-commits{2026-07-15-am}✓{context-menu-spawn{e1637cc9}+preview-edge-lines{b537d80b}+source-nodes{db3a6b23}+multilink-crash{ef39f4f9}+resize-vs-drag{ad71b6ca}+arcball+preview-settings{cd164589};details:archive/2026-07-15}
>node-properties-graph-selection::DONE✓
>procedural-default-window-layout::DONE✓{bb96806e}

[STATE]
@branch::main
uncommitted::desktop_windows.json+editor_settings.json{pre-existing-local-mods}

[OPEN]
?user-verify-INTERACTIVE::
  resize-shrink{6c4dc32e}::bottom-edge-continuous-shrink-in-one-drag{+other-edges/corners}
  brush-canvas-ghost{9d3002ce}::drag-brush-over-geometry-canvas→ghost+name{zoom-scales}
  palette-drag{ddc2a75f}::palette→canvas{ghost;wrong-editor-kind-refused}+palette→inventory/hotbar+slot-click-spawns+slots-persist-restart
  inventory→canvas{d4ef5d1b}::brush-slot+graph-node-slot-drag-to-canvas
  brush-slot-restart{5cedd865}::cube-brush-slot-survives-close/restart{thumbnail-returns}
  conway-group{4fea814b}::palette+context-menu-Conway-group;drag-Truncate-to-canvas/hotbar
  earlier-sprint-list::sdl-cursor+wire-cutting+replace-on-connect+arcball+edge-lines{see-archive-2026-07-15}
?content-library-user-interactive-verify{Copy-to-Scene,texture-combo,prefab}
?content-library-deferred+selection-deferred+animation-editor{#243}+6c-fields+PhaseC+cc-perf-leftovers+#239-per-scene-settings{parked}+geogram-upstream-issue{unfiled}

[BLOCKERS]
none
