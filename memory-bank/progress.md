§MBEL:5.0

[TASK::PhaseC-graph-editor-dedup-2026-07-05]
@status::✓CORE-DONE{7-extractions-C1..C7,branch-crease;as-built-ref-doc/graph_editor.md{plan-doc/graph-editor-shared-plan.md-removed→git-history}}
@goal::de-dup-3-erhe::graph-consumers{shader-graph|geometry-graph+Graph_mesh|texture-graph+Graph_texture}→shared-src/editor/graph_editor/
>commits::5a211b01..f85e3f56
5a211b01::C1-shared-canvas-steppers{graph_editor_widgets.*;removed-ODR-rename-hack:texture_index/enum_stepper-were-renamed-copies-of-imgui_*;13-call-sites-repointed}
47ea0763::C2-Graph_editor_window_base{Imgui_window-subclass,pure-virtual-controls_imgui}+one-Graph_editor_palette_window{replaced-2-byte-identical-palette-windows}
f8c7b5a1::C3-Graph_asset<Self,GraphT,NodeT>-CRTP-base{over-erhe::Item<...,not_clonable>;m_graph+m_nodes+ctors+accessors;Graph_texture-keeps-Texture_reference-pull,Graph_mesh-keeps-baked-products-push}
1cf42f39::C4-graph_serialization.hpp-templates{write/read_graph_asset_json;read<Asset,GraphT,Node,make-fn,set-owning-lambda>;GraphT-needed-bc-mark_dirty-on-derived-not-erhe::graph::Graph;wrappers-thin}
c985c352::C5-graph_operations.hpp-undo-op-templates{over-Traits{Window,Asset,Node,label};per-editor-headers=traits+using-aliases;deleted-geometry+texture-operations.cpp}
ebf69fc6::C6-Graph_editor_node-payload-agnostic-node-base{NON-template;owns-node_editor/show_pins/dirty/factory-name/param-plumbing;3-hooks:pin_key_color+commit_parameter_operation+after_node_content{tex=draw_preview};mark_dirty-virtual{tex-also-sets-m_preview_needs_render};concrete-node-names-unchanged→~34-node-files-untouched}
f85e3f56::C7-shared-node-palette-in-Graph_editor_window_base{Palette_entry/Category+filter/categories+node_palette();hooks-build_palette()+add_node_from_palette()}
!shader-graph-src/editor/graph/-LEFT-AS-IS{degenerate-predecessor:no-dirty/params/undo/factory/asset,fixed-struct-payload,global-selection-sync}
DEFERRED-optional{doc-recorded}::C7-remainder{window-canvas-render-loop+link-create/delete+node-positions+m_node_editor→base;C6-made-node-iteration-payload-blind-via-Graph_editor_node-cast;hooks-needed=graph()->Graph*+connect/disconnect/remove_node;HIGHER-RISK-interactive-per-frame-path-next-to-async-engine+#252-target-model}|C8{~9-twin-MCP-tool-bodies-mcp_server_graphs.cpp+2-scene_root-Create-branches+parallel-scene-save/load-blocks}
VERIFY::each-step-ninja-vulkan+headless-build+BOTH-sweeps{geom-129/129+tex-266/266,FRESH-editor-per-sweep}+C7-palette-screenshot-read{Texture-Graph-Palette-renders:filter-box+Clear+category-header}
!verify-gotcha::run-each-sweep-in-FRESH-editor{same-process-geom-then-tex=texture-bake-pollution-~11-has_output:false}|get_server_info-"build"-timestamp-lags{__DATE__-TU-compile-time-not-link-time→trust-pid}

[TASK::#252-independent-target-2026-07-05]
@status::✓DONE{5-phases,branch-crease;doc/252.md-Implementation-Status-section}
>commits::7d80b0e8..3df49974
7d80b0e8::phase1{graph-windows-explicit-weak_ptr-target{set_target/get_target,resolve_target-replaces-selection-scan}+target-selector-row{item_reference_imgui}+node<->global-selection-sync-REMOVED{geometry_graph_node+texture_graph_node,NOT-shader}+create-menu+create_graph_*-MCP→set_target+set_geometry/texture_graph_target-MCP-tools;smoke-migrated}
16cd28c5::phase2{Properties-weak_ptr<Item_base>-target+Pin-selector-row+effective_items()→{target}-when-pinned-else-selection-fallback}
177c913f::phase3{Editor_windows-part{src/editor/windows/editor_windows.*}-owns-extra-instances{unique-title,empty-ini_label,queue()-deferred-creation,prune-closed-per-frame};window-ctors-gained-title/ini_label;open_*_window-MCP-tools}
3df49974::phase4{OpenEditor{graph-assets+scenes}/OpenProperties{any-item}-context-menu+double-click→Editor_windows::open_editor_for_item/open_properties_for_item;Scene_views::open_new_viewport_scene_view_node(scene_root);reuse-primary-if-untargeted-else-new}
VERIFY::geometry-129/129{graph_editor-trace}+texture-266/266{fresh-editors,back-to-back-1-editor=texture-bake-pollution}+core-acceptance{selection-empty-after-building-graph,remove_node-keeps-asset-1→1,nodes-2→1}+2-geometry-windows-on-2-assets+Properties-pinned-A-while-selection-B-screenshot
!deviation::shader-graph-shader_graph_node.cpp-sync-LEFT-INTACT{feeds-Node_properties_window-which-reads-selection,shader-nodes-not-content-library-assets→no-asset-delete-bug;removing-would-regress-no-fix}
!behavior-change::selecting-graph-asset-in-hierarchy-no-longer-repoints-editor{use-OpenEditor/selector/set_*_graph_target};node-params-no-longer-in-Properties-for-geom/tex-graphs{edited-in-node-on-canvas}
!verify-limit::mouse-triggers{menu,double-click}+literal-canvas-Delete=UI-inspection-verified{reuse-MCP-verified-open-machinery};fix-verified-at-mechanism-level

[TASK::doc-audit-2026-07-03]
@status::✓DONE{future_prompt_2.txt-handoff-executed+deleted;52-doc-files-reviewed-via-7-parallel-read-only-agents{per-topic-groups};doc/-52→38}
>commits::6106f4d0..5b1cc01c
6106f4d0::dangling-refs{recover-glslang_bug_report_debugglobalvariable+debug_renderer_multiview-from-92c14dc5{never-merged-branch,content-matches-current-code,in-flight-banner→settled};hud_hotbar_depth_test_plan+work.md-never-committed→xr_session/xr_instance-comments-self-describing{quad-depth-test-gate-spelled-inline}+prewarm.hpp/md-drop-item-F-pointer;editor-builds-clean}
bd90adec::relic-deletions{imgui.md+editor_classes.drawio-0-bytes|bindings.drawio-2022|msys2_clang.md-unsupported-mingw|notes.md→occlusion-culling-idea-moved-to-todo.md}
10a8bab0::completed-plan-deletions{vulkan_openxr|command_buffer_update{false-Metal-stub-claim}|sky{root-cause→procedural_sky.md}|content_wide_line-fallback-plan{shipped-differently}|KHR_physics_regid_bodies-spec-copy{support-doc→upstream-URL}|circular_ring_buffer-extraction|scene_root_cleanup{deferred-steps-4/5→editor_improvements-items-7/8}|tool_improvements{5c/5d+narrow-Command-deps→editor_improvements}|graphics_compute_testing_plan{M6-CI-sketch→graphics_test_coverage.md}|hands-on+build_without_visual_studio_and_lsai{LSAI-cluster→keep-setup+playbook-pair}}
fbf62304::stale-claim-refresh{vulkan_backend+emulated-swapchain-section|metal_backend+6-file-pairs+real-Command_buffer|point_light_shadows-y-flip-note|forge-erhe-storage-image-GL/Metal-wired|building.md-option-table+ninja/headless-pointers|CLAUDE.md-defaults{NAVIGATION/PROFILE/XR=none}|quest.md-symbol-anchored-refs|editor_rendering-20-pass-table+SOLID_WIREFRAME|shader_variants|mesh_memory-6-formats|layout-header|khr-physics-fork-pin-wording|claude_review-#2/#11→fixed|settings-reference-#239-landed+Scene_file-v5|command_script-scene.create/load_scene+real-execution-model}
5b1cc01c::skills{quest-validation+shader-failure:editor.cpp-698/702→device_message-lambda-search-anchor;glslang-PATCHES-wording→fork-pin}
!verdict-guide::completed-plan-fully-in-code/history=DELETE;code/skill/CLAUDE-referenced=KEEP+UPDATE;overlap→MERGE-into-canonical
KEPT-AS-IS::shadows|shadow_improve{2-steps-open}|android|intermittent_main_loop_hang{~20-code-refs}|renderdoc_fork|opengl41_compatibility|multiview|post_processing|init_status_display_phase_ii{parallel-init-still-off}|profiling-2026-05-01{unactioned-findings,delete-once-mip/sampler-fix-lands}|todo|catmull_clark|mesh_component_selection|geometry-nodes-plan{active}

[TASK::editor-improvements-geometry-graph-2026-07-03]
@status::✓DONE{doc/editor_improvements.md-geometry-graph-items-8/7/9/6-all-closed→moved-to-Past-work}
>commits::d753e5d5..8f179479
d753e5d5::item8-pin-colors{pin_key_color()-per-Geometry_pin_key-fill-in-show_pins;geometry-teal|float-grey|int-green|bool-pink|vec3-indigo|vec4-yellow|mat4-steel-blue|material-orange|points-cyan|instances-spring-green;verified-headless-screenshot{enlarge-Geometry-Graph-window-via-imgui-ini-patch-pre-launch}}
de8a5740::item7-breadcrumbs{Scoped_phase_timer-ctor-sets-set_breadcrumb(phase)→covers-all-cc-phases+interpolate/sanitize/process;Geometry::process-adds-facet_centroids/facet_texcoords/tangents;copy_with_transform;Primitive_raytrace-buffer-mesh-build{with-counts}+both-BVH-commit-sites;84/84-gtests}
b262c367::item9-resolved-at-root{geogram-fork-pin-daf9e192-fixed-create_sub_elements-growth→per-element-create-amortized-O(1);conway-batching=constant-factor-only=diminishing-returns;doc-records-resolution,no-code-churn}
2e8a2225::item6-ASYNC-EVAL{snapshot-isolation:update_evaluation()-per-frame-from-editor-tick{after-operation_stack->update}→shadow-clone{factory+write/read_parameters-JSON+links+cached-payloads+dirty-flags}→tf::Executor-silent_async-evaluates-shadow-only→finish-copies-payloads-back-by-live-id+applies-output-scene-products;output-node-2-phase{evaluate=worker:copy+process+primitive+raytrace+convex-hull;apply_evaluated_to_scene=main:scene-node/mesh/physics};Group_node::adopt_subgraph_outputs{shadow/live-subgraph-pairing-by-index,path+count-guard,depth-limit};shadow-logs-live-ids{set_log_source_id}→smoke-incremental-id-matching-works;ops-drop-inline-evaluate_if_dirty;MCP:mutations-return-immediately+get_geometry_graph/save=wait_for_idle_evaluation-barrier+get_async_status-reports-via-shared-counters;smoke-test-scene_nodes()-calls-get_graph()-barrier-first}
8f179479::shadow-list-parallelism{factory-failure-pushes-null-placeholder,¬index-shift-mis-wiring}
VERIFY::sweep-120/120✓×2+async-proof{set_param-x6-returns-0.13s{was~20s-block},19-cheap-queries-answered-during-eval,get_async_status-running>0,final-counts-98306/98304✓,screenshot-rendered-x6-box}
!bug-found-during-verify::fresh-Geometry_graph-born-forced-full{m_dirty{true}}→shadow-graph-must-consume_forced_full-before-forwarding-live-flag,else-every-run-full-eval{incremental-silently-lost;caught-by-smoke-incremental-section}
!semantics::scene-state-lags-graph-mutations-until-run-finishes→scene-queries-need-get_geometry_graph-barrier{eventual-consistency-is-the-new-MCP-contract}

[TASK::smoke-coverage-extension-2026-07-03]
@status::✓DONE{future_prompt_1.txt-handoff-executed;65→120-checks;2-real-defects-found+fixed}
>commits::b553559b..bdc71123
b553559b::cycle-fix{baseline-proved-live:MCP-accepted-self+2-node-cycle→"not acyclic"-~125-err/s+perpetual-per-frame-re-eval;fix=erhe::graph::Graph::would_create_cycle{DFS-sink→source-reachability}+connect-refuses+Geometry_graph_window::connect-pre-validates{¬noop-undo-entry}+ax-gesture-pre-query+load_graph-rejects-cycle{Kahn-over-indices}+key-mismatch-files+Group_node-load-checks-connect-result}
4491835f::empty-geometry-crash{param-abuse-section-found:conway-op-42→empty-geometry→output-node→ERHE_VERIFY(total_index_count>0)-abort{Build_context_root::allocate_index_buffer};stack-via-crash-handler-cpptrace-stderr{¬VS-MCP,¬cdb-this-session};fix=output-evaluate-treats-0-facet-source-like-disconnected;VERIFY-kept}
bdc71123::script{+5-sections:multilink-partial-disconnect{join-3-input+instance-points+realize-pins,exact-count-shrink+undo/redo}|invalid-connect{mismatch/self/2-3-cycle→error+no-link+no-undo-entry+settles}|serialization-errors{8-malformed-file-loads-unchanged-graph+save-to-dir+group-bad-path/no-output/nested-2-deep/self-ref-depth-guard}|param-abuse{subdiv=-5/99-clamp,distribute=-10,box-size-0/neg,conway/boolean/math-op-OOR,instance-scale-0}|output-physics{attachment-follows-connect-state+3-motion-modes+duplicate-names}+screenshots{smoke_nodes+smoke_stress+final}}
VERIFY::sweep-120/120✓{one-session,x6-17.9s-Debug-unchanged}+both-builds-green{headless+ninja-msvc}+screenshots-read
!repro-trick::headless-editor-launch-with-RedirectStandardError→crash-handler-prints-cpptrace-callstack+minidump-path-to-stderr{debugger-free-crash-diagnosis}

[TASK::catmull-clark-performance-2026-07-02]
@status::✓DONE{harness+items-11→12→1→3→2;stopped-at-diminishing-returns-per-plan}
>commits::0171c8c4..650dc354
0171c8c4::harness{Operation_timing+Scoped_phase_timer{thread-local-collector,inert-uninstalled}+phase-markers{cc_classify/initial_points/edge_midpoints/facet_centroids/quads+interpolate/sanitize/process}+TimingHarness-DISABLED-gtest{cube-x7→98304-facets}+configure_tests.bat→build_tests{¬ASAN¬profiler,Debug+Release-one-tree};baseline-Release-689ms-chain{process-215ms>quads-121>interpolate-83@src-24576}=Debug-ranking-confirmed}
299d0332::item11{Post_processing-enum{full_default|structural_only}on-cc+sqrt3-entry-points;subdivide-node-intermediates=structural;SubdivisionChain-gtests-prove-final-bit-identical{positions+regenerated+preserved-channels,cc+sqrt3}}
9ad251ae::item12{subdivide+conway+unary-nodes-drop-process_for_graph{ops-self-post-process};boolean/join/realize/source-nodes-keep;x6-25.1→19.2s}
70169d17::item1{interpolate_attribute-skips-zero-present-channels+post_processing-regeneration_flags{skip-vertex_normal_smooth+corner_texcoord_1-when-regenerated||declared-throwaway-by-structural-chain};make_atlas-unaffected{structural-flags-infer-no-skips};interpolate-82.7→19ms@last-level}
eeb93354::item3{CC-pre-sizes-Source_tables-at-batch-create-points+m_vertex_src_to_dst;~583ms-chain}
650dc354::item2{sum-weights-once-per-Source_table{member-scratch};position-loop+fully-present-channels-use-precomputed{bit-identical,same-order};partial-presence-keeps-filtered-sum;~570ms;marginal-on-box-harness{few-present-channels},wins-on-attribute-rich-meshes}
RESULT::editor-Debug-stress{x5-5.5→4.4s,x6-25.1→17.8s,boolean-2×subdiv3-5.3→4.5s}+final-SMOKE-SWEEP-65/65✓+84/84-gtests{plain+ASAN}
?future::items-4/5/6{re-rank-by-Release:cc_quads-113ms>midpoints+centroids-75ms;process-194ms=legitimate-final-work}|9/10{user-sign-off}|conway-batch-creation{constant-factor}

[TASK::geometry-nodes]
@status::✓DONE{2026-07-02,phases1-5+extras;phase6=future}
>commits::713eb22d..d812547c
713eb22d::phase1{Geometry_payload-variant+typed-pin-keys,node-base,dirty-flag-graph,window}
cf4060d1::phase2{box+sphere+torus+cone+disc;param-pins+widget-fallback}
09e51836::phase3{subdivide+conway×9+transform+Geometry_unary_operation_node{shared-class,¬4-files}}
53ca69ee::phase4{join{1-multilink-pin,merge-in-payload+=}+boolean-csg+float/int/vector+math}
e8b43bb1::steppers{¬ImGui-popups-in-ax-canvas→imgui_enum_stepper}
c961b319::phase5-output{copy→process→Primitive(geometry)+make_renderable_mesh+make_raytrace;scene-node-in-place;scene/material-steppers}
cfe79f68::MCP{get_geometry_graph+add_node+connect}
a8dad173::undo-redo{node/link-insert-remove-ops+Operation_stack::execute_now+on_removed_from_graph+MCP-remove/disconnect}
e9d7bd44::serialization{JSON-v1+write/read_parameters-virtuals+factory_type_name+replace-op-undoable-load/clear+MCP-save/load/clear+window-Save/Load/Clear-UI}
d812547c::plan-doc{Implementation-Status-section+as-built-notes}
LIVE-VERIFIED✓::headless-MCP{box→output+box→dual→output-render;undo/redo-roundtrip{links+node-removal+load/clear};save-file-inspected;load-restores-scene-mesh;screenshots}

[TASK::geometry-nodes-phase6-2026-07-02]
@status::✓DONE{6a+6b+polish;6c/6d/6e=future}
>commits::a11abd21..0881e107
a11abd21::6a-incremental-eval{dirty-propagates-downstream-topo-order-in-evaluate();clean-nodes-keep-cached-output-payloads;structural-edits-mark-affected-nodes{insert→node,erase→sinks-of-outgoing-pre-unregister,connect/disconnect→sink};graph-mark_dirty=forced-full}
7585efe7::param-undo{Geometry_graph_parameter_operation{before/after-write_parameters-JSON-dumps,apply-via-read_parameters,first-execute-skips-apply{values-already-live},redo-applies};widget-gesture{node_editor-detects-mark_dirty-during-imgui()→commit-op-on-!IsAnyItemActive;m_committed_parameters-baseline};output-read_parameters-scene-switch-now-releases-scene-node}
1fcc38fc::MCP-set_parameter{window::set_node_parameters;partial-JSON-ok{read_parameters-fallback=current};get_geometry_graph+add_node-expose-parameters-object}
7fb5b32b::spawn-grid{add_node_of_type-places-4-col-grid-320×200;clear_graph-resets;¬(0,0)-stacking}
120e9176::output-name{InputText-in-node{commit-on-defocus¬keystroke}+apply_scene_node_name;serialized-"name";undoable-via-param-gesture}
0881e107::6b-CoW{identity-Transform-passes-source-through;single-link-Join+0-iter-Subdivide-already-did;doc-states-sharing-model}
LIVE-VERIFIED✓::headless-MCP{2-independent-chains:disconnect/undo/redo/set_parameter-re-eval-ONLY-affected-chain{trace-log};param-undo/redo-roundtrip-values+scene-node-name;spawn-positions-(0,0)/(320,0)/(640,0)-in-save-file;screenshots}
skipped::Node_physics-optional-on-output{low-value-now}

[TASK::geometry-nodes-phase6-completion-2026-07-02]
@status::✓DONE{output-physics+6d+6e+6c-design+comprehensive-smoke-sweep;6c-implementation=awaits-design-review}
>commits::ff414965..a2a36dd5
ff414965::output-physics{Physics-checkbox+motion-stepper{static/kinematic/dynamic};Node_physics+convex-hull{brush-pattern};sync-on-re-eval-via-set_collision_shape;released-with-scene-node;serialized+param-undo}
823cf2f1::6d-instances{Distribute_points{area-weighted-fan-tri-sampling,count+seed-deterministic,facet-normals}+Instance_on_points{scale+align-Y-to-normal}+Realize_instances{merge_with_transform/instance};Point_cloud+Geometry_instances-payloads+own-pin-keys;multi-link-concat}
e953ce5f::6e-groups{Group_node{path→private-subgraph,inline-eval,gi-feeds/go-reads}+Group_input/Group_output-interface-nodes;factory→make_geometry_graph_node-free-fn{shared:window+load+group-assets};thread_local-depth-guard-8{cycle→warn+empty};edit-groups-via-window-file-toolbar}
1dafd986::6c-design{plan-doc:value-or-field-payloads-on-existing-pin-keys+promoting-accessors,immutable-shared-expr-trees,memoized-vertex-domain-bulk-eval,dual-mode-Math_node,Set_position_node,4-slices}
4c28f849::CSG-FIX{geogram-mesh_boolean_operation-asserts-!single_precision→editor-death-on-boolean-with-2-inputs{found-by-sweep};run_mesh_boolean_operation{double-copies,remesh/decimate-pattern};test_csg.cpp;difference.cpp-dead-#else-dropped}
e466691b::MCP-output_payloads{per-node-per-slot:geometry-v/f-counts|points-count|instances-count|scalar-values→scripted-verification-evidence}
8e52a1b9::CC-QUADRATIC-FIX{geogram-create_sub_elements-growth-from-SIZE-not-capacity{upstream-main-too}→per-element-create-O(n)→CC-O(n²)→subdiv-x6=55min-practical-hang;batch-create_vertices/create_quads+map_dst_vertex_from_src_vertex/map_dst_vertex_from_src_facet_centroid/map_dst_facet_from_src_facet;iter-on-1536-facets-14370ms→542ms;x6-total-27s;82-gtests-pass-ASAN}
a2a36dd5::smoke-script{scripts/geometry_nodes_smoke_test.py;65-checks;busy-tolerant{queries-retry,mutations-once+drain-wait}}
SMOKE-SWEEP✓::65/65{every-node-type+param-sweeps-w-undo/redo+incremental-proof{trace-log}+multi-link-join-5-inputs+churn-17-undo/redo+serialization-all-types-roundtrip+output-edge-cases+stress{x5-6.9s,x6-26.9s,boolean-of-2-subdiv3-5.3s};one-long-session;final-screenshot-read}
!default-box=26v/24f{make_box-div1=2-segments/axis}→CC×1=98/96

?PENDING::6c-fields-implementation{after-design-review} batch-creation-for-other-ops{conway-still-quadratic-on-large-inputs} geogram-growth-fix{needs-fork,ask-user}

[TASK::session-tooling-2026-07-02]
@status::✓DONE
scripts/mcp_call.py::committed{b64-args+--list+token+port;tested-offline+live}
.claude/skills/erhe-headless-verify::new{build→launch→poll→drive→screenshot→cleanup}
CLAUDE.md::+cli-ninja-builds+headless-build-cmd+Testing-section{ERHE_BUILD_TESTS,erhe_<name>_tests,configure_tests_asan.bat}+clangd-false-positives+imgui-ini-restore+mcp_call.py-usage
erhe-renderdoc-skill::"already-wired"→"verify-first"{.mcp.json-was-absent}
.mcp.json::recreated{renderdoc-proxy;gitignored}
CLAUDE.md-slimmed::macOS+Quest-sections→skills{2c7a129e+70216207;new-erhe-macos-xcode;cpp-debugging+quest-launch-now-self-contained;CLAUDE.md-keeps-pointers+invariants{headset-protocol,display-constraint,self-serve-launch}}

[TASK::#239-per-scene-settings]{parked}
?PENDING::viewport+post_processing{init-consumed¬applied→needs-per-scene-refactor}+clear_color{editor-global-never-read→decide-wire||drop}+sky/grid-override-visual-verify{needs-runtime-override-setter-MCP-tool}

[NOTES]
!¬get_type_name-in-Item-derived{clashes-erhe::Item-virtual→C2555}→factory_type_name
!¬ImGui-popups-inside-ax-NodeEditor-canvas→steppers
!¬mutate-upstream-shared-Geometry→copy-first{copy_with_transform+identity}
!ax-GetNodePosition{never-drawn→ImVec2{FLT_MAX}}→is_valid_node_position-filter
!editor-run-dirties-desktop_window_imgui_host_imgui.ini→git-checkout-after-runs
!clangd-new-file-diagnostics::false-positives-until-reconfigure{ninja-build=truth}
