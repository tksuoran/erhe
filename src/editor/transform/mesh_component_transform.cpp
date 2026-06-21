#include "transform/mesh_component_transform.hpp"
#include "transform/mesh_component_extrude.hpp"
#include "transform/transform_tool.hpp"

#include "app_context.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/mesh_transform_mode.hpp"
#include "operations/compound_operation.hpp"
#include "operations/fork_geometry_operation.hpp"
#include "operations/move_mesh_vertices_operation.hpp"
#include "operations/operation.hpp"
#include "operations/operation_stack.hpp"
#include "scene/scene_root.hpp"
#include "tools/mesh_component_selection.hpp"

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_item/item_host.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_verify/verify.hpp"

#include <geogram/mesh/mesh.h>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

using erhe::geometry::get_pointf;
using erhe::geometry::set_pointf;

namespace editor {

namespace {

// Map the captured transform mode to how the extrude builder fills move_directions:
// the two normal modes slide along stored per-vertex normals, everything else uses the
// raw gizmo delta.
auto to_extrude_normal_mode(const Mesh_transform_mode mode) -> Extrude_normal_mode
{
    switch (mode) {
        case Mesh_transform_mode::extrude_group_normal:  return Extrude_normal_mode::group;
        case Mesh_transform_mode::extrude_vertex_normal: return Extrude_normal_mode::vertex;
        default:                                         return Extrude_normal_mode::none;
    }
}

// Has the gizmo actually moved? Used to defer fork-on-edit to the first real drag
// (a click on a handle without dragging passes an identity delta and must not fork).
auto is_nontrivial_delta(const glm::mat4& m) -> bool
{
    const glm::mat4 id{1.0f};
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (std::abs(m[c][r] - id[c][r]) > 1e-6f) {
                return true;
            }
        }
    }
    return false;
}

// Build a right-handed orthonormal rotation with the surface normal as local +Y
// and the tangent as local +X (the node "sits flat" on the surface, up along the
// normal). The normal is authoritative; the tangent is orthogonalized against it,
// falling back to the smallest world axis when degenerate.
auto make_frame_rotation_normal_up(glm::vec3 normal_ws, glm::vec3 tangent_ws) -> glm::quat
{
    const glm::vec3 y = glm::normalize(normal_ws);
    glm::vec3       x = tangent_ws - glm::dot(tangent_ws, y) * y;
    if (glm::length(x) < 1e-6f) {
        const glm::vec3 fallback = erhe::math::min_axis<float>(y);
        x = fallback - glm::dot(fallback, y) * y;
    }
    x = glm::normalize(x);
    const glm::vec3 z = glm::cross(x, y);
    return glm::quat_cast(glm::mat3{x, y, z});
}

// Edge variant: the edge direction is authoritative as local +X, the (blended)
// normal is orthogonalized into local +Y.
auto make_frame_rotation_edge(glm::vec3 edge_dir_ws, glm::vec3 normal_ws) -> glm::quat
{
    const glm::vec3 x = glm::normalize(edge_dir_ws);
    glm::vec3       y = normal_ws - glm::dot(normal_ws, x) * x;
    if (glm::length(y) < 1e-6f) {
        const glm::vec3 fallback = erhe::math::min_axis<float>(x);
        y = fallback - glm::dot(fallback, x) * x;
    }
    y = glm::normalize(y);
    const glm::vec3 z = glm::cross(x, y);
    return glm::quat_cast(glm::mat3{x, y, z});
}

