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

#include <algorithm>
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

namespace {

template <typename T>
void capture_attribute(const erhe::geometry::Attribute_present<T>& attribute, const GEO::index_t count, std::vector<T>& values, std::vector<bool>& present)
{
    values.resize(count);
    present.resize(count);
    for (GEO::index_t i = 0; i < count; ++i) {
        const bool has = attribute.has(i);
        present[i] = has;
        values [i] = has ? attribute.attribute[i] : T{};
    }
}

template <typename T>
void restore_attribute(erhe::geometry::Attribute_present<T>& attribute, const std::vector<T>& values, const std::vector<bool>& present)
{
    for (GEO::index_t i = 0, end = static_cast<GEO::index_t>(values.size()); i < end; ++i) {
        if (present[i]) {
            attribute.set(i, values[i]);
        } else {
            attribute.unset(i);
        }
    }
}

} // anonymous namespace

void Geometry_normals_snapshot::capture(const erhe::geometry::Geometry& geometry)
{
    const GEO::Mesh&                       mesh       = geometry.get_mesh();
    const erhe::geometry::Mesh_attributes& attributes = geometry.get_attributes();
    capture_attribute(attributes.facet_normal,         mesh.facets.nb(),        m_facet_normal,         m_facet_normal_present);
    capture_attribute(attributes.vertex_normal,        mesh.vertices.nb(),      m_vertex_normal,        m_vertex_normal_present);
    capture_attribute(attributes.vertex_normal_smooth, mesh.vertices.nb(),      m_vertex_normal_smooth, m_vertex_normal_smooth_present);
    capture_attribute(attributes.corner_normal,        mesh.facet_corners.nb(), m_corner_normal,        m_corner_normal_present);
}

void Geometry_normals_snapshot::restore(erhe::geometry::Geometry& geometry) const
{
    erhe::geometry::Mesh_attributes& attributes = geometry.get_attributes();
    restore_attribute(attributes.facet_normal,         m_facet_normal,         m_facet_normal_present);
    restore_attribute(attributes.vertex_normal,        m_vertex_normal,        m_vertex_normal_present);
    restore_attribute(attributes.vertex_normal_smooth, m_vertex_normal_smooth, m_vertex_normal_smooth_present);
    restore_attribute(attributes.corner_normal,        m_corner_normal,        m_corner_normal_present);
}

void Geometry_normals_snapshot::clear()
{
    m_facet_normal                = {};
    m_facet_normal_present        = {};
    m_vertex_normal               = {};
    m_vertex_normal_present       = {};
    m_vertex_normal_smooth        = {};
    m_vertex_normal_smooth_present = {};
    m_corner_normal               = {};
    m_corner_normal_present       = {};
}

Move_mesh_vertices_operation::Move_mesh_vertices_operation(Parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
    set_description(
        m_parameters.description.empty()
            ? ("Move " + std::to_string(m_parameters.vertices.size()) + " mesh vertices")
            : m_parameters.description
    );
}

void Move_mesh_vertices_operation::write_geometry(const State state)
{
    const std::vector<glm::vec3>& positions = (state == State::after) ? m_parameters.after_positions : m_parameters.before_positions;
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
}

