// Primitive_shape::release_triangle_soup() / get_triangle_soup() reload
// (doc/erhe/primitive.md "Releasing the triangle soup") and the raytrace's
// release of its vertex / index copies after commit.

#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/triangle_soup.hpp"

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_raytrace/igeometry.hpp"

#include <gtest/gtest.h>

#include <geogram/mesh/mesh.h>

#include <cstring>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

using erhe::dataformat::Format;
using erhe::dataformat::Vertex_attribute_usage;
using erhe::primitive::Primitive_raytrace;
using erhe::primitive::Primitive_render_shape;
using erhe::primitive::Shape_memory_statistics;
using erhe::primitive::Triangle_soup;
using erhe::primitive::Triangle_soup_source;

// A grid of quads, two triangles each, position + normal.
[[nodiscard]] auto make_grid_soup(const int size) -> std::shared_ptr<Triangle_soup>
{
    std::shared_ptr<Triangle_soup> soup = std::make_shared<Triangle_soup>();
    soup->vertex_format = erhe::dataformat::Vertex_format{
        {
            0,
            {
                {Format::format_32_vec3_float, Vertex_attribute_usage::position},
                {Format::format_32_vec3_float, Vertex_attribute_usage::normal}
            }
        }
    };
    const int row = size + 1;
    for (int y = 0; y <= size; ++y) {
        for (int x = 0; x <= size; ++x) {
            const float values[6] = {static_cast<float>(x), static_cast<float>(y), 0.25f * static_cast<float>(x * y), 0.0f, 0.0f, 1.0f};
            const std::size_t offset = soup->vertex_data.size();
            soup->vertex_data.resize(offset + sizeof(values));
            std::memcpy(soup->vertex_data.data() + offset, &values[0], sizeof(values));
        }
    }
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const uint32_t a = static_cast<uint32_t>((y * row) + x);
            const uint32_t b = a + 1;
            const uint32_t c = a + 1 + static_cast<uint32_t>(row);
            const uint32_t d = a + static_cast<uint32_t>(row);
            soup->index_data.insert(soup->index_data.end(), {a, b, c, a, c, d});
        }
    }
    return soup;
}

// Hands out copies of a reference soup and counts the loads.
class Copying_soup_source : public Triangle_soup_source
{
public:
    explicit Copying_soup_source(const Triangle_soup& reference) : m_reference{reference} {}

    auto load() const -> std::shared_ptr<Triangle_soup> override
    {
        ++load_count;
        if (fail) {
            return {};
        }
        return std::make_shared<Triangle_soup>(m_reference);
    }
    auto describe() const -> std::string override { return "test soup"; }

    mutable int load_count{0};
    bool        fail{false};

private:
    Triangle_soup m_reference;
};

void expect_same_soup(const Triangle_soup& lhs, const Triangle_soup& rhs)
{
    EXPECT_EQ(lhs.vertex_data, rhs.vertex_data);
    EXPECT_EQ(lhs.index_data, rhs.index_data);
    EXPECT_EQ(lhs.primitive_type, rhs.primitive_type);
    ASSERT_EQ(lhs.vertex_format.streams.size(), rhs.vertex_format.streams.size());
    for (std::size_t s = 0; s < lhs.vertex_format.streams.size(); ++s) {
        EXPECT_EQ(lhs.vertex_format.streams[s].stride, rhs.vertex_format.streams[s].stride);
        ASSERT_EQ(lhs.vertex_format.streams[s].attributes.size(), rhs.vertex_format.streams[s].attributes.size());
        for (std::size_t a = 0; a < lhs.vertex_format.streams[s].attributes.size(); ++a) {
            EXPECT_EQ(lhs.vertex_format.streams[s].attributes[a].format, rhs.vertex_format.streams[s].attributes[a].format);
            EXPECT_EQ(lhs.vertex_format.streams[s].attributes[a].usage_type, rhs.vertex_format.streams[s].attributes[a].usage_type);
            EXPECT_EQ(lhs.vertex_format.streams[s].attributes[a].offset, rhs.vertex_format.streams[s].attributes[a].offset);
        }
    }
}

TEST(Triangle_soup_release, release_needs_a_source)
{
    Primitive_render_shape shape{make_grid_soup(3)};
    EXPECT_FALSE(shape.release_triangle_soup());
    EXPECT_FALSE(shape.is_triangle_soup_released());
    EXPECT_TRUE(shape.get_triangle_soup());
}

