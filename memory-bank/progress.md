§MBEL:5.0

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
?graph-node-parameters::future-work{doc/property-system.md-section-6;only-when-user-asks}
?user-interactive-verify{Light-flux-slider-undo;Layout/Grid/Brush_placement-holders;joint-rows}

[TASK::usd-compatibility]{DONE-2026-09-06,via-doc/agent-orchestration-harness.md}
✓mapping-doc-rewrite+plan-doc{d25e505e4}+three-stage-goal{489f2c7a6}+anim/physics->future-work{46ce31a3c}
✓M1-item-paths{4d400211e;143-item-tests;headless-MCP-by-path-verified}
✓M2-sibling-unique-names{156-item-tests;headless:create_node-dup→Marker_1;hintze-hall-import-7-dups→suffixed;undo-restores;rename-refused}
✓roundtrip-baseline{70abffecd..4f7a7a45e;scene_roundtrip_verify.py-93/93;Khronos-validator-not-installed→section-skipped}
✓M3-purpose-vocabulary{5fe65f45d+7eb07c8c2;163-item+93-property-tests;screenshot-identical-except-status-bar-digits;roundtrip-93/93}
✓L1-LightUSD-optional-CPM{6f8739fea..bfbdc6ff8;MSVC+clang-cl+headless-green;describe_usd_file-on-usda/usdc/usdz}
✓I1-import-USD-asset{commit-1-a3aa83732-lib+7-usd-tests;commit-2-87ee48935-editor-import{headless:suzanne-pbr+parity-subset-textured;undo+close-clean;roundtrip-93/93};commit-3-import-failures+prim-filter+smoke-test-USD-leg}
✓M4-local=authored{100-property+67-scene+39-primitive-tests;headless-default-material-78-defaults;fresh-session-reload-source-diffs-none;roundtrip-94/94}
✓I2-authored-opinions{14-usd-tests;headless:authored.usda-visible/purpose/base_color-only/temperature-custom-attr;suzanne-local-set=base_color+metallic;roundtrip-94/94;smoke-52/52}=G1
✓E1-save-USDA{1d51c13a5-writer+29-tests;c1a32c475-editor{headless:suzanne-open/edit/save-51-lines/close-clean/reopen-edits-back;gltf-still-gltf;roundtrip-94/94}}=G2
✓E3-roundtrip-USD-leg{53d54294d;148/148;usdchecker-SKIP-not-installed}
✓Q1-Quest{build-593691fb5;headset-launch+describe_usd_file-10-prims-verified-2026-09-06}
?user-interactive-verify{asset-browser-Import/Load-scene-on-USD;Save-Scene-on-USD-scene;Properties-purpose-row}

[TASK::usd-object-model]{U1-U4,NEXT}
✓plan-revised-2026-09-06{C5-unified-prim-tree;doc/usd-compatibility-plan.md-section-3-U1..U4;section-4-order}
✓U1-prim-class-hierarchy{commit-1✓7986a326f:erhe::item-Typed(type_name-bridged-property;get_class_type_name-fixes-token)+Scope(root_owner_type-secondary);169-item-tests|commit-2a✓e635b27b6:Imageable/Xformable(=Node-alias,static_type_name-Xformable,bit-index-20)/Xform/Boundable/Gprim;113-creation-sites->Xform;74-fwd-decls=class-Xformable+using-Node;levels-below-Xformable-clone-via-(src,for_clone);no-icons-for-level-bits{draw_icon-draws-per-set-bit};item169/scene71/property100;viewport-pixel-identical|commit-2b✓d84968ad3:get_parent_node=walk-to-nearest-Xformable{not-cached};Typed::handle_parent_update+virtual-handle_item_host_update{host-carried-down-subtree;Xformable-overrides-for-scene-registration};Scene::update_subtree_transforms(Hierarchy&)-recurses-through-Scope;editor-lookups-walk-tree{find_prim_in_scene,find_items_by_ids,get_scene_nodes};create_node.prim_type=Xform|Scope;index_scope=49<index_typed=50{icon-picks-lowest-bit};transform_selection-refuses-Scope{generic-msg};scene75|commit-3✓b7e902fb3:import-dispatch-on-typeName{Model-prim=authored-token;shading-Scope-stays-namespace-until-U4;Typed-with-authored-xform=dropped+warn};export-typeName-per-class;Usd_data.prims+Gltf_data.prims{index-parallel};ERHE_node.prim_class/prim_type_name;usd-tests-38;roundtrip-152/164{12=save>5s-MCP-k_request_timeout;content-checks-all-pass}}
  U1-DONE-2026-09-06
  left-after-U1::collect_reference_candidates+Layout-do-not-reach-through-Scope;Create-menu-Scope-entry
