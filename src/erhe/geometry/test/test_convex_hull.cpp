#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/convex_hull.hpp"
#include "erhe_math/math_util.hpp"

#include <geogram/mesh/mesh.h>
#include <gtest/gtest.h>

#include <vector>

namespace {

void fill_point_mesh(GEO::Mesh& mesh, const std::vector<glm::vec3>& points)
{
    mesh.vertices.set_dimension(3);
    mesh.vertices.set_single_precision();
    mesh.vertices.create_vertices(static_cast<GEO::index_t>(points.size()));
    for (GEO::index_t v = 0; v < mesh.vertices.nb(); ++v) {
        float* p = mesh.vertices.single_precision_point_ptr(v);
        p[0] = points[v].x;
        p[1] = points[v].y;
        p[2] = points[v].z;
    }
}

[[nodiscard]] auto cube_points() -> std::vector<glm::vec3>
{
    return std::vector<glm::vec3>{
        glm::vec3{-1.0f, -1.0f, -1.0f},
        glm::vec3{ 1.0f, -1.0f, -1.0f},
        glm::vec3{ 1.0f,  1.0f, -1.0f},
        glm::vec3{-1.0f,  1.0f, -1.0f},
        glm::vec3{-1.0f, -1.0f,  1.0f},
        glm::vec3{ 1.0f, -1.0f,  1.0f},
        glm::vec3{ 1.0f,  1.0f,  1.0f},
        glm::vec3{-1.0f,  1.0f,  1.0f}
    };
}

[[nodiscard]] auto tetrahedron_points() -> std::vector<glm::vec3>
{
    return std::vector<glm::vec3>{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
        glm::vec3{0.0f, 0.0f, 1.0f}
    };
}

[[nodiscard]] auto coplanar_quad_points() -> std::vector<glm::vec3>
{
    return std::vector<glm::vec3>{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 1.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f}
    };
}

} // anonymous namespace

TEST(convex_hull, classify_affine_span_reports_the_reason)
{
    const std::vector<glm::vec3> too_few{glm::vec3{0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}};
    EXPECT_EQ(erhe::math::classify_affine_span(std::span<const glm::vec3>{too_few}), erhe::math::Affine_span::too_few_points);

    const std::vector<glm::vec3> repeated(5, glm::vec3{2.0f, 3.0f, 4.0f});
    EXPECT_EQ(erhe::math::classify_affine_span(std::span<const glm::vec3>{repeated}), erhe::math::Affine_span::single_point);

    const std::vector<glm::vec3> collinear{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1.0f, 1.0f, 1.0f},
        glm::vec3{2.0f, 2.0f, 2.0f},
        glm::vec3{3.0f, 3.0f, 3.0f}
    };
    EXPECT_EQ(erhe::math::classify_affine_span(std::span<const glm::vec3>{collinear}), erhe::math::Affine_span::collinear);

    const std::vector<glm::vec3> coplanar = coplanar_quad_points();
    EXPECT_EQ(erhe::math::classify_affine_span(std::span<const glm::vec3>{coplanar}), erhe::math::Affine_span::coplanar);

    const std::vector<glm::vec3> tetrahedron = tetrahedron_points();
    EXPECT_EQ(erhe::math::classify_affine_span(std::span<const glm::vec3>{tetrahedron}), erhe::math::Affine_span::volumetric);
}

TEST(convex_hull, degenerate_input_is_refused)
{
    // Each of these used to reach Geogram's Delaunay, where a geo_assert
    // aborts (or, on Windows, blocks in geo_abort()'s getchar()).
    const std::vector<std::vector<glm::vec3>> degenerate_inputs{
        coplanar_quad_points(),
        std::vector<glm::vec3>{ // collinear triple padded to 4 points
            glm::vec3{0.0f, 0.0f, 0.0f},
            glm::vec3{1.0f, 0.0f, 0.0f},
            glm::vec3{2.0f, 0.0f, 0.0f},
            glm::vec3{3.0f, 0.0f, 0.0f}
        },
        std::vector<glm::vec3>(4, glm::vec3{1.0f, 2.0f, 3.0f}), // one repeated point
        std::vector<glm::vec3>{ // fewer than 4 points
            glm::vec3{0.0f, 0.0f, 0.0f},
            glm::vec3{1.0f, 0.0f, 0.0f},
            glm::vec3{0.0f, 1.0f, 0.0f}
        }
    };

    for (const std::vector<glm::vec3>& points : degenerate_inputs) {
        GEO::Mesh source{};
        fill_point_mesh(source, points);
        GEO::Mesh destination{};
        EXPECT_FALSE(erhe::geometry::make_convex_hull(source, destination));
        EXPECT_EQ(destination.facets.nb(), 0u);

        GEO::Mesh shape_mesh{};
        EXPECT_FALSE(erhe::geometry::shapes::make_convex_hull(shape_mesh, points));
        EXPECT_EQ(shape_mesh.facets.nb(), 0u);
    }
}

TEST(convex_hull, tetrahedron_and_cube_build)
{
    {
        const std::vector<glm::vec3> points = tetrahedron_points();
        GEO::Mesh source{};
        fill_point_mesh(source, points);
        GEO::Mesh destination{};
        EXPECT_TRUE(erhe::geometry::make_convex_hull(source, destination));
        EXPECT_EQ(destination.facets.nb(), 4u);
        EXPECT_EQ(destination.vertices.nb(), 4u);
    }
    {
        const std::vector<glm::vec3> points = cube_points();
        GEO::Mesh source{};
        fill_point_mesh(source, points);
        GEO::Mesh destination{};
        EXPECT_TRUE(erhe::geometry::make_convex_hull(source, destination));
        // The hull is triangulated: 6 quad faces become 12 triangles.
        EXPECT_EQ(destination.facets.nb(), 12u);
        EXPECT_EQ(destination.vertices.nb(), 8u);
    }
}

TEST(convex_hull, duplicated_cube_points_still_build)
{
    std::vector<glm::vec3> points = cube_points();
    const std::size_t      count  = points.size();
    for (std::size_t i = 0; i < count; ++i) {
        points.push_back(points[i]);
    }

    GEO::Mesh shape_mesh{};
    EXPECT_TRUE(erhe::geometry::shapes::make_convex_hull(shape_mesh, points));
    EXPECT_GT(shape_mesh.facets.nb(), 0u);
}
