#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/lattice_deform.hpp"
#include "erhe_geometry/shapes/box.hpp"

#include <geogram/basic/geometry.h>

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <functional>
#include <memory>

using erhe::geometry::get_pointf;
using erhe::geometry::operation::Lattice_deform_parameters;
using erhe::geometry::operation::Lattice_interpolation;
using erhe::geometry::operation::lattice_control_point_count;
using erhe::geometry::operation::lattice_deform;

namespace {

// Unit box: 8 vertices at (+-1, +-1, +-1)
auto make_test_box() -> std::unique_ptr<erhe::geometry::Geometry>
{
    std::unique_ptr<erhe::geometry::Geometry> geo = std::make_unique<erhe::geometry::Geometry>("box");
    erhe::geometry::shapes::make_box(geo->get_mesh(), -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);
    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_compute_facet_centroids |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;
    geo->process({.flags = flags});
    return geo;
}

auto make_params(const glm::ivec3 divisions, const glm::vec3 cage_min, const glm::vec3 cage_max) -> Lattice_deform_parameters
{
    Lattice_deform_parameters parameters;
    parameters.cage_min  = cage_min;
    parameters.cage_max  = cage_max;
    parameters.divisions = divisions;
    parameters.control_point_offsets.assign(lattice_control_point_count(divisions), glm::vec3{0.0f});
    return parameters;
}

auto apply(const erhe::geometry::Geometry& source, const Lattice_deform_parameters& parameters) -> std::unique_ptr<erhe::geometry::Geometry>
{
    std::unique_ptr<erhe::geometry::Geometry> result = std::make_unique<erhe::geometry::Geometry>("lattice deformed");
    lattice_deform(source, *result, parameters);
    return result;
}

// Expects every destination vertex at source position + expected_displacement(source position)
void expect_displaced(
    const erhe::geometry::Geometry& source,
    const erhe::geometry::Geometry& result,
    std::function<GEO::vec3f(const GEO::vec3f&)> expected_displacement,
    const float tolerance = 1e-5f
)
{
    const GEO::Mesh& src = source.get_mesh();
    const GEO::Mesh& dst = result.get_mesh();
    ASSERT_EQ(dst.vertices.nb(), src.vertices.nb());
    ASSERT_EQ(dst.facets.nb(),   src.facets.nb());
    for (GEO::index_t v = 0; v < src.vertices.nb(); ++v) {
        const GEO::vec3f p        = get_pointf(src.vertices, v);
        const GEO::vec3f expected = p + expected_displacement(p);
        const GEO::vec3f actual   = get_pointf(dst.vertices, v);
        EXPECT_LT(GEO::distance(actual, expected), tolerance)
            << "Vertex " << v << " at (" << p.x << ", " << p.y << ", " << p.z << ")"
            << " expected (" << expected.x << ", " << expected.y << ", " << expected.z << ")"
            << " got (" << actual.x << ", " << actual.y << ", " << actual.z << ")";
    }
}

} // anonymous namespace

// Zero offsets reproduce the input exactly, in both interpolation modes
TEST(LatticeDeform, Identity)
{
    std::unique_ptr<erhe::geometry::Geometry> box = make_test_box();
    for (const Lattice_interpolation interpolation : {Lattice_interpolation::trilinear, Lattice_interpolation::bezier}) {
        Lattice_deform_parameters parameters = make_params(glm::ivec3{2, 2, 2}, glm::vec3{-1.0f}, glm::vec3{1.0f});
        parameters.interpolation = interpolation;
        std::unique_ptr<erhe::geometry::Geometry> result = apply(*box, parameters);
        expect_displaced(*box, *result, [](const GEO::vec3f&) { return GEO::vec3f{0.0f, 0.0f, 0.0f}; });
    }
}

// A uniform offset on every control point is a rigid translation
// (trilinear: partition of unity per cell; Bezier: Bernstein partition of unity)
TEST(LatticeDeform, UniformOffsetIsTranslation)
{
    std::unique_ptr<erhe::geometry::Geometry> box = make_test_box();
    const glm::vec3 offset{0.5f, -0.25f, 1.0f};
    for (const Lattice_interpolation interpolation : {Lattice_interpolation::trilinear, Lattice_interpolation::bezier}) {
        Lattice_deform_parameters parameters = make_params(glm::ivec3{3, 2, 4}, glm::vec3{-1.0f}, glm::vec3{1.0f});
        parameters.interpolation = interpolation;
        std::fill(parameters.control_point_offsets.begin(), parameters.control_point_offsets.end(), offset);
        std::unique_ptr<erhe::geometry::Geometry> result = apply(*box, parameters);
        expect_displaced(*box, *result, [&](const GEO::vec3f&) { return GEO::vec3f{offset.x, offset.y, offset.z}; });
    }
}

// Trilinear interpolation is local: moving one corner control point moves only
// vertices whose cells touch it - for the unit box with a 2x2x2 lattice, only
// the vertex coincident with that corner
TEST(LatticeDeform, TrilinearLocality)
{
    std::unique_ptr<erhe::geometry::Geometry> box = make_test_box();
    Lattice_deform_parameters parameters = make_params(glm::ivec3{2, 2, 2}, glm::vec3{-1.0f}, glm::vec3{1.0f});
    const glm::vec3 offset{-0.5f, 0.25f, 0.125f};
    parameters.control_point_offsets[0] = offset; // control point (0, 0, 0) at cage_min
    std::unique_ptr<erhe::geometry::Geometry> result = apply(*box, parameters);
    expect_displaced(
        *box, *result,
        [&](const GEO::vec3f& p) {
            const bool at_cage_min = GEO::distance(p, GEO::vec3f{-1.0f, -1.0f, -1.0f}) < 1e-6f;
            return at_cage_min ? GEO::vec3f{offset.x, offset.y, offset.z} : GEO::vec3f{0.0f, 0.0f, 0.0f};
        }
    );
}

