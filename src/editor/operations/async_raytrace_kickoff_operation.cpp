#include "operations/async_raytrace_kickoff_operation.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "items.hpp"
#include "operations/mesh_operation.hpp"
#include "parsers/gltf.hpp"
#include "scene/scene_commit_queue.hpp"
#include "app_scenes.hpp"
#include "config/generated/viewport_config.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"

#include "erhe_graphics/scoped_worker_context.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/generated/mesh_memory_config.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <optional>

namespace editor {

Async_raytrace_kickoff_operation::Async_raytrace_kickoff_operation(
    std::shared_ptr<Scene_root>                   scene_root,
    std::vector<std::shared_ptr<erhe::Item_base>> mesh_node_items
)
    : m_scene_root     {std::move(scene_root)}
    , m_mesh_node_items{std::move(mesh_node_items)}
{
    set_description(
        fmt::format(
            "[{}] Async_raytrace_kickoff ({} items)",
            get_serial(),
            m_mesh_node_items.size()
        )
    );
}

Async_raytrace_kickoff_operation::~Async_raytrace_kickoff_operation() noexcept = default;

namespace {

// Whether the geometry-path optimizer passes of a deferred finalize go through
// the import mesh optimizer cache (get_import_mesh_optimize_cache_directory()).
// An import's finalize does: the same asset loads again and replays. The edit
// commits' re-optimize does not: every edited mesh is new content that would
// only grow the cache.
enum class Mesh_optimize_cache_use : unsigned int
{
    uncached = 0,
    import_cache
};

// Deferred per-mesh finalize, running on a tf::Executor worker
// (doc/editor/async_asset_loading.md): builds the Geometry (edges, smooth
// normals), the real triangle raytrace and - when the load path deferred it -
// the full geometry-based buffer mesh (edge lines, corner / centroid points)
// for one mesh, all without touching the live scene, then hands the swap to
// the main thread through App_context::scene_commit_queue (applied at the
// start of the next Editor::tick()). Every step is idempotent and no-ops fast
// when the eager load path (or another task sharing the primitive) already
// produced the result, so this also covers the deferred_raytrace /
// deferred_edge_lines = false configurations, where it degenerates to the
// pre-existing "ensure raytrace exists" behavior.
// Whether the finalize builds prepared primitives (doc/editor/async_asset_loading.md
// "Prepared mesh data") in full; skip leaves their load state as it is.
// base_only builds only the deferred base variants (Base_mesh_build::deferred,
// doc/plans/gltf_fast_load.md "Item 10"): the soup build of the base, the
// optimized variant kept.
enum class Prepared_full_build : unsigned int {
    skip      = 0,
    build     = 1,
    base_only = 2
};

// How a finalize task rebuilds the optimized variant of a primitive it was
// asked to regenerate the LOD chain of (request_lod_generation()). geometry:
// the re-optimization from the Geometry (prepare_geometry_buffer_mesh() with
// force_rebuild), committed with the base. prepared: the core soup repacked
// with the chain (Primitive::make_prepared_optimized_render_shape()), the
// base untouched.
enum class Lod_rebuild : unsigned int {
    none      = 0,
    geometry  = 1,
    prepared  = 2
};

// `lod_primitives`: the primitives of this mesh whose optimized variant the
// task rebuilds for a LOD request (empty for every other finalize).
void deferred_finalize_mesh_items(
    Mesh_operation_parameters&&                                     parameters,
    const std::shared_ptr<Scene_root>&                              scene_root,
    const Mesh_optimize_cache_use                                   cache_use,
    const Prepared_full_build                                       prepared_full_build,
    const std::vector<std::shared_ptr<erhe::primitive::Primitive>>& lod_primitives = {}
)
{
    ERHE_PROFILE_FUNCTION();

    App_context& context = parameters.context;
    ERHE_VERIFY(context.scene_commit_queue != nullptr);

    if (cache_use == Mesh_optimize_cache_use::import_cache) {
        parameters.build_info.buffer_info.mesh_optimize_cache_directory = get_import_mesh_optimize_cache_directory(context);
    }

    // Same Build_info variant selection as finalize_imported_meshes():
    // skinned meshes need joint attributes in the GPU vertex buffer.
    erhe::primitive::Build_info skinned_build_info{
        .primitive_types = parameters.build_info.primitive_types,
        .buffer_info     = context.mesh_memory->make_skinned_primitive_buffer_info(),
        .constant_color  = parameters.build_info.constant_color,
        .keep_geometry   = parameters.build_info.keep_geometry,
        .normal_style    = parameters.build_info.normal_style,
        .vertex_id_vec3  = parameters.build_info.vertex_id_vec3,
        .autocolor       = parameters.build_info.autocolor
    };
    skinned_build_info.buffer_info.mesh_optimize_cache_directory = parameters.build_info.buffer_info.mesh_optimize_cache_directory;

    for (const std::shared_ptr<erhe::Item_base>& item : parameters.items) {
        std::shared_ptr<erhe::scene::Mesh> scene_mesh = erhe::scene::get_mesh(item);
        ERHE_VERIFY(scene_mesh);

        // Snapshot the primitive list under the scene lock; the prepare
        // phase below runs without it. The re-optimize decision is made here
        // too: optimized_render_shape, the optimization hold count and the
        // LOD request are main-thread state with no lock of their own, so a
        // worker must not read them.
        std::vector<erhe::scene::Mesh_primitive>                    mesh_primitives;
        std::vector<bool>                                           force_optimized_rebuild;
        std::vector<std::optional<erhe::primitive::Lod_settings>>   lod_settings;
        std::vector<Lod_rebuild>                                    lod_rebuild;
        {
            std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root->item_host_mutex};
            mesh_primitives = scene_mesh->get_primitives();
            force_optimized_rebuild.assign(mesh_primitives.size(), false);
            lod_settings.assign(mesh_primitives.size(), std::optional<erhe::primitive::Lod_settings>{});
            lod_rebuild.assign(mesh_primitives.size(), Lod_rebuild::none);
            for (std::size_t i = 0, end = mesh_primitives.size(); i < end; ++i) {
                const erhe::primitive::Primitive* const primitive = mesh_primitives[i].primitive.get();
                if (primitive != nullptr) {
                    lod_settings[i] = primitive->lod_settings;
                }
                // A complete base mesh missing its (requested) optimized
                // variant: an edit commit's synchronous base-only rebuild, or
                // a live-edit invalidation whose edit has ended. Skip under an
                // active hold - publish would refuse the result anyway; the
                // hold's releaser queues the re-optimization.
                force_optimized_rebuild[i] =
                    parameters.build_info.buffer_info.optimize_meshes &&
                    (primitive != nullptr) &&
                    !primitive->optimized_render_shape &&
                    (primitive->optimization_hold_count == 0) &&
                    primitive->render_shape &&
                    primitive->render_shape->has_buffer_mesh_triangles();

                // A LOD request rebuilds the live variant, whether present or
                // not. Skipped under an active hold, like the re-optimization
                // above: the settings are on the primitive, and the hold's
                // releaser queues the rebuild that makes the chain. The
                // mesh's slot may hold another primitive than the request
                // saw (an edit swapped it): that one is not rebuilt here,
                // its own build carries the settings it took over.
                const bool lod_requested =
                    (primitive != nullptr) &&
                    (std::find(lod_primitives.begin(), lod_primitives.end(), mesh_primitives[i].primitive) != lod_primitives.end());
                if (
                    lod_requested &&
                    parameters.build_info.buffer_info.optimize_meshes &&
                    (primitive->optimization_hold_count == 0) &&
                    primitive->render_shape
                ) {
                    const erhe::primitive::Primitive_render_shape& render_shape = *primitive->render_shape.get();
                    if (primitive->is_prepared() && !render_shape.has_edge_lines()) {
                        // A prepared primitive without its full build: the
                        // variant is the core soup packed, no Geometry.
                        lod_rebuild[i]             = Lod_rebuild::prepared;
                        force_optimized_rebuild[i] = false;
                    } else if (render_shape.has_buffer_mesh_triangles()) {
                        lod_rebuild[i]             = Lod_rebuild::geometry;
                        force_optimized_rebuild[i] = true;
                    }
                }
            }
        }
        // Phase A output of the prepared LOD rebuilds, published by the
        // commit below.
        std::vector<std::shared_ptr<erhe::primitive::Primitive_render_shape>> lod_shapes(mesh_primitives.size());

        const erhe::primitive::Build_info& mesh_build_info = scene_mesh->skin ? skinned_build_info : parameters.build_info;

        // Phase A - prepare, no scene lock: Geometry conversion, BVH build
        // and GPU buffer-mesh build are the expensive parts and touch only
        // primitive-local state (serialized per shape on the shape's own
        // mutex, so primitives shared between meshes are built exactly once).
        for (std::size_t primitive_index = 0, primitive_end = mesh_primitives.size(); primitive_index < primitive_end; ++primitive_index) {
            const erhe::scene::Mesh_primitive& mesh_primitive = mesh_primitives[primitive_index];
            erhe::primitive::Primitive& primitive = *mesh_primitive.primitive.get();
            const std::shared_ptr<erhe::primitive::Primitive_shape> raytrace_shape = primitive.get_shape_for_raytrace();
            if (raytrace_shape && !raytrace_shape->has_real_raytrace()) {
                if (!raytrace_shape->prepare_real_raytrace()) {
                    log_operations->warn("Deferred finalize: could not build raytrace for mesh '{}'", scene_mesh->get_name());
                }
            }
            const std::shared_ptr<erhe::primitive::Primitive_render_shape>& render_shape = primitive.render_shape;
            // A prepared primitive's load state is final without a Geometry
            // (doc/editor/async_asset_loading.md "Prepared mesh data"): no
            // full build for it.
            // A deferred base variant (Base_mesh_build::deferred) is built
            // by this full build.
            const bool needs_full_build =
                (!primitive.is_prepared() || (prepared_full_build == Prepared_full_build::build)) &&
                render_shape && (render_shape->has_buffer_mesh_triangles() || render_shape->is_base_deferred()) && !render_shape->has_edge_lines();
            // A complete mesh whose optimized variant is missing (snapshot
            // decision above) is rebuilt in full with force_rebuild: the
            // fresh optimized variant comes out of the same staged bytes,
            // and the identical base swaps in benignly.
            const bool needs_reoptimize =
                !needs_full_build && force_optimized_rebuild[primitive_index] && render_shape && render_shape->has_buffer_mesh_triangles();
            if (lod_rebuild[primitive_index] == Lod_rebuild::prepared) {
                erhe::graphics::Scoped_worker_context worker_context{*context.graphics_device};
                const erhe::primitive::Lod_settings* const settings = lod_settings[primitive_index].has_value()
                    ? &lod_settings[primitive_index].value()
                    : nullptr;
                lod_shapes[primitive_index] = primitive.make_prepared_optimized_render_shape(mesh_build_info.buffer_info, settings);
                if (!lod_shapes[primitive_index]) {
                    log_operations->warn("LOD generation: could not build the optimized variant of '{}' (soup not readable or out of GPU mesh memory?)", scene_mesh->get_name());
                }
            } else if ((prepared_full_build == Prepared_full_build::base_only) && render_shape && render_shape->is_base_deferred()) {
                erhe::graphics::Scoped_worker_context worker_context{*context.graphics_device};
                if (!render_shape->prepare_deferred_base_mesh(mesh_build_info.buffer_info)) {
                    log_operations->warn("Deferred base variant: could not build the base mesh for '{}' (out of GPU mesh memory?)", scene_mesh->get_name());
                }
            } else if (needs_full_build || needs_reoptimize) {
                // On GL a worker needs a share context for the GPU buffer
                // allocation inside prepare_geometry_buffer_mesh. Scope is
                // deliberately NARROW - around the GPU build only, not the
                // BVH build above - so the fixed context pool does not cap
                // the CPU-heavy part of the finalize (plan section 9 item
                // 1). Unreachable without worker contexts: the import path
                // only defers edge lines when supports_worker_contexts(),
                // and the edit commits dispatch the re-optimize only when
                // worker contexts exist.
                erhe::graphics::Scoped_worker_context worker_context{*context.graphics_device};
                // The import finalize always builds corner normals; a
                // re-optimize rebuild keeps the shape's committed normal
                // style so the swapped-in base shades identically.
                const erhe::primitive::Normal_style normal_style = needs_reoptimize
                    ? render_shape->get_normal_style()
                    : erhe::primitive::Normal_style::corner_normals;
                // A primitive with a LOD request rebuilds its optimized variant
                // with its chain (Primitive::lod_settings).
                std::optional<erhe::primitive::Build_info> lod_build_info{};
                if (lod_settings[primitive_index].has_value()) {
                    lod_build_info.emplace(mesh_build_info);
                    lod_build_info->lod_settings = lod_settings[primitive_index];
                }
                const erhe::primitive::Build_info& primitive_build_info = lod_build_info.has_value() ? lod_build_info.value() : mesh_build_info;
                if (!render_shape->prepare_geometry_buffer_mesh(primitive_build_info, normal_style, scene_mesh->get_name(), needs_reoptimize)) {
                    log_operations->warn(
                        "Deferred finalize: could not build full buffer mesh for '{}' (out of GPU mesh memory?)",
                        scene_mesh->get_name()
                    );
                }
            }
        }

        // Phase B - commit, queued for the main thread: swap in the prepared
        // results, then detach the raytrace instances of every mesh the swap
        // reaches, rebuild their raytrace primitives and re-attach with fresh
        // transforms. Runs from Editor::tick() ->
        // Scene_commit_queue::flush(), before the frame's hover raytrace,
        // physics, operations and rendering. The mesh's item host is
        // re-read at commit time: the mesh may have been detached (scene
        // closed, import undone) or re-hosted since the task was created;
        // the shape-level swaps are still applied so a later re-attach sees
        // the finished results.
        //
        // The swaps are SHAPE-level and shapes are shared: glTF instances
        // (the importer clones the template mesh per referencing node) and
        // brush instances hold the same Primitive. When a swap happens, every
        // mesh in the scene that shares one of the committed primitives is
        // rebuilt here, not just this task's mesh - their Raytrace_primitives
        // reference the proxy raytrace being replaced, and their draw list
        // records cache the proxy buffer mesh's vertex / index ranges, which
        // the swap frees. Refreshing only this mesh left the sharers drawing
        // from freed (reused) mesh memory until their own task committed,
        // seconds later on a large scene.
        //
        // The sharers come from the scene root's shape-to-meshes index, which
        // the change sites maintain: a load queues one commit per mesh, and
        // reading the index keeps each commit proportional to the sharers of
        // the committed shapes instead of to the whole scene.
        //
        // And they are collected only when this commit actually swapped a
        // shape, which is why the swaps run first and the collect reads what
        // they returned. A shape is committed ONCE however many meshes share
        // it, so for an asset instanced N times exactly one of the N commits
        // swaps anything and the other N-1 leave every sharer reading the
        // shapes it already read: collecting unconditionally made every one
        // of the N commits refresh all N sharers, which is N draw list
        // re-registrations per commit and N * N over the load (measured on a
        // 2000-instance stage: 4.1 million queued draw list operations and
        // 99 s in one flush).
        context.scene_commit_queue->enqueue(
            [scene_mesh, mesh_primitives = std::move(mesh_primitives), lod_shapes = std::move(lod_shapes), lod_rebuild = std::move(lod_rebuild)]()
            {
                ERHE_PROFILE_SCOPE("deferred finalize commit");
                Scene_root* const scene_root = static_cast<Scene_root*>(scene_mesh->get_item_host());
                std::unique_lock<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock;
                // Main-thread scratch (Scene_commit_queue::flush): the commit
                // runs once per finalized mesh, so the buffer reaches its
                // high-water mark and stops allocating. Nothing this commit
                // calls collects sharers again, so the buffer is never live
                // across a nested use; it is cleared after use as well, so it
                // holds no mesh between commits.
                static thread_local std::vector<std::shared_ptr<erhe::scene::Mesh>> affected_meshes;
                affected_meshes.clear();
                if (scene_root != nullptr) {
                    scene_lock = std::unique_lock<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)>{scene_root->item_host_mutex};
                }
                // The swaps run before the raytrace instances are detached,
                // which a replaced proxy raytrace survives by construction:
                // commit_real_raytrace() retires the proxy instead of freeing
                // it, precisely so instances that have not refreshed yet stay
                // valid - the sharers a later task refreshes rely on the same
                // thing, for frames rather than for the rest of this locked
                // region.
                bool swapped_a_shape = false;
                for (std::size_t primitive_index = 0, primitive_end = mesh_primitives.size(); primitive_index < primitive_end; ++primitive_index) {
                    const erhe::scene::Mesh_primitive& mesh_primitive = mesh_primitives[primitive_index];
                    erhe::primitive::Primitive& primitive = *mesh_primitive.primitive.get();
                    const std::shared_ptr<erhe::primitive::Primitive_shape> raytrace_shape = primitive.get_shape_for_raytrace();
                    if (raytrace_shape && raytrace_shape->commit_real_raytrace()) {
                        swapped_a_shape = true;
                    }
                    if (primitive.render_shape && primitive.render_shape->commit_deferred_base_mesh()) {
                        // A base-only build: the optimized variant stays.
                        swapped_a_shape = true;
                    }
                    if (primitive.render_shape) {
                        // The optimized variant rides the same swap and is
                        // attached here, before update_rt_primitives() below
                        // re-registers the mesh with the draw list -
                        // registration bakes the drawn variant's base_vertex
                        // and index ranges. Attaching after it would leave the
                        // draw list drawing one build from another's record.
                        //
                        // Assigning it also drops any variant built from the
                        // pre-finalize triangle soup. That one has to go: it
                        // was a reordering of the SOUP build, and the build it
                        // now sits beside is the geometry one, with smooth
                        // normals and tangents the soup build never had.
                        std::shared_ptr<erhe::primitive::Primitive_render_shape> optimized;
                        if (primitive.render_shape->commit_geometry_buffer_mesh(optimized)) {
                            swapped_a_shape = true;
                            // publish refuses under a live-edit optimization
                            // hold, so a variant built from pre-edit data can
                            // never appear beside an in-progress edit.
                            primitive.publish_optimized_render_shape(std::move(optimized));
                        }
                    }
                    if (lod_shapes[primitive_index]) {
                        // A prepared LOD rebuild: the new variant replaces the
                        // live one (retired, not freed) before the sharers
                        // re-register below; the hold rule applies as above.
                        primitive.publish_optimized_render_shape(lod_shapes[primitive_index]);
                        swapped_a_shape = true;
                    }
                    // A LOD request that ends without levels is reported; the
                    // erhe.primitive warning before this says why.
                    if (lod_rebuild[primitive_index] != Lod_rebuild::none) {
                        const std::shared_ptr<erhe::primitive::Primitive_render_shape>& variant = primitive.optimized_render_shape;
                        if (!variant) {
                            log_operations->warn("LOD generation: '{}' primitive {} has no optimized variant after the rebuild", scene_mesh->get_name(), primitive_index);
                        } else if (variant->get_renderable_mesh().lod_chain.is_empty()) {
                            log_operations->warn("LOD generation: '{}' primitive {} got no LOD levels", scene_mesh->get_name(), primitive_index);
                        }
                    }
                }
                if (scene_root == nullptr) {
                    scene_mesh->update_rt_primitives();
                    return; // affected_meshes is empty on this path
                }
                if (swapped_a_shape) {
                    scene_root->collect_meshes_sharing_primitives(scene_mesh, mesh_primitives, affected_meshes);
                } else {
                    // Nothing changed at shape level, so this mesh alone is
                    // refreshed - the task's own statement that its mesh's
                    // raytrace primitives are built from the shapes as they
                    // now stand.
                    affected_meshes.push_back(scene_mesh);
                }
                for (const std::shared_ptr<erhe::scene::Mesh>& affected_mesh : affected_meshes) {
                    scene_root->begin_mesh_rt_update(affected_mesh);
                }
                for (const std::shared_ptr<erhe::scene::Mesh>& affected_mesh : affected_meshes) {
                    // Rebuilds the raytrace primitives from the committed
                    // shapes and re-registers the mesh's draw list records
                    // (Mesh::update_rt_primitives -> notify_primitives_changed).
                    // Fresh raytrace instances start with identity
                    // transforms; update_rt_primitives seeds them with the
                    // mesh's world transform (and commits) right away.
                    affected_mesh->update_rt_primitives();
                    scene_root->end_mesh_rt_update(affected_mesh);
                }
                // Released here, not only at the next commit's clear(): the
                // scratch holds mesh references and must not pin them between
                // commits. Capacity stays.
                affected_meshes.clear();
            }
        );
    }
}

