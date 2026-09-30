#include "transform/mesh_component_transform.hpp"
#include "transform/mesh_component_extrude.hpp"
#include "transform/transform_tool.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/mesh_transform_mode.hpp"
#include "operations/async_raytrace_kickoff_operation.hpp"
#include "operations/compound_operation.hpp"
#include "operations/fork_geometry_operation.hpp"
#include "operations/mesh_primitive_swap.hpp"
#include "operations/move_mesh_vertices_operation.hpp"
#include "operations/operation.hpp"
#include "operations/operation_stack.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_view.hpp"
#include "tools/mesh_component_selection.hpp"

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/device.hpp"
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
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>

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
        const erhe::scene::Node* node = mesh.get();
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
                    const std::span<const GEO::index_t> facets = geometry->get_edge_facets(edge);
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

// Writes re-sampled corner texcoords into the geometry (the after state of
// each change).
void apply_corner_texcoords(erhe::geometry::Geometry& geometry, const std::vector<Corner_texcoord_change>& changes)
{
    erhe::geometry::Mesh_attributes& attributes = geometry.get_attributes();
    for (const Corner_texcoord_change& change : changes) {
        attributes.corner_texcoord(change.set).set(change.corner, GEO::vec2f{change.after.x, change.after.y});
    }
}

} // anonymous namespace

// Buffer_info decides the vertex format (it is NOT derived from the attributes
// at build time), so the plain make_primitive_buffer_info would rebuild a
// skinned mesh into the non-skinned format, silently dropping joints/weights
// from the GPU streams - the mesh would stop deforming after a vertex edit,
// fork or extrude. Same rule as Weight_paint_tool::end_stroke.
auto make_rebuild_build_info(
    erhe::scene_renderer::Mesh_memory& mesh_memory,
    const erhe::geometry::Geometry&    geometry
) -> erhe::primitive::Build_info
{
    const GEO::AttributesManager& vertex_attrs = geometry.get_mesh().vertices.attributes();
    const bool skinned =
        vertex_attrs.is_defined(erhe::geometry::c_joint_indices_0) &&
        vertex_attrs.is_defined(erhe::geometry::c_joint_weights_0);
    return erhe::primitive::Build_info{
        .primitive_types = {
            .fill_triangles          = true,
            .fill_triangles_expanded = true,
            .edge_lines              = true,
            .corner_points           = true,
            .centroid_points         = true
        },
        .buffer_info = skinned
            ? mesh_memory.make_skinned_primitive_buffer_info()
            : mesh_memory.make_primitive_buffer_info()
    };
}

auto Mesh_component_transform::gather(App_context& context, const Scalar_topology_step* const topology_step) -> bool
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
        if ((topology_step != nullptr) && ((mesh != topology_step->mesh) || (primitive_index != topology_step->primitive_index))) {
            continue;
        }
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
        const erhe::scene::Node* node = mesh.get();
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
            const erhe::scene::Node* node = first_mesh.get();
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
    // The slide modes take the scalar path: apply() maps the gizmo translation
    // to the slide factor.
    m_scalar = (m_transform_mode == Mesh_transform_mode::edge_slide) ||
               (m_transform_mode == Mesh_transform_mode::vertex_slide);
    if (m_scalar) {
        m_scalar_kind = (m_transform_mode == Mesh_transform_mode::edge_slide)
            ? Scalar_edit_kind::edge_slide
            : Scalar_edit_kind::vertex_slide;
        if (!build_scalar(context, m_scalar_kind)) {
            m_scalar = false;
            m_active = false;
            m_groups.clear();
            return;
        }
    }
    capture_start();
}

auto Mesh_component_transform::begin_scalar(
    App_context&                      context,
    const Scalar_edit_kind            kind,
    const Scalar_topology_step* const topology_step
) -> bool
{
    if (m_active) {
        return false;
    }
    if (!gather(context, topology_step)) {
        return false;
    }
    m_transform_mode = (kind == Scalar_edit_kind::edge_slide) ? Mesh_transform_mode::edge_slide : Mesh_transform_mode::vertex_slide;
    m_extrude        = false;
    m_scalar         = true;
    m_scalar_kind    = kind;
    if (!build_scalar(context, kind)) {
        m_scalar = false;
        m_groups.clear();
        return false;
    }
    capture_start();

    // A topology step the caller already swapped in: the group runs on the
    // step's primitive, and commit / cancel treat it as an extruded group
    // (one primitive swap from the step's before primitive).
    m_has_topology_step = (topology_step != nullptr);
    m_topology_description.clear();
    if (topology_step != nullptr) {
        m_topology_description = topology_step->description;
        m_topology_mode_before = topology_step->mode_before;
        for (Group& group : m_groups) {
            group.extruded       = true;
            group.extrude_before = topology_step->before;
            group.extrude_after  = topology_step->after;
        }
    }
    return m_active;
}

void Mesh_component_transform::capture_start()
{
    for (Group& group : m_groups) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = group.mesh.lock();
        erhe::scene::Node* const                 node = mesh ? mesh.get() : nullptr;
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

    // Requirement 11: the edit starts here, so the optimized variant goes now -
    // not at the first GPU write - and an optimization hold keeps any
    // concurrently finishing build from publishing a pre-edit variant
    // mid-drag. commit() releases the holds; the commit operations' primitive
    // rebuild is what brings the variant back.
    for (Group& group : m_groups) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = group.mesh.lock();
        if (mesh) {
            group.held_primitive = mesh->begin_optimized_variant_edit(group.primitive_index);
        }
    }
}

void Mesh_component_transform::apply(App_context& context, Transform_tool_shared& shared, const glm::mat4& updated_world_from_anchor)
{
    if (!m_active) {
        return;
    }
    const glm::mat4 world_delta = updated_world_from_anchor * shared.world_from_anchor_initial_state.get_inverse_matrix();
    const bool      moved       = is_nontrivial_delta(world_delta);

    // Slide transform modes: the gizmo translation drives the scalar path (the
    // rotation and scale of the gizmo delta do not apply to a slide).
    if (m_scalar) {
        const glm::vec3 translation{world_delta[3]};
        Scalar_input    input{};
        input.factor = moved ? factor_from_translation(translation) : 0.0f;
        apply_scalar(context, input);
        return;
    }

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
        if (m_extrude) {
            for (Group& group : m_groups) {
                const std::shared_ptr<erhe::scene::Mesh> mesh = group.mesh.lock();
                if (!mesh || !group.geometry) {
                    continue;
                }
                if (group.before_local.size() != group.vertices.size()) {
                    continue;
                }
                if (!group.extruded) {
                    extrude_group(context, group);
                }
            }
        } else {
            fork_shared_groups(context);
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
            write_vertex(context, group, vertex, local_after);
        }

        // Refresh the involved faces' normals from the new positions so shading is valid
        // live (and so extrude's brand-new faces get valid normals at all). Only when the
        // gizmo actually moved - an identity delta leaves positions and normals unchanged.
        if (moved) {
            update_group_normals(context, group, Normal_source::live_positions);
        }
    }
}

