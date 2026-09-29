§MBEL:5.0

[FOCUS]
@agfx-test-port::DONE-2026-09-29{38->176-Vk-tests;8-engine-fixes;remaining->doc/plans/graphics_tests_agfx_port.md;state->memory-bank/topics/graphics_tests.md}
@debug-line-aa::DONE-2026-09-29{doc/plans/debug_renderer_anti_aliasing.md;core+fringe-draws/pass(a81ebbfef;user-saw-joint-double-blend-on-hidden-sphere-silhouette->accepted-cost);on/off~1.7x;tests-13-Vk+GL;follow-up=joined-polylines+content-wide-lines+Metal}
@shadow-robustness::DONE-2026-09-29{future:Metal+G7-profiling;review-issue-1-fixed-2026-09-29(node-origin-vertex-bound,far_vertices-station);review-issue-3-checked-at-runtime;review-issues-2,4..11-open}{doc/plans/shadow_robustness.md;via-harness{user-asked};root-cause=head-on-tie{all-presets-cull_back+dz_dUV=0->zero-bias;1-ulp-ref-vs-stored};phases-0..7}
@radiance-cascades::PHASES-0-7-BUILT-2026-09-28{user-defaults-s0-1.5-q0-8}{doc/plans/radiance_cascades.md;via-harness{user-asked};world-space-3D-cascades->reduced-into-DDGI-probe-field-format{shared-consumer:heap-slots-5-7+Light_block-ddgi_*+USE_DDGI};Indirect_diffuse_source-enum{ambient|ddgi|radiance_cascades};test-scene=creation_24_gi_test_rooms{stations;no-Sponza};perf-budget=relative-to-measured-DDGI{iGPU;abs-numbers->memory-bank/local};phase-0{a:compute-Gpu_timer+DDGI-timings+get_indirect_diffuse_stats|b:creation_24|c:gi_verify.py+DDGI-baseline|d:DDGI-placement-fixes-if-sweep-finds}}
@rigging::PAUSED{state->memory-bank/topics/rigging.md[PAUSED_FOCUS_2026-09-27]}
NEXT=await-user-direction{agfx-port-open-findings:Vk-per-subresource-layout-tracking;AMD-GL-comparison-sampler}{shadow-future:Metal+G7-optimization;RC-remaining-work};RC-remaining-work{doc/plans/radiance_cascades.md;await-user-direction}->0c2-gi_verify+baseline->0d-DDGI-placement-fixes
[TOPICS]{memory-bank/topics/<name>.md;¬auto-loaded;read-the-ones-matching-the-session;DONE-work+traps+open-items-per-topic}
usd::USD compatibility: LightUSD fork, import/export, composition arcs, DrawModes, USD physics/animation, WG asset survey
property_system::erhe::property dependency properties: node values, styles, folders, migrations, Properties window
rigging::IK / skinning / rigging tools (paused; resume from [PAUSED_FOCUS_2026-09-27])
physics::Jolt/Box3D physics, jointed-body drags, convex hulls
viewport_ui::Viewports, cameras, gizmos, grids, four view, ImGui docking, active item, asset browser, MCP UI driving
performance::Startup profiling (Tracy), deferred brush geometry, frame-time work
tooling::Agent tooling: orchestration harness, doc layout, CI tests, skills location, Metal headless, MCP server policy
scenes_and_assets::Scene persistence (glTF), glTF uids, asset manager, inventory slots
editor::Editor object lifetimes: part construction, scene-close + undo-removal reference rules, retention traps
geometry::Geometry / geogram threading, geometry graph payload and pin rules
graphics_tests::erhe_graphics_gpu_tests goldens (FLIP), agfx port state, traps (DONE 2026-09-29)
build::CMake conventions and dependency gotchas (all platforms)
windows::Windows-only build/clangd/VS-MCP/minidump facts
input_bindings::User-editable persistent input bindings: Commands overrides, Input Bindings window, input_bindings.json, MCP tools (DONE 2026-09-26)

[STATE]
@branch::main{user-pushes-themselves}
@unpushed::main-ahead-of-origin{many-commits-since-2026-09-18;user-pushes;git-push-only-on-explicit-instruction}

[BLOCKERS]
none
