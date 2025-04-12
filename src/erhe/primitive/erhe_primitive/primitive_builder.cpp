// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/buffer_writer.hpp"
#include "erhe_primitive/format_info.hpp"
#include "erhe_primitive/index_range.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::primitive {

Build_context_root::Build_context_root(
    Buffer_mesh&      buffer_mesh,
    const GEO::Mesh&  mesh,
    const Build_info& build_info,
    Element_mappings& element_mappings_in
)
    : buffer_mesh     {buffer_mesh}
    , mesh            {mesh}
    , build_info      {build_info}
    , element_mappings{element_mappings_in}
    , mesh_info       {::get_mesh_info(mesh)}
    , vertex_format   {build_info.buffer_info.vertex_format}
{
    get_mesh_info          ();
    get_vertex_attributes  ();
    allocate_vertex_buffers();
    allocate_index_buffer  ();
}

void Build_context_root::get_mesh_info()
{
    const Primitive_types& primitive_types = build_info.primitive_types;

    // Count vertices
    total_vertex_count = 0;
    total_vertex_count += mesh_info.vertex_count_corners;
    if (primitive_types.centroid_points) {
        total_vertex_count += mesh_info.vertex_count_centroids;
    }

    // Count indices
    if (primitive_types.fill_triangles) {
        total_index_count += mesh_info.index_count_fill_triangles;
        allocate_index_range(Primitive_type::triangles, mesh_info.index_count_fill_triangles, buffer_mesh.triangle_fill_indices);
        const std::size_t primitive_count = mesh_info.index_count_fill_triangles;
        element_mappings.triangle_to_mesh_facet.resize(primitive_count);
    }

    if (primitive_types.edge_lines) {
        total_index_count += mesh_info.index_count_edge_lines;
        allocate_index_range(Primitive_type::lines, mesh_info.index_count_edge_lines, buffer_mesh.edge_line_indices);
    }

    if (primitive_types.corner_points) {
        total_index_count += mesh_info.index_count_corner_points;
        allocate_index_range(Primitive_type::points, mesh_info.index_count_corner_points, buffer_mesh.corner_point_indices);
    }

    if (primitive_types.centroid_points) {
        total_index_count += mesh_info.index_count_centroid_points;
        allocate_index_range(Primitive_type::points, mesh_info.facet_count, buffer_mesh.polygon_centroid_indices);
    }
}

void Build_context_root::get_vertex_attributes()
{
    ERHE_PROFILE_FUNCTION();

    using namespace erhe::dataformat;
    vertex_attributes.position              = Vertex_attribute_info{vertex_format, Vertex_attribute_usage::position,      0};
    vertex_attributes.normal                = Vertex_attribute_info{vertex_format, Vertex_attribute_usage::normal,        0}; // content normals
    vertex_attributes.normal_smooth         = Vertex_attribute_info{vertex_format, Vertex_attribute_usage::normal,        1}; // smooth normals
    vertex_attributes.tangent               = Vertex_attribute_info{vertex_format, Vertex_attribute_usage::tangent,       0};
    vertex_attributes.bitangent             = Vertex_attribute_info{vertex_format, Vertex_attribute_usage::bitangent,     0};
    vertex_attributes.id_vec3               = Vertex_attribute_info{vertex_format, Vertex_attribute_usage::custom,        custom_attribute_id};
    vertex_attributes.aniso_control         = Vertex_attribute_info{vertex_format, Vertex_attribute_usage::custom,        custom_attribute_aniso_control};
    vertex_attributes.valency_edge_count    = Vertex_attribute_info{vertex_format, Vertex_attribute_usage::custom,        custom_attribute_valency_edge_count};
    vertex_attributes.color        .push_back(Vertex_attribute_info{vertex_format, Vertex_attribute_usage::color,         0});
    vertex_attributes.color        .push_back(Vertex_attribute_info{vertex_format, Vertex_attribute_usage::color,         1});
    vertex_attributes.texcoord     .push_back(Vertex_attribute_info{vertex_format, Vertex_attribute_usage::tex_coord,     0});
    vertex_attributes.texcoord     .push_back(Vertex_attribute_info{vertex_format, Vertex_attribute_usage::tex_coord,     1});
    vertex_attributes.joint_indices.push_back(Vertex_attribute_info{vertex_format, Vertex_attribute_usage::joint_indices, 0});
    vertex_attributes.joint_indices.push_back(Vertex_attribute_info{vertex_format, Vertex_attribute_usage::joint_indices, 1});
    vertex_attributes.joint_weights.push_back(Vertex_attribute_info{vertex_format, Vertex_attribute_usage::joint_weights, 0});
    vertex_attributes.joint_weights.push_back(Vertex_attribute_info{vertex_format, Vertex_attribute_usage::joint_weights, 1});
}

