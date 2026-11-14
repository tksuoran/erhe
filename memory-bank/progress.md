§MBEL:5.0

@rule::active-tasks-only;DONE-work=one-pointer-line-to-its-doc(details=doc+commit-messages);DONE-task-sections-move-to-memory-bank/topics/<topic>.md[PROGRESS]

[TASK::prepared-assets]{started-2026-11-06;branch=main;doc/plans/prepared_assets_design.md(steps)+doc/plans/prepared_assets.md(R-numbers)}
✓M1-M5+D8-steps-1-15+raw-block-KTX2-refused-at-import+tinybvh-items+Pvs_bits_source-removal{2026-11-06..07;state+open-per-step=doc/plans/prepared_assets_design.md-D7/D8;behavior=doc/editor/prepared_assets.md+doc/erhe/asset_store.md+doc/erhe/gltf.md;verify=scripts/prepared_asset_verify.py+scripts/prepared_asset_bistro_verify.py}
✓pvs-scene-id-warning{802820c69;root-cause=open_scene_gltf-compared-PVS-header-id-with-get_scene_id()-which-GENERATES-an-id;Bistro-has-no-stored-id;now-compares-stored-id;prepared_asset_verify.py-check-added}
⚡quest-R13.4-remeasure-2026-11-07-22:15{Mac-release-APK-802820c69;device-needed-uninstall(other-machine-signing-key;©User-ok;files-moved-aside+restored);doc/reference/quest_bench_runs.md-22:15-entries;prepared-4.66s/800MB-vs-old-prepared-glb-5.20s/783MB;!old-glb-opens-UNTEXTURED+no-PVS(R12.2)->101-vs-83fps-is-texturing;R13.4-comparison-DROPPED(©User-2026-11-07;numbers-recorded-only)}✓
✓session-2026-11-08{status-on-request(84cfd0ed1)+world-space-shadows(33e8660cd)+render_scene_image-lod_nodes(6a3463923)+Windows/GCC-CI-fixes(see-activeContext)}
✓CI-e15ff8aca+3b181b4f4-green(GCC-weld-test-passes)
✓session-2026-11-08b-open-items-sweep{see-activeContext}
?then{CI-check-3458ea70f+707b1fcb6(Windows-'?'/UNC-paths;OpenXR-headset_view-first-compile-on-Android)+©User-rulings(remaining-D8-decisions)+prepared_asset_bistro_verify.py-LOD-off-for-images+mcp_server_tests-unrun(windowed)}

[TASK::quest-fragment-shader]{started-2026-11-08;branch=main;doc/plans/quest_fragment_shader.md}
✓plan{faf8033cc;shadow-lookup=60%-of-headset-lit-shader}
✓M1/M2-normal_offset{aaab0b1e2;offsets-1.0/1.0;shadow_verify-core-16bit:G1-0-acne,G3<=1.25t,G4-pass,G5<=1t(spot-2.5);G2=hardware-filter-softening}
✓M5-identity-texture-transform+OpenXR-preset{4be47d3c5}
✓device-bench{2026-11-08-12:04..12:08;Bistro-street-58.7->108fps,app-GPU-11.6->6.35ms;default-app-GPU-5.45->4.69ms;fp16-mask-1-level-on-Bistro,-8%-multiview-default->OpenXR-preset-shader_fp16_paths=1}
✓M4-16bit-material-record{23a32cc1d;material_record_half=auto(on-Android);320->192B;Adreno-13->12-regs,100%-occupancy;device-level(Bistro-105.8fps)}

[TASK::animation-editing]{started-2026-11-08;branch=main;doc/editor/editor.md"Animation window and playback"+doc/plans/timeline_editor.md}
✓paused-pose-edits{fly-camera+gizmo+MCP-set_node_transform-adopt-a-paused-playback-pose(adopt_playback_pose);keying-clears-session_only(default-camera-saves)}
✓animation-paths-debug-visualization{Debug-Visualization-popup:Animation-Paths-off/selected/all+Path-Interval;cyan/magenta-polyline+triad-at-playhead}
✓curve-editing{interpolation-menu(Step/Linear/Cubic)+Smooth-Tangents+handles(Alt=one-side)+Shift/Shift+Alt-drag-locks+prev/next-key(Up/Down)+Insert-Key(I);erhe_math/hermite.hpp-shared-with-texture-curve-widget;MCP-animation_set_interpolation+animation_set_tangents}
✓benchmark-animation-camera-path{camera_path.type=animation(animation+node-names;resolved-at-settle);benchmark_bistro_street(_90).json;Bistro-camera-animation-saved-in-logs/quest_bistro/2026-11-07/bistro_src.glb+quest-prepared-asset-re-exported(PVS-2068-cells)+pushed;device:90Hz-34.4fps-GPU-bound(multiview-22ms-median;first-run-after-install-11fps=one-off)}
?open{Properties-window-transform-edits-still-write-under-a-paused-pose;undo-of-keying-keeps-session_only-cleared;device-migrated-config-is-stale-after-install(fp16-preset-set-via-MCP)}

