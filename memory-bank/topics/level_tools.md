§MBEL:5.0
©erhe::Topic::level_tools{kit-bashing-level-tools;started-2026-11-13;branch=main}

[DOCS]{read-in-this-order}
requirements::doc/plans/level_tools.md{R-numbers;rulings-U1-U16;no-open-questions;"Later"-list}
design::doc/plans/level_tools_design.md{D-numbers}
steps::doc/plans/level_tools_implementation.md{"Built"=steps-1-7e;"Next steps"=7f-first-then-8..19}
behavior::doc/editor/level_tools.md(what-is-built)+doc/editor/navigation.md"Islands and queries"+doc/editor/brushes.md"Kit mode"+doc/editor/transform.md

[BUILT]{all-on-main;local-commits-cfd555376..febc96e90}
✓shape-library{src/editor/level/kit_shape*.{hpp,cpp}+kit_shapes_{basic,architecture,nature}.cpp;14-shapes;every-shape-has-seed+derive-mask;rock-forms-blob|angular|worn|slab+bevels-0-2;editor_level_tests-24}
✓pieces{Level_kits(app_context.level_kits);kit-brush-cache-weak-refs;Kit_piece-attached-property-group(shape+parameters-canonical-JSON-hexfloat+derive);place()=one-Compound_operation;set_parameters()=Kit_piece_parameters_operation;preview_parameters()=live-drag-without-operation;refresh_scene()-at-end-of-erhe-glTF-open}
✓UI{Window>Level-Kit(Level_kit_window);choose-shape->Brush_tool-kit-mode(preview+right-press-places+Z/X-rotate);Properties-"Kit Piece"-group-rows(kit_parameter_rows.hpp,Kit_piece_editor);toolbar+Move-tool-rows+Level-Kit-checkboxes-share-Surface_snap_config(both-off-by-default)}
✓surface{level_queries.hpp-find_surface_placement();Transform_tool::rest_on_surface()-per-translate-step;Y-arrow-drag-exempt}
✓navigation-reads{Navigation_mesh-islands+find_nearest_point+find_path;MCP-get_navigation_summary+navigation_find_point+navigation_find_path}
✓MCP{level_get_kits+level_place_pieces+level_get_pieces+level_set_piece_parameters+level_snap_to_surface+level_get_placement_preview+get/set_surface_snap_settings;transform_selection/drag_selection-surface_snap/surface_orient-explicit-default-off}
✓verify{scripts/level_tools_verify.py(199-checks-at-febc96e90;--only-transform;saves+restores-config/editor/{editor_settings,desktop_windows}.json)+scripts/navigation_verify.py(76)+editor_level_tests+editor_navigation_tests(16);mcp_server_tests=compiled-never-run(needs-display)}

[OPEN]
⚡step-7f::parameter-row-drag-freezes-in-WINDOWED-editor{©User-2026-11-13:"value in the row stops from changing, happens with all slider parameters"+"drag might get interrupted when the shape is live updated";headless-cannot-reproduce;suspects+method=doc/plans/level_tools_implementation.md-7f;WAITS-for-©User-reproduction-with-the-kitdrag-diagnostic-build(per-machine-state=memory-bank/local/context.md)}
?then::step-8(region-contents+height-map+support-report)+9..19

[WORKFLOW]{©User-2026-11-13}
orchestrator-writes-brief->one-opus-coder-at-a-time->orchestrator-reviews-diff+re-runs-one-check->commits-explicit-paths(doc/agents/orchestration_harness.md)
user-test-builds::windowed-DEBUG(©User:"use debug builds for testing from now on");coder-verification::headless-tree
!never-stage-the-user's-uncommitted-files{git-status-before-add;explicit-paths-only}

[TRAPS]
headless-injected-input!=SDL-input::a-UI-bug-seen-only-windowed-may-not-reproduce-headless(7f)
make_convex_hull::deterministic+outward-winding-since-df4e343c0(was-half-reversed;chamfer3-VERIFY)
find_surface_placement::rays-start-one-piece-height-above-base;piece-buried-deeper-stays(step-15);coincident-own-face-rule(D7.3)
Kit_piece-records-saved-before-df4e343c0(no-seed)::do-not-parse->follows_kit-false(no-migration;©User-policy)
developer-mode-on::raw-Kit_piece.shape/parameters-rows-still-show(developer_only)
Brush-insert-binding::plain-right-press-only(modifier-mask-0);Alt+right=camera-tumble
Recast-DebugUtils--Werror-on-GCC->build-named-targets-never-all
