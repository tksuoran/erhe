§MBEL:5.0

[FOCUS]
@task::geometry-nodes{doc/geometry-nodes-plan.md}
@branch::geometry_nodes{unmerged,10+commits-ahead-main}
>done::2026-07-02{phases1-5+MCP-tools+undo-redo+serialization+plan-doc-updated}✓
?next::phase6{6a-incremental-eval!first+CoW+fields+instances+groups}→next_prompt.txt

[STATE]
code::src/editor/geometry_graph/{payload+node-base+graph+window+operations+serialization+nodes/}
verify::headless-vulkan+MCP{geometry_graph_*-tools+scripts/mcp_call.py+erhe-headless-verify-skill}
undo-redo::structural-ops{execute_now,on_removed_from_graph,LIFO-safe}
serialization::JSON-v1{type+parameters+position,links-by-index+slot,load/clear=undoable-replace-op}

[ALSO-DONE-2026-07-02]
tooling::mcp_call.py{committed}+erhe-headless-verify-skill+CLAUDE.md{cli-builds+testing+clangd/ini-gotchas}+renderdoc-skill-verify-wording+.mcp.json-recreated{renderdoc-proxy,machine-local}

[PARKED]
#239-per-scene-settings::consumers-pending{viewport+post_processing-init-time-refactor;clear_color-decide-wire||drop;sky/grid-override-visual-verify}

[BLOCKERS]
none
