#include "erhe/primitive/primitive.hpp"
#include "erhe/primitive/buffer_sink.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/primitive/build_info.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/raytrace/ibuffer.hpp"
#include "erhe/raytrace/igeometry.hpp"
#include "erhe/toolkit/verify.hpp"

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

Geometry_raytrace::Geometry_raytrace(
    erhe::geometry::Geometry& geometry
)
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
            .index_type    = gl::Draw_elements_type::unsigned_int,
            .vertex_format = vertex_format,
            .buffer_sink   = buffer_sink
        }
    };

    rt_geometry_mesh = make_geometry_mesh(
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

Geometry_raytrace::~Geometry_raytrace() = default;

Geometry_primitive::Geometry_primitive(
    const std::shared_ptr<erhe::geometry::Geometry>& geometry
)
    : source_geometry{geometry}
{
}

Geometry_primitive::Geometry_primitive(
    Geometry_mesh&& gl_geometry_mesh
)
    : normal_style    {erhe::primitive::Normal_style::corner_normals}
    , gl_geometry_mesh{gl_geometry_mesh}
{
}

Geometry_primitive::Geometry_primitive(
    const std::shared_ptr<erhe::geometry::Geometry>& geometry,
    const Build_info&                                build_info,
    const Normal_style                               normal_style
)
    : source_geometry {geometry}
    , normal_style    {normal_style}
    , gl_geometry_mesh{
        make_geometry_mesh(*geometry.get(), build_info, normal_style)
    }
    , raytrace        {*geometry.get()}
{
}

Geometry_primitive::~Geometry_primitive() noexcept = default;

void Geometry_primitive::build_from_geometry(
    const Build_info&  build_info,
    const Normal_style normal_style_in
)
{
    normal_style = normal_style_in;
    gl_geometry_mesh = make_geometry_mesh(*source_geometry.get(), build_info, normal_style);
    raytrace = Geometry_raytrace{*source_geometry.get()};
}


} // namespace erhe::primitive
