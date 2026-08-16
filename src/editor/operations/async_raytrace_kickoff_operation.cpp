#include "operations/async_raytrace_kickoff_operation.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "items.hpp"
#include "operations/mesh_operation.hpp"
#include "scene/scene_commit_queue.hpp"
#include "scene/scene_root.hpp"

#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

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

// Every mesh hosted by scene_root whose primitive list contains one of
// mesh_primitives' Primitive objects - scene_mesh itself first (when it is
// hosted there), then the sharers. Caller holds scene_root->item_host_mutex.
auto collect_meshes_sharing_primitives(
    Scene_root&                                          scene_root,
    const std::shared_ptr<erhe::scene::Mesh>&            scene_mesh,
    const std::vector<erhe::scene::Mesh_primitive>&      mesh_primitives
) -> std::vector<std::shared_ptr<erhe::scene::Mesh>>
{
    std::vector<std::shared_ptr<erhe::scene::Mesh>> result;
    result.push_back(scene_mesh);
    const auto shares_primitive = [&mesh_primitives](const erhe::scene::Mesh& mesh) -> bool
    {
        for (const erhe::scene::Mesh_primitive& candidate : mesh.get_primitives()) {
            for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh_primitives) {
                if (candidate.primitive.get() == mesh_primitive.primitive.get()) {
                    return true;
                }
            }
        }
        return false;
    };
    for (const std::shared_ptr<erhe::scene::Mesh_layer>& mesh_layer : scene_root.get_scene().get_mesh_layers()) {
        for (const std::shared_ptr<erhe::scene::Mesh>& mesh : mesh_layer->meshes) {
            if ((mesh == scene_mesh) || !mesh || !shares_primitive(*mesh)) {
                continue;
            }
            result.push_back(mesh);
        }
    }
    return result;
}

