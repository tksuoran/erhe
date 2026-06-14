#include "transform/mesh_component_transform.hpp"
#include "transform/transform_tool.hpp"

#include "app_context.hpp"
#include "operations/move_mesh_vertices_operation.hpp"
#include "operations/operation_stack.hpp"
#include "tools/mesh_component_selection.hpp"

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include <geogram/mesh/mesh.h>

#include <algorithm>

using erhe::geometry::get_pointf;
using erhe::geometry::set_pointf;

namespace editor {

auto Mesh_component_transform::gather(App_context& context) -> bool
{
    Mesh_component_selection* selection = context.mesh_component_selection;
    if (selection == nullptr) {
        return false;
    }
    if (selection->get_mode() == Mesh_component_mode::object) {
        return false;
    }
    std::shared_ptr<erhe::scene::Mesh> mesh = selection->get_active_mesh();
    if (!mesh) {
        return false;
    }
    const std::size_t primitive_index = selection->get_active_primitive_index();
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    if (primitive_index >= primitives.size()) {
        return false;
    }
    const std::shared_ptr<erhe::primitive::Primitive>& primitive = primitives[primitive_index].primitive;
    if (!primitive) {
        return false;
    }
    // Component editing is only valid when the raytrace/selection geometry is the same
    // object as the render geometry, i.e. there is no separate collision Primitive_shape.
    // For two-geometry primitives the selection indices address the collision geometry,
    // not the render geometry, so editing would corrupt the mesh.
    if (primitive->collision_shape) {
        return false;
    }
    const std::shared_ptr<erhe::primitive::Primitive_render_shape>& render_shape = primitive->render_shape;
    if (!render_shape) {
        return false;
    }
    const std::shared_ptr<erhe::geometry::Geometry>& geometry = render_shape->get_geometry();
    if (!geometry) {
        return false;
    }
    if (mesh->get_node() == nullptr) {
        return false;
    }

    const GEO::Mesh& geo_mesh = geometry->get_mesh();

    m_vertices.clear();
    switch (selection->get_mode()) {
        case Mesh_component_mode::vertex: {
            for (const GEO::index_t vertex : selection->get_vertices()) {
                m_vertices.push_back(vertex);
            }
            break;
        }
        case Mesh_component_mode::edge: {
            for (const Mesh_edge_key& edge : selection->get_edges()) {
                m_vertices.push_back(edge.first);
                m_vertices.push_back(edge.second);
            }
            break;
        }
        case Mesh_component_mode::face: {
            for (const GEO::index_t facet : selection->get_facets()) {
                const GEO::index_t corner_begin = geo_mesh.facets.corners_begin(facet);
                const GEO::index_t corner_end   = geo_mesh.facets.corners_end(facet);
                for (GEO::index_t corner = corner_begin; corner < corner_end; ++corner) {
                    m_vertices.push_back(geo_mesh.facet_corners.vertex(corner));
                }
            }
            break;
        }
        case Mesh_component_mode::object:
        default: {
            return false;
        }
    }

    if (m_vertices.empty()) {
        return false;
    }

    std::sort(m_vertices.begin(), m_vertices.end());
    m_vertices.erase(std::unique(m_vertices.begin(), m_vertices.end()), m_vertices.end());

    // Drop any stale out-of-range indices defensively.
    const GEO::index_t vertex_count = geo_mesh.vertices.nb();
    m_vertices.erase(
        std::remove_if(
            m_vertices.begin(),
            m_vertices.end(),
            [vertex_count](const GEO::index_t vertex) { return vertex >= vertex_count; }
        ),
        m_vertices.end()
    );
    if (m_vertices.empty()) {
        return false;
    }

    m_mesh            = mesh;
    m_primitive_index = primitive_index;
    m_geometry        = geometry;
    return true;
}

auto Mesh_component_transform::update_anchor(App_context& context, Transform_tool_shared& shared) -> bool
{
    if (!gather(context)) {
        shared.component_mode = false;
        shared.entries.clear();
        return false;
    }

    std::shared_ptr<erhe::scene::Mesh> mesh = m_mesh.lock();
    erhe::scene::Node*                 node = mesh->get_node();
    const glm::mat4                    world_from_node = node->world_from_node();
    const GEO::Mesh&                   geo_mesh = m_geometry->get_mesh();

    glm::vec3 centroid_local{0.0f};
    for (const GEO::index_t vertex : m_vertices) {
        const GEO::vec3f p = get_pointf(geo_mesh.vertices, vertex);
        centroid_local += glm::vec3{p.x, p.y, p.z};
    }
    centroid_local /= static_cast<float>(m_vertices.size());
    const glm::vec3 centroid_world = glm::vec3{world_from_node * glm::vec4{centroid_local, 1.0f}};
    const glm::quat rotation       = node->world_from_node_transform().get_rotation();

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
    std::shared_ptr<erhe::scene::Mesh> mesh = m_mesh.lock();
    erhe::scene::Node*                 node = mesh->get_node();
    m_world_from_node = node->world_from_node();
    m_node_from_world = node->node_from_world();

    const GEO::Mesh& geo_mesh = m_geometry->get_mesh();
    m_before_local.clear();
    for (const GEO::index_t vertex : m_vertices) {
        const GEO::vec3f p = get_pointf(geo_mesh.vertices, vertex);
        m_before_local.push_back(glm::vec3{p.x, p.y, p.z});
    }
    m_active = true;
}

void Mesh_component_transform::apply(App_context& context, Transform_tool_shared& shared, const glm::mat4& updated_world_from_anchor)
{
    if (!m_active || !m_geometry) {
        return;
    }
    std::shared_ptr<erhe::scene::Mesh> mesh = m_mesh.lock();
    if (!mesh) {
        return;
    }

    const glm::mat4 world_delta = updated_world_from_anchor * shared.world_from_anchor_initial_state.get_inverse_matrix();
    GEO::Mesh&      geo_mesh    = m_geometry->get_mesh();

    for (std::size_t i = 0, end = m_vertices.size(); i < end; ++i) {
        const GEO::index_t vertex       = m_vertices[i];
        const glm::vec3    world_before = glm::vec3{m_world_from_node * glm::vec4{m_before_local[i], 1.0f}};
        const glm::vec3    world_after  = glm::vec3{world_delta       * glm::vec4{world_before,      1.0f}};
        const glm::vec3    local_after  = glm::vec3{m_node_from_world  * glm::vec4{world_after,       1.0f}};
        set_pointf(geo_mesh.vertices, vertex, GEO::vec3f{local_after.x, local_after.y, local_after.z});
        enqueue_gpu_position(context, vertex, local_after);
    }
}

void Mesh_component_transform::commit(App_context& context)
{
    if (!m_active) {
        return;
    }
    m_active = false;

    std::shared_ptr<erhe::scene::Mesh> mesh = m_mesh.lock();
    if (!mesh || !m_geometry || m_vertices.empty()) {
        return;
    }
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    if (m_primitive_index >= primitives.size() || !primitives[m_primitive_index].primitive) {
        return;
    }
    const erhe::primitive::Primitive& primitive = *primitives[m_primitive_index].primitive.get();
    if (!primitive.render_shape) {
        return;
    }

    GEO::Mesh& geo_mesh = m_geometry->get_mesh();
    std::vector<glm::vec3> after_local;
    after_local.reserve(m_vertices.size());
    for (const GEO::index_t vertex : m_vertices) {
        const GEO::vec3f p = get_pointf(geo_mesh.vertices, vertex);
        after_local.push_back(glm::vec3{p.x, p.y, p.z});
    }

    // Do not record a no-op edit (e.g. a click on a handle without dragging).
    if ((after_local.size() == m_before_local.size()) && (after_local == m_before_local)) {
        return;
    }

    erhe::primitive::Build_info build_info{
        .primitive_types = {
            .fill_triangles  = true,
            .edge_lines      = true,
            .corner_points   = true,
            .centroid_points = true
        },
        .buffer_info = context.mesh_memory->make_primitive_buffer_info()
    };

    context.operation_stack->queue(
        std::make_shared<Move_mesh_vertices_operation>(
            Move_mesh_vertices_operation::Parameters{
                .mesh             = mesh,
                .primitive_index  = m_primitive_index,
                .geometry         = m_geometry,
                .vertices         = m_vertices,
                .before_positions = m_before_local,
                .after_positions  = after_local,
                .build_info       = build_info,
                .normal_style     = primitive.render_shape->get_normal_style()
            }
        )
    );
}

void Mesh_component_transform::enqueue_gpu_position(App_context& context, const GEO::index_t vertex, const glm::vec3& local_position)
{
    std::shared_ptr<erhe::scene::Mesh> mesh = m_mesh.lock();
    if (!mesh) {
        return;
    }
    erhe::scene_renderer::Mesh_memory& mesh_memory = *context.mesh_memory;

    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    if (m_primitive_index >= primitives.size() || !primitives[m_primitive_index].primitive) {
        return;
    }
    const erhe::primitive::Primitive& primitive = *primitives[m_primitive_index].primitive.get();
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
    for (const GEO::index_t corner : m_geometry->get_vertex_corners(vertex)) {
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

}
