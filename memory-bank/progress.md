§MBEL:5.0

@rule::active-tasks-only;DONE-task-sections-move-to-memory-bank/topics/<topic>.md[PROGRESS]

[TASK::rigging-phase-2]{started-2026-09-18;via-harness}
✓pole-target-slice{484bac408+67aafe503+c59ba15b3+08a7a73e0;ik_pole_verify.py-all-pass;solver-tests-22;roundtrip-418/421}
✓effector-orientation{ebf606ff2;verify-5/5}+chain-visualization{edaf10d98;solver-tests-26;visual-check-interactive-only}
?left::stiffness+Phase-1-feel-questions{need-user-hands-on}->Phase-3
?user-interactive-deferred{ik_settings-slice;pole-picker+angle-rows;live-drag-with-pole;Move-tool-Effector-Orientation-combo;chain/root/pole-visualization-during-drag}
⚡interactive-pass{doc/plans/rigging/interactive_test_pass.md;0-8-AUTOMATED-2026-09-25{scripts/ik_interactive_pass_verify.py;65/66-pass};decisions->options{ik_drag_options.md-section-3;50aa400cb+a9078a35c+ab2c9edb1;pass-70/71};F7-fixed-546a8cb75;pass-71/71;left=stiffness->Phase-3}

[TASK::radiance-cascades]{started-2026-09-27;via-harness;doc/plans/radiance_cascades.md}
✓phase-0a::66c7e3233{Gpu_timer(Device&)+Scoped_gpu_timer;DDGI-4-pass-timings;get_indirect_diffuse_stats;default-scene-DDGI~0.3ms/update~8-9ms/Mray;GL-timer-not-compiled(no-GL-tree);Metal-timer-stub-reads-0}
✓phase-0b::a7c1beb62{creation_24_gi_test_rooms;7-stations-own-scene;viewport-fraction-rects-raycast-checked;DDGI-findings:pillar-faces-black(probe-in-pillar)+crawl-space-black+corridor-probes-outside-walls+weak-cornell-bleed~1.05}
✓phase-0c1::a87a7a909{sample_indirect_diffuse;ddgi_sample.comp;deferred-MCP-readback;cap-4096;Ddgi_renderer::get_forward_parameters-single-source}+315626c1f{Vulkan-Buffer_impl::invalidate-skips-coherent}
✓phase-0c2::d7b74666c{gi_verify.py;baseline-in-plan-section-10;DDGI-fails:pillar-faces-min/median-0.22(<0.25)+crawl-floor-near-black-0.0004;corridor/crawl-gates-relative-to-DDGI;timings->memory-bank/local/gi.md}
✓render_scene_image::be54a899f{Scene_image_capture-chain-shadow->Scene_image_view->post->readback;scene_only-skips-editor_aid;png|pfm;batch-refuses;expired-request-releases-chain;Scene_view::get_camera_viewport}+ba245bae7{gi_verify-uses-it}
✓phase-0d1::5787b3bba{reference_indirect_diffuse;erhe_ddgi_ray.glsl-shared;gate-12-Accuracy;crawl-space-dark-in-reference(scene-not-defect);placement-gate->accuracy}
✓phase-0d2::af58868d6{relocate-out-through-nearest-backface;probe-rays-t_min-0}+00f6ed920{backface-weight-from-unbiased-surface-point}+477756efc{probe_states-stats}+8f7dd9857{docs;post-fix-baseline};remaining-DDGI-error=discretization{spacing-sweep-evidence};no-relax-oscillation;history-reset-no-effect(reverted);known:offset-0.0-leak-0.0026(within-gate)+wall-plane-probe-side-random
✓PHASE-0-DONE
✓phase-1::6fa96b9cb{set_indirect_diffuse_source-single-change-site;settings-v5-migration;probe_grid+content_bounds-shared;radiance_cascades_layout-12-tests;RC-skeleton-atlases}+dbf47588d{gi_verify-set_indirect_diffuse;docs}
✓phase-2::4d51cd708{rc_trace.comp;ddgi_trace_ray_segment;R32F-c0-distance;rc_texel_verify.py-334k-texels-0-fail;RC-trace~0.7ms/Mray-vs-DDGI~6;top-cascade-beta1-in-small-rooms(expected)}
✓phase-3::cd0ac88b8{rc_merge.comp;exact-algebra-0-fail;approx-error-median-0..0.13-p90-0.43..0.86;mean-bias-11..34%-dark;leak_pair-B-picks-A-light-through-wall;mask-decomposition-exact}
✓phase-3b::92faf6288{merge_mode:interpolate(default)|visibility_masked;rc_visibility.comp-on-change;leak-0.0033->0.0025;cornell-bias-17->21%;premise-half-wrong:coarse-probes-outside-carry-far-field;remaining-leak=start-point-parallax->per_neighbour_trace-now-phase-5}
✓phase-4::89bbfe823{rc_reduce-exact-0.088%;Probe_field-published-one-place;shared-atlas-tile-wrap;RC-fails-leak(0.039/0.017)+far-field+accuracy(16-30%-dark)+cost(~2xDDGI;reduce-largest);passes-bounce+noise+door-convergence}
✓phase-4b::e47fcc8c7{upper-grids-drifted-half-spacing-per-odd-count(radiance_cascades_layout.cpp:get_upper_grid);centred;odd-weights-1/0+0.5/0.5;corridor-c4-probes-back-on-centre-line}
✓phase-5::b876b287d{per_neighbour_trace-default;leak_pair-B-leak-0;cornell-bias-2%;worst-group-1.40-vs-DDGI-0.65;cost-2.8-4.5xDDGI;gates-2,6,9,12-fail-all-modes;offset-0.5-leak=pre-averaging(32-ray-variant-not-built);corridor-c3-probes-outside-walls}
⚡phase-6::A-open-accuracy-causes+B-jitter+C-change-driven-reset(both)+D-multi_bounce+E-defaults-vs-budget{coder-running}
?follow-up::change-driven-refit-both-producers{doc/plans/ddgi.md}
?phase-0b..0d->phases-1..7
