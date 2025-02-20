#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_graphics/vertex_attribute.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_verify/verify.hpp"

#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_geometry.h>

#include <numeric>
#include <unordered_map>
#include <set>

namespace erhe::primitive {

[[nodiscard]] auto is_per_point(erhe::graphics::Vertex_attribute::Usage_type value) -> bool
{
    switch (value) {
        case erhe::graphics::Vertex_attribute::Usage_type::none         : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::automatic    : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::position     : return true;
        case erhe::graphics::Vertex_attribute::Usage_type::tangent      : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::bitangent    : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::normal       : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::color        : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::joint_indices: return true;
        case erhe::graphics::Vertex_attribute::Usage_type::joint_weights: return true;
        case erhe::graphics::Vertex_attribute::Usage_type::tex_coord    : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::id           : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::material     : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::aniso_control: return false;
        case erhe::graphics::Vertex_attribute::Usage_type::custom       : return false;
        default: return "?";
    }
}

[[nodiscard]] auto is_per_corner(erhe::graphics::Vertex_attribute::Usage_type value) -> bool
{
    switch (value) {
        case erhe::graphics::Vertex_attribute::Usage_type::none         : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::automatic    : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::position     : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::tangent      : return true;
        case erhe::graphics::Vertex_attribute::Usage_type::bitangent    : return true;
        case erhe::graphics::Vertex_attribute::Usage_type::normal       : return true;
        case erhe::graphics::Vertex_attribute::Usage_type::color        : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::joint_indices: return false;
        case erhe::graphics::Vertex_attribute::Usage_type::joint_weights: return false;
        case erhe::graphics::Vertex_attribute::Usage_type::tex_coord    : return true;
        case erhe::graphics::Vertex_attribute::Usage_type::id           : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::material     : return false;
        case erhe::graphics::Vertex_attribute::Usage_type::aniso_control: return false;
        case erhe::graphics::Vertex_attribute::Usage_type::custom       : return false;
        default: return "?";
    }
}

auto Triangle_soup::get_vertex_count() const -> std::size_t
{
    return vertex_data.size() / vertex_format.stride();
}

auto Triangle_soup::get_index_count() const -> std::size_t
{
    return index_data.size();
}

class Mesh_from_triangle_soup
{
public:
    Mesh_from_triangle_soup(
        GEO::Mesh&                         mesh,
        const Triangle_soup&               triangle_soup,
        erhe::primitive::Element_mappings& element_mappings
    )
        : m_mesh            {mesh}
        , m_attributes      {mesh}
        , m_triangle_soup   {triangle_soup}
        , m_element_mappings{element_mappings}
    {
    }

