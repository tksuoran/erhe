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
ScenePersistenceVerify::scripts/scene_roundtrip_verify.py{fresh-headless-session;all-11-ERHE_*-build→schema-validate+reload-MCP-diff+prefab-roundtrip+Khronos-validator{0-errors}+Blender-render;run-book@doc/scene_serialization.md}
ItemHostMutex::async-workers-lock-scene-item_host_mutex-for-hosted-state-mutation;main-thread-consumers-lock-too{Scene::update_node_transforms-locks-internally-since-f5a58c5b}
!geogram-threading::PDEL/parallel-algos-require-no-other-geogram-threads{unenforceable-in-erhe→use-sequential-BDEL-for-convex-hull;asserts→ASSERT_THROW-sans-debugger;upstream-ask:doc/geogram.md}
!geometry-payload-invariant::graph-payload-geometries-carry-connectivity+edges{process_for_graph}→every-producer-incl-merges-must-process{violation=fastfail-on-worker,uncatchable}
GraphPins::input-default-single-link{editors-replace-on-connect,one-Compound-undo}|Pin::multi_link=multi-input-socket{accumulate;join+instance-points+realize-instances}
GraphSourceNodes::external-item-refs{brush/scene_mesh}capture-geometry-MAIN-thread{lazy-getters-worker-unsafe}+serialize-by-name{owner-scene→all-scenes-resolution;shadow-clones-ownerless}
!scene-close-bug-class::parts-caching-scene-hosted-refs-must-handle-close{weak_ptr-insufficient:own-resolve-cache-pins-item}→subscribe-close_scene||validate-App_scenes::is_host_registered(get_item_host())-on-access{precedent:Geometry_graph_window::resolve_target};watchdog::on_close_scene-arms-60-frame-check→"scene-close leak:"-log-warnings{treat-as-bug;CLAUDE.md-"Scene-hosted references"}

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