void Build_context_root::allocate_vertex_buffers()
{
    for (size_t i = 0, end = vertex_format.streams.size(); i < end; ++i) {
        const erhe::dataformat::Vertex_stream& sink_stream = vertex_format.streams[i];
        Buffer_range buffer_range = build_info.buffer_info.buffer_sink.allocate_vertex_buffer(i, total_vertex_count, sink_stream.stride);
        buffer_mesh.vertex_buffer_ranges.emplace_back(std::move(buffer_range));
    }
}

void Build_context_root::allocate_index_buffer()
{
    ERHE_VERIFY(total_index_count > 0);

    const erhe::dataformat::Format index_type     {build_info.buffer_info.index_type};
    const std::size_t              index_type_size{erhe::dataformat::get_format_size(index_type)};

    log_primitive_builder->trace(
        "allocating index buffer "
        "total_index_count = {}, index type size = {}",
        total_index_count,
        index_type_size
    );

    buffer_mesh.index_buffer_range = build_info.buffer_info.buffer_sink.allocate_index_buffer(
        total_index_count,
        index_type_size
    );
}

class Mesh_point_source : public erhe::math::Bounding_volume_source
{
public:
    Mesh_point_source(const GEO::Mesh& mesh) : m_mesh{mesh} {}

    auto get_element_count() const -> std::size_t override { return m_mesh.vertices.nb(); }
    auto get_element_point_count(const std::size_t) const -> std::size_t override { return 1;}
    auto get_point(const std::size_t element_index, const std::size_t) const -> std::optional<glm::vec3> override
    {
        const GEO::vec3f p = get_pointf(m_mesh.vertices, static_cast<GEO::index_t>(element_index));
        return to_glm_vec3(p);
    }

private:
    const GEO::Mesh& m_mesh;
};

void Build_context_root::calculate_bounding_volume()
{
    const Mesh_point_source point_source{mesh};
    erhe::math::calculate_bounding_volume(point_source, buffer_mesh.bounding_box, buffer_mesh.bounding_sphere);
}

Primitive_builder::Primitive_builder(
    Buffer_mesh&       buffer_mesh,
    const GEO::Mesh&   mesh,
    const Build_info&  build_info,
    Element_mappings&  element_mappings,
    const Normal_style normal_style
)
    : m_buffer_mesh     {buffer_mesh}
    , m_mesh            {mesh}
    , m_build_info      {build_info}
    , m_element_mappings{element_mappings}
    , m_normal_style    {normal_style}
{
}

auto Primitive_builder::build() -> bool
{
    ERHE_PROFILE_FUNCTION();

    SPDLOG_LOGGER_INFO(log_primitive_builder, "Primitive_builder::build(normal_style = {})", c_str(m_normal_style));

    Build_context build_context{
        m_buffer_mesh,
        m_mesh,
        m_build_info,
        m_element_mappings,
        m_normal_style,
    };

    if (!build_context.is_ready()) {
        return false;
    }

    const Primitive_types& primitive_types = m_build_info.primitive_types;
    if (primitive_types.fill_triangles) {
        build_context.build_polygon_fill();
    }

    if (primitive_types.edge_lines) {
        build_context.build_edge_lines();
    }

    if (primitive_types.centroid_points) {
        build_context.build_centroid_points();
    }
    return true;
}

