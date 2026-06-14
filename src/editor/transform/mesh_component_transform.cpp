#include "transform/mesh_component_transform.hpp"
#include "transform/transform_tool.hpp"

#include "app_context.hpp"
#include "config/generated/editor_settings_config.hpp"
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
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_verify/verify.hpp"

#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <cmath>

using erhe::geometry::get_pointf;
using erhe::geometry::set_pointf;

namespace editor {

namespace {

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

    // Orient the anchor with the first group's mesh (multi-mesh has no single basis).
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    const std::shared_ptr<erhe::scene::Mesh> first_mesh = m_groups.front().mesh.lock();
    if (first_mesh) {
        const erhe::scene::Node* node = first_mesh->get_node();
        if (node != nullptr) {
            rotation = node->world_from_node_transform().get_rotation();
        }
    }

    shared.world_from_anchor_initial_state.set_trs(centroid_world, rotation, glm::vec3{1.0f});
    shared.world_from_anchor = shared.world_from_anchor_initial_state;
    shared.entries.clear();
    shared.component_mode = true;
    return true;
}

void Mesh_component_transform::begin(App_context& context)
{
    if (!gather(context)) {
        m_active = false;
        return;
    }
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

    for (Group& group : m_groups) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = group.mesh.lock();
        if (!mesh || !group.geometry) {
            continue;
        }
        if (group.before_local.size() != group.vertices.size()) {
            continue;
        }

        // Fork-on-first-move: the first time the user actually moves, if fork mode is
        // on and this group's geometry is shared by another mesh, deep-copy the
        // geometry onto a new primitive for THIS mesh only - BEFORE touching any
        // positions - so the other instances never move. A click without dragging
        // keeps `moved` false and never forks.
        if (fork_mode && moved && !group.forked && is_geometry_shared(context, mesh, group.geometry.get())) {
            fork_group(context, group);
        }

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
                const glm::vec3 world_after  = glm::vec3{world_delta           * glm::vec4{world_before,          1.0f}};
                local_after                  = glm::vec3{group.node_from_world  * glm::vec4{world_after,           1.0f}};
            }
            set_pointf(geo_mesh.vertices, vertex, GEO::vec3f{local_after.x, local_after.y, local_after.z});
            enqueue_gpu_position(context, group, vertex, local_after);
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

}
