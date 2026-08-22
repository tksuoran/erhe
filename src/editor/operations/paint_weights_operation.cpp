#include "operations/paint_weights_operation.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_item/item_host.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
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

    erhe::scene::Node* node = m_parameters.mesh->get_node();
    if (node == nullptr) {
        set_error("Paint_weights_operation: mesh node is null");
        return;
    }
    erhe::Item_host* item_host = node->get_item_host();
    if (item_host == nullptr) {
        set_error("Paint_weights_operation: item host is null");
        return;
    }
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};

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
    std::shared_ptr<erhe::primitive::Primitive> new_primitive = std::make_shared<erhe::primitive::Primitive>(m_parameters.geometry);
    const bool renderable_ok = new_primitive->make_renderable_mesh(m_parameters.build_info, m_parameters.normal_style);
    const bool raytrace_ok   = new_primitive->make_raytrace();
    ERHE_VERIFY(renderable_ok && raytrace_ok);

    // Collect-then-rebuild: the re-parent dance below mutates the scene's
    // mesh-layer vectors, so we must not be iterating them.
    auto* const                                     scene_root = static_cast<Scene_root*>(item_host);
    erhe::scene::Scene&                             scene      = scene_root->get_scene();
    std::vector<std::shared_ptr<erhe::scene::Mesh>> referers;
    for (const std::shared_ptr<erhe::scene::Mesh_layer>& layer : scene.get_mesh_layers()) {
        for (const std::shared_ptr<erhe::scene::Mesh>& mesh : layer->meshes) {
            if (!mesh) {
                continue;
            }
            const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
            for (const erhe::scene::Mesh_primitive& mesh_primitive : primitives) {
                const std::shared_ptr<erhe::primitive::Primitive>& primitive = mesh_primitive.primitive;
                if (primitive && primitive->render_shape &&
                    (primitive->render_shape->get_geometry().get() == m_parameters.geometry.get())) {
                    referers.push_back(mesh);
                    break;
                }
            }
        }
    }

    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : referers) {
        erhe::scene::Node* mesh_node = mesh->get_node();
        if (mesh_node == nullptr) {
            continue;
        }

        std::vector<erhe::scene::Mesh_primitive> new_primitives = mesh->get_primitives();
        for (erhe::scene::Mesh_primitive& mesh_primitive : new_primitives) {
            if (mesh_primitive.primitive && mesh_primitive.primitive->render_shape &&
                (mesh_primitive.primitive->render_shape->get_geometry().get() == m_parameters.geometry.get())) {
                mesh_primitive.primitive = new_primitive;
            }
        }

        // Re-attach raytrace via the node re-parent dance Mesh_operation uses.
        // No physics rebuild: vertex positions are unchanged.
        std::shared_ptr<erhe::Hierarchy> parent = mesh_node->get_parent().lock();
        mesh_node->set_parent(std::shared_ptr<erhe::Hierarchy>{});
        mesh->set_primitives(new_primitives);
        mesh_node->set_parent(parent);

        context.app_message_bus->mesh_geometry_changed.send_message(
            Mesh_geometry_changed_message{.mesh = mesh}
        );
    }
}

}