    void build()
    {
        get_used_indices();
        make_vertices();
        parse_triangles();
        parse_vertex_data();
    }

private:
    void get_used_indices()
    {
        m_used_indices = m_triangle_soup.index_data;

        // Remove duplicates
        std::sort(m_used_indices.begin(), m_used_indices.end());
        m_used_indices.erase(
            std::unique(
                m_used_indices.begin(),
                m_used_indices.end()
            ),
            m_used_indices.end()
        );

        // First and last index
        m_max_index = std::size_t{0};
        m_min_index = std::numeric_limits<std::size_t>::max();
        for (const std::size_t index : m_used_indices) {
            m_min_index = std::min(index, m_min_index);
            m_max_index = std::max(index, m_max_index);
        }

        // TODO non-indexed primitives are not currently supported
        ERHE_VERIFY(m_min_index != std::numeric_limits<std::size_t>::max());
    }
    void make_vertices()
    {
        const erhe::graphics::Vertex_attribute* position_attribute = nullptr;
        const std::vector<erhe::graphics::Vertex_attribute>& attributes = m_triangle_soup.vertex_format.get_attributes();

        std::size_t min_attribute_index = std::numeric_limits<std::size_t>::max();
        for (std::size_t i = 0, end = attributes.size(); i < end; ++i) {
            const erhe::graphics::Vertex_attribute& attribute = attributes[i];
            if (
                (attribute.usage.type == erhe::graphics::Vertex_attribute::Usage_type::position) &&
                (attribute.usage.index < min_attribute_index)
            ) {
                position_attribute = &attribute;
                min_attribute_index = attribute.usage.index;
            }
        }
        if (position_attribute == nullptr) {
            return;
        }

        // Get vertex positions and calculate bounding box
        m_vertex_positions.clear();

        const std::size_t vertex_count = m_max_index - m_min_index + 1;
        m_vertex_positions.resize(vertex_count);
        std::size_t vertex_stride = m_triangle_soup.vertex_format.stride();
        const std::uint8_t* position_base = m_triangle_soup.vertex_data.data() + position_attribute->offset;
        for (std::size_t index : m_used_indices) {
            float v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            const std::uint8_t* src = position_base + vertex_stride * index;
            std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(&v[0]);
            erhe::dataformat::convert(
                src, position_attribute->data_type, 
                dst, erhe::dataformat::Format::format_32_vec3_float, 
                1.0f
            );
            const GEO::vec3 position{v[0], v[1], v[2]};
            m_vertex_positions.at(index - m_min_index) = position;
        }

        // Sort vertices
        std::set<GEO::index_t> available_axis = { 0, 1, 2 };
        std::set<GEO::index_t> used_axis;

        double xyzmin[3];
        double xyzmax[3];
        GEO::get_bbox(m_mesh, xyzmin, xyzmax);
        const GEO::vec3 min_corner{xyzmin};
        const GEO::vec3 max_corner{xyzmax};

        const GEO::vec3 bounding_box_size0 = max_corner - min_corner;
        const GEO::index_t axis0 = max_axis_index(bounding_box_size0);
        available_axis.erase(axis0);
        used_axis.insert(axis0);
        GEO::vec3 bounding_box_size1 = bounding_box_size0;
        bounding_box_size1[axis0] = 0.0f;

        GEO::index_t axis1 = max_axis_index(bounding_box_size1);
        if (used_axis.count(axis1) > 0) {
            axis1 = *available_axis.begin();
        }
        available_axis.erase(axis1);
        used_axis.insert(axis1);

        GEO::vec3 bounding_box_size2 = bounding_box_size1;
        bounding_box_size2[axis1] = 0.0f;
        GEO::index_t axis2 = max_axis_index(bounding_box_size2);
        if (used_axis.count(axis2) > 0) {
            axis2 = * available_axis.begin();
        }
        available_axis.erase(axis2);
        used_axis.insert(axis2);

        //// TODO
        ////
        //// log_primitive->trace("Bounding box   = {}", bounding_box_size0);
        //// log_primitive->trace("Primary   axis = {}", "XYZ"[axis0]);
        //// log_primitive->trace("Secondary axis = {}", "XYZ"[axis1]);
        //// log_primitive->trace("Tertiary  axis = {}", "XYZ"[axis2]);

        m_sorted_vertex_indices.resize(vertex_count);
        m_vertex_from_index    .resize(vertex_count);

        std::fill(
            m_sorted_vertex_indices.begin(),
            m_sorted_vertex_indices.end(),
            std::numeric_limits<std::size_t>::max()
        );
        std::fill(
            m_vertex_from_index.begin(),
            m_vertex_from_index.end(),
            GEO::NO_INDEX
        );
        for (std::size_t index : m_used_indices) {
            m_sorted_vertex_indices[index - m_min_index] = index;
        }

        std::sort(
            m_sorted_vertex_indices.begin(),
            m_sorted_vertex_indices.end(),
            [this, axis0, axis1, axis2](const std::size_t lhs_index, const std::size_t rhs_index) {
                if (rhs_index == std::numeric_limits<std::size_t>::max()) {
                    return true;
                }
                const GEO::vec3 position_lhs = m_vertex_positions[lhs_index - m_min_index];
                const GEO::vec3 position_rhs = m_vertex_positions[rhs_index - m_min_index];
                if (position_lhs[axis0] != position_rhs[axis0]) {
                    return position_lhs[axis0] < position_rhs[axis0];
                }
                if (position_lhs[axis1] != position_rhs[axis1]) {
                    return position_lhs[axis1] < position_rhs[axis1];
                }
                return position_lhs[axis2] < position_rhs[axis2];
            }
        );

        // Create points for each unique vertex
        GEO::vec3 previous_position{
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()
        };
        GEO::index_t vertex{0};
        std::size_t point_share_count{0};
        for (std::size_t index : m_sorted_vertex_indices) {
            if (index == std::numeric_limits<std::size_t>::max()) {
                continue;
            }
            const GEO::vec3 position = m_vertex_positions[index - m_min_index];
            if (
                (position.x != previous_position.x) ||
                (position.y != previous_position.y) ||
                (position.z != previous_position.z)
            ) {
                vertex = m_mesh.vertices.create_vertices(1);
                previous_position = position;
            } else {
                ++point_share_count;
            }
            m_vertex_from_index[index - m_min_index] = vertex;
        }
        //log_primitive->trace(
        //    "point count = {}, point share count = {}",
        //    m_geometry.get_point_count(),
        //    point_share_count
        //);
    }
    void parse_triangles()
    {
        const std::size_t triangle_count = m_triangle_soup.index_data.size() / 3;
        auto& corner_to_vertex = m_element_mappings.mesh_corner_to_vertex_buffer_index;
        // Fill in one to one mapping
        auto& primitive_to_facet = m_element_mappings.triangle_to_mesh_facet;
        primitive_to_facet.resize(triangle_count);
        std::iota(primitive_to_facet.begin(), primitive_to_facet.end(), 0);
        m_mesh.facets.create_triangles(static_cast<GEO::index_t>(triangle_count));
        m_index_from_corner.resize(m_mesh.facet_corners.nb());
        corner_to_vertex.resize(m_mesh.facet_corners.nb());
        for (GEO::index_t facet : m_mesh.facets) {
            const uint32_t     i0 = m_triangle_soup.index_data[facet * 3 + 0];
            const uint32_t     i1 = m_triangle_soup.index_data[facet * 3 + 1];
            const uint32_t     i2 = m_triangle_soup.index_data[facet * 3 + 2];
            const GEO::index_t v0 = m_vertex_from_index.at(i0 - m_min_index);
            const GEO::index_t v1 = m_vertex_from_index.at(i1 - m_min_index);
            const GEO::index_t v2 = m_vertex_from_index.at(i2 - m_min_index);
            const GEO::index_t c0 = m_mesh.facets.corner(facet, 0);
            const GEO::index_t c1 = m_mesh.facets.corner(facet, 1);
            const GEO::index_t c2 = m_mesh.facets.corner(facet, 2);
            ERHE_VERIFY(v0 != GEO::NO_INDEX);
            ERHE_VERIFY(v1 != GEO::NO_INDEX);
            ERHE_VERIFY(v2 != GEO::NO_INDEX);
            ERHE_VERIFY(c0 != GEO::NO_INDEX);
            ERHE_VERIFY(c1 != GEO::NO_INDEX);
            ERHE_VERIFY(c2 != GEO::NO_INDEX);
            m_mesh.facets.set_vertex(facet, 0, v0);
            m_mesh.facets.set_vertex(facet, 1, v1);
            m_mesh.facets.set_vertex(facet, 2, v2);
            m_index_from_corner[c0] = i0;
            m_index_from_corner[c1] = i1;
            m_index_from_corner[c2] = i2;
            corner_to_vertex   [c0] = i0;
            corner_to_vertex   [c1] = i1;
            corner_to_vertex   [c2] = i2;
        }
    }
    void parse_vertex_data()
    {
        std::size_t vertex_stride = m_triangle_soup.vertex_format.stride();
        const std::vector<erhe::graphics::Vertex_attribute>& attributes = m_triangle_soup.vertex_format.get_attributes();
        std::size_t vertex_count = m_triangle_soup.vertex_data.size() / vertex_stride;
        const std::uint8_t* vertex_data_base = m_triangle_soup.vertex_data.data();
        for (std::size_t attribute_index = 0, end = attributes.size(); attribute_index < end; ++attribute_index) {
            const erhe::graphics::Vertex_attribute& attribute = attributes[attribute_index];
            const std::uint8_t* attribute_data_base = vertex_data_base + attribute.offset;

            if (is_per_point(attribute.usage.type)) {
                for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
                    const std::uint8_t* src = attribute_data_base + vertex_stride * vertex_index;
                    const GEO::index_t vertex = m_vertex_from_index.at(vertex_index - m_min_index);
                    ERHE_VERIFY(vertex != GEO::NO_INDEX); // TODO Is this better or worse than using if condition below?
                    if (vertex == GEO::NO_INDEX) {
                        continue;
                    }
                    switch (erhe::dataformat::get_format_kind(attribute.data_type)) {
                        case erhe::dataformat::Format_kind::format_kind_unsigned_integer: {
                            uint32_t value[4] = { 0u, 0u, 0u, 0u };
                            std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(&value[0]);
                            erhe::dataformat::convert(src, attribute.data_type, dst, erhe::dataformat::Format::format_32_vec4_uint, 1.0f);
                            put_vertex_attribute<uint32_t>(attribute, vertex, value);
                            break;
                        }
                        case erhe::dataformat::Format_kind::format_kind_signed_integer: {
                            geo_assert(false); // TODO
                            break;
                            // int32_t value[4] = { 0, 0, 0, 0 };
                            // std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(&value[0]);
                            // erhe::dataformat::convert(src, attribute.data_type, dst, erhe::dataformat::Format::format_32_vec4_sint, 1.0f);
                            // put_vertex_attribute<int32_t>(attribute, vertex, value);
                        }
                        case erhe::dataformat::Format_kind::format_kind_float: {
                            float value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                            std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(&value[0]);
                            erhe::dataformat::convert(src, attribute.data_type, dst, erhe::dataformat::Format::format_32_vec4_float, 1.0f);
                            put_vertex_attribute<float>(attribute, vertex, value);
                            break;
                        }
                        default: {
                            geo_assert(false);
                            break;
                        }
                    }
                }
            } else {
                for (GEO::index_t corner : m_mesh.facet_corners) {
                    const std::size_t vertex_index = m_index_from_corner.at(corner);
                    const std::uint8_t* src = attribute_data_base + vertex_stride * vertex_index;
                    switch (erhe::dataformat::get_format_kind(attribute.data_type)) {
                        case erhe::dataformat::Format_kind::format_kind_unsigned_integer: {
                            geo_assert(false); // TOOD
                            break;
                            // uint32_t value[4] = { 0u, 0u, 0u, 0u };
                            // std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(&value[0]);
                            // erhe::dataformat::convert(src, attribute.data_type, dst, erhe::dataformat::Format::format_32_vec4_uint, 1.0f);
                            // put_corner_attribute<uint32_t>(attribute, corner, value);
                        }
                        case erhe::dataformat::Format_kind::format_kind_signed_integer: {
                            geo_assert(false); // TOOD
                            break;
                            //int32_t value[4] = { 0, 0, 0, 0 };
                            //std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(&value[0]);
                            //erhe::dataformat::convert(src, attribute.data_type, dst, erhe::dataformat::Format::format_32_vec4_sint, 1.0f);
                            //put_corner_attribute<int32_t>(attribute, corner, value);
                        }
                        case erhe::dataformat::Format_kind::format_kind_float: {
                            float value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                            std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(&value[0]);
                            erhe::dataformat::convert(src, attribute.data_type, dst, erhe::dataformat::Format::format_32_vec4_float, 1.0f);
                            put_corner_attribute<float>(attribute, corner, value);
                            break;
                        }
                        default: {
                            geo_assert(false);
                            break;
                        }
                    }
                }
            }
        }
    }

