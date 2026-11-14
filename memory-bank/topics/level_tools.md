§MBEL:5.0
©erhe::Topic::level_tools{kit-bashing-level-tools;started-2026-11-13;branch=main}

[DOCS]{read-in-this-order}
requirements::doc/plans/level_tools.md{R-numbers;rulings-U1-U16;no-open-questions;"Later"-list}
design::doc/plans/level_tools_design.md{D-numbers}
steps::doc/plans/level_tools_implementation.md{"Built"=steps-1-11+23-25;"Next steps"=12..22,26}
behavior::doc/editor/level_tools.md(what-is-built)+doc/editor/navigation.md"Islands and queries"+doc/editor/brushes.md"Kit mode"+doc/editor/transform.md

[BUILT]{all-on-main;local-commits-cfd555376..febc96e90}
✓shape-library{src/editor/level/kit_shape*.{hpp,cpp}+kit_shapes_{basic,architecture,nature}.cpp;14-shapes;every-shape-has-seed+derive-mask;rock-forms-blob|angular|worn|slab+bevels-0-2;editor_level_tests-24}
✓pieces{Level_kits(app_context.level_kits);kit-brush-cache-weak-refs;Kit_piece-attached-property-group(shape+parameters-canonical-JSON-hexfloat+derive);place()=one-Compound_operation;set_parameters()=Kit_piece_parameters_operation;preview_parameters()=live-drag-without-operation;refresh_scene()-at-end-of-erhe-glTF-open}
✓UI{Window>Level-Kit(Level_kit_window);choose-shape->Brush_tool-kit-mode(preview+right-press-places+Z/X-rotate);Properties-"Kit Piece"-group-rows(kit_parameter_rows.hpp,Kit_piece_editor);toolbar+Move-tool-rows+Level-Kit-checkboxes-share-Surface_snap_config(both-off-by-default)}
✓surface{level_queries.hpp-find_surface_placement();Transform_tool::rest_on_surface()-per-translate-step;Y-arrow-drag-exempt}
✓navigation-reads{Navigation_mesh-islands+find_nearest_point+find_path;MCP-get_navigation_summary+navigation_find_point+navigation_find_path}
✓MCP{level_get_kits+level_place_pieces+level_get_pieces+level_set_piece_parameters+level_snap_to_surface+level_get_placement_preview+get/set_surface_snap_settings;transform_selection/drag_selection-surface_snap/surface_orient-explicit-default-off}
✓spatial-queries{a8b8f7ef3;level_queries.hpp-find_region_contents+make_height_map+find_support(shares-find_snap_distance-with-find_surface_placement);scene-free-parts=level_region.hpp(unit-tested);MCP-level_get_region_contents+level_get_height_map+level_get_support_report;Level-Kit-window-"Spatial Queries"-section;surface-rays-pass-a-solid-leaving-face-unless-just-after-the-moved-piece}
✓overlap+level-check{410a25bc2;find_overlaps(bounds-sweep->ICollision_shape::get_penetration_depth;measure=exact|convex_hull|not_measured;tag-exclusion)+run_level_check(support+overlap+unreachable-islands+R2.10+unreadable-records);MCP-level_get_overlap_report+level_check;window-Exclude-Tags+Overlap-Report+Level-Check;!Box3D-get_penetration_depth-stub-never-compiled-locally(Jolt-tree-only)}
✓repeat{level_repeat.hpp-expand_repeat(line|grid|ring|polyline;scene-free;limit-1024)+Level_kits::repeat()->place()-one-undo-entry+read_repeat_start(piece=element-0,skipped);MCP-level_repeat_pieces(shape|piece_id);window-Repeat-section(polyline-points=other-selected-nodes);design-D3.7+D4.7}
✓groups{group=plain-Xform-parent(no-record;nests);Level_kits::group|ungroup|duplicate|mirror(one-entry-each);mirror=proper-rotations-via-Kit_shape_definition::mirror_planes(¬negative-scale:physics-bodies-take-no-reflection);pieces-without-plane-reported-not-exact;set_copied_brush_collision_shapes(clone-lacks-collision-shape;clipboard-paste-uses-it-too);MCP-level_group_nodes+level_ungroup+level_duplicate_group+level_mirror_group;design-D1.10+D3.8+D4.8}
✓material-slots{Kit_shape_definition::material_slots(ordered,named)+facet-attribute-kit_slot(Kit_mesh_builder::set_slot)+Brush_data::part_geometries->one-mesh-primitive-per-slot;assignment=the-primitives-materials(Kit_piece-unchanged);palette=list+default+alternatives-per-slot-name;Level_kits::set_slot_material;MCP-materials{slot:name}(single-material-arg-REMOVED)+level_set_piece_material;window-combo-per-slot;!tree-pieces-saved-before-this=primitive-count-mismatch->keep-loaded-mesh(no-migration)}
✓verify{scripts/level_tools_verify.py(544-checks-after-step-24;--only-layouts|--only-window-layout;--only-transform;saves+restores-config/editor/{editor_settings,desktop_windows}.json)+scripts/navigation_verify.py(76)+editor_level_tests+editor_navigation_tests(16);mcp_server_tests=compiled-never-run(needs-display)}

