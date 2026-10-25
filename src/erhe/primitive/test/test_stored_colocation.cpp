#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/triangle_soup.hpp"

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_geometry/geometry.hpp"

#include <gtest/gtest.h>

#include <geogram/basic/attributes.h>
#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// The stored-colocation overload of mesh_from_triangle_soup()
// (ERHE_mesh_topology, doc/plans/gltf_fast_load.md step 2) must reproduce
// the colocate path bit for bit when given the colocation that path used:
// points, facet / corner topology, every vertex / facet / corner attribute,
// the element mappings, and after Geometry::process() the edges and smooth
// vertex normals. It must not take erhe::geometry::geogram_lock().

namespace {

using erhe::dataformat::Format;
using erhe::dataformat::Vertex_attribute;
using erhe::dataformat::Vertex_attribute_usage;
using erhe::dataformat::Vertex_format;
using erhe::dataformat::Vertex_stream;
using erhe::primitive::Element_mappings;
using erhe::primitive::Triangle_soup;
using erhe::primitive::Vertex_colocation;

[[nodiscard]] auto make_format() -> Vertex_format
{
    return Vertex_format{
        Vertex_stream{
            0,
            {
                {Format::format_32_vec3_float, Vertex_attribute_usage::position,      0},
                {Format::format_32_vec3_float, Vertex_attribute_usage::normal,        0},
                {Format::format_32_vec4_float, Vertex_attribute_usage::tangent,       0},
                {Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord,     0},
                {Format::format_32_vec4_float, Vertex_attribute_usage::color,         0},
                {Format::format_32_vec4_uint,  Vertex_attribute_usage::joint_indices, 0},
                {Format::format_32_vec4_float, Vertex_attribute_usage::joint_weights, 0}
            }
        }
    };
}

class Soup_writer
{
public:
    explicit Soup_writer(Triangle_soup& soup)
        : m_soup{soup}
    {
    }

    // Appends one vertex; attributes other than position are derived from
    // `seed` so every corner carries distinct values.
    auto add_vertex(const float x, const float y, const float z, const uint32_t seed) -> uint32_t
    {
        const Vertex_stream& stream = m_soup.vertex_format.streams.front();
        const std::size_t    vertex = m_soup.vertex_data.size() / stream.stride;
        m_soup.vertex_data.resize((vertex + 1) * stream.stride, uint8_t{0});
        uint8_t* const base = m_soup.vertex_data.data() + (vertex * stream.stride);
        const float s = static_cast<float>(seed);
        const float    position[3] = {x, y, z};
        const float    normal  [3] = {0.0f, 0.0f, 1.0f};
        const float    tangent [4] = {1.0f, 0.0f, 0.0f, ((seed % 2u) == 0u) ? 1.0f : -1.0f};
        const float    texcoord[2] = {0.125f * s, 1.0f - (0.0625f * s)};
        const float    color   [4] = {0.01f * s, 0.5f, 1.0f - (0.01f * s), 1.0f};
        const uint32_t joints  [4] = {seed % 3u, 1u, 2u, 3u};
        const float    weights [4] = {0.5f, 0.25f, 0.125f, 0.125f};
        write(base, Vertex_attribute_usage::position,      position, sizeof(position));
        write(base, Vertex_attribute_usage::normal,        normal,   sizeof(normal));
        write(base, Vertex_attribute_usage::tangent,       tangent,  sizeof(tangent));
        write(base, Vertex_attribute_usage::tex_coord,     texcoord, sizeof(texcoord));
        write(base, Vertex_attribute_usage::color,         color,    sizeof(color));
        write(base, Vertex_attribute_usage::joint_indices, joints,   sizeof(joints));
        write(base, Vertex_attribute_usage::joint_weights, weights,  sizeof(weights));
        return static_cast<uint32_t>(vertex);
    }

