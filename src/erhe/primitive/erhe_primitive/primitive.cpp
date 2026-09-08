#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_log/log.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_buffer/ibuffer.hpp"
#include "erhe_raytrace/igeometry.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <meshoptimizer.h>

#include <cmath>
#include <cstring>

namespace erhe::primitive {

auto supports_anisotropy(Bxdf_model bxdf_model) -> bool
{
    switch (bxdf_model) {
        case Bxdf_model::unlit: return false;
        case Bxdf_model::isotropic_brdf: return false;
        case Bxdf_model::anisotropic_brdf: return true;
        case Bxdf_model::anisotropic_slope: return true;
        case Bxdf_model::anisotropic_engine_ready: return true;
        default: {
            ERHE_FATAL("Bad Bxdf_model");
        }
    }
}

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
        case Primitive_mode::solid_wireframe:   return "solid_wireframe";
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

auto c_str(const Mesh_variant variant) -> const char*
{
    switch (variant) {
        case Mesh_variant::original:  return "original";
        case Mesh_variant::optimized: return "optimized";
        default: {
            ERHE_FATAL("Bad mesh variant");
        }
    }
}

namespace {

constexpr erhe::property::Enum_entry c_bxdf_model_entries[] = {
    {"Unlit",                    static_cast<int32_t>(Bxdf_model::unlit)},
    {"Isotropic BRDF",           static_cast<int32_t>(Bxdf_model::isotropic_brdf)},
    {"Anisotropic BRDF",         static_cast<int32_t>(Bxdf_model::anisotropic_brdf)},
    {"Anisotropic Slope",        static_cast<int32_t>(Bxdf_model::anisotropic_slope)},
    {"Anisotropic Engine-Ready", static_cast<int32_t>(Bxdf_model::anisotropic_engine_ready)},
};
constexpr erhe::property::Enum_entry c_material_blending_mode_entries[] = {
    {"Opaque",      static_cast<int32_t>(Material_blending_mode::opaque)},
    {"Alpha Blend", static_cast<int32_t>(Material_blending_mode::alpha_blend)},
    {"Multiply",    static_cast<int32_t>(Material_blending_mode::multiply)},
    {"Add",         static_cast<int32_t>(Material_blending_mode::add)},
    {"Subtract",    static_cast<int32_t>(Material_blending_mode::subtract)},
    {"Screen Door", static_cast<int32_t>(Material_blending_mode::screen_door)},
    {"Alpha Test",  static_cast<int32_t>(Material_blending_mode::alpha_test)},
};
constexpr erhe::property::Enum_entry c_texgen_mode_entries[] = {
    {"Uv 0",     static_cast<int32_t>(Texgen_mode::uv0)},
    {"Uv 1",     static_cast<int32_t>(Texgen_mode::uv1)},
    {"Uv 2",     static_cast<int32_t>(Texgen_mode::uv2)},
    {"World XY", static_cast<int32_t>(Texgen_mode::world_xy)},
    {"World XZ", static_cast<int32_t>(Texgen_mode::world_xz)},
    {"World YZ", static_cast<int32_t>(Texgen_mode::world_yz)},
    {"Node XY",  static_cast<int32_t>(Texgen_mode::node_xy)},
    {"Node XZ",  static_cast<int32_t>(Texgen_mode::node_xz)},
    {"Node YZ",  static_cast<int32_t>(Texgen_mode::node_yz)},
    {"Tangent",  static_cast<int32_t>(Texgen_mode::tangent)},
};
constexpr erhe::property::Enum_entry c_normalmap_encoding_entries[] = {
    {"Right Handed RGB",      static_cast<int32_t>(Normalmap_encoding::right_handed_three_channel)},
    {"Right Handed X+Y (GA)", static_cast<int32_t>(Normalmap_encoding::right_handed_two_channel_ga)},
    {"Left Handed RGB",       static_cast<int32_t>(Normalmap_encoding::left_handed_three_channel)},
    {"Left Handed X+Y (GA)",  static_cast<int32_t>(Normalmap_encoding::left_handed_two_channel_ga)},
    {"Right Handed X+Y (RG)", static_cast<int32_t>(Normalmap_encoding::right_handed_two_channel_rg)},
    {"Left Handed X+Y (RG)",  static_cast<int32_t>(Normalmap_encoding::left_handed_two_channel_rg)},
};

} // anonymous namespace

const erhe::property::Enum_info c_bxdf_model_enum_info            {"Bxdf_model",             c_bxdf_model_entries};
const erhe::property::Enum_info c_material_blending_mode_enum_info{"Material_blending_mode", c_material_blending_mode_entries};
const erhe::property::Enum_info c_texgen_mode_enum_info           {"Texgen_mode",            c_texgen_mode_entries};
const erhe::property::Enum_info c_normalmap_encoding_enum_info    {"Normalmap_encoding",     c_normalmap_encoding_entries};

namespace {

constexpr erhe::property::Enum_entry c_texture_channel_entries[] = {
    {"Red",   static_cast<int32_t>(Texture_channel::r)},
    {"Green", static_cast<int32_t>(Texture_channel::g)},
    {"Blue",  static_cast<int32_t>(Texture_channel::b)},
    {"Alpha", static_cast<int32_t>(Texture_channel::a)},
    {"None",  static_cast<int32_t>(Texture_channel::none)},
};

} // anonymous namespace

const erhe::property::Enum_info c_texture_channel_enum_info{"Texture_channel", c_texture_channel_entries};

constexpr erhe::property::Enum_entry c_sampler_address_mode_entries[] = {
    {"Repeat",          static_cast<int32_t>(erhe::graphics::Sampler_address_mode::repeat)},
    {"Clamp to Edge",   static_cast<int32_t>(erhe::graphics::Sampler_address_mode::clamp_to_edge)},
    {"Mirrored Repeat", static_cast<int32_t>(erhe::graphics::Sampler_address_mode::mirrored_repeat)},
};
constexpr erhe::property::Enum_entry c_filter_entries[] = {
    {"Nearest", static_cast<int32_t>(erhe::graphics::Filter::nearest)},
    {"Linear",  static_cast<int32_t>(erhe::graphics::Filter::linear)},
};
constexpr erhe::property::Enum_entry c_sampler_mipmap_mode_entries[] = {
    {"Not Mipmapped", static_cast<int32_t>(erhe::graphics::Sampler_mipmap_mode::not_mipmapped)},
    {"Nearest",       static_cast<int32_t>(erhe::graphics::Sampler_mipmap_mode::nearest)},
    {"Linear",        static_cast<int32_t>(erhe::graphics::Sampler_mipmap_mode::linear)},
};
const erhe::property::Enum_info c_sampler_address_mode_enum_info{"Sampler_address_mode", c_sampler_address_mode_entries};
const erhe::property::Enum_info c_filter_enum_info              {"Filter",               c_filter_entries};
const erhe::property::Enum_info c_sampler_mipmap_mode_enum_info {"Sampler_mipmap_mode",  c_sampler_mipmap_mode_entries};

auto c_str(const Bxdf_model bxdf_model) -> const char*
{
    switch (bxdf_model) {
        case Bxdf_model::unlit                   : return "unlit";
        case Bxdf_model::isotropic_brdf          : return "isotropic_brdf";
        case Bxdf_model::anisotropic_brdf        : return "anisotropic_brdf";
        case Bxdf_model::anisotropic_slope       : return "anisotropic_slope";
        case Bxdf_model::anisotropic_engine_ready: return "anisotropic_engine_ready";
        default: { 
            ERHE_FATAL("Bad Bxdf_model");
        }
    }
}

auto c_str(const Material_blending_mode blending_mode) -> const char*
{
    switch (blending_mode) {
        case Material_blending_mode::opaque     : return "opaque";
        case Material_blending_mode::alpha_blend: return "alpha_blend";
        case Material_blending_mode::multiply   : return "multiply";
        case Material_blending_mode::add        : return "add";
        case Material_blending_mode::subtract   : return "subtract";
        case Material_blending_mode::screen_door: return "screen_door";
        case Material_blending_mode::alpha_test : return "alpha_test";
        default: { 
            ERHE_FATAL("Bad Material_blending_mode");
        }
    }
}

auto c_str(const Texgen_mode texgen_mode) -> const char*
{
    switch (texgen_mode) {
        case Texgen_mode::uv0     : return "Uv 0";
        case Texgen_mode::uv1     : return "Uv 1";
        case Texgen_mode::uv2     : return "Uv 2";
        case Texgen_mode::world_xy: return "World XY";
        case Texgen_mode::world_xz: return "World XZ";
        case Texgen_mode::world_yz: return "World YZ";
        case Texgen_mode::node_xy : return "Node XY";
        case Texgen_mode::node_xz : return "Node XZ";
        case Texgen_mode::node_yz : return "Node YZ";
        case Texgen_mode::tangent : return "Tangent";
        default: { 
            ERHE_FATAL("Bad Texgen_mode");
        }
    }
}

[[nodiscard]] auto c_str(const Normalmap_encoding normalmap_encoding) -> const char*
{
    switch (normalmap_encoding) {
        case Normalmap_encoding::right_handed_three_channel : return "Right Handed RGB";
        case Normalmap_encoding::right_handed_two_channel_ga: return "Right Handed X+Y (GA)";
        case Normalmap_encoding::left_handed_three_channel  : return "Left Handed RGB";
        case Normalmap_encoding::left_handed_two_channel_ga : return "Left Handed X+Y (GA)";
        case Normalmap_encoding::right_handed_two_channel_rg: return "Right Handed X+Y (RG)";
        case Normalmap_encoding::left_handed_two_channel_rg : return "Left Handed X+Y (RG)";
        default: {
            ERHE_FATAL("Bad Normalmap_encoding");
        }
    }
}

[[nodiscard]] auto c_str(const Texture_channel texture_channel) -> const char*
{
    switch (texture_channel) {
        case Texture_channel::r: return "Red";
        case Texture_channel::g: return "Green";
        case Texture_channel::b: return "Blue";
        case Texture_channel::a: return "Alpha";
        case Texture_channel::none: return "None";
        default: {
            ERHE_FATAL("Bad Texture_channel");
        }
    }
}

auto to_uint32(const Texture_channel texture_channel) -> uint32_t
{
    switch (texture_channel) {
        case Texture_channel::r: return 0u;
        case Texture_channel::g: return 1u;
        case Texture_channel::b: return 2u;
        case Texture_channel::a: return 3u;
        case Texture_channel::none: return 4u;
        default: {
            ERHE_FATAL("Bad Texture_channel");
        }
    }
}

auto to_uint32(const Texgen_mode texgen_mode) -> uint32_t
{
    switch (texgen_mode) {
        case Texgen_mode::uv0     : return ERHE_TEXGEN_MODE_UV0;
        case Texgen_mode::uv1     : return ERHE_TEXGEN_MODE_UV1;
        case Texgen_mode::uv2     : return ERHE_TEXGEN_MODE_UV2;
        case Texgen_mode::world_xy: return ERHE_TEXGEN_MODE_WORLD_XY;
        case Texgen_mode::world_xz: return ERHE_TEXGEN_MODE_WORLD_XZ;
        case Texgen_mode::world_yz: return ERHE_TEXGEN_MODE_WORLD_YZ;
        case Texgen_mode::node_xy : return ERHE_TEXGEN_MODE_NODE_XY;
        case Texgen_mode::node_xz : return ERHE_TEXGEN_MODE_NODE_XZ;
        case Texgen_mode::node_yz : return ERHE_TEXGEN_MODE_NODE_YZ;
        case Texgen_mode::tangent : return ERHE_TEXGEN_MODE_TANGENT;
        default: { 
            ERHE_FATAL("Bad Texgen_mode");
        }
    }
}

auto to_uint32(const Normalmap_encoding normalmap_encoding) -> uint32_t
{
    switch (normalmap_encoding) {
        case Normalmap_encoding::right_handed_three_channel : return ERHE_NORMALMAP_ENCODING_RIGHT_HANDED_THREE_CHANNEL;
        case Normalmap_encoding::right_handed_two_channel_ga: return ERHE_NORMALMAP_ENCODING_RIGHT_HANDED_TWO_CHANNEL_GA;
        case Normalmap_encoding::left_handed_three_channel  : return ERHE_NORMALMAP_ENCODING_LEFT_HANDED_THREE_CHANNEL;
        case Normalmap_encoding::left_handed_two_channel_ga : return ERHE_NORMALMAP_ENCODING_LEFT_HANDED_TWO_CHANNEL_GA;
        case Normalmap_encoding::right_handed_two_channel_rg: return ERHE_NORMALMAP_ENCODING_RIGHT_HANDED_TWO_CHANNEL_RG;
        case Normalmap_encoding::left_handed_two_channel_rg : return ERHE_NORMALMAP_ENCODING_LEFT_HANDED_TWO_CHANNEL_RG;
        default: {
            ERHE_FATAL("Bad Normalmap_encoding");
        }
    }
}


#pragma region Primitive_raytrace
Primitive_raytrace::Primitive_raytrace() = default;

Primitive_raytrace::Primitive_raytrace(const GEO::Mesh& mesh)
{
    ERHE_PROFILE_FUNCTION();

    const erhe::dataformat::Vertex_format vertex_format{
        {
            0,
            {{erhe::dataformat::Format::format_32_vec3_float, erhe::dataformat::Vertex_attribute_usage::position}}
        }
    };
    const erhe::dataformat::Vertex_stream& vertex_stream = vertex_format.streams.front();
    const std::size_t vertex_stride = vertex_stream.stride;
    const std::size_t index_stride = 4;
    const erhe::geometry::Mesh_info mesh_info = erhe::geometry::get_mesh_info(mesh);
    // Tail padding of 16 bytes is required by Embree 4 for shared buffers
    static constexpr std::size_t raytrace_buffer_tail_padding = 16;
    m_rt_vertex_buffer = std::make_shared<erhe::buffer::Cpu_buffer>("raytrace_vertex", mesh_info.vertex_count_corners * vertex_stride, raytrace_buffer_tail_padding);
    m_rt_index_buffer  = std::make_shared<erhe::buffer::Cpu_buffer>("raytrace_index",  mesh_info.index_count_fill_triangles * index_stride, raytrace_buffer_tail_padding);
    Cpu_vertex_buffer_sink vertex_buffer_sink{{m_rt_vertex_buffer.get()}};
    Cpu_index_buffer_sink index_buffer_sink{*m_rt_index_buffer.get()};
    Build_info build_info{
        .primitive_types = {
            .fill_triangles = true,
        },
        .buffer_info = {
            .normal_style       = Normal_style::corner_normals,
            .index_type         = erhe::dataformat::Format::format_32_scalar_uint,
            .vertex_format      = vertex_format,
            .vertex_buffer_sink = vertex_buffer_sink,
            .index_buffer_sink  = index_buffer_sink
        }
    };

    Element_mappings element_mappings;

    // Breadcrumb with mesh counts: the raytrace buffer-mesh build walks the
    // whole mesh and a watchdog dump should attribute a stall here (and
    // reveal an absurdly sized mesh) rather than to the previous phase.
    erhe::log::set_breadcrumb(
        fmt::format(
            "raytrace: build_buffer_mesh facets={} verts={}",
            mesh.facets.nb(), mesh.vertices.nb()
        )
    );
    build_buffer_mesh(
        m_rt_mesh,
        mesh,
        build_info,
        element_mappings,
        Normal_style::none
    );
    m_triangle_to_mesh_facet = std::move(element_mappings.triangle_to_mesh_facet);

    make_raytrace_geometry();
    m_rt_geometry->set_user_data(nullptr);
}

Primitive_raytrace::Primitive_raytrace(const erhe::math::Aabb& aabb)
    : m_is_proxy{true}
{
    ERHE_PROFILE_FUNCTION();

    constexpr std::size_t vertex_stride = 3 * sizeof(float);
    constexpr std::size_t index_stride  = 4;
    constexpr std::size_t vertex_count  = 8;
    constexpr std::size_t index_count   = 36;
    // Tail padding of 16 bytes is required by Embree 4 for shared buffers
    constexpr std::size_t raytrace_buffer_tail_padding = 16;
    m_rt_vertex_buffer = std::make_shared<erhe::buffer::Cpu_buffer>("raytrace_proxy_vertex", vertex_count * vertex_stride, raytrace_buffer_tail_padding);
    m_rt_index_buffer  = std::make_shared<erhe::buffer::Cpu_buffer>("raytrace_proxy_index",  index_count * index_stride,   raytrace_buffer_tail_padding);

    const glm::vec3 corners[vertex_count] = {
        { aabb.min.x, aabb.min.y, aabb.min.z },
        { aabb.max.x, aabb.min.y, aabb.min.z },
        { aabb.max.x, aabb.max.y, aabb.min.z },
        { aabb.min.x, aabb.max.y, aabb.min.z },
        { aabb.min.x, aabb.min.y, aabb.max.z },
        { aabb.max.x, aabb.min.y, aabb.max.z },
        { aabb.max.x, aabb.max.y, aabb.max.z },
        { aabb.min.x, aabb.max.y, aabb.max.z }
    };
    // Outward-facing winding; hit position is what proxy picking consumes,
    // orientation only affects the (approximate anyway) reported normal.
    const uint32_t indices[index_count] = {
        0, 3, 2,  0, 2, 1, // -z
        4, 5, 6,  4, 6, 7, // +z
        0, 4, 7,  0, 7, 3, // -x
        1, 2, 6,  1, 6, 5, // +x
        0, 1, 5,  0, 5, 4, // -y
        3, 7, 6,  3, 6, 2  // +y
    };
    memcpy(m_rt_vertex_buffer->get_span().data(), &corners[0], vertex_count * vertex_stride);
    memcpy(m_rt_index_buffer ->get_span().data(), &indices[0], index_count * index_stride);

    m_rt_mesh.triangle_fill_indices.primitive_type = Primitive_type::triangles;
    m_rt_mesh.triangle_fill_indices.first_index    = 0;
    m_rt_mesh.triangle_fill_indices.index_count    = index_count;
    m_rt_mesh.vertex_buffer_ranges.push_back(
        Buffer_range{
            .count        = vertex_count,
            .element_size = vertex_stride,
            .byte_offset  = 0
        }
    );
    m_rt_mesh.index_buffer_range = Buffer_range{
        .count        = index_count,
        .element_size = index_stride,
        .byte_offset  = 0
    };
    m_rt_mesh.bounding_box    = aabb;
    m_rt_mesh.bounding_sphere = erhe::math::Sphere{
        .center = aabb.center(),
        .radius = 0.5f * glm::length(aabb.diagonal())
    };

    make_raytrace_geometry();
    m_rt_geometry->set_user_data(nullptr);
}

auto Primitive_raytrace::is_proxy() const -> bool
{
    return m_is_proxy;
}

auto Primitive_raytrace::get_raytrace_mesh() const -> const Buffer_mesh&
{
    return m_rt_mesh;
}

auto Primitive_raytrace::get_raytrace_geometry() const -> const std::shared_ptr<erhe::raytrace::IGeometry>&
{
    return m_rt_geometry;
}

auto Primitive_raytrace::get_mesh_facet_from_triangle(const uint32_t triangle) const -> GEO::index_t
{
    if (m_triangle_to_mesh_facet.empty()) {
        return GEO::NO_INDEX;
    }
    ERHE_VERIFY(triangle < m_triangle_to_mesh_facet.size());
    return m_triangle_to_mesh_facet[triangle];
}

auto Primitive_raytrace::has_raytrace_triangles() const -> bool
{
    return
        m_rt_geometry &&
        m_rt_index_buffer &&
        m_rt_vertex_buffer &&
        m_rt_mesh.index_range(Primitive_mode::polygon_fill).index_count > 0;
}

void Primitive_raytrace::make_raytrace_geometry()
{
    // Distinct label for AABB proxies: the "BVH build <label> in <n> ms" log
    // line is the only visible difference between a 12-triangle proxy and a
    // real triangle BVH build, and identical labels made load-time proxy
    // builds read as real BVH work.
    m_rt_geometry = erhe::raytrace::IGeometry::create_unique(
        m_is_proxy ? "rt_geometry_proxy" : "rt_geometry",
        erhe::raytrace::Geometry_type::GEOMETRY_TYPE_TRIANGLE
    );

    ERHE_VERIFY(m_rt_mesh.vertex_buffer_ranges.size() == 1);
    const auto& vertex_buffer_range   = m_rt_mesh.vertex_buffer_ranges.front();
    const auto& index_buffer_range    = m_rt_mesh.index_buffer_range;
    const auto& triangle_fill_indices = m_rt_mesh.triangle_fill_indices;

    m_rt_geometry->set_buffer(
        erhe::raytrace::Buffer_type::BUFFER_TYPE_VERTEX,
        0, // slot
        erhe::dataformat::Format::format_32_vec3_float,
        m_rt_vertex_buffer.get(),
        vertex_buffer_range.byte_offset,
        vertex_buffer_range.element_size,
        vertex_buffer_range.count
    );

    ERHE_VERIFY(index_buffer_range.element_size == 4);
    const auto index_count    = index_buffer_range.count;
    const auto index_size     = index_buffer_range.element_size;
    const auto triangle_count = index_count / 3;
    const auto triangle_size  = index_size * 3;
    m_rt_geometry->set_buffer(
        erhe::raytrace::Buffer_type::BUFFER_TYPE_INDEX,
        0, // slot
        erhe::dataformat::Format::format_32_vec3_uint,
        m_rt_index_buffer.get(),
        index_buffer_range.byte_offset + triangle_fill_indices.first_index * index_buffer_range.element_size,
        triangle_size,
        triangle_count
    );
    //SPDLOG_LOGGER_TRACE(log_raytrace, "{}:", m_source_geometry->name);

    {
        ERHE_PROFILE_SCOPE("geometry commit");
        erhe::log::set_breadcrumb("raytrace: BVH commit");
        m_rt_geometry->commit();
    }
}

Primitive_raytrace::Primitive_raytrace(Triangle_soup& triangle_soup)
{
    ERHE_PROFILE_FUNCTION();

    const erhe::dataformat::Vertex_format vertex_format{
        {
            0,
            {{erhe::dataformat::Format::format_32_vec3_float, erhe::dataformat::Vertex_attribute_usage::position}}
        }
    };
    const erhe::dataformat::Vertex_stream& vertex_stream = vertex_format.streams.front();
    const std::size_t vertex_stride = vertex_stream.stride;
    const std::size_t index_stride = 4;
    const std::size_t vertex_count = triangle_soup.get_vertex_count();
    const std::size_t index_count = triangle_soup.get_index_count();
    m_rt_vertex_buffer = std::make_shared<erhe::buffer::Cpu_buffer>("triangle_soup_raytrace_vertex", vertex_count * vertex_stride);
    m_rt_index_buffer = std::make_shared<erhe::buffer::Cpu_buffer>("triangle_soup_raytrace_index", index_count * index_stride);
    Cpu_vertex_buffer_sink vertex_buffer_sink{{m_rt_vertex_buffer.get()}};
    Cpu_index_buffer_sink  index_buffer_sink{*m_rt_index_buffer.get()};
    const Buffer_info buffer_info{
        .normal_style       = Normal_style::corner_normals,
        .index_type         = erhe::dataformat::Format::format_32_scalar_uint,
        .vertex_format      = vertex_format,
        .vertex_buffer_sink = vertex_buffer_sink,
        .index_buffer_sink  = index_buffer_sink
    };

    std::optional<Buffer_mesh> buffer_mesh_opt = build_buffer_mesh_from_triangle_soup(triangle_soup, buffer_info);
    if (!buffer_mesh_opt.has_value()) {
        return; // TODO
    }
    m_rt_mesh = std::move(buffer_mesh_opt.value());
    m_rt_geometry = erhe::raytrace::IGeometry::create_unique(
        "triangle_soup_triangle_geometry",
        erhe::raytrace::Geometry_type::GEOMETRY_TYPE_TRIANGLE
    );
    m_rt_geometry->set_user_data(nullptr); // TODO

    ERHE_VERIFY(m_rt_mesh.vertex_buffer_ranges.size() == 1);

    const auto& vertex_buffer_range   = m_rt_mesh.vertex_buffer_ranges.front();
    const auto& index_buffer_range    = m_rt_mesh.index_buffer_range;
    const auto& triangle_fill_indices = m_rt_mesh.triangle_fill_indices;

    m_rt_geometry->set_buffer(
        erhe::raytrace::Buffer_type::BUFFER_TYPE_VERTEX,
        0, // slot
        erhe::dataformat::Format::format_32_vec3_float,
        m_rt_vertex_buffer.get(),
        vertex_buffer_range.byte_offset,
        vertex_buffer_range.element_size,
        vertex_buffer_range.count
    );

    ERHE_VERIFY(index_buffer_range.element_size == 4);
    const auto index_size     = index_buffer_range.element_size;
    const auto triangle_count = index_count / 3;
    const auto triangle_size  = index_size * 3;
    m_rt_geometry->set_buffer(
        erhe::raytrace::Buffer_type::BUFFER_TYPE_INDEX,
        0, // slot
        erhe::dataformat::Format::format_32_vec3_uint,
        m_rt_index_buffer.get(),
        index_buffer_range.byte_offset + triangle_fill_indices.first_index * index_buffer_range.element_size,
        triangle_size,
        triangle_count
    );
    //SPDLOG_LOGGER_TRACE(log_raytrace, "{}:", m_source_geometry->name);

    {
        ERHE_PROFILE_SCOPE("geometry commit");
        erhe::log::set_breadcrumb("raytrace: BVH commit");
        m_rt_geometry->commit();
    }
}

Primitive_raytrace::Primitive_raytrace(Primitive_raytrace&&) noexcept = default;

Primitive_raytrace& Primitive_raytrace::operator=(Primitive_raytrace&&) noexcept = default;

Primitive_raytrace::~Primitive_raytrace() noexcept = default;
#pragma endregion Primitive_raytrace

#pragma region Primitive_shape
Primitive_shape::Primitive_shape()
{
}

// Manual moves: the mutex is not movable (each shape keeps its own).
// Shapes are only moved during construction, never while shared.
Primitive_shape::Primitive_shape(Primitive_shape&& old) noexcept
    : m_element_mappings       {std::move(old.m_element_mappings)}
    , m_geometry               {std::move(old.m_geometry)}
    , m_triangle_soup          {std::move(old.m_triangle_soup)}
    , m_raytrace               {std::move(old.m_raytrace)}
    , m_pending_raytrace       {std::move(old.m_pending_raytrace)}
    , m_retired_proxy_raytrace {std::move(old.m_retired_proxy_raytrace)}
{
    // Carry the publish flag and clear the source's: a moved-from m_geometry
    // is null, and leaving the source flag set would publish that null to
    // lock-free readers.
    m_geometry_published.store(old.m_geometry_published.exchange(false, std::memory_order_relaxed), std::memory_order_relaxed);
}

Primitive_shape& Primitive_shape::operator=(Primitive_shape&& old) noexcept
{
    if (this != &old) {
        m_element_mappings       = std::move(old.m_element_mappings);
        m_geometry               = std::move(old.m_geometry);
        m_triangle_soup          = std::move(old.m_triangle_soup);
        m_raytrace               = std::move(old.m_raytrace);
        m_pending_raytrace       = std::move(old.m_pending_raytrace);
        m_retired_proxy_raytrace = std::move(old.m_retired_proxy_raytrace);
        m_geometry_published.store(old.m_geometry_published.exchange(false, std::memory_order_relaxed), std::memory_order_relaxed);
    }
    return *this;
}

Primitive_shape::Primitive_shape(const std::shared_ptr<erhe::geometry::Geometry>& geometry)
    : m_geometry{geometry}
{
    m_geometry_published.store(m_geometry != nullptr, std::memory_order_release);
}

Primitive_shape::Primitive_shape(const std::shared_ptr<Triangle_soup>& triangle_soup)
    : m_triangle_soup{triangle_soup}
{
}

Primitive_shape::~Primitive_shape() noexcept
{
}

auto Primitive_shape::make_geometry() -> bool
{
    const std::lock_guard<std::mutex> build_lock{m_build_mutex};
    return make_geometry_build_locked() != nullptr;
}

auto Primitive_shape::make_geometry_build_locked() -> std::shared_ptr<erhe::geometry::Geometry>
{
    // Fast path: already published. Safe without the state lock - the slot is
    // written exactly once (see m_geometry_published).
    if (m_geometry_published.load(std::memory_order_acquire)) {
        return m_geometry;
    }
    if (!m_triangle_soup) {
        return {};
    }

    // Authoritative pre-build check under the state lock. m_element_mappings
    // is a state-protected slot (commit_geometry_buffer_mesh() writes it), so
    // the preconditions are asserted here rather than under the build lock
    // alone. They cannot change between here and the publish below: a commit
    // requires a prepare, which requires a published geometry.
    {
        const std::lock_guard<std::mutex> state_lock{m_state_mutex};
        if (m_geometry_published.load(std::memory_order_relaxed)) {
            return m_geometry;
        }
        ERHE_VERIFY(m_element_mappings.triangle_to_mesh_facet.empty());
        ERHE_VERIFY(m_element_mappings.mesh_corner_to_vertex_buffer_index.empty());
    }

    // Build into locals with no state lock held: this is the multi-second
    // part, and nothing on the main thread may wait for it.
    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>();
    Element_mappings element_mappings;
    GEO::Mesh& mesh = geometry->get_mesh();

    // Geogram concurrency: mesh_from_triangle_soup (colocate) and
    // Geometry::process serialize themselves internally on
    // erhe::geometry::geogram_lock(); compute_mesh_tangents is
    // mesh-local erhe code and needs no lock.
    mesh_from_triangle_soup(*m_triangle_soup.get(), mesh, element_mappings);

    const erhe::dataformat::Attribute_stream tangent_stream = m_triangle_soup->vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::tangent);
    if (tangent_stream.attribute == nullptr) {
        erhe::geometry::compute_mesh_tangents(mesh, {.orthonormalize = false, .make_facets_flat = false, .texcoord_usage_index = 0});
    }
    geometry->process({.flags =
        erhe::geometry::Geometry::process_flag_connect                       |
        erhe::geometry::Geometry::process_flag_build_edges                   |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
        erhe::geometry::Geometry::process_flag_compute_facet_centroids
    });

