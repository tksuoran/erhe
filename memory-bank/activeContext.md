§MBEL:5.0

[FOCUS]
>scene-close-bug-class-session::6-commits{2026-07-15-eve}
  graph-mesh-raytrace-pick{c18b2608}::Graph_mesh-bound-node-invisible-to-hover+pick
    root-cause::apply_baked_products-swapped-primitives-on-REGISTERED-mesh{rt-instances-attach+mask-only-at-register_mesh→rebuilt-instances-never-attached;rebake=dangling-instances}✗+missing-Item_flags::id{id_renderer-requires-visible|id}
    fix::begin/end_mesh_rt_update-bracket{mesh-ops-pattern}+id-flag
  raycast-MCP-tool{c3ee16ce}::rt-scene-query{origin+direction;mask-default=pickable_static→hit/mesh/node/primitive_index/distance/position/normal;raytrace-visibility-now-headless-observable}
  mcp-port-exclusive{a32dbbde}::Windows-SO_REUSEADDR-shadow-bind✗{httplib-default;2nd-editor-"bound"-8080-while-1st-answers-all-connections→fallback-scan-dead+MCP-drove-WRONG-process}→SO_EXCLUSIVEADDRUSE@_WIN32{verified:8080-taken→"bound to 8081 instead"✓}
  close-scene-graph-targets{dd9022bc}::geometry+texture-graph-windows-kept-showing-closed-scene-asset{weak_ptr-never-expires:window-own-m_graph_mesh-pins}→on_close_scene-clears{primaries+Editor_windows-extras}
  scene-close-defenses{37807545}::watchdog+self-heal+rule{systemPatterns-!scene-close-bug-class+CLAUDE.md-"Scene-hosted references"}
    watchdog::on_close_scene-collects-weak{Scene_root+flat-nodes+owned-library-items¬reference-entries}→60-frames→warn-per-survivor|info-"all released"
    App_scenes::is_host_registered{Item_host*-vs-registered-scene-roots}+resolve_target-self-heal{both-graph-windows;host-less-orphan-stays-editable}
  texture-node-cycle{ba23b612}::Texture_material_output_node+Texture_output_node-m_scene_root-shared_ptr=STRONG-CYCLE{Scene_root→library→Graph_texture→node→Scene_root;scene-with-baked-texture-graph-NEVER-freed}→weak_ptr+resolve_scene_root{is_host_registered-validated;scene-selector+serialization-preserved}
  audit::Explore-agent-sweep{scene-hosted-shared_ptr-members}→remaining-findings→prompt_queue-ITEM2

[PREV]
>graph-editor-dragdrop+inventory-session::6-commits{2026-07-15-pm}✓{resize-shrink+brush-ghost+palette-drag+inventory→canvas+brush-slot-persistence+conway-per-op;details:archive/2026-07-15}
>graph-editor-UX-sprint{2026-07-15-am}✓

[STATE]
@branch::main
uncommitted::desktop_windows.json+editor_settings.json{pre-existing-local-mods}
untracked::res/editor/scenes/Default-Scene.glb{user-saved-from-their-live-session-18:40¬touch}+prompt_queue.txt{handoff}

[HANDOFF]
prompt_queue.txt::2-items!
  ITEM1::PLAN-asset-reference+asset-manager{path+asset-in-file+loaded-ptr;manager=load-once-dedup;design-questions+precedents+scene-close-constraints-in-file;PLAN-deliverable¬code}
  ITEM2::remaining-scene-close-audit-findings{ranked:clipboard-copy-pins-originals>animation-player/window>hotbar/inventory-slots{maybe-intended,ask}>brush/paint-tool-state>3-trivial-caches;watchdog-flags-all-at-runtime}

[OPEN]
?user-verify-INTERACTIVE::
  graph-mesh-hover/pick{c18b2608}::hover+click-select-Graph-Mesh-node-in-viewport{+after-graph-param-change}
  close-scene-graph-window{dd9022bc}::close-scene→geometry-graph-window-empty-state{+extra-"Open Editor"-instances}
  earlier-dragdrop-list{2026-07-15-pm}::resize-shrink+brush-ghost+palette-drag+inventory→canvas+brush-slot-restart+conway-group
?content-library-user-interactive-verify{Copy-to-Scene,texture-combo,prefab}
?content-library-deferred+selection-deferred+animation-editor{#243}+6c-fields+PhaseC+cc-perf-leftovers+#239-per-scene-settings{parked}+geogram-upstream-issue{unfiled}

[BLOCKERS]
none