// Derive a world-space orientation for the temp frame from the first live mesh
// component (face / edge / vertex) of the selection, honoring the user's
// normal->+Y, tangent->+X convention. Returns false when no usable component
// exists (the caller then keeps its fallback orientation).
auto compute_selection_frame_rotation(Mesh_component_selection& selection, const float edge_blend, glm::quat& out_rotation) -> bool
{
    const Mesh_component_mode mode = selection.get_mode();
    if (mode == Mesh_component_mode::object) {
        return false;
    }

    for (const Mesh_component_entry& entry : selection.get_entries()) {
        if (!selection.is_live(entry)) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Mesh>        mesh     = entry.mesh.lock();
        const std::shared_ptr<erhe::geometry::Geometry> geometry = entry.geometry.lock();
        if (!mesh || !geometry) {
            continue;
        }
        const erhe::scene::Node* node = mesh->get_node();
        if (node == nullptr) {
            continue;
        }
        const glm::mat4                        world_from_node = node->world_from_node();
        const glm::mat3                        dir_matrix      = glm::mat3{world_from_node};
        const glm::mat3                        normal_matrix   = glm::transpose(glm::inverse(dir_matrix));
        const GEO::Mesh&                       geo_mesh        = geometry->get_mesh();
        const erhe::geometry::Mesh_attributes& attributes      = geometry->get_attributes();

        // Local-space facet normal: prefer the stored (normalized) attribute, else
        // compute and normalize the area-weighted normal.
        const auto facet_normal_local = [&](const GEO::index_t facet) -> glm::vec3 {
            if (attributes.facet_normal.has(facet)) {
                const GEO::vec3f n = attributes.facet_normal.get(facet);
                return glm::vec3{n.x, n.y, n.z};
            }
            const GEO::vec3f n = erhe::geometry::mesh_facet_normalf(geo_mesh, facet);
            return glm::normalize(glm::vec3{n.x, n.y, n.z});
        };

        switch (mode) {
            case Mesh_component_mode::face: {
                if (entry.facets.empty()) {
                    continue;
                }
                const GEO::index_t facet        = *entry.facets.begin();
                const glm::vec3    normal_local = facet_normal_local(facet);
                glm::vec3          tangent_local{0.0f};
                if (attributes.facet_tangent.has(facet)) {
                    const GEO::vec4f t = attributes.facet_tangent.get(facet);
                    tangent_local = glm::vec3{t.x, t.y, t.z};
                } else {
                    // Fall back to the facet's first edge direction.
                    const GEO::index_t corner_begin = geo_mesh.facets.corners_begin(facet);
                    const GEO::index_t corner_end   = geo_mesh.facets.corners_end(facet);
                    if (corner_begin + 1 < corner_end) {
                        const GEO::index_t v0 = geo_mesh.facet_corners.vertex(corner_begin);
                        const GEO::index_t v1 = geo_mesh.facet_corners.vertex(corner_begin + 1);
                        const GEO::vec3f   p0 = get_pointf(geo_mesh.vertices, v0);
                        const GEO::vec3f   p1 = get_pointf(geo_mesh.vertices, v1);
                        tangent_local = glm::vec3{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
                    }
                }
                out_rotation = make_frame_rotation_normal_up(normal_matrix * normal_local, dir_matrix * tangent_local);
                return true;
            }
            case Mesh_component_mode::edge: {
                if (entry.edges.empty()) {
                    continue;
                }
                const Mesh_edge_key edge_key = *entry.edges.begin();
                const GEO::index_t  v0       = edge_key.first;
                const GEO::index_t  v1       = edge_key.second;
                const GEO::vec3f    p0       = get_pointf(geo_mesh.vertices, v0);
                const GEO::vec3f    p1       = get_pointf(geo_mesh.vertices, v1);
                const glm::vec3     edge_dir_local{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};

                // Blend (nlerp) the normals of the two facets sharing the edge.
                glm::vec3          normal_local{0.0f, 1.0f, 0.0f};
                const GEO::index_t edge = geometry->get_edge(v0, v1);
                if (edge != GEO::NO_EDGE) {
                    const std::vector<GEO::index_t>& facets = geometry->get_edge_facets(edge);
                    if (!facets.empty()) {
                        const glm::vec3 n0 = facet_normal_local(facets[0]);
                        const glm::vec3 n1 = (facets.size() >= 2) ? facet_normal_local(facets[1]) : n0;
                        normal_local = glm::normalize(glm::mix(n0, n1, edge_blend));
                    }
                }
                out_rotation = make_frame_rotation_edge(dir_matrix * edge_dir_local, normal_matrix * normal_local);
                return true;
            }
            case Mesh_component_mode::vertex: {
                if (entry.vertices.empty()) {
                    continue;
                }
                const GEO::index_t vertex = *entry.vertices.begin();
                glm::vec3          normal_local{0.0f};
                if (attributes.vertex_normal.has(vertex)) {
                    const GEO::vec3f n = attributes.vertex_normal.get(vertex);
                    normal_local = glm::vec3{n.x, n.y, n.z};
                } else if (attributes.vertex_normal_smooth.has(vertex)) {
                    const GEO::vec3f n = attributes.vertex_normal_smooth.get(vertex);
                    normal_local = glm::vec3{n.x, n.y, n.z};
                } else {
                    // Average the normals of the facets around the vertex.
                    for (const GEO::index_t corner : geometry->get_vertex_corners(vertex)) {
                        normal_local += facet_normal_local(geometry->get_corner_facet(corner));
                    }
                }
                if (glm::length(normal_local) < 1e-6f) {
                    normal_local = glm::vec3{0.0f, 1.0f, 0.0f};
                }

                glm::vec3 tangent_local{0.0f};
                if (attributes.vertex_tangent.has(vertex)) {
                    const GEO::vec4f t = attributes.vertex_tangent.get(vertex);
                    tangent_local = glm::vec3{t.x, t.y, t.z};
                } else {
                    tangent_local = erhe::math::min_axis<float>(glm::normalize(normal_local));
                }
                out_rotation = make_frame_rotation_normal_up(normal_matrix * normal_local, dir_matrix * tangent_local);
                return true;
            }
            case Mesh_component_mode::object:
            default: {
                break;
            }
        }
    }
    return false;
}

} // anonymous namespace

auto Mesh_component_transform::gather(App_context& context) -> bool
{
    m_groups.clear();

    Mesh_component_selection* selection = context.mesh_component_selection;
    if (selection == nullptr) {
        return false;
    }
    const Mesh_component_mode mode = selection->get_mode();
    if (mode == Mesh_component_mode::object) {
        return false;
    }

    selection->prune();
    for (const Mesh_component_entry& entry : selection->get_entries()) {
        if (!selection->is_live(entry)) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Mesh> mesh = entry.mesh.lock();
        const std::size_t                        primitive_index = entry.primitive_index;
        const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
        const std::shared_ptr<erhe::primitive::Primitive>& primitive = primitives[primitive_index].primitive;

        // Component editing is only valid when the raytrace/selection geometry is the
        // same object as the render geometry, i.e. there is no separate collision
        // Primitive_shape; otherwise the selection indices address the collision
        // geometry, not the render geometry, and editing would corrupt the mesh.
        if (primitive->collision_shape) {
            continue;
        }
        const std::shared_ptr<erhe::primitive::Primitive_render_shape>& render_shape = primitive->render_shape;
        if (!render_shape) {
            continue;
        }
        const std::shared_ptr<erhe::geometry::Geometry>& geometry = render_shape->get_geometry();
        if (!geometry) {
            continue;
        }
        if (mesh->get_node() == nullptr) {
            continue;
        }

        const GEO::Mesh& geo_mesh = geometry->get_mesh();

        std::vector<GEO::index_t> vertices;
        switch (mode) {
            case Mesh_component_mode::vertex: {
                for (const GEO::index_t vertex : entry.vertices) {
                    vertices.push_back(vertex);
                }
                break;
            }
            case Mesh_component_mode::edge: {
                for (const Mesh_edge_key& edge : entry.edges) {
                    vertices.push_back(edge.first);
                    vertices.push_back(edge.second);
                }
                break;
            }
            case Mesh_component_mode::face: {
                for (const GEO::index_t facet : entry.facets) {
                    const GEO::index_t corner_begin = geo_mesh.facets.corners_begin(facet);
                    const GEO::index_t corner_end   = geo_mesh.facets.corners_end(facet);
                    for (GEO::index_t corner = corner_begin; corner < corner_end; ++corner) {
                        vertices.push_back(geo_mesh.facet_corners.vertex(corner));
                    }
                }
                break;
            }
            case Mesh_component_mode::object:
            default: {
                break;
            }
        }
        if (vertices.empty()) {
            continue;
        }
        std::sort(vertices.begin(), vertices.end());
        vertices.erase(std::unique(vertices.begin(), vertices.end()), vertices.end());
        if (vertices.empty()) {
            continue;
        }

        Group& group = m_groups.emplace_back();
        group.mesh            = mesh;
        group.primitive_index = primitive_index;
        group.geometry        = geometry;
        group.vertices        = std::move(vertices);
    }

    return !m_groups.empty();
}

