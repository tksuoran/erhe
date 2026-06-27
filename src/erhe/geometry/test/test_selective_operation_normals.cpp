#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/box.hpp"
#include "erhe_geometry/operation/subdivision/catmull_clark_subdivision.hpp"
#include "erhe_geometry/operation/conway/subdivide.hpp"

#include <geogram/basic/geometry.h>

#include <gtest/gtest.h>

#include <cmath>
#include <set>

using erhe::geometry::Geometry;

namespace {

// A flat-shaded box stores one axis-aligned corner_normal on every surface
// corner. "Axis-aligned" means the normal is (up to sign) one of the unit axes.
auto is_axis_aligned(const GEO::vec3f n) -> bool
{
    const float ax = std::abs(n.x);
    const float ay = std::abs(n.y);
    const float az = std::abs(n.z);
    const float largest = std::max(ax, std::max(ay, az));
    // Largest component near 1, sum of the two smaller magnitudes near 0.
    const float second = (ax + ay + az) - largest;
    return (largest > 0.99f) && (second < 0.02f);
}

// Build a subdivided box (div x div x div) whose surface corners all carry an
// axis-aligned corner_normal, then run process() so connectivity caches exist.
auto make_box_geometry(const int div) -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geo = std::make_unique<Geometry>("box");
    erhe::geometry::shapes::make_box(
        geo->get_mesh(),
        GEO::vec3f{2.0f, 2.0f, 2.0f},
        GEO::vec3i{div, div, div},
        1.0f
    );
    const uint64_t flags =
        Geometry::process_flag_connect |
        Geometry::process_flag_build_edges |
        Geometry::process_flag_compute_facet_centroids |
        Geometry::process_flag_compute_smooth_vertex_normals |
        Geometry::process_flag_generate_facet_texture_coordinates;
    geo->process({.flags = flags});
    return geo;
}

} // anonymous namespace

// Sanity: the source box really is flat-shaded (every surface corner carries an
// axis-aligned corner_normal). If this fails the rest is meaningless.
TEST(SelectiveOperationNormals, Source_Box_Is_Flat_Shaded)
{
    std::unique_ptr<Geometry> box = make_box_geometry(2);
    const GEO::Mesh& mesh = box->get_mesh();
    const erhe::geometry::Mesh_attributes& attr = box->get_attributes();

    GEO::index_t corner_count = 0;
    for (GEO::index_t facet : mesh.facets) {
        for (GEO::index_t corner : mesh.facets.corners(facet)) {
            ++corner_count;
            ASSERT_TRUE(attr.corner_normal.has(corner)) << "source corner " << corner << " has no corner_normal";
            const GEO::vec3f n = attr.corner_normal.get(corner);
            EXPECT_TRUE(is_axis_aligned(n))
                << "source corner " << corner << " normal not axis-aligned: ("
                << n.x << ", " << n.y << ", " << n.z << ")";
        }
    }
    EXPECT_GT(corner_count, 0u);
}