    // Publish: the mappings and the geometry they index become visible
    // together, and the release store hands both to lock-free readers.
    {
        const std::lock_guard<std::mutex> state_lock{m_state_mutex};
        if (m_geometry_published.load(std::memory_order_relaxed)) {
            return m_geometry; // another path won while we built; drop ours
        }
        m_element_mappings = std::move(element_mappings);
        m_geometry         = std::move(geometry);
        m_geometry_published.store(true, std::memory_order_release);
        return m_geometry;
    }
}

auto Primitive_shape::get_geometry() -> const std::shared_ptr<erhe::geometry::Geometry>&
{
    if (!m_geometry_published.load(std::memory_order_acquire)) {
        make_geometry();
    }
    return m_geometry;
}

auto Primitive_shape::get_geometry_const() const -> const std::shared_ptr<erhe::geometry::Geometry>&
{
    if (!m_geometry_published.load(std::memory_order_acquire)) {
        static const std::shared_ptr<erhe::geometry::Geometry> empty{};
        return empty;
    }
    return m_geometry;
}

auto Primitive_shape::get_raytrace() -> Primitive_raytrace&
{
    return m_raytrace;
}

auto Primitive_shape::get_raytrace() const -> const Primitive_raytrace&
{
    return m_raytrace;
}

auto Primitive_shape::get_triangle_soup() const -> const std::shared_ptr<Triangle_soup>&
{
    return m_triangle_soup;
}

