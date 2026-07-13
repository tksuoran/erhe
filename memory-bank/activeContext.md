§MBEL:5.0

[FOCUS]
>content-library-ownership::IMPLEMENTED✓{2026-07-13,doc/content-library-ownership-plan.md,commits:c1c75b1c-plan+99998e3d-impl}
  model::library-owned-by-Scene_root{Content_library::set_owner;Item_base.m_item_host-nullable+set_item_host{¬copied-on-clone};Content_library_node-add/remove-hooks-claim/release;ERHE_VERIFY-single-ownership}
  entries::OWNING{default,item-host=library-owner}|REFERENCE{Content_library_node.is_reference,never-touches-host;prefab-instantiate/refresh+material_preview.add_reference;GPU-Texture-¬duplicable→prefab-shared-items-stay-non-hosted}
  de-aliased::copy_content_library_folder{default+new-scenes-seed-OWN-libraries;Brush::make_shared_payload_copy-shares-geometry/primitive/collision-payload}+Scene_builder-template-library-never-owned+scene-population-uses-scene-library{floor/torus-chain/mesh-node/cube-materials}
  consumers::get_scene_root(Material*)-host-first{Properties-texture-combo-per-scene✓}+selection-scene-buckets+MCP-get_selection-scene_name-for-library-items+graph-output-nodes-via-asset-host{get_hosting_scene_root;fixes->1-scene-disables-graph-output}+Tool::get_default_material-rejects-cross-scene-material
  sharing::copy_library_item_to_library{"(N)"-collision-suffix}→MCP-copy_library_item+"Copy to Scene"-library-context-menu{copies¬aliases}
  caught-live::material_preview-claimed-inspected-material→VERIFY-abort-on-select{VS-debugger-callstack}→add_reference-fix
>verified::erhe_item_tests-8/8{+2-new-host-pointer-cases}+headless-smoke{2-scenes-distinct-brush-ids+material-scene_name+copy-collision-suffix+place-brush-scene-local-material+save/load-roundtrip-hosted+screenshot-clean}
?NEXT::user-interactive-verify{Copy-to-Scene-menu,Properties-texture-combo-across-scenes,prefab-instantiate/refresh-flows}

[PREV]
>per-scene-selection::ALL-PHASES-DONE✓{2026-07-13,doc/selection-improvements-plan.md,commits:572141cc+8f1ceb9b+a2b85321+165a73a4+458dcda1}
  note::library-items-now-HOSTED{selection-non-hosted-bucket-shrunk-to-prefab-shared+outside-library-items}

[STATE]
@branch::main
x-skills::.claude/commands-in-tree{usable-sans-LSAI:mcp__lsai__*-unregistered→grep-fallback-immediate;cpp-project.md-@code-nav-lsai/xmp4-lines-stale}

[OPEN]
?content-library-deferred::lazy-generator-brush-copies-materialize-per-scene-on-first-use{bounded,documented-in-plan}+copied-material-keeps-source-scene-texture-refs{renders,¬exports-with-target}+cross-library-DnD-still-move-semantics{menu-copy-covers-UX}
?selection-deferred{doc/selection-improvements-plan.md-open-questions}::deselect-all-command-missing{when-added→active-scene-scope}+dim-non-active-scene-selection-highlight+node-text-tint-if-title-too-subtle
?animation-editor-deferred{#243,doc/animation-keyframing-plan.md}::scene-markers+DopeTrack-key-drag-on-strip+standalone-Timeline-dock+autokey-persistence
?6c-fields-implementation{awaits-design-review,doc/geometry-nodes-plan.md}
?PhaseC-deferred-optional{C7-remainder{canvas-render-loop+links+positions→base,per-frame-risk}+C8{~9-twin-MCP-tool-bodies+scene_root-Create+save/load-dedup}}
?cc-perf-leftovers{items-4/5/6-re-rank-by-Release,9/10-user-sign-off,conway-batch=constant-factor-only}
?#239-per-scene-settings{parked→progress.md;runtime-setter-MCP-tool-exists{set_scene_settings@phase4}}
?geogram-upstream-issue{doc/geogram.md-draft,¬yet-filed}

[BLOCKERS]
none
