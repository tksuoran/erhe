#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_graphics/vertex_attribute.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/glm.hpp>

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

class Triangle_soup_to_geometry
{
public:
    Triangle_soup_to_geometry(const Triangle_soup& triangle_soup)
        : m_triangle_soup{triangle_soup}
        , m_geometry     {std::make_shared<erhe::geometry::Geometry>()}
    {
        std::unordered_map<erhe::graphics::Vertex_attribute::Usage_type, std::size_t> attribute_max_index;
        const std::vector<erhe::graphics::Vertex_attribute>& attributes = m_triangle_soup.vertex_format.get_attributes();
        for (std::size_t i = 0, end = attributes.size(); i < end; ++i) {
            const erhe::graphics::Vertex_attribute& attribute = attributes[i];
            auto j = attribute_max_index.find(attribute.usage.type);
            if (j != attribute_max_index.end()) {
                j->second = std::max(j->second, attribute.usage.index);
            } else {
                attribute_max_index[attribute.usage.type] = attribute.usage.index;
            }
        }
        for (auto [attribute_usage, last_index] : attribute_max_index) {
            switch (attribute_usage) {
                case erhe::graphics::Vertex_attribute::Usage_type::position:      m_point_locations    .resize(last_index + 1); break;
                case erhe::graphics::Vertex_attribute::Usage_type::normal:        m_corner_normals     .resize(last_index + 1); break;
                case erhe::graphics::Vertex_attribute::Usage_type::tangent:       m_corner_tangents    .resize(last_index + 1); break;
                case erhe::graphics::Vertex_attribute::Usage_type::tex_coord:     m_corner_texcoords   .resize(last_index + 1); break;
                case erhe::graphics::Vertex_attribute::Usage_type::color:         m_corner_colors      .resize(last_index + 1); break;
                case erhe::graphics::Vertex_attribute::Usage_type::joint_indices: m_point_joint_indices.resize(last_index + 1); break;
                case erhe::graphics::Vertex_attribute::Usage_type::joint_weights: m_point_joint_weights.resize(last_index + 1); break;
                default: continue;
            }
        }
        for (std::size_t i = 0, end = attributes.size(); i < end; ++i) {
            const erhe::graphics::Vertex_attribute& attribute = attributes[i];
            switch (attribute.usage.type) {
                case erhe::graphics::Vertex_attribute::Usage_type::position:      m_point_locations    [attribute.usage.index] = m_geometry->point_attributes ().create<glm::vec3 >(erhe::geometry::c_point_locations    ); break;
                case erhe::graphics::Vertex_attribute::Usage_type::normal:        m_corner_normals     [attribute.usage.index] = m_geometry->corner_attributes().create<glm::vec3 >(erhe::geometry::c_corner_normals     ); break;
                case erhe::graphics::Vertex_attribute::Usage_type::tangent:       m_corner_tangents    [attribute.usage.index] = m_geometry->corner_attributes().create<glm::vec4 >(erhe::geometry::c_corner_tangents    ); break;
                case erhe::graphics::Vertex_attribute::Usage_type::tex_coord:     m_corner_texcoords   [attribute.usage.index] = m_geometry->corner_attributes().create<glm::vec2 >(erhe::geometry::c_corner_texcoords   ); break;
                case erhe::graphics::Vertex_attribute::Usage_type::color:         m_corner_colors      [attribute.usage.index] = m_geometry->corner_attributes().create<glm::vec4 >(erhe::geometry::c_corner_colors      ); break;
                case erhe::graphics::Vertex_attribute::Usage_type::joint_indices: m_point_joint_indices[attribute.usage.index] = m_geometry->point_attributes ().create<glm::uvec4>(erhe::geometry::c_point_joint_indices); break;
                case erhe::graphics::Vertex_attribute::Usage_type::joint_weights: m_point_joint_weights[attribute.usage.index] = m_geometry->point_attributes ().create<glm::vec4 >(erhe::geometry::c_point_joint_weights); break;
                default: continue;
            }
        }

        get_used_indices();
        make_points();
        parse_vertex_data();
        m_geometry->make_point_corners();
        m_geometry->build_edges();
    }

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
    }

    void make_points()
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

        glm::vec3 min_corner{std::numeric_limits<float>::max(),    std::numeric_limits<float>::max(),    std::numeric_limits<float>::max()};
        glm::vec3 max_corner{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
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
            const auto position = glm::vec3{v[0], v[1], v[2]};
            m_vertex_positions.at(index - m_min_index) = position;
            min_corner = glm::min(min_corner, position);
            max_corner = glm::max(max_corner, position);
        }

        // Sort vertices
        std::set<glm::vec3::length_type> available_axis = { 0, 1, 2 };
        std::set<glm::vec3::length_type> used_axis;

        const glm::vec3 bounding_box_size0 = max_corner - min_corner;
        const auto axis0 = erhe::math::max_axis_index<float>(bounding_box_size0);
        available_axis.erase(axis0);
        used_axis.insert(axis0);
        glm::vec3 bounding_box_size1 = bounding_box_size0;
        bounding_box_size1[axis0] = 0.0f;

        auto axis1 = erhe::math::max_axis_index<float>(bounding_box_size1);
        if (used_axis.count(axis1) > 0) {
            axis1 = *available_axis.begin();
        }
        available_axis.erase(axis1);
        used_axis.insert(axis1);

        glm::vec3 bounding_box_size2 = bounding_box_size1;
        bounding_box_size2[axis1] = 0.0f;
        auto axis2 = erhe::math::max_axis_index<float>(bounding_box_size2);
        if (used_axis.count(axis2) > 0) {
            axis2 = * available_axis.begin();
        }
        available_axis.erase(axis2);
        used_axis.insert(axis2);

        log_primitive->trace("Bounding box   = {}", bounding_box_size0);
        log_primitive->trace("Primary   axis = {}", "XYZ"[axis0]);
        log_primitive->trace("Secondary axis = {}", "XYZ"[axis1]);
        log_primitive->trace("Tertiary  axis = {}", "XYZ"[axis2]);

        m_sorted_vertex_indices    .resize(vertex_count);
        m_point_id_from_index .resize(vertex_count);

        std::fill(
            m_sorted_vertex_indices.begin(),
            m_sorted_vertex_indices.end(),
            std::numeric_limits<std::size_t>::max()
        );
        std::fill(
            m_point_id_from_index.begin(),
            m_point_id_from_index.end(),
            std::numeric_limits<erhe::geometry::Point_id>::max()
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
                const glm::vec3 position_lhs = m_vertex_positions[lhs_index - m_min_index];
                const glm::vec3 position_rhs = m_vertex_positions[rhs_index - m_min_index];
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
        glm::vec3 previous_position{
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()
        };
        erhe::geometry::Point_id point_id{0};
        std::size_t point_share_count{0};
        for (std::size_t index : m_sorted_vertex_indices) {
            if (index == std::numeric_limits<std::size_t>::max()) {
                continue;
            }
            const glm::vec3 position = m_vertex_positions[index - m_min_index];
            if (position != previous_position) {
                point_id = m_geometry->make_point();
                previous_position = position;
            } else {
                ++point_share_count;
            }
            m_point_id_from_index[index - m_min_index] = point_id;
        }
        log_primitive->trace(
            "point count = {}, point share count = {}",
            m_geometry->get_point_count(),
            point_share_count
        );
    }
    void parse_triangles()
    {
        const std::size_t triangle_count = m_triangle_soup.index_data.size() / 3;

        log_primitive->trace(
            "index count = {}, unique vertex count = {}, triangle count = {}",
            m_triangle_soup.index_data.size(),
            m_used_indices.size(),
            triangle_count
        );

        m_geometry->reserve_polygons(triangle_count);
        m_corner_id_start = m_geometry->m_next_corner_id;
        m_index_from_corner_id.resize(3 * m_triangle_soup.index_data.size());
        for (std::size_t i = 0; i < m_triangle_soup.index_data.size();) {
            uint32_t v0 = m_triangle_soup.index_data[i++];
            uint32_t v1 = m_triangle_soup.index_data[i++];
            uint32_t v2 = m_triangle_soup.index_data[i++];
            erhe::geometry::Point_id   p0         = m_point_id_from_index.at(v0 - m_min_index);
            erhe::geometry::Point_id   p1         = m_point_id_from_index.at(v1 - m_min_index);
            erhe::geometry::Point_id   p2         = m_point_id_from_index.at(v2 - m_min_index);
            erhe::geometry::Polygon_id polygon_id = m_geometry->make_polygon();
            erhe::geometry::Corner_id  c0         = m_geometry->make_polygon_corner(polygon_id, p0);
            erhe::geometry::Corner_id  c1         = m_geometry->make_polygon_corner(polygon_id, p1);
            erhe::geometry::Corner_id  c2         = m_geometry->make_polygon_corner(polygon_id, p2);
            m_index_from_corner_id[c0 - m_corner_id_start] = v0;
            m_index_from_corner_id[c1 - m_corner_id_start] = v1;
            m_index_from_corner_id[c2 - m_corner_id_start] = v2;
            SPDLOG_LOGGER_TRACE(log_primitive, "vertex {} corner {} for polygon {}", v0, c0, polygon_id);
            SPDLOG_LOGGER_TRACE(log_primitive, "vertex {} corner {} for polygon {}", v1, c1, polygon_id);
            SPDLOG_LOGGER_TRACE(log_primitive, "vertex {} corner {} for polygon {}", v2, c2, polygon_id);
        }
        m_corner_id_end = m_geometry->m_next_corner_id;
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

            log_primitive->trace(
                "Primitive attribute[{}]: name = {}, attribute type = {}[{}], "
                "shader type = {}, data type = {}, offset = {}",
                attribute_index,
                attribute.name,
                erhe::graphics::Vertex_attribute::desc(attribute.usage.type),
                attribute.usage.index,
                erhe::graphics::c_str(attribute.shader_type),
                erhe::dataformat::c_str(attribute.data_type),
                attribute.offset
            );

            if (is_per_point(attribute.usage.type)) {
                for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
                    const std::uint8_t* src = attribute_data_base + vertex_stride * vertex_index;
                    const auto point_id = m_point_id_from_index.at(vertex_index - m_min_index);
                    if (point_id == std::numeric_limits<erhe::geometry::Point_id>::max()) {
                        continue;
                    }
                    switch (erhe::dataformat::get_format_kind(attribute.data_type)) {
                        case erhe::dataformat::Format_kind::format_kind_unsigned_integer: {
                            uint32_t value[4] = { 0u, 0u, 0u, 0u };
                            std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(&value[0]);
                            erhe::dataformat::convert(src, attribute.data_type, dst, erhe::dataformat::Format::format_32_vec4_uint, 1.0f);
                            put_point_attribute<uint32_t>(attribute, point_id, value);
                        }
                        case erhe::dataformat::Format_kind::format_kind_signed_integer: {
                            int32_t value[4] = { 0, 0, 0, 0 };
                            std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(&value[0]);
                            erhe::dataformat::convert(src, attribute.data_type, dst, erhe::dataformat::Format::format_32_vec4_sint, 1.0f);
                            put_point_attribute<int32_t>(attribute, point_id, value);
                        }
                        case erhe::dataformat::Format_kind::format_kind_float: {
                            float value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                            std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(&value[0]);
                            erhe::dataformat::convert(src, attribute.data_type, dst, erhe::dataformat::Format::format_32_vec4_float, 1.0f);
                            put_point_attribute<float>(attribute, point_id, value);
                        }
                    }
                }
            } else {
                for (erhe::geometry::Corner_id corner_id = m_corner_id_start; corner_id != m_corner_id_end; ++corner_id) {
                    const std::size_t vertex_index = m_index_from_corner_id.at(corner_id - m_corner_id_start);
                    const std::uint8_t* src = attribute_data_base + vertex_stride * vertex_index;
                    switch (erhe::dataformat::get_format_kind(attribute.data_type)) {
                        case erhe::dataformat::Format_kind::format_kind_unsigned_integer: {
                            uint32_t value[4] = { 0u, 0u, 0u, 0u };
                            std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(&value[0]);
                            erhe::dataformat::convert(src, attribute.data_type, dst, erhe::dataformat::Format::format_32_vec4_uint, 1.0f);
                            put_corner_attribute<uint32_t>(attribute, corner_id, value);
                        }
                        case erhe::dataformat::Format_kind::format_kind_signed_integer: {
                            int32_t value[4] = { 0, 0, 0, 0 };
                            std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(&value[0]);
                            erhe::dataformat::convert(src, attribute.data_type, dst, erhe::dataformat::Format::format_32_vec4_sint, 1.0f);
                            put_corner_attribute<int32_t>(attribute, corner_id, value);
                        }
                        case erhe::dataformat::Format_kind::format_kind_float: {
                            float value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                            std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(&value[0]);
                            erhe::dataformat::convert(src, attribute.data_type, dst, erhe::dataformat::Format::format_32_vec4_float, 1.0f);
                            put_corner_attribute<float>(attribute, corner_id, value);
                        }
                    }
                }
            }
        }
    }

    template <typename T>
    void put_point_attribute(
        const erhe::graphics::Vertex_attribute& attribute,
        erhe::geometry::Point_id                point_id,
        T                                       value[4]
    )
    {
        switch (attribute.usage.type) {
            case erhe::graphics::Vertex_attribute::Usage_type::position: {
                glm::vec3 pos = glm::vec3{value[0], value[1], value[2]};
                m_point_locations[attribute.usage.index]->put(point_id, pos);
                break;
            }
            case erhe::graphics::Vertex_attribute::Usage_type::joint_indices: {
                m_point_joint_indices[attribute.usage.index]->put(point_id, glm::uvec4{value[0], value[1], value[2], value[3]});
                break;
            }
            case erhe::graphics::Vertex_attribute::Usage_type::joint_weights: {
                m_point_joint_weights[attribute.usage.index]->put(point_id, glm::vec4{value[0], value[1], value[2], value[3]});
                break;
            }
            default: {
                log_primitive->warn("Unsupported attribute {} {} {}", erhe::graphics::Vertex_attribute::desc(attribute.usage.type), attribute.usage.index, attribute.name);
                break;
            }
        }
    }

    template <typename T>
    void put_corner_attribute(
        const erhe::graphics::Vertex_attribute& attribute,
        erhe::geometry::Corner_id               corner_id,
        T                                       value[4]
    )
    {
        switch (attribute.usage.type) {
            case erhe::graphics::Vertex_attribute::Usage_type::normal: {
                glm::vec3 n = glm::vec3{value[0], value[1], value[2]};
                m_corner_normals[attribute.usage.index]->put(corner_id, n);
                break; 
            }

            case erhe::graphics::Vertex_attribute::Usage_type::tangent: {
                glm::vec4 t = glm::vec4{value[0], value[1], value[2], value[3]};
                m_corner_tangents[attribute.usage.index]->put(corner_id, t);
                break;
            }

            case erhe::graphics::Vertex_attribute::Usage_type::tex_coord: {
                m_corner_texcoords[attribute.usage.index]->put(corner_id, glm::vec2{value[0], value[1]}); break;
                break;
            }
            case erhe::graphics::Vertex_attribute::Usage_type::color: {
                m_corner_colors[attribute.usage.index]->put(corner_id, glm::vec4{value[0], value[1], value[2], value[3]});
                break;
            }
            default: {
                log_primitive->warn("Unsupported attribute {} {} {}", erhe::graphics::Vertex_attribute::desc(attribute.usage.type), attribute.usage.index, attribute.name);
                break;
            }
        }
    }

    const Triangle_soup&                      m_triangle_soup;
    std::shared_ptr<erhe::geometry::Geometry> m_geometry             {};
    std::size_t                               m_min_index            {0};
    std::size_t                               m_max_index            {0};
    std::vector<uint32_t>                     m_used_indices         {};
    std::vector<glm::vec3>                    m_vertex_positions     {};
    std::vector<std::size_t>                  m_sorted_vertex_indices{};
    std::vector<erhe::geometry::Point_id>     m_point_id_from_index  {};
    erhe::geometry::Corner_id                 m_corner_id_start      {};
    erhe::geometry::Corner_id                 m_corner_id_end        {};
    std::vector<std::size_t>                  m_index_from_corner_id {};

    std::vector<erhe::geometry::Property_map<erhe::geometry::Corner_id, glm::vec3>*> m_corner_normals     ;
    std::vector<erhe::geometry::Property_map<erhe::geometry::Corner_id, glm::vec4>*> m_corner_tangents    ;
    std::vector<erhe::geometry::Property_map<erhe::geometry::Corner_id, glm::vec4>*> m_corner_bitangents  ;
    std::vector<erhe::geometry::Property_map<erhe::geometry::Corner_id, glm::vec2>*> m_corner_texcoords   ;
    std::vector<erhe::geometry::Property_map<erhe::geometry::Corner_id, glm::vec4>*> m_corner_colors      ;
    std::vector<erhe::geometry::Property_map<erhe::geometry::Corner_id, uint32_t>* > m_corner_indices     ;
    std::vector<erhe::geometry::Property_map<erhe::geometry::Point_id, glm::vec3>* > m_point_locations    ;
    std::vector<erhe::geometry::Property_map<erhe::geometry::Point_id, glm::uvec4>*> m_point_joint_indices;
    std::vector<erhe::geometry::Property_map<erhe::geometry::Point_id, glm::vec4>* > m_point_joint_weights;
};

auto Triangle_soup::get_vertex_count() const -> std::size_t
{
    return vertex_data.size() / vertex_format.stride();
}

auto Triangle_soup::get_index_count() const -> std::size_t
{
    return index_data.size();
}

} // amespace erhe::primitive