auto Mesh_component_transform::update_anchor(App_context& context, Transform_tool_shared& shared) -> bool
{
    if (!gather(context)) {
        shared.component_mode = false;
        shared.entries.clear();
        return false;
    }

    // Combined centroid (world space) over all selected vertices across all groups.
    glm::vec3   centroid_world{0.0f};
    std::size_t count = 0;
    for (const Group& group : m_groups) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = group.mesh.lock();
        if (!mesh) {
            continue;
        }
        const erhe::scene::Node* node = mesh->get_node();
        if (node == nullptr) {
            continue;
        }
        const glm::mat4  world_from_node = node->world_from_node();
        const GEO::Mesh& geo_mesh        = group.geometry->get_mesh();
        for (const GEO::index_t vertex : group.vertices) {
            const GEO::vec3f p     = get_pointf(geo_mesh.vertices, vertex);
            const glm::vec3  world = glm::vec3{world_from_node * glm::vec4{p.x, p.y, p.z, 1.0f}};
            centroid_world += world;
            ++count;
        }
    }
    if (count == 0) {
        shared.component_mode = false;
        shared.entries.clear();
        return false;
    }
    centroid_world /= static_cast<float>(count);

    // Orientation. In Selection mode the temp frame is derived from the selected
    // component geometry (face normal/tangent, edge direction/blended normal, or
    // vertex normal/tangent). In the other modes the frame keeps the first group's
    // mesh orientation; Reference and Global are resolved uniformly afterwards by
    // Transform_tool_shared::apply_reference_frame() / the gizmo basis.
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool      have_rotation = false;
    if (shared.settings.reference_mode == Transform_reference_mode::selection) {
        Mesh_component_selection* selection = context.mesh_component_selection;
        if (selection != nullptr) {
            have_rotation = compute_selection_frame_rotation(*selection, shared.settings.edge_normal_blend, rotation);
        }
    }
    if (!have_rotation) {
        const std::shared_ptr<erhe::scene::Mesh> first_mesh = m_groups.front().mesh.lock();
        if (first_mesh) {
            const erhe::scene::Node* node = first_mesh->get_node();
            if (node != nullptr) {
                rotation = node->world_from_node_transform().get_rotation();
            }
        }
    }

    shared.world_from_anchor_initial_state.set_trs(centroid_world, rotation, glm::vec3{1.0f});
    shared.entries.clear();
    shared.component_mode = true;
    shared.apply_reference_frame();
    return true;
}

void Mesh_component_transform::begin(App_context& context)
{
    if (!gather(context)) {
        m_active = false;
        return;
    }
    // Capture the transform mode once for the whole gesture. Extrude defers its topology
    // change to the first real move (apply()), like fork-on-edit. All three extrude modes
    // build the same topology (m_extrude); they differ only in how the duplicated vertices
    // then move: plain extrude follows the gizmo delta, the group/vertex normal modes slide
    // each vertex along a stored normal (per-subset average, or its own vertex normal).
    m_transform_mode = (context.editor_settings != nullptr)
        ? context.editor_settings->transform_mode
        : Mesh_transform_mode::move;
    m_extrude = (m_transform_mode == Mesh_transform_mode::extrude) ||
                (m_transform_mode == Mesh_transform_mode::extrude_group_normal) ||
                (m_transform_mode == Mesh_transform_mode::extrude_vertex_normal);
    for (Group& group : m_groups) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = group.mesh.lock();
        erhe::scene::Node* const                 node = mesh ? mesh->get_node() : nullptr;
        group.before_local.clear();
        if (node == nullptr) {
            continue;
        }
        group.world_from_node = node->world_from_node();
        group.node_from_world = node->node_from_world();
        const GEO::Mesh& geo_mesh = group.geometry->get_mesh();
        group.before_local.reserve(group.vertices.size());
        for (const GEO::index_t vertex : group.vertices) {
            const GEO::vec3f p = get_pointf(geo_mesh.vertices, vertex);
            group.before_local.push_back(glm::vec3{p.x, p.y, p.z});
        }
    }
    m_active = true;
}

void Mesh_component_transform::apply(App_context& context, Transform_tool_shared& shared, const glm::mat4& updated_world_from_anchor)
{
    if (!m_active) {
        return;
    }
    const glm::mat4 world_delta = updated_world_from_anchor * shared.world_from_anchor_initial_state.get_inverse_matrix();
    const bool      moved       = is_nontrivial_delta(world_delta);
    const bool      fork_mode   = (context.editor_settings != nullptr) &&
                                  (context.editor_settings->geometry_edit_mode == Geometry_edit_mode::fork);

    // Pre-pass on the first real move: trigger the deferred topology change (extrude) or
    // geometry isolation (fork) for every group BEFORE moving any vertices.
    //
    // Extrude-on-first-move: the first time the user actually moves, build an extruded
    // copy of the geometry (duplicate the selection boundary, bridge with new faces) and
    // redirect this group to move the duplicated vertices. Extrude inherently isolates
    // this instance (a new geometry copy), so it supersedes the fork path. A click
    // without dragging keeps `moved` false and never extrudes.
    //
    // Otherwise fork-on-first-move: if fork mode is on and this group's geometry is
    // shared by another mesh, deep-copy the geometry onto a new primitive for THIS mesh
    // only - BEFORE touching any positions - so the other instances never move.
    //
    // Doing the whole fleet up front (rather than lazily inside the move loop) lets the
    // extrude-normal amount below see every group's per-vertex move directions.
    const bool along_normal = (to_extrude_normal_mode(m_transform_mode) != Extrude_normal_mode::none);
    if (moved) {
        for (Group& group : m_groups) {
            const std::shared_ptr<erhe::scene::Mesh> mesh = group.mesh.lock();
            if (!mesh || !group.geometry) {
                continue;
            }
            if (group.before_local.size() != group.vertices.size()) {
                continue;
            }
            if (m_extrude) {
                if (!group.extruded) {
                    extrude_group(context, group);
                }
            } else if (fork_mode && !group.forked && is_geometry_shared(context, mesh, group.geometry.get())) {
                fork_group(context, group);
            }
        }
    }

    // Extrude (Group/Vertex Normal): the gizmo no longer transforms the vertices directly.
    // Instead each vertex slides along its own (world-space) normal by a single scalar
    // amount derived from the drag - the gizmo translation projected onto the overall
    // average move direction (falling back to the anchor frame's up axis when the per-vertex
    // normals cancel, e.g. a closed surface in Vertex Normal mode). A dedicated gizmo mode
    // will refine this mapping later.
    float amount = 0.0f;
    if (along_normal && moved) {
        glm::vec3 reference{0.0f};
        for (const Group& group : m_groups) {
            for (const glm::vec3& direction : group.move_directions) {
                reference += direction;
            }
        }
        if (glm::length(reference) < 1e-6f) {
            reference = glm::vec3{shared.world_from_anchor_initial_state.get_matrix() * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}};
        }
        if (glm::length(reference) > 1e-6f) {
            reference = glm::normalize(reference);
            const glm::vec3 translation{world_delta[3]};
            amount = glm::dot(translation, reference);
        }
    }

    for (Group& group : m_groups) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = group.mesh.lock();
        if (!mesh || !group.geometry) {
            continue;
        }
        if (group.before_local.size() != group.vertices.size()) {
            continue;
        }
        // The normal modes slide along stored per-vertex world normals; everything else
        // applies the raw gizmo delta. (move_directions is only populated/parallel after
        // a successful extrude_group; a failed extrude falls back to the delta path.)
        const bool use_normal = along_normal && (group.move_directions.size() == group.vertices.size());

        GEO::Mesh& geo_mesh = group.geometry->get_mesh();
        for (std::size_t i = 0, end = group.vertices.size(); i < end; ++i) {
            const GEO::index_t vertex = group.vertices[i];
            // When the gizmo has not moved, write the exact captured start position.
            // Going through the world round-trip with an identity delta would perturb
            // the position by a float ULP, which (before a fork) would leave a tiny
            // un-revertable change on the shared geometry, and would not exactly
            // restore a drag dragged back to the start.
            glm::vec3 local_after = group.before_local[i];
            if (moved) {
                const glm::vec3 world_before = glm::vec3{group.world_from_node * glm::vec4{group.before_local[i], 1.0f}};
                glm::vec3       world_after;
                if (use_normal) {
                    world_after = world_before + (amount * group.move_directions[i]);
                } else {
                    world_after = glm::vec3{world_delta * glm::vec4{world_before, 1.0f}};
                }
                local_after = glm::vec3{group.node_from_world * glm::vec4{world_after, 1.0f}};
            }
            set_pointf(geo_mesh.vertices, vertex, GEO::vec3f{local_after.x, local_after.y, local_after.z});
            enqueue_gpu_position(context, group, vertex, local_after);
            enqueue_gpu_edge_line_positions(context, group, vertex, local_after);
        }

        // Refresh the involved faces' normals from the new positions so shading is valid
        // live (and so extrude's brand-new faces get valid normals at all). Only when the
        // gizmo actually moved - an identity delta leaves positions and normals unchanged.
        if (moved) {
            update_group_normals(context, group);
        }
    }
}

