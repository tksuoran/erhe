#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/csg/difference.hpp"
#include "erhe_geometry/operation/csg/intersection.hpp"
#include "erhe_geometry/operation/csg/union.hpp"
#include "erhe_geometry/shapes/box.hpp"

#include <geogram/mesh/mesh.h>

#include <gtest/gtest.h>

#include <memory>

namespace {

// erhe geometries are single precision; Geogram's exact-arithmetic CSG
// requires double precision access. These tests are the regression
// guard for Geometry_operation::run_mesh_boolean_operation(), which
// bridges the two (the direct call used to assert with
// "!single_precision()" in geogram mesh.h).
auto make_test_box(const char* name, float size, float offset) -> std::unique_ptr<erhe::geometry::Geometry>
{
    std::unique_ptr<erhe::geometry::Geometry> geometry = std::make_unique<erhe::geometry::Geometry>(name);
    erhe::geometry::shapes::make_box(geometry->get_mesh(), GEO::vec3f{size, size, size}, GEO::vec3i{1, 1, 1}, 1.0f);
    if (offset != 0.0f) {
        GEO::Mesh& mesh = geometry->get_mesh();
        for (GEO::index_t vertex : mesh.vertices) {
            const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, vertex);
            erhe::geometry::set_pointf(mesh.vertices, vertex, p + GEO::vec3f{offset, offset, offset});
        }
    }
    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges;
    geometry->process({.flags = flags});
    return geometry;
}

} // anonymous namespace

TEST(Csg, Union_OverlappingBoxes)
{
    std::unique_ptr<erhe::geometry::Geometry> lhs = make_test_box("lhs", 1.0f, 0.0f);
    std::unique_ptr<erhe::geometry::Geometry> rhs = make_test_box("rhs", 1.0f, 0.4f);
    erhe::geometry::Geometry result{"union"};
    erhe::geometry::operation::union_(*lhs, *rhs, result);
    const GEO::Mesh& mesh = result.get_mesh();
    EXPECT_GT(mesh.vertices.nb(), 0u);
    EXPECT_GT(mesh.facets.nb(), 0u);
    EXPECT_TRUE(mesh.vertices.single_precision());
}

TEST(Csg, Intersection_OverlappingBoxes)
{
    std::unique_ptr<erhe::geometry::Geometry> lhs = make_test_box("lhs", 1.0f, 0.0f);
    std::unique_ptr<erhe::geometry::Geometry> rhs = make_test_box("rhs", 1.0f, 0.4f);
    erhe::geometry::Geometry result{"intersection"};
    erhe::geometry::operation::intersection(*lhs, *rhs, result);
    const GEO::Mesh& mesh = result.get_mesh();
    EXPECT_GT(mesh.vertices.nb(), 0u);
    EXPECT_GT(mesh.facets.nb(), 0u);
    EXPECT_TRUE(mesh.vertices.single_precision());
}

TEST(Csg, Difference_OverlappingBoxes)
{
    std::unique_ptr<erhe::geometry::Geometry> lhs = make_test_box("lhs", 1.0f, 0.0f);
    std::unique_ptr<erhe::geometry::Geometry> rhs = make_test_box("rhs", 1.0f, 0.4f);
    erhe::geometry::Geometry result{"difference"};
    erhe::geometry::operation::difference(*lhs, *rhs, result);
    const GEO::Mesh& mesh = result.get_mesh();
    EXPECT_GT(mesh.vertices.nb(), 0u);
    EXPECT_GT(mesh.facets.nb(), 0u);
    EXPECT_TRUE(mesh.vertices.single_precision());
}
