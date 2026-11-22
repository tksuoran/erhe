#include "tools/channel_paint.hpp"

#include "app_context.hpp"
#include "operations/async_raytrace_kickoff_operation.hpp"
#include "operations/compound_operation.hpp"
#include "operations/paint_colors_operation.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include <geogram/mesh/mesh.h>

#include <algorithm>

namespace editor {

using erhe::geometry::get_pointf;
using erhe::geometry::to_glm_vec3;

namespace {

// What the primitive builder renders for a corner: its corner color, else its vertex color, else white.
[[nodiscard]] auto get_corner_color(const erhe::geometry::Geometry& geometry, const GEO::index_t corner) -> glm::vec4
{
    const erhe::geometry::Mesh_attributes& attributes   = geometry.get_attributes();
    const std::optional<GEO::vec4f>        corner_color = attributes.corner_color_0.try_get(corner);
    if (corner_color.has_value()) {
        const GEO::vec4f c = corner_color.value();
        return glm::vec4{c.x, c.y, c.z, c.w};
    }
    const GEO::index_t              vertex       = geometry.get_mesh().facet_corners.vertex(corner);
    const std::optional<GEO::vec4f> vertex_color = attributes.vertex_color_0.try_get(vertex);
    if (vertex_color.has_value()) {
        const GEO::vec4f c = vertex_color.value();
        return glm::vec4{c.x, c.y, c.z, c.w};
    }
    return glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
}

[[nodiscard]] auto replace_masked(const glm::vec4& color, const glm::vec4& value, const unsigned int channel_mask) -> glm::vec4
{
    glm::vec4 result = color;
    for (int channel = 0; channel < 4; ++channel) {
        if ((channel_mask & (1u << channel)) != 0) {
            result[channel] = value[channel];
        }
    }
    return result;
}

[[nodiscard]] auto make_paint_colors_build_info(App_context& context, const erhe::scene::Mesh& mesh) -> erhe::primitive::Build_info
{
    return erhe::primitive::Build_info{
        .primitive_types = {
            .fill_triangles          = true,
            .fill_triangles_expanded = true,
            .edge_lines              = true,
            .corner_points           = true,
            .centroid_points         = true
        },
        // Skinned meshes rebuild into the skinned vertex format, or the GPU streams lose their joints.
        .buffer_info = mesh.skin
            ? context.mesh_memory->make_skinned_primitive_buffer_info()
            : context.mesh_memory->make_primitive_buffer_info()
    };
}

[[nodiscard]] auto get_primitive_geometry(const erhe::scene::Mesh& mesh, const std::size_t primitive_index) -> std::shared_ptr<erhe::geometry::Geometry>
{
    const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh.get_primitives();
    if (primitive_index >= mesh_primitives.size()) {
        return {};
    }
    const erhe::scene::Mesh_primitive& mesh_primitive = mesh_primitives[primitive_index];
    if (!mesh_primitive.primitive || !mesh_primitive.primitive->render_shape) {
        return {};
    }
    return mesh_primitive.primitive->render_shape->get_geometry();
}

} // anonymous namespace

Channel_paint_stroke::Channel_paint_stroke(App_context& context)
    : m_context{context}
{
}

Channel_paint_stroke::~Channel_paint_stroke() noexcept = default;

auto Channel_paint_stroke::is_active() const -> bool
{
    return static_cast<bool>(m_geometry);
}

auto Channel_paint_stroke::get_changed_vertex_count() const -> std::size_t
{
    return m_recorded_vertices.size();
}

auto Channel_paint_stroke::get_settings() -> Channel_brush_settings&
{
    return m_settings;
}

auto Channel_paint_stroke::get_mesh() const -> const std::shared_ptr<erhe::scene::Mesh>&
{
    return m_mesh;
}

auto Channel_paint_stroke::get_primitive_index() const -> std::size_t
{
    return m_primitive_index;
}

auto Channel_paint_stroke::begin(
    const std::shared_ptr<erhe::scene::Mesh>& mesh,
    const std::size_t                         primitive_index,
    const Channel_brush_settings&             settings
) -> std::optional<std::string>
{
    if (is_active()) {
        return std::string{"a stroke is already active"};
    }
    if (!mesh) {
        return std::string{"no mesh"};
    }
    const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_primitives();
    if (primitive_index >= mesh_primitives.size()) {
        return "primitive index " + std::to_string(primitive_index) + " is out of range (mesh '" + mesh->get_name() + "' has " + std::to_string(mesh_primitives.size()) + ")";
    }
    const std::shared_ptr<erhe::primitive::Primitive>& primitive = mesh_primitives[primitive_index].primitive;
    if (!primitive || !primitive->render_shape) {
        return "primitive " + std::to_string(primitive_index) + " of mesh '" + mesh->get_name() + "' has no render shape";
    }
    const std::shared_ptr<erhe::geometry::Geometry> geometry = primitive->render_shape->get_geometry();
    if (!geometry) {
        return "primitive " + std::to_string(primitive_index) + " of mesh '" + mesh->get_name() + "' has no Geometry; painting needs one";
    }
    if (primitive->render_shape->is_base_deferred()) {
        request_deferred_base_build(m_context, mesh, Deferred_base_build::full);
        return "primitive " + std::to_string(primitive_index) + " of mesh '" + mesh->get_name() + "' has no vertex buffer yet; its build is requested";
    }

    m_mesh            = mesh;
    m_primitive_index = primitive_index;
    m_primitive       = primitive;
    m_geometry        = geometry;
    m_settings        = settings;

    const GEO::Mesh&   geo_mesh     = geometry->get_mesh();
    const GEO::index_t vertex_count = geo_mesh.vertices.nb();
    const GEO::index_t corner_count = geo_mesh.facet_corners.nb();

    // Corners grouped by vertex; the value of a vertex is that of its first corner.
    m_vertex_corner_offsets.clear();
    m_vertex_corner_offsets.resize(static_cast<std::size_t>(vertex_count) + 1, 0);
    for (GEO::index_t corner = 0; corner < corner_count; ++corner) {
        ++m_vertex_corner_offsets[geo_mesh.facet_corners.vertex(corner) + 1];
    }
    for (GEO::index_t vertex = 0; vertex < vertex_count; ++vertex) {
        m_vertex_corner_offsets[vertex + 1] += m_vertex_corner_offsets[vertex];
    }
    m_vertex_corners.clear();
    m_vertex_corners.resize(corner_count, 0);
    m_corners.clear();
    m_corners.insert(m_corners.end(), m_vertex_corner_offsets.begin(), m_vertex_corner_offsets.end() - 1); // fill cursors
    for (GEO::index_t corner = 0; corner < corner_count; ++corner) {
        const GEO::index_t vertex = geo_mesh.facet_corners.vertex(corner);
        m_vertex_corners[m_corners[vertex]++] = corner;
    }
    m_corners.clear();

    const glm::mat4 world_from_node = mesh->world_from_node();
    m_positions.clear();
    m_values.clear();
    for (GEO::index_t vertex = 0; vertex < vertex_count; ++vertex) {
        const glm::vec3 p_in_node = to_glm_vec3(get_pointf(geo_mesh.vertices, vertex));
        m_positions.push_back(glm::vec3{world_from_node * glm::vec4{p_in_node, 1.0f}});
        const GEO::index_t first = m_vertex_corner_offsets[vertex];
        const GEO::index_t last  = m_vertex_corner_offsets[vertex + 1];
        m_values.push_back((first < last) ? get_corner_color(*geometry, m_vertex_corners[first]) : glm::vec4{1.0f});
    }
    m_stroke.begin(m_values);
    m_vertex_recorded.clear();
    m_vertex_recorded.resize(vertex_count, 0);
    m_recorded_vertices.clear();
    m_before_colors.clear();

    return {};
}

void Channel_paint_stroke::dab(const glm::vec3& center)
{
    if (!is_active()) {
        return;
    }
    apply_dab(m_settings, m_stroke, center, m_positions, m_values, m_changed);
    for (const std::uint32_t vertex : m_changed) {
        const GEO::index_t first = m_vertex_corner_offsets[vertex];
        const GEO::index_t last  = m_vertex_corner_offsets[vertex + 1];
        if (m_recorded_vertices.empty()) {
            // Before the first GPU write: the optimized variant cannot express the per corner edit.
            m_hold = m_mesh->begin_optimized_variant_edit(m_primitive_index);
        }
        if (m_vertex_recorded[vertex] == 0) {
            m_vertex_recorded[vertex] = 1;
            m_recorded_vertices.push_back(vertex);
            for (GEO::index_t i = first; i < last; ++i) {
                m_corners.push_back(m_vertex_corners[i]);
                m_before_colors.push_back(get_corner_color(*m_geometry, m_vertex_corners[i]));
            }
        }
        for (GEO::index_t i = first; i < last; ++i) {
            const GEO::index_t corner = m_vertex_corners[i];
            write_corner(corner, replace_masked(get_corner_color(*m_geometry, corner), m_values[vertex], m_settings.channel_mask));
        }
    }
}

void Channel_paint_stroke::write_corner(const GEO::index_t corner, const glm::vec4& color)
{
    m_geometry->get_attributes().corner_color_0.set(corner, GEO::vec4f{color.x, color.y, color.z, color.w});

    // GPU writes address the per corner base buffer: the element mappings describe that variant only.
    const erhe::primitive::Element_mappings& element_mappings = m_primitive->render_shape->get_element_mappings();
    if (corner >= element_mappings.mesh_corner_to_vertex_buffer_index.size()) {
        return;
    }
    const std::uint32_t                             vertex_id        = element_mappings.mesh_corner_to_vertex_buffer_index[corner];
    erhe::scene_renderer::Mesh_memory&              mesh_memory      = *m_context.mesh_memory;
    const erhe::primitive::Buffer_mesh&             buffer_mesh      = m_primitive->render_shape->get_renderable_mesh();
    const erhe::scene_renderer::Vertex_input_entry& vertex_input     = mesh_memory.get_vertex_input(buffer_mesh.vertex_input_key);
    const erhe::dataformat::Vertex_format&          vertex_format    = vertex_input.vertex_format;
    const erhe::dataformat::Attribute_stream        attribute_stream = vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::color, 0);
    if (attribute_stream.attribute == nullptr) {
        return;
    }
    const std::size_t                   stream_index  = attribute_stream.stream - vertex_format.streams.data();
    const std::size_t                   vertex_offset = (vertex_id * attribute_stream.stream->stride) + attribute_stream.attribute->offset;
    const erhe::primitive::Buffer_range stream_range  = buffer_mesh.vertex_buffer_ranges.at(stream_index);
    const erhe::primitive::Buffer_range update_range{
        .count        = 1,
        .element_size = erhe::dataformat::get_format_size_bytes(attribute_stream.attribute->format),
        .byte_offset  = stream_range.byte_offset + vertex_offset,
        .pool_id      = stream_range.pool_id,
        .buffer_id    = stream_range.buffer_id
    };
    std::vector<std::uint8_t> buffer;
    if (attribute_stream.attribute->format == erhe::dataformat::Format::format_32_vec4_float) {
        buffer.resize(sizeof(float) * 4);
        float* const ptr = reinterpret_cast<float*>(buffer.data());
        for (int channel = 0; channel < 4; ++channel) {
            ptr[channel] = color[channel];
        }
    } else if (attribute_stream.attribute->format == erhe::dataformat::Format::format_8_vec4_unorm) {
        buffer.resize(4);
        for (int channel = 0; channel < 4; ++channel) {
            buffer[channel] = erhe::dataformat::float_to_unorm8(color[channel]);
        }
    } else {
        return;
    }
    mesh_memory.enqueue_vertex_data(update_range, std::move(buffer));
}