void Mesh_component_transform::commit(App_context& context)
{
    if (!m_active) {
        return;
    }
    m_active = false;

    const erhe::primitive::Build_info build_info{
        .primitive_types = {
            .fill_triangles  = true,
            .edge_lines      = true,
            .corner_points   = true,
            .centroid_points = true
        },
        .buffer_info = context.mesh_memory->make_primitive_buffer_info()
    };

    std::vector<std::shared_ptr<Operation>> operations;
    for (Group& group : m_groups) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = group.mesh.lock();
        if (!mesh || !group.geometry || group.vertices.empty()) {
            continue;
        }
        if (group.before_local.size() != group.vertices.size()) {
            continue;
        }
        const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
        if ((group.primitive_index >= primitives.size()) || !primitives[group.primitive_index].primitive) {
            continue;
        }
        const erhe::primitive::Primitive& primitive = *primitives[group.primitive_index].primitive.get();
        if (!primitive.render_shape) {
            continue;
        }

        // Extruded group: the topology changed, so an in-place vertex move can't be
        // undone back to the original. Finalize normals from the now-final positions,
        // rebuild a clean primitive, and queue a primitive swap (before = original
        // geometry, after = extruded geometry) - undo removes the extrusion entirely.
        if (group.extruded) {
            finalize_extrude_normals(*group.geometry);
            std::shared_ptr<erhe::primitive::Primitive> after_primitive = std::make_shared<erhe::primitive::Primitive>(group.geometry);
            const bool renderable_ok = after_primitive->make_renderable_mesh(build_info, primitive.render_shape->get_normal_style());
            const bool raytrace_ok   = after_primitive->make_raytrace();
            ERHE_VERIFY(renderable_ok && raytrace_ok);
            group.extrude_after.primitive = after_primitive;

            const char* description =
                (m_transform_mode == Mesh_transform_mode::extrude_group_normal)  ? "Extrude (Group Normal)"  :
                (m_transform_mode == Mesh_transform_mode::extrude_vertex_normal) ? "Extrude (Vertex Normal)" :
                                                                                   "Extrude";
            operations.push_back(
                std::make_shared<Fork_geometry_operation>(
                    Fork_geometry_operation::Parameters{
                        .mesh            = mesh,
                        .primitive_index = group.primitive_index,
                        .before          = group.extrude_before,
                        .after           = group.extrude_after,
                        .description     = description
                    }
                )
            );
            continue;
        }

        // Record the fork (if this group forked) so it is undoable - even if the
        // subsequent move turns out to be a no-op (dragged out and back). Queued
        // before the Move op so undo reverts the move first, then un-forks.
        if (group.forked) {
            operations.push_back(
                std::make_shared<Fork_geometry_operation>(
                    Fork_geometry_operation::Parameters{
                        .mesh            = mesh,
                        .primitive_index = group.primitive_index,
                        .before          = group.fork_before,
                        .after           = group.fork_after
                    }
                )
            );
        }

        GEO::Mesh&             geo_mesh = group.geometry->get_mesh();
        std::vector<glm::vec3> after_local;
        after_local.reserve(group.vertices.size());
        for (const GEO::index_t vertex : group.vertices) {
            const GEO::vec3f p = get_pointf(geo_mesh.vertices, vertex);
            after_local.push_back(glm::vec3{p.x, p.y, p.z});
        }

        // Queue the move only if the group actually moved (a forked-but-unmoved group
        // still records its Fork op above).
        if (after_local != group.before_local) {
            operations.push_back(
                std::make_shared<Move_mesh_vertices_operation>(
                    Move_mesh_vertices_operation::Parameters{
                        .mesh             = mesh,
                        .primitive_index  = group.primitive_index,
                        .geometry         = group.geometry,
                        .vertices         = group.vertices,
                        .before_positions = group.before_local,
                        .after_positions  = after_local,
                        .build_info       = build_info,
                        .normal_style     = primitive.render_shape->get_normal_style()
                    }
                )
            );
        }
    }

    if (operations.empty()) {
        return;
    }
    if (operations.size() == 1) {
        context.operation_stack->queue(operations.front());
    } else {
        context.operation_stack->queue(
            std::make_shared<Compound_operation>(
                Compound_operation::Parameters{.operations = std::move(operations)}
            )
        );
    }
}

