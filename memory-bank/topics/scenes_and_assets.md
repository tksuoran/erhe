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

[OPEN]
?async-mesh-op-fails-"invalid vector subscript"-on-minimal-2-primitive-glTF-mesh{src/erhe/gltf/test/data/variants.gltf;variant-free-copy-reproduces;vertexcolor_test_grid.glb-does-not;found-X4-c3;NOT-diagnosed}