// The reported bug: after a selective operation, the UNMODIFIED region's flat
// per-corner normals were being replaced by smooth/diagonal ones, so the cube
// looked rounded. Root cause was an uninitialized accumulator in
// interpolate_attribute (GEO::vecng's initializer_list constructor left .y/.z
// garbage for "T value{0}"), which corrupted every interpolated corner normal,
// including the 1:1 copies of unselected facets.
//
// Invariant after the fix:
//  - Every result corner has a present, unit-length corner_normal.
//  - Every corner that references an ORIGINAL (unmodified) source vertex keeps
//    an axis-aligned (flat) normal. Only the brand-new subdivision vertices
//    (centroid / edge midpoints created on the selected facet) may carry a
//    smoothed normal, which is the expected Catmull-Clark behavior.
TEST(SelectiveOperationNormals, Selective_CatmullClark_Keeps_Unmodified_Region_Flat)
{
    std::unique_ptr<Geometry> box = make_box_geometry(2);
    const GEO::index_t source_vertex_count = box->get_mesh().vertices.nb();

    std::set<GEO::index_t> selected_facets{0};
    std::unique_ptr<Geometry> result = std::make_unique<Geometry>("cc_subset");
    erhe::geometry::operation::catmull_clark_subdivision(*box, *result, &selected_facets);

    const GEO::Mesh& mesh = result->get_mesh();
    const erhe::geometry::Mesh_attributes& attr = result->get_attributes();

    GEO::index_t total                 = 0;
    GEO::index_t missing               = 0;
    GEO::index_t not_unit              = 0;
    GEO::index_t original_not_flat     = 0;
    for (GEO::index_t facet : mesh.facets) {
        for (GEO::index_t corner : mesh.facets.corners(facet)) {
            ++total;
            const GEO::index_t v = mesh.facet_corners.vertex(corner);
            if (!attr.corner_normal.has(corner)) {
                ++missing;
                continue;
            }
            const GEO::vec3f n   = attr.corner_normal.get(corner);
            const float      len = GEO::length(n);
            if (std::abs(len - 1.0f) > 1e-3f) {
                ++not_unit;
            }
            // Original (unmodified) vertices keep their flat per-face normal.
            const bool is_original_vertex = (v < source_vertex_count);
            if (is_original_vertex && !is_axis_aligned(n)) {
                ++original_not_flat;
                EXPECT_TRUE(false)
                    << "corner " << corner << " on original vertex " << v
                    << " is no longer flat: (" << n.x << ", " << n.y << ", " << n.z << ")";
            }
        }
    }

    EXPECT_EQ(missing,           0u) << missing       << " / " << total << " result corners lost corner_normal";
    EXPECT_EQ(not_unit,          0u) << not_unit      << " / " << total << " result corner normals are not unit length";
    EXPECT_EQ(original_not_flat, 0u) << original_not_flat << " unmodified-region corners were smoothed (cube no longer flat there)";
}

// Same invariant for the Conway "ortho" subdivide. Subdivide never moves a vertex
// or edge midpoint off the original geometry, so EVERY corner that references an
// original source vertex must keep its axis-aligned (flat) normal - the only
// corners allowed to differ are the brand-new edge-midpoint / centroid vertices
// created on the selected facet (and, on a box silhouette edge, those midpoints
// legitimately average two face normals).
TEST(SelectiveOperationNormals, Selective_Subdivide_Keeps_Unmodified_Region_Flat)
{
    std::unique_ptr<Geometry> box = make_box_geometry(2);
    const GEO::index_t source_vertex_count = box->get_mesh().vertices.nb();

    std::set<GEO::index_t> selected_facets{0};
    std::unique_ptr<Geometry> result = std::make_unique<Geometry>("subdivide_subset");
    erhe::geometry::operation::subdivide(*box, *result, &selected_facets);

    const GEO::Mesh& mesh = result->get_mesh();
    const erhe::geometry::Mesh_attributes& attr = result->get_attributes();

    GEO::index_t total             = 0;
    GEO::index_t missing           = 0;
    GEO::index_t not_unit          = 0;
    GEO::index_t original_not_flat = 0;
    for (GEO::index_t facet : mesh.facets) {
        for (GEO::index_t corner : mesh.facets.corners(facet)) {
            ++total;
            const GEO::index_t v = mesh.facet_corners.vertex(corner);
            if (!attr.corner_normal.has(corner)) {
                ++missing;
                continue;
            }
            const GEO::vec3f n   = attr.corner_normal.get(corner);
            const float      len = GEO::length(n);
            if (std::abs(len - 1.0f) > 1e-3f) {
                ++not_unit;
            }
            const bool is_original_vertex = (v < source_vertex_count);
            if (is_original_vertex && !is_axis_aligned(n)) {
                ++original_not_flat;
                EXPECT_TRUE(false)
                    << "corner " << corner << " on original vertex " << v
                    << " is no longer flat: (" << n.x << ", " << n.y << ", " << n.z << ")";
            }
        }
    }

    EXPECT_EQ(missing,           0u) << missing           << " / " << total << " result corners lost corner_normal";
    EXPECT_EQ(not_unit,          0u) << not_unit          << " / " << total << " result corner normals are not unit length";
    EXPECT_EQ(original_not_flat, 0u) << original_not_flat << " unmodified-region corners were smoothed (cube no longer flat there)";
}