// Every primitive of the mesh carries prepared mesh data, has its real
// raytrace (the stored BVH was used) and, when meshes are optimized, its
// optimized variant.
[[nodiscard]] auto is_load_complete(const App_context& context, const erhe::scene::Mesh& mesh) -> bool
{
    const bool optimize_meshes = (context.mesh_memory_config != nullptr) && context.mesh_memory_config->optimize_meshes;
    const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh.get_primitives();
    if (mesh_primitives.empty()) {
        return false;
    }
    for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh_primitives) {
        const erhe::primitive::Primitive* const primitive = mesh_primitive.primitive.get();
        if ((primitive == nullptr) || !primitive->is_prepared() || !primitive->has_real_raytrace()) {
            return false;
        }
        if (optimize_meshes && !primitive->optimized_render_shape) {
            return false;
        }
    }
    return true;
}

} // anonymous namespace

void Async_raytrace_kickoff_operation::execute(App_context& context)
{
    std::shared_ptr<Scene_root> scene_root = m_scene_root;

    // One async task per mesh node (instead of one task for the whole
    // import): the expensive per-mesh work spreads across executor workers
    // and the scene lock is held only for each mesh's short commit phase.
    // async_for_nodes_with_mesh chains each task after any pending task for
    // the same item.
    // A viewport drawing edge lines or points needs the full build of
    // prepared primitives too; nothing is skipped then.
    const Prepared_full_build prepared_full_build = viewports_need_full_builds(context)
        ? Prepared_full_build::build
        : Prepared_full_build::skip;
    std::size_t queued_count  = 0;
    std::size_t skipped_count = 0;
    for (const std::shared_ptr<erhe::Item_base>& item : m_mesh_node_items) {
        // A mesh whose primitives all loaded with prepared mesh data and a
        // real raytrace has nothing left to finalize: no task is queued for
        // it. A mesh mixing prepared and other primitives keeps its task;
        // the task skips the prepared ones.
        const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(item);
        if ((prepared_full_build == Prepared_full_build::skip) && mesh && is_load_complete(context, *mesh)) {
            ++skipped_count;
            continue;
        }
        ++queued_count;
        const std::vector<std::shared_ptr<erhe::Item_base>> single_item{item};
        async_for_nodes_with_mesh(
            context,
            single_item,
            [scene_root, prepared_full_build](Mesh_operation_parameters&& mesh_operation_parameters)
            {
                deferred_finalize_mesh_items(std::move(mesh_operation_parameters), scene_root, Mesh_optimize_cache_use::import_cache, prepared_full_build);
            },
            // The finalize manages its own NARROW worker-context scope
            // around the GPU build only (the BVH build is CPU-heavy and
            // must not hold a pool context) - and is dispatched per mesh,
            // so it must not fall into the dispatcher's inline mode.
            /*op_builds_gpu_meshes=*/false
        );
    }
    log_operations->info(
        "Async_raytrace_kickoff: {} deferred finalize tasks queued, {} meshes skipped (prepared mesh data, nothing to finalize)",
        queued_count, skipped_count
    );
}

