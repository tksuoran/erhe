§MBEL:5.0

[FOCUS]
@task::geometry-nodes{doc/geometry-nodes-plan.md}
@branch::geometry_nodes{unmerged,ahead-main}
>done::2026-07-02{phases1-5+MCP+undo-redo+serialization+6a-incremental+6b-CoW+param-undo}✓
>done::2026-07-02{output-physics+6d-instances+6e-groups+6c-design+SMOKE-SWEEP-65/65✓+2-real-bugs-fixed}✓
?next::6c-fields-implementation{design-in-plan-doc-awaits-review,4-slices}
?queued::doc-audit{future_prompt.txt,52-files,4-dangling-refs}
?follow-up::batch-element-creation-for-other-Geometry_operations{conway-ops-still-quadratic-on-large-inputs}|geogram-fork-for-create_sub_elements-growth-bug{upstream-main-has-it-too}

[STATE]
code::src/editor/geometry_graph/{payload+node-base+graph+window+node-factory+operations+serialization+nodes/}
nodes::box+sphere+torus+cone+disc|subdivide+conway+transform+triangulate+normalize+reverse+repair|distribute+instance+realize|join+boolean|float+integer+vector+math|output{+physics}|group_input+group_output+group
payload::geometry|float|int|bool|vec3|vec4|mat4|material|points{Point_cloud}|instances{Geometry_instances}
verify::headless-vulkan+MCP{scripts/geometry_nodes_smoke_test.py=65-check-sweep;get_geometry_graph-reports-output_payloads-per-node}
!default-box::26-verts/24-facets{make_box-div-1=2-segments-per-axis,NOT-8/6}
verify-trick::logging.json-editor.graph_editor=trace{temporary,revert-before-commit}→"evaluating node"-lines-prove-incremental

[BUGS-FIXED-BY-SWEEP]
csg-single-precision::4c28f849{geogram-mesh_boolean_operation-asserts-!single_precision;fix=run_mesh_boolean_operation-double-copies;test_csg.cpp-regression}
cc-quadratic::8e52a1b9{geogram-create_sub_elements-growth-from-SIZE-not-capacity→per-element-create=O(n)→CC=O(n²)→subdiv-x6=55min-hang;fix=batch-create_vertices/create_quads+map_dst_*-helpers;x6-now-27s}

[PARKED]
#239-per-scene-settings::consumers-pending{viewport+post_processing-init-time-refactor;clear_color-decide-wire||drop;sky/grid-override-visual-verify}

[BLOCKERS]
none
