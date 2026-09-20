§MBEL:5.0

[TASK::hand-written-rows-to-properties]{DONE-2026-09-20;10-commits-a339719c0..ab26a8cd1;detail=activeContext;?user-interactive{layout-array-rows,collision-filter-lists,joint-axis-groups}}

[TASK::fly-camera-wheel-towards-hover]{DONE+USER-VERIFIED-2026-09-20;8125c035c;detail=activeContext}

[TASK::ortho-view-grids]{DONE+USER-VERIFIED-2026-09-20;33f67dfd9;detail=activeContext;left=screen-space-labels{doc/plans/editor.md}}

[TASK::ortho-camera-gizmo]{DONE+USER-VERIFIED-2026-09-19;4-commits;detail=activeContext}

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
✓plan-revised-2026-09-06{C5-unified-prim-tree;doc/erhe/usd_compatibility_design.md-section-3-U1..U4;section-4-order}
✓U1-prim-class-hierarchy{commit-1✓7986a326f:erhe::item-Typed(type_name-bridged-property;get_class_type_name-fixes-token)+Scope(root_owner_type-secondary);169-item-tests|commit-2a✓e635b27b6:Imageable/Xformable(=Node-alias,static_type_name-Xformable,bit-index-20)/Xform/Boundable/Gprim;113-creation-sites->Xform;74-fwd-decls=class-Xformable+using-Node;levels-below-Xformable-clone-via-(src,for_clone);no-icons-for-level-bits{draw_icon-draws-per-set-bit};item169/scene71/property100;viewport-pixel-identical|commit-2b✓d84968ad3:get_parent_node=walk-to-nearest-Xformable{not-cached};Typed::handle_parent_update+virtual-handle_item_host_update{host-carried-down-subtree;Xformable-overrides-for-scene-registration};Scene::update_subtree_transforms(Hierarchy&)-recurses-through-Scope;editor-lookups-walk-tree{find_prim_in_scene,find_items_by_ids,get_scene_nodes};create_node.prim_type=Xform|Scope;index_scope=49<index_typed=50{icon-picks-lowest-bit};transform_selection-refuses-Scope{generic-msg};scene75|commit-3✓b7e902fb3:import-dispatch-on-typeName{Model-prim=authored-token;shading-Scope-stays-namespace-until-U4;Typed-with-authored-xform=dropped+warn};export-typeName-per-class;Usd_data.prims+Gltf_data.prims{index-parallel};ERHE_node.prim_class/prim_type_name;usd-tests-38;roundtrip-152/164{12=save>5s-MCP-k_request_timeout;content-checks-all-pass}}
  U1-DONE-2026-09-06
  left-after-U1::collect_reference_candidates+Layout-do-not-reach-through-Scope;Create-menu-Scope-entry
✓U2-Mesh:Gprim{befec879f,2026-09-06:Mesh=Item<Item_base,Gprim,Mesh>;get_node()=this{transitional};get_mesh/for_each_mesh_child/set_mesh_parent{keeps-LOCAL-transform};Xformable::handle_transform_update-virtual;Xformable-secondary-owner=Item_base{D30};glTF-node+mesh<->Mesh-prim{mesh-entry-not-uid-stamped};3-latent-bugs-fixed{graph-mesh-release,excluded-mesh-export,lock_edit-seal-before-values};scene79/usd41/roundtrip164/smoke52;viewport-identical;UI-residue->U3}
✓asset-browser-refresh_file{af689d5c8;unblocks-roundtrip-script}
✓U3-Camera/Light:Xformable{commit-1✓d5f0be1a8:Camera/Light=Item<Item_base,Xformable,X>;get_node()=this;set_prim_parent(Xformable,parent){keeps-LOCAL;set_mesh_parent-delegates};get_camera/get_light;glTF-rule=node-with-two-of-mesh/camera/light->prim-of-first(mesh>camera>light)+others-as-child-prims;camera-entry-not-uid-stamped;Brush_preview-fill-light-bug-fixed;scene82/usd42/roundtrip164/smoke52|commit-2✓7fd9f447d:get_node()-removed-from-Mesh/Camera/Light{~165-sites-address-prim};Attachment_kind{child_prim|api_schema}='Add Child Prim'/'Add Attachment';DnD-payload=prim-class-name{U1-had-broken-DnD:literal-'Node';fixed;interactive-gesture-unverified};get_node_details.mesh/camera/light-on-node-entry;scene82/roundtrip164/smoke52;viewport-identical-99px-statusbar}
  U3-DONE-2026-09-06
✓U4-resources-are-prims{L;commit-1✓cf19e2608:every-library-kind=Item<Item_base,Typed,X>{tokens=class-names;Material='Material'};Hierarchy::get_inheritance_parent/is_name_available-fall-back-to-Item_base-when-parentless{container-inheritance-kept};Selection-delete/duplicate-require-parent;Graph_asset-keeps-set_item_host-override{single-choke-point};item171/scene84/primitive42/roundtrip164/smoke52|commit-2a✓Item_host::register_prim/unregister_prim+Typed-single-call-site{item176}|2b✓per-kind-lists+by-item-map+serial{fed-by-Content_library_node-add/remove-child-hooks;Asset_key::library_folder-removed->kind-type-bit;kind-routed-make/add/add_reference/remove;add-dedups-library-wide{tightening};Content_library::add-has-bool-is_reference{retires-in-2c}}|2c✓resources-are-prims-under-lazy-kind-Scopes{no-content-flag=glTF-node-writer-skips};Content_library_node+category-roots-gone;Content_library:Item_host{palette};Resource_metadata-table-weak_ptr-keyed{survives-undo/redo};referenced-resources=index-entry-no-placement;D1-container-link-gone{tree-parent-inherits};roundtrip-script-drop_library_prims();item177/roundtrip164/smoke52;DnD-of-resource-under-Xform-not-reachable-from-UI-yet|2c-was:resources-into-tree{kind-Scopes-lazy;Content_library:Item_host-for-palette;metadata-table-keyed-by-resource}|2d✓1b5722576:make_library_attach_operation->Item_insert_remove_operation{bookkeeping-outside-op};Content_library_move_operation->Item_parent_change_operation{any-prim-drop-target;move_library_item-same-kind-refusal-only-under-other-kind-scope};create_material-undoable;Create-menu-Scope;duplicate-skips-not_clonable;Selection::end_selection_change-no-op-change=no-message;Item_insert_remove_operation-reports-retained-prims-as-refs;material-under-Xform-does-not-survive-save-yet{2f}|2e✓:reference-listing-retired{Material_set-per-object-membership-IS-the-referenced-source;fed-by-enqueue_mesh_materials+sync_object_materials-change-sites};index=owned-only;definition-container=manager-record{Scene_root::is_asset_definition};prefab-instance-materials-render-with-no-entry{screenshots-identical};exporter-scans-meshes-per-save-for-proxy-candidates|2e-was:asset-manager+reference-listing{referenced-list=single-source-for-Material_set::sync_library;prefab-reference-entries-retire}|2f✓glTF:library_folders.path=holding-prim-path{scope-branch-creates;node-path-resolved-never-created;append_library_folders_operation-after-node-inserts};roundtrip-183/183{3-placements}|2g✓21da9055c:usd_export-two-pass{plan-paths-then-write};Material-prim-where-it-sits;is_shading_scope-retired;binding-by-path;resource-prims=show_in_ui-not-content;usd47/roundtrip215}
  U4-DONE-2026-09-06