[[nodiscard]] auto Build_context::get_attribute_writer(erhe::dataformat::Vertex_attribute_usage usage, std::size_t index) -> Vertex_buffer_writer*
{
    erhe::dataformat::Attribute_stream info = root.vertex_format.find_attribute(usage, index);
    if (info.attribute != nullptr) {
        std::size_t stream_index = info.stream - root.vertex_format.streams.data();
        return vertex_writers.at(stream_index).get();
    }
    return nullptr;
}

Build_context::Build_context(
    Buffer_mesh&       buffer_mesh,
    const GEO::Mesh&   mesh,
    const Build_info&  build_info,
    Element_mappings&  element_mappings,
    const Normal_style normal_style
)
    : root           {buffer_mesh, mesh, build_info, element_mappings}
    , normal_style   {normal_style}
    , index_writer   {*this, build_info.buffer_info.buffer_sink}
    , mesh_attributes{mesh}
{
    for (std::size_t stream_index = 0, stream_end = root.vertex_format.streams.size(); stream_index < stream_end; ++stream_index) {
        const erhe::dataformat::Vertex_stream& sink_stream = root.vertex_format.streams.at(stream_index);
        //Vertex_buffer_writer vertex_writer{*this, build_info.buffer_info.buffer_sink, stream_index, sink_stream.stride};
        vertex_writers.push_back(
            std::make_unique<Vertex_buffer_writer>(
                *this,
                build_info.buffer_info.buffer_sink,
                stream_index,
                sink_stream.stride
            )
        );
    }

    using namespace erhe::dataformat;
    attribute_writers.position           = get_attribute_writer(Vertex_attribute_usage::position);
    attribute_writers.normal             = get_attribute_writer(Vertex_attribute_usage::normal);
    attribute_writers.tangent            = get_attribute_writer(Vertex_attribute_usage::tangent);
    attribute_writers.bitangent          = get_attribute_writer(Vertex_attribute_usage::bitangent);
    attribute_writers.color_0            = get_attribute_writer(Vertex_attribute_usage::color);
    attribute_writers.texcoord_0         = get_attribute_writer(Vertex_attribute_usage::tex_coord);
    attribute_writers.joint_indices_0    = get_attribute_writer(Vertex_attribute_usage::joint_indices);
    attribute_writers.joint_weights_0    = get_attribute_writer(Vertex_attribute_usage::joint_weights);
    attribute_writers.id                 = get_attribute_writer(Vertex_attribute_usage::custom, custom_attribute_id);
    attribute_writers.aniso_control      = get_attribute_writer(Vertex_attribute_usage::custom, custom_attribute_aniso_control);
    attribute_writers.valency_edge_count = get_attribute_writer(Vertex_attribute_usage::custom, custom_attribute_valency_edge_count);

    root.calculate_bounding_volume();
}

Build_context::~Build_context() noexcept
{
    ERHE_VERIFY(vertex_buffer_index == root.total_vertex_count);
}

void Build_context::build_polygon_id()
{
    ERHE_PROFILE_FUNCTION();

    if (attribute_writers.id == nullptr) {
        return;
    }

    const glm::vec3 id_vec3 = erhe::math::vec3_from_uint(static_cast<uint32_t>(mesh_facet));
    attribute_writers.id->write(root.vertex_attributes.id_vec3, id_vec3);
}

void Build_context::build_vertex_position()
{
    ERHE_PROFILE_FUNCTION();

    v_position = get_pointf(root.mesh.vertices, mesh_vertex);

    ERHE_VERIFY(std::isfinite(v_position.x) && std::isfinite(v_position.y) && std::isfinite(v_position.z));
    attribute_writers.position->write(root.vertex_attributes.position, v_position);

    SPDLOG_LOGGER_TRACE(
        log_primitive_builder,
        "Mesh: facet {} corner {} vertex {} Vertex buffer index {} location {}",
        mesh_facet, mesh_corner, mesh_vertex, vertex_buffer_index, v_position
    );
}

