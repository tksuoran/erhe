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
⚡render_scene_image-MCP{user-asked;offscreen-window-independent;parity-with-viewport;gi_verify-capture_view-switch;coder-running}
?phase-0d::DDGI-placement-fixes{pillar-faces+crawl-space}
?phase-0b..0d->phases-1..7
