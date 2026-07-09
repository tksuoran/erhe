§MBEL:5.0

[FOCUS]
@task::uniform-scale-gizmo-handle{2026-07-09}✓impl+committed{c162eb69}
>root-cause::e_handle_scale_xyz-enum-plumbing-existed-dormant{no-handle-mesh+no-popcount-3-drag-path+no-material-mapping}
>added::center-cube-handle{gray,half-extent-0.25<plane-box-0.6→plane-handle-picking-preserved;visible-in-BOTH-scale-gizmo-modes{basic+bounding_box}}
>added::Scale_tool::update_uniform{drive=pointer-displacement-along-screen-up-right-diagonal-on-view-plane-through-anchor→s=2^(drive/gizmo_radius);deferred-first-frame-baseline→frame-1-no-op{box-mode-precedent}}
!design::distance-from-center-ratio-unusable-for-center-handle{grab-dist~0→hypersensitive+shrink-has-no-input-range}→signed-diagonal+exponential
>added::MCP-tool-set_gizmo_visibility{show_translate/rotate/scale+scale_gizmo_mode;tool-activation-otherwise-mouse-only{hotbar/viewport-toolbar}→headless-verify-needs-it}
>fixed::c_str(e_handle_scale_xz)-typo{"Scale YZ"→"Scale XZ"}
>verified::headless-screenshots{center-cube-renders,basic+bounding_box}✓+builds{ninja-win-vulkan+vs-headless}✓
?user-verify::drag-feel{windowed-run;tune-knob=drive/gizmo_radius@Scale_tool::update_uniform{one-gizmo-radius=2x||0.5x}}

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
