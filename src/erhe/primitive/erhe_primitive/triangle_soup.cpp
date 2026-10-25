#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_geometry.h>
#include <geogram/points/colocate.h>

#include <cstring>
#include <numeric>
#include <set>
#include <span>

using erhe::geometry::Mesh_attributes;
using erhe::geometry::set_pointf;

namespace erhe::primitive {

[[nodiscard]] auto is_per_point(erhe::dataformat::Vertex_attribute_usage value) -> bool
{
    switch (value) {
        case erhe::dataformat::Vertex_attribute_usage::none         : return false;
        case erhe::dataformat::Vertex_attribute_usage::position     : return true;
        case erhe::dataformat::Vertex_attribute_usage::tangent      : return false;
        case erhe::dataformat::Vertex_attribute_usage::bitangent    : return false;
        case erhe::dataformat::Vertex_attribute_usage::normal       : return false;
        case erhe::dataformat::Vertex_attribute_usage::color        : return false;
        case erhe::dataformat::Vertex_attribute_usage::joint_indices: return true;
        case erhe::dataformat::Vertex_attribute_usage::joint_weights: return true;
        case erhe::dataformat::Vertex_attribute_usage::tex_coord    : return false;
        case erhe::dataformat::Vertex_attribute_usage::custom       : return false;
        default: return "?";
    }
}

[[nodiscard]] auto is_per_corner(erhe::dataformat::Vertex_attribute_usage value) -> bool
{
    switch (value) {
        case erhe::dataformat::Vertex_attribute_usage::none         : return false;
        case erhe::dataformat::Vertex_attribute_usage::position     : return false;
        case erhe::dataformat::Vertex_attribute_usage::tangent      : return true;
        case erhe::dataformat::Vertex_attribute_usage::bitangent    : return true;
        case erhe::dataformat::Vertex_attribute_usage::normal       : return true;
        case erhe::dataformat::Vertex_attribute_usage::color        : return false;
        case erhe::dataformat::Vertex_attribute_usage::joint_indices: return false;
        case erhe::dataformat::Vertex_attribute_usage::joint_weights: return false;
        case erhe::dataformat::Vertex_attribute_usage::tex_coord    : return true;
        case erhe::dataformat::Vertex_attribute_usage::custom       : return false;
        default: return "?";
    }
}

auto Triangle_soup::get_vertex_count() const -> std::size_t
{
    return vertex_data.size() / vertex_format.get_stream(0)->stride;
}

auto Triangle_soup::get_index_count() const -> std::size_t
{
    return index_data.size();
}

// The colocation of a soup's used vertices into mesh points, in the order
// the points are created: point i is created at the position of used slot
// representative_slot[i], and used slot s (m_used_indices[s]) maps to point
// point_from_used_slot[s]. Either colocate() computes it or the caller
// supplies it (ERHE_mesh_topology vertex_points); everything after it is the
// same code, so both sources build identical meshes from identical input.
class Point_assignment
{
public:
    GEO::index_t              point_count{0};
    std::vector<GEO::index_t> point_from_used_slot;
    std::vector<GEO::index_t> representative_slot;
};

class Mesh_from_triangle_soup
{
public:
    Mesh_from_triangle_soup(
        GEO::Mesh&           mesh,
        const Triangle_soup& triangle_soup,
        Element_mappings&    element_mappings
    )
        : m_mesh            {mesh}
        , m_attributes      {mesh}
        , m_triangle_soup   {triangle_soup}
        , m_element_mappings{element_mappings}
    {
    }

    // Colocation by GEO::Geom::colocate() under erhe::geometry::geogram_lock().
    void build_colocated(Vertex_colocation* const out_colocation)
    {
        ERHE_VERIFY(m_triangle_soup.vertex_format.streams.size() == 1);
        // TODO non-indexed primitives are not currently supported
        ERHE_VERIFY(get_used_indices());
        // A soup without positions has nothing to colocate (it used to fail
        // later, on the first triangle's missing point).
        ERHE_VERIFY(read_positions());
        colocate_points();
        if (out_colocation != nullptr) {
            store_colocation(*out_colocation);
        }
        build_from_point_assignment();
    }