void Mesh_component_transform::commit(App_context& context)
{
    if (!m_active) {
        return;
    }
    m_active = false;
    const bool scalar = m_scalar;
    m_scalar = false;
    const bool        topology_step        = m_has_topology_step;
    const std::string topology_description = m_topology_description;
    m_has_topology_step = false;

    // Release the optimization holds begin() took (transferred by fork /
    // extrude), one per group, before the commit operations run - their
    // rebuild swaps in a fresh Primitive whose optimized variant must be
    // publishable.
    for (Group& group : m_groups) {
        if (group.held_primitive) {
            group.held_primitive->release_optimization_hold();
            group.held_primitive.reset();
        }
    }

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

        // Per group: the vertex format follows this group's geometry (skinned
        // meshes keep their joint attributes through the rebuild).
        const erhe::primitive::Build_info build_info = make_rebuild_build_info(*context.mesh_memory, *group.geometry);

        // Extruded group: the topology changed, so an in-place vertex move can't be
        // undone back to the original. Finalize normals from the now-final positions,
        // rebuild a clean primitive, and queue a primitive swap (before = original
        // geometry, after = extruded geometry) - undo removes the extrusion entirely.
        if (group.extruded) {
            if (topology_step) {
                // A slide after a topology step (loop cut): the corner
                // texcoords around the slid vertices are re-sampled into the
                // geometry itself, and the normals follow the final positions
                // only when something moved (an unmoved cut keeps the normals
                // the step interpolated).
                std::vector<Corner_texcoord_change> corner_texcoords;
                collect_corrected_texcoords(group, corner_texcoords);
                apply_corner_texcoords(*group.geometry, corner_texcoords);
                if (has_moved_vertex(group)) {
                    finalize_extrude_normals(*group.geometry);
                }
            } else {
                finalize_extrude_normals(*group.geometry);
            }
            std::shared_ptr<erhe::primitive::Primitive> after_primitive = std::make_shared<erhe::primitive::Primitive>(group.geometry);
            const bool renderable_ok = after_primitive->make_renderable_mesh(build_info, primitive.render_shape->get_normal_style());
            const bool raytrace_ok   = after_primitive->make_raytrace();
            ERHE_VERIFY(renderable_ok && raytrace_ok);
            group.extrude_after.primitive = after_primitive;

            const std::string description =
                topology_step                                                    ? topology_description                    :
                (m_transform_mode == Mesh_transform_mode::extrude_group_normal)  ? std::string{"Extrude (Group Normal)"}  :
                (m_transform_mode == Mesh_transform_mode::extrude_vertex_normal) ? std::string{"Extrude (Vertex Normal)"} :
                                                                                   std::string{"Extrude"};
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
        // still records its Fork op above). A slide also carries the corner texcoords
        // re-interpolated at the slid positions (doc/editor/transform.md "Scalar edits").
        if (after_local != group.before_local) {
            std::vector<Corner_texcoord_change> corner_texcoords;
            std::string                         description;
            if (scalar) {
                collect_corrected_texcoords(group, corner_texcoords);
                description = (m_scalar_kind == Scalar_edit_kind::edge_slide) ? "Edge Slide" : "Vertex Slide";
            }
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
                        .normal_style     = primitive.render_shape->get_normal_style(),
                        .corner_texcoords = std::move(corner_texcoords),
                        .description      = std::move(description)
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

    // The optimized variant was dropped when begin() took the edit's
    // optimization hold; while the hold is live no variant can be published,
    // so this write always lands beside a base-only primitive.

    // GPU vertex edits always address the per-corner original buffer: the
    // element mappings only describe that variant, and an optimized variant is
    // invalidated by the edit rather than written through.
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
            // The base variant's position is float3 by construction (position
            // quantization applies only to the optimized variant, which this
            // edit dropped above), so any dragged position is representable -
            // no AABB clamp, ever.
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

void Mesh_component_transform::update_group_normals(App_context& context, Group& group, const Normal_source source)
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
    // Cancel: write exactly what the primitive builder wrote from the geometry's
    // normal attributes (which the drag never changes), so the restored buffers
    // match the build (Build_context::build_tangent_frame / build_vertex_normal).
    if (source == Normal_source::stored_attributes) {
        const auto to_glm = [](const GEO::vec3f& v) -> glm::vec3 { return glm::vec3{v.x, v.y, v.z}; };
        for (const GEO::index_t facet : m_normal_scratch_facets) {
            const std::optional<GEO::vec3f> stored_facet_n = attributes.facet_normal.try_get(facet);
            const glm::vec3                 facet_n        = stored_facet_n.has_value() ? to_glm(stored_facet_n.value()) : facet_normal(facet);
            for (const GEO::index_t corner : geo_mesh.facets.corners(facet)) {
                const GEO::index_t              vertex        = geo_mesh.facet_corners.vertex(corner);
                const std::optional<GEO::vec3f> corner_n      = attributes.corner_normal.try_get(corner);
                const std::optional<GEO::vec3f> vertex_n      = attributes.vertex_normal.try_get(vertex);
                const std::optional<GEO::vec3f> vertex_smooth = attributes.vertex_normal_smooth.try_get(vertex);
                glm::vec3 content_n{0.0f, 1.0f, 0.0f};
                switch (normal_style) {
                    case erhe::primitive::Normal_style::corner_normals: {
                        content_n =
                            corner_n.has_value()       ? to_glm(corner_n.value()) :
                            stored_facet_n.has_value() ? facet_n                  :
                            vertex_n.has_value()       ? to_glm(vertex_n.value()) : facet_n;
                        break;
                    }
                    case erhe::primitive::Normal_style::point_normals: {
                        content_n = vertex_n.has_value() ? to_glm(vertex_n.value()) : facet_n;
                        break;
                    }
                    case erhe::primitive::Normal_style::polygon_normals: {
                        content_n = facet_n;
                        break;
                    }
                    case erhe::primitive::Normal_style::none:
                    default: {
                        break;
                    }
                }
                const glm::vec3 smooth_n = vertex_smooth.has_value() ? to_glm(vertex_smooth.value()) : glm::vec3{0.0f, 1.0f, 0.0f};
                write_stream(normal_target, corner, content_n);
                write_stream(smooth_target, corner, smooth_n);
            }
        }
        return;
    }

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
    erhe::scene::Node* node = mesh.get();
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
                // Non-blocking: pointer identity only. get_geometry() here
                // would force synchronous construction of every not-yet-built
                // shape in the scene.
                if (primitive && primitive->render_shape && (primitive->render_shape->get_geometry_const().get() == geometry)) {
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

    const erhe::primitive::Build_info build_info = make_rebuild_build_info(*context.mesh_memory, *fork_geometry);
    std::shared_ptr<erhe::primitive::Primitive> fork_primitive = std::make_shared<erhe::primitive::Primitive>(fork_geometry);
    const bool renderable_ok = fork_primitive->make_renderable_mesh(build_info, normal_style);
    const bool raytrace_ok   = fork_primitive->make_raytrace();
    ERHE_VERIFY(renderable_ok && raytrace_ok);

    // Record before/after Mesh_primitive for the commit's Fork_geometry_operation.
    group.fork_before          = shared_mesh_primitive; // shared primitive + material
    group.fork_after.primitive = fork_primitive;
    group.fork_after.material  = shared_mesh_primitive.material;

    // Swap the mesh's primitive to the fork in place (no physics change - the
    // fork is a position-identical copy).
    std::vector<erhe::scene::Mesh_primitive> new_primitives = current_primitives;
    new_primitives[group.primitive_index] = group.fork_after;
    swap_mesh_primitives(mesh, new_primitives);

    // Transfer the drag's optimization hold to the fork: release the object
    // begin() bracketed, bracket the swapped-in one (dropping its freshly
    // built variant too). commit() then releases the fork's hold.
    if (group.held_primitive) {
        group.held_primitive->release_optimization_hold();
    }
    group.held_primitive = mesh->begin_optimized_variant_edit(group.primitive_index);

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
    const erhe::primitive::Build_info   build_info   = make_rebuild_build_info(*context.mesh_memory, *extrude.geometry);
    std::shared_ptr<erhe::primitive::Primitive> extrude_primitive = std::make_shared<erhe::primitive::Primitive>(extrude.geometry);
    const bool renderable_ok = extrude_primitive->make_renderable_mesh(build_info, normal_style);
    const bool raytrace_ok   = extrude_primitive->make_raytrace();
    ERHE_VERIFY(renderable_ok && raytrace_ok);

    // Record before/after Mesh_primitive for the commit's swap operation.
    group.extrude_before          = original_mesh_primitive;
    group.extrude_after.primitive = extrude_primitive;
    group.extrude_after.material  = original_mesh_primitive.material;

    // Swap the mesh's primitive to the extruded copy in place.
    std::vector<erhe::scene::Mesh_primitive> new_primitives = current_primitives;
    new_primitives[group.primitive_index] = group.extrude_after;
    swap_mesh_primitives(mesh, new_primitives);

    // Transfer the drag's optimization hold to the extruded copy - same
    // reasoning as fork_group() above.
    if (group.held_primitive) {
        group.held_primitive->release_optimization_hold();
    }
    group.held_primitive = mesh->begin_optimized_variant_edit(group.primitive_index);

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

auto c_str(const Scalar_edit_kind kind) -> const char*
{
    switch (kind) {
        case Scalar_edit_kind::edge_slide:   return "edge_slide";
        case Scalar_edit_kind::vertex_slide: return "vertex_slide";
        default:                             return "?";
    }
}

namespace {

[[nodiscard]] auto position_of(const GEO::Mesh& mesh, const GEO::index_t vertex) -> glm::vec3
{
    const GEO::vec3f p = get_pointf(mesh.vertices, vertex);
    return glm::vec3{p.x, p.y, p.z};
}

[[nodiscard]] auto transform_point(const glm::mat4& m, const glm::vec3& p) -> glm::vec3
{
    return glm::vec3{m * glm::vec4{p, 1.0f}};
}

// The far vertex of `facet`'s other edge at `vertex`, given the near vertex
// `from` of one of the facet's two edges at `vertex`; GEO::NO_INDEX when
// `from` is not next to `vertex` in the facet.
[[nodiscard]] auto facet_other_neighbour(const GEO::Mesh& mesh, const GEO::index_t facet, const GEO::index_t vertex, const GEO::index_t from) -> GEO::index_t
{
    const GEO::index_t corner_begin = mesh.facets.corners_begin(facet);
    const GEO::index_t corner_count = mesh.facets.corners_end(facet) - corner_begin;
    for (GEO::index_t local = 0; local < corner_count; ++local) {
        if (mesh.facet_corners.vertex(corner_begin + local) != vertex) {
            continue;
        }
        const GEO::index_t next = mesh.facet_corners.vertex(corner_begin + ((local + 1) % corner_count));
        const GEO::index_t prev = mesh.facet_corners.vertex(corner_begin + ((local + corner_count - 1) % corner_count));
        if (next == from) {
            return prev;
        }
        if (prev == from) {
            return next;
        }
    }
    return GEO::NO_INDEX;
}

// The facet across the manifold edge (v0, v1) from `facet`; GEO::NO_INDEX for
// a boundary edge.
[[nodiscard]] auto facet_across(const erhe::geometry::Geometry& geometry, const GEO::index_t v0, const GEO::index_t v1, const GEO::index_t facet) -> GEO::index_t
{
    const GEO::index_t edge = geometry.get_edge(v0, v1);
    if (edge == GEO::NO_EDGE) {
        return GEO::NO_INDEX;
    }
    const std::span<const GEO::index_t> facets = geometry.get_edge_facets(edge);
    if (facets.size() != 2) {
        return GEO::NO_INDEX;
    }
    if (facets[0] == facet) {
        return facets[1];
    }
    if (facets[1] == facet) {
        return facets[0];
    }
    return GEO::NO_INDEX;
}

// Edge slide side consistency (doc/plans/mesh_modeling.md section 4.6): which
// side of the previous loop edge `facet` belongs to. `facet` holds the loop
// edge (vertex, from); the walk goes around `vertex` through the fan of facets
// away from that edge (never crossing the previous loop edge, whose facets are
// previous_facets) until it reaches a facet already assigned to a side. The
// first step covers "same facet", the second "same rail target". -1 when the
// fan ends at a boundary first.
[[nodiscard]] auto resolve_fan_side(
    const erhe::geometry::Geometry& geometry,
    const GEO::Mesh&                mesh,
    const GEO::index_t              facet,
    const GEO::index_t              vertex,
    const GEO::index_t              from,
    const GEO::index_t              previous_vertex,
    const GEO::index_t* const       previous_facets
) -> int
{
    GEO::index_t current = facet;
    GEO::index_t near_vertex = from;
    for (int guard = 0; guard < 256; ++guard) {
        if (current == previous_facets[0]) {
            return 0;
        }
        if (current == previous_facets[1]) {
            return 1;
        }
        const GEO::index_t far_vertex = facet_other_neighbour(mesh, current, vertex, near_vertex);
        if ((far_vertex == GEO::NO_INDEX) || (far_vertex == previous_vertex)) {
            return -1;
        }
        const GEO::index_t next = facet_across(geometry, vertex, far_vertex, current);
        if ((next == GEO::NO_INDEX) || (next == facet)) {
            return -1;
        }
        current = next;
        near_vertex = far_vertex;
    }
    return -1;
}

[[nodiscard]] auto facet_newell_normal(const GEO::Mesh& mesh, const GEO::index_t facet) -> glm::vec3
{
    glm::vec3          normal{0.0f};
    const GEO::index_t corner_begin = mesh.facets.corners_begin(facet);
    const GEO::index_t corner_count = mesh.facets.corners_end(facet) - corner_begin;
    for (GEO::index_t local = 0; local < corner_count; ++local) {
        const glm::vec3 a = position_of(mesh, mesh.facet_corners.vertex(corner_begin + local));
        const glm::vec3 b = position_of(mesh, mesh.facet_corners.vertex(corner_begin + ((local + 1) % corner_count)));
        normal += glm::cross(a, b);
    }
    return normal;
}

// Section 4.6: an interior loop vertex of valence 2 has no rail. It slides
// across its facet on that side: to the opposite corner of a quad, or along
// the facet's in-plane direction perpendicular to the loop to where that ray
// leaves the facet (n-gon; the facet centroid when no edge is hit).
[[nodiscard]] auto valence_two_destination(
    const GEO::Mesh&   mesh,
    const GEO::index_t facet,
    const GEO::index_t vertex,
    const GEO::index_t previous_vertex,
    const GEO::index_t next_vertex
) -> glm::vec3
{
    const GEO::index_t corner_begin = mesh.facets.corners_begin(facet);
    const GEO::index_t corner_count = mesh.facets.corners_end(facet) - corner_begin;
    const glm::vec3    p            = position_of(mesh, vertex);
    glm::vec3          centroid{0.0f};
    GEO::index_t       vertex_local = 0;
    for (GEO::index_t local = 0; local < corner_count; ++local) {
        const GEO::index_t corner_vertex = mesh.facet_corners.vertex(corner_begin + local);
        centroid += position_of(mesh, corner_vertex);
        if (corner_vertex == vertex) {
            vertex_local = local;
        }
    }
    centroid /= static_cast<float>(corner_count);
    if (corner_count == 4) {
        return position_of(mesh, mesh.facet_corners.vertex(corner_begin + ((vertex_local + 2) % 4)));
    }

    const glm::vec3 normal  = facet_newell_normal(mesh, facet);
    const glm::vec3 tangent = position_of(mesh, next_vertex) - position_of(mesh, previous_vertex);
    glm::vec3       direction = glm::cross(normal, tangent);
    if (glm::dot(direction, direction) < 1e-20f) {
        return centroid;
    }
    if (glm::dot(centroid - p, direction) < 0.0f) {
        direction = -direction;
    }
    direction = glm::normalize(direction);
    float best = std::numeric_limits<float>::max();
    for (GEO::index_t local = 0; local < corner_count; ++local) {
        const GEO::index_t va = mesh.facet_corners.vertex(corner_begin + local);
        const GEO::index_t vb = mesh.facet_corners.vertex(corner_begin + ((local + 1) % corner_count));
        if ((va == vertex) || (vb == vertex)) {
            continue;
        }
        const glm::vec3 a     = position_of(mesh, va);
        const glm::vec3 e     = position_of(mesh, vb) - a;
        const float     denom = glm::dot(glm::cross(direction, e), normal);
        if (std::abs(denom) < 1e-20f) {
            continue;
        }
        const float s = glm::dot(glm::cross(a - p, e), normal) / denom;
        const float r = glm::dot(glm::cross(a - p, direction), normal) / denom;
        if ((s > 1e-6f) && (r >= -1e-4f) && (r <= 1.0f + 1e-4f) && (s < best)) {
            best = s;
        }
    }
    return (best < std::numeric_limits<float>::max()) ? (p + (best * direction)) : centroid;
}

// Section 4.6: where the loop turns (the two loop edges' facets on one side
// give different rails), the destination is the corner of the slid loop: the
// intersection of the line through the previous rail's end parallel to the
// previous loop edge and the line through the next rail's end parallel to the
// next loop edge, when it lies inside the cone the two rails span; otherwise
// the midpoint of the two rail ends.
[[nodiscard]] auto turn_destination(
    const glm::vec3& p,
    const glm::vec3& previous_p,
    const glm::vec3& next_p,
    const glm::vec3& previous_rail_end,
    const glm::vec3& next_rail_end
) -> glm::vec3
{
    const glm::vec3 midpoint = 0.5f * (previous_rail_end + next_rail_end);
    const glm::vec3 d1 = p - previous_p;
    const glm::vec3 d2 = next_p - p;
    const glm::vec3 r  = previous_rail_end - next_rail_end;
    const float     a  = glm::dot(d1, d1);
    const float     b  = glm::dot(d1, d2);
    const float     e  = glm::dot(d2, d2);
    const float     c  = glm::dot(d1, r);
    const float     f  = glm::dot(d2, r);
    const float     denominator = (a * e) - (b * b);
    if (denominator <= (1e-8f * a * e)) {
        return midpoint; // parallel loop edges
    }
    const float     s = ((b * f) - (c * e)) / denominator;
    const float     t = ((a * f) - (b * c)) / denominator;
    const glm::vec3 x = 0.5f * ((previous_rail_end + (s * d1)) + (next_rail_end + (t * d2)));

    // x - p = alpha * rail_a + beta * rail_b with alpha, beta >= 0.
    const glm::vec3 rail_a = previous_rail_end - p;
    const glm::vec3 rail_b = next_rail_end - p;
    const glm::vec3 q      = x - p;
    const float     aa     = glm::dot(rail_a, rail_a);
    const float     ab     = glm::dot(rail_a, rail_b);
    const float     bb     = glm::dot(rail_b, rail_b);
    const float     det    = (aa * bb) - (ab * ab);
    if (std::abs(det) <= (1e-8f * aa * bb)) {
        return midpoint;
    }
    const float qa    = glm::dot(q, rail_a);
    const float qb    = glm::dot(q, rail_b);
    const float alpha = ((qa * bb) - (qb * ab)) / det;
    const float beta  = ((qb * aa) - (qa * ab)) / det;
    return ((alpha >= -1e-4f) && (beta >= -1e-4f)) ? x : midpoint;
}

// Mean value coordinates (Floater) of x in a planar polygon; false when the
// polygon is degenerate. A point on a corner or an edge gets the exact corner
// or linear edge weights.
[[nodiscard]] auto mean_value_weights(const std::vector<glm::vec2>& polygon, const glm::vec2 x, std::vector<float>& weights) -> bool
{
    const std::size_t n = polygon.size();
    weights.assign(n, 0.0f);
    if (n < 3) {
        return false;
    }
    for (std::size_t j = 0; j < n; ++j) {
        const glm::vec2 s = polygon[j] - x;
        if (glm::dot(s, s) < 1e-14f) {
            weights[j] = 1.0f;
            return true;
        }
    }
    const auto cross2 = [](const glm::vec2 a, const glm::vec2 b) -> float { return (a.x * b.y) - (a.y * b.x); };
    for (std::size_t j = 0; j < n; ++j) {
        const std::size_t k  = (j + 1) % n;
        const glm::vec2   sj = polygon[j] - x;
        const glm::vec2   sk = polygon[k] - x;
        const float       rj = glm::length(sj);
        const float       rk = glm::length(sk);
        if ((std::abs(cross2(sj, sk)) <= (1e-6f * rj * rk)) && (glm::dot(sj, sk) < 0.0f)) {
            weights[j] = rk / (rj + rk);
            weights[k] = rj / (rj + rk);
            return true;
        }
    }
    // tan(alpha_j / 2) of the angle at x spanned by corners j and j + 1.
    const auto half_angle_tangent = [&](const std::size_t j) -> float {
        const std::size_t k     = (j + 1) % n;
        const glm::vec2   sj    = polygon[j] - x;
        const glm::vec2   sk    = polygon[k] - x;
        const float       cross = cross2(sj, sk);
        const float       rr    = glm::length(sj) * glm::length(sk);
        if (std::abs(cross) <= (1e-9f * rr)) {
            return 0.0f;
        }
        return (rr - glm::dot(sj, sk)) / cross;
    };
    float sum = 0.0f;
    for (std::size_t j = 0; j < n; ++j) {
        const float t_before = half_angle_tangent((j + n - 1) % n);
        const float t_after  = half_angle_tangent(j);
        weights[j] = (t_before + t_after) / glm::length(polygon[j] - x);
        sum += weights[j];
    }
    if (std::abs(sum) < 1e-12f) {
        return false;
    }
    for (float& weight : weights) {
        weight /= sum;
    }
    return true;
}

[[nodiscard]] auto project_point(const Viewport_scene_view& view, const glm::vec3& position_in_world) -> std::optional<glm::vec2>
{
    const std::optional<glm::vec3> projected = view.project_to_viewport(position_in_world);
    if (!projected.has_value()) {
        return std::nullopt;
    }
    return glm::vec2{projected.value()};
}

constexpr float c_rail_epsilon = 1e-12f;

} // anonymous namespace

void Mesh_component_transform::write_vertex(App_context& context, const Group& group, const GEO::index_t vertex, const glm::vec3& local_position)
{
    GEO::Mesh& geo_mesh = group.geometry->get_mesh();
    set_pointf(geo_mesh.vertices, vertex, GEO::vec3f{local_position.x, local_position.y, local_position.z});
    enqueue_gpu_position(context, group, vertex, local_position);
    enqueue_gpu_edge_line_positions(context, group, vertex, local_position);
}

void Mesh_component_transform::fork_shared_groups(App_context& context)
{
    // Fork-on-first-move: if fork mode is on and a group's geometry is shared by
    // another mesh, deep-copy the geometry onto a new primitive for THIS mesh only -
    // BEFORE touching any positions - so the other instances never move.
    const bool fork_mode =
        (context.editor_settings != nullptr) &&
        (context.editor_settings->geometry_edit_mode == Geometry_edit_mode::fork);
    if (!fork_mode) {
        return;
    }
    for (Group& group : m_groups) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = group.mesh.lock();
        if (!mesh || !group.geometry) {
            continue;
        }
        if (group.before_local.size() != group.vertices.size()) {
            continue;
        }
        if (!group.forked && is_geometry_shared(context, mesh, group.geometry.get())) {
            fork_group(context, group);
        }
    }
}

auto Mesh_component_transform::build_scalar(App_context& context, const Scalar_edit_kind kind) -> bool
{
    m_slide_vertices.clear();
    m_slide_neighbours.clear();
    m_slide_loop_swapped.clear();
    m_slide_active            = 0;
    m_slide_last_side         = 0;
    m_slide_neighbours_picked = false;
    for (std::size_t group_index = 0, end = m_groups.size(); group_index < end; ++group_index) {
        const bool ok = (kind == Scalar_edit_kind::edge_slide)
            ? build_edge_slide  (context, group_index)
            : build_vertex_slide(context, group_index);
        if (!ok) {
            m_slide_vertices.clear();
            m_slide_neighbours.clear();
            m_slide_loop_swapped.clear();
            return false;
        }
    }
    if (m_slide_vertices.empty()) {
        log_trs_tool->info("{}: nothing to slide", c_str(kind));
        return false;
    }
    if (kind == Scalar_edit_kind::edge_slide) {
        align_slide_loops_world();
    }
    return true;
}

auto Mesh_component_transform::build_edge_slide(App_context& context, const std::size_t group_index) -> bool
{
    Group&                                   group     = m_groups[group_index];
    const std::shared_ptr<erhe::scene::Mesh> mesh      = group.mesh.lock();
    Mesh_component_selection*                selection = context.mesh_component_selection;
    if (!mesh || (selection == nullptr) || !group.geometry) {
        return false;
    }
    const Mesh_component_entry* entry = selection->find_entry(mesh, group.primitive_index, group.geometry);
    if (entry == nullptr) {
        return false;
    }
    const erhe::geometry::Geometry& geometry = *group.geometry;
    if (!geometry.has_connectivity() || !geometry.has_edge_connectivity()) {
        log_trs_tool->info("Edge slide: '{}' has no edge connectivity", mesh->get_name());
        return false;
    }
    const GEO::Mesh& geo_mesh        = geometry.get_mesh();
    const glm::mat4  world_from_node = mesh->world_from_node();

    // Every selected edge must be manifold or boundary.
    m_slide_edge_pairs.clear();
    for (const Mesh_edge_key& key : entry->edges) {
        const GEO::index_t edge = geometry.get_edge(key.first, key.second);
        if (edge == GEO::NO_EDGE) {
            return false;
        }
        const std::size_t facet_count = geometry.get_edge_facets(edge).size();
        if ((facet_count < 1) || (facet_count > 2)) {
            log_trs_tool->info("Edge slide refused: edge {}-{} of '{}' has {} facets", key.first, key.second, mesh->get_name(), facet_count);
            return false;
        }
        m_slide_edge_pairs.emplace_back(key.first, key.second);
        m_slide_edge_pairs.emplace_back(key.second, key.first);
    }
    group.vertices.clear();
    group.slide_offset = m_slide_vertices.size();
    if (m_slide_edge_pairs.empty()) {
        return true; // this mesh has no selected edges; it takes no part in the slide
    }
    std::sort(m_slide_edge_pairs.begin(), m_slide_edge_pairs.end());

    // Every selected vertex must be on one or two selected edges.
    m_slide_unique_vertices.clear();
    for (const std::pair<GEO::index_t, GEO::index_t>& pair : m_slide_edge_pairs) {
        if (m_slide_unique_vertices.empty() || (m_slide_unique_vertices.back() != pair.first)) {
            m_slide_unique_vertices.push_back(pair.first);
        }
    }
    const auto selected_neighbours = [this](const GEO::index_t vertex, GEO::index_t out[2]) -> std::size_t {
        using Pair = std::pair<GEO::index_t, GEO::index_t>;
        const auto first = std::lower_bound(m_slide_edge_pairs.begin(), m_slide_edge_pairs.end(), Pair{vertex, 0});
        const auto last  = std::upper_bound(first, m_slide_edge_pairs.end(), Pair{vertex, std::numeric_limits<GEO::index_t>::max()});
        std::size_t count = 0;
        for (auto i = first; i != last; ++i) {
            if (count < 2) {
                out[count] = i->second;
            }
            ++count;
        }
        return count;
    };
    for (const GEO::index_t vertex : m_slide_unique_vertices) {
        GEO::index_t neighbours[2];
        const std::size_t count = selected_neighbours(vertex, neighbours);
        if (count > 2) {
            log_trs_tool->info("Edge slide refused: vertex {} of '{}' is on {} selected edges", vertex, mesh->get_name(), count);
            return false;
        }
    }
    const auto slot_of = [this](const GEO::index_t vertex) -> std::size_t {
        return static_cast<std::size_t>(
            std::lower_bound(m_slide_unique_vertices.begin(), m_slide_unique_vertices.end(), vertex) - m_slide_unique_vertices.begin()
        );
    };
    m_slide_visited.assign(m_slide_unique_vertices.size(), 0);

    // Chain the vertices into loops: open loops walk from an end (pass 0),
    // closed loops from anywhere (pass 1, every remaining vertex has two
    // selected edges).
    for (int pass = 0; pass < 2; ++pass) {
        for (std::size_t slot = 0, slot_end = m_slide_unique_vertices.size(); slot < slot_end; ++slot) {
            if (m_slide_visited[slot] != 0) {
                continue;
            }
            const GEO::index_t start = m_slide_unique_vertices[slot];
            GEO::index_t       start_neighbours[2];
            if ((pass == 0) && (selected_neighbours(start, start_neighbours) != 1)) {
                continue;
            }
            m_slide_loop_vertices.clear();
            GEO::index_t previous = GEO::NO_INDEX;
            GEO::index_t current  = start;
            for (;;) {
                m_slide_visited[slot_of(current)] = 1;
                m_slide_loop_vertices.push_back(current);
                GEO::index_t      neighbours[2];
                const std::size_t count = selected_neighbours(current, neighbours);
                GEO::index_t      next  = GEO::NO_INDEX;
                for (std::size_t k = 0; k < count; ++k) {
                    if ((neighbours[k] != previous) && (m_slide_visited[slot_of(neighbours[k])] == 0)) {
                        next = neighbours[k];
                        break;
                    }
                }
                if (next == GEO::NO_INDEX) {
                    break;
                }
                previous = current;
                current  = next;
            }
            const bool        closed     = (pass == 1);
            const std::size_t n          = m_slide_loop_vertices.size();
            if (n < 2) {
                return false;
            }
            const std::size_t edge_count = closed ? n : (n - 1);
            const std::size_t loop_index = m_slide_loop_swapped.size();
            m_slide_loop_swapped.push_back(0);

            // The side facets of each loop edge, consistent along the loop.
            m_slide_loop_facets.assign(2 * edge_count, GEO::NO_INDEX);
            for (std::size_t k = 0; k < edge_count; ++k) {
                const GEO::index_t                  a      = m_slide_loop_vertices[k];
                const GEO::index_t                  b      = m_slide_loop_vertices[(k + 1) % n];
                const std::span<const GEO::index_t> facets = geometry.get_edge_facets(geometry.get_edge(a, b));
                GEO::index_t* const                 sides  = &m_slide_loop_facets[2 * k];
                if (k == 0) {
                    sides[0] = facets[0];
                    sides[1] = (facets.size() > 1) ? facets[1] : GEO::NO_INDEX;
                    continue;
                }
                const GEO::index_t* const previous_sides  = &m_slide_loop_facets[2 * (k - 1)];
                const GEO::index_t        previous_vertex = m_slide_loop_vertices[k - 1];
                int resolved[2]{-1, -1};
                for (std::size_t j = 0; j < facets.size(); ++j) {
                    const int side = resolve_fan_side(geometry, geo_mesh, facets[j], a, b, previous_vertex, previous_sides);
                    if ((side >= 0) && (sides[side] == GEO::NO_INDEX)) {
                        sides[side] = facets[j];
                        resolved[j] = side;
                    }
                }
                for (std::size_t j = 0; j < facets.size(); ++j) {
                    if (resolved[j] >= 0) {
                        continue;
                    }
                    // The empty side: the one the previous edge had no facet on,
                    // else whichever is still free.
                    int free_side = -1;
                    for (int s = 0; s < 2; ++s) {
                        if ((sides[s] == GEO::NO_INDEX) && (previous_sides[s] == GEO::NO_INDEX)) {
                            free_side = s;
                        }
                    }
                    if (free_side < 0) {
                        for (int s = 0; s < 2; ++s) {
                            if (sides[s] == GEO::NO_INDEX) {
                                free_side = s;
                                break;
                            }
                        }
                    }
                    if (free_side >= 0) {
                        sides[free_side] = facets[j];
                    }
                }
            }

            // Each vertex's rail end on each side.
            for (std::size_t i = 0; i < n; ++i) {
                const GEO::index_t vertex          = m_slide_loop_vertices[i];
                const glm::vec3    p               = position_of(geo_mesh, vertex);
                const bool         has_previous    = closed || (i > 0);
                const bool         has_next        = closed || ((i + 1) < n);
                const GEO::index_t previous_vertex = has_previous ? m_slide_loop_vertices[(i + n - 1) % n] : GEO::NO_INDEX;
                const GEO::index_t next_vertex     = has_next     ? m_slide_loop_vertices[(i + 1) % n]     : GEO::NO_INDEX;
                const std::size_t  previous_edge   = (i + edge_count - 1) % edge_count;
                const std::size_t  next_edge       = i;

                Slide_vertex slide_vertex{};
                slide_vertex.group       = group_index;
                slide_vertex.vertex      = vertex;
                slide_vertex.start_world = transform_point(world_from_node, p);
                slide_vertex.loop        = loop_index;
                for (int s = 0; s < 2; ++s) {
                    const GEO::index_t facet_previous = has_previous ? m_slide_loop_facets[(2 * previous_edge) + s] : GEO::NO_INDEX;
                    const GEO::index_t facet_next     = has_next     ? m_slide_loop_facets[(2 * next_edge)     + s] : GEO::NO_INDEX;
                    const GEO::index_t rail_previous  = (facet_previous != GEO::NO_INDEX) ? facet_other_neighbour(geo_mesh, facet_previous, vertex, previous_vertex) : GEO::NO_INDEX;
                    const GEO::index_t rail_next      = (facet_next     != GEO::NO_INDEX) ? facet_other_neighbour(geo_mesh, facet_next,     vertex, next_vertex)     : GEO::NO_INDEX;
                    std::optional<glm::vec3> target{};
                    if ((rail_previous == GEO::NO_INDEX) && (rail_next == GEO::NO_INDEX)) {
                        // A boundary side: zero rail.
                    } else if (rail_previous == GEO::NO_INDEX) {
                        target = position_of(geo_mesh, rail_next);
                    } else if ((rail_next == GEO::NO_INDEX) || (rail_previous == rail_next)) {
                        target = position_of(geo_mesh, rail_previous);
                    } else if ((facet_previous == facet_next) && (geometry.get_vertex_edges(vertex).size() == 2)) {
                        target = valence_two_destination(geo_mesh, facet_previous, vertex, previous_vertex, next_vertex);
                    } else {
                        target = turn_destination(
                            p,
                            position_of(geo_mesh, previous_vertex),
                            position_of(geo_mesh, next_vertex),
                            position_of(geo_mesh, rail_previous),
                            position_of(geo_mesh, rail_next)
                        );
                    }
                    if (target.has_value()) {
                        slide_vertex.side[s] = transform_point(world_from_node, target.value()) - slide_vertex.start_world;
                    }
                }
                m_slide_vertices.push_back(slide_vertex);
                group.vertices.push_back(vertex);
            }
        }
    }
    return true;
}

auto Mesh_component_transform::build_vertex_slide(App_context& context, const std::size_t group_index) -> bool
{
    Group&                                   group     = m_groups[group_index];
    const std::shared_ptr<erhe::scene::Mesh> mesh      = group.mesh.lock();
    Mesh_component_selection*                selection = context.mesh_component_selection;
    if (!mesh || (selection == nullptr) || !group.geometry) {
        return false;
    }
    const Mesh_component_entry* entry = selection->find_entry(mesh, group.primitive_index, group.geometry);
    if (entry == nullptr) {
        return false;
    }
    const erhe::geometry::Geometry& geometry = *group.geometry;
    if (!geometry.has_edge_connectivity()) {
        log_trs_tool->info("Vertex slide: '{}' has no edge connectivity", mesh->get_name());
        return false;
    }
    const GEO::Mesh& geo_mesh        = geometry.get_mesh();
    const glm::mat4  world_from_node = mesh->world_from_node();

    group.vertices.clear();
    group.slide_offset = m_slide_vertices.size();
    for (const GEO::index_t vertex : entry->vertices) {
        Slide_vertex slide_vertex{};
        slide_vertex.group           = group_index;
        slide_vertex.vertex          = vertex;
        slide_vertex.start_world     = transform_point(world_from_node, position_of(geo_mesh, vertex));
        slide_vertex.neighbour_begin = m_slide_neighbours.size();
        for (const GEO::index_t edge : geometry.get_vertex_edges(vertex)) {
            const GEO::index_t v0    = geo_mesh.edges.vertex(edge, 0);
            const GEO::index_t v1    = geo_mesh.edges.vertex(edge, 1);
            const GEO::index_t other = (v0 == vertex) ? v1 : v0;
            m_slide_neighbours.push_back(transform_point(world_from_node, position_of(geo_mesh, other)) - slide_vertex.start_world);
        }
        slide_vertex.neighbour_count = m_slide_neighbours.size() - slide_vertex.neighbour_begin;
        if (slide_vertex.neighbour_count == 0) {
            log_trs_tool->info("Vertex slide refused: vertex {} of '{}' has no neighbour", vertex, mesh->get_name());
            return false;
        }
        slide_vertex.side[0] = m_slide_neighbours[slide_vertex.neighbour_begin];
        m_slide_vertices.push_back(slide_vertex);
        group.vertices.push_back(vertex);
    }
    return true;
}

void Mesh_component_transform::align_slide_loops_world()
{
    if (m_slide_vertices.empty() || (m_scalar_kind != Scalar_edit_kind::edge_slide)) {
        return;
    }
    const Slide_vertex& active    = m_slide_vertices[m_slide_active];
    const glm::vec3     reference = active.side[0] - active.side[1];
    if (glm::dot(reference, reference) < c_rail_epsilon) {
        return;
    }
    for (std::size_t loop = 0, end = m_slide_loop_swapped.size(); loop < end; ++loop) {
        m_slide_loop_swapped[loop] = 0;
        if (loop == active.loop) {
            continue;
        }
        // The loop's vertex nearest to the active one decides the orientation.
        float     best_distance = std::numeric_limits<float>::max();
        glm::vec3 direction{0.0f};
        for (const Slide_vertex& slide_vertex : m_slide_vertices) {
            const glm::vec3 candidate = slide_vertex.side[0] - slide_vertex.side[1];
            if ((slide_vertex.loop != loop) || (glm::dot(candidate, candidate) < c_rail_epsilon)) {
                continue;
            }
            const glm::vec3 offset   = slide_vertex.start_world - active.start_world;
            const float     distance = glm::dot(offset, offset);
            if (distance < best_distance) {
                best_distance = distance;
                direction     = candidate;
            }
        }
        m_slide_loop_swapped[loop] = (glm::dot(direction, reference) < 0.0f) ? 1 : 0;
    }
}

auto Mesh_component_transform::select_active_slide_vertex(const Viewport_scene_view& view, const glm::vec2 position_in_viewport) -> bool
{
    if (!is_scalar_active() || m_slide_vertices.empty()) {
        return false;
    }
    float       best_distance = std::numeric_limits<float>::max();
    std::size_t best_index    = m_slide_vertices.size();
    for (std::size_t index = 0, end = m_slide_vertices.size(); index < end; ++index) {
        const std::optional<glm::vec2> projected = project_point(view, m_slide_vertices[index].start_world);
        if (!projected.has_value()) {
            continue;
        }
        const glm::vec2 offset   = projected.value() - position_in_viewport;
        const float     distance = glm::dot(offset, offset);
        if (distance < best_distance) {
            best_distance = distance;
            best_index    = index;
        }
    }
    if (best_index == m_slide_vertices.size()) {
        return false;
    }
    m_slide_active = best_index;
    if (m_scalar_kind != Scalar_edit_kind::edge_slide) {
        return true;
    }

    // Loops whose projected rail direction opposes the active vertex's swap
    // sides, so parallel loops move together (section 4.6).
    const auto screen_direction = [&view](const Slide_vertex& slide_vertex) -> std::optional<glm::vec2> {
        const std::optional<glm::vec2> a = project_point(view, slide_vertex.start_world + slide_vertex.side[0]);
        const std::optional<glm::vec2> b = project_point(view, slide_vertex.start_world + slide_vertex.side[1]);
        if (!a.has_value() || !b.has_value()) {
            return std::nullopt;
        }
        return a.value() - b.value();
    };
    const Slide_vertex&            active    = m_slide_vertices[m_slide_active];
    const std::optional<glm::vec2> reference = screen_direction(active);
    const std::optional<glm::vec2> origin    = project_point(view, active.start_world);
    if (!reference.has_value() || !origin.has_value() || (glm::dot(reference.value(), reference.value()) < 1e-6f)) {
        align_slide_loops_world();
        return true;
    }
    for (std::size_t loop = 0, end = m_slide_loop_swapped.size(); loop < end; ++loop) {
        m_slide_loop_swapped[loop] = 0;
        if (loop == active.loop) {
            continue;
        }
        float     best = std::numeric_limits<float>::max();
        glm::vec2 direction{0.0f};
        for (const Slide_vertex& slide_vertex : m_slide_vertices) {
            if (slide_vertex.loop != loop) {
                continue;
            }
            const std::optional<glm::vec2> candidate = screen_direction(slide_vertex);
            const std::optional<glm::vec2> projected = project_point(view, slide_vertex.start_world);
            if (!candidate.has_value() || !projected.has_value() || (glm::dot(candidate.value(), candidate.value()) < 1e-6f)) {
                continue;
            }
            const glm::vec2 offset   = projected.value() - origin.value();
            const float     distance = glm::dot(offset, offset);
            if (distance < best) {
                best      = distance;
                direction = candidate.value();
            }
        }
        m_slide_loop_swapped[loop] = (glm::dot(direction, reference.value()) < 0.0f) ? 1 : 0;
    }
    return true;
}

void Mesh_component_transform::pick_vertex_slide_neighbours_world(const glm::vec3& direction)
{
    if ((m_scalar_kind != Scalar_edit_kind::vertex_slide) || (glm::dot(direction, direction) < c_rail_epsilon)) {
        return;
    }
    const glm::vec3 unit_direction = glm::normalize(direction);
    m_slide_neighbours_picked = true;
    for (Slide_vertex& slide_vertex : m_slide_vertices) {
        float best = -std::numeric_limits<float>::max();
        for (std::size_t k = 0; k < slide_vertex.neighbour_count; ++k) {
            const glm::vec3& offset = m_slide_neighbours[slide_vertex.neighbour_begin + k];
            const float      length = glm::length(offset);
            if (length < 1e-6f) {
                continue;
            }
            const float alignment = glm::dot(offset / length, unit_direction);
            if (alignment > best) {
                best                 = alignment;
                slide_vertex.side[0] = offset;
            }
        }
    }
}

void Mesh_component_transform::pick_vertex_slide_neighbours(const Viewport_scene_view& view, const glm::vec2 drag_delta_in_viewport)
{
    if (!is_scalar_active() || (m_scalar_kind != Scalar_edit_kind::vertex_slide) || (glm::dot(drag_delta_in_viewport, drag_delta_in_viewport) < 1e-6f)) {
        return;
    }
    const glm::vec2 unit_delta = glm::normalize(drag_delta_in_viewport);
    m_slide_neighbours_picked = true;
    for (Slide_vertex& slide_vertex : m_slide_vertices) {
        const std::optional<glm::vec2> origin = project_point(view, slide_vertex.start_world);
        if (!origin.has_value()) {
            continue;
        }
        float best = -std::numeric_limits<float>::max();
        for (std::size_t k = 0; k < slide_vertex.neighbour_count; ++k) {
            const glm::vec3&               offset = m_slide_neighbours[slide_vertex.neighbour_begin + k];
            const std::optional<glm::vec2> end    = project_point(view, slide_vertex.start_world + offset);
            if (!end.has_value()) {
                continue;
            }
            const glm::vec2 direction = end.value() - origin.value();
            const float     length    = glm::length(direction);
            if (length < 1e-3f) {
                continue;
            }
            const float alignment = glm::dot(direction / length, unit_delta);
            if (alignment > best) {
                best                 = alignment;
                slide_vertex.side[0] = offset;
            }
        }
    }
}

auto Mesh_component_transform::get_active_slide_screen_frame(const Viewport_scene_view& view, Slide_screen_frame& out) const -> bool
{
    if (!is_scalar_active() || (m_slide_active >= m_slide_vertices.size())) {
        return false;
    }
    const Slide_vertex&            active = m_slide_vertices[m_slide_active];
    const std::optional<glm::vec2> origin = project_point(view, active.start_world);
    if (!origin.has_value()) {
        return false;
    }
    const auto project_side = [&](const glm::vec3& side) -> glm::vec2 {
        if (glm::dot(side, side) < c_rail_epsilon) {
            return origin.value();
        }
        const std::optional<glm::vec2> end = project_point(view, active.start_world + side);
        return end.has_value() ? end.value() : origin.value();
    };
    out.origin = origin.value();
    out.side_a = project_side(active.side[0]);
    out.side_b = (m_scalar_kind == Scalar_edit_kind::edge_slide) ? project_side(active.side[1]) : origin.value();
    return true;
}

auto Mesh_component_transform::factor_from_translation(const glm::vec3& translation) -> float
{
    if (m_slide_active >= m_slide_vertices.size()) {
        return 0.0f;
    }
    if (m_scalar_kind == Scalar_edit_kind::vertex_slide) {
        pick_vertex_slide_neighbours_world(translation);
        const glm::vec3& offset = m_slide_vertices[m_slide_active].side[0];
        const float      length_squared = glm::dot(offset, offset);
        return (length_squared > c_rail_epsilon) ? (glm::dot(translation, offset) / length_squared) : 0.0f;
    }
    // Edge slide: the translation projected on the active vertex's rail on the
    // side it points to; dragging the gizmo by exactly a rail gives +1 / -1.
    const Slide_vertex& active = m_slide_vertices[m_slide_active];
    const glm::vec3&    a      = active.side[0];
    const glm::vec3&    b      = active.side[1];
    if (glm::dot(translation, a - b) >= 0.0f) {
        const float length_squared = glm::dot(a, a);
        return (length_squared > c_rail_epsilon) ? (glm::dot(translation, a) / length_squared) : 0.0f;
    }
    const float length_squared = glm::dot(b, b);
    return (length_squared > c_rail_epsilon) ? -(glm::dot(translation, b) / length_squared) : 0.0f;
}

void Mesh_component_transform::apply_scalar(App_context& context, const Scalar_input& input)
{
    if (!is_scalar_active() || (m_slide_active >= m_slide_vertices.size())) {
        return;
    }
    const bool edge_slide = (m_scalar_kind == Scalar_edit_kind::edge_slide);
    float      factor     = input.factor;
    if (input.clamp) {
        factor = edge_slide ? std::clamp(factor, -1.0f, 1.0f) : std::clamp(factor, 0.0f, 1.0f);
    }
    // Vertex slide neighbours are re-picked while clamped and frozen once
    // picked when unclamped.
    if (!edge_slide && (input.clamp || !m_slide_neighbours_picked)) {
        pick_vertex_slide_neighbours_world(input.drag_direction_world);
    }

    // Edge slide: the side and the amount along it. Unclamped, the side of the
    // last clamped step is kept and the factor extrapolates along it.
    unsigned int side   = 0;
    float        amount = factor;
    if (edge_slide) {
        if (input.clamp) {
            side              = (factor >= 0.0f) ? 0u : 1u;
            m_slide_last_side = side;
            amount            = std::abs(factor);
        } else {
            side   = m_slide_last_side;
            amount = (side == 0u) ? factor : -factor;
        }
    }
    const bool flipped = input.even && input.flipped;
    const bool moved   = (amount != 0.0f) || flipped;
    if (moved) {
        fork_shared_groups(context);
    }

    // Even: the distance is the factor times the active vertex's rail length
    // on the side (edge slide; the first non-zero rail on that side when the
    // active vertex has none) or its chosen edge length (vertex slide).
    const auto side_of = [&](const Slide_vertex& slide_vertex) -> unsigned int {
        if (!edge_slide) {
            return 0u;
        }
        return (m_slide_loop_swapped[slide_vertex.loop] != 0) ? (1u - side) : side;
    };
    float reference_length = glm::length(m_slide_vertices[m_slide_active].side[side_of(m_slide_vertices[m_slide_active])]);
    if (reference_length < 1e-6f) {
        for (const Slide_vertex& slide_vertex : m_slide_vertices) {
            const float length = glm::length(slide_vertex.side[side_of(slide_vertex)]);
            if (length >= 1e-6f) {
                reference_length = length;
                break;
            }
        }
    }
    const float distance = amount * reference_length;

    for (Group& group : m_groups) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = group.mesh.lock();
        if (!mesh || !group.geometry) {
            continue;
        }
        if (group.before_local.size() != group.vertices.size()) {
            continue;
        }
        for (std::size_t i = 0, end = group.vertices.size(); i < end; ++i) {
            const Slide_vertex& slide_vertex = m_slide_vertices[group.slide_offset + i];
            const glm::vec3&    rail         = slide_vertex.side[side_of(slide_vertex)];
            const float         rail_length  = glm::length(rail);
            // The exact captured start position when the vertex does not move
            // (no world round trip, which would perturb it by a float ULP).
            glm::vec3 local_after = group.before_local[i];
            if (moved && (rail_length >= 1e-6f)) {
                const glm::vec3 direction = rail / rail_length;
                glm::vec3       world_after;
                if (!input.even) {
                    world_after = slide_vertex.start_world + (amount * rail);
                } else if (flipped) {
                    world_after = slide_vertex.start_world + rail - (direction * (reference_length - distance));
                } else {
                    world_after = slide_vertex.start_world + (direction * distance);
                }
                local_after = transform_point(group.node_from_world, world_after);
            }
            write_vertex(context, group, group.vertices[i], local_after);
        }
        // At the start position the built normals come back exactly; otherwise
        // the involved faces' normals follow the new positions.
        update_group_normals(context, group, moved ? Normal_source::live_positions : Normal_source::stored_attributes);
    }
}

void Mesh_component_transform::cancel(App_context& context)
{
    if (!m_active) {
        return;
    }
    m_active = false;
    m_scalar = false;
    const bool topology_step = m_has_topology_step;
    m_has_topology_step = false;

    for (Group& group : m_groups) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = group.mesh.lock();
        if (mesh && (group.extruded || group.forked)) {
            // The edit ran on a copy: swapping the before primitive back
            // restores the positions, the GPU buffers (never touched) and -
            // since Mesh_component_selection::is_live() follows the mesh's
            // current geometry - the pre-edit selection entry.
            std::vector<erhe::scene::Mesh_primitive> primitives = mesh->get_primitives();
            if (group.primitive_index < primitives.size()) {
                primitives[group.primitive_index] = group.extruded ? group.extrude_before : group.fork_before;
                swap_mesh_primitives(mesh, primitives);
            }
        } else if (mesh && group.geometry && (group.before_local.size() == group.vertices.size())) {
            for (std::size_t i = 0, end = group.vertices.size(); i < end; ++i) {
                write_vertex(context, group, group.vertices[i], group.before_local[i]);
            }
            update_group_normals(context, group, Normal_source::stored_attributes);
        }

        // Release the hold begin() took (transferred by fork / extrude). The
        // restored primitive lost its optimized variant when the edit began,
        // and no commit rebuild brings it back, so queue the re-optimization
        // (Primitive::publish_optimized_render_shape: the edit's end queues it).
        if (group.held_primitive) {
            group.held_primitive->release_optimization_hold();
            group.held_primitive.reset();
        }
        if (mesh && group.geometry) {
            const erhe::primitive::Build_info build_info = make_rebuild_build_info(*context.mesh_memory, *group.geometry);
            if (
                build_info.buffer_info.optimize_meshes &&
                (context.graphics_device != nullptr) &&
                context.graphics_device->supports_worker_contexts()
            ) {
                kickoff_deferred_finalize(context, mesh);
            }
        }
    }
    // Drop the edit's references (a fork / extrude copy and its primitive) so
    // the dormant selection entry keyed on the copy expires.
    m_groups.clear();

    // A topology step switched the mode (loop cut: to edge mode) after its
    // swap, so the pre-step entry was never converted: with the before
    // primitive back, switching to the before mode makes it the selection
    // again.
    if (topology_step && (context.mesh_component_selection != nullptr)) {
        context.mesh_component_selection->set_mode(m_topology_mode_before);
    }
}

auto Mesh_component_transform::has_moved_vertex(const Group& group) const -> bool
{
    if (!group.geometry || (group.before_local.size() != group.vertices.size())) {
        return false;
    }
    const GEO::Mesh& geo_mesh = group.geometry->get_mesh();
    for (std::size_t i = 0, end = group.vertices.size(); i < end; ++i) {
        if (position_of(geo_mesh, group.vertices[i]) != group.before_local[i]) {
            return true;
        }
    }
    return false;
}

auto Mesh_component_transform::count_moved_vertices() const -> std::size_t
{
    std::size_t count = 0;
    for (const Group& group : m_groups) {
        if (!group.geometry || (group.before_local.size() != group.vertices.size())) {
            continue;
        }
        const GEO::Mesh& geo_mesh = group.geometry->get_mesh();
        for (std::size_t i = 0, end = group.vertices.size(); i < end; ++i) {
            if (position_of(geo_mesh, group.vertices[i]) != group.before_local[i]) {
                ++count;
            }
        }
    }
    return count;
}

auto Mesh_component_transform::references_item_host(const erhe::Item_host* const item_host) const -> bool
{
    if (!m_active) {
        return false;
    }
    for (const Group& group : m_groups) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = group.mesh.lock();
        if (mesh && (mesh->get_item_host() == item_host)) {
            return true;
        }
    }
    return false;
}