auto Channel_paint_stroke::end() -> std::shared_ptr<Operation>
{
    if (!is_active()) {
        return {};
    }
    std::shared_ptr<Operation> operation;
    if (!m_corners.empty() && m_primitive->render_shape) {
        std::vector<glm::vec4> after_colors;
        after_colors.reserve(m_corners.size());
        for (const GEO::index_t corner : m_corners) {
            after_colors.push_back(get_corner_color(*m_geometry, corner));
        }
        operation = std::make_shared<Paint_colors_operation>(
            Paint_colors_operation::Parameters{
                .mesh            = m_mesh,
                .primitive_index = m_primitive_index,
                .geometry        = m_geometry,
                .corners         = m_corners,
                .before_colors   = m_before_colors,
                .after_colors    = std::move(after_colors),
                .build_info      = make_paint_colors_build_info(m_context, *m_mesh),
                .normal_style    = m_primitive->render_shape->get_normal_style()
            }
        );
    }
    release();
    return operation;
}

void Channel_paint_stroke::cancel()
{
    if (!is_active()) {
        return;
    }
    for (std::size_t i = 0, end = m_corners.size(); i < end; ++i) {
        write_corner(m_corners[i], m_before_colors[i]);
    }
    const std::shared_ptr<erhe::scene::Mesh> mesh    = m_mesh;
    const bool                               written = !m_corners.empty();
    release();
    // No operation rebuilds the primitive, so the dropped optimized variant is rebuilt in the background.
    if (written && (m_context.graphics_device != nullptr) && m_context.graphics_device->supports_worker_contexts()) {
        kickoff_deferred_finalize(m_context, mesh);
    }
}