void Move_mesh_vertices_operation::execute(App_context& context)
{
    if (!m_parameters.mesh || !m_parameters.geometry) {
        set_error("Move_mesh_vertices_operation: mesh or geometry is null");
        return;
    }
    if ((m_parameters.after_positions.size() != m_parameters.vertices.size()) || (m_parameters.before_positions.size() != m_parameters.vertices.size())) {
        set_error("Move_mesh_vertices_operation: position count mismatch");
        return;
    }
    erhe::Item_host* item_host = m_parameters.mesh->get_item_host();
    if (item_host == nullptr) {
        set_error("Move_mesh_vertices_operation: item host is null");
        return;
    }
    // unique_lock, not lock_guard: the background-optimize kickoff at the end
    // dispatches through async_for_nodes_with_mesh, which takes this same
    // mutex itself - it must run after an explicit unlock or std::mutex
    // throws "resource deadlock would occur".
    std::unique_lock<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};

    if (m_parameters.primitive_index >= m_parameters.mesh->get_primitives().size()) {
        set_error("Move_mesh_vertices_operation: primitive index out of range");
        return;
    }

    // Topology is unchanged: no connect, which would renumber corners and
    // invalidate the stored component indices.
    m_before_normals.capture(*m_parameters.geometry);
    write_geometry(State::after);
    refresh_geometry_normals(*m_parameters.geometry);

    // One new Primitive for the (unchanged) Geometry object, shared across
    // every mesh that references it. With worker contexts the optimized
    // variant is rebuilt in the background after the swap (kickoff below).
    erhe::primitive::Build_info build_info = m_parameters.build_info;
    const bool background_optimize =
        build_info.buffer_info.optimize_meshes &&
        (context.graphics_device != nullptr) &&
        context.graphics_device->supports_worker_contexts();
    if (background_optimize) {
        build_info.buffer_info.optimize_meshes = false;
    }
    std::shared_ptr<erhe::primitive::Primitive> new_primitive = std::make_shared<erhe::primitive::Primitive>(m_parameters.geometry);
    // The rebuilt primitive keeps the LOD request of the one it replaces.
    new_primitive->lod_settings = get_geometry_lod_settings(*m_parameters.mesh, m_parameters.geometry.get());
    const bool renderable_ok = new_primitive->make_renderable_mesh(build_info, m_parameters.normal_style);
    const bool raytrace_ok   = new_primitive->make_raytrace();
    ERHE_VERIFY(renderable_ok && raytrace_ok);

    // Shared convex-hull collision shape (same Geometry -> same local-space hull),
    // built lazily on the first referencing mesh that actually has static physics.
    const bool                                       static_enable = context.editor_settings->physics.static_enable;
    std::shared_ptr<erhe::physics::ICollision_shape> shared_collision_shape;
    const GEO::Mesh&                                 geo_mesh = m_parameters.geometry->get_mesh();

    m_before_physics.clear();
    share_rebuilt_primitive(
        context,
        m_parameters.mesh,
        m_parameters.geometry.get(),
        new_primitive,
        [&](erhe::scene::Mesh& mesh) {
            const Mesh_operation::Entry::Version physics_before = Mesh_operation::capture_physics(mesh);
            m_before_physics.push_back(physics_before);
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
        },
        &m_before_primitives
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

void Move_mesh_vertices_operation::undo(App_context& context)
{
    if (!m_parameters.mesh || !m_parameters.geometry) {
        set_error("Move_mesh_vertices_operation: mesh or geometry is null");
        return;
    }
    erhe::Item_host* item_host = m_parameters.mesh->get_item_host();
    if (item_host == nullptr) {
        set_error("Move_mesh_vertices_operation: item host is null");
        return;
    }
    std::unique_lock<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};

    write_geometry(State::before);
    m_before_normals.restore(*m_parameters.geometry);
    restore_mesh_primitives(
        context,
        m_before_primitives,
        [this](erhe::scene::Mesh& mesh, const std::size_t i) {
            Mesh_operation::restore_physics(mesh, m_before_physics.at(i));
        }
    );
    // A primitive a live drag edited lost its optimized variant at the drag
    // start (Mesh::begin_optimized_variant_edit()): the finalize rebuilds it.
    const bool lost_optimized_variant = std::any_of(
        m_before_primitives.begin(), m_before_primitives.end(),
        [this](const Retained_mesh_primitives& entry) {
            const std::vector<erhe::scene::Mesh_primitive>& primitives = entry.mesh->get_primitives();
            return
                (m_parameters.primitive_index < primitives.size()) &&
                primitives[m_parameters.primitive_index].primitive &&
                !primitives[m_parameters.primitive_index].primitive->optimized_render_shape;
        }
    );
    // The restored primitives are live again; redo captures them anew.
    m_before_primitives.clear();
    m_before_physics.clear();
    m_before_normals.clear();

    const bool background_optimize =
        lost_optimized_variant &&
        m_parameters.build_info.buffer_info.optimize_meshes &&
        (context.graphics_device != nullptr) &&
        context.graphics_device->supports_worker_contexts();
    scene_lock.unlock();
    if (background_optimize) {
        kickoff_deferred_finalize(context, m_parameters.mesh);
    }
}

}
