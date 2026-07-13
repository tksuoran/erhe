§MBEL:5.0

[FOCUS]
>merge-save-scene+save-prefab::DONE✓{2026-07-13,commit-b1eecef0,prompt_queue-ITEM2-cleared}
  one-save-shape::always-full-editor-state{save_scene_gltf+add_gltf_editor_state}
  destination::source_path{save-back-silent,¬modal}||res/editor/scenes/<name>.glb{overwrite-modal;success→set_source_path-writeback}
  prefab-reload-side-effect@save_scene_gltf{App_context&-overload;written-path∈prefab_library→reload→instances-refresh}
  open_scene_gltf-now-sets-source_path{weakly-canonical,like-Scene_open_operation}
  removed::File.SavePrefab-command+menu+Operations::save_prefab+save_prefab_scene+MCP-save_prefab
  MCP-save_scene::path-optional{omitted→resolve_scene_save_path;explicit-path¬re-associates}
  new-helpers@parsers/gltf::save_scene_gltf(App_context&)+default_scene_dir+resolve_scene_save_path
  limitation-documented::prefab-templates-ignore-ERHE_*-payloads→graph-baked-products-missing-in-instances{doc/scene_serialization.md}
>verified::prefab-edit-loop-18/18{author→instantiate→load-source→edit→path-less-save→instance-refreshed}+scene_roundtrip_verify-73/73{validator-section-skipped:not-installed;75-baseline-includes-it}
>gltf-scene-roundtrip{doc/gltf-scene-roundtrip-plan.md,rev3}::ALL-PHASES-DONE✓{phase6-2026-07-12,commits:ef31a4bb+f9f11321+90a8860a;harness:scripts/scene_roundtrip_verify.py}
?NEXT::none-designated{pick-from-OPEN}

[STATE]
@branch::main
x-skills::.claude/commands-in-tree{usable-sans-LSAI:mcp__lsai__*-unregistered→grep-fallback-immediate;cpp-project.md-@code-nav-lsai/xmp4-lines-stale}

[OPEN]
?animation-editor-deferred{#243,doc/animation-keyframing-plan.md}::scene-markers+DopeTrack-key-drag-on-strip+standalone-Timeline-dock+autokey-persistence
?6c-fields-implementation{awaits-design-review,doc/geometry-nodes-plan.md}
?PhaseC-deferred-optional{C7-remainder{canvas-render-loop+links+positions→base,per-frame-risk}+C8{~9-twin-MCP-tool-bodies+scene_root-Create+save/load-dedup}}
?cc-perf-leftovers{items-4/5/6-re-rank-by-Release,9/10-user-sign-off,conway-batch=constant-factor-only}
?#239-per-scene-settings{parked→progress.md;runtime-setter-MCP-tool-exists{set_scene_settings@phase4}}
?geogram-upstream-issue{doc/geogram.md-draft,¬yet-filed}

[BLOCKERS]
none