[TASK::world-space-pieces]{started-2026-11-08;branch=main;commit-735bfac9e;doc/erhe/primitive.md"Pieces";scripts(scratchpad,not-committed):pieces_bench.py/pieces_lists.py/quest_export.py/quest_pieces.sh}
✓split+lod-border-locks+gltf-member+buffer-mesh-pieces+entries-per-piece+setting+key+unit-test+docs
✓desktop-measured{corner-pose,world-space-form:tris-267k->71k(8m)/65k(5m);raster-6.75->6.16/5.89;shaded-2.4-level;0-mismatches;screenshots-differ<0.1%}
✓settle-failure-root-cause{003316476;animation_create_key-resolved-"Camera"-in-empty-startup-"Scene 1"(--no-scene)->exports-lacked-Animation+camera;tools-take-scene_name+ambiguous-name=error;assets-re-exported+pushed}
✓benchmark-views-head-locked{4ca6ec37b;eye-views-latched-once(view-space)+projection-layer-in-VIEW-space;©User-verified-worn:path-followed+image-fixed;!all-Quest-bench-runs-before-2026-11-08-21:18-INVALID(©User)}
✓quest-measured{2026-11-08-21:22..21:27;one-input-hash;0m=33.6fps(multiview-25.0ms,457-entries)|8m=25.7fps(31.3ms,1388)|5m=21.2fps(37.4ms,2294);pieces-SLOWER-on-Adreno}
?next{find-why(per-draw-cost|binning-of-more-smaller-draws|split-vertices)-before-splitting-further}

[TASK::bistro-quest-benchmark]{started-2026-11-08;branch=main;doc/reference/quest_bench_runs.md(17:12..17:37-entries)+doc/plans/open_items.md"Quest session follow-ups"}
✓run-cmd{python3 scripts/quest_bench.py run --bench config/editor/benchmark_bistro_street_90.json --bench-to-device --editor-args=--no-scene --config scripts/quest_bench_configs/bistro_headlamp_gpu_occlusion.json --mcp 'set_graphics_preset={"shadow_bias":"normal_offset","shader_fp16_paths":1}' --load-scene /sdcard/Android/data/org.libsdl.app.quest/files/bistro/bistro_src.quest.erhe_prepared --mcp-measure get_lod_stats --record;off-head;add---perfetto|--gpu-metrics-for-counters(exclusive)}
✓findings{first-run-after-APK-install-11fps=one-off(repeat-33-34fps);LOD-threshold-64-active(levels-0-5-in-use);eye-pass-fragment-bound-by-overdraw-at-bistro-corner(t2-3s);alpha-tested+translucent=4ms/frame-of-22;opaque-alone-18.4ms(458k-tris,613-entries)}
!traps{MCP-set_node_transform-arg=translation(¬position:silently-"Nothing to set");device-migrated-config-stale-after-install(fp16-preset-via---mcp);desktop-editor-has-no-headlamp(headset-only)->desktop-renders-show-scene-lights;a-second-editor-takes-port-3744;disk-nearly-full(1.7GB-free;Debug-trees=next-cleanup)}
?next{opaque-draw-order-front-to-back/early-Z-check|shorten-or-re-pose-path-away-from-corner|Perfetto-render-stages-empty(surfaces-[])->investigate-metavr-capture}


[TASK::navigation-mesh]{started-2026-11-10;branch=main;doc/editor/navigation.md+doc/plans/navigation_mesh.md}
✓steps-1-8{core+settings+seeds+job+staleness+visualization+Export-stage+editor-asset-item(Item_kind::navigation_mesh=9-on-main)}
!traps{Recast-target-built-without--fno-rtti(editor-subclasses-rcContext)+hollow-closed-mesh-gets-inner-walkable-floor(unreachable)+Recast-DebugUtils-fails--Werror-on-GCC->build-named-targets}
?next{open-items-in-doc/plans/navigation_mesh.md|triangle-classification-plan-needs-triangle-visibility(branch-triangle-visibility)}

[TASK::level-tools+curves]{started-2026-11-13;branch=main(UNPUSHED);plans=doc/plans/level_tools_implementation.md+doc/plans/curves.md;state=topics/level_tools.md}
✓level-tools-steps-1-11+23-26+curves-steps-1-4{behavior=doc/editor/level_tools.md+doc/editor/curves.md+doc/editor/point_set_editing.md;math=doc/erhe/math.md"Curve path"}
✓user-test-round-2026-11-14{b155b1bb1(curve-x-ray-drawing+handles)+78ff5a3c8(style-settings+removal-keeps-remaining-selection+live-curve-drag-preview)+1bf45ade7(path-layout=one-Curve-node+Kit-Layout-group-in-Properties+Level-Kit-Layout-section=initial-values+"Place As Layout")+c73e7a411(one-Level_kits-layout-preview:begin/step/end-for-curve-point-drags+Properties-value-drags+Transform-drags-of-surface-snapped-layouts)}
✓verify-at-c73e7a411{level_tools_verify.py-628(--only-layout-drags-27)+curve_verify.py-147+navigation_verify.py-76+editor_level_tests-63+erhe_math_tests-107;mcp_server_tests=compiled-never-run(needs-display)}
!not-compiled-locally{USD-export-warning-for-Curve(trees-build-ERHE_USD_LIBRARY=none)+Box3D-get_penetration_depth-stub}
?next{©User-feedback-from-hands-on-testing->then-curves-steps-5-8|level-tools-Next-steps;open-defects:level-plan-step-20(scripted-gizmo-drags-sub-pixel-offset)+27(Properties-transform-rows-skip-the-layout-move-hook)+29(surface-resting-disagreements-x3)+30(no-Escape-cancel-of-a-Transform-node-drag)}
