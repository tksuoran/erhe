§MBEL:5.0
©erhe::Topic::rendering

[FOCUS_ARCHIVE_2026-11-07]{compacted-pointers;moved-from-activeContext.md}
@debug-line-aa::DONE-2026-09-29{doc/plans/debug_renderer_anti_aliasing.md}
@shadow-robustness::DONE-2026-09-29{doc/plans/shadow_robustness.md}
@radiance-cascades::PHASES-0-7-BUILT-2026-09-28{doc/plans/radiance_cascades.md}

[PROGRESS_2026-11-07]{compacted-pointers;moved-from-progress.md}
[TASK::radiance-cascades]::archived{doc/plans/radiance_cascades.md;memory-bank/local/gi.md;doc/plans/ddgi.md;detail=git-log}
[TASK::shadow-robustness]::archived{doc/plans/shadow_robustness.md;doc/reference/shadow_robustness_review_2026_09_29.md;detail=git-log}
[TASK::debug-line-anti-aliasing]::archived{doc/plans/debug_renderer_anti_aliasing.md;detail=git-log}

[FOCUS_ARCHIVE_2026-11-22]{moved-verbatim-from-activeContext.md;hashes+UNPUSHED-notes-as-written-then:everything-through-3b760af35-is-pushed}
@detail-blend-weight::BUILT-2026-11-22{all-5-steps-built;local-main-UNPUSHED(d3385eacd=one-squashed-commit:requirements+design+steps-1-5;then-own-commits:d4a451a14-graph-bakes-hidden(show_in_ui-off)+Graph-Textures-inside-Textures+slot-row-drops(©User-ruling;older-scenes-not-migrated)|0dd840391-Graph-Texture-icon|2e77a2fad-Imgui_windows::flush_queue-runs-ops-outside-its-lock(Open-Properties-crash)|673dc8dde-ensure_vertex_corners-for-loaded-Geometry(Paint+Weight-Paint;©User:build-the-table,not-scan));docs:requirements=doc/plans/detail_blend_weight.md(B1-B8)+design=_design.md(BD1-BD9)+state=_implementation.md("Built"+"Open"+"User test (windowed)");behavior=doc/erhe/shader_variants.md"Detail blend"+doc/editor/tools.md"Channel painting"+"Paint tool brush";©User-2026-11-22:"Make design and implementation plan and the implement it"->built-on-the-planner-proposed-choices(color_0-as-data|Shader_debug-grayscale|point-noise-only|brush-in-Paint_tool|noise-on-headset);gates-2026-11-22(Windows-headless-Debug,committed-config):erhe_math_tests-117+erhe_primitive_tests-102+erhe_scene_renderer_tests-117+editor_ik_solver_tests-61+detail_blend_verify-108/108+scene_roundtrip-490/490(exit-1-cleanup,older)+shader_fp16-4-of-5-stations(grazing-timeout,older)+ctest-L-editor-108(2nd-run;1st-cold-shader-cache);Shader_bool_mask=uint64;material-record-368/224B;Editor_settings_config-v23;workflow:opus-coder-per-step+orchestrator-diff-review(no-Fable-review);NOT-done:windowed-test+Mali/PowerVR-cost(compilers-not-installed)+Fill/Invert-buttons-and-no-Geometry-refusal-unchecked-by-script;NEXT=©User-windowed-test+rulings("For the user to rule on"-1-5)->push-on-©User-instruction}

[PROGRESS_ARCHIVE_2026-11-22]{moved-verbatim-from-progress.md}
[TASK::detail-blend-weight]{started-2026-11-22;branch=main(UNPUSHED);doc/plans/detail_blend_weight_implementation.md}
✓steps-1-5{material|channel-brush|shader+debug-mode|Paint_tool-Brush|gates;commits+gates-in-activeContext}
?next{©User-windowed-test("User test (windowed)")+rulings->push-on-©User-instruction}

[DETAIL_BLEND_2026-11-22_END]{supersedes-the-archive-line-above-where-they-differ}
@state::local-main-7-commits-ahead-of-origin/main(3b760af35);series=d3385eacd(squashed-build)+d4a451a14+0dd840391+2e77a2fad+ad9bb63c1+673dc8dde+memory-bank-commit;UNPUSHED→push-only-on-©User-instruction
@open::doc/plans/detail_blend_weight_implementation.md"Open"+"User test (windowed)"
!traps{Viewport_scene_view-has-two-enable_shared_from_this-bases→shared_from_this()-throws-bad_weak_ptr;find-the-view-through-Scene_views::get_viewport_windows()|Geometry-loaded-from-a-scene-file-has-no-connectivity-tables→©User:build-the-table(ensure_vertex_corners)¬scan|texture-graph-bakes=show_in_ui-off(©User-ruling:only-the-Graph-Texture-is-a-user-facing-item;Graph-Textures-scope-inside-Textures;older-scenes-not-migrated)|a-shader-source-change-empties-the-SPIR-V-cache→first-ctest-L-editor-run-times-out-in-Mcp_test.get_async_status_reports_background_work,second-passes|Mali/PowerVR-offline-compilers-not-installed-on-the-Windows-machine}
@user-scene::res/editor/scenes/Default-Scene.glb=©User-data(read-only-for-agents);holds-two-stray-root-level-"Graph Texture"-prims-from-an-earlier-save