auto Build_context::get_facet_normal() -> GEO::vec3f
{
    ERHE_PROFILE_FUNCTION();
    {
        const std::optional<GEO::vec3f> facet_normal = mesh_attributes.facet_normal.try_get(mesh_facet);
        if (facet_normal.has_value()) {
            ERHE_VERIFY(GEO::length2(facet_normal.value()) > 0.9f);
            return facet_normal.value();
        }
    }

    const GEO::vec3f facet_normal = GEO::normalize(mesh_facet_normalf(root.mesh, mesh_facet));
    return facet_normal;
}

/////////////////////////////

void Build_context::build_tangent_frame()
{
    v_normal = GEO::vec3f{0.0f, 1.0f, 0.0f};

    std::optional<GEO::vec3f> corner_normal = mesh_attributes.corner_normal.try_get(mesh_corner);
    std::optional<GEO::vec3f> facet_normal  = mesh_attributes.facet_normal .try_get(mesh_facet);
    std::optional<GEO::vec3f> vertex_normal = mesh_attributes.vertex_normal.try_get(mesh_vertex);

    {
        ERHE_PROFILE_SCOPE("n");
        switch (normal_style) {
            //using enum Normal_style;
            case Normal_style::none: {
                // NOTE Was fallthrough to corner_normals
                break;
            }

            case Normal_style::corner_normals: {
                v_normal = 
                    corner_normal.has_value() ? corner_normal.value() :
                    facet_normal .has_value() ? facet_normal .value() :
                    vertex_normal.has_value() ? vertex_normal.value() : get_facet_normal();
                break;
            }

            case Normal_style::point_normals: {
                v_normal =
                    vertex_normal.has_value() ? vertex_normal.value() :
                    facet_normal .has_value() ? facet_normal .value() : get_facet_normal();

                break;
            }

            case Normal_style::polygon_normals: {
                v_normal = facet_normal.has_value() ? facet_normal.value() : get_facet_normal();
                break;
            }

            default: {
                ERHE_FATAL("bad normal style");
            }
        }
    }

    v_tangent = GEO::vec4f{1.0f, 0.0f, 0.0, 1.0f};

    std::optional<GEO::vec4f> corner_tangent = mesh_attributes.corner_tangent.try_get(mesh_corner);
    std::optional<GEO::vec4f> facet_tangent  = mesh_attributes.facet_tangent .try_get(mesh_facet);
    std::optional<GEO::vec4f> vertex_tangent = mesh_attributes.vertex_tangent.try_get(mesh_vertex);

    const GEO::vec3f fallback_tangent = min_axis(v_normal);
    v_tangent =
        corner_tangent.has_value() ? corner_tangent.value() :
        facet_tangent .has_value() ? facet_tangent .value() :
        vertex_tangent.has_value() ? vertex_tangent.value() : GEO::vec4f{fallback_tangent, 1.0f};

    const GEO::vec3f v_tangent3{v_tangent};

    std::optional<GEO::vec3f> corner_bitangent = mesh_attributes.corner_bitangent.try_get(mesh_corner);
    std::optional<GEO::vec3f> facet_bitangent  = mesh_attributes.facet_bitangent .try_get(mesh_facet);
    std::optional<GEO::vec3f> vertex_bitangent = mesh_attributes.vertex_bitangent.try_get(mesh_vertex);

    v_bitangent =
        corner_bitangent.has_value() ? corner_bitangent.value() :
        facet_bitangent .has_value() ? facet_bitangent .value() :
        vertex_bitangent.has_value() ? vertex_bitangent.value() : safe_normalize_cross(v_normal, v_tangent3);

    GEO::vec3f n0{v_normal};
    GEO::vec3f t0{v_tangent3};
    GEO::vec3f b0{v_bitangent};
    GEO::vec3f n{v_normal};
    GEO::vec3f t{v_tangent3};
    GEO::vec3f b{v_bitangent};
    gram_schmidt(t0, b0, n0, t, b, n);
    v_tangent   = GEO::vec4f{t, 1.0f};
    v_bitangent = b;
}

/////////////////////////////