// Vertices outside the cage clamp to it: with the cage strictly inside the box,
// moving the whole max-x control point plane displaces the x = +1 vertices by
// the full offset and leaves the x = -1 vertices untouched
TEST(LatticeDeform, OutsideCageClampsToNearestFace)
{
    std::unique_ptr<erhe::geometry::Geometry> box = make_test_box();
    const glm::ivec3 divisions{2, 2, 2};
    Lattice_deform_parameters parameters = make_params(divisions, glm::vec3{-0.5f}, glm::vec3{0.5f});
    const glm::vec3 offset{1.0f, 0.0f, 0.0f};
    for (int k = 0; k <= divisions.z; ++k) {
        for (int j = 0; j <= divisions.y; ++j) {
            const std::size_t index =
                static_cast<std::size_t>(divisions.x) +
                static_cast<std::size_t>(divisions.x + 1) * (static_cast<std::size_t>(j) + static_cast<std::size_t>(divisions.y + 1) * static_cast<std::size_t>(k));
            parameters.control_point_offsets[index] = offset;
        }
    }
    std::unique_ptr<erhe::geometry::Geometry> result = apply(*box, parameters);
    expect_displaced(
        *box, *result,
        [&](const GEO::vec3f& p) {
            return (p.x > 0.0f) ? GEO::vec3f{offset.x, offset.y, offset.z} : GEO::vec3f{0.0f, 0.0f, 0.0f};
        }
    );
}

// A cage transform alone (zero offsets) never deforms: it repositions the
// deformation region, it does not move geometry
TEST(LatticeDeform, CageTransformAloneDoesNotDeform)
{
    std::unique_ptr<erhe::geometry::Geometry> box = make_test_box();
    Lattice_deform_parameters parameters = make_params(glm::ivec3{2, 2, 2}, glm::vec3{-1.0f}, glm::vec3{1.0f});
    parameters.cage_transform =
        glm::translate(glm::mat4{1.0f}, glm::vec3{0.4f, -0.2f, 0.7f}) *
        glm::rotate(glm::mat4{1.0f}, glm::radians(30.0f), glm::vec3{0.0f, 1.0f, 0.0f});
    std::unique_ptr<erhe::geometry::Geometry> result = apply(*box, parameters);
    expect_displaced(*box, *result, [](const GEO::vec3f&) { return GEO::vec3f{0.0f, 0.0f, 0.0f}; });
}

// Offsets live in cage space: with a rotated cage, a uniform offset displaces
// vertices by the rotated offset (the transform's linear part)
TEST(LatticeDeform, CageTransformRotatesOffsets)
{
    std::unique_ptr<erhe::geometry::Geometry> box = make_test_box();
    Lattice_deform_parameters parameters = make_params(glm::ivec3{2, 2, 2}, glm::vec3{-1.0f}, glm::vec3{1.0f});
    parameters.cage_transform = glm::rotate(glm::mat4{1.0f}, glm::radians(90.0f), glm::vec3{0.0f, 0.0f, 1.0f});
    const glm::vec3 offset{1.0f, 0.0f, 0.0f};
    std::fill(parameters.control_point_offsets.begin(), parameters.control_point_offsets.end(), offset);
    std::unique_ptr<erhe::geometry::Geometry> result = apply(*box, parameters);
    // Rotating (1,0,0) by 90 degrees about z gives (0,1,0)
    expect_displaced(*box, *result, [](const GEO::vec3f&) { return GEO::vec3f{0.0f, 1.0f, 0.0f}; }, 1e-4f);
}

// Invalid parameters (offset count mismatch, degenerate cage) pass the
// geometry through unchanged instead of crashing
TEST(LatticeDeform, InvalidParametersPassThrough)
{
    std::unique_ptr<erhe::geometry::Geometry> box = make_test_box();

    {
        Lattice_deform_parameters parameters = make_params(glm::ivec3{2, 2, 2}, glm::vec3{-1.0f}, glm::vec3{1.0f});
        parameters.control_point_offsets.resize(5, glm::vec3{1.0f}); // wrong count
        std::unique_ptr<erhe::geometry::Geometry> result = apply(*box, parameters);
        expect_displaced(*box, *result, [](const GEO::vec3f&) { return GEO::vec3f{0.0f, 0.0f, 0.0f}; });
    }

    {
        Lattice_deform_parameters parameters = make_params(glm::ivec3{2, 2, 2}, glm::vec3{1.0f}, glm::vec3{1.0f}); // zero extent
        std::fill(parameters.control_point_offsets.begin(), parameters.control_point_offsets.end(), glm::vec3{1.0f});
        std::unique_ptr<erhe::geometry::Geometry> result = apply(*box, parameters);
        expect_displaced(*box, *result, [](const GEO::vec3f&) { return GEO::vec3f{0.0f, 0.0f, 0.0f}; });
    }
}
