§MBEL:5.0

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