TEST(Triangle_soup_release, reload_is_shared_while_held_and_counts_stay)
{
    const std::shared_ptr<Triangle_soup> original = make_grid_soup(4);
    const Triangle_soup                  reference = *original;
    std::shared_ptr<Copying_soup_source> source = std::make_shared<Copying_soup_source>(reference);

    Primitive_render_shape shape{original};
    shape.set_triangle_soup_source(source);
    ASSERT_TRUE(shape.release_triangle_soup());
    EXPECT_TRUE(shape.is_triangle_soup_released());
    EXPECT_TRUE(shape.has_triangle_soup());
    EXPECT_EQ(shape.get_triangle_soup_counts().vertex_count, reference.get_vertex_count());
    EXPECT_EQ(shape.get_triangle_soup_counts().index_count,  reference.get_index_count());
    EXPECT_EQ(source->load_count, 0);

    Shape_memory_statistics released_statistics;
    {
        std::unordered_set<const void*> counted;
        shape.add_memory_statistics(released_statistics, counted);
    }
    EXPECT_EQ(released_statistics.triangle_soup_count, 0u);
    EXPECT_EQ(released_statistics.triangle_soup_released_count, 1u);

    const erhe::primitive::Triangle_soup_reload_statistics before = erhe::primitive::get_triangle_soup_reload_statistics();
    {
        const std::shared_ptr<Triangle_soup> first  = shape.get_triangle_soup();
        const std::shared_ptr<Triangle_soup> second = shape.get_triangle_soup();
        ASSERT_TRUE(first);
        EXPECT_EQ(first, second); // one read while held
        EXPECT_EQ(source->load_count, 1);
        expect_same_soup(*first, reference);
    }
    // Every holder let go: the next reader reads it again.
    EXPECT_TRUE(shape.get_triangle_soup());
    EXPECT_EQ(source->load_count, 2);
    const erhe::primitive::Triangle_soup_reload_statistics after = erhe::primitive::get_triangle_soup_reload_statistics();
    EXPECT_EQ(after.reload_count - before.reload_count, 2u);

    // A source that cannot reproduce the soup gives null.
    source->fail = true;
    EXPECT_FALSE(shape.get_triangle_soup());
    EXPECT_EQ(erhe::primitive::get_triangle_soup_reload_statistics().reload_failure_count - after.reload_failure_count, 1u);
}

TEST(Triangle_soup_release, geometry_builds_from_the_reloaded_soup)
{
    const std::shared_ptr<Triangle_soup> original = make_grid_soup(5);

    Primitive_render_shape resident{std::make_shared<Triangle_soup>(*original)};
    const std::shared_ptr<erhe::geometry::Geometry>& resident_geometry = resident.get_geometry();
    ASSERT_TRUE(resident_geometry);

    Primitive_render_shape released{original};
    released.set_triangle_soup_source(std::make_shared<Copying_soup_source>(*original));
    ASSERT_TRUE(released.release_triangle_soup());
    const std::shared_ptr<erhe::geometry::Geometry>& released_geometry = released.get_geometry();
    ASSERT_TRUE(released_geometry);
    EXPECT_EQ(released_geometry->get_mesh().vertices.nb(),      resident_geometry->get_mesh().vertices.nb());
    EXPECT_EQ(released_geometry->get_mesh().facets.nb(),        resident_geometry->get_mesh().facets.nb());
    EXPECT_EQ(released_geometry->get_mesh().edges.nb(),         resident_geometry->get_mesh().edges.nb());
}

TEST(Triangle_soup_release, soup_raytrace_frees_its_copies_and_maps_facets_by_identity)
{
    const std::shared_ptr<Triangle_soup> soup = make_grid_soup(6);
    const Primitive_raytrace raytrace{*soup, nullptr};
    ASSERT_TRUE(raytrace.has_raytrace_triangles());

    Shape_memory_statistics statistics;
    std::unordered_set<const void*> counted;
    raytrace.add_memory_statistics(statistics, counted);
    EXPECT_EQ(statistics.raytrace_facet_map_bytes, 0u);
    // A backend that reads the buffers after commit (embree) keeps them.
    const bool backend_releases = erhe::raytrace::IGeometry::create_unique("probe", erhe::raytrace::Geometry_type::GEOMETRY_TYPE_TRIANGLE)->release_buffers();
    if (backend_releases) {
        EXPECT_EQ(statistics.raytrace_buffer_bytes, 0u);
    }
    const std::size_t triangle_count = soup->get_index_count() / 3;
    for (uint32_t t = 0; t < triangle_count; ++t) {
        EXPECT_EQ(raytrace.get_mesh_facet_from_triangle(t), static_cast<GEO::index_t>(t));
    }
    // The ranges and bounds stay for the Properties view and the bounding box.
    const erhe::primitive::Buffer_mesh& mesh = raytrace.get_raytrace_mesh();
    EXPECT_EQ(mesh.triangle_fill_indices.index_count, soup->get_index_count());
    EXPECT_TRUE(mesh.bounding_box.is_valid_3d());
}

} // anonymous namespace
