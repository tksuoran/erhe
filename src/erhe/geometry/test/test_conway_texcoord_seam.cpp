// Conway-operation corner-attribute seam handling, companion to
// test_catmull_clark_texcoord_seam.cpp: operations whose output facets each
// derive from a single source facet (subdivide, meta, gyro, truncate shrunken
// faces / corner caps, ambo facet-faces) must interpolate face-varying corner
// attributes within that facet only - never blending values across a seam
// edge. Vertex-faces (ambo / truncate faces replacing a source vertex) span
// many source facets by construction and are exempt.
//
// Setup: a cube where every facet carries a constant, facet-unique corner
// texcoord (facet f -> (f, 2f)), making every edge a seam; facet-local
// interpolation reproduces the constant exactly, cross-seam blending produces
// a mixture.

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/conway/ambo.hpp"
#include "erhe_geometry/operation/conway/gyro.hpp"
#include "erhe_geometry/operation/conway/meta.hpp"
#include "erhe_geometry/operation/conway/subdivide.hpp"
#include "erhe_geometry/operation/conway/truncate.hpp"
#include "erhe_geometry/shapes/regular_polyhedron.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <optional>
#include <set>

using erhe::geometry::Geometry;

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

// Every corner texcoord of every destination facet with at least
// min_corner_count corners must be exactly one of the seeded per-facet
// constants (u integral, v == 2u) and constant across the facet. Facets with
// fewer corners (vertex-faces spanning several source facets) are skipped.
// Returns the number of facets checked so tests can assert coverage.
auto expect_charts_intact(const Geometry& geometry, const GEO::index_t source_facet_count, const GEO::index_t min_corner_count = 0) -> GEO::index_t
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    const erhe::geometry::Mesh_attributes& attributes = geometry.get_attributes();
    GEO::index_t checked_facet_count = 0;
    for (GEO::index_t facet : mesh.facets) {
        if (mesh.facets.nb_corners(facet) < min_corner_count) {
            continue;
        }
        ++checked_facet_count;
        std::optional<GEO::vec2f> facet_value{};
        for (GEO::index_t corner : mesh.facets.corners(facet)) {
            const std::optional<GEO::vec2f> value = attributes.corner_texcoord_0.try_get(corner);
            EXPECT_TRUE(value.has_value()) << "corner " << corner << " of facet " << facet << " has no texcoord";
            if (!value.has_value()) {
                continue;
            }
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
    return checked_facet_count;
}

void expect_charts_intact_min_checked(const Geometry& geometry, const GEO::index_t source_facet_count, const GEO::index_t min_corner_count, const GEO::index_t min_checked)
{
    const GEO::index_t checked = expect_charts_intact(geometry, source_facet_count, min_corner_count);
    EXPECT_GE(checked, min_checked);
}

} // anonymous namespace

TEST(Conway_texcoord_seam, subdivide_respects_seams)
{
    const std::unique_ptr<Geometry> source = make_seam_cube();
    std::unique_ptr<Geometry> destination = std::make_unique<Geometry>("subdivided");
    erhe::geometry::operation::subdivide(*source, *destination, nullptr, nullptr);
    expect_charts_intact(*destination, source->get_mesh().facets.nb());
}

TEST(Conway_texcoord_seam, subdivide_selective_splice_respects_seams)
{
    const std::unique_ptr<Geometry> source = make_seam_cube();
    const std::set<GEO::index_t> selected_facets{0};
    std::unique_ptr<Geometry> destination = std::make_unique<Geometry>("subdivided");
    erhe::geometry::operation::subdivide(*source, *destination, &selected_facets, nullptr);
    expect_charts_intact(*destination, source->get_mesh().facets.nb());
}

TEST(Conway_texcoord_seam, meta_respects_seams)
{
    const std::unique_ptr<Geometry> source = make_seam_cube();
    std::unique_ptr<Geometry> destination = std::make_unique<Geometry>("meta");
    erhe::geometry::operation::meta(*source, *destination, nullptr, nullptr);
    expect_charts_intact(*destination, source->get_mesh().facets.nb());
}

TEST(Conway_texcoord_seam, gyro_respects_seams)
{
    const std::unique_ptr<Geometry> source = make_seam_cube();
    std::unique_ptr<Geometry> destination = std::make_unique<Geometry>("gyro");
    erhe::geometry::operation::gyro(*source, *destination, 1.0f / 3.0f, nullptr, nullptr);
    expect_charts_intact(*destination, source->get_mesh().facets.nb());
}

TEST(Conway_texcoord_seam, truncate_shrunken_faces_and_caps_respect_seams)
{
    // Whole-mesh truncation of a cube gives 6 octagons (shrunken faces, one per
    // source facet - must be chart-pure) and 8 triangles (vertex-faces spanning
    // three source facets - exempt). min_corner_count 8 selects the octagons.
    const std::unique_ptr<Geometry> source = make_seam_cube();
    std::unique_ptr<Geometry> destination = std::make_unique<Geometry>("truncated");
    erhe::geometry::operation::truncate(*source, *destination, 1.0f / 3.0f, nullptr, nullptr);
    expect_charts_intact_min_checked(*destination, source->get_mesh().facets.nb(), 8, 6);

    // Selective truncation of one facet: shrunken face plus corner caps
    // (triangles that include an original corner, chart-pure by construction of
    // src_corner + facet-local split corners) plus re-emitted unselected facets.
    // Vertex-faces do not occur (no interior vertex), so every facet must be pure.
    const std::set<GEO::index_t> selected_facets{0};
    std::unique_ptr<Geometry> selective_destination = std::make_unique<Geometry>("truncated_selective");
    erhe::geometry::operation::truncate(*source, *selective_destination, 1.0f / 3.0f, &selected_facets, nullptr);
    expect_charts_intact(*selective_destination, source->get_mesh().facets.nb());
}

TEST(Conway_texcoord_seam, ambo_facet_faces_respect_seams)
{
    // Ambo on a cube gives 6 squares (one per source facet - must be chart-pure)
    // and 8 triangles (vertex-faces, exempt). min_corner_count 4 selects the squares.
    const std::unique_ptr<Geometry> source = make_seam_cube();
    std::unique_ptr<Geometry> destination = std::make_unique<Geometry>("ambo");
    erhe::geometry::operation::ambo(*source, *destination);
    expect_charts_intact_min_checked(*destination, source->get_mesh().facets.nb(), 4, 6);
}