auto Primitive_shape::has_raytrace_triangles() const -> bool
{
    const std::lock_guard<std::mutex> state_lock{m_state_mutex};
    return m_raytrace.has_raytrace_triangles();
}

auto Primitive_shape::has_real_raytrace() const -> bool
{
    const std::lock_guard<std::mutex> state_lock{m_state_mutex};
    return has_real_raytrace_state_locked();
}

auto Primitive_shape::has_real_raytrace_state_locked() const -> bool
{
    return m_raytrace.has_raytrace_triangles() && !m_raytrace.is_proxy();
}

auto Primitive_shape::make_raytrace(const GEO::Mesh& mesh) -> bool
{
    const std::lock_guard<std::mutex> build_lock{m_build_mutex};
    return make_raytrace_build_locked(mesh);
}

auto Primitive_shape::make_raytrace_build_locked(const GEO::Mesh& mesh) -> bool
{
    // Build aside, install under the state lock: the BVH build is one of the
    // long steps the state lock must never be held across.
    Primitive_raytrace raytrace{mesh};
    if (!raytrace.has_raytrace_triangles()) {
        return false;
    }
    const std::lock_guard<std::mutex> state_lock{m_state_mutex};
    m_raytrace = std::move(raytrace);
    return true;
}

auto Primitive_shape::make_raytrace() -> bool
{
    const std::lock_guard<std::mutex> build_lock{m_build_mutex};

    // Ensure geometry and element mappings exists
    const std::shared_ptr<erhe::geometry::Geometry> geometry = make_geometry_build_locked();
    if (!geometry) {
        return false;
    }

    //bool has_element_mappings =
    //    !m_element_mappings.triangle_to_mesh_facet.empty() &&
    //    !m_element_mappings.mesh_corner_to_vertex_buffer_index.empty();

    return make_raytrace_build_locked(geometry->get_mesh());

    // TODO Is it possible to make raytrace only / directly from triangle soup?
    //      We would lack element mappings, is that still useful?
    //
    // if (m_geometry) {
    //     m_raytrace = Primitive_raytrace{*m_geometry.get()};
    // } else if (m_triangle_soup) {
    //     m_raytrace = Primitive_raytrace{*m_triangle_soup.get()};
    // }
}

