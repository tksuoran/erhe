§MBEL:5.0
@WriteOnly{¬read,archive→archive/YYYY-MM-DD.md@EndOfDay}

[2026-06-19]
>built::Ninja-Windows-builds{vulkan-MSVC+clang-cl}✓©vulcan
>fixed::geogram-OpenNL-submodule-empty→submodule-update-force+reconfigure✓
>ran::editor.exe{Vulkan,MainLoop-frames-OK}✓
>installed::lsai-MCP+clangd{winget LLVM}+LLVM-user-PATH-persistent✓
>forked::tksuoran/erhe→LadislavSopko/erhe-PUBLIC✓
>committed::doc+ninja-vulkan-scripts→fork{acfac4bc,pushed}✓
>verified::.mcp.json-gitignored{machine-local,never-committed}✓
>wrote::doc/semantic_cpp_mcp_setup_xmp4_lsai.md{xmp4+lsai-setup-for-tksuoran}✓
>proved::clangd-VS-env-requirement{26→0-errors,INCLUDE}✓
>fieldtest::lsai-on-erhe{outline/info/source✓,project-wide✗-index-warmup}✓
>xmp4::navigated-simdjson-internals{dom_parser_implementation}✓
>diagnosed::lsai-jdtls-fixpoint{CheckStale:63,88832ms→172ms-via-stub,non-converging-fixpoint}©vulcan+lsai✓
>diagnosed::lsai-install-stale-dll-Windows{dotnet-host-lock,kill-PID+reinstall→1.0.180-on-disk}©lsai✓
>shipped::lsai-1.0.179{fixpoint-fix}+1.0.180{installer-hardening}@prod©lsai✓
>insight::lsai-must-parasite-build{compile_commands#0-occurrences}¬scan→issues#28/#29/#30/#31©vulcan+Ladislav+lsai
>replied::tksuoran-issue#10{vs-mcp-roslyn-no-cpp,lsai-fills-gap}✓
>initialized::memory-bank{MBEL5.0,6-core-files}©vulcan

[2026-06-29]
>FIXED::build_ninja_win_clang-editor.exe-builds-clean✓©Timo{5-commits-main}
  cc9e5fd5::tracy-pin-master-4cd6c389{=nullptr→(nullptr),clang-cl-deleted-atomic-copy-ctor<C++17;no-release>0.13.1-yet}
  0faceae9::mango-route-clang-cl→MSVC-flag-branch{else-set(CMAKE_CXX_FLAGS)-clobbered-/EHsc}
  faa6104e::Clang.cmake-g3-GNU-frontend-only{Jolt-Werror-fatal-on-clang-cl-unknown-arg}
  6c0fea37::Jolt-ENABLE_ALL_WARNINGS-OFF@clang-cl{/Wall=-Weverything+/WX→-Wpadded}
  ce656f05::Clang.cmake-global-avx2@clang-cl{Jolt-PUBLIC-avx2-vs-shared-erhe_pch,clang-PCH-feature-check-symmetric}
  d756c994::tracy-OPTIONS+"TRACY_ENABLE ON"{master-flipped-default-ON→OFF,ALL-builds;cl.exe-/W4/WX-on-editor.cpp-unused-name-outside-#if;clang-passed-only-on-stale-cached-ON}
  60d63927::profile.hpp-alias-4-more-gl*-for-TracyOpenGL{glGetError+glGetIntegerv+glGetString+glGetStringi;master-TracyOpenGL.hpp-probes-GL-context;OpenGL-backend-only-C3861}
>verified::editor.exe×4{clang-cl-ninja+cl.exe-ninja-vulkan+VS-vulkan+VS-opengl;all-0-error}✓
>learning::each-extra-build-config-exposed-a-tracy-pin-regression{cl.exe→TRACY_ENABLE-default-flip;OpenGL-backend→TracyOpenGL-new-gl-probes;clang-only-build_ninja_win_clang-masked-both}
>verified-also::VS-asan×2(vulkan+opengl)+headless-null+vulkan_headless✓{all-0-error,needed-no-new-fix}→8-local-build-configs-total-clean
>reviewed::upstream-mango-cmake{no-clang-cl-handling-but-never-clobbers-CMAKE_CXX_FLAGS}+jolt-native-flags{ENABLE_ALL_WARNINGS-knob}
>archived-from-activeContext::SkillKit-delivery-task©vulcan{Deliver-Cpp-Semantic-MCP+SkillKit→tksuoran,issue#10}
  pending-was{commit+push→LadislavSopko/erhe-fork|PR→tksuoran:main|comment-issue#10}{status-unknown,superseded-by-Timo-clang-cl-focus}

[2026-07-02]
✓geometry-nodes::phases1-5+MCP-tools+undo-redo+serialization{branch:geometry_nodes,713eb22d..d812547c,live-verified-headless-MCP,plan-doc-updated,phase6→prompt_queue.txt}
✓#240-selectable-scene::archived{done-2026-07-01,see-git}
✓session-tooling::mcp_call.py+erhe-headless-verify-skill+CLAUDE.md{cli-builds+testing+gotchas}+renderdoc-skill-verify-wording+.mcp.json-recreated
✓geometry-nodes-phase6::6a-incremental+6b-CoW+param-undo+MCP-set_parameter+spawn-grid+output-name{a11abd21..0881e107}
✓geometry-nodes-phase6-completion::output-physics+6d-instances+6e-groups+6c-design+CSG-fix+CC-quadratic-fix+smoke-sweep-65/65{ff414965..a2a36dd5}
✓catmull-clark-performance::harness+items-11-12-1-3-2{0171c8c4..650dc354,editor-x6-25.1→17.8s-Debug,Release-chain-689→570ms,doc/catmull_clark.md}

[2026-07-03]
✓smoke-coverage-extension::65→120-checks+2-real-fixes{cycle-acceptance-b553559b,empty-geometry-output-crash-4491835f,script-bdc71123}
✓doc-audit::52-files-reviewed→16-deleted+~16-refreshed{6106f4d0..5b1cc01c,doc-52→38}
✓editor-improvements-geometry-graph::pin-colors+pipeline-breadcrumbs+geogram-fork-resolution+ASYNC-EVAL-snapshot-isolation{d753e5d5..8f179479,sweep-120/120}

[2026-07-05]
✓PhaseC-graph-editor-dedup::C1..C7→src/editor/graph_editor/{5a211b01..f85e3f56,as-built-doc/graph_editor.md,shader-graph-left-as-is,C7-remainder+C8-deferred}
✓#252-independent-target::5-phases{7d80b0e8..3df49974,doc/252.md,explicit-weak_ptr-targets+pinned-Properties+Editor_windows-multi-instance+OpenEditor/OpenProperties}

[2026-07-07]
✓agent-tooling-setup::local-prereqs{dotnet10-runtime+LLVM-clangd-22}+compile-db{build_ninja_win_clang}+.clangd{CompileFlags-nesting-fix}+clangd--check-0-errors
✓ninja-wrappers-locate-VS-via-vswhere{was-hardcoded-Community-path}+setup-doc-.clangd-fix
✗LSAI+xmp4::now-opt-in-per-machine¬default{installer-reviewed-not-run;default-code-nav=Grep+VS-MCP+clangd--check,deps→.cpm_cache}
✓memory-bank-trim::progress.md-completed-tasks→history{20.7KB→747B}+activeContext/techContext-refresh{stale-branch-claims:geometry_nodes-actually-merged,no-crease-branch;stale-MCP-entries-superseded}

[2026-07-08]
✓machine-scope-rule::README.md{committed-files-machine-neutral:¬usernames/¬hostnames/¬user-paths/¬install-state/¬secrets;capabilities¬inventory;©public-identities-only}+memory-bank/local/{gitignored-per-machine-state}+scrub{activeContext/techContext/history-person-machine-attributions}

[2026-07-09]
>completed::uniform-scale-gizmo-handle{c162eb69}
  dormant-e_handle_scale_xyz→center-cube-mesh+materials+visibility{both-scale-modes}
  Scale_tool::update_uniform{screen-up-right-diagonal,s=2^(drive/gizmo_radius),deferred-baseline}
  +MCP-set_gizmo_visibility{headless-gizmo-activation}+c_str-scale_xz-typo-fix
  verified::headless-screenshots-both-modes✓;?user-drag-feel-verify-pending
>archived::agent-tooling-setup{2026-07-07}✓DONE{from-activeContext}
  local-prereqs{dotnet10+LLVM-clangd-22}✓;compile_commands+.clangd✓
  ninja-wrappers-vswhere-fix✓;clangd--check-0-errors✓
  policy::LSAI+xmp4-opt-in-per-machine;memory-bank-trimmed+Machine-Scope-Rule-added
>user-verified::uniform-scale-gizmo-drag-feel{2026-07-09,windowed}✓→task-fully-DONE{no-tuning-needed}

[2026-07-12]
>focus-switch::animation-editor{#243}→gltf-scene-roundtrip{plan-executed-via-prompt_queue-handoffs}
  animation-editor-state-at-switch::LW-keying{aa78d9ec}✓user-iterated;deferred→doc/animation-keyframing-plan.md{scene-markers+DopeTrack-key-drag-on-strip+standalone-Timeline-dock+autokey-persistence}
✓gltf-scene-roundtrip-phases0-4{doc/gltf-scene-roundtrip-plan.md-rev3}
  phase0::exporter-completeness;phase1::fastgltf-fork-generic-ERHE_*-passthrough{0c3bd202};phase2::ERHE_geometry
  phase3::ERHE_*-extensions{72ac5da9+f70143b5}::ERHE_node/camera/light/material{library}+ERHE_physics/scene/layout/brushes/node_graphs/collections{editor}+exclusion-hook+11-spec-pages+schemas
  phase4::save/open-switchover{3a4989b6}::save_scene_gltf+open_scene_gltf{parsers/gltf}+Load_scene_file_message-single-entry-branching+.glb-file-picker+MCP-save/load+get/set_scene_settings+per-scene-imgui.ini-REMOVED+physics-import-folded-carrier-node-removal{save/open/re-save-node-identical,was+6-nodes/cycle}
  verified-headless::save→open→re-save-identical{nodes+ERHE-payloads};settings/ambient/physics-applied-on-open;open-¬undoable;foreign-glb→Scene_open_operation;legacy-.erhescene-still-loads
✓gltf-scene-roundtrip-phase5{migration+removal}
  migrated::"Prefab test"+"pf2"{.erhescene→.glb,node-sets-verified-identical,bundle-dirs-deleted}
  deleted::scene_serialization.{hpp,cpp}+29-codegen-defs{gltf_source_reference+scene_settings-stay}+Asset_file_scene-handling+.erhescene-load-branch;Item_type::asset_file_scene-kept{renumber-risk}
  extras::writers-already-gone-since-phase3→readers-kept{old-files};stale-setExtrasWriteCallback-comment-fixed
  smoke-tests-ported→.glb{read_glb_json+wait_for_scene;checks→extensions.ERHE_node_graphs};geometry_nodes:130/130✓
✓race-fix::Scene::update_node_transforms-locks-m_host->item_host_mutex
  crash::2nd-glb-load→AV@Mesh::handle_node_transform_update{async-raytrace-kickoff-worker-rebuilt-m_rt_primitives-mid-iteration}
  diagnosed::minidump{python-minidump+llvm-symbolizer-stack-scan;live-VS-debugger-repro-failed{timing}}
✓geogram-wedge-fix{texture-suite-hang;user-attached-VS-debugger-to-hung-pid→real-stack}
  hang::main-thread-brush-preview-convex-hull{PDEL}+worker-geogram-threads→CellStatusArray-assert→geo_abort-getchar{Windows}=eternal-headless-wedge
  fix::make_convex_hull-PDEL→BDEL{sequential}+editor-ASSERT_THROW-explicit-sans-debugger{GEO_DEBUG-default-was-ASSERT_ABORT}
  upstream-draft::doc/geogram.md{concurrent-geogram+non-interactive-geo_abort}
  suites-green-post-fix::texture_graph-268/268{fresh-session-per-suite!}+geometry_nodes-130/130

[2026-07-12b]
>DONE::gltf-scene-roundtrip-plan-ALL-PHASES{phase6-verification-last}✓
>built::scripts/scene_roundtrip_verify.py{standing-harness:75/75;all-11-ERHE_*-coverage+schema-validate+reload-MCP-diff+Prefab-test-roundtrip+Khronos-validator+Blender-render}✓
>fixed::3-export-defects-found-by-validator{3394-errors->0}
  ef31a4bb::zero-NORMAL-dual-listing{present_*-mask-check}+soup-primitives-export-soup{TEXCOORD/JOINTS/WEIGHTS-restored,ERHE_geometry-only-authored}+mesh-names+settings-less-joint-export{empty-description}
  f9f11321::mcp-get_scene_nodes+parent_id+import_root
  90a8860a::harness+docs{scene_serialization-limitations:Brush_placement+static-mass;ERHE_geometry-dual-list-rule;plan-phase6-as-built}
>verified::geometry-smoke130/130+texture-smoke268/268{post-exporter-change,fresh-sessions}✓
>decided::no-erhe_gltf-unit-test{parse_gltf-needs-live-Device+executor+Image_transfer->harness-level-assertion}
>tooling::Khronos-validator-win64-downloadable{github-releases}+Blender-4.2-headless-import-render{BLENDER_WORKBENCH,factory-startup}

## 2026-07-13
>merge-save-scene+save-prefab::DONE✓{commit-b1eecef0,prompt_queue-ITEM2}
  one-save::always-full-editor-state;source_path-save-back-silent||default-dir+modal+writeback
  prefab-reload@save_scene_gltf{App_context&};open_scene_gltf-sets-source_path
  removed::File.SavePrefab+Operations::save_prefab+save_prefab_scene+MCP-save_prefab;MCP-save_scene-path-optional
  limitation-doc::prefab-templates-ignore-ERHE_*→graph-baked-products-missing-in-instances
>verified::prefab-edit-loop-18/18+scene_roundtrip_verify-73/73{validator-skipped:not-installed}

[2026-07-15::node-properties+houdini-graph-features::COMPLETE]
>node-properties-graph-selection{a2eeba7b+bc8c8377+c62ef868+6aa91d97+042d7c13+040e6f18}::NodeProperties-canvas-selection-all-editors+panel-param-edit+pin-edge-layout+node-Size{width/height-canvas-units}+preview-fit+interactive-edge/corner-resize+SDL-cursor-shapes-impl{was-empty-stub}
>houdini-graph-features{a7d98635+886e4c31+77feb2a8+11061763+a939c8e6+51c064fd}::wire-cutting{Y+drag,tessellated-hit-test}+display/ghost-flags+node-previews{N.V-headlight,hover-spin,zoom-sharp}
>user-verified::selection+pin-edges+resize{cursor-fix+cut-re-test-still-open→carried-in-activeContext-OPEN}

[2026-07-15::graph-editor-UX-sprint::COMPLETE{awaiting-interactive-verify}]
>e1637cc9::context-menu-node-at-click-pos
>b537d80b::preview-edge-line-overlay{solid-wireframe-pass;Preview_edge_lines_config×2-v14}
>db3a6b23::brush+scene_mesh-source-nodes+brush-drop-on-canvas
>ef39f4f9::multilink-merge-crash-root-fix+replace-on-connect+Pin::multi_link{smoke-130/130}
>ad71b6ca::resize-vs-drag-press-pos-hit-test
>cd164589::arcball-preview-rotation{quat}+Graph_node_previews_config-v15{on-by-default,global,persistent}

## 2026-07-15 (pm) graph-editor drag-drop + inventory session
§MBEL:5.0
>6-commits::all-on-main
- 6c4dc32e::imgui-node-editor-resize-shrink-fix{SizeAction-m_MinimumSize-latch-exact-equality-vs-ImCeil-quantized-adoption→tolerance-1.5cu+direction-check+trace-log}
- 9d3002ce::brush-drag-ghost-preview-on-geometry-canvas{AcceptBeforeDelivery+IsDelivery}
- ddc2a75f::palette-entries-draggable→canvas{ghost}+inventory/hotbar{Inventory_slot-v3:graph_node_kind/type/label;spawn_node_from_slot;find_window_by_kind;new-graph_node_drag_payload.hpp}
- d4ef5d1b::canvas-accepts-"Inventory_Slot"-payloads{graph-node+brush-slots;copy-semantics}
- 5cedd865::inventory-brush/material-slots-resolve-by-name-on-load{was-deferred-TODO→slot-mutated-to-bare-tool-across-restart+autosave-made-permanent;fix:all-scene-libraries+per-frame-retry+write_config-preserves-unresolved}
- 4fea814b::per-operator-Conway-node-types{conway_ambo..conway_gyro}+Conway-palette-group{c_operation_infos-single-source;legacy-"conway"-migrates-via-read_parameters;smoke+per-op-section→132/132}
verification::headless-boot-smoke{config-v3-loads}+full-geometry-smoke-132/132{fresh-headless-session,logging-trace-tweak-restored}+config-brush-slot-survives-session
carry-over::user-interactive-verify-list-in-activeContext
lesson::user's-live-editor{build_vs2026_vulkan}held-8080+shares-logs/config→always-identify-editor.exe-owner{ExecutablePath+CreationDate}before-headless-runs;user-must-rebuild-their-tree-to-pick-up-fixes

[2026-07-15-eve::scene-close-bug-class-session]
✓graph-mesh-raytrace-pick{c18b2608}::apply_baked_products-primitive-swap-on-registered-mesh→begin/end_mesh_rt_update-bracket+Item_flags::id
✓raycast-MCP-tool{c3ee16ce}::headless-raytrace-hit-query{pickable_static-default}
✓mcp-port-exclusive{a32dbbde}::Windows-SO_REUSEADDR-shadow-bind→SO_EXCLUSIVEADDRUSE{fallback-scan-now-works}
✓close-scene-clears-graph-targets{dd9022bc}::geometry+texture-windows{primaries+extras}
✓scene-close-defenses{37807545}::leak-watchdog{60-frames,"scene-close leak"-warnings}+App_scenes::is_host_registered+resolve_target-self-heal+CLAUDE.md-rule+systemPatterns-line
✓texture-node-Scene_root-cycle{ba23b612}::Texture_material_output_node+Texture_output_node-weak_ptr+resolve_scene_root{cycle-kept-whole-scene-alive}
✓audit{Explore-agent}::remaining-findings→prompt_queue-ITEM2{clipboard-copy-pins-originals+animation-player/window+hotbar/inventory-slots+brush/paint-tool-state+trivial-caches}
>handoff::prompt_queue-ITEM1{PLAN-asset-reference+asset-manager}
