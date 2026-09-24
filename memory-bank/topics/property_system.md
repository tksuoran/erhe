§MBEL:5.0
©erhe::Topic::property_system
@scope::erhe::property dependency properties: node values, styles, folders, migrations, Properties window
@docs::doc/erhe/property_system.md+doc/erhe/property_inventory.md+doc/editor/properties_window.md+doc/editor/style_library.md+doc/plans/property_system.md

[STATE]
@node-attachments-to-properties::DONE-2026-09-22{P1-P11;PLAN-DOC-RETIRED{standing-text=doc/erhe/property_system.md-4.23{key-property+session-values+prefab-pairing+registering-a-group-recipe}+doc/erhe/item.md-Composition-arcs+doc/editor/scene_serialization.md-Native-carriers+doc/erhe/scene.md-Node-systems/Transform-observers};via-harness;UNPUSHED;P1-DONE-3ccaea2e1{erhe_property/attached_group.hpp:carries_attached_group+attached_group_visible_when+is_attached_property_listed;erhe_scene/node_system.hpp:INode_system+Scene::add_node_system{non-owning-list,recursive-mutex-held-during-dispatch}+node_system_property_changed+active-bit-forward-in-Xformable::handle_flag_bits_update;property-147,scene-154;docs=property_system.md-4.23+scene.md-Node-systems};P2-DONE-8b96a76c2{Draw_mode=static-holder+Draw_mode_system-on-Scene_root;key=Draw_mode.apply_draw_mode;non-key-write-sets-key;applied-schema-attachment-registry-deleted;link_carrier_values_to_target;Item_type-bit-56-gone;glTF-carries-draw-modes-via-ERHE_node.properties;usd-405}+P3-DONE-fc31543e7{Layout=static-holder-in-erhe::scene;key=Layout.type-default-none;Layout_system-member-of-Scene-owns-solve,no-steady-alloc;ERHE_layout-DELETED;Create-Layout-menu+F6-GONE->Add-Property-Layout.type;Item_type-bits-36/37-gone-renumbered;scene-158;roundtrip-468/465}+P4-DONE-7e7e71337{Brush_placement=3-unserialized-node-values;key=Brush_placement.brush-STRONG-ref;clones-now-carry-placement{Dependency_object-copy-ctor-copies-all-locals};set-by-reference-path-not-bare-name}+P5-DONE{Xformable::add_transform_observer->Transform_observer_token{erhe_scene/transform_observer.hpp};Frame_controller+Four_view-links=plain-objects-owned-by-Fly_camera_tool/Four_view;scene-167}+P6-DONE-141877437{Grid=item-owned-by-Grid_tool;Grid.frame_node-weak-ref-session-state;transform-observer;MCP-resolve_item-finds-grids-by-id/name;smoke-59/59;!coder-DELETED-user's-gitignored-imgui-ini-to-pass-layout-sensitive-Mcp_-tests->briefs-now-forbid-it}+P7-DONE-40d199a07{Geometry_graph_mesh.graph_mesh-key-strong-ref-not-serialized{native-carriers-fed-from-record};Geometry_graph_mesh_system-on-Scene_root;clone-keeps-binding;Item_type-bit-40-gone}+P8-MAIN-DONE-beb405148{Node_physics=static-holder;key=Node_physics.motion_mode-default-Motion_mode::e_none{renamed-from-e_invalid};Node_physics_system-on-Scene_root{entry=shape+body+mirror;shape-outlives-body-for-undo/redo;default-shape=convex-hull-else-0.5-box;key-WRITE-wakes-body};MCP-get_node_details.physics;FIXED:Brush::make_instance-sealed-before-writing-values{floor-had-no-body;was-the-startup-'lightmapped: object is sealed'-error}+Geometry_graph_mesh_system::release-UAF-on-adopted-mesh{P7-regression};took-3-coder-passes{~35-sites-too-big-for-one-context->split-big-phases-up-front};physics-34,box3d-85,sweep-16/16-both,roundtrip-468/465}+P8b-DONE{69d514288-get_node_details-by-id+MCP-set_log_levels+geometry_nodes_smoke_test-136/136|7743095c7-ERHE_physics-DELETED{editor::motion_state_of-projection;kinematic-modes-ride-ERHE_node.properties}+Node_physics-native-USD-names{physics.usda-second-save-byte-identical,clock-paused}}+P9-DONE-5cf5ab676{editor::Joint-typed-prim-Imageable;Joint.body_0/body_1-weak+joint_settings+enable_collision;Joint_system-on-Scene_root-via-Item_host::register_prim;Item_type-bit-26-physics->joint;MCP-create_physics_joint/edit_physics_joint-RENAMED-create_joint/edit_joint;get_node_details.joints;USD-rel-erhe:Joint:joint_settings;FOUND-preexisting:six-dof-all-axes-limited-min==max-does-not-weld}+D4-DECIDED-2026-09-22{(c)User-chose-option-3:unique_ptr<vector<Composition_arc>>-on-erhe::Typed{null=not-carrier;any-prim-type;clone-deep-copies};rejected:Xformable-vector{Scope/Material-never-carriers}+host/scene-side-table{template-trees-unhosted+clipboard-clone-unhosted+redo-needs-entry-past-scene-membership=Resource_metadata-trap}+flag-bit+table{two-sources-of-truth;Hierarchy-row-draw-is-per-frame-so-flag-speed-matters};plan-1db018267}+P10a-DONE-90ba6dd4c{Composition_arc{source_path+prim_path+name+kind+variant_selections};Typed::m_composition_arcs-unique_ptr<vector>-null=not-carrier;copy-ctor-deep-copies;Typed.composition_arcs-computed-visible_when-has_arcs;Item_type-bit-40-kept-until-P10b}+FOUND+FIXED-b900c8bfe{P9-Joint-prim-collided-with-xr/hand_tracker.hpp-editor::Joint->Hand_joint;only-OpenXR-trees(ninja)-broke,headless-tree-has-xr=none}+P10b-DONE-2a239b966{instance_structure-owns-carrier-vocabulary{get_instance_arcs/get_first_instance_arc/is_instance_carrier/is_sealed_instance_carrier/get_outermost_prefab_instance_node};MCP-get_node_details.composition_arcs;TRAP:roundtrip-total-468-needs-ERHE_USDCHECKER=<usd_root>/scripts/usdchecker.bat-else-466{2-usdchecker-checks-skipped}}+P11a-DONE-e27c8197b{editor-half;Icon_set::get_feature_icons{carries_<x>-per-visible-row-per-frame;flatten-time-read-was-stale:row-cache-rebuilds-on-item-serial-not-property-writes};create_child_prim-MCP;TRAP:Mcp_-failing-set=layout-dependent{69/73-here,pristine-same};LEFT:scripts/texture_graph_smoke_test.py-attachment_types-branch-dead+material-lookup-broken;mcp_tools.json-stale-'attachment'-wording-in-set_item_flags/get_item_properties;MDI-glyphs-BRUSH/LINK-outside-loaded-range-render-blank}+P11b-DONE-6a977a0f4{library-half;bit-28-unused}+ALL-PHASES-LANDED-2026-09-22;P11c-DONE{plan-retired};NEXT=prompt_queue.txt-item-0=rigging-interactive-pass{USER-DRIVEN};Mcp_-71/72-failing-case-is-LAYOUT-dependent{gizmo-drag|property_row_is_addressable_by_its_label};FOUND-preexisting:Mcp_test.mouse_drag_on_a_transform_handle_moves_the_selection-fails-at-fc31543e7-already{uninvestigated};?user-interactive{Add-Property-Layout.type;Draw_mode-rows};}
@hand-written-rows-to-properties::DONE-2026-09-20{10-commits-a339719c0..ab26a8cd1;UNPUSHED;via-harness;both-plan-docs-removed,standing-text=doc/erhe/property_system.md-4.13+4.20+4.21+4.22+D34+D35;inventory-Not-yet-migrated=EMPTY;Scene::ambient_light_property{vec3;mirror;ERHE_scene+erhe:scene-gain-properties+style}|Layout::grid_track_extent_x/y/z{float_array;coerce-to-track-count;re-coerced-in-on_property_changed;array-row=per-element-drag-up-to-16;"Custom track sizes"-row-actions}|Property_type::string_array=19+Property_ui::Array_size{fixed|editable}|Collision_filter-3-lists{string_array,inherits=false,native_gltf;Node_physics-observes-filter->reapply}|Physics_joint_settings=66-per-axis-props{trans_x..rot_z-x-11;Joint_axis_limit{free|limited}+Joint_axis_drive{off|force|acceleration};Joint_limit/Joint_drive/Drive_type/Drive_mode-GONE;mirrors=Constraint_axis_limit/drive-arrays;H6-child-items-OVERTURNED:consumer+backends+USD-already-fixed-6-axes;Node_joint-observes-settings,rebuilds-only-on-Physics_joint_settings-owned-props{name/visible-must-not-rebuild};Rebuild-Joint-button-STAYS=re-capture-frames-after-moving-nodes;ERHE_scene.physics_joints{name+local-set};USD-native_usd_property_name-lists-them,join_limits-gone};CLOSED:physicsJoints[].limits-re-import-parse-failure-no-longer-reproduces;LEFT{plans/property_system.md}:USD-save-states-effective-values->folder/style-held-physics-value-reloads-local;tests:property-141,scene-149,physics-34{box3d-85},usd-405,roundtrip-464/467{2-known+P6-flake};sweep-16/16-both-backends;TRAP:MCP-open_scene-on-.glb=foreign-path,use-load_scene;?user-interactive{Scene-Ambient-Light-row-VERIFIED;Layout-array-rows+Custom-buttons;Collision-filter-Add/-/text;joint-axis-groups+visible_when+degrees+live-edit-no-rebuild}}
@style-library::doc/editor/style_library.md{R1-R6,D1-D5}✓2026-09-04{9132674f2+a88dd405c+persistence+docs}
  D1::style-source=any-Dependency_object{local-values=style;m_style_users-registry;deliver()->propagate_to_style_users;Property_style=Dependency_object-subclass}
  D2::editor::Style-item{content_library/style.{hpp,cpp};secondary-owner=target-class;Item_type::style=bit47;Styles-category-folder;icon}
  D3::Item_base::style_property{bridged-object-ref;validate-target-vs-owner-chain-or-secondary;flags-partition+variant+serialize}
  D4::ERHE_scene.styles{name,target,properties}+ERHE_material.style+library_folders.style;import order:styles→...→material-styles→folders
  D5::add_default_materials→"Brushed metal"-Style-item;make_style_from_values{paste-as-style+MCP-set_item_style}
  verify✓headless{scratchpad-verify_styles.py:live-edit,clear/reassign-by-name,folder-style-inherit(after-clearing-local),paste-as-style+undo,save/open,close-clean}|?user-interactive
@node-holds-attachment-values::D30-generalized✓2026-09-04{ff0f28de6+62b80d7fd+8d1b12a57;user-report:"cannot add light properties to empty node / to style"}
  D30::secondary-covers-descendant-types{is_secondary_property:secondary|ancestor|descendant;¬bridged+¬computed;deliver-skips-property_changed-metadata-callback-on-holder{callback-casts-to-registering-class→was-UB}}
  Node::secondary=Node_attachment{offers-Light.*/Camera.*/...;Light-props-all-inherits}|Item_base::style_applies{target-on-chain||target-descends-from-object-secondary}|candidates-filtered
  style-any-class::©User-chose-2026-09-04{"still cannot add light or camera properties to style"}→Style-secondary=root_owner_type{holds-every-class;applies-to-every-item;no-target;ERHE_scene.styles={name,properties};Create-Style-plain-item;MCP-create_style{name}}|listing-rule=has_own_value{style-provided-secondary-listed;x-clears-local-only}
  camera-migrated✓::©User-asked{"all camera properties placed into empty node and/or style"}→every-Camera-prop-entry-store+inherits;projection()=const-mirror{refresh_projection_mirror-in-on_property_changed};writers=setters/set_projection;ERHE_camera.properties=complete-local-set;content-fit-widens-LOCAL-z_far/shadow_range-only{style-assigned-after-fit}
@physics-material-only-carrier::©User-asked-2026-09-04→Node_physics-friction/restitution-removed{+create-info+ERHE_physics+MCP-args};body-without-material=material-defaults{c_default_friction/restitution;Jolt-creation};Physics_material-props-inherits;Create-Physics-Material-menu{Styles/Physics-Materials-folder-context};no-old-asset-migration
  ERHE_scene.physics_material_names+collision_filter_names::KHR-entries-nameless{fastgltf-fork-parses-none}→names-by-index;fixed-"Physics material N"-on-reload
  trap::MCP-edit_physics_body-NOT-undoable{undo-after-it-pops-previous-op}
  trap-fixed::for_each_local_value-bridged-list-unsorted{root-first-visit;style_property-on-Item_base-exposed-it;Node_properties-test-failed-since-a88dd405c}
  persistence::ERHE_node.style{Item_style_by_name_operation}+ERHE_light.properties=complete-local-set{loader-clears-unlisted-KHR-baked-values→light-keeps-inheriting-after-reload}
  trap-hit::Mesh.world_bounds_*-computed-listed-as-secondary-on-node→compute(node)-cast-to-Mesh→crash{fixed-by-¬computed-rule}|find_scene("")¬first-scene{create_library_folder-schema-claims-default}
  verify✓headless{scratchpad-verify_light_inherit.py;doc/editor/style_library.md-step-6}|?user-interactive{Create-Style>Light,Add-Property-on-empty-node}
@folder-category-properties::D30{secondary-owner-type}✓2026-09-04{5ff73f41b+59dd60042+34d66432b}
  Content_library_node.category_owner_type→get_secondary_property_owner_type{folders-only};Material-value-props-inherits=true;Add-Property-offers-"Material.<name>"-on-Materials-folder;listing=is_extra_property_listed+collect_addable_properties
  trap::Material-visible_when-lambdas-static_cast-to-Material→NEVER-evaluate-on-a-folder{secondary-listed-by-local-value-only}
  limitation::material-own-values-are-local{Reset-to-default-lets-folder-through};glTF-export-bakes-effective→local-on-reload{folder-value-persists}
  texture-slots::entry-store+inherits{db69e84fc;Material::on_property_changed-mirrors-effective-value-into-Material_data;set_data/create-info:default-field=unset,else-local;folder-texture-ref-resolves-via-Content_library_node::resolve_expression_object;find_scene_root_for_item-knows-library-nodes}
  verify✓headless{scratchpad-verify_folder_material.py:addable→set-folder-red→clear-Copper→inherited→undo/redo→remove→save/open→close-clean}|?user-interactive
@content-library-folders::doc/editor/content_library_folders.md{R1-R6,D1-D7}✓2026-09-04{5-commits-48518c02f..7993068f6+follow-ups}
  D1::Item_base.m_inheritance_container{set-by-Content_library_node::handle_add_child;cleared-in-dtor;for_each_inheritance_child-visits-item;reference-entries-never}
  D2/D3::Create-Folder-menu{scene_root.cpp}+drag-onto-folder{Content_library_move_operation}
  D5/D6::ERHE_scene.library_folders{path+properties+items;brushes-folder_path-read-only}+Content_library_folders_operation{last-in-import_gltf_editor_state}
  D7::MCP-create_library_folder+move_library_item{folder_path,undoable}+find_item_in_scene-visits-library-nodes
  trap-fixed::add/remove-scanned-direct-children-only→duplicate-node-per-item-after-folder-move{find_entry-subtree;get_all-caches-cleared-up-to-root}
  verify✓headless{scratchpad-recipe:create→move-Copper→set-visible-false→inherited→undo/redo→save/open→local+inherited→close-clean;undo_reference_clearing_smoke_test-45/45}|?user-interactive{Create-Folder,rename,drag-drop,Ctrl+Z}
@property-system::erhe::property{doc/erhe/property_system.md=design-record;doc/erhe/property_inventory.md=per-field-status;doc/erhe/property.md=library-reference}
  >2026-09-04::Add/Remove-Property-UI{940aeccbb..3a2318199}✓|?user-interactive
@node-physics-entry-store::✓2026-09-04{a5233529c+901dab96a}
  split::©User-chose{"each property: node physics (unlikely) or physics material (likely)"}→Physics_material=kind-of-matter{+linear_damping+angular_damping+wind_receptivity+density;Jolt-snapshot-applies-damping+density-mass-when-no-explicit-mass}|Node_physics=instance{motion_mode,is_trigger,mass,com,velocities,gravity_factor,material+filter-refs;ALL-entry+inherits;create-info+m_motion_mode=mirrors;ctor-sets-local-only-where-differs-from-default;mass-source-default=density-mass}
  gltf::ERHE_physics={motion_mode,properties}|ERHE_scene.physics_materials=[{name,properties}]{replaces-physics_material_names}|Gltf_data.unresolved_object_properties→Item_object_property_by_name_operation{node-held-object-refs-survive-reload;erhe_gltf-records,editor-resolves-late}
  verify✓headless{scratchpad:verify_material_move*.py+verify_np_inherit.py}|?user-interactive
@interactive-check-fixes::✓2026-09-04{900328b01:attached-holder-type{register_attached(name,owner,HOLDER,...);applies_to;for_each_attached_property_of;Layout-hints-held-by-Node-only}|b8cea9cd0+34cac3e24:multi-selection=per-owner-type-sections{library-entry→its-item;nodes+their-attachments;scratch-cleared-after-use}|9c4ff5327:shadow_cast+lightmapped=Mesh-properties{Item_base-keeps-visible;node/style-hold-Mesh.shadow_cast-via-Add-Property;legacy-flags-apply-to-meshes-only}}
  by-design::own-class-properties-always-rows{Add-Property-offers-only-other-class-values};Style-row-on-every-item
@writable-computed::D26-extended✓2026-09-05{56615421c:register_computed(compute,set,writes)→compute_set+compute_writes;set_value→setter;clear/expression-rejected;editor-records-Property_set_operation-on-`writes`{make_computed_write_operation+rows.recorded_property};MCP-get_item_properties.writes;Light.flux{writes-intensity}+Light.blackbody{read-only};light_properties=zero-range-warning-only}
@property-migrations::✓2026-09-05{7-commits-56615421c..c8146b473:Light-flux/blackbody{writable-computed}+Layout{entry+mirror+ERHE_layout-properties=complete-local-set}+Grid{entry+mirror;read_config-through-store}+Brush_placement{entry;brush-validated}+Rendertarget_mesh{computed-size,own-owner-type}+Animation{computed-range/counts;notify_keyframes_changed}+Node_joint{connected_node=BRIDGED-weak{no-node-cycle};settings+enable_collision-entry-inherit}}
  lookup-fixes::find_item_in_scene-visits-brushes+animations+physics_joints{by-name-refs-resolved-to-library-entry-node-before}
  left::graph-node-parameters{doc/erhe/property_system.md-section-6;recipe=section-4.18;handoff-doc-deleted-2026-09-05;only-when-user-asks}
@properties-window-single-path::step-1✓2026-09-05{89986a2b3+7789f1332:Item_base-name/tags-string-bridges+authored-flag-bool-bridges{Locks-group;developer_only-for-obscure-bits};Property_flags::writable_when_sealed{lock_edit-owns-seal;Dependency_object::is_write_sealed(property)=per-property-check-in-rows/context-menu/MCP-set_item_property};window-Name/Locks/flag-grid+material-Name-row-removed;dev-mode-Flags=read-only-to_string}
  verify✓headless{scratchpad-verify_item_rows.py:name/tags/lock_edit-set+undo;sealed-refuses-name+accepts-lock_edit=false}+erhe_item_tests-129/129|?user-interactive{multi-select-per-type-sections,Locks-checkboxes}
  modes✓{9d989e29a:Individual|Combined-combo-left-of-Pin{default-combined;single-item=individual};combined=per-type-sections-only;mixed=per-component{mixed_mask+merge_components+drag_components;"mixed"-placeholder-format;bool=ImGuiItemFlags_MixedValue;quat/string/enum/object=whole}}|?user-interactive{drag-one-component→only-that-component-on-every-item;one-undo-entry}
  step-3✓{7c5e2d9f7+6f43e57e0:Material_sampler_state{plain-data;defaults=Sampler_create_info}+35-entry-props{<slot>_texture_wrap_u/v,min/mag_filter,mipmap_mode,max_anisotropy,lod_bias(dev)};Material_sampler_cache-in-Material_buffer{state→Sampler;replaces-fallback};no-Sampler-on-materials{gltf-import/export,texture-nodes,rendertarget,MCP-edit_material-device-free};inspect-snapshot+m_material_state-gone}
  step-4✓{4cb30bd4a:Brush::material_property+Geometry_graph_mesh::graph_mesh_property{register_member;after_set=release+apply};item_properties=frame+item_diagnostics;R5-list=joint-limits/drives+collision-filter-lists+layout-track-extents+scene-block}
  DONE::item-0-removed-from-queue-2026-09-05|?user-interactive{material-sampler-rows,brush-material-row,graph-mesh-row}
✓capture_inheritance_snapshot-cost-FIXED-2026-09-21{6d6960261+a3cd73b22;supplied-properties-of-the-two-ancestor-chains-only}

[OPEN]
?startup-log-error::"property 'mass': value rejected by validate callback"{pre-existing,unrelated-to-M1/M2}
?user-interactive-check{folders+category-props+texture-slots+styles+node-attachment-values+camera+physics-materials}→expect-fixes;then-migrations{Node_physics-first}
?material-STYLE-values-reload-as-local::native-glTF-fields-carry-effective-value{only-ERHE_material-complete-local-set-fixes;doc/plans/gltf_properties_extension.md-steps-2+;reflectance-has-no-glTF-carrier}
?inherits-registration-check{doc/erhe/property_system.md-section-6}
?Light-derived-rows→Rendertarget_mesh→Animation{doc/erhe/property_inventory.md}

[PROGRESS]
[TASK::node-attachments-to-properties]{DONE-2026-09-22;via-harness;P1-P11-landed}
✓P1{3ccaea2e1:key-property-groups+node-systems;property-147,scene-154}
✓P2{8b96a76c2:Draw_mode}
✓P3{fc31543e7:Layout;ERHE_layout-deleted;scene-158;roundtrip-468/465}
✓P4{7e7e71337:Brush_placement}
✓P5{transform-observers;Frame_controller+Four_view_link;scene-167}
✓P6{141877437:Grid}
✓P7{40d199a07:Geometry_graph_mesh}
✓P8-main{beb405148:Node_physics+Node_physics_system}
✓P8b{69d514288+7743095c7:ERHE_physics-deleted}
✓P9{5cf5ab676:Joint-prim+Joint_system}
✓D4-decided{1db018267;record-on-Typed,option-3}
✓P10a{90ba6dd4c:Composition_arc+Typed-unique_ptr-storage+computed-Typed.composition_arcs+instance_override-null-check;item-195}
✓openxr-build-fix{b900c8bfe:hand-tracking-Joint->Hand_joint;P9-collided-with-Joint-prim-in-OpenXR-trees-only}
✓P10b{2a239b966:editor-on-record;Prefab_instance+bit-40-deleted;refresh-re-authors-whole-arc-list;usd-405/scene-167/item-195;roundtrip-466/463=468-minus-2-usdchecker-checks{ERHE_USDCHECKER-unset};smoke-59}
✓P11a{e27c8197b:editor-half;feature-icons-from-key-properties-per-visible-row;attachment_types->child_prim_types;MCP-add_node_attachment->create_child_prim,remove_node_attachment-gone;get_node_details.attachments-gone;roundtrip-468/465;Mcp_-69/73=pristine-same-layout}
✓P11b{6a977a0f4:Node_attachment+attach-API+Node_data.attachments+Item_type-bit-28-deleted;for_each_inheritance_child-override-gone;find_override_property_target=item-alone;suites-at-baseline;roundtrip-468/465;sweep-16/16}
✓P11c{plan-doc-deleted;standing-text->property_system.md-4.23+item.md+scene_serialization.md-Native-carriers;citations-rewritten}-delete-infrastructure;handoff=prompt_queue.txt-item-0+plan-section-'How the remaining phases are worked'

[TASK::hand-written-rows-to-properties]{DONE-2026-09-20;10-commits-a339719c0..ab26a8cd1;detail=activeContext;?user-interactive{layout-array-rows,collision-filter-lists,joint-axis-groups}}

[TASK::style-library]{DONE-2026-09-04}
✓style-source-generalization{9132674f2}+Style-item+style-property{a88dd405c}+persistence+docs
✓node-holds-attachment-values+Light-inherits+Create-Style/create_style+node-style-persistence{ff0f28de6..8d1b12a57}
✓style-holds-any-class{no-target}
✓Camera-props-entry-store+inherits{node/style-hold-Camera.*}
✓physics-material-only-friction-carrier+names-persist{ERHE_scene}
?user-interactive-verify

[TASK::content-library-folders]{DONE-2026-09-04}
✓drop-fix{adc3b676f}+category-properties-D30{5ff73f41b..34d66432b}+texture-slots-inherit{db69e84fc..9431513f4}
✓D1-inheritance-link{48518c02f}+UI{ac5126944}+ERHE_scene.library_folders{e0704b0f0}+subtree-dedup-fix{9e768b4e0}+MCP{7993068f6}+docs
?user-interactive-verify

[TASK::Add/Remove-Property-UI]{DONE-3a2318199-2026-09-04}
✓shared-listing-rule{940aeccbb}+Add-Property-row{93153dcf9}+Remove-Property{5ea6655e8}+rows-m_items-leak-fix{4e496b984}+MCP-tool{75ca1498c}+docs-D12/D13{3a2318199;plan-doc-deleted}
?user-interactive-verify

[TASK::properties-window-single-path]{DONE-2026-09-05}
✓step-1{89986a2b3+7789f1332,2026-09-05}::name/tags/flag-bridges+writable_when_sealed;headless-verified
✓modes+per-component-mixed{9d989e29a}|?user-interactive
✓step-3{7c5e2d9f7+6f43e57e0}::sampler-state-properties+Material_sampler_cache+snapshot-retired
✓step-4{4cb30bd4a}::brush-material+graph-mesh-properties;item_diagnostics-frame
?user-interactive-verify

[TASK::property-migrations]{DONE-2026-09-05}
✓Material+Node+Light+Camera{entry-store-since-2026-09-04}+graph-nodes+Mesh_primitive+Node_physics+Grid+Brush_placement+Physics_material+Layout+Layout-hints{attached}
✓Node_physics-entry-store+material-carries-damping/wind/density{a5233529c+901dab96a,2026-09-04}
✓interactive-fixes{holder-type+per-type-multiselect+Mesh-owned-shadow_cast/lightmapped;900328b01..9c4ff5327}
✓Light-derived-rows{56615421c}+Layout{90ab4c97b}+Grid{07be30baa}+Brush_placement{65620bed4}+Rendertarget_mesh{11897628c}+Animation{526f35383}+Node_joint{c8146b473};2026-09-05;all-headless-verified
?graph-node-parameters::future-work{doc/erhe/property_system.md-section-6;only-when-user-asks}
?user-interactive-verify{Light-flux-slider-undo;Layout/Grid/Brush_placement-holders;joint-rows}
