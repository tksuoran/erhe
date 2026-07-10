§MBEL:5.0

[FOCUS]
@animation-editor{#243}⚡iterating-with-user{curve-editor{87678676}→Animated-filter{7dcf73b7}→two-list-pane{f35e2a3b}→tri-state{730c52b8}→ctx-menu{6a120891}→LW-keying{aa78d9ec}}
>LW-keying{aa78d9ec}::autokey{off|modified|all,hook@Transform_tool::record_transform_operation,compound-undo-with-transform}+CreateKey/DeleteKey{selected-objects,auto-create-channels+animation}+timeline-strip{scrub+ticks+key-markers{filter:selected-objects|active-animation|shown-channels}}+MCP{animation_create_key+animation_delete_key+autokey-param}
!gltf-granularity::channel=whole-path{T/R/S}¬per-axis{LW-X/Y/Z/H/P/B-inapplicable}
@plan::doc/animation-keyframing-plan.md{deferred:scene-markers+DopeTrack-key-drag-on-strip+standalone-Timeline-dock+autokey-persistence}
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