void Build_context::build_vertex_normal(bool do_normal, bool do_normal_smooth)
{
    ERHE_PROFILE_FUNCTION();

    /// if (!root.attributes.normal.is_valid() && !root.attributes.normal_smooth.is_valid()) {
    ///     return;
    /// }

    if (do_normal) {
        attribute_writers.normal->write(root.vertex_attributes.normal, to_glm_vec3(v_normal));
    }

    // if (features.normal_flat && root.attributes.normal_flat.is_valid()) {
    //     vertex_writer.write(root.attributes.normal_flat, polygon_normal);
    //     SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} flat polygon normal {}", point_id, corner_id, polygon_normal);
    // }
    // 
    if (do_normal_smooth) {
        ERHE_PROFILE_SCOPE("2n");
    
        std::optional<GEO::vec3f> smooth_vertex_normal = mesh_attributes.vertex_normal_smooth.try_get(mesh_vertex);
        if (smooth_vertex_normal.has_value()) {
            attribute_writers.normal->write(root.vertex_attributes.normal_smooth, to_glm_vec3(smooth_vertex_normal.value()));
        } else {
            // Smooth normals are currently used only for wide line depth bias.
            // If edge lines are not used, do not generate warning about missing smooth normals.
            if (root.build_info.primitive_types.edge_lines) {
                SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} smooth unit y normal", point_id, corner_id);
                used_fallback_smooth_normal = true;
            }
            const GEO::vec3f fallback_vertex_normal_smooth{0.0f, 1.0f, 0.0f};
            attribute_writers.normal->write(root.vertex_attributes.normal_smooth, fallback_vertex_normal_smooth);
        }
    }
}

void Build_context::build_vertex_tangent()
{
    attribute_writers.tangent->write(root.vertex_attributes.tangent, to_glm_vec4(v_tangent));
}

void Build_context::build_vertex_bitangent()
{
    attribute_writers.bitangent->write(root.vertex_attributes.bitangent, to_glm_vec3(v_bitangent));
}

void Build_context::build_vertex_texcoord(size_t usage_index)
{
    std::optional<GEO::vec2f> corner_texcoord = mesh_attributes.corner_texcoord(usage_index).try_get(mesh_corner);
    std::optional<GEO::vec2f> vertex_texcoord = mesh_attributes.vertex_texcoord(usage_index).try_get(mesh_vertex);

    GEO::vec2f texcoord = 
        corner_texcoord.has_value() ? corner_texcoord.value() :
        vertex_texcoord.has_value() ? vertex_texcoord.value() : GEO::vec2f{0.0f, 0.0f};

    attribute_writers.texcoord_0->write(root.vertex_attributes.texcoord[usage_index], texcoord);
}

void Build_context::build_vertex_joint_indices(size_t usage_index)
{
    std::optional<GEO::vec4u> vertex_joint_indices = mesh_attributes.vertex_joint_indices(usage_index).try_get(mesh_vertex);
    GEO::vec4u joint_indices = vertex_joint_indices.has_value() ? vertex_joint_indices.value() : GEO::vec4u{0, 0, 0, 0};
    attribute_writers.joint_indices_0->write(root.vertex_attributes.joint_indices[usage_index], joint_indices);
}

void Build_context::build_vertex_joint_weights(size_t usage_index)
{
    std::optional<GEO::vec4f> vertex_joint_weights = mesh_attributes.vertex_joint_weights(usage_index).try_get(mesh_vertex);
    GEO::vec4f joint_weights = vertex_joint_weights.has_value() ? vertex_joint_weights.value() : GEO::vec4f{1.0f, 0.0f, 0.0f, 0.0f};
    attribute_writers.joint_weights_0->write(root.vertex_attributes.joint_weights[usage_index], joint_weights);
}

void Build_context::build_vertex_color(size_t usage_index)
{
    const std::optional<GEO::vec4f> corner_color = mesh_attributes.corner_color(usage_index).try_get(mesh_corner);
    const std::optional<GEO::vec4f> facet_color  = mesh_attributes.facet_color (usage_index).try_get(mesh_facet);
    const std::optional<GEO::vec4f> vertex_color = mesh_attributes.vertex_color(usage_index).try_get(mesh_vertex);

    GEO::vec4f color =
        corner_color.has_value() ? corner_color.value() :
        facet_color .has_value() ? facet_color .value() :
        vertex_color.has_value() ? vertex_color.value() : root.build_info.constant_color;

    attribute_writers.color_0->write(root.vertex_attributes.color[usage_index], color);
}