void Mesh_component_transform::enqueue_gpu_position(App_context& context, const Group& group, const GEO::index_t vertex, const glm::vec3& local_position)
{
    const std::shared_ptr<erhe::scene::Mesh> mesh = group.mesh.lock();
    if (!mesh) {
        return;
    }
    erhe::scene_renderer::Mesh_memory& mesh_memory = *context.mesh_memory;

    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    if ((group.primitive_index >= primitives.size()) || !primitives[group.primitive_index].primitive) {
        return;
    }
    const erhe::primitive::Primitive& primitive = *primitives[group.primitive_index].primitive.get();
    if (!primitive.render_shape) {
        return;
    }

    const erhe::primitive::Buffer_mesh&             buffer_mesh      = primitive.render_shape->get_renderable_mesh();
    const erhe::scene_renderer::Vertex_input_entry& vertex_input     = mesh_memory.get_vertex_input(buffer_mesh.vertex_input_key);
    const erhe::dataformat::Vertex_format&          vertex_format    = vertex_input.vertex_format;
    const erhe::dataformat::Attribute_stream        attribute_stream = vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::position, 0);
    if ((attribute_stream.attribute == nullptr) || (attribute_stream.stream == nullptr)) {
        return;
    }

    const erhe::primitive::Element_mappings& element_mappings = primitive.render_shape->get_element_mappings();
    const std::size_t                        stream_index     = static_cast<std::size_t>(attribute_stream.stream - vertex_format.streams.data());
    if (stream_index >= buffer_mesh.vertex_buffer_ranges.size()) {
        return;
    }
    const erhe::primitive::Buffer_range stream_vertex_buffer_range = buffer_mesh.vertex_buffer_ranges.at(stream_index);
    const erhe::dataformat::Format       format = attribute_stream.attribute->format;

    // Update every GPU vertex that maps to this geometry vertex (one per corner when
    // corner normals split the vertex), matching Paint_tool Point mode.
    for (const GEO::index_t corner : group.geometry->get_vertex_corners(vertex)) {
        if (corner >= element_mappings.mesh_corner_to_vertex_buffer_index.size()) {
            continue;
        }
        const uint32_t    vertex_id     = element_mappings.mesh_corner_to_vertex_buffer_index[corner];
        const std::size_t vertex_offset = vertex_id * attribute_stream.stream->stride + attribute_stream.attribute->offset;

        const erhe::primitive::Buffer_range vertex_buffer_update_range{
            .count        = 1,
            .element_size = erhe::dataformat::get_format_size_bytes(format),
            .byte_offset  = stream_vertex_buffer_range.byte_offset + vertex_offset,
            .pool_id      = stream_vertex_buffer_range.pool_id,
            .buffer_id    = stream_vertex_buffer_range.buffer_id
        };

        std::vector<std::uint8_t> buffer;
        if (format == erhe::dataformat::Format::format_32_vec3_float) {
            buffer.resize(sizeof(float) * 3);
            auto* const ptr = reinterpret_cast<float*>(buffer.data());
            ptr[0] = local_position.x;
            ptr[1] = local_position.y;
            ptr[2] = local_position.z;
        } else if (format == erhe::dataformat::Format::format_32_vec4_float) {
            buffer.resize(sizeof(float) * 4);
            auto* const ptr = reinterpret_cast<float*>(buffer.data());
            ptr[0] = local_position.x;
            ptr[1] = local_position.y;
            ptr[2] = local_position.z;
            ptr[3] = 1.0f;
        } else {
            continue;
        }
        mesh_memory.enqueue_vertex_data(vertex_buffer_update_range, std::move(buffer));
    }
}

void Mesh_component_transform::enqueue_gpu_edge_line_positions(App_context& context, const Group& group, const GEO::index_t vertex, const glm::vec3& local_position)
{
    const std::shared_ptr<erhe::scene::Mesh> mesh = group.mesh.lock();
    if (!mesh) {
        return;
    }
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    if ((group.primitive_index >= primitives.size()) || !primitives[group.primitive_index].primitive) {
        return;
    }
    const erhe::primitive::Primitive& primitive = *primitives[group.primitive_index].primitive.get();
    if (!primitive.render_shape) {
        return;
    }
    const erhe::primitive::Buffer_mesh&  buffer_mesh = primitive.render_shape->get_renderable_mesh();
    const erhe::primitive::Buffer_range& edge_range  = buffer_mesh.edge_line_vertex_buffer_range;
    if (edge_range.count == 0) {
        return; // primitive built without edge lines
    }

    // The content wide-line renderer reads edge_line_vertex_buffer_range (a separate
    // buffer), not the main vertex buffer enqueue_gpu_position() patches. Each geometry
    // edge contributes two consecutive 8-float entries (vec4 position + vec4 normal),
    // in mesh.edges index order, so edge e local-endpoint w is at entry (2*e + w). Patch
    // the position (first 3 floats) of every entry referencing this moved vertex so the
    // wide lines follow the drag live. Normals stay as built until commit rebuilds.
    const GEO::Mesh&                   geo_mesh    = group.geometry->get_mesh();
    const std::size_t                  entry_size  = 8 * sizeof(float);
    erhe::scene_renderer::Mesh_memory& mesh_memory = *context.mesh_memory;

    for (const GEO::index_t edge : group.geometry->get_vertex_edges(vertex)) {
        for (GEO::index_t which = 0; which < 2; ++which) {
            if (geo_mesh.edges.vertex(edge, which) != vertex) {
                continue;
            }
            const std::size_t entry_index = (static_cast<std::size_t>(edge) * 2) + which;
            const erhe::primitive::Buffer_range update_range{
                .count        = 1,
                .element_size = 3 * sizeof(float),
                .byte_offset  = edge_range.byte_offset + (entry_index * entry_size),
                .pool_id      = edge_range.pool_id,
                .buffer_id    = edge_range.buffer_id
            };
            std::vector<std::uint8_t> buffer(3 * sizeof(float));
            auto* const ptr = reinterpret_cast<float*>(buffer.data());
            ptr[0] = local_position.x;
            ptr[1] = local_position.y;
            ptr[2] = local_position.z;
            mesh_memory.enqueue_vertex_data(update_range, std::move(buffer));
        }
    }
}

