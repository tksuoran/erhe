§MBEL:5.0

[FOCUS]
@harness::doc/agent-orchestration-harness.md{e2605f494,2026-09-05;©User-asked-token-saving:orchestrator-writes-brief-per-commit→fresh-opus-coder-edits+builds+verifies+leaves-uncommitted→orchestrator-reviews-diff+commits;coders-strictly-sequential{shared-build-trees+MCP-port};fixes-via-SendMessage-same-agent;Explore-scouts-for-pre-brief-questions}
@usd-compatibility::doc/usd-compatibility-plan.md{M1✓4d400211e{path=names-below-root,root-excluded,'/'-sep;find_by_path;get_reference_path=path|name;resolvers-accept-both-forms;MCP-paths}+M2✓{sibling-unique:make_sibling_unique_name<base>_<n>-from-1;choke-point=handle_add_child;refusal=name-bridge-validate{Property_bridge::validate+Dependency_object::validate_value}};roundtrip-baseline-green✓{70abffecd..4f7a7a45e:instantiate_prefab-dangling-stack-capture-segfault+builtin-assets-not-record-assets+owning-entries-win-name-over-reference-entries{D1b}+harness-async/additionalProperties/sort+light-bag-test;93/93};M3✓{5fe65f45d-D31-compute_default-per-object-default-layer+7eb07c8c2-Item_base::purpose_property{Purpose::default_/render/proxy/guide;default-derived-from-tool|brush|controller|rendertarget|!show_in_ui;draw-list-filters-keep-flag-tests}};L1-in-progress,2026-09-05}
  goals::G1-load+edit-USD{no-save}->G2-save-USD->G3-glTF-or-USD-independent{never-converted/mixed};C1=each-format-carries-own-features{no-glTF-ext-for-USD-features}
  order::M1-item-paths->M2-sibling-unique-names->M3-purpose-enum->L1-LightUSD-optional-CPM{ERHE_USD_LIBRARY}->I1-import-USD-asset{Tydra}->M4-local=authored->I2->E1-save-USDA->E3-roundtrip
  mapping::doc/usd_compatibility.md{property-layers<->USD-opinions;per-domain-tables;plan-never-restates-rows}
  future-work::animation+physics{plan-section-5};clones::memory-bank/local/context.md{<LightUSD>,<OpenUSD>}
  L1-traps::LightUSD-bundles-meshoptimizer/fpng/miniz/libjpeg-turbo{dup-symbol-check};LIGHTUSD_WITH_GEOGRAM-off;not-/WX-clean
@style-library::doc/style-library.md{R1-R6,D1-D5}✓2026-09-04{9132674f2+a88dd405c+persistence+docs}
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
  verify✓headless{scratchpad-verify_light_inherit.py;doc/style-library.md-step-6}|?user-interactive{Create-Style>Light,Add-Property-on-empty-node}
@folder-category-properties::D30{secondary-owner-type}✓2026-09-04{5ff73f41b+59dd60042+34d66432b}
  Content_library_node.category_owner_type→get_secondary_property_owner_type{folders-only};Material-value-props-inherits=true;Add-Property-offers-"Material.<name>"-on-Materials-folder;listing=is_extra_property_listed+collect_addable_properties
  trap::Material-visible_when-lambdas-static_cast-to-Material→NEVER-evaluate-on-a-folder{secondary-listed-by-local-value-only}
  limitation::material-own-values-are-local{Reset-to-default-lets-folder-through};glTF-export-bakes-effective→local-on-reload{folder-value-persists}
  texture-slots::entry-store+inherits{db69e84fc;Material::on_property_changed-mirrors-effective-value-into-Material_data;set_data/create-info:default-field=unset,else-local;folder-texture-ref-resolves-via-Content_library_node::resolve_expression_object;find_scene_root_for_item-knows-library-nodes}
  verify✓headless{scratchpad-verify_folder_material.py:addable→set-folder-red→clear-Copper→inherited→undo/redo→remove→save/open→close-clean}|?user-interactive
