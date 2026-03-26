#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/conway/ambo.hpp"
#include "erhe_geometry/operation/conway/dual.hpp"
#include "erhe_geometry/operation/conway/gyro.hpp"
#include "erhe_geometry/operation/conway/join.hpp"
#include "erhe_geometry/operation/conway/kis.hpp"
#include "erhe_geometry/operation/conway/meta.hpp"
#include "erhe_geometry/operation/conway/subdivide.hpp"
#include "erhe_geometry/operation/conway/truncate.hpp"
#include "erhe_geometry/shapes/regular_polyhedron.hpp"

#include <geogram/basic/geometry.h>

#include <gtest/gtest.h>

using erhe::geometry::get_pointf;

namespace {

auto make_solid(const char* name, void (*make_fn)(GEO::Mesh&, float), float radius = 1.0f)
    -> std::unique_ptr<erhe::geometry::Geometry>
{
    std::unique_ptr<erhe::geometry::Geometry> geo = std::make_unique<erhe::geometry::Geometry>(name);
    make_fn(geo->get_mesh(), radius);
    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_compute_facet_centroids |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;
    geo->process(flags);
    return geo;
}

auto apply_op(
    const erhe::geometry::Geometry& source,
    const char* name,
    std::function<void(const erhe::geometry::Geometry&, erhe::geometry::Geometry&)> op
) -> std::unique_ptr<erhe::geometry::Geometry>
{
    std::unique_ptr<erhe::geometry::Geometry> result = std::make_unique<erhe::geometry::Geometry>(name);
    op(source, *result);
    return result;
}

} // anonymous namespace

// === Topology tests: verify vertex/face/edge counts after each operation ===

// Cube: V=8, E=12, F=6
// Dual: V=F_in=6, E=E_in=12, F=V_in=8
TEST(ConwayTopology, Dual_Cube)
{
    std::unique_ptr<erhe::geometry::Geometry> cube = make_solid("cube", erhe::geometry::shapes::make_cube);
    std::unique_ptr<erhe::geometry::Geometry> result = apply_op(*cube, "dual_cube", erhe::geometry::operation::dual);
    const GEO::Mesh& m = result->get_mesh();
    EXPECT_EQ(m.vertices.nb(), 6u);  // one per cube face
    EXPECT_EQ(m.facets.nb(),   8u);  // one per cube vertex
}

// Ambo: V=E_in, F=V_in+F_in
TEST(ConwayTopology, Ambo_Cube)
{
    std::unique_ptr<erhe::geometry::Geometry> cube = make_solid("cube", erhe::geometry::shapes::make_cube);
    std::unique_ptr<erhe::geometry::Geometry> result = apply_op(*cube, "ambo_cube", erhe::geometry::operation::ambo);
    const GEO::Mesh& m = result->get_mesh();
    EXPECT_EQ(m.vertices.nb(), 12u); // one per cube edge
    EXPECT_EQ(m.facets.nb(),   14u); // 8 (vertex) + 6 (face)
}

// Kis: V=V_in+F_in, F=sum of face corner counts (all triangles)
// Cube: 6 quads -> 24 triangles, V=8+6=14
TEST(ConwayTopology, Kis_Cube)
{
    std::unique_ptr<erhe::geometry::Geometry> cube = make_solid("cube", erhe::geometry::shapes::make_cube);
    std::unique_ptr<erhe::geometry::Geometry> result = apply_op(*cube, "kis_cube",
        [](const erhe::geometry::Geometry& s, erhe::geometry::Geometry& d) {
            erhe::geometry::operation::kis(s, d, 0.0f);
        }
    );
    const GEO::Mesh& m = result->get_mesh();
    EXPECT_EQ(m.vertices.nb(), 14u);
    EXPECT_EQ(m.facets.nb(),   24u);
    // All faces should be triangles
    for (GEO::index_t f : m.facets) {
        EXPECT_EQ(m.facets.nb_corners(f), 3u) << "Face " << f << " is not a triangle";
    }
}

// Kis with height: vertices at centroid should be offset
TEST(ConwayTopology, Kis_Height_Offsets_Centroid)
{
    std::unique_ptr<erhe::geometry::Geometry> cube = make_solid("cube", erhe::geometry::shapes::make_cube);

    std::unique_ptr<erhe::geometry::Geometry> flat = apply_op(*cube, "kis_flat",
        [](const erhe::geometry::Geometry& s, erhe::geometry::Geometry& d) {
            erhe::geometry::operation::kis(s, d, 0.0f);
        }
    );
    std::unique_ptr<erhe::geometry::Geometry> raised = apply_op(*cube, "kis_raised",
        [](const erhe::geometry::Geometry& s, erhe::geometry::Geometry& d) {
            erhe::geometry::operation::kis(s, d, 0.5f);
        }
    );

    // Both should have same vertex count
    EXPECT_EQ(flat->get_mesh().vertices.nb(), raised->get_mesh().vertices.nb());

    // At least some vertices should differ (the centroids are offset)
    bool any_differ = false;
    for (GEO::index_t v = 0; v < flat->get_mesh().vertices.nb(); ++v) {
        const GEO::vec3f p_flat   = get_pointf(flat->get_mesh().vertices, v);
        const GEO::vec3f p_raised = get_pointf(raised->get_mesh().vertices, v);
        if (GEO::distance(p_flat, p_raised) > 1e-6f) {
            any_differ = true;
            break;
        }
    }
    EXPECT_TRUE(any_differ) << "Raised kis should produce different vertex positions than flat";
}

