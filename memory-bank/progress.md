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

[TASK::usd-compatibility]{IN-PROGRESS-2026-09-05,via-doc/agent-orchestration-harness.md}
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
⚡Q1-Quest{android-build-coder-running;launch-needs-user}


[NOTES]
!headless-recipe::build_vs2026_vulkan_headless-editor→ERHE_AI_DRIVER=1-launch-hidden→mcp_call.py-b64-args{get_item_properties/set_item_property/get_addable_item_properties/undo;ids-reshuffle-per-launch;scene_name-required-for-create_node/select_items/get_node_details}
!default-scene::floor-mesh-sealed{lock_edit}→use-cube-attachment-for-attachment-tests
!scene-close-check::close_scene→wait≈6s→grep-"scene-close"{clean="all N released"}
!clangd-db::re-run-configure_ninja_win_clang.bat-after-adding-source-files{done-2026-09-04}
