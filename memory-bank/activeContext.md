§MBEL:5.0

[FOCUS]
>gltf-scene-roundtrip{doc/gltf-scene-roundtrip-plan.md,rev3}::ALL-PHASES-DONE✓{phase6-verification-complete-2026-07-12}
>phase6-commits::ef31a4bb{gltf-export-fixes}+f9f11321{mcp-parent_id+import_root}+90a8860a{harness+docs}
>phase6-harness::scripts/scene_roundtrip_verify.py{75/75✓;builds-scene-all-11-ERHE_*+physics-joint+brush+graphs+layout+tags+authored-anim-keys+prefab-instance→save×2{2.0-foreign+2.1-full}→schema-validate{embedded-subset-validator,unknown-keyword=fail}→reload+MCP-diff{import_root-aware}→Prefab-test.glb-roundtrip→Khronos-validator{0-errors-required}+Blender-headless-render}
>phase6-found+fixed::3-export-defects+1-mcp-gap
  1::zero-NORMAL-dual-listing{allocated-but-unset-geogram-attrs,present_*-mask-false→3388×ACCESSOR_VECTOR3_NON_UNIT;fix:dual-list-requires-fully-present}
  2::soup-primitives-exported-geometry-normative{derived-welded-geometry-won→TEXCOORD/JOINTS/WEIGHTS-dropped;fix:soup=source-of-truth-exports-as-is,ERHE_geometry-only-on-authored-geometry;+mesh-names-exported}
  3::settings-less-joints-skipped{fix:export-empty-joint-description,reload-materializes-settings-item}
  4::get_scene_nodes+parent_id+import_root{wrapper-transparency-diffable}
>phase6-regression-net::geometry-smoke130/130✓+texture-smoke268/268✓{post-exporter-change}
>validator::3394-errors→0{both-saved-files}
?NEXT::merge-Save-Scene+Save-Prefab{prompt_queue-ITEM2,user-requested-2026-07-12,fresh-context-recommended}

[STATE]
@branch::main
x-skills::.claude/commands-in-tree{usable-sans-LSAI:mcp__lsai__*-unregistered→grep-fallback-immediate;cpp-project.md-@code-nav-lsai/xmp4-lines-stale}

[OPEN]
?merge-save-scene+save-prefab{NEXT,handoff@prompt_queue-ITEM2}::one-save{source_path-writeback||res/editor/scenes}+Prefab_library::reload-side-effect;decide::editor-state-in-prefab-sources{ERHE_scene-marker-flips-open-branch}+MCP-merge+menu-removal;re-run-affected-scene_roundtrip_verify-checks-after
?animation-editor-deferred{#243,doc/animation-keyframing-plan.md}::scene-markers+DopeTrack-key-drag-on-strip+standalone-Timeline-dock+autokey-persistence
?6c-fields-implementation{awaits-design-review,doc/geometry-nodes-plan.md}
?PhaseC-deferred-optional{C7-remainder{canvas-render-loop+links+positions→base,per-frame-risk}+C8{~9-twin-MCP-tool-bodies+scene_root-Create+save/load-dedup}}
?cc-perf-leftovers{items-4/5/6-re-rank-by-Release,9/10-user-sign-off,conway-batch=constant-factor-only}
?#239-per-scene-settings{parked→progress.md;runtime-setter-MCP-tool-exists{set_scene_settings@phase4}}
?geogram-upstream-issue{doc/geogram.md-draft,¬yet-filed}

[BLOCKERS]
none
