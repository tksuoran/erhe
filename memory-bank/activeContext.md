§MBEL:5.0

[FOCUS]
@animation-curve-editor{#243,87678676}✓implemented+headless-verified{2026-07-09}?awaits-user-interactive-verify
>replaced::Timeline_window+Properties-curve-plot→Animation_window{combo+transport+channel-pane+curve-canvas{pan/zoom+ruler-scrub+box-select+drag-keys+ctrl-click-insert+Delete/X}}
>added::Animation_player{per-frame-update@Editor::tick,apply→nodes+Animation_update_message}+animation_edit-helpers{glTF-sampler-storage,cubic-tangent-blocks}+Animation_edit_operation{undo}
>added::MCP{get_scene_animations+set_animation_target+animation_playback+animation_edit_keyframe}
>verified::RiggedFigure.glb{57ch,pose@seek✓,play-advances✓,move/insert/delete+undo✓,screenshots✓}

[STATE]
@branch::main
x-skills::.claude/commands-in-tree{usable-sans-LSAI:mcp__lsai__*-unregistered→grep-fallback-immediate;cpp-project.md-@code-nav-lsai/xmp4-lines-stale}

[OPEN]
?6c-fields-implementation{awaits-design-review,doc/geometry-nodes-plan.md}
?PhaseC-deferred-optional{C7-remainder{canvas-render-loop+links+positions→base,per-frame-risk}+C8{~9-twin-MCP-tool-bodies+scene_root-Create+save/load-dedup}}
?cc-perf-leftovers{items-4/5/6-re-rank-by-Release,9/10-user-sign-off,conway-batch=constant-factor-only}
?#239-per-scene-settings{parked→progress.md}

[BLOCKERS]
none