    template <typename T>
    void put_vertex_attribute(const erhe::graphics::Vertex_attribute& attribute, GEO::index_t vertex, T value[4])
    {
        switch (attribute.usage.type) {
            case erhe::graphics::Vertex_attribute::Usage_type::position: {
                if constexpr (std::is_floating_point_v<T>) {
                    set_pointf(m_mesh.vertices, vertex, GEO::vec3f{value[0], value[1], value[2]});
                } else {
                    geo_assert(false);
                }
                break;
            }
            case erhe::graphics::Vertex_attribute::Usage_type::joint_indices: {
                if constexpr (std::is_integral_v<T>) {
                    m_attributes.vertex_joint_indices(attribute.usage.index).set(vertex, GEO::vec4u{value[0], value[1], value[2], value[3]});
                } else {
                    geo_assert(false);
                }
                break;
            }
            case erhe::graphics::Vertex_attribute::Usage_type::joint_weights: {
                if constexpr (std::is_floating_point_v<T>) {
                    m_attributes.vertex_joint_weights(attribute.usage.index).set(vertex, GEO::vec4f{value[0], value[1], value[2], value[3]});
                } else {
                    geo_assert(false);
                }
                break;
            }
            default: {
                geo_assert(false);
                break;
            }
        }
    }

    template <typename T>
    void put_corner_attribute(const erhe::graphics::Vertex_attribute& attribute, GEO::index_t corner, T value[4])
    {
        switch (attribute.usage.type) {
            case erhe::graphics::Vertex_attribute::Usage_type::normal: {
                geo_assert(attribute.usage.index == 0);
                m_attributes.corner_normal.set(corner, GEO::vec3f{value[0], value[1], value[2]});
                break; 
            }

            case erhe::graphics::Vertex_attribute::Usage_type::tangent: {
                geo_assert(attribute.usage.index == 0);
                m_attributes.corner_tangent.set(corner, GEO::vec4f{value[0], value[1], value[2], value[3]});
                break;
            }

            case erhe::graphics::Vertex_attribute::Usage_type::tex_coord: {
                m_attributes.corner_texcoord(attribute.usage.index).set(corner, GEO::vec2f{value[0], value[1]});
                break;
            }
            case erhe::graphics::Vertex_attribute::Usage_type::color: {
                m_attributes.corner_color(attribute.usage.index).set(corner, GEO::vec4f{value[0], value[1], value[2], value[3]});
                break;
            }
            default: {
                geo_assert(false);
                break;
            }
        }
    }