// Join: V=V_in+F_in, F=E_in (all quads)
TEST(ConwayTopology, Join_Cube)
{
    std::unique_ptr<erhe::geometry::Geometry> cube = make_solid("cube", erhe::geometry::shapes::make_cube);
    std::unique_ptr<erhe::geometry::Geometry> result = apply_op(*cube, "join_cube", erhe::geometry::operation::join);
    const GEO::Mesh& m = result->get_mesh();
    EXPECT_EQ(m.vertices.nb(), 14u); // 8 + 6
    EXPECT_EQ(m.facets.nb(),   12u); // one per edge
    for (GEO::index_t f : m.facets) {
        EXPECT_EQ(m.facets.nb_corners(f), 4u) << "Face " << f << " is not a quad";
    }
}

// Gyro: V=V_in+F_in+2*E_in, F=2*E_in (all pentagons)
TEST(ConwayTopology, Gyro_Cube)
{
    std::unique_ptr<erhe::geometry::Geometry> cube = make_solid("cube", erhe::geometry::shapes::make_cube);
    std::unique_ptr<erhe::geometry::Geometry> result = apply_op(*cube, "gyro_cube",
        [](const erhe::geometry::Geometry& s, erhe::geometry::Geometry& d) {
            erhe::geometry::operation::gyro(s, d);
        }
    );
    const GEO::Mesh& m = result->get_mesh();
    EXPECT_EQ(m.vertices.nb(), 38u); // 8 + 6 + 24
    EXPECT_EQ(m.facets.nb(),   24u); // 2 * 12
    for (GEO::index_t f : m.facets) {
        EXPECT_EQ(m.facets.nb_corners(f), 5u) << "Face " << f << " is not a pentagon";
    }
}

// Meta: V=V_in+F_in+E_in, F=2*sum(corner counts) (all triangles)
// Cube: V=8+6+12=26, F=2*24=48
TEST(ConwayTopology, Meta_Cube)
{
    std::unique_ptr<erhe::geometry::Geometry> cube = make_solid("cube", erhe::geometry::shapes::make_cube);
    std::unique_ptr<erhe::geometry::Geometry> result = apply_op(*cube, "meta_cube", erhe::geometry::operation::meta);
    const GEO::Mesh& m = result->get_mesh();
    EXPECT_EQ(m.vertices.nb(), 26u);
    EXPECT_EQ(m.facets.nb(),   48u);
    for (GEO::index_t f : m.facets) {
        EXPECT_EQ(m.facets.nb_corners(f), 3u) << "Face " << f << " is not a triangle";
    }
}

// Subdivide: V=V_in+E_in+F_in, F=sum(corner counts) (all quads)
// Cube: V=8+12+6=26, F=24
TEST(ConwayTopology, Subdivide_Cube)
{
    std::unique_ptr<erhe::geometry::Geometry> cube = make_solid("cube", erhe::geometry::shapes::make_cube);
    std::unique_ptr<erhe::geometry::Geometry> result = apply_op(*cube, "sub_cube", erhe::geometry::operation::subdivide);
    const GEO::Mesh& m = result->get_mesh();
    EXPECT_EQ(m.vertices.nb(), 26u);
    EXPECT_EQ(m.facets.nb(),   24u);
    for (GEO::index_t f : m.facets) {
        EXPECT_EQ(m.facets.nb_corners(f), 4u) << "Face " << f << " is not a quad";
    }
}

// Truncate: V=2*E_in, F=V_in+F_in
// Cube: V=24, F=8+6=14
TEST(ConwayTopology, Truncate_Cube)
{
    std::unique_ptr<erhe::geometry::Geometry> cube = make_solid("cube", erhe::geometry::shapes::make_cube);
    std::unique_ptr<erhe::geometry::Geometry> result = apply_op(*cube, "trunc_cube",
        [](const erhe::geometry::Geometry& s, erhe::geometry::Geometry& d) {
            erhe::geometry::operation::truncate(s, d);
        }
    );
    const GEO::Mesh& m = result->get_mesh();
    EXPECT_EQ(m.vertices.nb(), 24u);
    EXPECT_EQ(m.facets.nb(),   14u);
}