void Build_context::build_vertex_aniso_control()
{
    // X is used to modulate anisotropy level:
    //   0.0 -- Anisotropic
    //   1.0 -- Isotropic when approaching texcoord (0, 0)
    // Y is used for tangent space selection/control:
    //   0.0 -- Use geometry T and B (from vertex attribute
    //   1.0 -- Use T and B derived from texcoord
    std::optional<GEO::vec2f> corner_aniso_control = mesh_attributes.corner_aniso_control.try_get(mesh_corner);
    std::optional<GEO::vec2f> facet_aniso_control  = mesh_attributes.facet_aniso_control .try_get(mesh_facet);
    std::optional<GEO::vec2f> vertex_aniso_control = mesh_attributes.vertex_aniso_control.try_get(mesh_vertex);
    GEO::vec2f aniso_control = 
        corner_aniso_control.has_value() ? corner_aniso_control.value() :
        facet_aniso_control .has_value() ? facet_aniso_control .value() :
        vertex_aniso_control.has_value() ? vertex_aniso_control.value() : GEO::vec2f{1.0f, 1.0f};

    attribute_writers.aniso_control->write(root.vertex_attributes.aniso_control, aniso_control);
}

void Build_context::build_centroid_position()
{
    if (!root.build_info.primitive_types.centroid_points) {
        return;
    }

    std::optional<GEO::vec3f> facet_centroid = mesh_attributes.facet_centroid.try_get(mesh_facet);
    GEO::vec3f position = facet_centroid.has_value() 
        ? facet_centroid.value() 
        : mesh_facet_centerf(root.mesh, mesh_facet);

    attribute_writers.position->write(root.vertex_attributes.position, position);
}

void Build_context::build_centroid_normal()
{
    if (!root.build_info.primitive_types.centroid_points) {
        return;
    }

    if (root.vertex_attributes.normal.is_valid()) {
        std::optional<GEO::vec3f> facet_normal = mesh_attributes.facet_normal.try_get(mesh_facet);
        GEO::vec3f normal = facet_normal.has_value() ? facet_normal.value() : GEO::vec3f{0.0f, 1.0f, 0.0f};
        attribute_writers.normal->write(root.vertex_attributes.normal, normal);
    }
}

void Build_context::build_valency_edge_count()
{
    // TODO
    //// //if (root.attributes.valency_edge_count.is_valid()) 
    //// //{
    //// const unsigned int vertex_valency      = static_cast<unsigned int>(root.geometry.points.at(point_id).corner_count);
    //// const unsigned int polygone_edge_count = static_cast<unsigned int>(root.geometry.polygons.at(polygon_id).corner_count);
    //// const glm::uvec2 valency_edge_count{vertex_valency, polygone_edge_count};
    //// vertex_writer.write(root.attributes.valency_edge_count, valency_edge_count);
    //// //}
}

void Build_context::build_corner_point_index()
{
    //if (root.build_info.primitive_types.corner_points) {
    index_writer.write_corner(vertex_buffer_index);
    //}
}

void Build_context::build_triangle_fill_index()
{
    if (root.build_info.primitive_types.fill_triangles) {
        if (previous_index != first_index) {
            index_writer.write_triangle(first_index, previous_index, vertex_buffer_index);
            root.element_mappings.triangle_to_mesh_facet[primitive_index] = mesh_facet;
            ++primitive_index;
        }
    }

    previous_index = vertex_buffer_index;
}

auto Build_context::is_ready() const -> bool
{
    const bool ready = 
        (root.buffer_mesh.index_buffer_range.count != 0) &&
        (root.buffer_mesh.vertex_buffer_ranges.front().count != 0);
    if (ready) {
        return true;
    }
    return false;
}

