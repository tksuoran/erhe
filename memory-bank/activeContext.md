§MBEL:5.0

[FOCUS]
@prepared-assets::IN-PROGRESS-2026-11-06{M1-M5+D8-steps-1-15-DONE;requirements=doc/plans/prepared_assets.md(R1-R14);design=doc/plans/prepared_assets_design.md(D1-D9);behavior=doc/editor/prepared_assets.md;NOW=©User-rulings-on-remaining-D8-coder-decisions(Scene-item-only-edits-not-marking-status-stale+load-key-uses-Export-LOD+what-is-not-loaded-from-items);RULED-2026-11-07/08:status-computes-on-request-only(84cfd0ed1)+R13.4-comparison-dropped+PVS/world-bake-invalidation-stays;git-stash-ERHE_pvs-column-rewrite=superseded(drop-when-©User-agrees);doc/plans/prepare_pvs_export.md=superseded-except-Established}
@session-2026-11-07/08::DONE-pushed{802820c69-PVS-scene-id-check-compares-stored-id+84cfd0ed1-prepared-status-on-request+6a3463923-MCP-render_scene_image-lod_nodes(LOD-levels-of-rendered-frame)+33e8660cd-world-space-form-casts-its-own-shadows(per-view-caster-filter;Fable-reviewed)+CI-green-chain(b0a6463e7-basis_universal-fork-tksuoran/basis_universal-branch-erhe_msvc_popcount;54f9d1c5c;dd5cee75b-SPIR-V-shader-tools-only-with-ERHE_SPIRV;f090bf11f;1dc3fa7b9-world-mesh-key-hexfloat-glibc-form-everywhere;e15ff8aca-GCC-ffp-contract-off);prepared_asset_verify.py-312/312(LOD-off-in-script;path-realpath)}
@mac-build-test-session-2026-11-07::DONE{pushed-to-origin/main-94139a08c;Metal-1886/1886+MoltenVK+KosmicKrisp-1889/1889+editor-97/97;fixes-in-commit-messages(b450f30e2+ac629f47c+e93284ef5+94139a08c);docs:doc/agents/macos.md(debugger-developer-mode-check+stale-SDK-recovery)+doc/plans/open_items.md;SPIRV-Tools-pin-ahead-of-glslang->CI-check-pending;+04682c6ce-Metal/Metal4-BC-copies-derive-tightly-packed-pitch(was-0->every-BC7/BC5-texture-garbled-on-Metal="blotchy-Mac-textures";Block_copy_test;Bistro-Vespa-Metal==MoltenVK-max-3/255)+c913db9e4-prompt_queue.txt-retired->doc/plans/open_items.md"Work-order"}
NEXT=doc/plans/open_items.md"Work-order"{committed;shared-by-all-machines;prompt_queue.txt-retired(AGENTS.md"Session-handoff")}
!open-items::doc/plans/open_items.md{defects-found-in-passing+unverified-paths+user-checks+user-decisions;add-there¬memory-bank}
>archived-2026-11-07::DONE|paused|user-gated-FOCUS-entries->topics/<topic>.md[FOCUS_ARCHIVE_2026-11-07](read-when-resuming-that-work)
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
quest::Quest 3 / Android: open items in priority order (baseline+headset verification, unculled headset passes, GPU profile items, headset prepare phase, warts), landed fixes, traps
metal::Metal backend on macOS: autorelease pools, render-thread threading, frame retirement, GPU fault diagnosis, macOS test status
pvs::Potentially visible sets (complete on desktop 2026-10-31): verify recipe + reference hashes, traps, step 8 headset + open items
lod::LOD: GPU LOD selection, LOD chains (ERHE_mesh_lod), Quest LOD results
culling::Draw-list culling phases 1-7, GPU classification/commands, raster occlusion culling, shadow draw-list culling
render_thread::Render thread + frame pipelining (M1/M2/M3/R phases), step history, gates, open user hand-offs
rendering::Radiance cascades / DDGI, shadow robustness, debug line anti-aliasing

[STATE]
@branch::main{in-sync-with-origin/main-at-e15ff8aca(2026-11-08);©User-2026-11-08:push-to-main-allowed-until-further-notice(never-force);local/*-branches=local-only(¬push)}
@branch::lod{Mac-worktree-../erhe-lod;REBASED-onto-main-2026-11-01(©User)+main-fast-forwarded+PUSHED-origin/main-a966e68ac;origin/lod=stale-pre-rebase-history(diverged;force-push-or-delete=user-decision);render-thread-worktree-../erhe-unchanged;-Werror-off-on-Clang/GCC/AppleClang(main-policy:MSVC-/WX-only);git-push-only-on-explicit-instruction}

[BLOCKERS]
none
