§MBEL:5.0

[FOCUS]
@gltf-scene-roundtrip{doc/gltf-scene-roundtrip-plan.md,rev3,design-reviewed-2026-07-12}⚡phases0-4✓→?phase5{migration+removal,handoff@prompt_queue.txt}
>phase4{3a4989b6}::save/open-switchover
  save_scene_gltf+open_scene_gltf{parsers/gltf.hpp}::save=1×export_gltf{physics+prefab-external-assets+texture-sources+animations+add_gltf_editor_state,ERHE_scene-in-extensionsUsed=erhe-authored-marker}
  open::¬undoable+fresh-EMPTY-Content_library+ERHE_scene-applied{enable_physics@Scene_root-ctor+ambient_light+Scene_settings-codegen-deserialize}+¬import_root-wrapper+¬default-camera/lights
  Load_scene_file_message-handler{operations_window-ctor}::single-load-entry{menu+asset-browser+MCP+--scene+commands.json}→branch{.erhescene→legacy-loader|ERHE_scene-glb→open_scene_gltf|foreign-glTF→Scene_open_operation}
  File>SaveScene::<name>.glb@res/editor/scenes{Overwrite/Cancel-modal-kept};File>LoadScene::.glb/.gltf-file-picker{was-folder-dialog}
  MCP::save_scene→.glb;load_scene→queued{poll-list_scenes};+get_scene_settings/set_scene_settings{#239-runtime-setter,settings=REPLACE-semantics}
  physics-import-fix::folded-shape-carrier-nodes{"<body>_collider_<i>",¬attachments+¬children}removed-after-compound-fold→save/open/re-save-node-identical{was+6-nodes/cycle}
  per-scene-imgui.ini::REMOVED{scene_imgui_ini_path-deleted}
>verified-headless::save→open→re-save-identical{nodes+ERHE_scene/collections/brushes/physics-payloads};settings/ambient/physics-applied-on-open;open-¬on-undo-stack;foreign-glb→Scene_open_operation;legacy-.erhescene-loads;screenshots✓
?phase5{next,fresh-context}::migrate-local-bundles{load→save-.glb}→delete{scene_serialization.{hpp,cpp}+scene.json-codegen-defs{gltf_source_reference.py-STAYS}+geogram-mesh_save/load-in-save-path+Asset_file_scene/.erhescene-handling}+notes/CLAUDE.md/memory-bank-updates
?phase6{last}::verification{gtest+schema-validation+headless-e2e+foreign-tool-smoke}

[STATE]
@branch::main
x-skills::.claude/commands-in-tree{usable-sans-LSAI:mcp__lsai__*-unregistered→grep-fallback-immediate;cpp-project.md-@code-nav-lsai/xmp4-lines-stale}

[OPEN]
?animation-editor-deferred{#243,doc/animation-keyframing-plan.md}::scene-markers+DopeTrack-key-drag-on-strip+standalone-Timeline-dock+autokey-persistence
?6c-fields-implementation{awaits-design-review,doc/geometry-nodes-plan.md}
?PhaseC-deferred-optional{C7-remainder{canvas-render-loop+links+positions→base,per-frame-risk}+C8{~9-twin-MCP-tool-bodies+scene_root-Create+save/load-dedup}}
?cc-perf-leftovers{items-4/5/6-re-rank-by-Release,9/10-user-sign-off,conway-batch=constant-factor-only}
?#239-per-scene-settings{parked→progress.md;runtime-setter-MCP-tool-now-exists{set_scene_settings@phase4}}

[BLOCKERS]
none
