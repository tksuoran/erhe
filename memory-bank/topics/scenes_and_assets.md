§MBEL:5.0
©erhe::Topic::scenes_and_assets
@scope::Scene persistence (glTF), glTF uids, asset manager, inventory slots
@docs::doc/editor/scene_serialization.md+doc/editor/asset_manager.md+doc/gltf_extensions/

[PATTERNS]
ScenePersistence::single-erhe-authored-glb{ERHE_scene-marker+ERHE_*-extensions;ref:doc/editor/scene_serialization.md;wire:doc/gltf_extensions/;¬persisted:unused-library-materials+session-state+Brush_placement-attachments+static-body-mass}
!gltf-export-rule::soup=source-of-truth-when-present{exports-full-vertex-attrs¬ERHE_geometry;geometry-normative=authored-only;dual-list-NORMAL-requires-fully-present-present_*-mask;import_root-wrappers-transparent{children-in-their-place};settings-less-joint→empty-joint-description{reload-materializes-settings-item}}
GltfUid::Item_base.m_gltf_uid{glTF-2.1-#2597;assigned-once{import|first-export-store-back}+never-changed+¬copied-by-clone;export-stamps-item-backed-objects-only{¬accessors/buffers/samplers/extra-meshes/synthesized-nodes→no-churn};uid+name=one-identifier-namespace-per-file;isolated-behind-erhe::gltf{pre-ratification};since-577d9f75}
ScenePersistenceVerify::scripts/scene_roundtrip_verify.py{fresh-headless-session;all-11-ERHE_*-build→schema-validate+reload-MCP-diff+prefab-roundtrip+Khronos-validator{0-errors}+Blender-render;run-book@doc/editor/scene_serialization.md}
AssetManager{R1-a5cdda26}::single-loader-axiom{acquire=only-asset-materialization;registry+usership;src/editor/assets/}::Asset_key{scope:builtin|scene_local|file;uid-wins→unique-name-fallback→ambiguous=loud-error;¬index-field;self-heals-name→uid}+Asset_reference{holding-resolved=registered-user;special-members-maintain-bookkeeping;adopt{R2}=exact-object-adoption-for-drag-drop¬name-re-resolution}+get_or_load_container{one-parse_gltf;free-root-node-¬holding-scene;refuses-open-as-scene-until-R5}+request_unload{container-granularity;refuse-naming-users;weak_ptr-exclusivity-verify→"undeclared asset user"}+debug-holds{MCP:acquire_asset/release_asset/unload_asset};file-scope-types=material+animation-until-R5/R7;scene-open/import-NOT-routed-yet{R5-flip}
AssetSlots{R2-95ec5eec}::Slot_entry-brush/material=Asset_reference{labels-name-slot;per-frame-resolve-in-Inventory_window::imgui{window-must-be-visible→MCP-set_window_visibility};Asset_reference_data-v1-config-codegen{scope+asset_type+path+uid+name};Inventory_slot-v4{legacy-names-read-migrated¬written};collect_pinned_items=transitive-brush-material-only;scene_local-resolution-binds-container-copy-when-loaded{registry-before-scenes}}
BackgroundSave{2026-11-03;31f3bb06c+b4a46d04a}::Scene_save_service{src/editor/scene/scene_save_service.*;File>Save+Prepare(PC|Quest)+MCP-save_scene/prepare_scene;waits-until-quiescent(is_scene_load_in_flight+no-mouse-drag+no-component-edit+no-transform-animation)->begin_scene_save_gltf(main)->write_scene_save_gltf(worker-std::thread)->finish_scene_save_gltf(main);one-at-a-time;doc/editor/scene_serialization.md"Saving-in-the-background"}
SaveReadOnlyGates::Operation_stack::is_held(queue+execute_now-deferred;undo/redo-declined)+scene-commit/geometry-graph/lightmap/PVS/animation-held+Imgui_windows::set_window_contents_disabled+menu-disabled+hover-flags-not-written+saved-scene{physics-skip;viewport-no-pointer(Scene_views::is_input_blocked);fly-camera-skip;Scene_root::request_close-refused(set_save_in_progress)}+MCP-refuses-all-but{get_save_status,get_async_status,get_viewports,list_scenes,capture_screenshot,save-tools}
SaveProgress::erhe::gltf::Gltf_export_progress{stages:mesh_data|meshes|scene|textures|encode}->status-line;soup-read-back-parallel(was-serial-30s);Bistro-Prepare(PC)-55->26s
PreparePcLods::Gltf_export_arguments::default_lod_settings<-get_prepare_lod_settings(pc->editor-config-lod;quest->none);Bistro:2900-chains+952-no-simplify;LOD-Bias-2:2.64M->1.08M-tris;Properties:Optimized-Buffer-Mesh>"Level of Detail"
OpenSceneGltfDefaultCamera{7acfc3c5e}::finish_open_scene_gltf-injects-session_only-default-camera-when-scene-has-none(was-USD-only->camera-less-prepared-files-rendered-black)

[TRAPS]
✓variants.gltf-"invalid vector subscript"::diagnosed-2026-10-03{fixture-primitive-1-indices-4..7-into-4-vertex-accessor(invalid-glTF)+Triangle_soup->Geometry-assumed-indices-start-at-0;fixed-both+importer-skips-out-of-range-indices;regression=erhe_gltf_tests{out_of_range_indices.gltf}}
!known-2026-10-04::scene_roundtrip_verify.py-fails-3-on-baseline-7c7c0c0bf{reload-diff-dynamic-bodies-keep-falling(glTF-leg-no-toggle_physics);DEVICE_LOST-write-fault-on-authored.usda-after-glTF-leg(draw-list-path-only);USD-textured-local_property_names+references_override}->doc/plans/audit_2026_09_30_followups.md"Follow-ups found by review"
!background-save-open::doc/plans/background_scene_save.md{disabled-windows-not-dimmed(input-blocked-verified);Mcp_test.property_row_is_addressable_by_its_label-fails(unverified-vs-HEAD~;user-config-modified);Quest-LOD-decision(user);save_container+USD-still-main-thread;Bistro-animation-close-leak}
!mcp-readiness-trap::wait-for-"listening on 127.0.0.1:3743 (pid <pid>"-of-THIS-run{logs/log.txt-holds-previous-run-until-rewritten}+stop-own-editor-by-launch-pid-only(AGENTS.md"Stopping-processes")

[FOCUS_ARCHIVE_2026-11-07]{compacted-pointers;moved-from-activeContext.md}
@world-space-vertices-session-2026-11-06::DONE{doc/plans/prepare_pvs_export.md}
@scene-export-settings+world-space-bake::BUILT-2026-11-04{doc/editor/world_space_bake.md}
@item-tree-open-state::DONE-2026-11-04{8dad29527+391c288c1}
@background-scene-save::DONE-2026-11-03{memory-bank/topics/scenes_and_assets.md;doc/plans/background_scene_save.md}
@gltf-fast-load::STEPS-1-8-DONE-2026-10-25{doc/plans/gltf_fast_load.md;doc/reference/memory_usage_2026_10.md}