    // Colocation supplied by the caller. Validates everything before touching
    // the mesh or the mappings, so a false return leaves both as they were.
    // Takes no geogram lock and calls no geogram algorithm.
    [[nodiscard]] auto build_from_vertex_points(const std::span<const uint32_t> vertex_points, const uint32_t point_count) -> bool
    {
        if (m_triangle_soup.vertex_format.streams.size() != 1) {
            return false;
        }
        const std::size_t vertex_count = m_triangle_soup.get_vertex_count();
        if (vertex_points.size() != vertex_count) {
            return false;
        }
        if ((point_count == 0) || (point_count > vertex_count)) {
            return false;
        }
        for (const uint32_t index : m_triangle_soup.index_data) {
            if (index >= vertex_count) {
                return false;
            }
        }
        // A vertex no index references carries Vertex_colocation::no_point
        // or an id below point_count; it creates nothing either way.
        for (const uint32_t point : vertex_points) {
            if ((point >= point_count) && (point != Vertex_colocation::no_point)) {
                return false;
            }
        }
        if (!get_used_indices()) {
            return false;
        }

        // Every used vertex names a point, and every point is named by at
        // least one used vertex. A point's representative is its first used
        // slot, as in colocate_points().
        const std::size_t used_count = m_used_indices.size();
        m_point_assignment.point_count = static_cast<GEO::index_t>(point_count);
        m_point_assignment.point_from_used_slot.resize(used_count);
        m_point_assignment.representative_slot.assign(point_count, GEO::NO_INDEX);
        for (std::size_t used_slot = 0; used_slot < used_count; ++used_slot) {
            const uint32_t point = vertex_points[m_used_indices[used_slot]];
            if (point >= point_count) {
                return false;
            }
            m_point_assignment.point_from_used_slot[used_slot] = static_cast<GEO::index_t>(point);
            if (m_point_assignment.representative_slot[point] == GEO::NO_INDEX) {
                m_point_assignment.representative_slot[point] = static_cast<GEO::index_t>(used_slot);
            }
        }
        for (const GEO::index_t representative : m_point_assignment.representative_slot) {
            if (representative == GEO::NO_INDEX) {
                return false;
            }
        }
        if (!read_positions()) {
            return false;
        }

        build_from_point_assignment();
        return true;
    }

private:
    void build_from_point_assignment()
    {
        make_vertices();
        parse_triangles();
        parse_vertex_data();
    }

    // False when the soup has no indices.
    [[nodiscard]] auto get_used_indices() -> bool
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

        return (m_min_index != std::numeric_limits<std::size_t>::max());
    }

    // Positions of the used vertices only, in m_used_indices order: a
    // primitive's indices need not start at 0 (glTF primitives sharing one
    // vertex buffer) nor be contiguous, and an unused index inside
    // [m_min_index, m_max_index] must not become a mesh point. False when the
    // soup has no position attribute.
    [[nodiscard]] auto read_positions() -> bool
    {
        using namespace erhe::dataformat;
        const Vertex_stream&    vertex_stream      = m_triangle_soup.vertex_format.streams.front();
        const Vertex_attribute* position_attribute = vertex_stream.find_attribute(Vertex_attribute_usage::position);
        if (position_attribute == nullptr) {
            return false;
        }

        const std::size_t used_count = m_used_indices.size();
        m_vertex_positions.clear();
        m_vertex_positions.resize(used_count);
        const std::size_t   vertex_stride = vertex_stream.stride;
        const std::uint8_t* position_base = m_triangle_soup.vertex_data.data() + position_attribute->offset;
        for (std::size_t used_slot = 0; used_slot < used_count; ++used_slot) {
            const std::size_t index = m_used_indices[used_slot];
            float v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            const std::uint8_t* src = position_base + vertex_stride * index;
            std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(&v[0]);
            erhe::dataformat::convert(
                src, position_attribute->format,
                dst, erhe::dataformat::Format::format_32_vec3_float,
                1.0f
            );
            m_vertex_positions.at(used_slot) = GEO::vec3{v[0], v[1], v[2]};
        }
        return true;
    }

