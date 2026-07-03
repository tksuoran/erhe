§MBEL:5.0

[FOCUS]
@task::geometry-nodes{doc/geometry-nodes-plan.md}
@branch::geometry_nodes{unmerged,ahead-main}
>done::2026-07-02{phases1-5+MCP+undo-redo+serialization+6a-incremental+6b-CoW+param-undo}✓
>done::2026-07-02{output-physics+6d-instances+6e-groups+6c-design+SMOKE-SWEEP-65/65✓+2-real-bugs-fixed}✓
>done::2026-07-02{cc-performance-pass:harness+items-11-12-1-3-2;doc/catmull_clark.md-"Implemented-optimizations"-section;editor-x6-25.1s→17.8s-Debug{x5-5.5→4.4s};Release-harness-chain-689→570ms;stopped-at-diminishing-returns;items-4/5/6=future{re-rank-by-Release-numbers},9/10-need-user-sign-off}✓
>done::2026-07-03{smoke-coverage-extension:65→120-checks{bdc71123}+2-real-bugs-fixed{cycle/self-link-acceptance→Graph::would_create_cycle-b553559b,facet-less-geometry-output-crash-4491835f};sweep-120/120✓}✓
?queued::6c-fields-implementation{awaits-design-review}
>done::2026-07-03{doc-audit:52-files-reviewed{7-parallel-agents}→16-deleted{completed-plans+relics+spec-copy+LSAI-dupes}+2-recovered-from-92c14dc5{glslang_bug_report+debug_renderer_multiview}+4-dangling-refs-fixed{hud_hotbar+work.md-comments-reworded}+~16-docs-refreshed{stale-claims}+CLAUDE.md-option-defaults+2-quest-skills-line-refs;doc/-52→38-files;commits-6106f4d0..5b1cc01c}✓
>done::geogram-growth-bug{fork-fix+pin-daf9e192{commit-88376b78}+upstream-issue-371;per-element-create-amortized-O(1)-again;batching-remaining-ops=constant-factor-only-now}
>done::2026-07-03{editor-improvements-geometry-graph-items:8-pin-colors{d753e5d5}+7-pipeline-tail-breadcrumbs{de8a5740,Scoped_phase_timer-sets-breadcrumb}+9-resolved-by-geogram-fork-pin{doc-only-b262c367}+6-ASYNC-EVAL{2e8a2225+8f179479};sweep-120/120✓;set_param-x6-0.13s{was-20s-block}}✓

[STATE]
code::src/editor/geometry_graph/{payload+node-base+graph+window+node-factory+operations+serialization+nodes/}
nodes::box+sphere+torus+cone+disc|subdivide+conway+transform+triangulate+normalize+reverse+repair|distribute+instance+realize|join+boolean|float+integer+vector+math|output{+physics}|group_input+group_output+group
payload::geometry|float|int|bool|vec3|vec4|mat4|material|points{Point_cloud}|instances{Geometry_instances}
verify::headless-vulkan+MCP{scripts/geometry_nodes_smoke_test.py=120-check-sweep;get_geometry_graph-reports-output_payloads-per-node}
perf-harness::scripts/configure_tests.bat→build_tests{VS-multi-config,¬ASAN¬profiler,Debug+Release-one-tree}+TimingHarness-DISABLED-gtests{--gtest_also_run_disabled_tests}+Operation_timing/Scoped_phase_timer{thread-local,inert-uninstalled}
!default-box::26-verts/24-facets{make_box-div-1=2-segments-per-axis,NOT-8/6}
verify-trick::logging.json-editor.graph_editor=trace{temporary,revert-before-commit}→"evaluating node"-lines-prove-incremental
!async-eval::graph-evaluates-on-tf::Executor-worker{snapshot-isolation:shadow-clone{factory+param-JSON+links+cached-payloads+dirty-flags,logs-under-live-ids-via-set_log_source_id};live-graph-never-touched-by-worker→UI/undo/MCP-stay-interactive;output-node-2-phase{evaluate=worker-builds-geometry/primitive/hull,apply_evaluated_to_scene=main};group-internal-outputs-via-adopt_subgraph_outputs;MCP-mutations-return-immediately,get_geometry_graph=completion-barrier{waits},get_async_status-reports-runs;!scene-queries-after-graph-mutation-need-get_geometry_graph-barrier-first{smoke-test-scene_nodes()-does-this};!fresh-Geometry_graph-born-forced-full→shadow-must-consume_forced_full-before-copying-live-flag{else-incremental-eval-silently-lost}}

[BUGS-FIXED-BY-SWEEP]
csg-single-precision::4c28f849{geogram-mesh_boolean_operation-asserts-!single_precision;fix=run_mesh_boolean_operation-double-copies;test_csg.cpp-regression}
cc-quadratic::8e52a1b9{geogram-create_sub_elements-growth-from-SIZE-not-capacity→per-element-create=O(n)→CC=O(n²)→subdiv-x6=55min-hang;fix=batch-create_vertices/create_quads+map_dst_*-helpers;x6-now-27s}
cycle-acceptance::b553559b{MCP-connect+crafted-files-accepted-self/cycle-links→sort-fails-every-frame+perpetual-dirty=permanent-re-eval;fix=Graph::would_create_cycle+connect-refuses+window-pre-validates{no-noop-undo-entry}+load-rejects-cycle/key-mismatch-files+group-asset-link-check}
empty-geometry-output-crash::4491835f{conway/boolean-out-of-range-op→empty-geometry→output-node→ERHE_VERIFY(total_index_count>0)-abort-in-Primitive_builder;fix=output-treats-0-facet-source-like-disconnected{clear-primitives+remove-physics};VERIFY-kept-fail-loud-elsewhere}

[PARKED]
#239-per-scene-settings::consumers-pending{viewport+post_processing-init-time-refactor;clear_color-decide-wire||drop;sky/grid-override-visual-verify}

[BLOCKERS]
none
