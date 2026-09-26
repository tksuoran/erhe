§MBEL:5.0
©erhe::Topic::tooling
@scope::Agent tooling: orchestration harness, doc layout, CI tests, skills location, Metal headless, MCP server policy
@docs::doc/agents/orchestration_harness.md+doc/README.md+doc/testing.md

[MCP_SERVERS]
!policy::lsai+xmp4=opt-in-per-machine¬default{2026-07-07;whether-installed→memory-bank/local/}
default-code-nav::Grep/Glob{erhe-naming-consistent}+VS-MCP{goto_definition/find_references-from-open-docs}+clangd--check{per-file-diagnostics}
third-party-deps::read-.cpm_cache-sources-directly{CPM-fetches-all-dep-sources-at-configure}
doc::doc/agents/semantic_cpp_mcp_setup_xmp4_lsai.md+doc/agents/lsai_usage_playbook.md{kept,machine-neutral,for-machines-that-opt-in}
!MCP-loads-at-session-start→restart-to-pick-up-changes

[STATE]
@agent-files-relocation::DONE-2026-09-21{4-commits-04e698b77+bb15cc631+0609df7e4+54f7d17af;UNPUSHED;project-skills=.claude/skills{Claude-Code-discovers-only-there;.agents/skills-was-invisible-since-bc2456830;.gitignore:.claude/*+!.claude/skills/};cppskills-x-*-command-kit+.agents/commands-REMOVED{unreachable+contradicted-AGENTS.md:Catch2,gdb,LSAI-first};mcp_server_usage.md->doc/agents/mcp_server_usage.md;audit-report->doc/reference/audit_erhe_2026_06_21.md;~/.agents/erhe_mcp_token=home-path-UNCHANGED;NEXT=prompt_queue.txt-item-1=skill-discovery-check-after-restart}
@doc-restructure::DONE-2026-09-18{doc/README.md=layout+header-line+writing-rules+index{regenerate-by-hand-or-script-when-files-change};doc/erhe/<name>.md=libraries+src/erhe-subsystems{erhe_<name>-target-minus-prefix}|doc/editor/=editor{editor.md+<subdir>.md+<feature>.md}|doc/agents/=AI-targeted{harness+MCP-guidelines+RenderDoc/survey-run-books+creations+LSAI-setup}|doc/<subject>.md=building+platforms-only|doc/<topic>/=frame_pacing|doc/plans/{+rigging,lightmap,geometry_graph}=future-work{Status:proposed|in progress|blocked}|doc/reference/=external{no-header};Stability:stable|mostly stable|experimental-line-in-first-10-lines;scripts/check_doc_links.py=refs+relative-links+notes.md-ban+header-check{0-problems-at-HEAD;run-after-any-doc-change};sweep=9-groups-via-harness{history-removed,plans-split-out,stale-claims-corrected-against-code};file-renames=snake_case+no--plan-suffix{code-cited-labels-D5/R3/C7-kept};LEFT=prompt_queue.txt-item-1=library-API-CHANGELOG{erhe::*-only,deferred-by-user}|plans/timeline_editor.md-body-predates-animation_window{reconcile-when-touched}}
@ci-tests::DONE-2026-09-15{build.yml:every-matrix-entry-configures-ERHE_BUILD_TESTS=ON+builds-erhe_tests+ctest---label-exclude-gpu|editor---output-junit{continue-on-error;build-badge=build-verdict};tests.yml=workflow_run-on-build-completed{downloads-test-results-*-artifacts;scripts/ci_test_summary.py;fails-on-non-success-build;README-tests-badge};labels:gpu=graphics/scene_renderer-gpu-tests,editor=mcp_server_tests+fixtures;all-wrappers-pass-through-args{Linux+xcode-got-"$@"};codegen-test-registered-with-ctest;LEFT=gpu-tests-under-lavapipe{doc/erhe/graphics_test_coverage.md};CI-run-itself-unobserved-until-user-pushes}
@metal-headless::doc/erhe/metal_headless.md{870efa949,2026-09-12;ERHE_WINDOW_LIBRARY=none-on-Metal=emulated-ring-of-3-offscreen-BGRA8Unorm_sRGB-textures-inside-Swapchain_impl{is_headless=no-CAMetalLayer;get_current_color_texture;mark_render_pass_recorded;capture_last_frame-synchronous-fresh-cb};scripts/configure_xcode_metal_headless.sh->build_xcode_metal_headless/;harness:1-opus-coder;verified:headless-launch+first-call-capture_screenshot-2304x1200-1233-colors+graphics-tests-64/64;windowed-built-only|?user-interactive{windowed-Metal-presents+capture_screenshot-still-armed}}
@harness::doc/agents/orchestration_harness.md{e2605f494,2026-09-05;©User-2026-09-12:scouts=haiku(lookup)|sonnet(read+summarize)-NEVER-opus{coder=opus};©User-asked-token-saving:orchestrator-writes-brief-per-commit→fresh-opus-coder-edits+builds+verifies+leaves-uncommitted→orchestrator-reviews-diff+commits;coders-strictly-sequential{shared-build-trees+MCP-port};fixes-via-SendMessage-same-agent;Explore-scouts-for-pre-brief-questions}

[PROGRESS]
[TASK::agent-files-relocation]{DONE-2026-09-21;4-commits;detail=activeContext;?verify-after-restart{prompt_queue.txt-item-1}}

[NOTES]
!headless-recipe::build_vs2026_vulkan_headless-editor→ERHE_AI_DRIVER=1-launch-hidden→mcp_call.py-b64-args{get_item_properties/set_item_property/get_addable_item_properties/undo;ids-reshuffle-per-launch;scene_name-required-for-create_node/select_items/get_node_details}
!default-scene::floor-mesh-sealed{lock_edit}→use-cube-attachment-for-attachment-tests
!scene-close-check::close_scene→wait≈6s→grep-"scene-close"{clean="all N released"}
!clangd-db::re-run-configure_ninja_win_clang.bat-after-adding-source-files{done-2026-09-04}

[TASK::metal-headless]{DONE-2026-09-12;870efa949;via-harness;1-coder}
✓emulated-ring-in-Swapchain_impl+SDL-guarded-surface+synchronous-headless-readback+configure_xcode_metal_headless.sh+doc/erhe/metal_headless.md
?user-interactive{windowed-Metal-regression:present+armed-capture}

[TASK::ci-tests]{DONE-2026-09-15}
erhe_tests-aggregate-target+gpu/editor-labels+wrapper-arg-pass-through+build.yml-ctest-step+tests.yml-verdict-workflow+ci_test_summary.py+README-tests-badge+AGENTS/building/graphics_test_coverage-docs
verified-local:build_vs2026_vulkan-reconfigure+erhe_tests-build-clean+ctest--LE-gpu|editor-1371-tests{gpu-80,editor-53-excluded}
?first-CI-run-after-push{Linux/macOS/Windows-headless-test-builds-never-built-here}

[TASK::doc-restructure]{DONE-2026-09-18}
✓checker+moves+notes-migration+headers+index{5-commits}
✓content-sweep{9-group-commits;~190-docs;18-docs-folded/deleted;~45-plans-created}
?changelog=prompt_queue.txt-item-1{erhe::*-API-only}
!ai-runs-config-read-only::ERHE_AI_DRIVER=1->main()-sets-erhe::codegen::Config_persistence::read_only{save_config-no-op+Imgui_host-ini-read-never-written}->config/-untouched-by-agent-runs+MCP-tests{d6a053272;doc/agents/editor_runs.md}
!trap::user-ini-desktop_window_imgui_host_imgui.ini-holds-AI-written-Viewport_window-3..9{2026-09-25;slot-7-floating}->agent-runs-reaching-those-slots-restore-them;user-owned-file-not-touched
!changelog::CHANGELOG.md-root{erhe::*-public-API-only;rule-doc/README.md-Changelog}