    void add_triangle(const uint32_t a, const uint32_t b, const uint32_t c)
    {
        m_soup.index_data.push_back(a);
        m_soup.index_data.push_back(b);
        m_soup.index_data.push_back(c);
    }

private:
    void write(uint8_t* const base, const Vertex_attribute_usage usage, const void* const data, const std::size_t size)
    {
        const Vertex_attribute* const attribute = m_soup.vertex_format.streams.front().find_attribute(usage, 0);
        ASSERT_NE(attribute, nullptr);
        std::memcpy(base + attribute->offset, data, size);
    }

    Triangle_soup& m_soup;
};

// A 4 x 3 grid of quads, two triangles each, every triangle with its own
// three vertices (so colocate() has corners to merge). Some copies of a grid
// position are offset by less than the 1e-5 tolerance, some triangles reuse
// a vertex of the previous triangle (shared indices), one triangle collapses
// onto two points (dropped), and the vertex data has an unreferenced vertex
// first and one last.
constexpr int grid_x = 4;
constexpr int grid_y = 3;
constexpr uint32_t expected_point_count = (grid_x + 1) * (grid_y + 1);

[[nodiscard]] auto make_soup() -> Triangle_soup
{
    Triangle_soup soup;
    soup.vertex_format = make_format();
    Soup_writer writer{soup};
    uint32_t seed = 0;
    // Unreferenced vertex at the start: indices do not start at 0.
    static_cast<void>(writer.add_vertex(100.0f, 100.0f, 100.0f, seed++));

    const auto corner = [&](const int x, const int y) -> uint32_t {
        // Every third copy is nudged by 3e-6, inside the 1e-5 tolerance.
        const float nudge = ((seed % 3u) == 0u) ? 3.0e-6f : 0.0f;
        return writer.add_vertex(static_cast<float>(x) + nudge, static_cast<float>(y), 0.0f - nudge, seed++);
    };
    for (int y = 0; y < grid_y; ++y) {
        for (int x = 0; x < grid_x; ++x) {
            const uint32_t a = corner(x,     y);
            const uint32_t b = corner(x + 1, y);
            const uint32_t c = corner(x + 1, y + 1);
            writer.add_triangle(a, b, c);
            if (((x + y) % 2) == 0) {
                // Shares vertices a and c with the first triangle.
                const uint32_t d = corner(x, y + 1);
                writer.add_triangle(a, c, d);
            } else {
                const uint32_t a2 = corner(x,     y);
                const uint32_t c2 = corner(x + 1, y + 1);
                const uint32_t d  = corner(x,     y + 1);
                writer.add_triangle(a2, c2, d);
            }
        }
    }
    // Degenerate after colocation: two corners on grid point (0, 0).
    {
        const uint32_t a = corner(0, 0);
        const uint32_t b = corner(0, 0);
        const uint32_t c = corner(1, 0);
        writer.add_triangle(a, b, c);
    }
    // Unreferenced vertex at the end.
    static_cast<void>(writer.add_vertex(-100.0f, -100.0f, -100.0f, seed++));
    return soup;
}

[[nodiscard]] auto hex(const float value) -> std::string
{
    return std::format("{:a}", value);
}

void compare_attribute_stores(const GEO::AttributesManager& a, const GEO::AttributesManager& b, const char* const domain)
{
    GEO::vector<std::string> names_a;
    GEO::vector<std::string> names_b;
    a.list_attribute_names(names_a);
    b.list_attribute_names(names_b);
    std::sort(names_a.begin(), names_a.end());
    std::sort(names_b.begin(), names_b.end());
    // Mesh_attributes binds its stores in every domain, so an empty list
    // would mean nothing was compared.
    ASSERT_FALSE(names_a.empty()) << domain;
    ASSERT_EQ(std::vector<std::string>(names_a.begin(), names_a.end()), std::vector<std::string>(names_b.begin(), names_b.end())) << domain;
    for (const std::string& name : names_a) {
        const GEO::AttributeStore* const store_a = a.find_attribute_store(name);
        const GEO::AttributeStore* const store_b = b.find_attribute_store(name);
        ASSERT_NE(store_a, nullptr) << domain << "." << name;
        ASSERT_NE(store_b, nullptr) << domain << "." << name;
        ASSERT_EQ(store_a->size(),         store_b->size())         << domain << "." << name;
        ASSERT_EQ(store_a->dimension(),    store_b->dimension())    << domain << "." << name;
        ASSERT_EQ(store_a->element_size(), store_b->element_size()) << domain << "." << name;
        const std::size_t byte_count = static_cast<std::size_t>(store_a->size()) * store_a->dimension() * store_a->element_size();
        EXPECT_EQ(std::memcmp(store_a->data(), store_b->data(), byte_count), 0) << domain << "." << name << " differs";
    }
}

void compare_meshes(const GEO::Mesh& a, const GEO::Mesh& b)
{
    ASSERT_EQ(a.vertices.nb(),      b.vertices.nb());
    ASSERT_EQ(a.facets.nb(),        b.facets.nb());
    ASSERT_EQ(a.facet_corners.nb(), b.facet_corners.nb());
    ASSERT_EQ(a.edges.nb(),         b.edges.nb());

    for (GEO::index_t vertex = 0; vertex < a.vertices.nb(); ++vertex) {
        const GEO::vec3f pa = erhe::geometry::get_pointf(a.vertices, vertex);
        const GEO::vec3f pb = erhe::geometry::get_pointf(b.vertices, vertex);
        for (GEO::index_t i = 0; i < 3; ++i) {
            EXPECT_EQ(hex(pa[i]), hex(pb[i])) << "point " << vertex << " coordinate " << i;
        }
    }
    for (GEO::index_t facet = 0; facet < a.facets.nb(); ++facet) {
        ASSERT_EQ(a.facets.nb_vertices(facet), b.facets.nb_vertices(facet));
        ASSERT_EQ(a.facets.corners_begin(facet), b.facets.corners_begin(facet));
    }
    for (GEO::index_t corner = 0; corner < a.facet_corners.nb(); ++corner) {
        EXPECT_EQ(a.facet_corners.vertex(corner),         b.facet_corners.vertex(corner))         << "corner " << corner;
        EXPECT_EQ(a.facet_corners.adjacent_facet(corner), b.facet_corners.adjacent_facet(corner)) << "corner " << corner;
    }
    for (GEO::index_t edge = 0; edge < a.edges.nb(); ++edge) {
        EXPECT_EQ(a.edges.vertex(edge, 0), b.edges.vertex(edge, 0)) << "edge " << edge;
        EXPECT_EQ(a.edges.vertex(edge, 1), b.edges.vertex(edge, 1)) << "edge " << edge;
    }

    compare_attribute_stores(a.vertices.attributes(),      b.vertices.attributes(),      "vertices");
    compare_attribute_stores(a.facets.attributes(),        b.facets.attributes(),        "facets");
    compare_attribute_stores(a.facet_corners.attributes(), b.facet_corners.attributes(), "facet_corners");
    compare_attribute_stores(a.edges.attributes(),         b.edges.attributes(),         "edges");
}

void process_like_gltf_import(erhe::geometry::Geometry& geometry)
{
    // The flags finalize_imported_meshes processes a converted soup with.
    geometry.process({.flags =
        erhe::geometry::Geometry::process_flag_connect                       |
        erhe::geometry::Geometry::process_flag_build_edges                   |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals
    });
}

enum class Conversion_path : unsigned int
{
    colocate,
    supplied
};

class Colocated
{
public:
    erhe::geometry::Geometry geometry{"colocated"};
    Element_mappings         mappings;
    Vertex_colocation        colocation;
};

TEST(Stored_colocation, colocate_path_numbers_points_in_first_reference_order)
{
    const Triangle_soup soup = make_soup();
    Colocated colocated;
    erhe::primitive::mesh_from_triangle_soup(soup, colocated.geometry.get_mesh(), colocated.mappings, colocated.colocation);

    const Vertex_colocation& colocation = colocated.colocation;
    ASSERT_EQ(colocation.vertex_points.size(), soup.get_vertex_count());
    EXPECT_EQ(colocation.point_count, expected_point_count);
    EXPECT_EQ(colocated.geometry.get_mesh().vertices.nb(), expected_point_count);
    EXPECT_EQ(colocation.vertex_points.front(), Vertex_colocation::no_point);
    EXPECT_EQ(colocation.vertex_points.back(),  Vertex_colocation::no_point);

    // ERHE_mesh_topology's point order: scanning the referenced vertices
    // upwards, each value is an id seen before or the next unused id.
    uint32_t next_point = 0;
    for (std::size_t vertex = 1; (vertex + 1) < colocation.vertex_points.size(); ++vertex) {
        const uint32_t point = colocation.vertex_points[vertex];
        ASSERT_LE(point, next_point) << "vertex " << vertex;
        if (point == next_point) {
            ++next_point;
        }
    }
    EXPECT_EQ(next_point, colocation.point_count);

    // The collapsed triangle is dropped.
    EXPECT_EQ(colocated.mappings.triangle_to_mesh_facet.back(), GEO::NO_INDEX);
    EXPECT_EQ(colocated.geometry.get_mesh().facets.nb(), static_cast<GEO::index_t>(2 * grid_x * grid_y));
}

TEST(Stored_colocation, supplied_points_reproduce_colocate_path_bit_exact)
{
    const Triangle_soup soup = make_soup();
    Colocated colocated;
    erhe::primitive::mesh_from_triangle_soup(soup, colocated.geometry.get_mesh(), colocated.mappings, colocated.colocation);

    erhe::geometry::Geometry supplied_geometry{"supplied"};
    Element_mappings         supplied_mappings;
    ASSERT_TRUE(
        erhe::primitive::mesh_from_triangle_soup(
            soup,
            colocated.colocation.vertex_points,
            colocated.colocation.point_count,
            supplied_geometry.get_mesh(),
            supplied_mappings
        )
    );

    EXPECT_EQ(colocated.mappings.triangle_to_mesh_facet,             supplied_mappings.triangle_to_mesh_facet);
    EXPECT_EQ(colocated.mappings.mesh_corner_to_vertex_buffer_index, supplied_mappings.mesh_corner_to_vertex_buffer_index);
    EXPECT_EQ(colocated.mappings.mesh_vertex_to_vertex_buffer_index, supplied_mappings.mesh_vertex_to_vertex_buffer_index);
    {
        SCOPED_TRACE("after conversion");
        compare_meshes(colocated.geometry.get_mesh(), supplied_geometry.get_mesh());
    }

    process_like_gltf_import(colocated.geometry);
    process_like_gltf_import(supplied_geometry);
    ASSERT_GT(colocated.geometry.get_mesh().edges.nb(), 0u);
    {
        SCOPED_TRACE("after process");
        compare_meshes(colocated.geometry.get_mesh(), supplied_geometry.get_mesh());
    }
    // The smooth normals are in the attribute stores compared above; compare
    // them once more as hexfloat values, and require them present on every
    // point so the comparison is not vacuous.
    const erhe::geometry::Mesh_attributes& attributes_a = colocated.geometry.get_attributes();
    const erhe::geometry::Mesh_attributes& attributes_b = supplied_geometry.get_attributes();
    for (GEO::index_t vertex = 0, end = colocated.geometry.get_mesh().vertices.nb(); vertex < end; ++vertex) {
        ASSERT_TRUE(attributes_a.vertex_normal_smooth.has(vertex)) << "vertex " << vertex;
        ASSERT_TRUE(attributes_b.vertex_normal_smooth.has(vertex)) << "vertex " << vertex;
        const GEO::vec3f normal_a = attributes_a.vertex_normal_smooth.get(vertex);
        const GEO::vec3f normal_b = attributes_b.vertex_normal_smooth.get(vertex);
        for (GEO::index_t i = 0; i < 3; ++i) {
            EXPECT_EQ(hex(normal_a[i]), hex(normal_b[i])) << "smooth normal " << vertex << " component " << i;
        }
    }
}

// Control for the point comparison above (the point coordinates are not an
// attribute-manager store, so compare_meshes() checks them separately): a
// valid colocation with two point ids swapped builds a different, equally
// valid mesh whose point 0 is the colocate path's point 1.
TEST(Stored_colocation, swapped_point_ids_build_a_different_mesh)
{
    const Triangle_soup soup = make_soup();
    Colocated colocated;
    erhe::primitive::mesh_from_triangle_soup(soup, colocated.geometry.get_mesh(), colocated.mappings, colocated.colocation);

    std::vector<uint32_t> swapped = colocated.colocation.vertex_points;
    for (uint32_t& point : swapped) {
        if (point == 0) {
            point = 1;
        } else if (point == 1) {
            point = 0;
        }
    }
    erhe::geometry::Geometry swapped_geometry{"swapped"};
    Element_mappings         swapped_mappings;
    ASSERT_TRUE(
        erhe::primitive::mesh_from_triangle_soup(
            soup, swapped, colocated.colocation.point_count, swapped_geometry.get_mesh(), swapped_mappings
        )
    );
    const GEO::Mesh& a = colocated.geometry.get_mesh();
    const GEO::Mesh& b = swapped_geometry.get_mesh();
    ASSERT_EQ(a.vertices.nb(), b.vertices.nb());
    const GEO::vec3f a0 = erhe::geometry::get_pointf(a.vertices, 0);
    const GEO::vec3f b0 = erhe::geometry::get_pointf(b.vertices, 0);
    const GEO::vec3f b1 = erhe::geometry::get_pointf(b.vertices, 1);
    EXPECT_NE(std::memcmp(&a0, &b0, sizeof(GEO::vec3f)), 0);
    EXPECT_EQ(std::memcmp(&a0, &b1, sizeof(GEO::vec3f)), 0);
}

TEST(Stored_colocation, supplied_path_takes_no_geogram_lock)
{
    const Triangle_soup soup = make_soup();
    Colocated colocated;
    erhe::primitive::mesh_from_triangle_soup(soup, colocated.geometry.get_mesh(), colocated.mappings, colocated.colocation);

    // This thread holds the geogram lock while another thread converts. A
    // conversion that takes the lock blocks until it is released, so it does
    // not finish within the wait; one that does not take it finishes.
    const auto convert_on_other_thread = [&](const Conversion_path path) -> std::future<bool> {
        return std::async(std::launch::async, [&soup, &colocated, path]() -> bool {
            erhe::geometry::Geometry geometry{"locked"};
            Element_mappings         mappings;
            if (path == Conversion_path::supplied) {
                return erhe::primitive::mesh_from_triangle_soup(
                    soup, colocated.colocation.vertex_points, colocated.colocation.point_count, geometry.get_mesh(), mappings
                );
            }
            erhe::primitive::mesh_from_triangle_soup(soup, geometry.get_mesh(), mappings);
            return true;
        });
    };

    {
        std::unique_lock<std::recursive_mutex> geogram_guard{erhe::geometry::geogram_lock()};
        std::future<bool> result = convert_on_other_thread(Conversion_path::supplied);
        const std::future_status status = result.wait_for(std::chrono::seconds{30});
        EXPECT_EQ(status, std::future_status::ready) << "supplied path waited for geogram_lock()";
        geogram_guard.unlock();
        EXPECT_TRUE(result.get());
    }

    // Control: the colocate path does wait for the lock.
    {
        std::unique_lock<std::recursive_mutex> geogram_guard{erhe::geometry::geogram_lock()};
        std::future<bool> result = convert_on_other_thread(Conversion_path::colocate);
        const std::future_status status = result.wait_for(std::chrono::milliseconds{500});
        EXPECT_EQ(status, std::future_status::timeout) << "colocate path did not wait for geogram_lock()";
        geogram_guard.unlock();
        EXPECT_TRUE(result.get());
    }
}

TEST(Stored_colocation, invalid_vertex_points_are_rejected)
{
    const Triangle_soup soup = make_soup();
    Colocated colocated;
    erhe::primitive::mesh_from_triangle_soup(soup, colocated.geometry.get_mesh(), colocated.mappings, colocated.colocation);
    const std::vector<uint32_t>& valid       = colocated.colocation.vertex_points;
    const uint32_t               point_count = colocated.colocation.point_count;

    const auto expect_rejected = [&soup](const std::vector<uint32_t>& vertex_points, const uint32_t count, const char* const what) {
        erhe::geometry::Geometry geometry{"rejected"};
        Element_mappings         mappings;
        EXPECT_FALSE(erhe::primitive::mesh_from_triangle_soup(soup, vertex_points, count, geometry.get_mesh(), mappings)) << what;
        EXPECT_EQ(geometry.get_mesh().vertices.nb(), 0u) << what;
        EXPECT_EQ(geometry.get_mesh().facets.nb(),   0u) << what;
        EXPECT_TRUE(mappings.triangle_to_mesh_facet.empty()) << what;
        EXPECT_TRUE(mappings.mesh_corner_to_vertex_buffer_index.empty()) << what;
    };

    {
        std::vector<uint32_t> short_span = valid;
        short_span.pop_back();
        expect_rejected(short_span, point_count, "span shorter than the vertex count");
    }
    {
        std::vector<uint32_t> long_span = valid;
        long_span.push_back(0);
        expect_rejected(long_span, point_count, "span longer than the vertex count");
    }
    {
        std::vector<uint32_t> out_of_range = valid;
        out_of_range[1] = point_count;
        expect_rejected(out_of_range, point_count, "referenced vertex id == point_count");
    }
    {
        std::vector<uint32_t> unused_out_of_range = valid;
        unused_out_of_range.front() = point_count + 7;
        expect_rejected(unused_out_of_range, point_count, "unreferenced vertex id out of range and not no_point");
    }
    {
        std::vector<uint32_t> referenced_no_point = valid;
        referenced_no_point[1] = Vertex_colocation::no_point;
        expect_rejected(referenced_no_point, point_count, "referenced vertex with no_point");
    }
    expect_rejected(valid, point_count + 1, "point never referenced");
    expect_rejected(valid, 0, "zero points");
    {
        // A point referenced only by an unreferenced vertex does not count.
        std::vector<uint32_t> via_unused = valid;
        via_unused.front() = point_count;
        expect_rejected(via_unused, point_count + 1, "point referenced only by an unreferenced vertex");
    }

    // An unreferenced vertex may name any point below point_count.
    {
        std::vector<uint32_t> unused_named = valid;
        unused_named.front() = 0;
        erhe::geometry::Geometry geometry{"accepted"};
        Element_mappings         mappings;
        EXPECT_TRUE(erhe::primitive::mesh_from_triangle_soup(soup, unused_named, point_count, geometry.get_mesh(), mappings));
    }

    // Index out of range for the vertex data.
    {
        Triangle_soup bad_index = soup;
        bad_index.index_data.back() = static_cast<uint32_t>(soup.get_vertex_count());
        erhe::geometry::Geometry geometry{"rejected"};
        Element_mappings         mappings;
        EXPECT_FALSE(erhe::primitive::mesh_from_triangle_soup(bad_index, valid, point_count, geometry.get_mesh(), mappings));
        EXPECT_EQ(geometry.get_mesh().vertices.nb(), 0u);
    }
}

} // namespace