auto Primitive_shape::make_raytrace_proxy(const erhe::math::Aabb& aabb) -> bool
{
    const std::lock_guard<std::mutex> build_lock{m_build_mutex};
    {
        const std::lock_guard<std::mutex> state_lock{m_state_mutex};
        if (m_raytrace.has_raytrace_triangles()) {
            return true;
        }
    }
    if (!aabb.is_valid()) {
        return false;
    }
    // 12 triangles, but built aside and installed under the state lock like
    // every other build step, so the sequence is the same everywhere.
    Primitive_raytrace raytrace{aabb};
    if (!raytrace.has_raytrace_triangles()) {
        return false;
    }
    const std::lock_guard<std::mutex> state_lock{m_state_mutex};
    if (m_raytrace.has_raytrace_triangles()) {
        return true; // another path won while we built; drop ours
    }
    m_raytrace = std::move(raytrace);
    return true;
}

auto Primitive_shape::prepare_real_raytrace() -> bool
{
    // The build lock is what makes this idempotent for a shape shared by many
    // meshes: the second task in blocks here, then sees the first task's
    // result in the authoritative check below and returns without rebuilding.
    const std::lock_guard<std::mutex> build_lock{m_build_mutex};
    {
        const std::lock_guard<std::mutex> state_lock{m_state_mutex};
        if (has_real_raytrace_state_locked()) {
            return true;
        }
        if (m_pending_raytrace) {
            return true; // prepared by another task, not yet committed
        }
    }
    const std::shared_ptr<erhe::geometry::Geometry> geometry = make_geometry_build_locked();
    if (!geometry) {
        return false;
    }
    // The BVH build runs with no state lock held.
    std::unique_ptr<Primitive_raytrace> pending = std::make_unique<Primitive_raytrace>(geometry->get_mesh());
    if (!pending->has_raytrace_triangles()) {
        return false;
    }
    const std::lock_guard<std::mutex> state_lock{m_state_mutex};
    // Re-check: commit_real_raytrace() runs on the main thread under the state
    // lock only, so it can have landed while we built. Installing a stale
    // second pending would later free an IGeometry that live
    // Raytrace_primitives still point at (see the retirement below).
    if (has_real_raytrace_state_locked() || m_pending_raytrace) {
        return true;
    }
    m_pending_raytrace = std::move(pending);
    return true;
}

auto Primitive_shape::commit_real_raytrace() -> bool
{
    // State lock only: the main thread's Scene_commit_queue::flush() must
    // never wait for a loader worker's build.
    const std::lock_guard<std::mutex> state_lock{m_state_mutex};
    if (!m_pending_raytrace) {
        return false;
    }
    if (m_raytrace.has_raytrace_triangles() && m_raytrace.is_proxy() && !m_retired_proxy_raytrace) {
        // Keep the proxy alive: Raytrace_primitives of meshes sharing this
        // shape may still reference its IGeometry until their own deferred
        // task refreshes them (Mesh::update_rt_primitives).
        m_retired_proxy_raytrace = std::make_unique<Primitive_raytrace>(std::move(m_raytrace));
    }
    m_raytrace = std::move(*m_pending_raytrace);
    m_pending_raytrace.reset();
    return true;
}
#pragma endregion Primitive_shape

#pragma region Primitive_render_shape
Primitive_render_shape::Primitive_render_shape(const std::shared_ptr<erhe::geometry::Geometry>& geometry)
    : Primitive_shape{geometry}
{
    // TODO if (geometry->has_corner_normals()) {
        m_normal_style = Normal_style::corner_normals;
    //} else if (geometry->has_point_normals()) {
    //    m_normal_style = Normal_style::point_normals;
    //} else if (geometry->has_polygon_normals()) {
    //    m_normal_style = Normal_style::polygon_normals;
    //} else {
    //    m_normal_style = Normal_style::none;
    //}
}

