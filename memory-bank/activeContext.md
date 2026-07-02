§MBEL:5.0

[FOCUS]
@task::geometry-nodes{doc/geometry-nodes-plan.md}
@branch::geometry_nodes{unmerged,ahead-main}
>done::2026-07-02{phases1-5+MCP+undo-redo+serialization+6a-incremental+6b-CoW+param-undo}✓
>done::2026-07-02{output-physics+6d-instances+6e-groups+6c-design+SMOKE-SWEEP-65/65✓+2-real-bugs-fixed}✓
>done::2026-07-02{cc-performance-pass:harness+items-11-12-1-3-2;doc/catmull_clark.md-"Implemented-optimizations"-section;editor-x6-25.1s→17.8s-Debug{x5-5.5→4.4s};Release-harness-chain-689→570ms;stopped-at-diminishing-returns;items-4/5/6=future{re-rank-by-Release-numbers},9/10-need-user-sign-off}✓
?queued::smoke-coverage-extensions{future_prompt_1.txt}|doc-audit{future_prompt_2.txt}|6c-fields-implementation{awaits-design-review}
>done::geogram-growth-bug{fork-fix+pin-daf9e192{commit-88376b78}+upstream-issue-371;per-element-create-amortized-O(1)-again;batching-remaining-ops=constant-factor-only-now}

[STATE]
code::src/editor/geometry_graph/{payload+node-base+graph+window+node-factory+operations+serialization+nodes/}
nodes::box+sphere+torus+cone+disc|subdivide+conway+transform+triangulate+normalize+reverse+repair|distribute+instance+realize|join+boolean|float+integer+vector+math|output{+physics}|group_input+group_output+group
payload::geometry|float|int|bool|vec3|vec4|mat4|material|points{Point_cloud}|instances{Geometry_instances}
verify::headless-vulkan+MCP{scripts/geometry_nodes_smoke_test.py=65-check-sweep;get_geometry_graph-reports-output_payloads-per-node}
perf-harness::scripts/configure_tests.bat→build_tests{VS-multi-config,¬ASAN¬profiler,Debug+Release-one-tree}+TimingHarness-DISABLED-gtests{--gtest_also_run_disabled_tests}+Operation_timing/Scoped_phase_timer{thread-local,inert-uninstalled}
!default-box::26-verts/24-facets{make_box-div-1=2-segments-per-axis,NOT-8/6}
verify-trick::logging.json-editor.graph_editor=trace{temporary,revert-before-commit}→"evaluating node"-lines-prove-incremental

[BUGS-FIXED-BY-SWEEP]
csg-single-precision::4c28f849{geogram-mesh_boolean_operation-asserts-!single_precision;fix=run_mesh_boolean_operation-double-copies;test_csg.cpp-regression}
cc-quadratic::8e52a1b9{geogram-create_sub_elements-growth-from-SIZE-not-capacity→per-element-create=O(n)→CC=O(n²)→subdiv-x6=55min-hang;fix=batch-create_vertices/create_quads+map_dst_*-helpers;x6-now-27s}

[PARKED]
#239-per-scene-settings::consumers-pending{viewport+post_processing-init-time-refactor;clear_color-decide-wire||drop;sky/grid-override-visual-verify}

[BLOCKERS]
none