    void colocate_points()
    {
        // colocate() maps each used slot to the used slot of its
        // representative (old_to_new). The representative is the smallest
        // slot of its cluster: each slot first maps to the smallest slot
        // within tolerance (itself included), then colocate() follows those
        // non-increasing links to the slot that maps to itself. Sorting the
        // representatives ascending therefore numbers the points in the order
        // of their first reference by ascending used slot, which is ascending
        // soup vertex index (doc/gltf_extensions/ERHE_mesh_topology.md).
        const std::size_t used_count = m_used_indices.size();
        GEO::vector<GEO::index_t> old_to_new(static_cast<GEO::index_t>(used_count), GEO::NO_INDEX);
        const double tolerance = 0.00001; // 0.01mm if node has scale = 1.0, 1mm if node scale is 100.0
        // See erhe::geometry::geogram_lock(): colocate() is a geogram
        // algorithm (kd-tree + parallel_for) and must not run concurrently
        // with another geogram algorithm invocation. Only this call is
        // guarded; everything else this builder does is mesh-local element and
        // attribute construction, which the threading contract leaves
        // unguarded (doc/erhe/geogram.md), so conversions of different soups
        // run in parallel and serialize on colocate() alone.
        GEO::index_t point_count = 0;
        {
            ERHE_PROFILE_SCOPE("colocate"); // includes the wait for the lock
            const std::lock_guard<std::recursive_mutex> geogram_guard{erhe::geometry::geogram_lock()};
            {
                ERHE_PROFILE_SCOPE("colocate locked");
                point_count = GEO::Geom::colocate(
                    m_vertex_positions.data()->data(),                    // const double* points,
                    3,                                                    // coord_index_t dim,
                    static_cast<GEO::index_t>(m_vertex_positions.size()), // index_t nb_points,
                    old_to_new,                                           // vector<index_t>& old2new,
                    tolerance,                                            // double tolerance,
                    3,                                                    // index_t stride,
                    "default"                                             // const std::string& nn_algo
                );
            }
        }
        GEO::vector<GEO::index_t> representatives = old_to_new;
        std::sort(representatives.begin(), representatives.end());
        representatives.erase(std::unique(representatives.begin(), representatives.end()), representatives.end());
        ERHE_VERIFY(representatives.size() == point_count);

        GEO::vector<GEO::index_t> point_from_representative(static_cast<GEO::index_t>(used_count), GEO::NO_INDEX);
        m_point_assignment.point_count = point_count;
        m_point_assignment.representative_slot.resize(point_count);
        for (GEO::index_t i = 0, end = static_cast<GEO::index_t>(representatives.size()); i < end; ++i) {
            const GEO::index_t representative = representatives[i];
            point_from_representative.at(representative) = i;
            m_point_assignment.representative_slot[i] = representative;
        }
        m_point_assignment.point_from_used_slot.resize(used_count);
        for (std::size_t used_slot = 0; used_slot < used_count; ++used_slot) {
            const GEO::index_t point = point_from_representative.at(old_to_new.at(used_slot));
            ERHE_VERIFY(point != GEO::NO_INDEX);
            ERHE_VERIFY(point < point_count);
            m_point_assignment.point_from_used_slot[used_slot] = point;
        }
    }

    void store_colocation(Vertex_colocation& out_colocation) const
    {
        const std::size_t vertex_count = m_triangle_soup.get_vertex_count();
        ERHE_VERIFY(m_max_index < vertex_count);
        out_colocation.point_count = static_cast<uint32_t>(m_point_assignment.point_count);
        out_colocation.vertex_points.assign(vertex_count, Vertex_colocation::no_point);
        for (std::size_t used_slot = 0, end = m_used_indices.size(); used_slot < end; ++used_slot) {
            out_colocation.vertex_points[m_used_indices[used_slot]] = static_cast<uint32_t>(m_point_assignment.point_from_used_slot[used_slot]);
        }
    }