Primitive_render_shape::Primitive_render_shape(Buffer_mesh&& renderable_mesh)
    : m_normal_style   {Normal_style::corner_normals}
    , m_renderable_mesh{std::move(renderable_mesh)}
{
}

Primitive_render_shape::Primitive_render_shape(const std::shared_ptr<Triangle_soup>& triangle_soup)
    : Primitive_shape{triangle_soup}
    , m_normal_style {Normal_style::corner_normals}
{
}

Primitive_render_shape::Primitive_render_shape(const std::shared_ptr<Triangle_soup>& triangle_soup, Element_mappings&& element_mappings)
    : Primitive_shape{triangle_soup}
    , m_normal_style {Normal_style::corner_normals}
{
    // Set directly rather than through a build: no build produced these, they
    // were composed onto this soup's order from another shape's.
    m_element_mappings = std::move(element_mappings);
}

Primitive_render_shape::Primitive_render_shape(Buffer_mesh&& renderable_mesh, Element_mappings&& element_mappings)
    : m_normal_style   {Normal_style::corner_normals}
    , m_renderable_mesh{std::move(renderable_mesh)}
{
    // Set directly rather than through a build: no build produced these, they
    // were composed onto this mesh's order from the source build's.
    m_element_mappings = std::move(element_mappings);
}

auto Primitive_render_shape::has_buffer_mesh_triangles() const -> bool
{
    const std::lock_guard<std::mutex> state_lock{m_state_mutex};
    return m_renderable_mesh.index_range(Primitive_mode::polygon_fill).index_count > 0;
}

auto Primitive_render_shape::has_edge_lines() const -> bool
{
    const std::lock_guard<std::mutex> state_lock{m_state_mutex};
    return has_edge_lines_state_locked();
}

auto Primitive_render_shape::has_edge_lines_state_locked() const -> bool
{
    return m_renderable_mesh.index_range(Primitive_mode::edge_lines).index_count > 0;
}

auto Primitive_render_shape::prepare_geometry_buffer_mesh(
    const Build_info&      build_info,
    const Normal_style     normal_style,
    const std::string_view name,
    const bool             force_rebuild
) -> bool
{
    const std::lock_guard<std::mutex> build_lock{m_build_mutex};
    {
        const std::lock_guard<std::mutex> state_lock{m_state_mutex};
        if (m_pending_buffer_mesh) {
            return true; // prepared by another task, not yet committed
        }
        if (!force_rebuild && has_edge_lines_state_locked()) {
            return true; // already committed; a second task must not rebuild
        }
    }
    const std::shared_ptr<erhe::geometry::Geometry> geometry = make_geometry_build_locked();
    if (!geometry) {
        return false;
    }
    // The buffer mesh build (GPU allocation, polygon fill, edge lines) runs
    // with no state lock held.
    std::unique_ptr<Pending_buffer_mesh> pending = std::make_unique<Pending_buffer_mesh>();
    pending->normal_style = normal_style;
    const bool ok = build_buffer_mesh(
        pending->buffer_mesh,
        geometry->get_mesh(),
        build_info,
        pending->element_mappings,
        normal_style,
        &pending->optimized_shape,
        name.empty() ? geometry->get_name() : name
    );
    if (!ok) {
        return false;
    }
    const std::lock_guard<std::mutex> state_lock{m_state_mutex};
    if (m_pending_buffer_mesh || (!force_rebuild && has_edge_lines_state_locked())) {
        // Another path won while we built; drop ours. Under force_rebuild the
        // committed edge lines are exactly what is being rebuilt, so only a
        // pending build (a genuinely newer preparation) wins.
        return true;
    }
    m_pending_buffer_mesh = std::move(pending);
    return true;
}

auto Primitive_render_shape::commit_geometry_buffer_mesh(std::shared_ptr<Primitive_render_shape>& out_optimized_shape) -> bool
{
    // State lock only, and the caller must hold the item host lock: the
    // per-frame readers of get_renderable_mesh() are protected by that lock,
    // not by this one. See doc/primitive-shape-lock-split-plan.md.
    const std::lock_guard<std::mutex> state_lock{m_state_mutex};
    if (!m_pending_buffer_mesh) {
        return false;
    }
    // Mesh and mappings swap together: they describe one order, so a renderer
    // can never see one beside the other's.
    m_renderable_mesh  = std::move(m_pending_buffer_mesh->buffer_mesh);
    m_element_mappings = std::move(m_pending_buffer_mesh->element_mappings);
    m_normal_style     = m_pending_buffer_mesh->normal_style;
    // The variant belongs to the Primitive, not to this shape, so it rides the
    // same atomic swap and is handed out here for the caller to attach - which
    // it must do BEFORE the mesh is re-registered with the draw list, since
    // registration bakes the drawn variant's base_vertex and index ranges.
    // Null when optimization is off, which correctly clears any variant built
    // from the pre-finalize soup: that one describes a different build of this
    // primitive, not a reordering of the one just committed.
    out_optimized_shape = std::move(m_pending_buffer_mesh->optimized_shape);
    m_pending_buffer_mesh.reset();
    return true;
}

auto Primitive_render_shape::make_buffer_mesh(
    const Build_info&                        build_info,
    Normal_style                             normal_style,
    std::shared_ptr<Primitive_render_shape>* out_optimized_shape
) -> bool
{
    const std::lock_guard<std::mutex> build_lock{m_build_mutex};
    return make_buffer_mesh_build_locked(build_info, normal_style, out_optimized_shape);
}

auto Primitive_render_shape::make_buffer_mesh(const Buffer_info& buffer_info) -> bool
{
    const std::lock_guard<std::mutex> build_lock{m_build_mutex};
    return make_buffer_mesh_build_locked(buffer_info);
}

auto Primitive_render_shape::make_buffer_mesh_build_locked(
    const Build_info&                        build_info,
    Normal_style                             normal_style,
    std::shared_ptr<Primitive_render_shape>* out_optimized_shape
) -> bool
{
    if (!m_geometry_published.load(std::memory_order_acquire)) {
        // The overload below takes the same build lock via its own entry
        // point, so it must be called through the *_build_locked() helper.
        return make_buffer_mesh_build_locked(build_info.buffer_info);
    }
    // Build into locals and publish under the state lock: m_renderable_mesh
    // and m_element_mappings are state-protected slots.
    Buffer_mesh      buffer_mesh;
    Element_mappings element_mappings;
    const bool ok = build_buffer_mesh(
        buffer_mesh,
        m_geometry->get_mesh(),
        build_info,
        element_mappings,
        normal_style,
        out_optimized_shape,
        m_geometry->get_name()
    );
    if (!ok) {
        return false;
    }
    const std::lock_guard<std::mutex> state_lock{m_state_mutex};
    m_renderable_mesh  = std::move(buffer_mesh);
    m_element_mappings = std::move(element_mappings);
    return true;
}

auto Primitive_render_shape::make_buffer_mesh_build_locked(const Buffer_info& buffer_info) -> bool
{
    if (!m_triangle_soup) {
        return false;
    }
    std::optional<Buffer_mesh> buffer_mesh_opt = build_buffer_mesh_from_triangle_soup(*m_triangle_soup.get(), buffer_info);
    if (!buffer_mesh_opt.has_value()) {
        return false;
    }
    const std::lock_guard<std::mutex> state_lock{m_state_mutex};
    m_renderable_mesh = std::move(buffer_mesh_opt.value());
    return true;
}
#pragma endregion Primitive_render_shape