✓U2-Mesh:Gprim{befec879f,2026-09-06:Mesh=Item<Item_base,Gprim,Mesh>;get_node()=this{transitional};get_mesh/for_each_mesh_child/set_mesh_parent{keeps-LOCAL-transform};Xformable::handle_transform_update-virtual;Xformable-secondary-owner=Item_base{D30};glTF-node+mesh<->Mesh-prim{mesh-entry-not-uid-stamped};3-latent-bugs-fixed{graph-mesh-release,excluded-mesh-export,lock_edit-seal-before-values};scene79/usd41/roundtrip164/smoke52;viewport-identical;UI-residue->U3}
✓asset-browser-refresh_file{af689d5c8;unblocks-roundtrip-script}
✓U3-Camera/Light:Xformable{commit-1✓d5f0be1a8:Camera/Light=Item<Item_base,Xformable,X>;get_node()=this;set_prim_parent(Xformable,parent){keeps-LOCAL;set_mesh_parent-delegates};get_camera/get_light;glTF-rule=node-with-two-of-mesh/camera/light->prim-of-first(mesh>camera>light)+others-as-child-prims;camera-entry-not-uid-stamped;Brush_preview-fill-light-bug-fixed;scene82/usd42/roundtrip164/smoke52|commit-2✓7fd9f447d:get_node()-removed-from-Mesh/Camera/Light{~165-sites-address-prim};Attachment_kind{child_prim|api_schema}='Add Child Prim'/'Add Attachment';DnD-payload=prim-class-name{U1-had-broken-DnD:literal-'Node';fixed;interactive-gesture-unverified};get_node_details.mesh/camera/light-on-node-entry;scene82/roundtrip164/smoke52;viewport-identical-99px-statusbar}
  U3-DONE-2026-09-06
⚡U4-resources-are-prims{L;commit-1✓cf19e2608:every-library-kind=Item<Item_base,Typed,X>{tokens=class-names;Material='Material'};Hierarchy::get_inheritance_parent/is_name_available-fall-back-to-Item_base-when-parentless{container-inheritance-kept};Selection-delete/duplicate-require-parent;Graph_asset-keeps-set_item_host-override{single-choke-point};item171/scene84/primitive42/roundtrip164/smoke52|commit-2a✓Item_host::register_prim/unregister_prim+Typed-single-call-site{item176}|2b✓per-kind-lists+by-item-map+serial{fed-by-Content_library_node-add/remove-child-hooks;Asset_key::library_folder-removed->kind-type-bit;kind-routed-make/add/add_reference/remove;add-dedups-library-wide{tightening};Content_library::add-has-bool-is_reference{retires-in-2c}}|2c✓resources-are-prims-under-lazy-kind-Scopes{no-content-flag=glTF-node-writer-skips};Content_library_node+category-roots-gone;Content_library:Item_host{palette};Resource_metadata-table-weak_ptr-keyed{survives-undo/redo};referenced-resources=index-entry-no-placement;D1-container-link-gone{tree-parent-inherits};roundtrip-script-drop_library_prims();item177/roundtrip164/smoke52;DnD-of-resource-under-Xform-not-reachable-from-UI-yet|2c-was:resources-into-tree{kind-Scopes-lazy;Content_library:Item_host-for-palette;metadata-table-keyed-by-resource}|2d✓1b5722576:make_library_attach_operation->Item_insert_remove_operation{bookkeeping-outside-op};Content_library_move_operation->Item_parent_change_operation{any-prim-drop-target;move_library_item-same-kind-refusal-only-under-other-kind-scope};create_material-undoable;Create-menu-Scope;duplicate-skips-not_clonable;Selection::end_selection_change-no-op-change=no-message;Item_insert_remove_operation-reports-retained-prims-as-refs;material-under-Xform-does-not-survive-save-yet{2f}|2e✓:reference-listing-retired{Material_set-per-object-membership-IS-the-referenced-source;fed-by-enqueue_mesh_materials+sync_object_materials-change-sites};index=owned-only;definition-container=manager-record{Scene_root::is_asset_definition};prefab-instance-materials-render-with-no-entry{screenshots-identical};exporter-scans-meshes-per-save-for-proxy-candidates|2e-was:asset-manager+reference-listing{referenced-list=single-source-for-Material_set::sync_library;prefab-reference-entries-retire}|2f?glTF-carrier-from-tree+UI+scripts+docs|commit-3?USD-placement}
  traps::save_usda-writes-/Materials-from-index{would-duplicate-kind-scope};Xformable::node_sanity_check-static_casts-Item_host->Scene_host{palette-prims-must-not-reach};kind-scopes+resource-prims-carry-no-content-flag{glTF-node-export-skips}


[NOTES]
!headless-recipe::build_vs2026_vulkan_headless-editor→ERHE_AI_DRIVER=1-launch-hidden→mcp_call.py-b64-args{get_item_properties/set_item_property/get_addable_item_properties/undo;ids-reshuffle-per-launch;scene_name-required-for-create_node/select_items/get_node_details}
!default-scene::floor-mesh-sealed{lock_edit}→use-cube-attachment-for-attachment-tests
!scene-close-check::close_scene→wait≈6s→grep-"scene-close"{clean="all N released"}
!clangd-db::re-run-configure_ninja_win_clang.bat-after-adding-source-files{done-2026-09-04}
