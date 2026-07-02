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

?PENDING::6c::fields{large} 6d::instances 6e::groups

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