    void make_vertices()
    {
        const std::size_t index_range = m_max_index - m_min_index + 1;
        m_vertex_from_index.resize(index_range);
        std::fill(m_vertex_from_index.begin(), m_vertex_from_index.end(), GEO::NO_INDEX);

        const GEO::index_t point_count = m_point_assignment.point_count;
        const GEO::index_t base_vertex = m_mesh.vertices.create_vertices(point_count);
        m_mesh.vertices.set_double_precision();
        for (GEO::index_t i = 0; i < point_count; ++i) {
            const GEO::index_t representative = m_point_assignment.representative_slot[i];
            m_mesh.vertices.point<3>(base_vertex + i) = m_vertex_positions.at(representative);
        }
        m_mesh.vertices.set_single_precision();
        for (std::size_t used_slot = 0, used_count = m_used_indices.size(); used_slot < used_count; ++used_slot) {
            const GEO::index_t point = m_point_assignment.point_from_used_slot[used_slot];
            m_vertex_from_index.at(m_used_indices[used_slot] - m_min_index) = base_vertex + point;
        }
    }
    void parse_triangles()
    {
        // Count non-degenerate triangles
        const std::size_t triangle_soup_triangle_count = m_triangle_soup.index_data.size() / 3;
        std::size_t mesh_triangle_facet_count = 0;
        for (size_t i = 0; i < triangle_soup_triangle_count; ++i) {
            const uint32_t i0 = m_triangle_soup.index_data[i * 3 + 0];
            const uint32_t i1 = m_triangle_soup.index_data[i * 3 + 1];
            const uint32_t i2 = m_triangle_soup.index_data[i * 3 + 2];
            const GEO::index_t v0 = m_vertex_from_index.at(i0 - m_min_index);
            const GEO::index_t v1 = m_vertex_from_index.at(i1 - m_min_index);
            const GEO::index_t v2 = m_vertex_from_index.at(i2 - m_min_index);
            ERHE_VERIFY(v0 != GEO::NO_INDEX);
            ERHE_VERIFY(v1 != GEO::NO_INDEX);
            ERHE_VERIFY(v2 != GEO::NO_INDEX);
            if ((v0 == v1) || (v0 == v2) || (v1 == v2)) {
                continue;
            }
            ++mesh_triangle_facet_count;
        }

        auto& corner_to_vertex = m_element_mappings.mesh_corner_to_vertex_buffer_index;

        auto& primitive_to_facet = m_element_mappings.triangle_to_mesh_facet;
        primitive_to_facet.resize(triangle_soup_triangle_count);

        GEO::index_t facet = m_mesh.facets.create_triangles(static_cast<GEO::index_t>(mesh_triangle_facet_count));
        m_index_from_corner.resize(m_mesh.facet_corners.nb());
        corner_to_vertex.resize(m_mesh.facet_corners.nb());
        for (size_t i = 0, end = m_triangle_soup.index_data.size() / 3; i < end; ++i) {
            const uint32_t     i0 = m_triangle_soup.index_data[i * 3 + 0];
            const uint32_t     i1 = m_triangle_soup.index_data[i * 3 + 1];
            const uint32_t     i2 = m_triangle_soup.index_data[i * 3 + 2];
            const GEO::index_t v0 = m_vertex_from_index.at(i0 - m_min_index);
            const GEO::index_t v1 = m_vertex_from_index.at(i1 - m_min_index);
            const GEO::index_t v2 = m_vertex_from_index.at(i2 - m_min_index);
            ERHE_VERIFY(v0 != GEO::NO_INDEX);
            ERHE_VERIFY(v1 != GEO::NO_INDEX);
            ERHE_VERIFY(v2 != GEO::NO_INDEX);
            if ((v0 == v1) || (v0 == v2) || (v1 == v2)) {
                primitive_to_facet[i] = GEO::NO_INDEX;
                continue;
            }
            primitive_to_facet[i] = facet;
            m_mesh.facets.set_vertex(facet, 0, v0);
            m_mesh.facets.set_vertex(facet, 1, v1);
            m_mesh.facets.set_vertex(facet, 2, v2);

            const GEO::index_t c0 = m_mesh.facets.corner(facet, 0);
            const GEO::index_t c1 = m_mesh.facets.corner(facet, 1);
            const GEO::index_t c2 = m_mesh.facets.corner(facet, 2);
            ERHE_VERIFY(c0 != GEO::NO_INDEX);
            ERHE_VERIFY(c1 != GEO::NO_INDEX);
            ERHE_VERIFY(c2 != GEO::NO_INDEX);

            m_index_from_corner[c0] = i0;
            m_index_from_corner[c1] = i1;
            m_index_from_corner[c2] = i2;
            corner_to_vertex   [c0] = i0;
            corner_to_vertex   [c1] = i1;
            corner_to_vertex   [c2] = i2;
            ++facet;
        }
    }
    void parse_vertex_data()
    {
        ERHE_VERIFY(m_triangle_soup.vertex_format.streams.size() == 1);
        using namespace erhe::dataformat;
        const Vertex_stream&                 vertex_stream = m_triangle_soup.vertex_format.streams.front();
        const std::vector<Vertex_attribute>& attributes    = vertex_stream.attributes;

        const std::uint8_t* vertex_data_base = m_triangle_soup.vertex_data.data();
        for (std::size_t attribute_index = 0, end = attributes.size(); attribute_index < end; ++attribute_index) {
            const Vertex_attribute& attribute           = attributes[attribute_index];
            const std::uint8_t*     attribute_data_base = vertex_data_base + attribute.offset;

            if (is_per_point(attribute.usage_type)) {
                // Only the vertices the indices use: the soup's vertex data
                // may hold more (glTF primitives sharing one vertex buffer).
                for (const std::size_t vertex_index : m_used_indices) {
                    const std::uint8_t* src    = attribute_data_base + vertex_stream.stride * vertex_index;
                    const GEO::index_t  vertex = m_vertex_from_index.at(vertex_index - m_min_index);
                    ERHE_VERIFY(vertex != GEO::NO_INDEX);
                    switch (erhe::dataformat::get_format_kind(attribute.format)) {
                        case erhe::dataformat::Format_kind::format_kind_unsigned_integer: {
                            uint32_t value[4] = { 0u, 0u, 0u, 0u };
                            std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(&value[0]);
                            erhe::dataformat::convert(src, attribute.format, dst, erhe::dataformat::Format::format_32_vec4_uint, 1.0f);
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
                            erhe::dataformat::convert(src, attribute.format, dst, erhe::dataformat::Format::format_32_vec4_float, 1.0f);
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
                    const std::uint8_t* src = attribute_data_base + vertex_stream.stride * vertex_index;
                    switch (erhe::dataformat::get_format_kind(attribute.format)) {
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
                            erhe::dataformat::convert(src, attribute.format, dst, erhe::dataformat::Format::format_32_vec4_float, 1.0f);
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
    void put_vertex_attribute(const erhe::dataformat::Vertex_attribute& attribute, GEO::index_t vertex, T value[4])
    {
        switch (attribute.usage_type) {
            case erhe::dataformat::Vertex_attribute_usage::position: {
                if constexpr (std::is_floating_point_v<T>) {
                    set_pointf(m_mesh.vertices, vertex, GEO::vec3f{value[0], value[1], value[2]});
                } else {
                    geo_assert(false);
                }
                break;
            }
            case erhe::dataformat::Vertex_attribute_usage::joint_indices: {
                if constexpr (std::is_integral_v<T>) {
                    m_attributes.vertex_joint_indices(attribute.usage_index).set(vertex, GEO::vec4u{value[0], value[1], value[2], value[3]});
                } else {
                    geo_assert(false);
                }
                break;
            }
            case erhe::dataformat::Vertex_attribute_usage::joint_weights: {
                if constexpr (std::is_floating_point_v<T>) {
                    m_attributes.vertex_joint_weights(attribute.usage_index).set(vertex, GEO::vec4f{value[0], value[1], value[2], value[3]});
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
    void put_corner_attribute(const erhe::dataformat::Vertex_attribute& attribute, GEO::index_t corner, T value[4])
    {
        switch (attribute.usage_type) {
            case erhe::dataformat::Vertex_attribute_usage::normal: {
                geo_assert(attribute.usage_index == 0);
                m_attributes.corner_normal.set(corner, GEO::vec3f{value[0], value[1], value[2]});
                break; 
            }

            case erhe::dataformat::Vertex_attribute_usage::tangent: {
                geo_assert(attribute.usage_index == 0);
                m_attributes.corner_tangent.set(corner, GEO::vec4f{value[0], value[1], value[2], value[3]});
                break;
            }

            case erhe::dataformat::Vertex_attribute_usage::tex_coord: {
                m_attributes.corner_texcoord(attribute.usage_index).set(corner, GEO::vec2f{value[0], value[1]});
                break;
            }
            case erhe::dataformat::Vertex_attribute_usage::color: {
                m_attributes.corner_color(attribute.usage_index).set(corner, GEO::vec4f{value[0], value[1], value[2], value[3]});
                break;
            }
            default: {
                geo_assert(false);
                break;
            }
        }
    }

    GEO::Mesh&                m_mesh;
    Mesh_attributes           m_attributes;
    const Triangle_soup&      m_triangle_soup;
    Element_mappings&         m_element_mappings;
    std::size_t               m_min_index            {0};
    std::size_t               m_max_index            {0};
    std::vector<uint32_t>     m_used_indices         {};
    GEO::vector<GEO::vec3>    m_vertex_positions     {};
    GEO::vector<GEO::index_t> m_vertex_from_index    {}; // GEO::Mesh vertex (point) from triangle soup vertex index
    GEO::index_t              m_corner_start         {};
    GEO::index_t              m_corner_end           {};
    std::vector<GEO::index_t> m_index_from_corner    {};
    Point_assignment          m_point_assignment     {};

    GEO::Attribute<GEO::Numeric::int32> m_corner_indices;
};

auto make_triangle_soup_with_constant_color(
    const Triangle_soup& source,
    const glm::vec4&     color
) -> std::shared_ptr<Triangle_soup>
{
    using erhe::dataformat::Format;
    using erhe::dataformat::Vertex_attribute;
    using erhe::dataformat::Vertex_attribute_usage;
    using erhe::dataformat::Vertex_stream;

    if (source.vertex_format.streams.empty() || source.vertex_data.empty()) {
        return {};
    }
    const std::size_t source_stride = source.vertex_format.streams.front().stride;
    if (source_stride == 0) {
        return {};
    }
    const std::size_t vertex_count = source.vertex_data.size() / source_stride;
    if (vertex_count == 0) {
        return {};
    }

    std::shared_ptr<Triangle_soup> result = std::make_shared<Triangle_soup>();
    result->primitive_type = source.primitive_type;
    result->index_data     = source.index_data;
    result->vertex_format  = source.vertex_format;

    Vertex_stream&  stream    = result->vertex_format.streams.front();
    const bool      had_color = (stream.find_attribute(Vertex_attribute_usage::color, 0) != nullptr);
    if (!had_color) {
        stream.emplace_back(Format::format_32_vec4_float, Vertex_attribute_usage::color, 0);
        stream.finalize_stride();
    }
    const Vertex_attribute* const attribute = stream.find_attribute(Vertex_attribute_usage::color, 0);
    if (attribute == nullptr) {
        return {};
    }
    const std::size_t stride = stream.stride;

    result->vertex_data.resize(vertex_count * stride, std::uint8_t{0});
    const std::uint8_t* const src_base = source.vertex_data.data();
    std::uint8_t* const       dst_base = result->vertex_data.data();
    for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
        // The attributes the source already carries keep their offsets: an
        // appended attribute only grows the stride past them.
        std::memcpy(dst_base + (vertex * stride), src_base + (vertex * source_stride), source_stride);
        erhe::dataformat::convert(
            &color.x,
            Format::format_32_vec4_float,
            dst_base + (vertex * stride) + attribute->offset,
            attribute->format,
            1.0f
        );
    }
    return result;
}

void mesh_from_triangle_soup(const Triangle_soup& triangle_soup, GEO::Mesh& mesh, Element_mappings& element_mappings)
{
    ERHE_VERIFY(element_mappings.mesh_corner_to_vertex_buffer_index.empty());
    ERHE_VERIFY(element_mappings.triangle_to_mesh_facet.empty());

    ERHE_PROFILE_FUNCTION();

    // No geogram_lock() here: the builder takes it around the one geogram
    // algorithm it calls (colocate() in colocate_points()).
    Mesh_from_triangle_soup builder{mesh, triangle_soup, element_mappings};
    builder.build_colocated(nullptr);
}

void mesh_from_triangle_soup(
    const Triangle_soup& triangle_soup,
    GEO::Mesh&           mesh,
    Element_mappings&    element_mappings,
    Vertex_colocation&   out_colocation
)
{
    ERHE_VERIFY(element_mappings.mesh_corner_to_vertex_buffer_index.empty());
    ERHE_VERIFY(element_mappings.triangle_to_mesh_facet.empty());

    ERHE_PROFILE_FUNCTION();

    out_colocation = Vertex_colocation{};
    Mesh_from_triangle_soup builder{mesh, triangle_soup, element_mappings};
    builder.build_colocated(&out_colocation);
}

auto mesh_from_triangle_soup(
    const Triangle_soup&            triangle_soup,
    const std::span<const uint32_t> vertex_points,
    const uint32_t                  point_count,
    GEO::Mesh&                      mesh,
    Element_mappings&               element_mappings
) -> bool
{
    ERHE_VERIFY(element_mappings.mesh_corner_to_vertex_buffer_index.empty());
    ERHE_VERIFY(element_mappings.triangle_to_mesh_facet.empty());

    ERHE_PROFILE_FUNCTION();

    // No geogram_lock(): the supplied colocation replaces colocate(), the
    // only geogram algorithm this conversion runs.
    Mesh_from_triangle_soup builder{mesh, triangle_soup, element_mappings};
    return builder.build_from_vertex_points(vertex_points, point_count);
}

} // amespace erhe::primitive
