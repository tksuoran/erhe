§MBEL:5.0

[FOCUS]
@gltf-scene-roundtrip{doc/gltf-scene-roundtrip-plan.md,rev3}⚡phases0-5✓→?phase6{verification,LAST,handoff@prompt_queue.txt}
>phase5-commits::f5a58c5b{mutex}+1c643354{removal}+77ca784b{geogram}+533e7784{mb}+d4188760{doc}
>doc::scene_serialization.md{NEW,d4188760}::process+parts-reference{save/open-pipelines+parts-map+not-persisted-limitations;cross-linked-from-editor/scene-notes+gltf_extensions-README;wire-format-stays@gltf_extensions/,history@roundtrip-plan}
>phase5::migration+removal
  migrated::"Prefab test"+"pf2"{.erhescene→.glb,MCP-load+save,node-sets-verified-identical{name/parent/attachments/TRS/tags},bundle-dirs-deleted,.glb-untracked@res/editor/scenes}
  deleted::scene_serialization.{hpp,cpp}+scene.json-codegen-defs{29×.py;gltf_source_reference.py+scene_settings.py-STAY}+Asset_file_scene{asset-browser-bundle-handling}+.erhescene-branch@Load_scene_file-handler
  kept::erhe::Item_type::asset_file_scene{removal=renumber-42-45,editor-side-icon-use-removed}
  extras-writers::already-gone-since-phase3{ERHE_node/ERHE_material-replaced-them}→readers-STAY{erhe_flags+material-extras,old-files}+stale-setExtrasWriteCallback-comment-fixed
  smoke-tests-ported::geometry_nodes+texture_graph{save/load→.glb,read_glb_json-helper,checks→root-extensions.ERHE_node_graphs{graph_meshes/graph_textures/node_bindings/material_bindings,material-by-gltf-INDEX},load_scene-async→wait_for_scene-polls-list_scenes}
>race-fix{found-via-migration-crash}::Scene::update_node_transforms-now-locks-m_host->item_host_mutex{orphan-fallback}
  cause::async-raytrace-kickoff-worker{open_scene_gltf-tail}holds-item_host_mutex+clears/rebuilds-m_rt_primitives-while-main-thread-iterated-it{Mesh::handle_node_transform_update,AV-0xffffffffffffffff}
  diagnosis::minidump{python-minidump-pkg+llvm-symbolizer-stack-scan}¬live-repro{race-timing-hid-under-debugger}
  resolved::app_scenes.cpp-"TODO ?"-lock-comments
>geogram-wedge-fix{found-via-texture-suite-hang,user-attached-VS-debugger→real-stack}
  hang::brush-preview-thumbnail{main-thread}→make_convex_hull→PDEL::set_vertices→CellStatusArray::resize-geo_debug_assert{!Process::is_running_threads,async-worker-geogram-threads-live}→geo_abort-getchar{Windows,headless=eternal-wedge}
  fix1::editor.cpp-GEO::set_assert_mode{ASSERT_THROW-explicit-sans-debugger;GEO_DEBUG-default=ASSERT_ABORT¬THROW-as-comment-claimed}
  fix2::make_convex_hull-PDEL→BDEL{sequential;hulls-small;PDEL-precondition-unfixable-in-erhe}
  upstream::doc/geogram.md{draft-github-issue:concurrent-geogram-use+geo_abort-getchar}
!smoke-suites::run-each-in-FRESH-editor-session{material_output-material-param-resolves-via-get_single_scene_root→null-when->1-scene-open→fails-in-shared-session}
?phase6{next,fresh-context}::verification{gtest+schema-validation+headless-e2e+prefab-scenes+Khronos-validator/stock-viewer-smoke}→then-delete-prompt_queue.txt

[STATE]
@branch::main
x-skills::.claude/commands-in-tree{usable-sans-LSAI:mcp__lsai__*-unregistered→grep-fallback-immediate;cpp-project.md-@code-nav-lsai/xmp4-lines-stale}

[OPEN]
?merge-save-scene+save-prefab{user-requested-2026-07-12,handoff@prompt_queue-ITEM2,after-phase6}::one-save{source_path-writeback||res/editor/scenes}+Prefab_library::reload-side-effect;decide::editor-state-in-prefab-sources{ERHE_scene-marker-flips-open-branch}+MCP-merge+menu-removal
?animation-editor-deferred{#243,doc/animation-keyframing-plan.md}::scene-markers+DopeTrack-key-drag-on-strip+standalone-Timeline-dock+autokey-persistence
?6c-fields-implementation{awaits-design-review,doc/geometry-nodes-plan.md}
?PhaseC-deferred-optional{C7-remainder{canvas-render-loop+links+positions→base,per-frame-risk}+C8{~9-twin-MCP-tool-bodies+scene_root-Create+save/load-dedup}}
?cc-perf-leftovers{items-4/5/6-re-rank-by-Release,9/10-user-sign-off,conway-batch=constant-factor-only}
?#239-per-scene-settings{parked→progress.md;runtime-setter-MCP-tool-exists{set_scene_settings@phase4}}

[BLOCKERS]
none