void Async_raytrace_kickoff_operation::undo(App_context&)
{
    // In-flight async tasks captured scene_root and the item shared_ptrs at
    // task creation; they complete safely against the captured scene_root
    // even if items have been detached from it by sibling sub-op undos.
}

void kickoff_deferred_finalize(App_context& context, const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    if (!mesh) {
        return;
    }
    erhe::scene::Node* const node = mesh.get();
    if (node == nullptr) {
        return;
    }
    erhe::Item_host* const item_host = node->get_item_host();
    if (item_host == nullptr) {
        return;
    }
    const std::shared_ptr<Scene_root> scene_root = static_cast<Scene_root*>(item_host)->shared_from_this();
    const std::vector<std::shared_ptr<erhe::Item_base>> single_item{node->shared_from_this()};
    async_for_nodes_with_mesh(
        context,
        single_item,
        [scene_root](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            deferred_finalize_mesh_items(std::move(mesh_operation_parameters), scene_root, Mesh_optimize_cache_use::uncached, Prepared_full_build::skip);
        },
        // The finalize manages its own narrow worker-context scope.
        /*op_builds_gpu_meshes=*/false
    );
}

namespace {

// A prepared primitive of the mesh has no full build yet.
[[nodiscard]] auto needs_prepared_full_build(const erhe::scene::Mesh& mesh) -> bool
{
    for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh.get_primitives()) {
        const erhe::primitive::Primitive* const primitive = mesh_primitive.primitive.get();
        if ((primitive != nullptr) && primitive->is_prepared() && !primitive->render_shape->has_edge_lines()) {
            return true;
        }
    }
    return false;
}

