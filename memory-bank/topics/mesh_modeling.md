§MBEL:5.0
©erhe::Topic::mesh_modeling
@scope::Blender-style mesh modeling: walkers, Edit_mesh, selection commands, modal tools (loop cut, inset, knife, bevel), slides, discrete ops
@docs::doc/editor/mesh_modeling.md+doc/editor/mesh_component_selection.md+doc/editor/transform.md+doc/editor/operations.md+doc/erhe/geometry.md+doc/plans/mesh_modeling.md{remaining}

[STATE]
@DONE-2026-09-30..10-01{via-harness;opus-coders;26-commits-78ade44bd..a12416cdf}
M0::walkers(topology.hpp;Blender-rules-incl-hub)+fan-order-fix+Edit_mesh(10-primitives;tombstones;emission-via-provenance)+edge_sharpness-identity-in-post_processing
M1::flush+Mode_conversion{flush|expand}+all/none/invert/linked+vertex/edge-box+paint(CPU-projection,selects-through)
M2::loop/ring/face-loop/boundary-select{Alt/Ctrl+Alt-click;Shift;boundary-cycle;change-driven-preview}
M3::delete-contexts+dissolve-faces/edges/vertices/limited{Delete-key+Ctrl+X-mode-dispatch}
M4::merge{center/position/first/last/collapse/by-distance(octree);M=center;no-3D-cursor->hovered-point}
M8::subdivide-edges{Blender-patterns;smoothness=Hermite}
M6::slide{scalar-drag-path:begin_scalar/apply_scalar/cancel;G-key;E/F/C;undo-blocked-during-live-edit(Operation_stack::get_undo_block_reason);correct-UVs-at-commit}
M5::loop-cut{Ctrl+R;ring-preview;cut=subdivide-ring;chained-slide;one-Fork_geometry_operation}
M7::inset{I;scalar-mode;O/I/B/E/R;commit-rebuilds-via-library}
M9::split(Y)/rip(V)/separate(P;Separate_selection_operation;new-node-after-original)
M11+M12::Screen_snap+knife{K;Knife_cut-library-driven-point-by-point;no-plane-query-on-IScene->plane-vs-CPU-facets;occlusion=own-facets}
M13::bevel{Ctrl+B;1-segment->segments+profile(superellipse)+cutoff-patch;linear-directions;W/L/PageUp/PageDown/[/]}
M14::bridge{open/closed/pairs,merge,twist,cuts}|M15::fill(F;contextual-order;no-wire-edges)|M16::connect(J;set+pair-cutting-plane-search)
M10::flip/recalc-outside(Shift+N)/smooth-selected(in-place-Move_mesh_vertices_operation)
tests::erhe_geometry_tests-264;scripts/mesh_modeling_verify.py-465-checks(headless;key-injection);geometry_edit_node_order_verify-extended

[TRAPS]
!coder-invented-rules::Blender-is-behaviour-ref(D1)->check-invented-rules-against-source(cube-loop-single-facet-rule-was-wrong;hub-rule-is-real)
!key-conflicts::mask-less-bindings-match-any-modifier->erhe::commands-masked-bindings-dispatch-first(M1a);modal-keys-bound-with-exact-mask-0;mode-dispatch-commands-share-has_component_mode_selection()-guard(declines-during-modal-gesture)
!pre-existing-crashes-fixed::transform-in-face-mode-with-empty-selection(Transform_tool-gizmo-ownership)+empty-Geometry-result(empty-Primitive-legal)
!Geometry::build_edges-made-facet-less-duplicate-edge-when-two-facets-ran-shared-edge-same-way(fixed-M10)
!configure_vs2026_vulkan_headless.bat-leaves-ERHE_BUILD_TESTS=OFF(doc-claims-ON)->pass--DERHE_BUILD_TESTS=ON-in-VsDevCmd-shell;ninja-wrapper-splits--DX=Y-from-cmd->seed-CMakeCache
!cpptrace-crash-stack->stderr(verify-launch-helper-now-keeps-logs/editor_stderr.txt)

[OPEN]
?user-interactive-checks::loop/ring-preview-look;loop-cut-preview;knife-gesture-by-hand;slide-rail-drawing;move-mode-snap-option(never-run)
?remaining::doc/plans/mesh_modeling.md{R1-grid-fill;R2-bevel-rest;R3-knife-project+bisect;R4-separate-by-parts/material;R5-proportional;R6-symmetry;R7-tris/quads;R8-path-select;R9-spin;R10-compute-box-select;R11-follow-ups(Edit-menu-Delete,no-op-undo-entries,Mcp_test-gesture-cases,rail-drawing,pointer-wrap)}
?doc-stale::doc/agents/windows.md-claims-headless-tests-ON