void Mesh_component_transform::update_group_normals(App_context& context, Group& group)
{
    const std::shared_ptr<erhe::scene::Mesh> mesh = group.mesh.lock();
    if (!mesh || !group.geometry) {
        return;
    }
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    if ((group.primitive_index >= primitives.size()) || !primitives[group.primitive_index].primitive) {
        return;
    }
    const erhe::primitive::Primitive& primitive = *primitives[group.primitive_index].primitive.get();
    if (!primitive.render_shape) {
        return;
    }
    const erhe::primitive::Buffer_mesh&             buffer_mesh      = primitive.render_shape->get_renderable_mesh();
    const erhe::primitive::Element_mappings&        element_mappings = primitive.render_shape->get_element_mappings();
    const erhe::primitive::Normal_style             normal_style     = primitive.render_shape->get_normal_style();
    erhe::scene_renderer::Mesh_memory&              mesh_memory      = *context.mesh_memory;
    const erhe::scene_renderer::Vertex_input_entry& vertex_input     = mesh_memory.get_vertex_input(buffer_mesh.vertex_input_key);
    const erhe::dataformat::Vertex_format&          vertex_format    = vertex_input.vertex_format;

    // Resolve the two per-corner normal attribute streams: content normal (location 0,
    // shading) and smooth normal (location 1, wide-line depth bias).
    class Stream_target
    {
    public:
        bool                     valid     {false};
        std::size_t              base_offset{0};
        std::size_t              pool_id    {0};
        std::size_t              buffer_id  {0};
        std::size_t              stride     {0};
        std::size_t              offset     {0};
        erhe::dataformat::Format format     {};
    };
    const auto resolve = [&](const std::size_t attribute_index) -> Stream_target {
        Stream_target target;
        const erhe::dataformat::Attribute_stream stream = vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::normal, attribute_index);
        if ((stream.attribute == nullptr) || (stream.stream == nullptr)) {
            return target;
        }
        const std::size_t stream_index = static_cast<std::size_t>(stream.stream - vertex_format.streams.data());
        if (stream_index >= buffer_mesh.vertex_buffer_ranges.size()) {
            return target;
        }
        const erhe::primitive::Buffer_range& range = buffer_mesh.vertex_buffer_ranges[stream_index];
        target.valid       = true;
        target.base_offset = range.byte_offset;
        target.pool_id     = range.pool_id;
        target.buffer_id   = range.buffer_id;
        target.stride      = stream.stream->stride;
        target.offset      = stream.attribute->offset;
        target.format      = stream.attribute->format;
        return target;
    };
    const Stream_target normal_target = resolve(erhe::dataformat::normal_attribute);
    const Stream_target smooth_target = resolve(erhe::dataformat::normal_attribute_smooth);
    if (!normal_target.valid && !smooth_target.valid) {
        return;
    }

    erhe::geometry::Geometry&              geometry   = *group.geometry;
    const erhe::geometry::Mesh_attributes& attributes = geometry.get_attributes();
    const GEO::Mesh&                       geo_mesh   = geometry.get_mesh();

    // Affected facets = facets incident to the moved vertices (each face whose shape
    // changed). Dedup with sort+unique on the persistent scratch vector.
    m_normal_scratch_facets.clear();
    for (const GEO::index_t vertex : group.vertices) {
        for (const GEO::index_t corner : geometry.get_vertex_corners(vertex)) {
            m_normal_scratch_facets.push_back(geometry.get_corner_facet(corner));
        }
    }
    std::sort(m_normal_scratch_facets.begin(), m_normal_scratch_facets.end());
    m_normal_scratch_facets.erase(std::unique(m_normal_scratch_facets.begin(), m_normal_scratch_facets.end()), m_normal_scratch_facets.end());

    const auto facet_normal = [&](const GEO::index_t facet) -> glm::vec3 {
        const GEO::vec3f n = erhe::geometry::mesh_facet_normalf(geo_mesh, facet);
        const glm::vec3  v{n.x, n.y, n.z};
        const float      len = glm::length(v);
        return (len > 1e-8f) ? (v / len) : glm::vec3{0.0f, 0.0f, 0.0f};
    };
    // Smooth vertex normal: normalize each incident facet normal, then sum and
    // normalize (matching compute_mesh_vertex_normal_smooth). Cached per vertex so a
    // vertex shared by several affected facets is computed once.
    m_normal_smooth_cache.clear();
    const auto smooth_normal = [&](const GEO::index_t vertex) -> glm::vec3 {
        const auto it = m_normal_smooth_cache.find(vertex);
        if (it != m_normal_smooth_cache.end()) {
            return it->second;
        }
        glm::vec3 sum{0.0f};
        for (const GEO::index_t corner : geometry.get_vertex_corners(vertex)) {
            sum += facet_normal(geometry.get_corner_facet(corner));
        }
        const float     len    = glm::length(sum);
        const glm::vec3 result = (len > 1e-8f) ? (sum / len) : glm::vec3{0.0f, 1.0f, 0.0f};
        m_normal_smooth_cache.emplace(vertex, result);
        return result;
    };

    const auto write_stream = [&](const Stream_target& target, const GEO::index_t corner, const glm::vec3& value) {
        if (!target.valid) {
            return;
        }
        if (corner >= element_mappings.mesh_corner_to_vertex_buffer_index.size()) {
            return;
        }
        const uint32_t    vertex_id   = element_mappings.mesh_corner_to_vertex_buffer_index[corner];
        const std::size_t byte_offset = target.base_offset + (static_cast<std::size_t>(vertex_id) * target.stride) + target.offset;

        std::vector<std::uint8_t> buffer;
        if (target.format == erhe::dataformat::Format::format_32_vec3_float) {
            buffer.resize(sizeof(float) * 3);
            auto* const ptr = reinterpret_cast<float*>(buffer.data());
            ptr[0] = value.x;
            ptr[1] = value.y;
            ptr[2] = value.z;
        } else if (target.format == erhe::dataformat::Format::format_32_vec4_float) {
            buffer.resize(sizeof(float) * 4);
            auto* const ptr = reinterpret_cast<float*>(buffer.data());
            ptr[0] = value.x;
            ptr[1] = value.y;
            ptr[2] = value.z;
            ptr[3] = 0.0f; // direction, w = 0
        } else {
            return;
        }
        const erhe::primitive::Buffer_range update_range{
            .count        = 1,
            .element_size = erhe::dataformat::get_format_size_bytes(target.format),
            .byte_offset  = byte_offset,
            .pool_id      = target.pool_id,
            .buffer_id    = target.buffer_id
        };
        mesh_memory.enqueue_vertex_data(update_range, std::move(buffer));
    };

    // Write per-corner normals for every affected facet. The content normal must match
    // exactly what the commit's make_renderable_mesh would select per corner, so there is
    // no shading pop on release. The commit recomputes facet normals, recomputes smooth
    // vertex normals, and collapses any PRESENT corner_normal / vertex_normal to the
    // smooth normal; make_renderable_mesh then picks per the mesh's normal style:
    //   polygon_normals -> flat facet normal.
    //   corner_normals  -> corner_normal (collapsed to smooth) if present, else facet.
    //   point_normals   -> vertex_normal (collapsed to smooth) if present, else facet.
    //   none            -> +Y.
    // So: use the smooth normal only where the geometry actually carries the matching
    // per-corner / per-vertex normal attribute; otherwise use the flat facet normal
    // (matching the fallback). New extrude faces carry no such attribute, so they render
    // flat - consistent with how the commit builds them.
    for (const GEO::index_t facet : m_normal_scratch_facets) {
        const glm::vec3 facet_n = facet_normal(facet);
        for (const GEO::index_t corner : geo_mesh.facets.corners(facet)) {
            const GEO::index_t vertex   = geo_mesh.facet_corners.vertex(corner);
            const glm::vec3    smooth_n = smooth_normal(vertex);
            glm::vec3          content_n;
            switch (normal_style) {
                case erhe::primitive::Normal_style::corner_normals: {
                    content_n = attributes.corner_normal.has(corner) ? smooth_n : facet_n;
                    break;
                }
                case erhe::primitive::Normal_style::point_normals: {
                    content_n = attributes.vertex_normal.has(vertex) ? smooth_n : facet_n;
                    break;
                }
                case erhe::primitive::Normal_style::polygon_normals: {
                    content_n = facet_n;
                    break;
                }
                case erhe::primitive::Normal_style::none:
                default: {
                    content_n = glm::vec3{0.0f, 1.0f, 0.0f};
                    break;
                }
            }
            write_stream(normal_target, corner, content_n);
            write_stream(smooth_target, corner, smooth_n);
        }
    }
}