void Channel_paint_stroke::release()
{
    if (m_hold) {
        m_hold->release_optimization_hold();
    }
    m_hold.reset();
    m_mesh.reset();
    m_primitive.reset();
    m_geometry.reset();
    m_stroke.clear();
    m_positions.clear();
    m_values.clear();
    m_vertex_corner_offsets.clear();
    m_vertex_corners.clear();
    m_vertex_recorded.clear();
    m_recorded_vertices.clear();
    m_corners.clear();
    m_before_colors.clear();
    m_changed.clear();
}

auto make_channel_fill_operation(
    App_context&                                        context,
    const std::span<const std::shared_ptr<erhe::scene::Mesh>> meshes,
    const unsigned int                                  channel_mask,
    const Channel_fill_mode                             mode,
    const float                                         value,
    std::string&                                        out_error
) -> std::shared_ptr<Operation>
{
    std::vector<std::shared_ptr<Operation>>              operations;
    std::vector<const erhe::geometry::Geometry*>          visited;
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : meshes) {
        if (!mesh) {
            continue;
        }
        const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_primitives();
        for (std::size_t primitive_index = 0, end = mesh_primitives.size(); primitive_index < end; ++primitive_index) {
            const std::shared_ptr<erhe::geometry::Geometry> geometry = get_primitive_geometry(*mesh, primitive_index);
            if (!geometry) {
                continue;
            }
            // A Geometry shared by several meshes is written once; the rebuild reaches every sharer.
            if (std::find(visited.begin(), visited.end(), geometry.get()) != visited.end()) {
                continue;
            }
            visited.push_back(geometry.get());
            const GEO::index_t corner_count = geometry->get_mesh().facet_corners.nb();
            if (corner_count == 0) {
                continue;
            }
            Paint_colors_operation::Parameters parameters{
                .mesh            = mesh,
                .primitive_index = primitive_index,
                .geometry        = geometry,
                .build_info      = make_paint_colors_build_info(context, *mesh),
                .normal_style    = mesh_primitives[primitive_index].primitive->render_shape->get_normal_style()
            };
            parameters.corners      .reserve(corner_count);
            parameters.before_colors.reserve(corner_count);
            parameters.after_colors .reserve(corner_count);
            for (GEO::index_t corner = 0; corner < corner_count; ++corner) {
                const glm::vec4 before = get_corner_color(*geometry, corner);
                const glm::vec4 target = (mode == Channel_fill_mode::fill)
                    ? glm::vec4{std::clamp(value, 0.0f, 1.0f)}
                    : (glm::vec4{1.0f} - glm::clamp(before, glm::vec4{0.0f}, glm::vec4{1.0f}));
                parameters.corners      .push_back(corner);
                parameters.before_colors.push_back(before);
                parameters.after_colors .push_back(replace_masked(before, target, channel_mask));
            }
            operations.push_back(std::make_shared<Paint_colors_operation>(std::move(parameters)));
        }
    }
    if (operations.empty()) {
        out_error = "none of the meshes has a primitive with a Geometry";
        return {};
    }
    if (operations.size() == 1) {
        return operations.front();
    }
    return std::make_shared<Compound_operation>(Compound_operation::Parameters{.operations = std::move(operations)});
}

}
