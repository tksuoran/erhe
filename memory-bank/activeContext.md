§MBEL:5.0

[FOCUS]
@prepared-assets::IN-PROGRESS-2026-11-06{M1-M5+D8-steps-1-15-DONE-2026-11-07;requirements=doc/plans/prepared_assets.md(R1-R14);design+per-step-state+open=doc/plans/prepared_assets_design.md(D1-D9);behavior=doc/editor/prepared_assets.md;NOW=Quest-testing(progress.md[TASK::prepared-assets]);FOUND:PVS-item-empty-scene-id(fix-at-Export-bake);NEXT=R13.4-Quest-remeasure(memory-gap-14MB)->PVS-scene-id-fix->©User-rulings-on-coder-decisions(design-doc-steps-12/14/15)->other-tree-builds(GL/Metal/Windows/Android);git-stash-ERHE_pvs-column-rewrite=superseded(drop-when-©User-agrees);doc/plans/prepare_pvs_export.md=superseded-except-Established}
@mac-build-test-session-2026-11-07::DONE{pushed-to-origin/main-94139a08c;Metal-1886/1886+MoltenVK+KosmicKrisp-1889/1889+editor-97/97;fixes-in-commit-messages(b450f30e2+ac629f47c+e93284ef5+94139a08c);docs:doc/agents/macos.md(debugger-developer-mode-check+stale-SDK-recovery)+doc/plans/open_items.md;SPIRV-Tools-pin-ahead-of-glslang->CI-check-pending}
NEXT=prompt_queue.txt{UNTRACKED-per-machine;holds-the-ordered-outstanding-items}
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
@branch::main{ahead-of-origin/main-unpushed(9-commits-up-to-37fdc1d66+memory-bank-commit);history-rewritten-locally-2026-11-06(©User;one-subsystem-split-out;details->memory-bank/local/context.md[HISTORY_SPLIT_2026_11_06]);local/*-branches=local-only(¬push);uncommitted:config/editor/editor_settings.json+desktop_windows.json(user-interactive-session-saves;user-decides)}
@branch::lod{Mac-worktree-../erhe-lod;REBASED-onto-main-2026-11-01(©User)+main-fast-forwarded+PUSHED-origin/main-a966e68ac;origin/lod=stale-pre-rebase-history(diverged;force-push-or-delete=user-decision);render-thread-worktree-../erhe-unchanged;-Werror-off-on-Clang/GCC/AppleClang(main-policy:MSVC-/WX-only);git-push-only-on-explicit-instruction}

[BLOCKERS]
none