void queue_prepared_full_build(
    App_context&                              context,
    const std::shared_ptr<erhe::scene::Mesh>& mesh,
    std::size_t&                              queued_count,
    const Prepared_full_build                 mode = Prepared_full_build::build
)
{
    erhe::Item_host* const item_host = mesh->get_item_host();
    Scene_root* const      host      = dynamic_cast<Scene_root*>(item_host);
    if (host == nullptr) {
        return;
    }
    const std::shared_ptr<Scene_root>                   scene_root = host->shared_from_this();
    const std::vector<std::shared_ptr<erhe::Item_base>> single_item{mesh->shared_from_this()};
    async_for_nodes_with_mesh(
        context,
        single_item,
        [scene_root, mode](Mesh_operation_parameters&& mesh_operation_parameters)
        {
            deferred_finalize_mesh_items(std::move(mesh_operation_parameters), scene_root, Mesh_optimize_cache_use::import_cache, mode);
        },
        // The finalize manages its own narrow worker-context scope.
        /*op_builds_gpu_meshes=*/false
    );
    ++queued_count;
}

} // anonymous namespace

void request_prepared_full_builds(App_context& context, const std::vector<std::shared_ptr<erhe::Item_base>>& items)
{
    std::size_t queued_count = 0;
    for (const std::shared_ptr<erhe::Item_base>& item : items) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(item);
        if (mesh && needs_prepared_full_build(*mesh)) {
            queue_prepared_full_build(context, mesh, queued_count);
        }
    }
    if (queued_count > 0) {
        log_operations->info("prepared mesh data: {} full build tasks queued for the selection", queued_count);
    }
}

