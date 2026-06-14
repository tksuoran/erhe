#include "operations/move_mesh_vertices_operation.hpp"

#include "app_context.hpp"
#include "app_settings.hpp"
#include "editor_log.hpp"
#include "scene/node_physics.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_item/item_host.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_verify/verify.hpp"

#include <geogram/mesh/mesh.h>

#include <string>

using erhe::geometry::get_pointf;
using erhe::geometry::set_pointf;
using erhe::geometry::make_convex_hull;

namespace editor {

namespace {

// A vertex move leaves the geometry's baked normal attributes (facet_normal,
// corner_normal, vertex_normal, vertex_normal_smooth) stale, and the primitive
// builder prefers those stored attributes over computing from positions. Recompute
// them from the moved positions so shading follows the new surface. Only attributes
// that already exist are refreshed (preserving which attributes the mesh carries);
// per-corner hard-edge shading is collapsed to smooth - a known limitation.
void refresh_geometry_normals(erhe::geometry::Geometry& geometry)
{
    GEO::Mesh&                       mesh       = geometry.get_mesh();
    erhe::geometry::Mesh_attributes& attributes = geometry.get_attributes();

    erhe::geometry::compute_facet_normals          (mesh, attributes);
    erhe::geometry::compute_mesh_vertex_normal_smooth(mesh, attributes);

    for (GEO::index_t vertex = 0, end = mesh.vertices.nb(); vertex < end; ++vertex) {
        if (attributes.vertex_normal.try_get(vertex).has_value()) {
            attributes.vertex_normal.set(vertex, attributes.vertex_normal_smooth.get(vertex));
        }
    }
    for (GEO::index_t corner = 0, end = mesh.facet_corners.nb(); corner < end; ++corner) {
        if (attributes.corner_normal.try_get(corner).has_value()) {
            const GEO::index_t vertex = mesh.facet_corners.vertex(corner);
            attributes.corner_normal.set(corner, attributes.vertex_normal_smooth.get(vertex));
        }
    }
}

} // anonymous namespace

Move_mesh_vertices_operation::Move_mesh_vertices_operation(Parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
    set_description("Move " + std::to_string(m_parameters.vertices.size()) + " mesh vertices");
}

void Move_mesh_vertices_operation::execute(App_context& context)
{
    apply(context, m_parameters.after_positions);
}

void Move_mesh_vertices_operation::undo(App_context& context)
{
    apply(context, m_parameters.before_positions);
}

void Move_mesh_vertices_operation::apply(App_context& context, const std::vector<glm::vec3>& positions)
{
    if (!m_parameters.mesh || !m_parameters.geometry) {
        set_error("Move_mesh_vertices_operation: mesh or geometry is null");
        return;
    }
    if (positions.size() != m_parameters.vertices.size()) {
        set_error("Move_mesh_vertices_operation: position count mismatch");
        return;
    }

    erhe::scene::Node* node = m_parameters.mesh->get_node();
    if (node == nullptr) {
        set_error("Move_mesh_vertices_operation: mesh node is null");
        return;
    }
    erhe::Item_host* item_host = node->get_item_host();
    if (item_host == nullptr) {
        set_error("Move_mesh_vertices_operation: item host is null");
        return;
    }
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};

    const std::vector<erhe::scene::Mesh_primitive>& current_primitives = m_parameters.mesh->get_primitives();
    if (m_parameters.primitive_index >= current_primitives.size()) {
        set_error("Move_mesh_vertices_operation: primitive index out of range");
        return;
    }

    // Write the target positions into the shared geometry, then refresh the normal
    // attributes from the new positions (topology is unchanged - no connect, which
    // would renumber corners and invalidate the stored component indices).
    GEO::Mesh& geo_mesh = m_parameters.geometry->get_mesh();
    for (std::size_t i = 0, end = m_parameters.vertices.size(); i < end; ++i) {
        const glm::vec3& p = positions[i];
        set_pointf(geo_mesh.vertices, m_parameters.vertices[i], GEO::vec3f{p.x, p.y, p.z});
    }
    refresh_geometry_normals(*m_parameters.geometry);

    // Rebuild only the edited primitive's GPU + raytrace data, keeping the same
    // Geometry object so the component selection (keyed on the Geometry pointer)
    // remains valid. Other primitives and materials are preserved.
    std::vector<erhe::scene::Mesh_primitive> new_primitives = current_primitives;
    std::shared_ptr<erhe::primitive::Primitive> new_primitive = std::make_shared<erhe::primitive::Primitive>(m_parameters.geometry);
    const bool renderable_ok = new_primitive->make_renderable_mesh(m_parameters.build_info, m_parameters.normal_style);
    const bool raytrace_ok   = new_primitive->make_raytrace();
    ERHE_VERIFY(renderable_ok && raytrace_ok);
    new_primitives[m_parameters.primitive_index].primitive = new_primitive;

    // Re-attach raytrace (and rebuild static physics) via the same node re-parent
    // dance Mesh_operation uses.
    std::shared_ptr<erhe::Hierarchy>   parent      = node->get_parent().lock();
    std::shared_ptr<erhe::scene::Node> node_shared = std::dynamic_pointer_cast<erhe::scene::Node>(node->shared_from_this());

    std::shared_ptr<Node_physics> old_node_physics = erhe::scene::get_attachment<Node_physics>(node);
    const erhe::physics::Motion_mode motion_mode = old_node_physics
        ? old_node_physics->get_motion_mode()
        : erhe::physics::Motion_mode::e_invalid;

    std::shared_ptr<Node_physics> new_node_physics;
    if (context.editor_settings->physics.static_enable && old_node_physics) {
        GEO::Mesh convex_hull{};
        const bool convex_hull_ok = make_convex_hull(geo_mesh, convex_hull);
        ERHE_VERIFY(convex_hull_ok);

        std::vector<float> coordinates;
        coordinates.resize(convex_hull.vertices.nb() * 3);
        for (GEO::index_t vertex : convex_hull.vertices) {
            const GEO::vec3f p = get_pointf(convex_hull.vertices, vertex);
            coordinates[3 * vertex + 0] = p.x;
            coordinates[3 * vertex + 1] = p.y;
            coordinates[3 * vertex + 2] = p.z;
        }

        std::shared_ptr<erhe::physics::ICollision_shape> collision_shape =
            erhe::physics::ICollision_shape::create_convex_hull_shape_shared(
                coordinates.data(),
                static_cast<int>(convex_hull.vertices.nb()),
                static_cast<int>(3 * sizeof(float))
            );

        const erhe::physics::IRigid_body_create_info rigid_body_create_info{
            .collision_shape = collision_shape,
            .debug_label     = m_parameters.geometry->get_name(),
            .motion_mode     = motion_mode
        };
        new_node_physics = std::make_shared<Node_physics>(rigid_body_create_info);
    }

    node->set_parent(std::shared_ptr<erhe::Hierarchy>{});
    if (old_node_physics) {
        node->detach(old_node_physics.get());
    }
    m_parameters.mesh->set_primitives(new_primitives);
    if (new_node_physics) {
        node->attach(new_node_physics);
    }
    node->set_parent(parent);
}

}
