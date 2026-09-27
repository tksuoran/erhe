#include "operations/paint_weights_operation.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "operations/async_raytrace_kickoff_operation.hpp"
#include "operations/mesh_primitive_swap.hpp"

#include "erhe_graphics/device.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_item/item_host.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_verify/verify.hpp"

#include <geogram/mesh/mesh.h>

#include <string>

namespace editor {

Paint_weights_operation::Paint_weights_operation(Parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
    set_description("Paint weights on " + std::to_string(m_parameters.vertices.size()) + " vertices");
}

void Paint_weights_operation::execute(App_context& context)
{
    apply(context, m_parameters.after_joint_indices, m_parameters.after_joint_weights);
}

void Paint_weights_operation::undo(App_context& context)
{
    apply(context, m_parameters.before_joint_indices, m_parameters.before_joint_weights);
}

void Paint_weights_operation::apply(
    App_context&                   context,
    const std::vector<glm::uvec4>& joint_indices,
    const std::vector<glm::vec4>&  joint_weights
)
{
    if (!m_parameters.mesh || !m_parameters.geometry) {
        set_error("Paint_weights_operation: mesh or geometry is null");
        return;
    }
    if (
        (joint_indices.size() != m_parameters.vertices.size()) ||
        (joint_weights.size() != m_parameters.vertices.size())
    ) {
        set_error("Paint_weights_operation: attribute count mismatch");
        return;
    }

    erhe::scene::Node* node = m_parameters.mesh.get();
    if (node == nullptr) {
        set_error("Paint_weights_operation: mesh node is null");
        return;
    }
    erhe::Item_host* item_host = node->get_item_host();
    if (item_host == nullptr) {
        set_error("Paint_weights_operation: item host is null");
        return;
    }
    // unique_lock, not lock_guard: the background-optimize kickoff at the end
    // dispatches through async_for_nodes_with_mesh, which takes this same
    // mutex itself - it must run after an explicit unlock or std::mutex
    // throws "resource deadlock would occur".
    std::unique_lock<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};

    const std::vector<erhe::scene::Mesh_primitive>& current_primitives = m_parameters.mesh->get_primitives();
    if (m_parameters.primitive_index >= current_primitives.size()) {
        set_error("Paint_weights_operation: primitive index out of range");
        return;
    }

    // Write the target joint data into the shared geometry's attributes.
    erhe::geometry::Mesh_attributes& attributes = m_parameters.geometry->get_attributes();
    for (std::size_t i = 0, end = m_parameters.vertices.size(); i < end; ++i) {
        const GEO::index_t vertex = m_parameters.vertices[i];
        const glm::uvec4&  ji     = joint_indices[i];
        const glm::vec4&   jw     = joint_weights[i];
        attributes.vertex_joint_indices_0.set(vertex, GEO::vec4u{ji.x, ji.y, ji.z, ji.w});
        attributes.vertex_joint_weights_0.set(vertex, GEO::vec4f{jw.x, jw.y, jw.z, jw.w});
    }

    // Rebuild one Primitive for the (unchanged) Geometry and share it across
    // every mesh that references the Geometry, exactly as
    // Move_mesh_vertices_operation does, so shared-geometry instances change
    // and revert together. The rebuild regenerates the fill mesh AND the
    // solid-wireframe / edge-line streams that carry their own joint data.
    //
    // Requirements 10-11: with worker contexts available the optimized variant
    // is rebuilt in the background after the swap (kickoff at the end); the
    // synchronous build stays base-only so the main thread never pays the
    // meshopt passes.
    erhe::primitive::Build_info build_info = m_parameters.build_info;
    const bool background_optimize =
        build_info.buffer_info.optimize_meshes &&
        (context.graphics_device != nullptr) &&
        context.graphics_device->supports_worker_contexts();
    if (background_optimize) {
        build_info.buffer_info.optimize_meshes = false;
    }
    std::shared_ptr<erhe::primitive::Primitive> new_primitive = std::make_shared<erhe::primitive::Primitive>(m_parameters.geometry);
    const bool renderable_ok = new_primitive->make_renderable_mesh(build_info, m_parameters.normal_style);
    const bool raytrace_ok   = new_primitive->make_raytrace();
    ERHE_VERIFY(renderable_ok && raytrace_ok);

    // No physics rebuild: vertex positions are unchanged.
    share_rebuilt_primitive(context, m_parameters.mesh, m_parameters.geometry.get(), new_primitive);

    // Background re-optimization: the finalize's snapshot sees the complete
    // base mesh missing its optimized variant, force-rebuilds it on a worker
    // and commits frame-safely; sharers are refreshed by the commit. Unlock
    // first - the dispatcher takes the item host mutex itself.
    scene_lock.unlock();
    if (background_optimize) {
        kickoff_deferred_finalize(context, m_parameters.mesh);
    }
}

}