auto Mesh_component_transform::is_geometry_shared(App_context&, const std::shared_ptr<erhe::scene::Mesh>& mesh, const erhe::geometry::Geometry* geometry) const -> bool
{
    if (!mesh || (geometry == nullptr)) {
        return false;
    }
    erhe::scene::Node* node = mesh->get_node();
    if (node == nullptr) {
        return false;
    }
    erhe::Item_host* item_host = node->get_item_host();
    if (item_host == nullptr) {
        return false;
    }
    Scene_root*               scene_root = static_cast<Scene_root*>(item_host);
    const erhe::scene::Scene& scene      = scene_root->get_scene();
    for (const std::shared_ptr<erhe::scene::Mesh_layer>& layer : scene.get_mesh_layers()) {
        for (const std::shared_ptr<erhe::scene::Mesh>& other : layer->meshes) {
            if (!other || (other == mesh)) {
                continue;
            }
            for (const erhe::scene::Mesh_primitive& mesh_primitive : other->get_primitives()) {
                const std::shared_ptr<erhe::primitive::Primitive>& primitive = mesh_primitive.primitive;
                if (primitive && primitive->render_shape && (primitive->render_shape->get_geometry().get() == geometry)) {
                    return true;
                }
            }
        }
    }
    return false;
}

void Mesh_component_transform::fork_group(App_context& context, Group& group)
{
    const std::shared_ptr<erhe::scene::Mesh> mesh = group.mesh.lock();
    if (!mesh) {
        return;
    }
    erhe::scene::Node* node = mesh->get_node();
    if (node == nullptr) {
        return;
    }
    const std::vector<erhe::scene::Mesh_primitive>& current_primitives = mesh->get_primitives();
    if ((group.primitive_index >= current_primitives.size()) || !current_primitives[group.primitive_index].primitive) {
        return;
    }
    const erhe::scene::Mesh_primitive& shared_mesh_primitive = current_primitives[group.primitive_index];
    if (!shared_mesh_primitive.primitive->render_shape) {
        return;
    }
    const erhe::primitive::Normal_style             normal_style = shared_mesh_primitive.primitive->render_shape->get_normal_style();
    const std::shared_ptr<erhe::geometry::Geometry> old_geometry = group.geometry;

    // Deep-copy the geometry (identity transform) so the fork is independent of the
    // shared original.
    std::shared_ptr<erhe::geometry::Geometry> fork_geometry = std::make_shared<erhe::geometry::Geometry>(old_geometry->get_name() + " (fork)");
    fork_geometry->copy_with_transform(*old_geometry, GEO::create_scaling_matrix(1.0f));

    const erhe::primitive::Build_info build_info{
        .primitive_types = {
            .fill_triangles  = true,
            .edge_lines      = true,
            .corner_points   = true,
            .centroid_points = true
        },
        .buffer_info = context.mesh_memory->make_primitive_buffer_info()
    };
    std::shared_ptr<erhe::primitive::Primitive> fork_primitive = std::make_shared<erhe::primitive::Primitive>(fork_geometry);
    const bool renderable_ok = fork_primitive->make_renderable_mesh(build_info, normal_style);
    const bool raytrace_ok   = fork_primitive->make_raytrace();
    ERHE_VERIFY(renderable_ok && raytrace_ok);

    // Record before/after Mesh_primitive for the commit's Fork_geometry_operation.
    group.fork_before          = shared_mesh_primitive; // shared primitive + material
    group.fork_after.primitive = fork_primitive;
    group.fork_after.material  = shared_mesh_primitive.material;

    // Swap the mesh's primitive to the fork via the node re-parent dance (no physics
    // change - the fork is a position-identical copy).
    std::vector<erhe::scene::Mesh_primitive> new_primitives = current_primitives;
    new_primitives[group.primitive_index] = group.fork_after;
    std::shared_ptr<erhe::Hierarchy>   parent      = node->get_parent().lock();
    std::shared_ptr<erhe::scene::Node> node_shared = std::dynamic_pointer_cast<erhe::scene::Node>(node->shared_from_this());
    node->set_parent(std::shared_ptr<erhe::Hierarchy>{});
    mesh->set_primitives(new_primitives);
    node->set_parent(parent);

    // Preserve the component selection across fork/un-fork: add an entry keyed on the
    // fork geometry copying the indices from the shared-geometry entry. Both entries
    // are retained; is_live() shows whichever matches the mesh's current geometry, so
    // the selection survives the fork and its undo.
    Mesh_component_selection* selection = context.mesh_component_selection;
    if (selection != nullptr) {
        Mesh_component_entry&       fork_entry = selection->find_or_create_entry(mesh, group.primitive_index, fork_geometry);
        const Mesh_component_entry* orig       = selection->find_entry(mesh, group.primitive_index, old_geometry);
        if (orig != nullptr) {
            fork_entry.vertices = orig->vertices;
            fork_entry.facets   = orig->facets;
            fork_entry.edges    = orig->edges;
        }
    }

    group.geometry = fork_geometry;
    group.forked   = true;
}

