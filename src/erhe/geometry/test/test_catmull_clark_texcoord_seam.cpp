// Catmull-Clark corner-attribute seam handling: corner attributes (texcoords,
// colors, corner normals) are face-varying and may be discontinuous across an
// edge (UV chart seams). Subdivision must interpolate them within each source
// facet only - never blending values across the seam edge.
//
// Setup: a cube where every facet carries a constant, facet-unique corner
// texcoord (facet f -> (f, 2f)). Every edge of the cube is then a seam. Any
// facet-local interpolation reproduces the constant exactly, while blending
// across an edge produces a mixture, so the tests assert that every
// destination facet's corner texcoords are all equal to one of the seeded
// per-facet constants.

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/subdivision/catmull_clark_subdivision.hpp"
#include "erhe_geometry/shapes/regular_polyhedron.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <optional>
#include <set>

using erhe::geometry::Geometry;
using erhe::geometry::operation::catmull_clark_subdivision;

namespace {

constexpr uint64_t process_flags =
    Geometry::process_flag_connect |
    Geometry::process_flag_build_edges |
    Geometry::process_flag_compute_facet_centroids |
    Geometry::process_flag_compute_smooth_vertex_normals |
    Geometry::process_flag_generate_facet_texture_coordinates;

// A processed cube with a constant, facet-unique corner texcoord chart on
// every facet: facet f gets (f, 2f) on all of its corners.
auto make_seam_cube() -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geometry = std::make_unique<Geometry>("seam_cube");
    erhe::geometry::shapes::make_cube(geometry->get_mesh(), 1.0f);
    geometry->process({.flags = process_flags});

    const GEO::Mesh& mesh = geometry->get_mesh();
    erhe::geometry::Mesh_attributes& attributes = geometry->get_attributes();
    for (GEO::index_t facet : mesh.facets) {
        const float f = static_cast<float>(facet);
        for (GEO::index_t corner : mesh.facets.corners(facet)) {
            attributes.corner_texcoord_0.set(corner, GEO::vec2f{f, 2.0f * f});
        }
    }
    return geometry;
}

// Every corner texcoord of every destination facet must be exactly one of the
// seeded per-facet constants (u integral, v == 2u), and constant across the
// facet. Any value off the chart constants means a seam was blended across.
void expect_charts_intact(const Geometry& geometry, const GEO::index_t source_facet_count)
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    const erhe::geometry::Mesh_attributes& attributes = geometry.get_attributes();
    for (GEO::index_t facet : mesh.facets) {
        std::optional<GEO::vec2f> facet_value{};
        for (GEO::index_t corner : mesh.facets.corners(facet)) {
            const std::optional<GEO::vec2f> value = attributes.corner_texcoord_0.try_get(corner);
            ASSERT_TRUE(value.has_value()) << "corner " << corner << " of facet " << facet << " has no texcoord";
            const GEO::vec2f uv = value.value();
            EXPECT_EQ(uv.x, std::floor(uv.x)) << "facet " << facet << " corner " << corner << " u blended across a seam: " << uv.x;
            EXPECT_GE(uv.x, 0.0f);
            EXPECT_LT(uv.x, static_cast<float>(source_facet_count));
            EXPECT_EQ(uv.y, 2.0f * uv.x) << "facet " << facet << " corner " << corner << " v blended across a seam: " << uv.y;
            if (!facet_value.has_value()) {
                facet_value = uv;
            } else {
                EXPECT_EQ(uv.x, facet_value.value().x) << "facet " << facet << " mixes charts";
                EXPECT_EQ(uv.y, facet_value.value().y) << "facet " << facet << " mixes charts";
            }
        }
    }
}

} // anonymous namespace

TEST(Catmull_clark_texcoord_seam, whole_mesh_respects_seams)
{
    const std::unique_ptr<Geometry> source = make_seam_cube();
    const GEO::index_t source_facet_count = source->get_mesh().facets.nb();

    std::unique_ptr<Geometry> destination = std::make_unique<Geometry>("subdivided");
    catmull_clark_subdivision(*source, *destination, nullptr, nullptr, process_flags, process_flags);

    expect_charts_intact(*destination, source_facet_count);
}

TEST(Catmull_clark_texcoord_seam, iterated_subdivision_respects_seams)
{
    std::unique_ptr<Geometry> current = make_seam_cube();
    const GEO::index_t source_facet_count = current->get_mesh().facets.nb();

    for (int i = 0; i < 2; ++i) {
        std::unique_ptr<Geometry> next = std::make_unique<Geometry>("subdivided");
        catmull_clark_subdivision(*current, *next, nullptr, nullptr, process_flags, process_flags);
        current = std::move(next);
    }

    expect_charts_intact(*current, source_facet_count);
}

TEST(Catmull_clark_texcoord_seam, selective_boundary_splice_respects_seams)
{
    const std::unique_ptr<Geometry> source = make_seam_cube();
    const GEO::index_t source_facet_count = source->get_mesh().facets.nb();

    const std::set<GEO::index_t> selected_facets{0};
    std::unique_ptr<Geometry> destination = std::make_unique<Geometry>("subdivided");
    catmull_clark_subdivision(*source, *destination, &selected_facets, nullptr, process_flags, process_flags);

    expect_charts_intact(*destination, source_facet_count);
}