// Deferred per-mesh finalize, running on a tf::Executor worker
// (doc/gltf-load-speedup-plan.md): builds the Geometry (edges, smooth
// normals), the real triangle raytrace and - when the load path deferred it -
// the full geometry-based buffer mesh (edge lines, corner / centroid points)
// for one mesh, all without touching the live scene, then hands the swap to
// the main thread through App_context::scene_commit_queue (applied at the
// start of the next Editor::tick()). Every step is idempotent and no-ops fast
// when the eager load path (or another task sharing the primitive) already
// produced the result, so this also covers the deferred_raytrace /
// deferred_edge_lines = false configurations, where it degenerates to the
// pre-existing "ensure raytrace exists" behavior.
void deferred_finalize_mesh_items(Mesh_operation_parameters&& parameters, const std::shared_ptr<Scene_root>& scene_root)
{
    ERHE_PROFILE_FUNCTION();

    App_context& context = parameters.context;
    ERHE_VERIFY(context.scene_commit_queue != nullptr);

    // Same Build_info variant selection as finalize_imported_meshes():
    // skinned meshes need joint attributes in the GPU vertex buffer.
    const erhe::primitive::Build_info skinned_build_info{
        .primitive_types = parameters.build_info.primitive_types,
        .buffer_info     = context.mesh_memory->make_skinned_primitive_buffer_info(),
        .constant_color  = parameters.build_info.constant_color,
        .keep_geometry   = parameters.build_info.keep_geometry,
        .normal_style    = parameters.build_info.normal_style,
        .vertex_id_vec3  = parameters.build_info.vertex_id_vec3,
        .autocolor       = parameters.build_info.autocolor
    };

    for (const std::shared_ptr<erhe::Item_base>& item : parameters.items) {
        std::shared_ptr<erhe::scene::Mesh> scene_mesh = erhe::scene::get_mesh(item);
        ERHE_VERIFY(scene_mesh);

        // Snapshot the primitive list under the scene lock; the prepare
        // phase below runs without it.
        std::vector<erhe::scene::Mesh_primitive> mesh_primitives;
        {
            std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root->item_host_mutex};
            mesh_primitives = scene_mesh->get_primitives();
        }

        const erhe::primitive::Build_info& mesh_build_info = scene_mesh->skin ? skinned_build_info : parameters.build_info;

        // Phase A - prepare, no scene lock: Geometry conversion, BVH build
        // and GPU buffer-mesh build are the expensive parts and touch only
        // primitive-local state (serialized per shape on the shape's own
        // mutex, so primitives shared between meshes are built exactly once).
        for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh_primitives) {
            erhe::primitive::Primitive& primitive = *mesh_primitive.primitive.get();
            const std::shared_ptr<erhe::primitive::Primitive_shape> raytrace_shape = primitive.get_shape_for_raytrace();
            if (raytrace_shape && !raytrace_shape->has_real_raytrace()) {
                if (!raytrace_shape->prepare_real_raytrace()) {
                    log_operations->warn("Deferred finalize: could not build raytrace for mesh '{}'", scene_mesh->get_name());
                }
            }
            const std::shared_ptr<erhe::primitive::Primitive_render_shape>& render_shape = primitive.render_shape;
            if (render_shape && render_shape->has_buffer_mesh_triangles() && !render_shape->has_edge_lines()) {
                if (!render_shape->prepare_geometry_buffer_mesh(mesh_build_info, erhe::primitive::Normal_style::corner_normals)) {
                    log_operations->warn(
                        "Deferred finalize: could not build full buffer mesh for '{}' (out of GPU mesh memory?)",
                        scene_mesh->get_name()
                    );
                }
            }
        }

        // Phase B - commit, queued for the main thread: detach the raytrace
        // instances (they may reference a proxy raytrace being replaced),
        // swap in the prepared results, rebuild the raytrace primitives and
        // re-attach with fresh transforms. Runs from Editor::tick() ->
        // Scene_commit_queue::flush(), before the frame's hover raytrace,
        // physics, operations and rendering. The mesh's item host is
        // re-read at commit time: the mesh may have been detached (scene
        // closed, import undone) or re-hosted since the task was created;
        // the shape-level swaps are still applied so a later re-attach sees
        // the finished results.
        //
        // The swaps are SHAPE-level and shapes are shared: glTF instances
        // (the importer clones the template mesh per referencing node) and
        // brush instances hold the same Primitive. Every mesh in the scene
        // that shares one of the committed primitives is rebuilt here, not
        // just this task's mesh - their Raytrace_primitives reference the
        // proxy raytrace being replaced, and their draw list records cache
        // the proxy buffer mesh's vertex / index ranges, which the swap
        // frees. Refreshing only this mesh left the sharers drawing from
        // freed (reused) mesh memory until their own task committed,
        // seconds later on a large scene.
        context.scene_commit_queue->enqueue(
            [scene_mesh, mesh_primitives = std::move(mesh_primitives)]()
            {
                ERHE_PROFILE_SCOPE("deferred finalize commit");
                Scene_root* const scene_root = static_cast<Scene_root*>(scene_mesh->get_item_host());
                std::unique_lock<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock;
                std::vector<std::shared_ptr<erhe::scene::Mesh>> affected_meshes;
                if (scene_root != nullptr) {
                    scene_lock = std::unique_lock<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)>{scene_root->item_host_mutex};
                    affected_meshes = collect_meshes_sharing_primitives(*scene_root, scene_mesh, mesh_primitives);
                    for (const std::shared_ptr<erhe::scene::Mesh>& affected_mesh : affected_meshes) {
                        scene_root->begin_mesh_rt_update(affected_mesh);
                    }
                }
                for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh_primitives) {
                    erhe::primitive::Primitive& primitive = *mesh_primitive.primitive.get();
                    const std::shared_ptr<erhe::primitive::Primitive_shape> raytrace_shape = primitive.get_shape_for_raytrace();
                    if (raytrace_shape) {
                        static_cast<void>(raytrace_shape->commit_real_raytrace());
                    }
                    if (primitive.render_shape) {
                        static_cast<void>(primitive.render_shape->commit_geometry_buffer_mesh());
                    }
                }
                if (scene_root == nullptr) {
                    scene_mesh->update_rt_primitives();
                    return;
                }
                for (const std::shared_ptr<erhe::scene::Mesh>& affected_mesh : affected_meshes) {
                    // Rebuilds the raytrace primitives from the committed
                    // shapes and re-registers the mesh's draw list records
                    // (Mesh::update_rt_primitives -> notify_primitives_changed).
                    affected_mesh->update_rt_primitives();
                    scene_root->end_mesh_rt_update(affected_mesh);
                    // Fresh raytrace instances start with identity transforms;
                    // push the node's world transform (and commit) right away
                    // instead of waiting for the next node-transform update.
                    affected_mesh->handle_node_transform_update();
                }
            }
        );
    }
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
    for (const std::shared_ptr<erhe::Item_base>& item : m_mesh_node_items) {
        const std::vector<std::shared_ptr<erhe::Item_base>> single_item{item};
        async_for_nodes_with_mesh(
            context,
            single_item,
            [scene_root](Mesh_operation_parameters&& mesh_operation_parameters)
            {
                deferred_finalize_mesh_items(std::move(mesh_operation_parameters), scene_root);
            }
        );
    }
}

void Async_raytrace_kickoff_operation::undo(App_context&)
{
    // In-flight async tasks captured scene_root and the item shared_ptrs at
    // task creation; they complete safely against the captured scene_root
    // even if items have been detached from it by sibling sub-op undos.
}

}