@content-library-folders::doc/content-library-folders.md{R1-R6,D1-D7}✓2026-09-04{5-commits-48518c02f..7993068f6+follow-ups}
  D1::Item_base.m_inheritance_container{set-by-Content_library_node::handle_add_child;cleared-in-dtor;for_each_inheritance_child-visits-item;reference-entries-never}
  D2/D3::Create-Folder-menu{scene_root.cpp}+drag-onto-folder{Content_library_move_operation}
  D5/D6::ERHE_scene.library_folders{path+properties+items;brushes-folder_path-read-only}+Content_library_folders_operation{last-in-import_gltf_editor_state}
  D7::MCP-create_library_folder+move_library_item{folder_path,undoable}+find_item_in_scene-visits-library-nodes
  trap-fixed::add/remove-scanned-direct-children-only→duplicate-node-per-item-after-folder-move{find_entry-subtree;get_all-caches-cleared-up-to-root}
  verify✓headless{scratchpad-recipe:create→move-Copper→set-visible-false→inherited→undo/redo→save/open→local+inherited→close-clean;undo_reference_clearing_smoke_test-45/45}|?user-interactive{Create-Folder,rename,drag-drop,Ctrl+Z}
@property-system::erhe::property{doc/property-system.md=design-record;doc/property-inventory.md=per-field-status;src/erhe/property/notes.md=library-reference}
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
  left::graph-node-parameters{doc/property-system.md-section-6;recipe=section-4.18;handoff-doc-deleted-2026-09-05;only-when-user-asks}
@properties-window-single-path::step-1✓2026-09-05{89986a2b3+7789f1332:Item_base-name/tags-string-bridges+authored-flag-bool-bridges{Locks-group;developer_only-for-obscure-bits};Property_flags::writable_when_sealed{lock_edit-owns-seal;Dependency_object::is_write_sealed(property)=per-property-check-in-rows/context-menu/MCP-set_item_property};window-Name/Locks/flag-grid+material-Name-row-removed;dev-mode-Flags=read-only-to_string}
  verify✓headless{scratchpad-verify_item_rows.py:name/tags/lock_edit-set+undo;sealed-refuses-name+accepts-lock_edit=false}+erhe_item_tests-129/129|?user-interactive{multi-select-per-type-sections,Locks-checkboxes}
  modes✓{9d989e29a:Individual|Combined-combo-left-of-Pin{default-combined;single-item=individual};combined=per-type-sections-only;mixed=per-component{mixed_mask+merge_components+drag_components;"mixed"-placeholder-format;bool=ImGuiItemFlags_MixedValue;quat/string/enum/object=whole}}|?user-interactive{drag-one-component→only-that-component-on-every-item;one-undo-entry}
  step-3✓{7c5e2d9f7+6f43e57e0:Material_sampler_state{plain-data;defaults=Sampler_create_info}+35-entry-props{<slot>_texture_wrap_u/v,min/mag_filter,mipmap_mode,max_anisotropy,lod_bias(dev)};Material_sampler_cache-in-Material_buffer{state→Sampler;replaces-fallback};no-Sampler-on-materials{gltf-import/export,texture-nodes,rendertarget,MCP-edit_material-device-free};inspect-snapshot+m_material_state-gone}
  step-4✓{4cb30bd4a:Brush::material_property+Geometry_graph_mesh::graph_mesh_property{register_member;after_set=release+apply};item_properties=frame+item_diagnostics;R5-list=joint-limits/drives+collision-filter-lists+layout-track-extents+scene-block}
  DONE::item-0-removed-from-queue-2026-09-05|?user-interactive{material-sampler-rows,brush-material-row,graph-mesh-row}

[STATE]
@branch::main{#18-commits-unpushed;user-pushes-themselves}
prompt_queue.txt::item-0=USD-compatibility-plan{only-item;GL-slot-scope-narrowing->doc/gl-worker-context-enforcement.md-Follow-ups-F}

[OPEN]
?startup-log-error::"property 'mass': value rejected by validate callback"{pre-existing,unrelated-to-M1/M2}
?startup-log-error::"property 'mass': value rejected by validate callback"{pre-existing,unrelated-to-M1/M2}
?user-interactive-check{folders+category-props+texture-slots+styles+node-attachment-values+camera+physics-materials}→expect-fixes;then-migrations{Node_physics-first}
?material-reload-limitation-still-open::ERHE_material-bakes-effective→local{lights-fixed-via-ERHE_light.properties-rule;same-rule-for-materials=candidate}
?startup-log-error::"property 'lightmapped': object is sealed"{pre-existing,at-startup,unrelated}
?inherits-registration-check{doc/property-system.md-section-6}
?Light-derived-rows→Rendertarget_mesh→Animation{doc/property-inventory.md}

[BLOCKERS]
none
