§MBEL:5.0

[TASK::node-properties-graph-selection]{impl-done;user-verified-selection+pin-edges;size-rework-awaits-user-verify}
✓all{a2eeba7b}::NodeProperties-shows-canvas-selection-of-every-graph-editor+panel-parameter-editing+2-MCP-select-tools+dispatch-table
✓pin-edges{bc8c8377}::Inputs/Outputs-layout-for-geometry+texture-nodes{shared-node_edge.hpp;left|right-only;serialized-input_edge/output_edge;MCP-set_node_layout-tools}
✓size-rework{c62ef868,user-rejected-scale}::Size=requested-width/height{canvas-units,<=0=auto;content-NOT-scaled;width→center-column,height→Dummy-pad;JSON-width/height;MCP-width/height-args;headless-verified-500x320+auto-restore}
✓preview-fit{6aa91d97}::geometry+texture-previews-fill-node{get_preview_fit_size;preview_display_size-virtual-removed;default-preview-150cu-now;headless-verified-420-node-fill-both-graphs}
?user-verify::Size-width/height-drags+Auto-button-feel+preview-fill-look{default-slightly-larger-previews}+panel-parameter-edit-undo+position-drag
!preview-fit-quantization-cap-512{very-large-node-preview-upscales-past-512px-display}
!canvas-selection-per-window-persists{clicking-in-one-graph-window-does-NOT-clear-another's→NodeProperties-can-show-nodes-from-several-windows-at-once;dedup-by-node-only}
!MSVC-C1061::~125-else-if=nesting-limit{mcp_server.cpp-dispatch;table-must-be-function-local{private-member-ptr-access}}
!node-layout-¬in-write_parameters{concrete-nodes-don't-call-base}→serialized-by-write/read_graph_asset_json-next-to-parameters{width/height/input_edge/output_edge};¬in-shadow-clones{irrelevant-for-eval}
!requested-node-width-must-subtract-NodePadding{ax-Style.NodePadding-ImVec4-canvas-units}-else-GetNodeSize≠request
!properties_imgui-forces-content_scale=1{restores-after;else-preview-size-quantization-thrash-between-panel+canvas}
!ax-SelectNode-needs-node-drawn-once{editor-context-creates-canvas-nodes-on-first-draw}→MCP-select-tools-document-show-window-first

[TASK::houdini-graph-features]{impl-done,awaiting-user-RE-verify-post-fixes}
✓wire-cutting{a7d98635}+display/ghost-flags{886e4c31}+node-previews{77feb2a8}+cut-fixes+N.V-shading{11061763}+hover-spin+zoom-sharp{a939c8e6}+texture-graph-zoom-sharp{51c064fd}
>user-tested-2026-07-15::cut-gesture-partial{slow-drag-missed+highlight-without-delete}→both-root-caused+fixed{11061763}
?user-re-verify::slow-Y+drag-highlights+DELETES{undo,Escape}+hover-spin-feel+zoom-sharpening-both-graphs+badge-clicks
!designation-ids=session-node-ids{persist-as-node-INDICES-in-graph-JSON;shadow-resolution-via-get_log_id}
!ghost-invisible-when-content_edge_lines.use_id_buffer{known-limitation,documented-in-commit}
!designating-value-node→null-geometry-bake→scene-mesh-clears{consistent-with-empty-semantics;badges-hidden-on-non-geometry-output-nodes}
!Brush_preview-primitive-overload{render_preview(texture,layer,primitive,material,rotation_radians,headlight_shading);brush-overload-derives-rotation-from-time;headlight=key-light-at-camera→Lambert=dot(N,V)}
!DeleteItemsAction::Add-refuses-while-any-action-current{silent-drop!}→AddFromAction-for-queueing-from-inside-an-action
!ImCubicBezierLineIntersect-unreliable-for-tiny-segments{~2px;also-inside-Link::TestHit-rect-edges}→tessellate{ImCubicBezierSubdivide}+exact-segment-segment-tests;broad-phase-rects-need-generous-Expand
!open_texture_graph_window-MCP-opens-NEW-instance-without-target→pass{"graph_texture":name}-arg
!no-texture_graph_set_view-zoom-MCP-tool{geometry-has-one;add-if-headless-zoom-verify-needed}
!preview-texture-size-quantization::pow2-[64,512]-from-display-px{96*content_scale}→reallocs-only-at-boundaries

[TASK::#239-per-scene-settings]{parked}
✓runtime-setter-MCP-tool{set_scene_settings+get_scene_settings@phase4,3a4989b6}→sky/grid-override-visual-verify-unblocked
?PENDING::viewport+post_processing{init-consumed¬applied→needs-per-scene-refactor}+clear_color{editor-global-never-read→decide-wire||drop}+sky/grid-override-visual-verify

[NOTES]
!library-items-HOSTED-since-99998e3d{material/brush/graph-asset/physics-item->get_item_host()=owning-Scene_root;prefab-shared-items+not-in-library=null-host;get_hosting_scene_root(item)-helper}
!Content_library-add-claims-host{ERHE_VERIFY-¬already-owned-elsewhere}→listing-item-owned-by-another-library-needs-add_reference{is_reference-entry;precedent:material_preview+prefab-paths}
!MCP-copy_library_item{item_name+source_scene+target_scene;material/brush/physics-types;textures/graph-assets-not-copyable;"(N)"-suffix;¬undoable}
!MCP-place_brush-takes-brush_id{¬brush_name;get_scene_brushes-for-ids;ids-differ-per-scene-since-brush-copies}
!¬get_type_name-in-Item-derived{clashes-erhe::Item-virtual→C2555}→factory_type_name
!¬ImGui-popups-inside-ax-NodeEditor-canvas→steppers
!¬mutate-upstream-shared-Geometry→copy-first{copy_with_transform+identity}
!ax-GetNodePosition{never-drawn→ImVec2{FLT_MAX}}→is_valid_node_position-filter
!imgui-ini-untracked+gitignored-since-bb96806e{editor-runs-rewrite-it-freely;¬git-checkout-needed;rm-ini→procedural-default-layout-from-config/editor/default_layout.json}
!default-layout-iteration::edit-default_layout.json{no-rebuild}→rm-ini→relaunch;window-key=ImGui-TITLE¬ini_label{"$primary_viewport"-token-for-viewport};fraction=root-dockspace-relative
!DockBuilderDockWindow-on-closed-window::settings-only→empty-node-merged-away→wrong-place-on-open→layout-build-temporarily-shows-placement-windows-1-frame{still-imperfect-for-some,parked}
!clangd-new-file-diagnostics::false-positives-until-reconfigure{ninja-build=truth}
!MCP-execute_command-fallback::any-registered-command-callable-by-name{BUT-Hotbar.rotate-executes-without-switching-tool-headless}→gizmo-handle-visibility-via-set_gizmo_visibility-tool¬hotbar
!MCP-node-ids-differ-per-run{create_shape-returns-node_id→always-use-returned-id,¬cached-from-previous-run}
!MCP-load_scene-async-since-phase4{queued→poll-list_scenes}
!stale-editor.exe::locks-exe{headless-relink-LNK1168}+holds-port-8080→kill-before-build+launch
!incremental-build-stamp-stale{get_server_info-"built"-from-unchanged-TU}→check-exe-LastWriteTime
!PS5.1-embedded-double-quotes-in-native-args-mangled{git-commit--m-heredoc-splits-at-quote}→write-msg-to-file+git-commit--F
!smoke-suites::one-FRESH-editor-session-each{texture-material_output-resolves-material-via-get_single_scene_root→null-when->1-scene-open→section-fails}
!scene-save-exports-only-mesh-referenced-materials{graph-binding-on-unused-material=dropped+warn}→tests/users-bind-mesh-used-materials
!¬persisted::Brush_placement-attachments{brush-LIBRARY-persists-via-ERHE_brushes}+static-body-mass{KHR-no-motion-object-for-statics}→doc/scene_serialization.md-limitations
!import_root-wrappers-transparent-on-export{children-written-in-their-place}→roundtrip-diffs-need-parent_id+import_root-from-get_scene_nodes
!settings-less-joint-reload-materializes-Physics_joint_settings-item{"Physics joint 0"}→joint_settings-name-not-comparable-across-roundtrip
!animation-authoring-headless::animation_create_key-needs-EXISTING-target{set_animation_target;new-animation-only-via-ImGui-+Key-button}→import-animated-gltf-then-key
!MCP-create_shape-with-motion_mode-already-creates-rigid-body→edit_physics_body-for-field-tweaks{create_physics_body-errors-"already has"}
!Blender-stock-gltf-importer-rejects-2.1{externalAssets-scene}→foreign-tool-checks-use-prefab-free-2.0-save
!MCP-save_prefab-REMOVED{b1eecef0}→save_scene{path-optional:omitted→source-path||res/editor/scenes/<name>.glb;written-path=loaded-prefab→auto-reload-instances;explicit-path¬re-associates-scene}
!scene-with-source_path-saves-back-SILENTLY{¬overwrite-modal;open/load-sets-source_path;first-default-dir-save-associates}
!MCP-select_items-SCOPED-since-per-scene-selection{clears+selects-within-target-scene-only;ids=[]-clears-that-scene-only;activates-target-scene;materials=non-hosted→persist-across-scene-scoped-clears}
!commands-target-ACTIVE-scene{transform_selection/mesh-ops/Selection.delete→active-scene-selection-only;switch-via-set_active_scene||select_items;get_selection-reports-active_scene+per-item-scene_name}
!Operation_stack::queue=main-thread-only{async-worker-completions→queue_from_thread;drained-in-update}
!get_hosted_selection-returns-host-owned-scratch{cleared+refilled-per-call→copy-before-next-call-with-same-host}
