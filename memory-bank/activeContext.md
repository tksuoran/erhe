§MBEL:5.0

[FOCUS]
@radiance-cascades::IN-PROGRESS-2026-09-27{doc/plans/radiance_cascades.md;via-harness{user-asked};world-space-3D-cascades->reduced-into-DDGI-probe-field-format{shared-consumer:heap-slots-5-7+Light_block-ddgi_*+USE_DDGI};Indirect_diffuse_source-enum{ambient|ddgi|radiance_cascades};test-scene=creation_24_gi_test_rooms{stations;no-Sponza};perf-budget=relative-to-measured-DDGI{iGPU;abs-numbers->memory-bank/local};phase-0{a:compute-Gpu_timer+DDGI-timings+get_indirect_diffuse_stats|b:creation_24|c:gi_verify.py+DDGI-baseline|d:DDGI-placement-fixes-if-sweep-finds}}
@rigging::PAUSED{state->memory-bank/topics/rigging.md[PAUSED_FOCUS_2026-09-27]}
NEXT=RC-phase-3b-visibility-merge{coder-running}->phase-4-reduce+render->0c2-gi_verify+baseline->0d-DDGI-placement-fixes
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
build::CMake conventions and dependency gotchas (all platforms)
windows::Windows-only build/clangd/VS-MCP/minidump facts
input_bindings::User-editable persistent input bindings: Commands overrides, Input Bindings window, input_bindings.json, MCP tools (DONE 2026-09-26)

[STATE]
@branch::main{user-pushes-themselves}
@unpushed::main-ahead-of-origin{many-commits-since-2026-09-18;user-pushes;git-push-only-on-explicit-instruction}

[BLOCKERS]
none