auto build_buffer_mesh_from_triangle_soup(const Triangle_soup& triangle_soup, const Buffer_info& buffer_info) -> std::optional<Buffer_mesh>
{
    // TODO Use index_type from buffer_info
    //const std::size_t  sink_vertex_stride   = buffer_info.vertex_format.stride();
    const std::size_t      source_vertex_stride  = triangle_soup.vertex_format.streams.front().stride;
    const std::size_t      vertex_count          = triangle_soup.vertex_data.size() / source_vertex_stride;
    const std::size_t      index_count           = triangle_soup.index_data.size();

    Buffer_mesh buffer_mesh;

    {
        // One atomic multi-pool allocation transaction per mesh - see
        // buffer_mesh_allocation_mutex() (primitive_builder.hpp).
        const std::lock_guard<std::mutex> allocation_lock{buffer_mesh_allocation_mutex()};

        Buffer_sink_allocation index_sink_allocation = buffer_info.index_buffer_sink.allocate_index_buffer_range(buffer_info.index_type, index_count);
        if (index_sink_allocation.range.count == 0) {
            return std::optional<Buffer_mesh>{};
        }

        buffer_mesh.triangle_fill_indices.primitive_type = Primitive_type::triangles;
        buffer_mesh.triangle_fill_indices.first_index    = 0;
        buffer_mesh.triangle_fill_indices.index_count    = index_count;
        buffer_mesh.index_buffer_range                   = index_sink_allocation.range;
        buffer_mesh.index_allocation                     = std::move(index_sink_allocation.allocation);
        buffer_mesh.vertex_input_key                     = buffer_info.vertex_input_key;

        for (std::size_t i = 0, end = buffer_info.vertex_format.streams.size(); i < end; ++i) {
            const erhe::dataformat::Vertex_stream& stream = buffer_info.vertex_format.streams.at(i);
            Buffer_sink_allocation vertex_sink_allocation = buffer_info.vertex_buffer_sink.allocate_vertex_buffer_range(stream, vertex_count);
            if (vertex_sink_allocation.range.count == 0) {
                return std::optional<Buffer_mesh>{};
            }
            buffer_mesh.vertex_buffer_ranges.emplace_back(vertex_sink_allocation.range);
            buffer_mesh.vertex_allocations.emplace_back(std::move(vertex_sink_allocation.allocation));
        }
    }

    // Lockstep invariant: the indirect draw applies a single vertexOffset
    // (computed from stream 0 in Buffer_mesh::base_vertex()) to every vertex
    // binding, so byte_offset / stride and the block index must be identical
    // for all streams of this mesh (see buffer_pool.hpp). If a pool has
    // desynced, refuse the mesh loudly instead of letting the GPU fetch
    // non-position attributes from the wrong offsets (symptom: positions
    // correct, normals / tangents / tex_coords / colors garbage or zero).
    if (!buffer_mesh.vertex_buffer_ranges.empty()) {
        const Buffer_range& range_0     = buffer_mesh.vertex_buffer_ranges.front();
        const std::size_t   base_vertex = range_0.byte_offset / range_0.element_size;
        for (std::size_t stream = 1, stream_end = buffer_mesh.vertex_buffer_ranges.size(); stream < stream_end; ++stream) {
            const Buffer_range& range = buffer_mesh.vertex_buffer_ranges[stream];
            if (
                ((range.byte_offset % range.element_size) != 0)           ||
                ((range.byte_offset / range.element_size) != base_vertex) ||
                (range.buffer_id != range_0.buffer_id)
            ) {
                log_primitive->error(
                    "build_buffer_mesh_from_triangle_soup(): vertex stream allocations out of lockstep "
                    "(stream 0: pool {} buffer {} byte_offset {} stride {}; "
                    "stream {}: pool {} buffer {} byte_offset {} stride {}) - mesh skipped",
                    range_0.pool_id, range_0.buffer_id, range_0.byte_offset, range_0.element_size,
                    stream,
                    range.pool_id, range.buffer_id, range.byte_offset, range.element_size
                );
                return std::optional<Buffer_mesh>{};
            }
        }
    }

    const Buffer_range& index_range = buffer_mesh.index_buffer_range;

    // Copy indices to buffer
    {
        std::vector<uint8_t> sink_index_data(index_count * index_range.element_size);
        memcpy(sink_index_data.data(), triangle_soup.index_data.data(), index_count * index_range.element_size);
        buffer_info.index_buffer_sink.enqueue_index_data(index_range, std::move(sink_index_data));
    }

    // Bounding volume FIRST: the sink position encoding is derived from the AABB,
    // so it has to exist before any vertex is converted. This is a self-contained
    // pass over the SOURCE positions, so moving it here costs nothing.
    const erhe::dataformat::Attribute_stream position = triangle_soup.vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::position);
    erhe::math::Point_vector_bounding_volume_source positions{vertex_count};
    if (position.attribute != nullptr) {
        for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
            const uint8_t* src_vertex_data_base = triangle_soup.vertex_data.data();
            const uint8_t* src = src_vertex_data_base + position.attribute->offset + vertex_index * position.stream->stride;
            float pos[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            erhe::dataformat::convert(src, position.attribute->format, &pos[0], erhe::dataformat::Format::format_32_vec4_float, 1.0f);
            positions.add(pos[0], pos[1], pos[2]);
        }
    }
    erhe::math::calculate_bounding_volume(positions, buffer_mesh.bounding_box, buffer_mesh.bounding_sphere);

    // UV ranges of the affine texcoord channels, for the same reason and in the
    // same order as the bounding volume above: the sink encoding is derived from
    // them, so they have to exist before any vertex is converted. Computed from
    // whichever soup this call was handed - the source soup for a base build, the
    // optimized soup for the optimized re-run - so each Buffer_mesh carries the
    // ranges its own vertices were encoded against.
    for (std::size_t channel = 0; channel < affine_texcoord_channel_count; ++channel) {
        const erhe::dataformat::Attribute_stream source_texcoord = triangle_soup.vertex_format.find_attribute(
            erhe::dataformat::Vertex_attribute_usage::tex_coord, static_cast<unsigned int>(channel)
        );
        if (source_texcoord.attribute == nullptr) {
            continue;
        }
        Texcoord_range& range = buffer_mesh.texcoord_ranges[channel];
        for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
            const uint8_t* src = triangle_soup.vertex_data.data() +
                source_texcoord.attribute->offset +
                vertex_index * source_texcoord.stream->stride;
            float uv[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            erhe::dataformat::convert(src, source_texcoord.attribute->format, &uv[0], erhe::dataformat::Format::format_32_vec4_float, 1.0f);
            if (!std::isfinite(uv[0]) || !std::isfinite(uv[1])) {
                continue;
            }
            range.add(glm::vec2{uv[0], uv[1]});
        }
    }
    const Texcoord_quantization texcoord_quantization = get_texcoord_quantization(buffer_mesh);

    // Sink encoding. Gated on the SINK format, not the source: this function is
    // also called with a local float3-only format by Primitive_raytrace, whose
    // Cpu_buffer feeds the CPU BVH backends and must stay unquantized.
    const erhe::dataformat::Vertex_position_encoding sink_position_encoding =
        erhe::dataformat::get_vertex_position_encoding(&buffer_info.vertex_format);
    glm::vec3 position_encode_center   {0.0f, 0.0f, 0.0f};
    glm::vec3 position_encode_inv_scale{1.0f, 1.0f, 1.0f};
    if ((sink_position_encoding != erhe::dataformat::Vertex_position_encoding::passthrough) && (buffer_mesh.bounding_box.is_valid())) {
        // Same affine as Build_context and get_position_quantization().
        constexpr float epsilon = 1e-6f;
        const glm::vec3 half_extent = 0.5f * buffer_mesh.bounding_box.diagonal();
        const glm::vec3 scale       = glm::max(half_extent, glm::vec3{epsilon});
        position_encode_center    = buffer_mesh.bounding_box.center();
        position_encode_inv_scale = glm::vec3{1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z};
    }

    // Copy and convert vertices to buffer
    for (size_t stream_index = 0, stream_end = buffer_info.vertex_format.streams.size(); stream_index < stream_end; ++stream_index) {
        const erhe::dataformat::Vertex_stream& sink_stream = buffer_info.vertex_format.streams[stream_index];
        std::vector<uint8_t> sink_vertex_data(vertex_count * sink_stream.stride);
        const std::vector<erhe::dataformat::Vertex_attribute>& attributes = sink_stream.attributes;
        uint8_t* sink_vertex_data_base = sink_vertex_data.data();
        const uint8_t* src_vertex_data_base = triangle_soup.vertex_data.data();
        for (std::size_t attribute_index = 0, attribute_index_end = attributes.size(); attribute_index < attribute_index_end; ++attribute_index) {
            const erhe::dataformat::Vertex_attribute& sink_attribute = attributes[attribute_index];
            const erhe::dataformat::Attribute_stream src = triangle_soup.vertex_format.find_attribute(
                sink_attribute.usage_type,
                static_cast<unsigned int>(sink_attribute.usage_index)
            );
            uint8_t* sink_attribute_base = sink_vertex_data_base + sink_attribute.offset;
            // The position attribute of a quantized sink needs the AABB pack applied
            // before conversion. Note convert()'s format_16_vec3_snorm sink case
            // ASSERTS on out-of-range input rather than clamping (unlike write_low3(),
            // which the Primitive_builder path uses), so an unencoded position here is
            // a fatal abort, not a silent squash.
            const bool encode_position =
                (sink_position_encoding != erhe::dataformat::Vertex_position_encoding::passthrough) &&
                (sink_attribute.usage_type == erhe::dataformat::Vertex_attribute_usage::position) &&
                (sink_attribute.usage_index == 0);
            // Loop-invariant, so check it here rather than per vertex.
            // get_vertex_position_encoding() reports snorm16x3_aabb for no other
            // position format, which is what lets the encode branch below write
            // its snorm16 triple straight into the sink.
            ERHE_VERIFY(!encode_position || (sink_attribute.format == erhe::dataformat::Format::format_16_vec3_snorm));
            // Texcoord channels 0 and 1 of a quantized sink are normalized into
            // this mesh's own per-channel UV range - an affine encode, not a pure
            // format conversion. Channel 2 (lightmap) is excluded: it is in
            // [0, 1] by construction and convert() handles it directly.
            const bool encode_texcoord =
                (sink_attribute.usage_type  == erhe::dataformat::Vertex_attribute_usage::tex_coord)  &&
                (sink_attribute.usage_index <  affine_texcoord_channel_count)                        &&
                (sink_attribute.format      == erhe::dataformat::Format::format_16_vec2_unorm);
            // Skinning influences: sorted smallest-last with the indices
            // permuted in lockstep, so both attributes read both sources.
            const bool joint_weights_are_implicit_sum =
                erhe::dataformat::get_vertex_joint_weights_encoding(&buffer_info.vertex_format) ==
                erhe::dataformat::Vertex_joint_weights_encoding::unorm16x3_implicit_sum;
            const bool encode_joint_weights = joint_weights_are_implicit_sum &&
                (sink_attribute.usage_type == erhe::dataformat::Vertex_attribute_usage::joint_weights);
            const bool encode_joint_indices = joint_weights_are_implicit_sum &&
                (sink_attribute.usage_type == erhe::dataformat::Vertex_attribute_usage::joint_indices);
            const erhe::dataformat::Attribute_stream source_joint_indices = (encode_joint_weights || encode_joint_indices)
                ? triangle_soup.vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::joint_indices, 0)
                : erhe::dataformat::Attribute_stream{};
            const erhe::dataformat::Attribute_stream source_joint_weights = (encode_joint_weights || encode_joint_indices)
                ? triangle_soup.vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::joint_weights, 0)
                : erhe::dataformat::Attribute_stream{};
            // The tangent slot of a quantized sink carries the whole tangent
            // frame as a quaternion, so it reads TWO source attributes.
            const bool encode_tbn =
                (sink_attribute.usage_type == erhe::dataformat::Vertex_attribute_usage::tangent) &&
                (sink_attribute.format     == erhe::dataformat::Format::format_16_vec4_sint);
            const erhe::dataformat::Attribute_stream source_normal = encode_tbn
                ? triangle_soup.vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::normal, erhe::dataformat::normal_attribute)
                : erhe::dataformat::Attribute_stream{};
            const glm::length_t texcoord_channel = encode_texcoord
                ? static_cast<glm::length_t>(2 * sink_attribute.usage_index)
                : glm::length_t{0};
            const glm::vec2 texcoord_offset{
                texcoord_quantization.offset[texcoord_channel + 0],
                texcoord_quantization.offset[texcoord_channel + 1]
            };
            const glm::vec2 texcoord_inv_scale{
                1.0f / texcoord_quantization.scale[texcoord_channel + 0],
                1.0f / texcoord_quantization.scale[texcoord_channel + 1]
            };
            // Handled before the has-source split below, because these two sinks
            // read a PAIR of source attributes, not the one `src` resolved to.
            // Missing either leaves the sink zero-filled, which the decoder reads
            // as full influence from the first joint - what an unskinned vertex
            // of a skinned mesh means anyway.
            if (encode_joint_weights || encode_joint_indices) {
                if ((source_joint_indices.attribute != nullptr) && (source_joint_weights.attribute != nullptr)) {
                    for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
                        uint8_t* sink = sink_attribute_base + vertex_index * sink_stream.stride;
                        float indices[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                        float weights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                        erhe::dataformat::convert(
                            src_vertex_data_base + source_joint_indices.attribute->offset + vertex_index * source_joint_indices.stream->stride,
                            source_joint_indices.attribute->format, &indices[0], erhe::dataformat::Format::format_32_vec4_float, 1.0f
                        );
                        erhe::dataformat::convert(
                            src_vertex_data_base + source_joint_weights.attribute->offset + vertex_index * source_joint_weights.stream->stride,
                            source_joint_weights.attribute->format, &weights[0], erhe::dataformat::Format::format_32_vec4_float, 1.0f
                        );
                        const Joint_influences influences = sort_joint_influences(
                            glm::vec4{indices[0], indices[1], indices[2], indices[3]},
                            glm::vec4{weights[0], weights[1], weights[2], weights[3]}
                        );
                        if (encode_joint_indices) {
                            std::memcpy(sink, influences.indices.data(), influences.indices.size());
                        } else {
                            const std::array<uint16_t, 3> quantized = encode_implicit_sum_joint_weights(influences.weights);
                            std::memcpy(sink, quantized.data(), quantized.size() * sizeof(uint16_t));
                        }
                    }
                }
                continue;
            }
            if (src.attribute != nullptr) {
                const uint8_t* src_attribute_base = src_vertex_data_base + src.attribute->offset;
                for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
                    uint8_t* sink = sink_attribute_base + vertex_index * sink_stream.stride;
                    const uint8_t* src_data = src_attribute_base + vertex_index * source_vertex_stride;
                    if (encode_position) {
                        float source_position[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                        erhe::dataformat::convert(src_data, src.attribute->format, &source_position[0], erhe::dataformat::Format::format_32_vec4_float, 1.0f);
                        // Both clamps below propagate NaN, and meshopt_quantizeSnorm()
                        // maps NaN to the AABB's -X/-Y/-Z corner instead of failing.
                        // convert() used to catch that with its own range verify, so
                        // keep the diagnostic here - matching build_vertex_position().
                        ERHE_VERIFY(
                            std::isfinite(source_position[0]) &&
                            std::isfinite(source_position[1]) &&
                            std::isfinite(source_position[2])
                        );
                        const glm::vec3 p{source_position[0], source_position[1], source_position[2]};
                        const glm::vec3 encoded = glm::clamp(
                            (p - position_encode_center) * position_encode_inv_scale,
                            glm::vec3{-1.0f},
                            glm::vec3{ 1.0f}
                        );
                        // Quantize through meshoptimizer rather than convert():
                        // the clamp above makes meshopt_quantizeSnorm() bit-identical
                        // to erhe::dataformat::float_to_snorm16() (same 32767 scale,
                        // same round-half-away-from-zero; they can only differ on
                        // input outside [-1, 1], which the clamp and the finite check
                        // above rule out).
                        const int16_t quantized[3] = {
                            static_cast<int16_t>(meshopt_quantizeSnorm(encoded.x, 16)),
                            static_cast<int16_t>(meshopt_quantizeSnorm(encoded.y, 16)),
                            static_cast<int16_t>(meshopt_quantizeSnorm(encoded.z, 16))
                        };
                        std::memcpy(sink, &quantized[0], sizeof(quantized));
                    } else if (encode_tbn) {
                        float source_tangent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                        float source_normal_value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                        erhe::dataformat::convert(src_data, src.attribute->format, &source_tangent[0], erhe::dataformat::Format::format_32_vec4_float, 1.0f);
                        if (source_normal.attribute != nullptr) {
                            const uint8_t* normal_src = src_vertex_data_base +
                                source_normal.attribute->offset +
                                vertex_index * source_normal.stream->stride;
                            erhe::dataformat::convert(normal_src, source_normal.attribute->format, &source_normal_value[0], erhe::dataformat::Format::format_32_vec4_float, 1.0f);
                        }
                        const std::array<int16_t, 4> quantized = encode_tbn_quaternion(
                            glm::vec3{source_normal_value[0], source_normal_value[1], source_normal_value[2]},
                            glm::vec4{source_tangent[0], source_tangent[1], source_tangent[2], source_tangent[3]}
                        );
                        std::memcpy(sink, quantized.data(), quantized.size() * sizeof(int16_t));
                    } else if (encode_texcoord) {
                        float source_texcoord[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                        erhe::dataformat::convert(src_data, src.attribute->format, &source_texcoord[0], erhe::dataformat::Format::format_32_vec4_float, 1.0f);
                        // The clamp is what makes this safe for a non-finite or
                        // out-of-range UV: it lands on a range endpoint instead
                        // of wrapping. convert()'s unorm16 sink would assert.
                        const glm::vec2 normalized = glm::clamp(
                            (glm::vec2{source_texcoord[0], source_texcoord[1]} - texcoord_offset) * texcoord_inv_scale,
                            glm::vec2{0.0f},
                            glm::vec2{1.0f}
                        );
                        const uint16_t quantized[2] = {
                            static_cast<uint16_t>(meshopt_quantizeUnorm(normalized.x, 16)),
                            static_cast<uint16_t>(meshopt_quantizeUnorm(normalized.y, 16))
                        };
                        std::memcpy(sink, &quantized[0], sizeof(quantized));
                    } else {
                        erhe::dataformat::convert(src_data, src.attribute->format, sink, sink_attribute.format, 1.0f);
                    }
                }
            } else if (encode_tbn) {
                // No source tangent, but a source normal may still be there - and
                // the encoder derives an arbitrary orthogonal tangent from it,
                // which is a usable frame. Converting a zero vec4 into the sint
                // sink instead would store a quaternion nobody encoded.
                for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
                    uint8_t* sink = sink_attribute_base + vertex_index * sink_stream.stride;
                    float source_normal_value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    if (source_normal.attribute != nullptr) {
                        const uint8_t* normal_src = src_vertex_data_base +
                            source_normal.attribute->offset +
                            vertex_index * source_normal.stream->stride;
                        erhe::dataformat::convert(normal_src, source_normal.attribute->format, &source_normal_value[0], erhe::dataformat::Format::format_32_vec4_float, 1.0f);
                    }
                    const std::array<int16_t, 4> quantized = encode_tbn_quaternion(
                        glm::vec3{source_normal_value[0], source_normal_value[1], source_normal_value[2]},
                        glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}
                    );
                    std::memcpy(sink, quantized.data(), quantized.size() * sizeof(int16_t));
                }
            } else {
                const float src_data[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
                    uint8_t* sink = sink_attribute_base + vertex_index * sink_stream.stride;
                    erhe::dataformat::convert(src_data, erhe::dataformat::Format::format_32_vec4_float, sink, sink_attribute.format, 1.0f);
                }
            }
        }
        buffer_info.vertex_buffer_sink.enqueue_vertex_data(
            buffer_mesh.vertex_buffer_ranges[stream_index],
            std::move(sink_vertex_data)
        );
    }

    return buffer_mesh;
}