void Mesh_component_transform::extrude_group(App_context& context, Group& group)
{
    const std::shared_ptr<erhe::scene::Mesh> mesh = group.mesh.lock();
    if (!mesh) {
        return;
    }
    erhe::scene::Node* node = mesh->get_node();
    if (node == nullptr) {
        return;
    }
    const std::vector<erhe::scene::Mesh_primitive>& current_primitives = mesh->get_primitives();
    if ((group.primitive_index >= current_primitives.size()) || !current_primitives[group.primitive_index].primitive) {
        return;
    }
    const erhe::scene::Mesh_primitive& original_mesh_primitive = current_primitives[group.primitive_index];
    if (!original_mesh_primitive.primitive->render_shape) {
        return;
    }

    // Resolve the live selection entry (sets + mode) on the original geometry.
    Mesh_component_selection* selection = context.mesh_component_selection;
    if (selection == nullptr) {
        return;
    }
    const Mesh_component_entry* entry = selection->find_entry(mesh, group.primitive_index, group.geometry);
    if (entry == nullptr) {
        return;
    }
    const Mesh_component_mode mode = selection->get_mode();

    // Build the extruded copy (topology change). Original vertex/facet indices are
    // preserved; new duplicates/faces are appended. In a normal extrude mode the builder
    // also returns a per-moved-vertex normal (geometry-local) in move_directions.
    const Extrude_normal_mode normal_mode = to_extrude_normal_mode(m_transform_mode);
    Extrude_result extrude = extrude_mesh_components(*group.geometry, mode, entry->vertices, entry->edges, entry->facets, normal_mode);
    if (!extrude.is_valid()) {
        return;
    }

    const erhe::primitive::Normal_style normal_style = original_mesh_primitive.primitive->render_shape->get_normal_style();
    const erhe::primitive::Build_info   build_info{
        .primitive_types = {
            .fill_triangles  = true,
            .edge_lines      = true,
            .corner_points   = true,
            .centroid_points = true
        },
        .buffer_info = context.mesh_memory->make_primitive_buffer_info()
    };
    std::shared_ptr<erhe::primitive::Primitive> extrude_primitive = std::make_shared<erhe::primitive::Primitive>(extrude.geometry);
    const bool renderable_ok = extrude_primitive->make_renderable_mesh(build_info, normal_style);
    const bool raytrace_ok   = extrude_primitive->make_raytrace();
    ERHE_VERIFY(renderable_ok && raytrace_ok);

    // Record before/after Mesh_primitive for the commit's swap operation.
    group.extrude_before          = original_mesh_primitive;
    group.extrude_after.primitive = extrude_primitive;
    group.extrude_after.material  = original_mesh_primitive.material;

    // Swap the mesh's primitive to the extruded copy via the node re-parent dance
    // (hold node_shared so set_parent(nullptr) cannot drop the node mid-swap).
    std::vector<erhe::scene::Mesh_primitive> new_primitives = current_primitives;
    new_primitives[group.primitive_index] = group.extrude_after;
    std::shared_ptr<erhe::Hierarchy>   parent      = node->get_parent().lock();
    std::shared_ptr<erhe::scene::Node> node_shared = std::dynamic_pointer_cast<erhe::scene::Node>(node->shared_from_this());
    node->set_parent(std::shared_ptr<erhe::Hierarchy>{});
    mesh->set_primitives(new_primitives);
    node->set_parent(parent);

    // Redirect the component selection onto the extruded geometry, carrying the
    // post-extrude selection sets (the moved duplicates / re-pointed facets). The
    // original-geometry entry goes dormant; is_live() follows the mesh's current
    // geometry, so the selection survives the extrude and its undo.
    Mesh_component_entry& extrude_entry = selection->find_or_create_entry(mesh, group.primitive_index, extrude.geometry);
    extrude_entry.vertices = extrude.selection_vertices;
    extrude_entry.edges    = extrude.selection_edges;
    extrude_entry.facets   = extrude.selection_facets;

    // Redirect the group to move the extruded (duplicate + interior) vertices. They
    // start coincident with their originals; capture that as the move's start state.
    group.geometry = extrude.geometry;
    group.vertices = std::move(extrude.moved_vertices);
    const GEO::Mesh& geo_mesh = group.geometry->get_mesh();
    group.before_local.clear();
    group.before_local.reserve(group.vertices.size());
    for (const GEO::index_t vertex : group.vertices) {
        const GEO::vec3f p = get_pointf(geo_mesh.vertices, vertex);
        group.before_local.push_back(glm::vec3{p.x, p.y, p.z});
    }

    // Normal modes: transform the per-vertex normals from geometry-local to world space
    // (direction transform = inverse-transpose of the node basis) so apply() can slide each
    // vertex along its own normal regardless of node rotation/scale. Parallel to
    // group.vertices. Left empty for plain extrude, which uses the gizmo delta.
    group.move_directions.clear();
    if ((normal_mode != Extrude_normal_mode::none) && (extrude.move_directions.size() == group.vertices.size())) {
        const glm::mat3 node_basis    = glm::mat3{group.world_from_node};
        const glm::mat3 normal_matrix = glm::transpose(glm::inverse(node_basis));
        group.move_directions.reserve(group.vertices.size());
        for (const GEO::vec3f& local_direction : extrude.move_directions) {
            glm::vec3   world_direction = normal_matrix * glm::vec3{local_direction.x, local_direction.y, local_direction.z};
            const float length          = glm::length(world_direction);
            world_direction = (length > 1e-6f) ? (world_direction / length) : glm::vec3{0.0f, 1.0f, 0.0f};
            group.move_directions.push_back(world_direction);
        }
    }

    group.extruded = true;
}

}
