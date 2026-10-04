§MBEL:5.0

[FOCUS]
@audit-2026-09-30::DONE-c72a8cbb3{status-table-in-doc;doc/reference/audit_erhe_2026_09_30.md+6-slice-reports;read-only;options-ranked-in-sec-8;plan-deleted}
@audit-2026-09-30-medium-term::IN-PROGRESS-2026-10-04{set=10+18+12-include-diet;item-10-DONE{4fc17d988-per-subresource-Vk-layouts+1d5d45f28-blit-region-types+51dfc09fb-narrowing-fix};5ef90f13c-GL-depth-stencil-format-fix;each-commit-Fable-medium-review;next+followups->doc/plans/audit_2026_09_30_followups.md;GL-failures-ALL-FIXED-2026-10-04{state->memory-bank/topics/graphics_tests.md};item-18-DONE-glslang+Vulkan-2026-10-04{Spirv_cache-settings-hash+atomic;VkPipelineCache-persisted-per-identity;Metal-MTLBinaryArchive-left=needs-macOS};item-12-diet-DONE-2026-10-04{device.hpp-177k->75k-lines;texture.hpp-bound-by-item.hpp;split=clean-to-start};layout-follow-ups+hextiles-DONE-2026-10-04{3-defects-each-with-gpu-test};item-12-split-DONE-2026-10-04{5342524d6:erhe_graphics_interface-OBJECT-lib(neutral-headers+TUs,neutral-deps-only)+erhe_graphics(backend+pimpl-bridges,archives-interface-objects);consumers-link-erhe::graphics-unchanged;backend-include-in-interface-TU-fails-to-compile;rebuild-benefit-was-already-delivered-by-include-diet(measured:1/26-objects,0-consumer-TUs);b9eb1061b-null_device.hpp-Buffer-fwd-decl(API=none-build-broken-since-afbe3d5b5)};open:null-backend-editor-crashes-in-get_command_buffer-stub(doc/plans/audit_2026_09_30_followups.md);set-10+18+12-DONE-except-Metal-MTLBinaryArchive=needs-macOS}
@mesh-modeling::DONE-2026-10-01{plan-build-order-M0-M16-complete;26-commits;state->memory-bank/topics/mesh_modeling.md;remaining->doc/plans/mesh_modeling.md;user-interactive-checks-pending}
@agfx-test-port::DONE-2026-09-29{38->176-Vk-tests;8-engine-fixes;remaining->doc/plans/graphics_tests_agfx_port.md;state->memory-bank/topics/graphics_tests.md}+Metal+macOS-Vk-runs-2026-09-30{175/1-skip;165/11-skip;2-test-fixes}
@debug-line-aa::DONE-2026-09-29{doc/plans/debug_renderer_anti_aliasing.md;core+fringe-draws/pass(a81ebbfef;user-saw-joint-double-blend-on-hidden-sphere-silhouette->accepted-cost);on/off~1.7x;tests-13-Vk+GL;follow-up=joined-polylines+content-wide-lines+Metal}
@shadow-robustness::DONE-2026-09-29{future:Metal+G7-profiling;review-issue-1-fixed-2026-09-29(node-origin-vertex-bound,far_vertices-station);review-issue-3-checked-at-runtime;review-issues-2,4..11-open}{doc/plans/shadow_robustness.md;via-harness{user-asked};root-cause=head-on-tie{all-presets-cull_back+dz_dUV=0->zero-bias;1-ulp-ref-vs-stored};phases-0..7}
@radiance-cascades::PHASES-0-7-BUILT-2026-09-28{user-defaults-s0-1.5-q0-8}{doc/plans/radiance_cascades.md;via-harness{user-asked};world-space-3D-cascades->reduced-into-DDGI-probe-field-format{shared-consumer:heap-slots-5-7+Light_block-ddgi_*+USE_DDGI};Indirect_diffuse_source-enum{ambient|ddgi|radiance_cascades};test-scene=creation_24_gi_test_rooms{stations;no-Sponza};perf-budget=relative-to-measured-DDGI{iGPU;abs-numbers->memory-bank/local};phase-0{a:compute-Gpu_timer+DDGI-timings+get_indirect_diffuse_stats|b:creation_24|c:gi_verify.py+DDGI-baseline|d:DDGI-placement-fixes-if-sweep-finds}}
@rigging::PAUSED{state->memory-bank/topics/rigging.md[PAUSED_FOCUS_2026-09-27]}
@shadow-draw-list-culling::DONE-2026-10-04{item-9-shadow-half;gather_shadow_bounds+light_frustum_planes+Shadow_draw_statistics+Mesh::notify_primitive_bounds_changed(drag-bounds);verified:T7-10/10+Mcp-shadow-2/2+final-gate+live-cull-counts+mid-drag-aabb}
@item-11-editor-bits::DONE-2026-10-04{Item_flags/Item_type-application-ranges+register_*;editor_item_bits.hpp{Editor_item_flags+Editor_item_types+Editor_item_properties};hosted_selection->Selection;item_flags.hpp+item_type.hpp+profile_mutex.hpp;build_info-no-geogram;texture.cpp-pp-174294->174209-lines{split-is-not-a-diet:item.hpp-weight=property-system};scene_roundtrip_verify.py-3-pre-existing-failures-on-7c7c0c0bf{recorded-in-followups}}
@item-17-ci-hardening::DONE-2026-10-04{ERHE_USE_UBSAN+ERHE_WARNINGS_AS_ERRORS(default-ON;per-erhe-target)+Clang-ASan+UBSan-matrix-entry+erhe::version(stamped-each-build;first-startup-log-line);Clang/GCC-halves-verified-only-by-first-CI-run{no-clang-on-this-machine};open:lavapipe-GPU-tests+LSan}
NEXT=prompt_queue.txt{scene-roundtrip-failures-investigation->doc/plans/scene_roundtrip_failures.md{3-pre-existing-failures;DEVICE_LOST=draw-list-path-only;baseline-7c7c0c0bf-reproduces}}+after-that=new-pick-from-audit-section-8{open-parts->doc/plans/audit_2026_09_30_followups.md}+other-open{Metal-MTLBinaryArchive+Metal-parity-need-macOS;mesh-modeling-remaining:doc/plans/mesh_modeling.md;RC-remaining:doc/plans/radiance_cascades.md;shadow-future:Metal+G7}
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
graphics_tests::erhe_graphics_gpu_tests goldens (FLIP), agfx port state, per-backend results, traps
build::CMake conventions and dependency gotchas (all platforms)
windows::Windows-only build/clangd/VS-MCP/minidump facts
mesh_modeling::Blender-style mesh modeling tools and operations (DONE 2026-10-01; traps; remaining work pointer)
input_bindings::User-editable persistent input bindings: Commands overrides, Input Bindings window, input_bindings.json, MCP tools (DONE 2026-09-26)

[STATE]
@branch::main{user-pushes-themselves}
@unpushed::main-ahead-of-origin{many-commits-since-2026-09-18;user-pushes;git-push-only-on-explicit-instruction}

[BLOCKERS]
none