auto Primitive_shape::get_mesh_facet_from_triangle(const erhe::raytrace::IGeometry* geometry, const uint32_t triangle) const -> GEO::index_t
{
    if (geometry == nullptr) {
        return GEO::NO_INDEX;
    }
    // State lock only: this is a per-frame main-thread read on the hover path.
    const std::lock_guard<std::mutex> state_lock{m_state_mutex};
    if (m_raytrace.get_raytrace_geometry().get() == geometry) {
        return m_raytrace.get_mesh_facet_from_triangle(triangle);
    }
    if (m_retired_proxy_raytrace && (m_retired_proxy_raytrace->get_raytrace_geometry().get() == geometry)) {
        return m_retired_proxy_raytrace->get_mesh_facet_from_triangle(triangle);
    }
    return GEO::NO_INDEX;
}

auto Primitive_shape::get_element_mappings() const -> const Element_mappings&
{
    return m_element_mappings;
}

/////////////////////////


Primitive::Primitive() = default;

Primitive::Primitive(const Primitive&) = default;
Primitive::Primitive(Primitive&&) noexcept = default;
Primitive& Primitive::operator=(const Primitive&) = default;
Primitive& Primitive::operator=(Primitive&&) noexcept = default;

Primitive::~Primitive() noexcept = default;

