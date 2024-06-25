#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_raytrace/ibuffer.hpp"
#include "erhe_raytrace/igeometry.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::primitive
{

auto c_str(const Primitive_mode primitive_mode) -> const char*
{
    switch (primitive_mode) {
        //using enum Primitive_mode;
        case Primitive_mode::not_set:           return "not_set";
        case Primitive_mode::polygon_fill:      return "polygon_fill";
        case Primitive_mode::edge_lines:        return "edge_lines";
        case Primitive_mode::corner_points:     return "corner_points";
        case Primitive_mode::corner_normals:    return "corner_normals";
        case Primitive_mode::polygon_centroids: return "polygon_centroids";
        case Primitive_mode::count:             return "count";
        default: {
            ERHE_FATAL("Bad mesh mode");
        }
    }
}

auto c_str(const Normal_style normal_style) -> const char*
{
    switch (normal_style) {
        //using enum Normal_style;
        case Normal_style::none:            return "none";
        case Normal_style::corner_normals:  return "corner_normals";
        case Normal_style::polygon_normals: return "polygon_normals";
        case Normal_style::point_normals:   return "point_normals";
        default: {
            ERHE_FATAL("Bad mesh normal style");
        }
    }
}

Geometry_raytrace::Geometry_raytrace() = default;

Geometry_raytrace::Geometry_raytrace(erhe::geometry::Geometry& geometry)
{
    ERHE_PROFILE_FUNCTION();

    const erhe::graphics::Vertex_format vertex_format{
        erhe::graphics::Vertex_attribute::position_float3()
    };
    const std::size_t vertex_stride = vertex_format.stride();
    const std::size_t index_stride = 4;
    const erhe::geometry::Mesh_info mesh_info = geometry.get_mesh_info();
    rt_vertex_buffer = erhe::raytrace::IBuffer::create_shared(
        geometry.name + "_vertex",
        mesh_info.vertex_count_corners * vertex_stride
    );
    rt_index_buffer = erhe::raytrace::IBuffer::create_shared(
        geometry.name + "_index",
        mesh_info.index_count_fill_triangles * index_stride
    );
    erhe::primitive::Raytrace_buffer_sink buffer_sink{*rt_vertex_buffer.get(), *rt_index_buffer.get()};
    const erhe::primitive::Build_info build_info{
        .primitive_types = {
            .fill_triangles = true,
        },
        .buffer_info = {
            .normal_style  = erhe::primitive::Normal_style::corner_normals,
            .index_type    = erhe::dataformat::Format::format_32_scalar_uint,
            .vertex_format = vertex_format,
            .buffer_sink   = buffer_sink
        }
    };

    rt_geometry_mesh = make_renderable_mesh(
        geometry,
        build_info,
        erhe::primitive::Normal_style::none
    );

    rt_geometry = erhe::raytrace::IGeometry::create_unique(
        geometry.name + "_triangle_geometry",
        erhe::raytrace::Geometry_type::GEOMETRY_TYPE_TRIANGLE
    );
    rt_geometry->set_user_data(&geometry);

    const auto& vertex_buffer_range   = rt_geometry_mesh.vertex_buffer_range;
    const auto& index_buffer_range    = rt_geometry_mesh.index_buffer_range;
    const auto& triangle_fill_indices = rt_geometry_mesh.triangle_fill_indices;

    rt_geometry->set_buffer(
        erhe::raytrace::Buffer_type::BUFFER_TYPE_VERTEX,
        0, // slot
        erhe::raytrace::Format::FORMAT_FLOAT3,
        rt_vertex_buffer.get(),
        vertex_buffer_range.byte_offset,
        vertex_buffer_range.element_size,
        vertex_buffer_range.count
    );

    ERHE_VERIFY(index_buffer_range.element_size == 4);
    const auto index_count    = index_buffer_range.count;
    const auto index_size     = index_buffer_range.element_size;
    const auto triangle_count = index_count / 3;
    const auto triangle_size  = index_size * 3;
    rt_geometry->set_buffer(
        erhe::raytrace::Buffer_type::BUFFER_TYPE_INDEX,
        0, // slot
        erhe::raytrace::Format::FORMAT_UINT3,
        rt_index_buffer.get(),
        index_buffer_range.byte_offset + triangle_fill_indices.first_index * index_buffer_range.element_size,
        triangle_size,
        triangle_count
    );
    //SPDLOG_LOGGER_TRACE(log_raytrace, "{}:", m_source_geometry->name);

    {
        ERHE_PROFILE_SCOPE("geometry commit");
        rt_geometry->commit();
    }

    ////{
    ////    ERHE_PROFILE_SCOPE("create scene");
    ////    primitive.rt_scene = erhe::raytrace::IScene::create_unique(
    ////        geometry.name + "_scene"
    ////    );
    ////}
    ////
    ////primitive.rt_scene->attach(primitive.rt_geometry.get());
    ////
    ////primitive.rt_instance = erhe::raytrace::IInstance::create_unique(
    ////    geometry.name + "_instance_geometry"
    ////);
    ////
    ////primitive.rt_instance->set_scene(primitive.rt_scene.get());
    ////primitive.rt_instance->commit();
    ////primitive.rt_instance->set_user_data(this);
    ////
    ////const bool visible = is_visible();
    ////if (visible) {
    ////    m_instance->enable();
    ////} else {
    ////    m_instance->disable();
    ////}
}

Geometry_raytrace& Geometry_raytrace::operator=(Geometry_raytrace&& other) = default;

Geometry_raytrace::~Geometry_raytrace() noexcept = default;

Geometry_primitive::Geometry_primitive(const std::shared_ptr<erhe::geometry::Geometry>& geometry)
    : m_geometry{geometry}
{
}

Geometry_primitive::Geometry_primitive(Renderable_mesh&& renderable_mesh)
    : m_normal_style   {erhe::primitive::Normal_style::corner_normals}
    , m_renderable_mesh{renderable_mesh}
{
}

Geometry_primitive::Geometry_primitive(
    const std::shared_ptr<erhe::geometry::Geometry>& geometry,
    const Build_info&                                build_info,
    const Normal_style                               normal_style
)
    : m_geometry       {geometry}
    , m_normal_style   {normal_style}
    , m_renderable_mesh{make_renderable_mesh(*geometry.get(), build_info, normal_style)}
    , m_raytrace       {*geometry.get()}
{
}

Geometry_primitive::Geometry_primitive(
    const std::shared_ptr<erhe::geometry::Geometry>& render_geometry,
    const std::shared_ptr<erhe::geometry::Geometry>& collision_geometry,
    const Build_info&                                build_info,
    const Normal_style                               normal_style
)
    : m_geometry       {render_geometry}
    , m_normal_style   {normal_style}
    , m_renderable_mesh{make_renderable_mesh(*render_geometry.get(), build_info, normal_style)}
    , m_raytrace       {*collision_geometry.get()}
{
}

Geometry_primitive::Geometry_primitive(const Triangle_soup& triangle_soup, const Buffer_info& buffer_info)
{
    // TODO Use index_type from buffer_info
    const std::size_t sink_vertex_stride   = buffer_info.vertex_format.stride();
    const std::size_t source_vertex_stride = triangle_soup.vertex_format.stride();
    const std::size_t vertex_count         = triangle_soup.vertex_data.size() / source_vertex_stride;
    const std::size_t index_count          = triangle_soup.index_data.size();

    const Buffer_range index_range  = buffer_info.buffer_sink.allocate_index_buffer(index_count, 4);
    const Buffer_range vertex_range = buffer_info.buffer_sink.allocate_vertex_buffer(vertex_count, sink_vertex_stride);

    m_renderable_mesh.triangle_fill_indices.primitive_type = gl::Primitive_type::triangles;
    m_renderable_mesh.triangle_fill_indices.first_index = 0;
    m_renderable_mesh.triangle_fill_indices.index_count = index_count;
    m_renderable_mesh.index_buffer_range  = index_range;
    m_renderable_mesh.vertex_buffer_range = vertex_range;

    // Copy indices to buffer
    std::vector<uint8_t> sink_index_data(index_count * index_range.element_size);
    memcpy(sink_index_data.data(), triangle_soup.index_data.data(), index_count * index_range.element_size);
    buffer_info.buffer_sink.enqueue_index_data(index_range.byte_offset, std::move(sink_index_data));

    // Copy and convert vertices to buffer
    std::vector<uint8_t> sink_vertex_data(vertex_count * vertex_range.element_size);
    const std::vector<erhe::graphics::Vertex_attribute>& attributes = buffer_info.vertex_format.get_attributes();
    uint8_t* sink_vertex_data_base = sink_vertex_data.data();
    const uint8_t* src_vertex_data_base = triangle_soup.vertex_data.data();
    for (std::size_t attribute_index = 0, end = attributes.size(); attribute_index < end; ++attribute_index) {
        const erhe::graphics::Vertex_attribute& sink_attribute = attributes[attribute_index];
        const erhe::graphics::Vertex_attribute* src_attribute = triangle_soup.vertex_format.find_attribute_maybe(
            sink_attribute.usage.type,
            static_cast<unsigned int>(sink_attribute.usage.index)
        );
        uint8_t* sink_attribute_base = sink_vertex_data_base + sink_attribute.offset;
        if (src_attribute != nullptr) {
            const uint8_t* src_attribute_base = src_vertex_data_base + src_attribute->offset;
            for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
                uint8_t* sink = sink_attribute_base + vertex_index * sink_vertex_stride;
                const uint8_t* src = src_attribute_base + vertex_index * source_vertex_stride;
                erhe::dataformat::convert(src, src_attribute->data_type, sink, sink_attribute.data_type, 1.0f);
            }
        } else {
            const uint8_t* src = reinterpret_cast<const uint8_t*>(&sink_attribute.default_value[0]);
            for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
                uint8_t* sink = sink_attribute_base + vertex_index * sink_vertex_stride;
                erhe::dataformat::convert(src, erhe::dataformat::Format::format_32_vec4_float, sink, sink_attribute.data_type, 1.0f);
            }
        }
    }

    buffer_info.buffer_sink.enqueue_vertex_data(vertex_range.byte_offset, std::move(sink_vertex_data));

    const erhe::graphics::Vertex_attribute* position_attribute = buffer_info.vertex_format.find_attribute_maybe(erhe::graphics::Vertex_attribute::Usage_type::position);
    erhe::math::Point_vector_bounding_volume_source positions{vertex_count};
    if (position_attribute != nullptr) {
        for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
            const uint8_t* src = src_vertex_data_base + position_attribute->offset + vertex_index * source_vertex_stride;
            float position[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            erhe::dataformat::convert(src, position_attribute->data_type, &position[0], erhe::dataformat::Format::format_32_vec4_float, 1.0f);
            positions.add(position[0], position[1], position[2]);
        }
    }
    erhe::math::calculate_bounding_volume(
        positions,
        m_renderable_mesh.bounding_box,
        m_renderable_mesh.bounding_sphere
    );
}

Geometry_primitive::~Geometry_primitive() noexcept = default;

void Geometry_primitive::build_from_geometry(const Build_info& build_info, const Normal_style normal_style_in)
{
    m_normal_style    = normal_style_in;
    m_renderable_mesh = make_renderable_mesh(*m_geometry.get(), build_info, m_normal_style);
    m_raytrace        = Geometry_raytrace{*m_geometry.get()};
}

} // namespace erhe::primitive