void request_prepared_full_builds_in_all_scenes(App_context& context)
{
    if (context.app_scenes == nullptr) {
        return;
    }
    std::size_t queued_count = 0;
    for (const std::shared_ptr<Scene_root>& scene_root : context.app_scenes->get_scene_roots()) {
        if (!scene_root) {
            continue;
        }
        std::vector<std::shared_ptr<erhe::scene::Mesh>> meshes;
        {
            std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root->item_host_mutex};
            for (const std::shared_ptr<erhe::scene::Mesh_layer>& mesh_layer : scene_root->get_scene().get_mesh_layers()) {
                for (const std::shared_ptr<erhe::scene::Mesh>& mesh : mesh_layer->meshes) {
                    if (mesh && needs_prepared_full_build(*mesh)) {
                        meshes.push_back(mesh);
                    }
                }
            }
        }
        for (const std::shared_ptr<erhe::scene::Mesh>& mesh : meshes) {
            queue_prepared_full_build(context, mesh, queued_count);
        }
    }
    log_operations->info("prepared mesh data: {} full build tasks queued (a viewport style needs full builds)", queued_count);
}

auto has_deferred_base(const erhe::scene::Mesh& mesh) -> bool
{
    for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh.get_primitives()) {
        const erhe::primitive::Primitive* const primitive = mesh_primitive.primitive.get();
        if ((primitive != nullptr) && primitive->render_shape && primitive->render_shape->is_base_deferred()) {
            return true;
        }
    }
    return false;
}

