§MBEL:5.0

[FOCUS]
@task::geometry-nodes{doc/geometry-nodes-plan.md}
@branch::geometry_nodes{unmerged,17-commits-ahead-main}
>done::2026-07-02{phases1-5+MCP-tools+undo-redo+serialization}✓
>done::2026-07-02{phase6a-incremental-eval+param-undo+MCP-set_parameter+spawn-grid+output-name+phase6b-CoW}✓
?next::phase6c-fields{large}|6d-instances|6e-groups
?queued::doc-audit{next_prompt.txt,52-files,4-dangling-refs}

[STATE]
code::src/editor/geometry_graph/{payload+node-base+graph+window+operations+serialization+nodes/}
eval::incremental{dirty-propagates-downstream-topo-order,clean-nodes-keep-cached-outputs,structural-edits-mark-affected-at-edit-site}
param-undo::Geometry_graph_parameter_operation{before/after-write_parameters-JSON,first-execute-skips-apply,widget-gesture-commit-on-deactivate}
verify::headless-vulkan+MCP{geometry_graph_*+set_parameter+scripts/mcp_call.py+erhe-headless-verify-skill}
verify-trick::logging.json-editor.graph_editor=trace{temporary,revert-before-commit}→"evaluating node"-lines-prove-incremental

[PARKED]
#239-per-scene-settings::consumers-pending{viewport+post_processing-init-time-refactor;clear_color-decide-wire||drop;sky/grid-override-visual-verify}

[BLOCKERS]
none
