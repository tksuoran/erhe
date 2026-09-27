#include "operations/paint_colors_operation.hpp"

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

Paint_colors_operation::Paint_colors_operation(Parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
    set_description("Paint colors on " + std::to_string(m_parameters.corners.size()) + " corners");
}

void Paint_colors_operation::execute(App_context& context)
{
    apply(context, m_parameters.after_colors);
}

void Paint_colors_operation::undo(App_context& context)
{
    apply(context, m_parameters.before_colors);
}

void Paint_colors_operation::apply(App_context& context, const std::vector<glm::vec4>& colors)
{
    if (!m_parameters.mesh || !m_parameters.geometry) {
        set_error("Paint_colors_operation: mesh or geometry is null");
        return;
    }
    if (colors.size() != m_parameters.corners.size()) {
        set_error("Paint_colors_operation: color count mismatch");
        return;
    }

    erhe::scene::Node* node = m_parameters.mesh.get();
    if (node == nullptr) {
        set_error("Paint_colors_operation: mesh node is null");
        return;
    }
    erhe::Item_host* item_host = node->get_item_host();
    if (item_host == nullptr) {
        set_error("Paint_colors_operation: item host is null");
        return;
    }
    // unique_lock, not lock_guard: the background-optimize kickoff at the end
    // dispatches through async_for_nodes_with_mesh, which takes this same
    // mutex itself - it must run after an explicit unlock or std::mutex
    // throws "resource deadlock would occur".
    std::unique_lock<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};

    const std::vector<erhe::scene::Mesh_primitive>& current_primitives = m_parameters.mesh->get_primitives();
    if (m_parameters.primitive_index >= current_primitives.size()) {
        set_error("Paint_colors_operation: primitive index out of range");
        return;
    }

    // Write the target colors into the shared geometry's corner attribute -
    // the attribute the primitive builder prefers for the color stream.
    erhe::geometry::Mesh_attributes& attributes = m_parameters.geometry->get_attributes();
    for (std::size_t i = 0, end = m_parameters.corners.size(); i < end; ++i) {
        const GEO::index_t corner = m_parameters.corners[i];
        const glm::vec4&   color  = colors[i];
        attributes.corner_color_0.set(corner, GEO::vec4f{color.x, color.y, color.z, color.w});
    }

    // Rebuild one Primitive for the (unchanged) Geometry and share it across
    // every mesh that references the Geometry, exactly as
    // Paint_weights_operation does, so shared-geometry instances change and
    // revert together. The rebuild reads the corner colors back into the fill
    // mesh and the auxiliary streams.
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