namespace {

// Claims the build request of every deferred base of the mesh; true when one
// was not claimed before (the mesh's full build is to be queued).
[[nodiscard]] auto claim_deferred_base_builds(const erhe::scene::Mesh& mesh) -> bool
{
    bool claimed = false;
    for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh.get_primitives()) {
        const erhe::primitive::Primitive* const primitive = mesh_primitive.primitive.get();
        if ((primitive != nullptr) && primitive->render_shape && primitive->render_shape->claim_base_build_request()) {
            claimed = true;
        }
    }
    return claimed;
}

} // anonymous namespace

void request_deferred_base_build(App_context& context, const std::shared_ptr<erhe::scene::Mesh>& mesh, const Deferred_base_build build)
{
    if (!mesh || !claim_deferred_base_builds(*mesh)) {
        return;
    }
    std::size_t queued_count = 0;
    queue_prepared_full_build(context, mesh, queued_count, (build == Deferred_base_build::base_only) ? Prepared_full_build::base_only : Prepared_full_build::build);
    if (queued_count > 0) {
        log_operations->info(
            "deferred base variant: {} queued for '{}'",
            (build == Deferred_base_build::base_only) ? "base build" : "full build",
            mesh->get_name()
        );
    }
}

void request_deferred_base_builds_in_all_scenes(App_context& context)
{
    if (context.app_scenes == nullptr) {
        return;
    }
    std::size_t queued_count = 0;
    for (const std::shared_ptr<Scene_root>& scene_root : context.app_scenes->get_scene_roots()) {
        if (!scene_root) {
            continue;
        }
        std::vector<std::shared_ptr<erhe::scene::Mesh>> meshes;
        {
            std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root->item_host_mutex};
            for (const std::shared_ptr<erhe::scene::Mesh_layer>& mesh_layer : scene_root->get_scene().get_mesh_layers()) {
                for (const std::shared_ptr<erhe::scene::Mesh>& mesh : mesh_layer->meshes) {
                    if (mesh && claim_deferred_base_builds(*mesh)) {
                        meshes.push_back(mesh);
                    }
                }
            }
        }
        for (const std::shared_ptr<erhe::scene::Mesh>& mesh : meshes) {
            queue_prepared_full_build(context, mesh, queued_count, Prepared_full_build::base_only);
        }
    }
    if (queued_count > 0) {
        log_operations->info("deferred base variant: {} base build tasks queued (a feature reads every base variant)", queued_count);
    }
}

