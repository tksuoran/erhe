§MBEL:5.0

[FOCUS]
>houdini-graph-features::IMPLEMENTED✓{2026-07-14,commits:a7d98635+886e4c31+77feb2a8}
  wire-cutting::CutLinksAction-in-vendored-imgui-node-editor{hold-Y+LMB-drag;Config::CutLinksKey;segment-vs-curve-ImCubicBezierLineIntersect;crossed-links→DeleteItemsAction→standard-QueryDeletedLink-flow→all-3-graph-windows-undoable-zero-per-window-changes;Escape-cancels;accepted-before-SelectAction→suppresses-box-select}
  display/ghost-flags::per-graph-ids-on-Geometry_graph{0=none;setters-mark-scene-outputs-dirty;two-phase-evaluate{scene-outputs-last→designation-reads-any-payload-despite-topo-order};ids-resolve-via-get_log_id→shadow-clone-raw-copy-in-launch_evaluation}
    display::replaces-output-wired-input-in-WHOLE-bake{render+physics}
    ghost::edge-lines-only-companion-mesh{visible|render_wireframe,¬content/shadow_cast/id;no-raytrace;ghost_geometry+ghost_primitive-in-baked-products;second-controlled-mesh-in-Geometry_graph_mesh;excluded-from-gltf-export}
    ghost-pass::"Ghost edge lines"@app_rendering{wide-line-group-3,filter-render_wireframe,fixed-dim-violet-primitive_settings,¬render-style-gated;fed-in-Viewport_scene_view+Headset_view-color-mode;LIMITATION:invisible-in-id-buffer-edge-mode}
    ui::D/G-badges-in-after_node_content{hidden-for-outputs/non-geometry/non-asset-nodes;undoable-Geometry_graph_display_designation_operation{one-op-covers-badge-move}}
    serialization::display_node/ghost_node-as-node-indices-in-graph-JSON
    mcp::geometry_graph_set_display_flags+get_geometry_graph-reports-ids
  node-previews::texture-graph-style-per-node-mesh-thumbnails{OFF-default;per-Graph_mesh-toggle{checkbox+geometry_graph_set_node_previews-MCP};enable→mark_dirty-full-re-eval}
    worker::build_preview_primitive-fill-only-after-evaluate{Geometry_graph::set_preview_mesh_memory-shadow-only-hook};take_preview-in-finish_evaluation→needs-render
    main::Geometry_graph_window::update_node_previews{budget-2/frame;lazy-128px-per-node-texture;Brush_preview-new-primitive-overload{brush-overload-delegates}}
    on_removed_from_graph-releases-preview-GPU-resources{output/group-overrides-call-base-now}
  verified::headless-MCP+screenshots{display-override+ghost-render+undo/redo+save/reload-roundtrip+previews-visible}✓
  ?user-interactive-verify-PENDING::wire-cutting{Y+drag-in-all-3-graph-windows,undo,Escape,plain-drag-still-box-select}+badge-clicks+preview-toggle-checkbox

[PREV]
>procedural-default-window-layout::DONE✓{2026-07-14,commit:bb96806e;iterate:edit-default_layout.json→rm-ini→relaunch}
>content-library-ownership::IMPLEMENTED✓{2026-07-13,doc/content-library-ownership-plan.md,commits:c1c75b1c+99998e3d}
>per-scene-selection::ALL-PHASES-DONE✓{2026-07-13,doc/selection-improvements-plan.md}

[STATE]
@branch::main
uncommitted::desktop_windows.json+editor_settings.json{pre-existing-local-mods}+res/editor/scenes/{untracked}

[OPEN]
?houdini-graph-user-verify{see-FOCUS}
?default-layout-closed-window-placement-imperfect{user:"not bad,ignore-for-now"}
?content-library-user-interactive-verify{Copy-to-Scene-menu,Properties-texture-combo-across-scenes,prefab-instantiate/refresh}
?content-library-deferred::lazy-generator-brush-copies+copied-material-texture-refs+cross-library-DnD-move-semantics
?selection-deferred{doc/selection-improvements-plan.md}::deselect-all-command+dim-non-active-scene-highlight
?animation-editor-deferred{#243,doc/animation-keyframing-plan.md}
?6c-fields-implementation{awaits-design-review,doc/geometry-nodes-plan.md}
?PhaseC-deferred-optional{C7-remainder+C8}
?cc-perf-leftovers{items-4/5/6-re-rank-by-Release,9/10-user-sign-off}
?#239-per-scene-settings{parked→progress.md}
?geogram-upstream-issue{doc/geogram.md-draft,¬yet-filed}

[BLOCKERS]
none