    GEO::Mesh&                         m_mesh;
    Mesh_attributes                    m_attributes;
    const Triangle_soup&               m_triangle_soup;
    erhe::primitive::Element_mappings& m_element_mappings;
    std::size_t                        m_min_index            {0};
    std::size_t                        m_max_index            {0};
    std::vector<uint32_t>              m_used_indices         {};
    std::vector<GEO::vec3>             m_vertex_positions     {};
    std::vector<std::size_t>           m_sorted_vertex_indices{};
    std::vector<GEO::index_t>          m_vertex_from_index    {};
    GEO::index_t                       m_corner_start         {};
    GEO::index_t                       m_corner_end           {};
    std::vector<GEO::index_t>          m_index_from_corner    {};

    GEO::Attribute<GEO::Numeric::int32> m_corner_indices;
};

void mesh_from_triangle_soup(const Triangle_soup& triangle_soup, GEO::Mesh& mesh, erhe::primitive::Element_mappings& element_mappings)
{
    ERHE_VERIFY(element_mappings.mesh_corner_to_vertex_buffer_index.empty());
    ERHE_VERIFY(element_mappings.triangle_to_mesh_facet.empty());

    Mesh_from_triangle_soup builder{mesh, triangle_soup, element_mappings};
    builder.build();
}

} // amespace erhe::primitive
