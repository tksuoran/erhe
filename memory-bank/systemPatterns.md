§MBEL:5.0
©erhe::ArchitecturePatterns

[LIB_STRUCTURE]
src/erhe/<name>::CMakeTarget{erhe_<name>}
EachLib→notes.md{purpose+types+API+deps}!checkFirst
Core::gl+graphics+rendergraph+scene+scene_renderer+geometry+primitive+item+renderer+imgui+physics+window+commands+log+verify
Editor::src/editor{main.cpp→run_editor()}+rendergraph/+tools/+operations/+windows/+scene/+res/

[KEY_PATTERNS]
Rendergraph::DAG{Rendergraph_node,typed-io,exec/frame}
App_context::holds-all-parts+shared-resources
!rule::PartCtor¬ReadAppContext{nullptr-until-post-construct}→PassRefsExplicit
Backends::swappable{#ifdef ERHE_<SUBSYS>_LIBRARY_<VALUE>}{physics+raytrace+window+xr}
ScenePersistence::single-erhe-authored-glb{ERHE_scene-marker+ERHE_*-extensions;ref:doc/scene_serialization.md;wire:doc/gltf_extensions/;¬persisted:unused-library-materials+session-state+Brush_placement-attachments+static-body-mass}
!gltf-export-rule::soup=source-of-truth-when-present{exports-full-vertex-attrs¬ERHE_geometry;geometry-normative=authored-only;dual-list-NORMAL-requires-fully-present-present_*-mask;import_root-wrappers-transparent{children-in-their-place};settings-less-joint→empty-joint-description{reload-materializes-settings-item}}
GltfUid::Item_base.m_gltf_uid{glTF-2.1-#2597;assigned-once{import|first-export-store-back}+never-changed+¬copied-by-clone;export-stamps-item-backed-objects-only{¬accessors/buffers/samplers/extra-meshes/synthesized-nodes→no-churn};uid+name=one-identifier-namespace-per-file;isolated-behind-erhe::gltf{pre-ratification};since-577d9f75}
ScenePersistenceVerify::scripts/scene_roundtrip_verify.py{fresh-headless-session;all-11-ERHE_*-build→schema-validate+reload-MCP-diff+prefab-roundtrip+Khronos-validator{0-errors}+Blender-render;run-book@doc/scene_serialization.md}
ItemHostMutex::async-workers-lock-scene-item_host_mutex-for-hosted-state-mutation;main-thread-consumers-lock-too{Scene::update_node_transforms-locks-internally-since-f5a58c5b}
!geogram-threading::PDEL/parallel-algos-require-no-other-geogram-threads{unenforceable-in-erhe→use-sequential-BDEL-for-convex-hull;asserts→ASSERT_THROW-sans-debugger;upstream-ask:doc/geogram.md}
!geometry-payload-invariant::graph-payload-geometries-carry-connectivity+edges{process_for_graph}→every-producer-incl-merges-must-process{violation=fastfail-on-worker,uncatchable}
GraphPins::input-default-single-link{editors-replace-on-connect,one-Compound-undo}|Pin::multi_link=multi-input-socket{accumulate;join+instance-points+realize-instances}
GraphSourceNodes::external-item-refs{brush/scene_mesh}capture-geometry-MAIN-thread{lazy-getters-worker-unsafe}+serialize-by-name{owner-scene→all-scenes-resolution;shadow-clones-ownerless}
!scene-close-bug-class::parts-caching-scene-hosted-refs-must-handle-close{weak_ptr-insufficient:own-resolve-cache-pins-item}→PREFERRED:per-part-close_scene-subscription{part-drops-own-refs-by-get_item_host()-check;since-856dedd3:Brush_tool+Material_paint_tool+Material_preview+Brdf_slice+Physics_tool+Operations+Animation_player/window}||validate-App_scenes::is_host_registered(get_item_host())-on-access{precedent:Geometry_graph_window::resolve_target};rationale:parts→item-refs-invisible-from-item-side→push-must-reach-parts{virtual-Item::handle_item_host_update-rejected-for-this:wrong-direction;per-item-observers=R-phase-Asset_manager-userships}
!scene-close-watchdog::on_close_scene-only-QUEUES-scene{pending-shared_ptr};update_scene_close_leak_watches-ARMS-post-pump-in-tick{after-ALL-close_scene-subscribers,subscription-order-irrelevant}→60-frames→"scene-close leak:"-warn=bug{+"N holder(s)"=use_count-since-8df79fa1}|slot-pinned-items{Hotbar+Inventory-collect_pinned_items:brush+brush-material+material}→info-"intentionally pinned"{persistent-inventory-by-design}|asset-manager-pinned{Asset_manager::is_pinned:owned-strong-ref|declared-usership}→info-"intentionally pinned by the asset manager";clean="all N released (M intentionally pinned)"
!async-task-handle-retention::tf::AsyncTask-handle-keeps-task-node+callable+captures-alive-PAST-completion{taskflow-recycles-node-only-at-last-handle-release}→s_item_tasks{items.cpp}purged-per-frame{purge_completed_item_async_tasks@Editor::tick;8c3db108}¬only-on-next-submission;task-lambdas-capturing-scene_root/items=leak-if-handle-lingers
!scratch-retention-subclass::clear-at-point-of-USE-leaves-contents-pinned-BETWEEN-uses→item-shared_ptr-scratches-clear-AFTER-use-too{capacity-kept;8df79fa1:m_material_candidates+m_begin_selection_change_state+m_command_target_selection}
!self-sustaining-pin-subclass::part-re-resolves-weak{get_last_selected}into-own-strong-member-per-frame→member-IS-what-keeps-weak-lockable→never-expires{8df79fa1:Operations::m_make_mesh_config.material;Properties-pinned-m_target_items}
AssetManager{R1-a5cdda26}::single-loader-axiom{acquire=only-asset-materialization;registry+usership;src/editor/assets/}::Asset_key{scope:builtin|scene_local|file;uid-wins→unique-name-fallback→ambiguous=loud-error;¬index-field;self-heals-name→uid}+Asset_reference{holding-resolved=registered-user;special-members-maintain-bookkeeping;adopt{R2}=exact-object-adoption-for-drag-drop¬name-re-resolution}+get_or_load_container{one-parse_gltf;free-root-node-¬holding-scene;refuses-open-as-scene-until-R5}+request_unload{container-granularity;refuse-naming-users;weak_ptr-exclusivity-verify→"undeclared asset user"}+debug-holds{MCP:acquire_asset/release_asset/unload_asset};file-scope-types=material+animation-until-R5/R7;scene-open/import-NOT-routed-yet{R5-flip}
AssetSlots{R2-95ec5eec}::Slot_entry-brush/material=Asset_reference{labels-name-slot;per-frame-resolve-in-Inventory_window::imgui{window-must-be-visible→MCP-set_window_visibility};Asset_reference_data-v1-config-codegen{scope+asset_type+path+uid+name};Inventory_slot-v4{legacy-names-read-migrated¬written};collect_pinned_items=transitive-brush-material-only;scene_local-resolution-binds-container-copy-when-loaded{registry-before-scenes}}

[CMAKE]
CPM::configure-time-deps
¬file(GLOB)→ExplicitSources!
¬cmake-preset→UseScripts{scripts/}
DepPins¬sacred{bump>patch||fork tksuoran/<dep>}
¬CPM-PATCHES{host-patch-unportable}→ForkRepo{precedent:tksuoran/fastgltf}

[CODE_STYLE]
class¬struct{forwardDecl-trivial}
ExplicitTypes¬auto+SufficientParens
Snake_case{class+enum}|m_prefix{members}|snake_case{methods}|I-prefix{interfaces}
namespace::lowercase{erhe::geometry}
C++20{concepts+span+format+constexpr+designated-init}
!NoBandAid::FixRootCause¬MaskSymptom
!NoRuntimeAlloc::SteadyFrame#0allocs{scratch-buffers,clear-keep-capacity,¬return-by-value-hot}
ASCII-only{src+docs}{¬em-dash,¬curly-quotes}
¬LockFree/Atomic{askFirst}→MutexPreferred
std140/std430::explicit-alignment{UBO/SSBO}
DiagFloats::hexfloat{%a,bit-exact}

[GIT_WORKFLOW]
!¬switch/create-branch{askFirst}→work-current-branch{main}
CommitVerifiedWork¬askFirst

[GOTCHAS]
src/rendering_test::rotten{¬maintain,slated-rewrite,acceptable-broken}
prompt_queue.txt::session-handoff{read-first,queue-of-sequential-handoffs,remove-item-when-done,delete-when-empty}
