§MBEL:5.0

[FOCUS]
>graph-editor-UX-sprint::6-commits{2026-07-15,all-headless-verified-where-possible}
  context-menu-spawn{e1637cc9}::right-click-add-node-spawns-AT-click-position{GetMousePosOnOpeningCurrentPopup→ScreenToCanvas{valid-while-suspended};add_node_from_palette+add_node_of_type-gain-optional-position;palette-list-passes-nullptr→grid}
  preview-edge-lines{b537d80b}::graph-node+brush-previews-draw-edge-overlay{Brush_preview-2nd-composition-pass{solid_wireframe+SOLID_WIREFRAME-mask,depth-LE-no-write-pipeline-mirrors-Pipeline_renderpasses};per-render_preview-call-state{two-settings-groups};preview-primitives-build-fill_triangles_expanded;codegen-Preview_edge_lines_config{enabled+width+color}×2-in-editor-settings-v14{graph=on,brush=off-via-designated-init-field-default};settings-UI-reflection;window-snapshots-settings→re-arms-cached-previews}
  source-nodes{db3a6b23}::Brush+Scene_Mesh-geometry-source-nodes{types:brush+scene_mesh;palette-"Sources";MAIN-THREAD-geometry-capture{lazy-getters-unsafe-on-worker}→evaluate-only-copies;serialize-by-name;resolve-owner-scene→ALL-scenes{shadow-clones-ownerless};Refresh-re-captures;drag-brush-from-item-tree-onto-canvas→bound-Brush-node-at-drop-pos{BeginDragDropTargetCustom+"Content_library_node"-payload}}
  multilink-crash-fix{ef39f4f9}::2nd-link-to-linked-input-pin-crashed{VS-debugger-diagnosed:payload-merge-geometry-lacked-connectivity→catmull-clark-get_vertex_corners-empty-table-fastfail-on-worker{NOT-c++-exception→try/catch-blind→silent-death}}
    root-fix::operator+=-merge-runs-process_for_graph{payload-invariant-restored}
    defense::ERHE_VERIFY-bounds-in-get_vertex_corners/edges/corner_facet
    behavior::replace-on-connect{single-link-pins;old-link-out+new-in=ONE-Compound_operation;geometry+texture-windows;canvas+MCP}
    multi-input-sockets::Pin::multi_link-flag{join-in,instance-points,realize-instances=accumulate-preserved;smoke-130/130}
  resize-vs-drag{ad71b6ca}::vendored-SizeAction::Accept-hit-tests-PRESS-position{ImGui_GetMouseClickPos}¬current{drag-threshold-fires-after-motion→flick-left-6px-edge-strip→fell-to-DragAction}
  arcball+preview-settings{cd164589}::LMB-drag-on-preview-image-tumbles{InvisibleButton-overlay{active-item-suppresses-node-drag};yaw-worldY+pitch-worldX-premultiplied;auto-rotate-paused-during-drag-resumes-after;orientation-quat-persists}
    settings::Graph_node_previews_config{enabled+auto_rotate,BOTH-default-true}-editor-settings-v15{editor-global+persistent;per-Graph_mesh-flag-REMOVED;Auto-rotate-checkbox-next-to-Show-node-previews;MCP-geometry_graph_set_node_previews→global+marks-all-graphs-dirty-on-enable}
    orientation::float-Y-angle→glm::quat{Brush_preview::render_preview-takes-quat;brush-thumbnails-derive-Y-spin-from-time}

[PREV]
>node-properties-graph-selection::DONE✓{2026-07-15,user-verified-selection+pin-edges+resize}
>houdini-graph-features::DONE✓{cut-fixes+previews+shading}
>procedural-default-window-layout::DONE✓{bb96806e}

[STATE]
@branch::main
uncommitted::desktop_windows.json+editor_settings.json{pre-existing-local-mods}

[OPEN]
?user-verify-INTERACTIVE::
  sdl-cursor-fix{040e6f18}::hover-edge/corner-cursors-change
  wire-cutting-re-test{11061763}::slow-Y+drag-highlights+DELETES
  brush-drag-drop{db3a6b23}::drag-brush-onto-canvas→bound-node-at-cursor+undo
  replace-on-connect{ef39f4f9}::drag-link-to-linked-input→old-drops{1-undo-restores};join-still-stacks
  resize-gesture{ad71b6ca}::fast-flick-from-edge-always-resizes¬drags
  arcball{cd164589}::LMB-drag-tumbles{node-¬moves}+auto-rotate-pause/resume+checkbox-off+settings-survive-restart
  edge-lines{b537d80b}::settings-groups-in-Settings-window+brush-preview-toggle-on-look
?content-library-user-interactive-verify{Copy-to-Scene,texture-combo,prefab}
?content-library-deferred+selection-deferred+animation-editor{#243}+6c-fields+PhaseC+cc-perf-leftovers+#239-per-scene-settings{parked}+geogram-upstream-issue{unfiled}

[BLOCKERS]
none