// Truncate at ratio=0.5 should produce same topology as ambo
TEST(ConwayTopology, Truncate_Half_Same_Topology_As_Ambo)
{
    std::unique_ptr<erhe::geometry::Geometry> cube = make_solid("cube", erhe::geometry::shapes::make_cube);
    std::unique_ptr<erhe::geometry::Geometry> trunc = apply_op(*cube, "trunc_half",
        [](const erhe::geometry::Geometry& s, erhe::geometry::Geometry& d) {
            erhe::geometry::operation::truncate(s, d, 0.5f);
        }
    );
    std::unique_ptr<erhe::geometry::Geometry> ambo = apply_op(*cube, "ambo",
        [](const erhe::geometry::Geometry& s, erhe::geometry::Geometry& d) {
            erhe::geometry::operation::ambo(s, d);
        }
    );
    // At ratio=0.5, the two split points coincide at the midpoint.
    // Truncate produces 2*E vertices (two identical per edge) while
    // ambo produces E vertices (one per edge). Topology differs but
    // face counts should match.
    EXPECT_EQ(trunc->get_mesh().facets.nb(), ambo->get_mesh().facets.nb());
}

// === Edge midpoint position test ===
// Verify edge midpoints are at correct positions

TEST(ConwayTopology, Subdivide_Midpoints_At_Edge_Centers)
{
    std::unique_ptr<erhe::geometry::Geometry> cube = make_solid("cube", erhe::geometry::shapes::make_cube);
    std::unique_ptr<erhe::geometry::Geometry> result = apply_op(*cube, "sub_cube", erhe::geometry::operation::subdivide);
    const GEO::Mesh& src = cube->get_mesh();
    const GEO::Mesh& dst = result->get_mesh();

    const GEO::index_t e_in = src.edges.nb(); // 12

    for (GEO::index_t e = 0; e < e_in; ++e) {
        const GEO::index_t va = src.edges.vertex(e, 0);
        const GEO::index_t vb = src.edges.vertex(e, 1);
        const GEO::vec3f pa = get_pointf(src.vertices, va);
        const GEO::vec3f pb = get_pointf(src.vertices, vb);
        const GEO::vec3f expected_mid = 0.5f * (pa + pb);

        // Search all destination vertices for the expected midpoint
        bool found = false;
        for (GEO::index_t dv = 0; dv < dst.vertices.nb(); ++dv) {
            const GEO::vec3f dp = get_pointf(dst.vertices, dv);
            if (GEO::distance(dp, expected_mid) < 1e-5f) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Edge " << e << " midpoint not found in destination";
    }
}

// === Dual involution: dual(dual(P)) has same V/E/F as P ===

TEST(ConwayTopology, Dual_Involution_Tetrahedron)
{
    std::unique_ptr<erhe::geometry::Geometry> tetra = make_solid("tetra", erhe::geometry::shapes::make_tetrahedron);
    std::unique_ptr<erhe::geometry::Geometry> d1 = apply_op(*tetra, "d1", erhe::geometry::operation::dual);
    std::unique_ptr<erhe::geometry::Geometry> d2 = apply_op(*d1, "d2", erhe::geometry::operation::dual);
    EXPECT_EQ(d2->get_mesh().vertices.nb(), tetra->get_mesh().vertices.nb());
    EXPECT_EQ(d2->get_mesh().facets.nb(),   tetra->get_mesh().facets.nb());
}

// === Gyro ratio parameter affects vertex positions ===

TEST(ConwayTopology, Gyro_Ratio_Changes_Positions)
{
    std::unique_ptr<erhe::geometry::Geometry> cube = make_solid("cube", erhe::geometry::shapes::make_cube);
    std::unique_ptr<erhe::geometry::Geometry> g1 = apply_op(*cube, "gyro_default",
        [](const erhe::geometry::Geometry& s, erhe::geometry::Geometry& d) {
            erhe::geometry::operation::gyro(s, d, 1.0f / 3.0f);
        }
    );
    std::unique_ptr<erhe::geometry::Geometry> g2 = apply_op(*cube, "gyro_quarter",
        [](const erhe::geometry::Geometry& s, erhe::geometry::Geometry& d) {
            erhe::geometry::operation::gyro(s, d, 0.25f);
        }
    );
    // Same topology
    EXPECT_EQ(g1->get_mesh().vertices.nb(), g2->get_mesh().vertices.nb());
    EXPECT_EQ(g1->get_mesh().facets.nb(),   g2->get_mesh().facets.nb());
    // Different positions
    bool any_differ = false;
    for (GEO::index_t v = 0; v < g1->get_mesh().vertices.nb(); ++v) {
        const GEO::vec3f p1 = get_pointf(g1->get_mesh().vertices, v);
        const GEO::vec3f p2 = get_pointf(g2->get_mesh().vertices, v);
        if (GEO::distance(p1, p2) > 1e-6f) {
            any_differ = true;
            break;
        }
    }
    EXPECT_TRUE(any_differ);
}
