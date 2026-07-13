§MBEL:5.0

[FOCUS]
>per-scene-selection::ALL-PHASES-DONE✓{2026-07-13,doc/selection-improvements-plan.md,commits:572141cc+8f1ceb9b+a2b85321+165a73a4+458dcda1}
  model::selection-partitioned-per-Item_host{union=authoritative;Item_host.hosted_selection=bucket-derived-at-query{clear+refill,capacity-kept}→no-stale-on-cross-scene-reparent;non-hosted-bucket-in-Selection{materials/library=non-hosted}}
  active-scene::Selection::get_active_scene_root{explicit-weak-Scene_root;set-by:selection-change+viewport/hierarchy-window-focus+MCP-set_active_scene;¬hover;fallback:last_scene_view→single-scene;Active_scene_changed-msg}
  scoped-semantics::plain-click/CtrlA/range-selection-per-host{other-scenes'-selection-persists};commands→active-scene-bucket+non-hosted{get_command_target_selection:delete/cut/duplicate/mesh-ops/merge/booleans/keying/paste-target/frame-selection}
  gizmo::binds-ACTIVE-scene¬per-viewport{entries/anchor-read-outside-render{numeric-edits+MCP+undo}→per-view-rebuild=render-order-dependent;handles-visible/hoverable/draggable-only-in-active-scene-views{update_for_view-per-view-flag}}
  ui::active-scene-title+dock-tab-tint{windows/active_scene_highlight;on_begin-push/on_end-pop}
  mcp-mirrors-ux::select_items-scoped-clear+activates-target-scene{explicit,¬only-diff:re-select-has-no-diff};set/get_active_scene;get_selection-reports-per-item-scene_name+active_scene
  fixed-latent::Operation_stack-worker-queue-abort{214c12c5-audit-missed-async-mesh-op-lambdas-on-tf-workers→queue_from_thread-inbox-drained-in-update}+bake/center_transform-empty-items{geometry-half-was-no-op}+leading-material-mesh-op-abort+clipboard-null-derefs
>verified::headless-smokes{selection-8/8+gizmo-7/7+phase4-5/5+phase5-7/7;fresh-session-each;two-scenes}
?NEXT::user-interactive-verify{tints,gizmo-visible-only-in-active-scene-viewports,drags,CtrlA/ctrl-click-across-hierarchy-windows}

[STATE]
@branch::main
x-skills::.claude/commands-in-tree{usable-sans-LSAI:mcp__lsai__*-unregistered→grep-fallback-immediate;cpp-project.md-@code-nav-lsai/xmp4-lines-stale}

[OPEN]
?selection-deferred{doc/selection-improvements-plan.md-open-questions}::deselect-all-command-missing{when-added→active-scene-scope}+dim-non-active-scene-selection-highlight+node-text-tint-if-title-too-subtle
?animation-editor-deferred{#243,doc/animation-keyframing-plan.md}::scene-markers+DopeTrack-key-drag-on-strip+standalone-Timeline-dock+autokey-persistence
?6c-fields-implementation{awaits-design-review,doc/geometry-nodes-plan.md}
?PhaseC-deferred-optional{C7-remainder{canvas-render-loop+links+positions→base,per-frame-risk}+C8{~9-twin-MCP-tool-bodies+scene_root-Create+save/load-dedup}}
?cc-perf-leftovers{items-4/5/6-re-rank-by-Release,9/10-user-sign-off,conway-batch=constant-factor-only}
?#239-per-scene-settings{parked→progress.md;runtime-setter-MCP-tool-exists{set_scene_settings@phase4}}
?geogram-upstream-issue{doc/geogram.md-draft,¬yet-filed}

[BLOCKERS]
none
