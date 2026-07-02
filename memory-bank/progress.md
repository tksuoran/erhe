§MBEL:5.0

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

?PENDING::phase6→next_prompt.txt
6a::incremental-eval{per-node-m_dirty-exists,evaluate()-runs-all}!first
polish::node-placement-(0,0)-stacking+unique-scene-node-names+MCP-set-parameter+param-edit-undo+Node_physics-optional
6b::CoW 6d::instances 6e::groups 6c::fields{large}

[TASK::session-tooling-2026-07-02]
@status::✓DONE
scripts/mcp_call.py::committed{b64-args+--list+token+port;tested-offline+live}
.claude/skills/erhe-headless-verify::new{build→launch→poll→drive→screenshot→cleanup}
CLAUDE.md::+cli-ninja-builds+headless-build-cmd+Testing-section{ERHE_BUILD_TESTS,erhe_<name>_tests,configure_tests_asan.bat}+clangd-false-positives+imgui-ini-restore+mcp_call.py-usage
erhe-renderdoc-skill::"already-wired"→"verify-first"{.mcp.json-was-absent}
.mcp.json::recreated{renderdoc-proxy;gitignored}

[TASK::#239-per-scene-settings]{parked}
?PENDING::viewport+post_processing{init-consumed¬applied→needs-per-scene-refactor}+clear_color{editor-global-never-read→decide-wire||drop}+sky/grid-override-visual-verify{needs-runtime-override-setter-MCP-tool}

[NOTES]
!¬get_type_name-in-Item-derived{clashes-erhe::Item-virtual→C2555}→factory_type_name
!¬ImGui-popups-inside-ax-NodeEditor-canvas→steppers
!¬mutate-upstream-shared-Geometry→copy-first{copy_with_transform+identity}
!ax-GetNodePosition{never-drawn→ImVec2{FLT_MAX}}→is_valid_node_position-filter
!editor-run-dirties-desktop_window_imgui_host_imgui.ini→git-checkout-after-runs
!clangd-new-file-diagnostics::false-positives-until-reconfigure{ninja-build=truth}