void Mesh_component_transform::collect_corrected_texcoords(const Group& group, std::vector<Corner_texcoord_change>& out)
{
    if (!group.geometry || (group.before_local.size() != group.vertices.size())) {
        return;
    }
    const erhe::geometry::Geometry&        geometry   = *group.geometry;
    const erhe::geometry::Mesh_attributes& attributes = geometry.get_attributes();
    if (!geometry.has_connectivity()) {
        return;
    }
    const GEO::Mesh& geo_mesh = geometry.get_mesh();
    const erhe::geometry::Attribute_present<GEO::vec2f>* const texcoord_sets[3] = {
        &attributes.corner_texcoord_0,
        &attributes.corner_texcoord_1,
        &attributes.corner_texcoord_2
    };

    m_texcoord_lookup.clear();
    for (std::size_t i = 0, end = group.vertices.size(); i < end; ++i) {
        m_texcoord_lookup.emplace_back(group.vertices[i], i);
    }
    std::sort(m_texcoord_lookup.begin(), m_texcoord_lookup.end());
    // The pre-slide position of a vertex: its captured start when it slid.
    const auto before_position = [&](const GEO::index_t vertex) -> glm::vec3 {
        const auto i = std::lower_bound(
            m_texcoord_lookup.begin(), m_texcoord_lookup.end(),
            std::pair<GEO::index_t, std::size_t>{vertex, 0}
        );
        if ((i != m_texcoord_lookup.end()) && (i->first == vertex)) {
            return group.before_local[i->second];
        }
        return position_of(geo_mesh, vertex);
    };

    for (std::size_t i = 0, end = group.vertices.size(); i < end; ++i) {
        const GEO::index_t vertex = group.vertices[i];
        const glm::vec3    after  = position_of(geo_mesh, vertex);
        if (after == group.before_local[i]) {
            continue;
        }
        for (const GEO::index_t corner : geometry.get_vertex_corners(vertex)) {
            const GEO::index_t facet        = geometry.get_corner_facet(corner);
            const GEO::index_t corner_begin = geo_mesh.facets.corners_begin(facet);
            const GEO::index_t corner_end   = geo_mesh.facets.corners_end(facet);
            const GEO::index_t corner_count = corner_end - corner_begin;
            if (corner_count < 3) {
                continue;
            }

            // The facet's pre-slide polygon in its own plane.
            glm::vec3 normal{0.0f};
            for (GEO::index_t local = 0; local < corner_count; ++local) {
                const glm::vec3 a = before_position(geo_mesh.facet_corners.vertex(corner_begin + local));
                const glm::vec3 b = before_position(geo_mesh.facet_corners.vertex(corner_begin + ((local + 1) % corner_count)));
                normal += glm::cross(a, b);
            }
            if (glm::dot(normal, normal) < 1e-20f) {
                continue;
            }
            normal = glm::normalize(normal);
            const glm::vec3 origin = before_position(geo_mesh.facet_corners.vertex(corner_begin));
            glm::vec3       axis_u = before_position(geo_mesh.facet_corners.vertex(corner_begin + 1)) - origin;
            axis_u -= glm::dot(axis_u, normal) * normal;
            if (glm::dot(axis_u, axis_u) < 1e-20f) {
                axis_u = erhe::math::min_axis<float>(normal);
                axis_u -= glm::dot(axis_u, normal) * normal;
            }
            axis_u = glm::normalize(axis_u);
            const glm::vec3 axis_v = glm::cross(normal, axis_u);
            const auto to_plane = [&](const glm::vec3& p) -> glm::vec2 {
                return glm::vec2{glm::dot(p - origin, axis_u), glm::dot(p - origin, axis_v)};
            };
            m_texcoord_polygon.clear();
            for (GEO::index_t c = corner_begin; c < corner_end; ++c) {
                m_texcoord_polygon.push_back(to_plane(before_position(geo_mesh.facet_corners.vertex(c))));
            }
            if (!mean_value_weights(m_texcoord_polygon, to_plane(after), m_texcoord_weights)) {
                continue;
            }

            for (std::uint32_t set = 0; set < 3; ++set) {
                const erhe::geometry::Attribute_present<GEO::vec2f>& texcoord = *texcoord_sets[set];
                bool complete = true;
                for (GEO::index_t c = corner_begin; c < corner_end; ++c) {
                    if (!texcoord.has(c)) {
                        complete = false;
                        break;
                    }
                }
                if (!complete) {
                    continue;
                }
                glm::vec2 value{0.0f};
                for (GEO::index_t local = 0; local < corner_count; ++local) {
                    const GEO::vec2f t = texcoord.get(corner_begin + local);
                    value += m_texcoord_weights[local] * glm::vec2{t.x, t.y};
                }
                const GEO::vec2f before = texcoord.get(corner);
                if ((value.x != before.x) || (value.y != before.y)) {
                    out.push_back(Corner_texcoord_change{
                        .corner = corner,
                        .set    = set,
                        .before = glm::vec2{before.x, before.y},
                        .after  = value
                    });
                }
            }
        }
    }
}

}