auto request_lod_generation(
    App_context&                                         context,
    const std::vector<std::shared_ptr<erhe::Item_base>>& items,
    const std::optional<erhe::primitive::Lod_settings>&  settings
) -> Lod_generation_result
{
    Lod_generation_result result{};

    // The meshes among the items, and their host (callers pass one scene).
    std::vector<std::shared_ptr<erhe::scene::Mesh>> meshes;
    Scene_root*                                     host = nullptr;
    for (const std::shared_ptr<erhe::Item_base>& item : items) {
        std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(item);
        if (!mesh) {
            continue;
        }
        Scene_root* const mesh_host = dynamic_cast<Scene_root*>(mesh->get_item_host());
        if (mesh_host == nullptr) {
            continue;
        }
        ERHE_VERIFY((host == nullptr) || (host == mesh_host));
        host = mesh_host;
        meshes.push_back(std::move(mesh));
    }
    result.mesh_count = meshes.size();
    if (host == nullptr) {
        return result;
    }
    const std::shared_ptr<Scene_root> scene_root = host->shared_from_this();
    const bool optimize_meshes = (context.mesh_memory_config != nullptr) && context.mesh_memory_config->optimize_meshes;

    // Per mesh, the distinct primitives its task rebuilds: a primitive shared
    // by several meshes is claimed by the first, and that task's commit
    // refreshes the others (collect_meshes_sharing_primitives()). The LOD
    // request is main-thread state written under the item host lock, which
    // is how the finalize snapshot reads it.
    std::vector<std::vector<std::shared_ptr<erhe::primitive::Primitive>>> claimed(meshes.size());
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root->item_host_mutex};
        std::vector<const erhe::primitive::Primitive*> seen;
        for (std::size_t mesh_index = 0, mesh_end = meshes.size(); mesh_index < mesh_end; ++mesh_index) {
            for (const erhe::scene::Mesh_primitive& mesh_primitive : meshes[mesh_index]->get_primitives()) {
                erhe::primitive::Primitive* const primitive = mesh_primitive.primitive.get();
                if ((primitive == nullptr) || (std::find(seen.begin(), seen.end(), primitive) != seen.end())) {
                    continue;
                }
                seen.push_back(primitive);
                ++result.primitive_count;
                if ((primitive->lod_settings == settings) && primitive->optimized_render_shape) {
                    ++result.unchanged_count;
                    continue;
                }
                primitive->lod_settings = settings;
                if (!optimize_meshes) {
                    ++result.not_optimized_count;
                    continue;
                }
                if (primitive->optimization_hold_count > 0) {
                    ++result.held_count;
                    continue;
                }
                claimed[mesh_index].push_back(mesh_primitive.primitive);
                ++result.queued_count;
            }
        }
    }

    // The dispatcher takes the item host lock itself.
    for (std::size_t mesh_index = 0, mesh_end = meshes.size(); mesh_index < mesh_end; ++mesh_index) {
        if (claimed[mesh_index].empty()) {
            continue;
        }
        const std::vector<std::shared_ptr<erhe::Item_base>> single_item{meshes[mesh_index]->shared_from_this()};
        async_for_nodes_with_mesh(
            context,
            single_item,
            [scene_root, lod_primitives = std::move(claimed[mesh_index])](Mesh_operation_parameters&& mesh_operation_parameters)
            {
                // Uncached like the edit re-optimization: the cache stores
                // the optimizer remaps, not the levels, and a procedural or
                // edited mesh would only grow it.
                deferred_finalize_mesh_items(std::move(mesh_operation_parameters), scene_root, Mesh_optimize_cache_use::uncached, Prepared_full_build::skip, lod_primitives);
            },
            // The finalize manages its own narrow worker-context scope.
            /*op_builds_gpu_meshes=*/false
        );
        ++result.task_count;
    }
    log_operations->info(
        "LOD generation ({}): {} primitives of {} meshes, {} queued in {} tasks, {} unchanged, {} under an edit hold, {} without mesh optimization",
        settings.has_value() ? "generate" : "clear",
        result.primitive_count, result.mesh_count, result.queued_count, result.task_count,
        result.unchanged_count, result.held_count, result.not_optimized_count
    );
    return result;
}

auto viewports_need_full_builds(App_context& context) -> bool
{
    if (context.scene_views == nullptr) {
        return false;
    }
    const auto needs = [](const Render_style_data& style) -> bool {
        return style.edge_lines || style.corner_points || style.polygon_centroids || style.solid_wireframe;
    };
    for (const std::shared_ptr<Viewport_scene_view>& scene_view : context.scene_views->get_viewport_scene_views()) {
        if (!scene_view) {
            continue;
        }
        const Viewport_config& config = scene_view->get_config();
        if (needs(config.render_style_not_selected) || needs(config.render_style_selected)) {
            return true;
        }
    }
    return false;
}

}