[OPEN]
✓step-7f::DONE-2026-11-14{Properties-row-drag-froze-because-rows-had-positional-ImGui-ids+developer-mode-rows-above-changed-count-during-preview;fix=key-based-row-ids(doc/editor/properties_window.md-section-5);©User-verified-windowed;!trap:developer-mode-off-headless-hid-it->reproduce-UI-bugs-with-the-user's-config;gdb-logging-breakpoint-on-ImGui::SetActiveID(id!=ActiveId&&MouseDown)-gives-the-drag-loss-callstack}
✓kit-layout-node{step-23;level/kit_layout{,_values}.{hpp,cpp}+level_kits_layout.cpp;Kit_layout-group(pattern+items-canonical-text,module_snap,surface_snap,surface_orient,random_seeds,seed,locked);count|gap-modes(gap:step=largest-item-extent+gap,remainder-spread,both-ends-hold-a-piece;full-ring-closes);seed-mask=each-items-derive-list;locked:value-change/lock/refresh/surface-snapped-move-regenerate(one-entry;matching-children-kept);lock-replaces-all-children,undo-restores-user-nodes;raw-Kit_layout-writes=Property_set_operation-follow-ups;refusals:selection-redirect-to-layout+instance_structure-predicates-extended+locked_layout_refusal()-in-each-item-editing-MCP-tool;level-settings-change-needs-explicit-refresh;layout-node-world-scale!=1-refused;MCP-level_create_layout+level_set_layout+level_get_layouts+level_set_layout_lock+level_refresh_layout}
?curves::doc/plans/curves.md(rulings-V1-V6+C1-C9+steps;kit-layout-path-arrangement-follows-a-curve-node=R5.12.8;replaces-plan-step-22-and-the-layout-polyline-point-list;reuse:sweep.cpp-bezier+frames->erhe::math,lattice-tool-pick/draw/gizmo-anchor->point-set-interface,animation-window-handle-behavior)
✓layout-window{step-24+25;level/kit_layout_editor.{hpp,cpp}=Level-Kit-window-"Layout"-section(Make-Layout-from-piece=one-entry,piece-replaced-by-element-0;rows-for-every-value;Lock/Unlock/Refresh);Kit_layout_changed_message;level_create_layout-from_piece_id;level_repeat_pieces+Level_kits::repeat-REMOVED;!imgui_click-index-counts-visible-items-only->click-by-id}
?then::curves-plan+step-26(layout-drag-preview);spec(©User-U17-2026-11-14:properties=pattern+items+snap+random-seeds+seed-mask;owns-children;locked=layout-regenerates+children-not-editable;unlocked=layout-hands-off;unlock-changes-nothing;lock-discards-edits;both-undoable,undo-of-lock-restores-user-children;scatter-later-as-a-pattern-kind)+12..22
!layout-recompute-on-need::doc/plans/open_items.md"Performance and measurement"(©User-2026-11-14;Layout_system-recomputes-every-frame-today)
!test-level::res/editor/scenes/level_kit_test.glb(built-over-MCP-2026-11-14;findings:polyline-wall-runs-overlap-0.125m-at-corners+hollow-platform-inner-island+1m-height-map-misses-0.25m-walls+duplicated-groups-keep-piece-names)
?step-21::exact-overlap-depth-for-two-triangle-mesh-pieces(now-convex-hull-stand-in->false-positive-for-a-piece-inside-an-opening)
?step-20::gizmo-X-drags-land-~1px-short-in-a-wide-viewport(cause-unknown:script-window_point|Transform-tool-drag-mapping);check_spatial_queries-closes-its-scene-until-found

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