void Build_context::build_polygon_fill()
{
    ERHE_PROFILE_FUNCTION();

    if (!is_ready()) {
        return;
    }

    // TODO mesh_attributes.corner_indices needs to be setup
    //      also if edge lines are wanted.

    vertex_buffer_index = 0;

    //const bool any_normal_feature = root.build_info.format.features.normal =
    //    root.build_info.format.features.normal      ||
    //    root.build_info.format.features.normal_flat ||
    //    root.build_info.format.features.normal_smooth;

    //const Polygon_id polygon_id_end = root.geometry.get_polygon_count();
    root.element_mappings.mesh_corner_to_vertex_buffer_index.resize(root.mesh.facet_corners.nb());
    root.element_mappings.mesh_vertex_to_vertex_buffer_index.resize(root.mesh.vertices.nb());

    const bool do_polygon_id           = root.vertex_attributes.id_vec3           .is_valid();
    const bool do_vertex_position      = root.vertex_attributes.position          .is_valid();
    const bool do_vertex_normal        = root.vertex_attributes.normal            .is_valid();
    const bool do_vertex_normal_smooth = root.vertex_attributes.normal_smooth     .is_valid();
    const bool do_vertex_normal_either = do_vertex_normal || do_vertex_normal_smooth;
    const bool do_vertex_tangent       = root.vertex_attributes.tangent           .is_valid();
    const bool do_vertex_bitangent     = root.vertex_attributes.bitangent         .is_valid();
    const bool do_vertex_texcoord_0    = root.vertex_attributes.texcoord[0]       .is_valid();
    const bool do_vertex_texcoord_1    = root.vertex_attributes.texcoord[1]       .is_valid();
    const bool do_vertex_color_0       = root.vertex_attributes.color[0]          .is_valid();
    const bool do_vertex_color_1       = root.vertex_attributes.color[1]          .is_valid();
    const bool do_aniso_control        = root.vertex_attributes.aniso_control     .is_valid();
    const bool do_joint_indices_0      = root.vertex_attributes.joint_indices[0]  .is_valid();
    const bool do_joint_indices_1      = root.vertex_attributes.joint_indices[1]  .is_valid();
    const bool do_joint_weights_0      = root.vertex_attributes.joint_weights[0]  .is_valid();
    const bool do_joint_weights_1      = root.vertex_attributes.joint_weights[1]  .is_valid();
    const bool do_vertex_valency       = root.vertex_attributes.valency_edge_count.is_valid();
    const bool do_corner_points        = root.build_info.primitive_types.corner_points;
    const bool do_tangent_frame = do_vertex_normal_either || do_vertex_tangent || do_vertex_bitangent;

    for (GEO::index_t facet : root.mesh.facets) {
        mesh_facet = facet;
        ERHE_PROFILE_SCOPE("polygon");
        first_index    = vertex_buffer_index;
        previous_index = first_index;

        mesh_attributes.facet_id.set(mesh_facet, vec3_from_index(mesh_facet));

        //const Polygon_corner_id polyon_corner_id_end = polygon.first_polygon_corner_id + polygon.corner_count;
        for (GEO::index_t corner : root.mesh.facets.corners(mesh_facet)) {
            mesh_corner = corner;
            ERHE_PROFILE_SCOPE("corner");
            mesh_vertex = root.mesh.facet_corners.vertex(mesh_corner);
            ERHE_VERIFY(mesh_vertex != GEO::NO_INDEX);

            root.element_mappings.mesh_corner_to_vertex_buffer_index[mesh_corner] = vertex_buffer_index;
            root.element_mappings.mesh_vertex_to_vertex_buffer_index[mesh_vertex] = vertex_buffer_index;

            if (do_polygon_id          ) build_polygon_id          ();

            if (do_tangent_frame       ) build_tangent_frame       ();
            if (do_vertex_position     ) build_vertex_position     ();
            if (do_vertex_normal_either) build_vertex_normal       (do_vertex_normal, do_vertex_normal_smooth);
            if (do_vertex_tangent      ) build_vertex_tangent      ();
            if (do_vertex_bitangent    ) build_vertex_bitangent    ();
            if (do_vertex_texcoord_0   ) build_vertex_texcoord     (0);
            if (do_vertex_texcoord_1   ) build_vertex_texcoord     (1);
            if (do_vertex_color_0      ) build_vertex_color        (0);
            if (do_vertex_color_1      ) build_vertex_color        (1);
            if (do_aniso_control       ) build_vertex_aniso_control();
            if (do_joint_indices_0     ) build_vertex_joint_indices(0);
            if (do_joint_indices_1     ) build_vertex_joint_indices(1);
            if (do_joint_weights_0     ) build_vertex_joint_weights(0);
            if (do_joint_weights_1     ) build_vertex_joint_weights(1);
            if (do_vertex_valency      ) build_valency_edge_count  ();

            // Indices
            if (do_corner_points) build_corner_point_index();
            build_triangle_fill_index();

            for (const std::unique_ptr<Vertex_buffer_writer>& vertex_writer : vertex_writers) {
                vertex_writer->next_vertex();
            }
            ++vertex_buffer_index;
        }
    }

    if (used_fallback_smooth_normal) {
        log_primitive_builder->warn("Warning: Used fallback smooth normal");
    }
    if (used_fallback_tangent) {
        log_primitive_builder->warn("Warning: Used fallback tangent");
    }
    if (used_fallback_bitangent) {
        log_primitive_builder->warn("Warning: Used fallback bitangent");
    }
    if (used_fallback_texcoord) {
        log_primitive_builder->warn("Warning: Used fallback texcoord");
    }
}

