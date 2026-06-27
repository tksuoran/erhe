#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/box.hpp"
#include "erhe_geometry/operation/subdivision/catmull_clark_subdivision.hpp"
#include "erhe_geometry/operation/conway/subdivide.hpp"
#include "erhe_geometry/operation/conway/meta.hpp"
#include "erhe_geometry/operation/conway/kis.hpp"
#include "erhe_geometry/operation/conway/join.hpp"
#include "erhe_geometry/operation/conway/gyro.hpp"

#include <geogram/basic/geometry.h>

#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <set>
#include <utility>

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

// Same invariant for the Conway "meta" operation. Like subdivide, meta never moves
// a vertex or edge midpoint off the original geometry, so EVERY corner referencing
// an original source vertex must keep its axis-aligned (flat) normal - only the
// brand-new edge-midpoint / centroid vertices created on the selected facet may
// differ.
TEST(SelectiveOperationNormals, Selective_Meta_Keeps_Unmodified_Region_Flat)
{
    std::unique_ptr<Geometry> box = make_box_geometry(2);
    const GEO::index_t source_vertex_count = box->get_mesh().vertices.nb();

    std::set<GEO::index_t> selected_facets{0};
    std::unique_ptr<Geometry> result = std::make_unique<Geometry>("meta_subset");
    erhe::geometry::operation::meta(*box, *result, &selected_facets);

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

// Same invariant for the Conway "kis" operation (zero height, so the centroid stays
// in the facet plane). Kis keeps every original edge and never inserts an edge
// midpoint, so EVERY result corner - including the centroid fan - references an
// original source vertex or the in-plane centroid and must stay axis-aligned (flat).
TEST(SelectiveOperationNormals, Selective_Kis_Keeps_Unmodified_Region_Flat)
{
    std::unique_ptr<Geometry> box = make_box_geometry(2);
    const GEO::index_t source_vertex_count = box->get_mesh().vertices.nb();

    std::set<GEO::index_t> selected_facets{0};
    std::unique_ptr<Geometry> result = std::make_unique<Geometry>("kis_subset");
    erhe::geometry::operation::kis(*box, *result, 0.0f, &selected_facets);

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

// Conway "join" rewires each facet into centroid kites and CONSUMES the original
// edge (it becomes a quad diagonal), so unlike the fan ops join does not carry a
// corner_normal onto its endpoint corners - only the centroid corners and the
// untouched 1:1-copied unselected facets do. The selective-join guarantees are
// therefore expressed differently:
//   1. The unselected region is preserved bit-for-bit: every corner that has a
//      corner_normal and references an original vertex stays axis-aligned (this is
//      exactly where the interpolation-corruption bug surfaced), and there are many
//      such corners (the bulk of the box that was not selected).
//   2. The result is a closed, consistently-oriented manifold: every undirected
//      edge is shared by exactly two facets, each traversed once in each direction
//      (a flipped boundary triangle or a crack would break this), and Euler
//      V - E + F = 2.
TEST(SelectiveOperationNormals, Selective_Join_Is_Watertight_And_Keeps_Unmodified_Region_Flat)
{
    std::unique_ptr<Geometry> box = make_box_geometry(2);
    const GEO::Mesh& src_mesh = box->get_mesh();
    const GEO::index_t source_vertex_count = src_mesh.vertices.nb();

    // Select two facets that share an edge, so the straddling-quad path (interior to
    // the selection) AND the boundary-triangle path are both exercised.
    std::set<GEO::index_t> selected_facets;
    for (GEO::index_t e = 0; (e < src_mesh.edges.nb()) && selected_facets.empty(); ++e) {
        const std::vector<GEO::index_t>& facets = box->get_edge_facets(e);
        if (facets.size() == 2) {
            selected_facets.insert(facets[0]);
            selected_facets.insert(facets[1]);
        }
    }
    ASSERT_EQ(selected_facets.size(), 2u) << "could not find two adjacent facets to select";

    std::unique_ptr<Geometry> result = std::make_unique<Geometry>("join_subset");
    erhe::geometry::operation::join(*box, *result, &selected_facets);

    const GEO::Mesh& mesh = result->get_mesh();
    const erhe::geometry::Mesh_attributes& attr = result->get_attributes();

    // (1) Unselected region stays flat.
    GEO::index_t present_original = 0;
    GEO::index_t original_not_flat = 0;
    GEO::index_t not_unit = 0;
    for (GEO::index_t facet : mesh.facets) {
        for (GEO::index_t corner : mesh.facets.corners(facet)) {
            const GEO::index_t v = mesh.facet_corners.vertex(corner);
            if (!attr.corner_normal.has(corner)) {
                continue;
            }
            const GEO::vec3f n   = attr.corner_normal.get(corner);
            const float      len = GEO::length(n);
            if (std::abs(len - 1.0f) > 1e-3f) {
                ++not_unit;
            }
            if (v < source_vertex_count) {
                ++present_original;
                if (!is_axis_aligned(n)) {
                    ++original_not_flat;
                    EXPECT_TRUE(false)
                        << "corner " << corner << " on original vertex " << v
                        << " is no longer flat: (" << n.x << ", " << n.y << ", " << n.z << ")";
                }
            }
        }
    }
    EXPECT_EQ(original_not_flat, 0u) << original_not_flat << " unmodified-region corners were smoothed";
    EXPECT_EQ(not_unit,          0u) << not_unit          << " result corner normals are not unit length";
    EXPECT_GT(present_original, 200u) << "expected the large unselected region to keep its corner normals";

    // (2) Closed, consistently-oriented manifold.
    std::map<std::pair<GEO::index_t, GEO::index_t>, int> directed_edges;
    for (GEO::index_t facet : mesh.facets) {
        const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
        for (GEO::index_t lc = 0; lc < corner_count; ++lc) {
            const GEO::index_t c  = mesh.facets.corner(facet, lc);
            const GEO::index_t cn = mesh.facets.corner(facet, (lc + 1) % corner_count);
            const GEO::index_t v0 = mesh.facet_corners.vertex(c);
            const GEO::index_t v1 = mesh.facet_corners.vertex(cn);
            ++directed_edges[std::make_pair(v0, v1)];
        }
    }
    GEO::index_t bad_direction = 0;
    GEO::index_t missing_twin  = 0;
    for (const auto& [edge, count] : directed_edges) {
        if (count != 1) {
            ++bad_direction; // an edge traversed twice the same way => flipped facet
        }
        const std::pair<GEO::index_t, GEO::index_t> twin{edge.second, edge.first};
        if (directed_edges.find(twin) == directed_edges.end()) {
            ++missing_twin; // open edge / crack
        }
    }
    EXPECT_EQ(bad_direction, 0u) << bad_direction << " directed edges traversed more than once (flipped facet)";
    EXPECT_EQ(missing_twin,  0u) << missing_twin  << " edges have no opposite-direction twin (crack / hole)";

    const long long V = static_cast<long long>(mesh.vertices.nb());
    const long long E = static_cast<long long>(mesh.edges.nb());
    const long long F = static_cast<long long>(mesh.facets.nb());
    EXPECT_EQ(V - E + F, 2) << "Euler characteristic != 2 (V=" << V << " E=" << E << " F=" << F << ")";
}

// Conway "gyro" is the first multi-split selective op: it puts TWO midpoints on
// every edge, so the boundary weld must splice both of them into the unselected
// neighbor (a single-midpoint splice would leave a T-junction). Like the other fan
// ops gyro keeps both split points on the original segment, so the unmodified region
// stays flat; and the result must remain a closed, consistently-oriented manifold.
TEST(SelectiveOperationNormals, Selective_Gyro_Watertight_And_Keeps_Unmodified_Region_Flat)
{
    std::unique_ptr<Geometry> box = make_box_geometry(2);
    const GEO::Mesh& src_mesh = box->get_mesh();
    const GEO::index_t source_vertex_count = src_mesh.vertices.nb();

    // Select two adjacent facets, so both the interior-to-selection shared edge (its
    // two midpoints serve both pentagons) and the multi-split boundary weld are
    // exercised.
    std::set<GEO::index_t> selected_facets;
    for (GEO::index_t e = 0; (e < src_mesh.edges.nb()) && selected_facets.empty(); ++e) {
        const std::vector<GEO::index_t>& facets = box->get_edge_facets(e);
        if (facets.size() == 2) {
            selected_facets.insert(facets[0]);
            selected_facets.insert(facets[1]);
        }
    }
    ASSERT_EQ(selected_facets.size(), 2u) << "could not find two adjacent facets to select";

    std::unique_ptr<Geometry> result = std::make_unique<Geometry>("gyro_subset");
    erhe::geometry::operation::gyro(*box, *result, 1.0f / 3.0f, &selected_facets);

    const GEO::Mesh& mesh = result->get_mesh();
    const erhe::geometry::Mesh_attributes& attr = result->get_attributes();

    // (1) Unmodified region stays flat; every result corner keeps a unit normal.
    GEO::index_t total = 0;
    GEO::index_t missing = 0;
    GEO::index_t not_unit = 0;
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
            if ((v < source_vertex_count) && !is_axis_aligned(n)) {
                ++original_not_flat;
                EXPECT_TRUE(false)
                    << "corner " << corner << " on original vertex " << v
                    << " is no longer flat: (" << n.x << ", " << n.y << ", " << n.z << ")";
            }
        }
    }
    EXPECT_EQ(missing,           0u) << missing           << " / " << total << " result corners lost corner_normal";
    EXPECT_EQ(not_unit,          0u) << not_unit          << " / " << total << " result corner normals are not unit length";
    EXPECT_EQ(original_not_flat, 0u) << original_not_flat << " unmodified-region corners were smoothed";

    // (2) Closed, consistently-oriented manifold (catches a multi-split T-junction).
    std::map<std::pair<GEO::index_t, GEO::index_t>, int> directed_edges;
    for (GEO::index_t facet : mesh.facets) {
        const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
        for (GEO::index_t lc = 0; lc < corner_count; ++lc) {
            const GEO::index_t c  = mesh.facets.corner(facet, lc);
            const GEO::index_t cn = mesh.facets.corner(facet, (lc + 1) % corner_count);
            const GEO::index_t v0 = mesh.facet_corners.vertex(c);
            const GEO::index_t v1 = mesh.facet_corners.vertex(cn);
            ++directed_edges[std::make_pair(v0, v1)];
        }
    }
    GEO::index_t bad_direction = 0;
    GEO::index_t missing_twin  = 0;
    for (const auto& [edge, count] : directed_edges) {
        if (count != 1) {
            ++bad_direction;
        }
        const std::pair<GEO::index_t, GEO::index_t> twin{edge.second, edge.first};
        if (directed_edges.find(twin) == directed_edges.end()) {
            ++missing_twin;
        }
    }
    EXPECT_EQ(bad_direction, 0u) << bad_direction << " directed edges traversed more than once (flipped facet)";
    EXPECT_EQ(missing_twin,  0u) << missing_twin  << " edges have no opposite-direction twin (crack / T-junction)";

    const long long V = static_cast<long long>(mesh.vertices.nb());
    const long long E = static_cast<long long>(mesh.edges.nb());
    const long long F = static_cast<long long>(mesh.facets.nb());
    EXPECT_EQ(V - E + F, 2) << "Euler characteristic != 2 (V=" << V << " E=" << E << " F=" << F << ")";
}
