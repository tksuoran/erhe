#include "operations/move_mesh_vertices_operation.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_settings.hpp"
#include "editor_log.hpp"
#include "operations/async_raytrace_kickoff_operation.hpp"
#include "operations/mesh_primitive_swap.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_physics_system.hpp"
#include "operations/mesh_operation.hpp"
#include "scene/scene_root.hpp"

#include "erhe_graphics/device.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_item/item_host.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
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
    set_description(
        m_parameters.description.empty()
            ? ("Move " + std::to_string(m_parameters.vertices.size()) + " mesh vertices")
            : m_parameters.description
    );
}

void Move_mesh_vertices_operation::execute(App_context& context)
{
    apply(context, State::after);
}

void Move_mesh_vertices_operation::undo(App_context& context)
{
    apply(context, State::before);
}

void Move_mesh_vertices_operation::apply(App_context& context, const State state)
{
    const std::vector<glm::vec3>& positions = (state == State::after) ? m_parameters.after_positions : m_parameters.before_positions;
    if (!m_parameters.mesh || !m_parameters.geometry) {
        set_error("Move_mesh_vertices_operation: mesh or geometry is null");
        return;
    }
    if (positions.size() != m_parameters.vertices.size()) {
        set_error("Move_mesh_vertices_operation: position count mismatch");
        return;
    }

    erhe::scene::Node* node = m_parameters.mesh.get();
    if (node == nullptr) {
        set_error("Move_mesh_vertices_operation: mesh node is null");
        return;
    }
    erhe::Item_host* item_host = node->get_item_host();
    if (item_host == nullptr) {
        set_error("Move_mesh_vertices_operation: item host is null");
        return;
    }
    // unique_lock, not lock_guard: the background-optimize kickoff at the end
    // dispatches through async_for_nodes_with_mesh, which takes this same
    // mutex itself - it must run after an explicit unlock or std::mutex
    // throws "resource deadlock would occur".
    std::unique_lock<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};

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
    erhe::geometry::Mesh_attributes& attributes = m_parameters.geometry->get_attributes();
    for (const Corner_texcoord_change& change : m_parameters.corner_texcoords) {
        const glm::vec2& texcoord = (state == State::after) ? change.after : change.before;
        attributes.corner_texcoord(change.set).set(change.corner, GEO::vec2f{texcoord.x, texcoord.y});
    }
    refresh_geometry_normals(*m_parameters.geometry);

    // Build one new Primitive for the (unchanged) Geometry object and share it
    // across EVERY mesh that references this Geometry - not just the edited one.
    // The Geometry pointer is reused (component-selection entries keyed on it stay
    // valid); the Primitive (GPU + raytrace) is rebuilt so all instances reflect
    // the move and, crucially, revert together on undo. (A duplicated node shares
    // the Primitive/Geometry by shared_ptr, so rebuilding only the edited mesh left
    // the others stale on undo.)
    //
    // Requirements 10-11: with worker contexts available the optimized variant
    // is rebuilt in the BACKGROUND after the swap (kickoff below) - the
    // synchronous build here stays base-only so the swapped-in primitive is
    // immediately renderable and the main thread never pays the meshopt
    // passes. Without worker contexts it builds synchronously as before.
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

    // Shared convex-hull collision shape (same Geometry -> same local-space hull),
    // built lazily on the first referencing mesh that actually has static physics.
    const bool                                       static_enable = context.editor_settings->physics.static_enable;
    std::shared_ptr<erhe::physics::ICollision_shape> shared_collision_shape;

    share_rebuilt_primitive(
        context,
        m_parameters.mesh,
        m_parameters.geometry.get(),
        new_primitive,
        [&](erhe::scene::Mesh& mesh) {
            const Mesh_operation::Entry::Version physics_before = Mesh_operation::capture_physics(mesh);
            Mesh_operation::Entry::Version       physics_after{};
            if (static_enable && (physics_before.motion_mode != erhe::physics::Motion_mode::e_none)) {
                if (!shared_collision_shape) {
                    GEO::Mesh convex_hull{};
                    // A moved-vertex result with no volume has no convex hull;
                    // make_convex_hull() logs the reason and the mesh is left
                    // without a rigid body.
                    const bool convex_hull_ok = make_convex_hull(geo_mesh, convex_hull);
                    if (convex_hull_ok) {
                        std::vector<float> coordinates;
                        coordinates.resize(convex_hull.vertices.nb() * 3);
                        for (GEO::index_t vertex : convex_hull.vertices) {
                            const GEO::vec3f p = get_pointf(convex_hull.vertices, vertex);
                            coordinates[3 * vertex + 0] = p.x;
                            coordinates[3 * vertex + 1] = p.y;
                            coordinates[3 * vertex + 2] = p.z;
                        }
                        shared_collision_shape = erhe::physics::ICollision_shape::create_convex_hull_shape_shared(
                            coordinates.data(),
                            static_cast<int>(convex_hull.vertices.nb()),
                            static_cast<int>(3 * sizeof(float))
                        );
                    }
                }

                if (shared_collision_shape) {
                    physics_after.collision_shape = shared_collision_shape;
                    physics_after.motion_mode     = physics_before.motion_mode;
                }
            }
            Mesh_operation::restore_physics(mesh, physics_after);
        }
    );

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