void Build_context::build_edge_lines()
{
    if (!is_ready()) {
        return;
    }

    if (!root.build_info.primitive_types.edge_lines) {
        return;
    }

    for (GEO::index_t mesh_edge : root.mesh.edges) {
        const GEO::index_t mesh_vertex_a  = root.mesh.edges.vertex(mesh_edge, 0);
        const GEO::index_t mesh_vertex_b  = root.mesh.edges.vertex(mesh_edge, 1);
        const uint32_t     vertex_index_a = root.element_mappings.mesh_vertex_to_vertex_buffer_index[mesh_vertex_a];
        const uint32_t     vertex_index_b = root.element_mappings.mesh_vertex_to_vertex_buffer_index[mesh_vertex_b];
        index_writer.write_edge(vertex_index_a, vertex_index_b);
    }
}

void Build_context::build_centroid_points()
{
    if (!is_ready()) {
        return;
    }

    if (!root.build_info.primitive_types.centroid_points) {
        return;
    }

    for (GEO::index_t facet : root.mesh.facets) {
        mesh_facet = facet;
        build_centroid_position();
        build_centroid_normal();

        index_writer.write_centroid(vertex_buffer_index);
        for (const std::unique_ptr<Vertex_buffer_writer>& vertex_writer : vertex_writers) {
            vertex_writer->next_vertex();
        }
        ++vertex_buffer_index;
    }
}

void Build_context_root::allocate_index_range(const Primitive_type primitive_type, const std::size_t index_count, Index_range& out_range)
{
    out_range.primitive_type = primitive_type;
    out_range.first_index    = next_index_range_start;
    out_range.index_count    = index_count;
    next_index_range_start += index_count;

    // If index buffer has not yet been allocated, no check for enough room for index range
    ERHE_VERIFY(
        (buffer_mesh.index_buffer_range.count == 0) ||
        (next_index_range_start <= buffer_mesh.index_buffer_range.count)
    );
}

auto build_buffer_mesh(
    Buffer_mesh&      buffer_mesh,
    const GEO::Mesh&  source_mesh,
    const Build_info& build_info,
    Element_mappings& element_mappings,
    Normal_style      normal_style
) -> bool
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(element_mappings.triangle_to_mesh_facet.empty());
    ERHE_VERIFY(element_mappings.mesh_corner_to_vertex_buffer_index.empty());
    ERHE_VERIFY(element_mappings.mesh_vertex_to_vertex_buffer_index.empty());
    Primitive_builder builder{buffer_mesh, source_mesh, build_info, element_mappings, normal_style};
    return builder.build();
}

} // namespace erhe::primitive