✓M6-double+mat4{078f5fd86,2026-09-06,harness:1-coder;property-tests-100→104;usd-tests-47→48;rows-unverified-interactively{no-shipped-property-yet}}
✓X1-references-as-prefab-instances{2026-09-06,harness:2-coders;usd56→63→68;headless:open-references.usda→5-carriers+instances;import+undo;save→arcs-written,content-absent;reopen-same-tree;close-clean}|?user-interactive{open-a-USD-with-references;Hierarchy-shows-sealed-instance}
✓M6-asset-path+arrays{2026-09-06,harness:1-coder;property108→112;usd55→56;M6-complete}
✓M7-style-chains{2026-09-06,harness:1-coder;property104→108;item177→178;headless:A→B→light-chain+live-edit+cycle-refused+save/open+close-clean}|?user-interactive{Style-item-style-row-picker}
✓M8-xformOp-stacks{2026-09-06,harness:2-coders;scene84→105;usd48→55;headless:open-xform_ops.usda→transform_selection→save-usda:translate-op-moved,rotate/scale/pivot/matrix-lines-byte-identical;close-clean}|?user-interactive{drag-a-USD-imported-prim;Properties-TRS-rows-on-stacked-prim}
✓X2-commit-1{3278251eb,2026-09-07:erhe::property-reference-layer-D33{set_reference/get_reference/reference_chain_reaches;R3=coerced>local>style>reference>inherited>default;layer-reads-counterpart's-SUPPLIED-value{local/expression/computed/style/reference;NOT-its-inherited}→instance-tree-provides-inheritance;has_own_value=local|style|reference;propagate_to_reference_users;property112→121}}
✓X2-commit-2{4f4ba458b:Item_base::active{own-opinion,serialize,NOT-inherits}+derived-Item_flags::active{bit38;own&&parent-bit;rederive_active_flag_bits-change-driven:property-callback+set_parent+set_node+set_inheritance_container};consumers=visible-filters+active;Node_physics-leaves/enters-Jolt-on-flip{register_node_physics-VERIFY-not-in-world};hierarchy-row-TextDisabled;glTF=ERHE_node.properties.active;USD=prim-active-metadata{Tydra-does-not-prune};label_color-reference=pink;item185/usd73;headless-verified}
✓X2-commit-3{7b9736b82:link_instance_to_template{lockstep-template-vs-clone,skips-!is_clonable(){new-on-Clonable_base};set_reference-per-pair;clears-copied-locals-the-counterpart-supplies};structure-protection=instance_structure.hpp{instance_structure_refusal+instance_child_refusal;asked-at-Hierarchy-DnD/Cut/Duplicate/Paste/Create-menu/Selection::delete_items/MCP-create_node/create_shape/place_brush*/reparent_node/delete_nodes/clipboard_paste/move_library_item;operations-unconditional};USD-instances-unsealed{is_sealed_prefab_instance=glTF-only:seal+leaf-row+pick-redirect};MCP-set_prefab_template_property{not-undoable};set_item_property-value-null-clears-local;headless:references.usda-5-carriers,reference→local→undo→reference,template-edit-live-3-users,refusals,carrier-delete+undo,close-clean;glb-instance-still-sealed;roundtrip-214/215-flake}
✓X2-commit-4{4bb22f914:erhe::scene::instance_override{definition-once:local-non-bridged-serializable-value|transform-differing-from-counterpart;name=structure;collect_instance_override_items+collect_instance_overrides+apply_instance_overrides};USD-over-prims-below-carrier{typeless-Model-Specifier::Over;target-clone=carrier-prim{its-overrides-authored-on-carrier,transform-not-writable};reader=root-layer-PrimSpec-specifier;def-below-carrier=dropped+warn};glTF=ERHE_node.overrides{path+properties+16-float-transform;applied-before-seal};refresh_instance_subtrees-capture+reapply{+keeps-payload-arc-kind};attachments-not-walked{plan-section-6};usd73→79;roundtrip-230/230-minus-flake{usd_references_leg};headless:usda-over-lines+reopen+byte-identical-second-save;glb-override-survives-reopen+reload_prefab}
  X2-DONE-2026-09-07{4-commits:3278251eb+4f4ba458b+7b9736b82+4bb22f914;harness:4-coders+4-scouts}|?user-interactive{Properties-reference-hue-pink;dimmed-inactive-rows;USD-instance-interior-editable;DnD-refusals}
✓X3-class-prims-as-styles{e91149487,2026-09-07,harness:1-coder:Usd_data::classes{root-layer-PrimSpec-Class-specifier;Tydra-never-walks-class-prims}+prim_inherits;editor-makes-Style-at-class-path+first-resolving-target=style;writer:Style=typeless-class-prim-where-it-sits{identified-by-class-token-'Style'}+inherits=</planned-path>-on-every-styled-prim;erhe::scene::apply_property_values-factored-from-X2;usd79→91;roundtrip-247/247-minus-flake{styles-leg};traps:imported-meshes-author-shadow_cast=true-local{style-never-reaches-it};Material.roughness=vec2}
✓X3-follow-up{6f9c7a010:over-prims-write-schema-named-values-as-erhe:Owner:name-custom-attrs{typeless-over-has-no-schema;Material-inputs-live-on-child-Shader-def};usd91→92;left:write_instance_root_override-same-gap-when-target-clone-class!=carrier-class}
✓X4-commit-1{c199caf5a:Usd_variant_set/Usd_variant/Usd_variant_binding{material-bindings-only;unsupported_opinion_count-reported-once};reader-applies-selected-variant-bindings-itself{Tydra-composes-no-variant;variant-only-materials-converted-via-ConvertMaterial+appended};writer:append-variantSets+variants-selection+variantSet-blocks{deeper-bindings=over-prims};Usd_save_arguments::variant_sets{editor-fills-in-c2};usd92→98;LOSSY:non-binding-variant-opinions-dropped-on-save{c2-warns-at-save}}
✓X4-commit-2{c0b050b09:editor::Variant_table-on-Scene_root{weak-prim+weak-materials;pruned-on-items_removed};Scene_settings.variant_selections{codegen-Variant_selection-added_in=1};Scene_root::select_variant{Variant_switch_mode-undoable|immediate;compound=Variant_select_operation+Mesh_material_assign_operation-per-binding};Properties-Scene-combo;MCP-get_scene_variants+select_variant;save_scene_usd-fills-variant_sets+warns-unsupported-opinions;roundtrip-264/264-minus-flake{variants-leg-17}}
✓root-cause-fixes-found-by-X4{cdd3a7577:Asset_reference-move-left-dangling-user-registration{registry-keyed-by-item;replace_user};62cd67877:usd-customLayerData-StringData-never-read{NO-USD-scene-reloaded-erhe:scene-state-before}}
✓X4-commit-3{KHR_materials_variants:Gltf_data::material_variants{bindings-per-instantiated-Mesh-prim+primitive-index}+Gltf_export_arguments::material_variants{mappings;own-material-untouched};editor=ONE-set-'materials'-on-import-root|scene-root-prim{empty-path};primitive-form=<mesh-path>#<index>{USD=GeomSubset-name;resolver-reads-both};selection-in-ERHE_scene.settings-under-empty-path;extension-mask-needed;fixture-src/erhe/gltf/test/data/variants.gltf;roundtrip-280/280-minus-flake{gltf-variants-leg-16}}
  X4-DONE-2026-09-07{3-commits:c199caf5a+c0b050b09+5a2b01bb5;first-slice=material-bindings;node-subtree-variants+non-binding-opinions=later{lossy-on-USD-save,warned}}|?user-interactive{Scene-Variants-combo}
✓X5-provenance{2026-09-07,harness:1-coder:editor::describe_property_origin{on-demand-from-Value_source+scene-file+M1-path+Prefab_instance-carrier+style-chain;layer/prim_path/arc{root_layer|reference|payload|inherits|none}/arc_target/authored_as};erhe::usd::get_usd_authored_as{writer-naming-rule-public};tooltip-extra-while-hovered{Property_editor::set_entry_tooltip_extra}+MCP-get_item_properties.origin;NO-Layer-kept-alive{©Orchestrator-decision:erhe-resolves-all-arcs→value-source-IS-provenance;ArcOrigin-prim-level-only};headless-verified-reference/local-over/style-chain/inherited;payload+expression-origins-unexercised}
  X3-X4-X5-DONE-2026-09-07|?user-interactive{Properties-tooltip-origin-lines;Scene-Variants-combo;Style-items-from-class-prims-in-Hierarchy}
✓E4a-brushes{2026-09-07,harness:1-coder:def-Brush-prim-where-it-sits{erhe:Brush:density+normal_style;rel-material:binding;child-def-Mesh-geometry-subdivisionScheme=none};Usd_data::brushes{root-layer-walk;conversion-stops-below-Brush;no-Typed-item}+Usd_save_arguments::brushes{editor-fills;class-token-recognition};write_geometry_mesh_prim-factored;get_scene_brushes-from-library-index+material/density/normal_style;usd98→107;roundtrip-297/297-minus-flake{brushes-leg};trap:first-save-of-in-memory-brush-differs-in-vertex-order{fixed-point-from-first-reload};left:brush-geometry-meshes-still-counted-in-Usd_data::meshes}
?E4b-geometry-graphs→E4c-texture-graphs→E4d-folders{plan-section-3;user-asked-E4a-only-2026-09-07}
✓S1-usd-wg-survey{2026-09-07,harness:1-coder+2-orchestrator-interventions:scripts/usd_wg_asset_survey.py+doc/agents/usd_wg_assets.md;MCP-frame_scene{binds-scene-to-viewport+own-camera-on-mesh-AABB+headlight-when-no-lights};146-entries/0-crash/30-works/115-gap/1-fails;top-gaps:no-light→black(139)+arcs-on-non-Xformable-dropped(44)+UsdGeom-Cube/Sphere/...-no-mesh(28)+appearance{normal-map-bias/scale,UsdTransform2d,mirrored-uv,opacity,roughness,UsdUVTexture-color,usdz-texture};traps:first-run-captured-empty-viewport{opened-scene-not-bound-to-viewport}+ElephantWithMonochord-IS-1.2mm-in-file{metersPerUnit-0.01,extent-0.12};authored-cameras-import-correctly-12/12{coder-claim-retracted-after-measuring}}
✓S1-F1{b1f68a03f:per-viewport-headlight{Shadow_render_node::resolve_headlight;own-one-light-Light_set-when-scene-light-layer-empty;Graphics_settings::headlight_when_unlit-default-on-NOT-persisted;MCP-set_graphics_settings;frame_scene-no-longer-creates-a-light}|b47e29918:DomeLight→Scene-ambient_light{color*intensity*2^exposure;Usd_data::dome_lights-record-on-Scene_root-written-back-as-DomeLight-prim-at-root;texture-warned};usd107→113;survey-subset:McUsd-lit}
✓S1-F2{arcs-on-typeless/Scope-prims→Xform-carrier{keeps-authored-transform;writer-spells-def-Xform};usd113→116;NEW-GAPS-FOUND:subLayers-not-composed{Teapot_Payload.usd;LightUSD-composes-nothing→erhe-must-resolve-sublayers-like-arcs;Teapot/DrawModes-still-0-meshes}+DrawModes-hierarchy-sanity-'child-Teapot-parent==(none)'-x28-during-prefab-instantiation{pre-existing,undiagnosed}}
✓S1-F3{333f48d3b:Cube/Sphere/Cone/Cylinder/Capsule/_1-variants→Mesh-via-erhe-generators{axis-baked;normative-geometry;in-convert_meshes-for-Tydra-carried,convert_node-for-_1};premise-corrected:Tydra-DOES-tessellate-but-erhe's-xformable-type-list-excluded-them;GPrim-fallback-property-names;usd116→127;survey-subset-8-no-mesh→7-works;PointInstancer-still-Typed{erhe-side-instancing-gap}}
✓S1-F4{wrap/transform2d/scale-factor{connected-input-factor=inputs:scale}+normal_texture_decode_scale/bias-vec4-props+UBO+standard.frag+alpha=sampled_alpha+usdz-packed-textures{ReadUSDZAssetInfoFromFile→Usd_image::bytes→Image_loader-memory};usd127→134;validation-clean;NEW-GAPS:USD-st-V-flip{every-USD-texture-vertically-mirrored;largest}+.hdr-decodes-nowhere+UsdUVTexture-per-channel-outputs-ignored+unauthored-diffuseColor=white-not-0.18{I2-rule}+headlight-lights-files-intending-none}
✓S1-survey-rerun{0db8a2d78:47-works/97-gap/1-fails/1-CRASH{References/OverridingReferencedInternalReferencesTest.usda-kills-editor-on-open_scene;new-since-F2}};top-gaps-now:reference-targets-unresolved(42+38+31+13)+subLayers-not-composed(25)+Material-prim-without-network(23)+PointInstancer;doc-by-eye-verdicts-UNRELIABLE-at-3/4-framing{AlphaBlend-composite-shows-blend-works-but-doc-says-opaque→survey-must-frame-through-authored-camera}
✓S1-crash-fix{Light_projections-stale-raw-Light*-across-Shadow_render_node-early-exits→UAF-after-scene-close{clear()-each-frame,capacity-kept};Xformable-for_clone-attach()-ran-node_sanity_check-in-ctor→false-'parent==(none)'{set_node()};diagnosed-via-cdbX64-minidump{VS-MCP-not-running};usd134→138;full-survey-0-crash-48-works;NEW-GAP:class-Scope-'Prefabs'-holding-real-prims-imports-descendants-as-Style-items{X3-rule-too-broad;References-asset-shows-only-plate}}
✓survey-authored-camera-pass{frame_scene-camera_source-authored|computed;composite-at-reference-size;50-works/95-gap/1-fails/0-crash;corrections:NormalsTextureBiasAndScale=works,AlphaBlendModeTest-opaque-claim-withdrawn;new:McUsd-stained-glass-opaque,animated-translation-not-sampled,Vehicles-cross-layer-material-binding-black}
✓S1-F5{c7e006b67:any-prim=reference-target{find_prim_hierarchy;wrapper-keeps-transform-only-for-Xformable;attach/retarget/seal-over-Hierarchy};class-prim-def-descendants=prototypes{content-clear;Usd_data::class_prototypes;under-Style-item;clone-gets-content;writer-writes-class-children-as-defs};every-Material-prim-converted{append_unconverted_materials};.mtlx-named-once;usd138→146;subset-59:24-works/35-gap/0-fail;LEFT:template-materials-'unowned'{DESIGN:owner=prefab-holding-scene→warning-should-recognize-that-host;15-lines-cross-file-bindings}+Vehicles-cross-layer-binding-still-black+SpinningPyramids=node-subtree-variants+UsdPrimvarReader-diffuseColor-fails-Tydra}
✓S1-F6{7f1683e51:load_stage-composes-subLayers-via-LightUSD-CompositeSublayers+LayerToStage{root-strongest;metadata-root-else-strongest-sublayer-second-walk;erhe-asset-resolution-handler-for-'..'-paths;top-level-prims-sorted-by-name};save=one-flattened-layer-no-subLayers{Scene_root::get_usd_sublayers-logged};usd146→155;subset-39:25-empty-stages-now-load{teapotScene-4429-prims};LEFT:sublayer-authored-prims-give-no-authored-opinions/class-prims/xformOp-stacks{root-layer-only-reads}+usdz-sublayer-warned+teapotScene-trips-main-loop-stall-watchdog{load-perf}+Teapot.usd-geometry-inside-variantSet{X4-node-subtree-variants}}
✓S1-survey-rerun-2{42-works/103-gap/1-fails/0-crash;1578s;chess_set=works;teapotScene-slow-success+stall-watchdog;failure-rows-gone;doc-'stage-scale-not-applied'-row-WRONG{metersPerUnit=1-in-both-layers;framing}}
✓S1-F7{992669e6f:material-binding=3rd-override-kind{Instance_override::material_path;subset=last-path-segment;resolve-below-carrier-then-ancestors;writer:rel-material:binding+MaterialBindingAPI-on-over/subset-over/carrier;glTF-overrides[].material;ShapingAPI:ShapingAPI-spelling-fixed}|c1ae23a81:material-owned-if-in-a-prim-tree-or-Prefab_library::owns_material{warning-states-only-true-case};usd155→163,scene105→112;Vehicles-render-in-reference-colors;subset-40:0-unowned}
✓S1-F8{74d924401:Texture_channel-per-scalar-input{Material-props;uvec4-texture_channels-in-UBO;standard.frag+erhe_ray_hit.glsl}|1278c330b:st-V-flip{v'=1-v-on-read+write;UsdTransform2d-through-flip:rotation=-r,offset=(Tx-sin(r)*sy,1-Ty-cos(r)*sy);writer-authors-UsdTransform2d-prim};outputs:<c>-read/written;diffuseColor-fallback-0.18-local;usd163→173,primitive42→44;roundtrip-296/297-flake;labels-read-correctly-everywhere;OPEN:null-material-fallback-color-for-unbound-USD-meshes}
✓S1-survey-rerun-3{45-works/100-gap/1-fails/0-crash;mirroring-gone-everywhere;unowned-row-gone;tractor/wheel/TextureCoordinateTest/chess_set/UsdCookie=works}
  S1-FIX-ROUND-DONE-2026-09-08{13-commits:F1-F8+crash;usd-tests-107→173;works-30→45;plan-section-3-S1-lists-remaining-in-order}
✓S1-variants-2026-09-08{©User-ordered:variants→transform-test→sublayer-opinions;harness:2-coders;93480bcb5:Usd_variant::overrides=Instance_override-per-path{empty=carrier;over-children;X2-form}+Usd_variant_set::base_values{cleared-state=Instance_override_value_state}+switch=one-compound{selection+Property_set_operation-per-(path,name)+Node_transform_operation+bindings}+writer-writes-opinions-in-variant-blocks;usd173→176|1e0087055:def-children-hoisted-at-load_stage{hoist_variant_prims-on-composed-layer;M2-unique-names-via-Hierarchy::make_unique_name;unselected=active-false;Stage::Impl::root_layer-kept+variant_prims-records;importer-no-longer-re-parses-root;switch=active-writes;writer=def-inside-variant-block-under-authored-name,active-suppressed-for-unselected};usd176→180;LEFT:def-below-over-child+usdz-not-hoisted;sublayer-authored-variantSets-hoisted-but-no-table-entry{root-layer-only-read→sublayer-task};Teapot.usd-variants=GeomModelAPI-cards-only}
✓S1-transform-test-2026-09-08{6c7a56550:VERDICT-WAS-BY-EYE-ERROR;erhe==usdview{hdSt-codeGen:T+R(r)*(s*in);NCC-face-on:0.98/0.94/0.94-vs-control-0.97-0.98};asset-drift:usd-wg-903264c-un-nested-cards-vs-2022-reference;frame_scene=coplanar-meshes→straight-down-shared-axis{extent<1%-of-largest};Uv_transform_placement-test;usd180→181}
✓S1-sublayer-opinions-2026-09-08{7dea6ab25:Stage::Impl::layer=COMPOSED-layer{copy-before-LayerToStage;~14ms-of-32s-teapotScene};find_layer_primspec/read_layer_composition→sublayer-class=Style,over=override,Brush,variantSet-table-entry;NOTES-CORRECTED:M8-xformOps+I2-authored-read-composed-stage-not-layer;provenance-names-scene-file{CompositeSublayers-keeps-no-per-property-source};usd181→188;SublayeredInternalReferenceTest-1-class→prototypes-as-clones}
✓survey-script-fix-2026-09-08{0ab68f266:doc/agents/usd_wg_assets_eye.json-sidecar{16-entries;--eye-note/--eye-gap-writes;runs+--from-summary-read};subset-runs-merge-by-path{surveyed_at/survey_seconds};full-rerun=51-works/94-gap/1-fails/0-crash{1681s;46→51=run-variation-not-code};S1-gap-list-unchanged;LEFT:--refresh-cameras-fields-still-dropped-by-full-run{re-run-it-after}}
✓survey-captures-2026-09-08{a952a2d77:©User-asked-sky+grid-off;Graphics_settings::sky_enabled/grid_visible=session-only-ANDed-with-config{never-persisted};MCP-set_graphics_settings-args;survey-waits-get_async_status-idle-2-consecutive-reads-before-counts/frame/capture{chess_set-11s;3-scene-close-leak-gap-rows-were-survey-closing-mid-load};FOUND:chess_set-black=0-materials→null-material-fallback-gap{was-legible-only-as-silhouette-against-sky}}
!survey-policy::©User-2026-09-08:after-a-fix-rerun-ONLY-affected-entries{--only;merge};full-survey-ONLY-after-all-identified-gaps-fixed
✓null-material-fallback-2026-09-08{df448f19a:Material_set-reserves-slot-0=default-record{0.18-grey/rough-0.5;written-each-update;slots-start-at-1};draw-list-already-wrote-index-0-for-null;displayColor-already-vertex-colors;chess_set-grey-now}
✓PointInstancer-2026-09-08{2d49c3b59:erhe::scene::Point_instancer=Boundable{bit-55;NO-arrays};prototypes-stay-in-place-content-flag-clear;each-instance=child-Xform+INTERNAL-reference→prefab-instance-via-existing-arc-loop;writer-recomputes-positions/orientations/scales/protoIndices-from-instance-children{proto=Prefab_instance-target-path-match};LightUSD-ComputeInstanceTransformsAtTime+mask;survey:timeout-entry=fails+relaunch,--load-timeout-400;usd188→199,scene112→114;chess_set-16-pawns;intent-vfx-scenes-2x-prims{268-466s-loads;stall=per-instance-main-thread:scene_commit-flush/draw-lists/hover,NOT-BVH-per-clone}}
!scope::©User-2026-09-08:MaterialX=FUTURE-WORK{not-S1;findings-kept:Tydra-converts-ND_standard_surface/open_pbr→RenderMaterial::openPBRShader-which-erhe-never-reads;.mtlx-targets-need-LIGHTUSD_WITH_USDMTLX;orange_squares-fails=LightUSD-usda-parser-defect-on-attribute-colorSpace-metadata}
✓time-samples-2026-09-08{c1df4dee6:Xform_op::samples{file-time-codes;authored-record-writer-writes-back}+Xform_op::value=pose-at-stage-wide-eval-time{startTimeCode|earliest-sample;Tydra-given-same};one-Animation-per-file-in-content-library{TRS-stacks-only;LINEAR;seconds=timeCode/tcps-24};non-TRS-stacks-warned{BoxAnimated-orient-before-translate};layer-time-code-metas-written;usd199→209;LEFT:keyed-edit-in-erhe-changes-channels-not-samples-on-save}
✓doubleSided-2026-09-08{e90fc37f8:Gprim::double_sided-entry-property{inherits};is_double_sided(mesh,primitive)=material||prim{draw_list_scene+mesh_memory};Mesh::handle_gprim_render_state_changed→notify_primitives_changed;usd209→214,scene114→118;glTF-via-ERHE_node-mesh-properties;FOUND-RENDERER-BUG:grid-pass-between-opaque+translucent-fills-writes-depth,stencil-guard-not_equal-bit7-never-rejects{polygon_fill_standard-writes-no-stencil}→NO-alpha_blend/alpha_test-primitive-renders-with-grid-on;McUsd-glass+cards-correct-with-grid-off}
✓grid-fix-2026-09-08{22e8e23c4:grid-pipeline-depth_write_enable=false{overlay-not-surface;measured-4-builds:reorder-puts-lines-over-glass,stencil-guard-IS-live-for-selection};draw_list-logger-in-logging.json;McUsd-glass+cards-render-with-grid-on;OPEN:grid-depth-disagrees-with-content{lines-cross-opaque-objects-below-horizon;doc/editor/rendering.md;needs-RenderDoc-windowed}}
✓RoughnessTest-2026-09-08{b50b91033:PREMISE-WRONG:usdz-subdir-paths-already-resolved{Tydra-fs-probe-warning=noise,filtered};REAL-BUG:shared-metallic_roughness-slot-multiplied-BOTH-inputs-by-texture→Texture_channel::none=4{texture_channel_value()-in-erhe_texture.glsl/standard.frag/erhe_ray_hit.glsl→1.0};importer-names-none-for-untextured-partner;writer-skips;highlight-gap=framing;usd214→216;validation-run-clean{warnings-only}}
  S1-ROUND-2-DONE-2026-09-08{13-code-commits;usd-tests-173→216;survey-doc=subset-merges,full-run-NOT-re-run-this-session}
!scope::©User-2026-09-08:FUTURE-WORK{not-S1}=16-bit-PNG+CMYK-JPEG-decode{wuffs-decode-failure-undiagnosed;.hdr-via-stb_image-in-cpm-cache}+load-perf{collect_meshes_sharing_primitives-O(N)-scan-per-commit→O(N^2);hover-linear-trace-while-TLAS-cannot-settle;finalize_imported_meshes-serial-BVH-on-tick-thread;rebuild_all-churn}+grid-depth-disagreement{lines-cross-opaque-objects}+animation-edit-write-back{keyed-edit-changes-channels-not-samples}
✓stopped-2026-09-08{plan-S1-empty+section-6-future-work;queue-rewritten};NEXT-SESSION:full-survey-run-ONCE→E4c{doc/erhe/usd_node_graphs.md}→E4b→E4d→E2
?S1-remaining{doubleSided→Gprim-property{brief-drafted;McUsd-cards};usdz-subdir-texture-paths{RoughnessTest:0/roughness.png-unresolved};image-formats{16-bit/CMYK-blank=undiagnosed-decode-failure;.hdr-via-stb_image-in-cpm-cache};animation-time-sample;16/32-bit+CMYK+.hdr-images;McUsd-alpha+cards;RoughnessTest-specular;load-perf-4000-prims;MaterialX;null-material-fallback}→E4c
  traps::save_usda-writes-/Materials-from-index{would-duplicate-kind-scope};Xformable::node_sanity_check-static_casts-Item_host->Scene_host{palette-prims-must-not-reach};kind-scopes+resource-prims-carry-no-content-flag{glTF-node-export-skips}


[NOTES]
!headless-recipe::build_vs2026_vulkan_headless-editor→ERHE_AI_DRIVER=1-launch-hidden→mcp_call.py-b64-args{get_item_properties/set_item_property/get_addable_item_properties/undo;ids-reshuffle-per-launch;scene_name-required-for-create_node/select_items/get_node_details}
!default-scene::floor-mesh-sealed{lock_edit}→use-cube-attachment-for-attachment-tests
!scene-close-check::close_scene→wait≈6s→grep-"scene-close"{clean="all N released"}
!clangd-db::re-run-configure_ninja_win_clang.bat-after-adding-source-files{done-2026-09-04}

[TASK::K1-skinning]{2026-09-08,via-harness}
✓commit-1{usd-import:Skeleton=Xformable+token;joint-Xforms-at-rest;Skin=inverse(bind)*geomBind-per-(skeleton,geomBind);joint-attrs-both-build-paths;SkelAnimation->joint-channels;usd217→224}
✓commit-2{editor:append_usd_content_library_operations-attaches-skins;register/build-already-format-neutral;CarbonFrameBike-cables-match-pxr-skinned-AABB-4mm;survey-gap-row-gone}
✓commit-3{export:Skeleton-from-Mesh::skin-pivot;joints=arrays-not-prims;first-skin-identity-geomBind+bind=inverse(ibm),further-skins-geomBind=bind_0*ibm_0;SkelBindingAPI-vertex-primvars-narrowest-elementSize;SkelAnimation-from-Usd_save_arguments::animations;bone-materials-no-longer-in-content-library;usd224→232;roundtrip-skinning-leg+usdchecker-pass}
  K1-DONE-2026-09-08{3-commits:ff4c67107+a3ec776fd+5a2b01bb5;plan-section-2}|?user-interactive{selected/hovered-bone-proxy-color-after-bone-material-unlisting;Hierarchy-shows-Skeleton+joint-prims+Skins-scope}

[TASK::E4c-texture-graphs]{2026-09-08,via-harness}
✓survey-tooling{d43fcbb8e:--usd-root=OpenUSD-leg{pxr-composed-counts+AABB;usdrecord-Storm-render-through-erhe-camera{get_viewports-rect+camera-frame;session-layer-camera-in-stage-space};storm_match-NCC+silhouette-IoU+bounds_deviation>10%=gap};validated:RoughnessTest-0.92/TextureCoordinateTest-0.90;0%-bounds-dev-on-Z-up+0.01-unit;4wdFullAsset-55%=REAL-GAP{25-meshes-vs-5-composed;not-diagnosed}}
✓phase-1{501584e87:erhe::usd-Usd_node_graph{pins/params-(usd_type,literal-text)/nodes/interface-outputs}+material_graph_bindings;reader=collect_layer_composition-stops-at-marked-NodeGraph;writer=generic-Shader-prims+connections;load_stage-strips-graph-wiring-from-layer-copy-for-Tydra{fork-future-work};fixture-texture_graph.usda;usd232→245}
✓phase-2{e88ec1d8e:collect/resolve_usd_node_graphs-in-parsers/usd.cpp;JSON-params→(usd_type,literal)-by-value-kind{nested=JSON-text-with-single-quotes:LightUSD-escaped-quote-defect→fork-future-work};links-by-name;output-sink-linked-input=interface-output;`scene`-param-not-written;MCP-get_scene_node_graphs;roundtrip-leg-17/17{330-total,1=P6-dynamic-flake};headless:plane-shows-noise→gradient;byte-identical-2nd-save;close-clean}
  E4c-DONE-2026-09-08|?user-interactive{Hierarchy-shows-graph-under-holding-prim;Texture-Graph-window-opens-USD-loaded-graph}
✓full-survey-with-OpenUSD-leg{2026-09-08:146-entries/3164s/0-crash;21-works/124-gap/1-fails;Storm-match-high=animated-usdz-0.98,low=Vehicles-kit-negative;bounds-gap-47→43-after-skinned-skip{5608ad117}}
!FOUND-vehicle-kit-gap{MEASURED-via-pxr-XformCache:reference-target=Mesh-prim-authoring-own-xformOp:transform{39.3701-scale,4wdGeo.usd}→erhe-drops-target-Mesh's-own-xform;~30-entries;References/*-0.495-rows-likely-same-class}→ROOT-CAUSE-WAS-DIFFERENT{scout-measured:prefab-template-load-applied-TARGET-file's-own-upAxis/metersPerUnit-a-second-time;0.0254*39.37=1}
✓fix{976a56adf:Usd_load_arguments::stage_metrics{root|referenced};load_usd_prefab_template→referenced;usd245→250;4wdBody-bounds==pxr;bounds-rows-41→11;roundtrip-331/332}
  STOPPED-2026-09-08{©User};left=11-bounds-rows+stale-first-framing-bounds{plan-section-6};queue=E4b→E4d→E2


[TASK::undefined-usd-prims]{DONE-2026-09-09}
✓fix-1{7d0002f3d:Item_base::defined+derived-active-bit+import/export;84f735c45:apply_defined-reads-composed-layer-PrimSpec-specifier{LightUSD-LayerToStage-leaves-Prim::specifier()=Invalid;typed-struct.spec-only};user-verified}
✓fix-2{d01eead74:build_primitive_schema_geometry-reads-GPrim-displayColor/displayOpacity->corner_color_0-every-corner{first-element;>1-warned};primitives.usda-Box-colored;usd-tests-256;user-verified}
✓fix-3{12f0ce012:null-material-rendered-unlit{Shader_key::derive-skipped-material-block->BXDF_MODEL-0=unlit,no-USE_VERTEX_VARYING_NORMAL;default-record-slot-0-says-isotropic_brdf}->derive-uses-Material_values{}-defaults+empty-samplers-for-null;shadow-path-unaffected;headless+user-verified}
✓tests-in-main-build{cf367a263-(c)User:configure_vs2026_vulkan.bat-ERHE_BUILD_TESTS=ON;run:build_vs2026_vulkan/bin/Debug/erhe_usd_tests.exe}
  ALL-DONE-2026-09-09;NEXT=prompt_queue.txt-item-0{E4b}

[TASK::usd-survey-gap-loop]{DONE-2026-09-11;harness=doc/agents/usd_survey_gap_loop.md;3-workers}
✓survey-tooling{test-db+ordering-options+--stop-on-gap+expected-results-sidecar+eye-per-entry+bounds-fixes;19-commits-156911452..b2f1e143a}
✓erhe-fixes{12e674e96+a5ffe14ad+ca9b2b651+f346d58f4+232213b0e+bcdc3a721+69700edb3+736ec447b|loop:3cc978116-shared-Skin-registered-once{Scene-use-counts;scene-tests-121}+0268de381-visibility/purpose-on-Skel/Points/Curves/PointInstancer/DomeLight+56f40c44c-holeIndices-facets-not-drawn;usd-tests-272}
✓survey-fix{a3f314d09-expired-MCP-request-line-benign}
✓deferrals-section-6{(c)User:PrimvarReader-fed-UsdPreviewSurface-input|Teapot-arcs-authored-inside-variant+xformOp-supplied-by-arc;worker:DomeLight-env-map+.exr+load-perf-vehicleVariants/intent-vfx+LightUSD-single-path-rel-list-op-merge{fork-fix,2816-instances}+nested-carrier-override-path{find_instance_item}};bounds-item-deleted{both-rows-0.0}
✓scopes-clean{test_assets+full_assets+intent-vfx;18-commits-2da54eec8..ff2a735d9}
✓full-run{ff2a735d9:146-entries/3124s;works-138/gap-7/fails-1/crash-0;all-non-works=MaterialX}
!caveat::per-entry-diagnostics-order-dependent{Tydra-warns-only-on-first-load-of-shared-file-per-launch;expectations-name-patterns-on-every-affected-entry-so-verdicts-hold,line-counts-vary}
?schedule::LightUSD-rel-list-op-fork-fix+nested-carrier-override-resolution{section-6;erhe/fork-defects-not-by-design}

[TASK::E4b-geometry-graphs]{DONE-2026-09-11;via-harness;3-commits}
✓commit-1{3199d4396:erhe::usd-format-token+prefix-pairing+result-child-Mesh;11-new-tests->283;fixture-geometry_graph.usda}
✓commit-2{5c304f4d2+fc9d21605:plan_usd_prim_paths+editor-half;scene-block-graph_meshes-by-planned-path;variant-selections-same;geometry-leg-25/25;usd-tests-285}
  E4b-DONE-2026-09-11|?user-interactive{Hierarchy-shows-geometry-graph-under-holding-prim;Geometry-Graph-window-opens-USD-loaded-graph}
✓follow-up{3f57351ed:Item_flags::session_only;injected-default-camera-not-saved{USD+glTF};roundtrip-354/357}
?open{textured.usda-texture-network-lost-at-import;references_override-DefCarrier-query-wrong-carrier;P6-flake}

[TASK::E4d-folders]{DONE-2026-09-11;8060c4aa4;via-harness}
✓every-Scope-written+kind-scope-adopted-by-name+skin/animation-items-not-prims+top-level-Scope-no-World-wrapper;usd-tests-291;roundtrip-375/378
?user-interactive{empty-folder-survives-USD-save;Materials-scope-not-duplicated-after-reopen}

[TASK::E2-material-fidelity]{DONE-2026-09-11;via-harness;2-commits}
✓commit-1{36e828bf0:import-OpenPBR-network{prefers-over-UsdPreviewSurface;anisotropy-formula;authoredness-from-struct;graph-bindings-both-paths};usd-tests-297;roundtrip-377/380}
✓commit-2{199d9a77c:export-OpenPBR-network-beside-preview{outputs:mtlx:surface;inverse-formula;exact-erhe:Material:roughness};usd-tests-302;roundtrip-398/401}
  E2-DONE;USD-PLAN-SECTION-3-COMPLETE-2026-09-11;left=section-6-future-work+3-unrelated-roundtrip-failures


[TASK::metal-headless]{DONE-2026-09-12;870efa949;via-harness;1-coder}
✓emulated-ring-in-Swapchain_impl+SDL-guarded-surface+synchronous-headless-readback+configure_xcode_metal_headless.sh+doc/erhe/metal_headless.md
?user-interactive{windowed-Metal-regression:present+armed-capture}

[TASK::usd-physics-P1]{DONE-2026-09-12;via-harness;4-coders-3-agents}
✓commit-1{0aecd7718:physics-description->erhe::scene}
✓commit-2{4d4672caf:UsdPhysics-reader+physics.usda+13-tests}
✓commit-3{a3cf13baf:writer+fixed-point+usdchecker;fixes:box-scale,trigger-form}
✓commit-4{721c745fa:editor-shared-import/export;fix:no-scale-baking-in-reader;headless:open/edit/save/reopen/undo-clean;roundtrip-411/413}
?user-interactive{open-physics.usda;simulate-fixture=Jolt-assert-known}

[TASK::A1-animated-value-layer]{DONE-2026-09-12;via-harness;3-coders+2-scouts}
✓commit-1{647273769:erhe::property-animated-layer;132-tests}
✓commit-2{f2950f9d9:playback-through-layer+player-stop-clears+writers-read-base;scene-125;headless-verified}
✓commit-3{b03a6dcfe:USD-write-back-of-edited-clip;usd-334;roundtrip-413/416-baseline}
?user-interactive{play/edit/stop/save-on-time_samples.usda}

[TASK::usd-composition-real-assets]{DONE-2026-09-12;3-commits-913c2a0ca+cccc565a4+2d976dccf;plan-section-2-C6;plan-section-3-item-2;via-harness;one-commit-per-section-6-entry}
✓commit-1{913c2a0ca:selected-variant's-references/payload-list-ops-resolved-onto-carrier-after-own-ops{Usd_reference::variant_set/variant_name;Usd_variant::references;unselected-variant-arcs=unsupported_opinion_count};hoisted-variant-defs-admitted-below-carrier{is_hoisted_variant_child;convert_hoisted_variant_children};writer-authors-arc-inside-variant-block{Child_selection::variant_prims_only;fixed-point};editor-Variant_reference+collect_usd_variant_sets-before-collect_usd_references;usd-tests-338;survey:Teapot.usd-1/1-mesh-2/2-materials-0.97,DrawModes-35/35-meshes+57%-bounds-gap=xformOp-item;LEFT:nested-variantSet-inside-variant-block-not-tabled}
✓commit-2{cccc565a4:xformOpOrder-token-the-prim-does-not-author-resolved-against-arc-targets-before-ReconstructXformOpsFromProperties{read_prim_references-in-arc-order;first-authoring-target-wins;carrier-chain-followed-with-visited-guard;target's-raw-PrimSpec-props-from-composed-layer=type+precision+samples-kept};external-file-arc-NOT-followed{one-warning-naming-ops};save-writes-resolved-op-local-on-carrier{composes-same;provenance-flag-rejected:LightUSD-XformOp-has-no-order-token-without-attribute};override-reader-same-helper;usd-tests-342;survey:DrawModes-233->42-lines,bounds-57%->0.01,now-gap=Main-loop-STALLED{load-perf-item}}
✓commit-3{2d976dccf:find_instance_item-resolves-path-segment-by-segment{find_below;Instance_level::carrier|ordinary;at-every-carrier-look-one-level-down-through-each-clone-first-then-own-children;carrier=prim-with-attachment-carrying-Item_type::prefab_instance-bit->erhe::scene-names-no-editor-class};scene-tests-131;headless:intent-vfx-teapot-override-reaches-mesh{remaining=section-5-mtl-scope-drop->binding-names-no-material};survey-not-rerun-for-teapotScene{expected.json-updated-to-new-line}}
?user-interactive{open-full_assets/Teapot/DrawModes.usd:7-teapot-rows-spread-out;Hierarchy-shows-Materials-scope-under-carrier;save-writes-arc-inside-variant-block}

[TASK::degenerate-convex-hull]{DONE-2026-09-12;4945c0110;via-harness;1-coder+1-scout}
✓erhe::math::classify_affine_span{Affine_span:too_few_points|single_point|collinear|coplanar|volumetric;O(n)-no-alloc;epsilon-relative-to-extent}+make_convex_hull-refuses-before-geogram{warn-reason+count}+shapes::make_convex_hull->bool-delegates{BDEL+lock;was-PDEL-void}+MCP-create_shape-isError-reason+3-ERHE_VERIFY-sites-graceful{brush/mesh_operation/move_mesh_vertices:no-hull=no-shape/body}
!measured::BDEL-on-flat-input=warns+returns-NON-hull-true{silent-wrong-answer}|PDEL=never-returns-from-set_vertices{hang,not-getchar}|latent-bug-fixed:double-precision-points-into-single-precision-Geometry-mesh=geo_assert-swallowed->set_double_precision-before-assign
geometry-tests-116->120;headless:coplanar-create_shape=error-editor-alive,tetra-ok;plan-section-6-bullet-dropped,P1-statement-extended

[TASK::usd-joint-frames]{DONE-2026-09-12;via-harness;1-coder+1-scout}
✓reader:non-identity-localPos/localRot->Xform-frame-node-<joint>_frame0/_frame1-below-body{Node_joint-on-frame0,connected=frame1;reuse-existing-child-by-name+transform-1e-4-quat-up-to-sign;identity=today's-behavior;warning-gone}|writer:body=nearest-self-or-ancestor-Node_physics-prim,frame=node-transform-in-body-space-from-world-transforms{identity-unwritten;old-encoding-localPos1=inv(connected)*node-was-unreadable}|fixture:Panel+Post-kinematic+PhysicsRevoluteJoint-Hinge{dynamic-Panel-trips-Jolt-sleeping-velocity-assert=P1-residue}|usd-tests-344|headless:frame-nodes-created+save/reopen-no-duplicates+joint-block-byte-identical{second-save-diff=Rock/shell-mesh-collider-residue}
!model::erhe-joint=glTF-model{two-nodes=two-frames;Six_dof_constraint_settings::frame_in_a/b+Jolt-already-frame-capable;no-erhe::scene/editor-change}

[TASK::p1-physics-residue]{DONE-2026-09-12;via-harness;3-commits-planned;1-scout-for-all-three}
✓commit-A{523f323c9:Node_physics::collision_mesh{bridged-weak-object-ref-like-Node_joint::connected_node;empty=body's-own-mesh;import-sets-when-collider-mesh-prim-below-body;export-emits-collider-as-own-entry-on-that-prim->USD-schemas-on-Mesh-prim,glTF-collider-on-descendant-node;no-erhe::usd/gltf-change;get_node_details.collision_mesh};physics.usda-Rock/shell-second-save-byte-identical;roundtrip-413/416-baseline;FOUND-unrelated:import_gltf-of-exported-physics.glb-fails-parse-on-KHR_physics_rigid_bodies.physicsJoints[].limits{bisected;colliders/motions-fine}}
✓commit-B{5af2ac2fe:Jolt_world::add_rigid_body-activation-from-body's-own-velocity{is_moving:non-static+non-near-zero-linear|angular->Activate,else-DontActivate};MEASURED:Jolt-USE_ASSERTS-OFF-in-every-erhe-tree->no-assert,velocity-SILENTLY-DROPPED{Crate-loaded-asleep,never-moved};set_linear_velocity-already-activates-in-world-body{documented};new-erhe_physics_tests{4;gated-ERHE_BUILD_TESTS+jolt;built-in-build_ninja_win_vulkan-reconfigured-with-tests-ON};AGENTS.md-suite-list+physics;headless:Crate-moves-1m/s+90deg/s,Rock-asleep,dynamic_enable-off=world-not-stepped}
✓commit-C{Content_library::get_scope-split=get_existing_scope+make_kind_scope{registered-before-placed;detached};make_library_insert_operation=single-resource-insert-builder{14-sites};Kind_scope_operation-prepended-when-scope-not-standing{execute=no-op-if-placed;undo=remove-only-while-childless};Content_library_folders_operation-same-rule;smoke-test-55/55{+3-kind-scope-checks};LEFT:gltf_extensions_import-legacy-folder_path-brush-read-non-undoable-get_scope}
  P1-RESIDUE-DONE-2026-09-12{section-6-left=glTF-physicsJoints[].limits-re-import-parse-failure}

[TASK::time-samples-beyond-the-transform]{DONE-2026-09-12;via-harness;3-commits}
✓commit-1{609628bf6:erhe::scene-Animation_channel-names-Dependency_property+Item_base-target;Animation_path=classification;WEIGHTS-gone;scene-tests-131->135;usd-344-green;headless-playback-verified}
✓commit-2{89083b431:USD-reader/writer-time-samples-on-light/material/visibility-attributes;attribute_samples.usda;usd-tests-344->351;headless-seek-0.5s-verified;usdchecker-Success}
✓commit-3{fbd21f60c:Ts-splines-on-intensity/roughness/metallic/opacity<->CUBICSPLINE;hermite-write-back;attribute_splines.usda;usd-tests-351->358;headless+usdchecker-verified}
  plan-section-2-statement+section-6-entry-dropped;prompt_queue.txt-deleted|?user-interactive

[TASK::frame-time-after-usd-import]{via-harness;started-2026-09-14}
!cause::tracy-D:\erhe.tracy{post-DrawModes-import-tick-128-150ms:update_material_sets-104-124ms{Material_set::update-hashed-every-material-every-frame-x3-sets;Material::get_values=26-layered-property-reads}+check_material_changes-12-15ms{Shader_key-derive-per-material-per-frame};import-tick-30.5s{load_usd_prefab_template-35x-16s+rebuild_display_colors-13s-main-thread};Asset_browser-ctor-8.1s-walks-res/editor/assets}
✓step-1{350943536:Material::get_change_serial+data-private-get_data()+Texture_reference_user{Graph_texture-notifies-on-rebake};all-direct-data-writers-moved-to-setters}
✓step-2{005dd0756:Material_slot::recorded_serial-optional;update()-gates-on-serial;check_material_changes-re-derives-only-on-serial-move;get_content_hash-gone;MEASURED:tick-6.1ms-median,update_material_sets-0.47ms,check_material_changes-0.04ms}
✓step-3{372309b97:Prefab_key=consumed-variant-sets-only{Prefab::consumed_variant_sets;reduce_variant_selections;self-correcting-reload};Prefab_instance-keeps-full-arc-selection;35->25-templates,16s->10.4s;internal-arc-premise-was-false{siblings-already-share-key};roundtrip-413/416-baseline}
✓step-4{c46021a4e:rebuild_display_colors=kickoff;Display_color_build-grouped-by(geometry|soup,color,normal_style,skinned)->one-task-per-group-via-async_for_nodes_with_mesh;worker-builds+Scene_commit_queue-swap;host-check-at-commit;sync-path-kept-for-no-worker-contexts;MEASURED:13.1s->0.42ms-worst-call,7-builds-on-workers;FOUND-preexisting:second-close_scene-of-closed-scene-aborts-at-Scene_root::unregister_from_editor_scenes-VERIFY}
✓step-5{8ae7796d6:Asset_tree+Asset_scan_request{Gltf_scan_request-shape};scan()-submits-to-tf::Executor{explicit-ctor-arg};apply_finished_scan-on-main{imgui()+refresh_file};pending-refresh-paths-queued;MEASURED:ctor-8.1s->0.12ms,walk-7.7s-on-worker}
✓cards-interactive-fixes{c5ea88930:cross-pair-coincident+single-sided{culling-resolves-pair;adapter-2^-23-epsilon-z-fights}|78c390969:Z--card-UV=adapter-uv_flipped_st{other-faces-unflipped};ref=<OpenUSD>/pxr/usdImaging/usdImaging/drawModeAdapter.cpp-_GenerateCardsGeometry+_GenerateTextureCoordinates;edit-only-user-tests;LEFT:card-image-borrowing-from-opposite-face{adapter-does,erhe-draws-flat-color;plan-section-6}};USER-VERIFIED-2026-09-14{z-fight-gone}|?user-interactive{Z--card-upright}
  PLAN-DONE-2026-09-14{5-commits;post-import-tick-128-150ms->6ms;import-tick-30.5s->12.5s;left=per-file-USD-stage-cache{25-parses->1-per-file,plan-step-3-note}+preexisting-double-close_scene-abort}|?user-interactive{DrawModes-import-windowed;material-edit-live;texture-graph-bake-updates-material;Asset-Browser-populates-after-startup}
!trap::erhe_scene_renderer_gpu_tests-needs-live-display{aborts-exit-3-after-env-set-up-when-display-asleep;not-a-code-failure;16/16-pass-2026-09-14-with-display}
✓follow-ups-2026-09-14{058503b8d:Scene_root::request_close-single-producer{refuses-pending/unregistered;MCP-batch-same-frame-double-close-verified};headlight-on-imported-scene=not-a-defect{create_scene-default-camera-sees-CeramicBlack-row,displayColor-0.025;headlight-measured-working}}

[TASK::drawmodes-fidelity]{plan-section-3-item-1;via-harness;started-2026-09-13}
✓camera-exposure-stop{3f93df1e3:import-2^stop,export-log2;capture-no-longer-black;usd-tests-360}
✓step-1-arc-carried-variant-selection{daa4f7fc9-erhe::usd+30c2fba97-editor;C7;usd-tests-366;headless:references_variants.usda+DrawModes.usd-7-templates;roundtrip-411/414}
✓step-2a-nested-variantSets{435656f6d:enclosing_set/variant-fields;resolve_selected_variant_name=carried>block-variants>prim>first;Variant_branch_state-gate;writer-nests-set-in-block;material_path-remapped-through-M2-renamed-hoisted-scope;usd-tests-374;editor=pass-through-only}
✓step-2b-Gprim.display_color{ea69a023b:entry-vec3-inherits-default-0.18;Scene_host::on_mesh_display_color_changed->Scene_root-queue->App_scenes::rebuild_display_colors{Build_info::constant_color|make_triangle_soup_with_constant_color;mark-has_vertex_colors};importer-fills+admits-primvars:displayColor-constant-opinion;writer-constant-primvar;usd-378/scene-139}
✓step-2b'-pending-variant-opinions{7dd783c4c:Usd_variant::pending_overrides/bindings{path-behind-arcs};editor-apply_pending_variant_opinions-after-resolve_usd_references{find_instance_item;base-values};find_instance_item-fix:clone-that-is-carrier-transparent;carried-selection-validated-against-every-same-name-nested-set;Mesh-host-attach-queues-display-color-rebuild;usd-383/scene-142;DrawModes:6-Utah-colors-visible-via-vertex-colored-default;roundtrip-413/416}
!FOUND::Fancy-PorcelainFlowers-column-near-black{material-now-bound;converts-metallic-1/roughness-1;img_ARM-channel-inputs-suspect;needs-diagnosis-commit-after-step-3}
✓step-3-PrimvarReader{dbe7bf822:Material_input_source{value|vertex_color};base_color_source/opacity_source-props;record-uvec2-input_sources+padding;standard.frag+erhe_ray_hit.glsl-branch;load_stage-strips-PrimvarReader-connection+records;writer-authors-reader-prim;outputs:surface-followed-through-NodeGraph;usd-390/primitive-46;DrawModes-Ceramic-converts;SpinningPyramids-3/3;validation-clean;C8}
✓appended-material-images{48dcc1355:Tydra-imageMap-outlives-conversion->ids-index-scene-list;swap-scene-lists-into-converter;shift_texture_ids-gone;usd-391}
✓Fancy-column-NOT-a-defect{authored-camera-crops-Fancy-column-out-of-frame;porcelain-renders-correctly-at-HEAD;no-commit}
✓step-2c-editor-switch{1e6da0f83:Variant_set_key{prim+set+enclosing-set+variant};Variant_selection-codegen-v2;switch-compound:old-block's-nested-sets-off->own->new-block's-nested-on-recursive;dead-branch=record-only+combo-disabled;switch-resolves-via-find_instance_item;MCP-enclosing_selected;roundtrip-413/416;C9}
✓step-4a-record{e1a7b3cce:erhe::scene::Draw_mode_description{USD-tokens-as-enum-labels;authored-flags};Usd_data::draw_modes+Usd_save_arguments::draw_modes;override-name-Draw_mode.<prop>-via-find_override_property_target;variant-blocks-carry-model:*;usd-400}
✓step-4b-attachment{3233e6e46:editor::Draw_mode{Item_type-bit-56;entry-props;resolved_draw_mode-ancestor-walk};pruning=Item_base::set_prunes_children+Hierarchy::is_pruned_by_parent-ANDed-in-active-derivation{attachments-exempt};Draw_mode_renderer-background-Tool{bounds-box+origin-tripod-lines;registry-on-Scene_root};extent=extentsHint|measured-mesh-AABB-cached;item-tests-188}
✓step-4c-cards{492c3e721:draw_mode_cards.cpp{cross=2-faces/axis-epsilon-apart,box,fromTexture-worldtoscreen-PNG-tEXt-fallback-box;adapter-UV-order+erhe-V-flip};proxy=session_only+Item_flags::draw_mode_proxy(bit-40)-child-Mesh{exempt-from-pruning;pick-redirect-to-model-prim;materials-under-proxy-not-library};App_scenes::rebuild_draw_mode_proxies-queue}
✓step-4d-values-through-variants+arcs{192d5a22b:erhe::scene::register_applied_schema_attachment{class,owner-type,factory}->find_override_property_target-materializes-Draw_mode-on-opinion;link_carrier_attachments_to_target{carrier-attachment-set_reference(target's);no-local-clearing};card-texture-relative-text-resolved-vs-authoring-file;DrawModes:colored-bounds/origin+textured-cards;survey-row=works,storm_match-0.16}
✓step-4e{3bc9c275d:card-face-alpha_test-cutoff-0.1{adapter-opacityThreshold};inactive-prim-owns-no-proxy{handle_flag_bits_update};survey-row-works-storm_match-0.18;residual=Storm/usdrecord-draws-bounds/origin-as-filled-slabs-vs-usdview-lines}
  ITEM-DONE-2026-09-13{plan-section-2-C7+C8+C9+C10;section-3-item-removed;queue-item-removed}|?user-interactive{open-DrawModes.usd-windowed:5-rows-match-reference;Properties-Draw_mode-rows;variant-combo-nested-sets;pick-a-card->selects-model-prim}

[TASK::asset-browser-two-phase-scan]{DONE-2026-09-14;c5d81a1d9;via-harness;1-coder}
✓step-1{Asset_walk-worker+Asset_scan_entry{path,parent_path_key,Asset_node_kind}+apply_scan_progress;headless:7-publish-lines-~50ms-apart+save-refresh-node-added}
✓user-verified-2026-09-14

[TASK::lightusd-fork-fixes]{DONE-2026-09-14;plan-section-3-item-1-removed;via-harness;4-coders+4-scouts}
✓commit-1{00842c2d1;fork-896dc0559-tag-14a:Tydra-unresolved-connection=schema-fallback+warning;erhe-strip-pass-gone;usd-401}
✓commit-2{4e65094fd;fork-c4d61a6ff-tag-14b:USDA-escape-pair;erhe-string/token-verbatim,single-quote-swap-gone;usd-401}
✓commit-3{233688d69;fork-22181bf3a-tag-14c:ComposeRelationshipTargets;erhe-instancer-skip-warning+sublayer-fixtures;teapotScene-1235-lost-instances->0;usd-405}
✓commit-4{3b211f211;fork-22af62ad7-tag-14d:inputs:st-float2;usdchecker-clean;roundtrip-413/416;usd-405}
!left::tags-a..d-unpushed{user-pushes};ninja+headless-trees-carry-CPM_LightUSD_SOURCE-override->reconfigure-after-push

[TASK::load-performance]{DONE-2026-09-14;plan-section-2;via-harness;3-coders+2-scouts}
✓commit-1{004bc4fd8:Scene_root-shape-to-meshes-index;measured-null-alone}
✓commit-2{18d269042:hover-gate=App_context::is_scene_load_in_flight;get_async_status.idle;headless-cannot-exercise-per-frame-hover}
✓commit-3{721be3dca:deferred-finalize-commit-collects-sharers-only-on-swap{quadratic-refresh-was-the-cost};USD-phase-breadcrumbs;simpleAssetScene-340->203s,stalls-BVH-commit-39->0}
?left::synchronous-load-on-tick-thread{plan-section-3-item-1-Asynchronous-load;simpleAssetScene-203s='usd: attach to scene'}

[TASK::active-item]{doc/editor/active_item.md;via-harness;started-2026-09-15}
✓plan{7f0409f36}+phase-1{41c535c30:state+rules+message+undo-snapshot+MCP+Mcp_test;item-tests-188;Mcp_-52/53(first-case-flake-pre-existing);smoke-55/55;scene-close-clean}
✓phase-2{outline-3rd-color-via-Primitive_interface_settings::constant_color_active+get_selected_color;TRAP:selection-outline-is-drawn-by-Content_wide_line_renderer-fed-in-viewport_scene_view/headset_view-not-primitive_buffer;hierarchy-accent+Properties-order;headless-pixel-verified}|?user-interactive{accent-tint,Settings-fields}
✓3a{05646d4e7:get_active_item_as<T>+is_command_reference_host;Tool::get_node/Brush_tool-Parent-to-Active/flip_joint/create_brush/rigid_body/joint/paste/Create::find_parent;D7-map=Material+Brush-only;MCP-cannot-reach-5-of-7-rows-bare->user-interactive}
✓3b{8482a6c92:resolve_operation_items(Operation_reference{operands_only|active_is_target})-active-mesh-first,inserted-when-unselected-for-merge/CSG-only;Merge_operation-items-from-resolver,depth-sort-gone;Attach=attach_selection_to_active-compound+MCP-tool;MCP-csg/retarget-name-active-explicitly;headless:merge-survivor=unselected-active,CSG-target=active,Attach-children,CatmullClark-leaves-unselected-active-alone}
✓3c{cc2f309cd:update_target_nodes-rotates-active-target-to-front;anchor-rotation=representative-entry(was-last);on_active_item-rebuild-outside-component-mode/drag}
✓phase-4{plan-doc=standing-description;section-3=verification+user-interactive-list}
  ACTIVE-ITEM-DONE+USER-VERIFIED-2026-09-15

[TASK::ci-tests]{DONE-2026-09-15}
erhe_tests-aggregate-target+gpu/editor-labels+wrapper-arg-pass-through+build.yml-ctest-step+tests.yml-verdict-workflow+ci_test_summary.py+README-tests-badge+AGENTS/building/graphics_test_coverage-docs
verified-local:build_vs2026_vulkan-reconfigure+erhe_tests-build-clean+ctest--LE-gpu|editor-1371-tests{gpu-80,editor-53-excluded}
?first-CI-run-after-push{Linux/macOS/Windows-headless-test-builds-never-built-here}

[TASK::newtons-cradle-physics-drag]{DONE+USER-VERIFIED+PUSHED-2026-09-17;via-harness;4-coders}
✓creation-21{8974faec7+fdccecf7d+cabe99c3d+821303b6c+fcd084d98:cradle+backend-gap-flags+--keep-windows+--scene-only+pivots-in-the-frame}
✓drag-through-physics{6de7b258b-physics-springs+Box3D-joint-stiffness|873fe258c-Transform-tool|8b9317662-Joint_reach|7fb8411a4-project_ray|535597229-both-tools-project+Physics-tool-bounded|9aecd84b8-Node_joint-rebuild-scope|05ca66ada-braking-drag-point}
✓instrumentation{621049a60-editor.physics_drag-monitor}->removed-after-verification{41eeff517}+replaced-by-scripts/physics_drag_joint_sweep.py{878106c4d}
✓cradle-hinge-unlimited{6bfd7ca5e:removes-the-last-flake=undragged-ball-hitting-a-hard-limit-on-Jolt}
!measured::hold<=0.23mm-Jolt/0.05mm-Box3D,after-release<=0.37/0.13;reported-drag-704mm->0.01mm;far-ball-peak-unchanged-0.408/0.406
?left::doc/agents/creations.md-entry-21+doc-image-are-in{8974faec7};unjointed-drag-fly-off+Box3D-0Hz-rigid-path-unfixed

[TASK::doc-restructure]{DONE-2026-09-18}
✓checker+moves+notes-migration+headers+index{5-commits}
✓content-sweep{9-group-commits;~190-docs;18-docs-folded/deleted;~45-plans-created}
?changelog=prompt_queue.txt-item-1{erhe::*-API-only}

[TASK::rigging-phase-2]{started-2026-09-18;via-harness}
✓pole-target-slice{484bac408+67aafe503+c59ba15b3+08a7a73e0;ik_pole_verify.py-all-pass;solver-tests-22;roundtrip-418/421}
✓effector-orientation{ebf606ff2;verify-5/5}+chain-visualization{edaf10d98;solver-tests-26;visual-check-interactive-only}
?left::stiffness+Phase-1-feel-questions{need-user-hands-on}->Phase-3
?user-interactive-deferred{ik_settings-slice;pole-picker+angle-rows;live-drag-with-pole;Move-tool-Effector-Orientation-combo;chain/root/pole-visualization-during-drag}
⚡interactive-pass{doc/plans/rigging/interactive_test_pass.md;0-3-PASS;next=section-4;user-resumes-2026-09-19}

[TASK::ik-as-node-properties]{DONE-2026-09-19;via-harness;4-coders+1-scout}
✓commit-1{7f4739a90:erhe::property-Weak_object_reference;property-tests-140}
✓commit-2{82b9db1be:Ik.*-attached-properties-on-Node+bind-pose-rest-default+Ik_settings/ERHE_rig-code-removed;ik-solver-tests-29}
✓commit-3{2beea77db:ERHE_node.property_node_refs+by-name-resolution-after-node-inserts;roundtrip-426/429}
✓commit-4{5387c7e30:docs+plans/node_attachments_to_properties.md}
?user-interactive{interactive_test_pass.md-resume-note-at-section-4}