Primitive::Primitive(const std::shared_ptr<Triangle_soup>& triangle_soup)
    : render_shape{std::make_shared<Primitive_render_shape>(triangle_soup)}
{
}

Primitive::Primitive(Buffer_mesh&& renderable_mesh)
    : render_shape{std::make_shared<Primitive_render_shape>(std::move(renderable_mesh))}
{
}

Primitive::Primitive(const std::shared_ptr<erhe::geometry::Geometry>& geometry)
    : render_shape{std::make_shared<Primitive_render_shape>(geometry)}
{
}

Primitive::Primitive(
    const std::shared_ptr<erhe::geometry::Geometry>& geometry,
    const Build_info&                                build_info,
    const Normal_style                               normal_style
)
    : render_shape{std::make_shared<Primitive_render_shape>(geometry)}
{
    const bool ok = make_renderable_mesh(build_info, normal_style);
    ERHE_VERIFY(ok);
}

Primitive::Primitive(
    const std::shared_ptr<erhe::geometry::Geometry>& render_geometry,
    const std::shared_ptr<erhe::geometry::Geometry>& collision_geometry
)
    : render_shape   {std::make_shared<Primitive_render_shape>(render_geometry)}
    , collision_shape{std::make_shared<Primitive_shape>(collision_geometry)}
{
    ERHE_VERIFY(render_geometry);
    ERHE_VERIFY(collision_geometry);
    ERHE_VERIFY(render_geometry != collision_geometry);
}

auto Primitive::has_renderable_triangles() const -> bool
{
    return render_shape ? render_shape->has_buffer_mesh_triangles() : false;
}

auto Primitive::has_raytrace_triangles() const -> bool
{
    if (render_shape && render_shape->has_raytrace_triangles()) {
        return true;
    }
    if (collision_shape && collision_shape->has_raytrace_triangles()) {
        return true;
    }
    return false;
}

auto Primitive::make_geometry() const -> bool
{
    if (render_shape) {
        if (render_shape->make_geometry()) {
            return true;
        }
    }
    if (collision_shape) {
        if (collision_shape->make_geometry()) {
            return true;
        }
    }
    return false;
}

auto Primitive::make_raytrace_proxy() const -> bool
{
    const std::shared_ptr<Primitive_shape> shape = get_shape_for_raytrace();
    if (!shape) {
        return false;
    }
    if (shape->has_raytrace_triangles()) {
        return true;
    }
    return shape->make_raytrace_proxy(get_bounding_box());
}

auto Primitive::has_real_raytrace() const -> bool
{
    const std::shared_ptr<Primitive_shape> shape = get_shape_for_raytrace();
    return shape && shape->has_real_raytrace();
}

auto Primitive::make_raytrace() const -> bool
{
    if (collision_shape) {
        if (collision_shape->has_raytrace_triangles()) {
            return true;
        }
        if (collision_shape->make_raytrace()) {
            return true;
        }
    }
    if (render_shape) {
        if (render_shape->has_raytrace_triangles()) {
            return true;
        }
        if (render_shape->make_raytrace()) {
            return true;
        }
    }
    return false;
}

auto Primitive::make_renderable_mesh(const Build_info& build_info, const Normal_style normal_style) -> bool
{
    std::shared_ptr<Primitive_render_shape> optimized;

    if (!render_shape) {
        return false;
    }
    if (render_shape->has_buffer_mesh_triangles()) {
        return true;
    }
    // The variant is published here, together with the build it describes, and
    // only once it is complete - make_buffer_mesh() leaves it null unless the
    // whole optimized build succeeded.
    //
    // Assigned only when there IS one. make_buffer_mesh() also has a path that
    // never reaches the geometry builder at all (no Geometry published yet, so
    // it falls back to the triangle-soup build), and an unconditional assign
    // would let that path silently drop a variant somebody else attached -
    // outside the publish/retire discipline the rest of this file protects.
    if (!render_shape->make_buffer_mesh(build_info, normal_style, &optimized)) {
        return false;
    }
    if (optimized) {
        publish_optimized_render_shape(std::move(optimized));
    }
    return true;
}

void Primitive::publish_optimized_render_shape(std::shared_ptr<Primitive_render_shape> shape)
{
    if (optimization_hold_count > 0) {
        // A live edit is addressing the base variant in place; a shape built
        // from pre-edit data must not become visible beside it. Drop the shape
        // (its GPU ranges retire through ~Buffer_mesh) and leave the variant
        // absent - the edit's end queues a fresh optimization.
        return;
    }
    optimized_render_shape = std::move(shape);
}

void Primitive::release_optimization_hold()
{
    ERHE_VERIFY(optimization_hold_count > 0);
    --optimization_hold_count;
}

auto Primitive::make_renderable_mesh(const Buffer_info& buffer_info) const -> bool
{
    if (!render_shape) {
        return false;
    }
    return render_shape->make_buffer_mesh(buffer_info);
}

auto Primitive::get_render_shape(const Mesh_variant variant) const -> const std::shared_ptr<Primitive_render_shape>&
{
    switch (variant) {
        case Mesh_variant::original:  return render_shape;
        case Mesh_variant::optimized: return optimized_render_shape;
        default: {
            ERHE_FATAL("Bad mesh variant");
        }
    }
}

auto Primitive::has_renderable_mesh(const Mesh_variant variant) const -> bool
{
    return static_cast<bool>(get_render_shape(variant));
}

auto Primitive::get_resolved_renderable_mesh(
    const Mesh_variant  preference,
    const Primitive_mode primitive_mode
) const -> std::pair<Mesh_variant, const Buffer_mesh*>
{
    // Resolve and fetch in one step. A caller that asked "is the optimized
    // build there?" and then fetched it separately could have the answer go
    // stale in between - aborting on the fetch, or silently dropping the
    // primitive from a pass.
    if ((preference == Mesh_variant::optimized) && optimized_render_shape) {
        // The optimized build carries FILL TRIANGLES ONLY - welding merges
        // corners across facets, which is exactly what edge lines, corner
        // points, centroid points and the expanded solid-wireframe copy are
        // built per-facet to avoid. Preferring it for one of those modes would
        // hand the caller an empty range, and every caller reads that as "this
        // primitive has nothing to draw" and skips it - edge lines would simply
        // disappear. So the preference only applies to a mode the optimized
        // build actually has; everything else resolves to the original, which
        // has them all.
        const Buffer_mesh& optimized_mesh = optimized_render_shape->get_renderable_mesh();
        if (optimized_mesh.index_range(primitive_mode).index_count > 0) {
            return {Mesh_variant::optimized, &optimized_mesh};
        }
    }
    if (!render_shape) {
        return {Mesh_variant::original, nullptr};
    }
    return {Mesh_variant::original, &render_shape->get_renderable_mesh()};
}

auto Primitive::get_renderable_mesh(const Mesh_variant variant) const -> const Buffer_mesh*
{
    const std::shared_ptr<Primitive_render_shape>& shape = get_render_shape(variant);
    if (!shape) {
        return nullptr;
    }
    return &shape->get_renderable_mesh();
}

static std::string Primitive__get_name__empty{};

auto Primitive::get_name() const -> std::string_view
{
    const std::shared_ptr<erhe::geometry::Geometry>& render_geometry    = render_shape ? render_shape->get_geometry_const() : std::shared_ptr<erhe::geometry::Geometry>{};
    const std::shared_ptr<erhe::geometry::Geometry>& collision_geometry = collision_shape ? collision_shape->get_geometry_const() : std::shared_ptr<erhe::geometry::Geometry>{};
    return
        render_geometry    ? render_geometry   ->get_name() :
        collision_geometry ? collision_geometry->get_name() : Primitive__get_name__empty;
}

auto Primitive::get_bounding_box() const -> erhe::math::Aabb
{
    erhe::math::Aabb bounding_box{};
    if (render_shape) {
        // Either build's bounding box would do - welding and reordering do not
        // move vertices - but the source one is always there.
        bounding_box.include(render_shape->get_renderable_mesh().bounding_box);
    }
    if (collision_shape) {
        bounding_box.include(collision_shape->get_raytrace().get_raytrace_mesh().bounding_box);
    }
    return bounding_box;
}

auto Primitive::get_shape_for_raytrace() const -> std::shared_ptr<Primitive_shape>
{
    ERHE_VERIFY(collision_shape || render_shape );
    return collision_shape ? collision_shape : render_shape;

    // TODO Should this version be considered?
    //
    // if (collision_shape && collision_shape->has_raytrace_triangles()) {
    //     return collision_shape;
    // }
    // if (render_shape && render_shape->has_raytrace_triangles()) {
    //     return render_shape;
    // }
}

} // namespace erhe::primitive
