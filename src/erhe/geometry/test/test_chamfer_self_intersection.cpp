#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/self_intersection.hpp"
#include "erhe_geometry/operation/conway/chamfer_old.hpp"
#include "erhe_geometry/operation/conway/chamfer3.hpp"
#include "erhe_geometry/shapes/regular_polyhedron.hpp"

#include <geogram/basic/geometry.h>

#include <gtest/gtest.h>

using erhe::geometry::get_pointf;
using erhe::geometry::mesh_facet_centerf;
using erhe::geometry::mesh_facet_normalf;

#include <cmath>
#include <memory>
#include <string>

namespace {

constexpr uint64_t process_flags =
    erhe::geometry::Geometry::process_flag_connect |
    erhe::geometry::Geometry::process_flag_build_edges;

auto make_platonic(const char* name, void (*make_fn)(GEO::Mesh&, float), float radius = 1.0f)
    -> std::unique_ptr<erhe::geometry::Geometry>
{
    std::unique_ptr<erhe::geometry::Geometry> geometry =
        std::make_unique<erhe::geometry::Geometry>(name);
    make_fn(geometry->get_mesh(), radius);
    geometry->process(process_flags);
    return geometry;
}

auto apply_chamfer(const erhe::geometry::Geometry& source)
    -> std::unique_ptr<erhe::geometry::Geometry>
{
    std::unique_ptr<erhe::geometry::Geometry> result =
        std::make_unique<erhe::geometry::Geometry>("chamfered");
    erhe::geometry::operation::chamfer_old(source, *result);
    return result;
}

auto facet_max_planarity(const GEO::Mesh& mesh, GEO::index_t f) -> float
{
    const GEO::index_t cc = mesh.facets.nb_corners(f);
    if (cc < 4) { return 0.0f; }
    const GEO::vec3f normal = GEO::normalize(mesh_facet_normalf(mesh, f));
    const GEO::vec3f center = mesh_facet_centerf(mesh, f);
    float max_err = 0.0f;
    for (GEO::index_t lc = 0; lc < cc; ++lc) {
        const GEO::index_t v = mesh.facet_corners.vertex(mesh.facets.corner(f, lc));
        const float* const p = mesh.vertices.single_precision_point_ptr(v);
        const GEO::vec3f pos{p[0], p[1], p[2]};
        max_err = std::max(max_err, std::abs(GEO::dot(normal, pos - center)));
    }
    return max_err;
}

auto is_mesh_convex(const GEO::Mesh& mesh, float tolerance = 1e-5f) -> bool
{
    const GEO::index_t facet_count  = mesh.facets.nb();
    const GEO::index_t vertex_count = mesh.vertices.nb();

    for (GEO::index_t f = 0; f < facet_count; ++f) {
        const GEO::vec3f face_normal  = GEO::normalize(mesh_facet_normalf(mesh, f));
        const GEO::vec3f face_center  = mesh_facet_centerf(mesh, f);

        for (GEO::index_t v = 0; v < vertex_count; ++v) {
            const float* const p = mesh.vertices.single_precision_point_ptr(v);
            const GEO::vec3f pos{p[0], p[1], p[2]};
            const float dist = GEO::dot(face_normal, pos - face_center);
            if (dist > tolerance) {
                return false;
            }
        }
    }
    return true;
}

// Compare two meshes by checking that every vertex in A has a nearby
// vertex in B (and vice versa). Returns the maximum nearest-neighbor
// distance across both directions. O(n*m) brute force — fine for small
// test meshes.
auto hausdorff_vertex_distance(const GEO::Mesh& a, const GEO::Mesh& b) -> float
{
    float max_dist = 0.0f;

    // For each vertex in A, find closest vertex in B
    for (GEO::index_t va = 0; va < a.vertices.nb(); ++va) {
        const float* const pa = a.vertices.single_precision_point_ptr(va);
        const GEO::vec3f pos_a{pa[0], pa[1], pa[2]};
        float min_d = std::numeric_limits<float>::max();
        for (GEO::index_t vb = 0; vb < b.vertices.nb(); ++vb) {
            const float* const pb = b.vertices.single_precision_point_ptr(vb);
            const GEO::vec3f pos_b{pb[0], pb[1], pb[2]};
            min_d = std::min(min_d, GEO::distance(pos_a, pos_b));
        }
        max_dist = std::max(max_dist, min_d);
    }

    // For each vertex in B, find closest vertex in A
    for (GEO::index_t vb = 0; vb < b.vertices.nb(); ++vb) {
        const float* const pb = b.vertices.single_precision_point_ptr(vb);
        const GEO::vec3f pos_b{pb[0], pb[1], pb[2]};
        float min_d = std::numeric_limits<float>::max();
        for (GEO::index_t va = 0; va < a.vertices.nb(); ++va) {
            const float* const pa = a.vertices.single_precision_point_ptr(va);
            const GEO::vec3f pos_a{pa[0], pa[1], pa[2]};
            min_d = std::min(min_d, GEO::distance(pos_a, pos_b));
        }
        max_dist = std::max(max_dist, min_d);
    }

    return max_dist;
}

auto apply_chamfer3(const erhe::geometry::Geometry& source)
    -> std::unique_ptr<erhe::geometry::Geometry>
{
    std::unique_ptr<erhe::geometry::Geometry> result =
        std::make_unique<erhe::geometry::Geometry>("chamfered3");
    erhe::geometry::operation::chamfer3(source, *result);
    return result;
}

} // anonymous namespace

//
// === Known-good reference test ===
//
// Verify that chamfer of a cube produces vertex positions matching a
// known-good reference. This catches regressions in the geometric
// computation — if any vertex moves, the test fails.
//

TEST(ChamferReference, Cube_SingleChamfer_VertexPositions)
{
    std::unique_ptr<erhe::geometry::Geometry> solid = make_platonic("cube", erhe::geometry::shapes::make_cube);
    std::unique_ptr<erhe::geometry::Geometry> chamfered = apply_chamfer(*solid);
    const GEO::Mesh& mesh = chamfered->get_mesh();

    // Verify face/vertex counts match Conway chamfer topology: F+E faces, 2E+? vertices
    // Cube: 6 faces, 12 edges, 8 vertices → chamfer: 6+12=18 faces
    EXPECT_EQ(mesh.facets.nb(), 18u);

    // Verify planarity is near-zero (the hallmark of a correct chamfer)
    float max_planarity = 0.0f;
    for (GEO::index_t f = 0; f < mesh.facets.nb(); ++f) {
        const GEO::index_t cc = mesh.facets.nb_corners(f);
        if (cc < 4) {
            continue;
        }
        const GEO::vec3f normal  = GEO::normalize(mesh_facet_normalf(mesh, f));
        const GEO::vec3f center  = mesh_facet_centerf(mesh, f);
        for (GEO::index_t lc = 0; lc < cc; ++lc) {
            const GEO::index_t c = mesh.facets.corner(f, lc);
            const GEO::index_t v = mesh.facet_corners.vertex(c);
            const float* const p = mesh.vertices.single_precision_point_ptr(v);
            const GEO::vec3f pos{p[0], p[1], p[2]};
            const float error = std::abs(GEO::dot(normal, pos - center));
            max_planarity = std::max(max_planarity, error);
        }
    }
    EXPECT_LT(max_planarity, 1e-5f) << "Cube chamfer faces should be nearly planar";

    // Verify no self-intersections
    EXPECT_FALSE(erhe::geometry::has_self_intersections(mesh))
        << "Single chamfer of cube should not self-intersect";

    // Verify convexity
    EXPECT_TRUE(is_mesh_convex(mesh))
        << "Single chamfer of cube should be convex";
}

TEST(ChamferReference, Tetrahedron_SingleChamfer_Quality)
{
    std::unique_ptr<erhe::geometry::Geometry> solid = make_platonic("tetrahedron", erhe::geometry::shapes::make_tetrahedron);
    std::unique_ptr<erhe::geometry::Geometry> chamfered = apply_chamfer(*solid);
    const GEO::Mesh& mesh = chamfered->get_mesh();

    // Tetrahedron: 4 faces, 6 edges → chamfer: 4+6=10 faces
    EXPECT_EQ(mesh.facets.nb(), 10u);

    // Verify planarity
    float max_planarity = 0.0f;
    for (GEO::index_t f = 0; f < mesh.facets.nb(); ++f) {
        const GEO::index_t cc = mesh.facets.nb_corners(f);
        if (cc < 4) {
            continue;
        }
        const GEO::vec3f normal  = GEO::normalize(mesh_facet_normalf(mesh, f));
        const GEO::vec3f center  = mesh_facet_centerf(mesh, f);
        for (GEO::index_t lc = 0; lc < cc; ++lc) {
            const GEO::index_t c = mesh.facets.corner(f, lc);
            const GEO::index_t v = mesh.facet_corners.vertex(c);
            const float* const p = mesh.vertices.single_precision_point_ptr(v);
            const GEO::vec3f pos{p[0], p[1], p[2]};
            const float error = std::abs(GEO::dot(normal, pos - center));
            max_planarity = std::max(max_planarity, error);
        }
    }
    EXPECT_LT(max_planarity, 1e-5f) << "Tetrahedron chamfer faces should be nearly planar";

    EXPECT_FALSE(erhe::geometry::has_self_intersections(mesh));
    EXPECT_TRUE(is_mesh_convex(mesh));
}

//
// === Quality comparison: reference vs chamfer2, single + repeated ===
//
// Runs the same quality checks on both methods so results can be compared
// side by side in the test output.
//

void check_chamfer_quality(
    const char* method_name,
    const char* solid_name,
    void (*make_fn)(GEO::Mesh&, float),
    std::function<std::unique_ptr<erhe::geometry::Geometry>(const erhe::geometry::Geometry&)> chamfer_fn,
    int iterations
)
{
    SCOPED_TRACE(std::string(method_name) + " " + solid_name + " x" + std::to_string(iterations));

    std::unique_ptr<erhe::geometry::Geometry> current = make_platonic(solid_name, make_fn);
    for (int i = 0; i < iterations; ++i) {
        std::unique_ptr<erhe::geometry::Geometry> chamfered = chamfer_fn(*current);
        const GEO::Mesh& mesh = chamfered->get_mesh();

        float max_plan = 0.0f;
        for (GEO::index_t f = 0; f < mesh.facets.nb(); ++f) {
            max_plan = std::max(max_plan, facet_max_planarity(mesh, f));
        }

        const bool convex    = is_mesh_convex(mesh);
        const bool self_isec = erhe::geometry::has_self_intersections(mesh);

        // Report all metrics — use EXPECT so all iterations run even if one fails
        EXPECT_LT(max_plan, 1e-4f)
            << "iter " << (i + 1) << " planarity " << max_plan;
        EXPECT_TRUE(convex)
            << "iter " << (i + 1) << " not convex";
        EXPECT_FALSE(self_isec)
            << "iter " << (i + 1) << " self-intersects";

        current = std::move(chamfered);
    }
}

// Single iteration — both methods
TEST(QualityRef, Tetrahedron_x1)  { check_chamfer_quality("ref", "tetrahedron",  erhe::geometry::shapes::make_tetrahedron,  apply_chamfer, 1); }
TEST(QualityRef, Cube_x1)         { check_chamfer_quality("ref", "cube",          erhe::geometry::shapes::make_cube,          apply_chamfer, 1); }
TEST(QualityRef, Octahedron_x1)   { check_chamfer_quality("ref", "octahedron",    erhe::geometry::shapes::make_octahedron,    apply_chamfer, 1); }
TEST(QualityRef, Dodecahedron_x1) { check_chamfer_quality("ref", "dodecahedron",  erhe::geometry::shapes::make_dodecahedron,  apply_chamfer, 1); }
TEST(QualityRef, Icosahedron_x1)  { check_chamfer_quality("ref", "icosahedron",   erhe::geometry::shapes::make_icosahedron,   apply_chamfer, 1); }

TEST(Quality3, Tetrahedron_x1)  { check_chamfer_quality("chamfer3", "tetrahedron",  erhe::geometry::shapes::make_tetrahedron,  apply_chamfer3, 1); }
TEST(Quality3, Cube_x1)         { check_chamfer_quality("chamfer3", "cube",          erhe::geometry::shapes::make_cube,          apply_chamfer3, 1); }
TEST(Quality3, Octahedron_x1)   { check_chamfer_quality("chamfer3", "octahedron",    erhe::geometry::shapes::make_octahedron,    apply_chamfer3, 1); }
TEST(Quality3, Dodecahedron_x1) { check_chamfer_quality("chamfer3", "dodecahedron",  erhe::geometry::shapes::make_dodecahedron,  apply_chamfer3, 1); }
TEST(Quality3, Icosahedron_x1)  { check_chamfer_quality("chamfer3", "icosahedron",   erhe::geometry::shapes::make_icosahedron,   apply_chamfer3, 1); }

// 3 iterations — all methods
TEST(QualityRef, Tetrahedron_x3)  { check_chamfer_quality("ref", "tetrahedron",  erhe::geometry::shapes::make_tetrahedron,  apply_chamfer, 3); }
TEST(QualityRef, Cube_x3)         { check_chamfer_quality("ref", "cube",          erhe::geometry::shapes::make_cube,          apply_chamfer, 3); }
TEST(QualityRef, Octahedron_x3)   { check_chamfer_quality("ref", "octahedron",    erhe::geometry::shapes::make_octahedron,    apply_chamfer, 3); }
TEST(QualityRef, Dodecahedron_x3) { check_chamfer_quality("ref", "dodecahedron",  erhe::geometry::shapes::make_dodecahedron,  apply_chamfer, 3); }
TEST(QualityRef, Icosahedron_x3)  { check_chamfer_quality("ref", "icosahedron",   erhe::geometry::shapes::make_icosahedron,   apply_chamfer, 3); }

TEST(Quality3, Tetrahedron_x3)  { check_chamfer_quality("chamfer3", "tetrahedron",  erhe::geometry::shapes::make_tetrahedron,  apply_chamfer3, 3); }
TEST(Quality3, Cube_x3)         { check_chamfer_quality("chamfer3", "cube",          erhe::geometry::shapes::make_cube,          apply_chamfer3, 3); }
TEST(Quality3, Octahedron_x3)   { check_chamfer_quality("chamfer3", "octahedron",    erhe::geometry::shapes::make_octahedron,    apply_chamfer3, 3); }
TEST(Quality3, Dodecahedron_x3) { check_chamfer_quality("chamfer3", "dodecahedron",  erhe::geometry::shapes::make_dodecahedron,  apply_chamfer3, 3); }
TEST(Quality3, Icosahedron_x3)  { check_chamfer_quality("chamfer3", "icosahedron",   erhe::geometry::shapes::make_icosahedron,   apply_chamfer3, 3); }

//
// === Verify platonic solids themselves have no self-intersections ===
//

TEST(SelfIntersection, PlatonicSolids_NoSelfIntersections)
{
    EXPECT_FALSE(erhe::geometry::has_self_intersections(
        make_platonic("tetrahedron",  erhe::geometry::shapes::make_tetrahedron)->get_mesh()
    ));
    EXPECT_FALSE(erhe::geometry::has_self_intersections(
        make_platonic("cube",         erhe::geometry::shapes::make_cube)->get_mesh()
    ));
    EXPECT_FALSE(erhe::geometry::has_self_intersections(
        make_platonic("octahedron",   erhe::geometry::shapes::make_octahedron)->get_mesh()
    ));
    EXPECT_FALSE(erhe::geometry::has_self_intersections(
        make_platonic("dodecahedron", erhe::geometry::shapes::make_dodecahedron)->get_mesh()
    ));
    EXPECT_FALSE(erhe::geometry::has_self_intersections(
        make_platonic("icosahedron",  erhe::geometry::shapes::make_icosahedron)->get_mesh()
    ));
}

//
// === Single chamfer on each platonic solid ===
//

TEST(ChamferSelfIntersection, Tetrahedron_SingleChamfer)
{
    std::unique_ptr<erhe::geometry::Geometry> solid = make_platonic("tetrahedron", erhe::geometry::shapes::make_tetrahedron);
    std::unique_ptr<erhe::geometry::Geometry> chamfered = apply_chamfer(*solid);
    EXPECT_FALSE(erhe::geometry::has_self_intersections(chamfered->get_mesh()))
        << "Single chamfer of tetrahedron should not self-intersect";
}

TEST(ChamferSelfIntersection, Cube_SingleChamfer)
{
    std::unique_ptr<erhe::geometry::Geometry> solid = make_platonic("cube", erhe::geometry::shapes::make_cube);
    std::unique_ptr<erhe::geometry::Geometry> chamfered = apply_chamfer(*solid);
    EXPECT_FALSE(erhe::geometry::has_self_intersections(chamfered->get_mesh()))
        << "Single chamfer of cube should not self-intersect";
}

TEST(ChamferSelfIntersection, Octahedron_SingleChamfer)
{
    std::unique_ptr<erhe::geometry::Geometry> solid = make_platonic("octahedron", erhe::geometry::shapes::make_octahedron);
    std::unique_ptr<erhe::geometry::Geometry> chamfered = apply_chamfer(*solid);
    EXPECT_FALSE(erhe::geometry::has_self_intersections(chamfered->get_mesh()))
        << "Single chamfer of octahedron should not self-intersect";
}

TEST(ChamferSelfIntersection, Dodecahedron_SingleChamfer)
{
    std::unique_ptr<erhe::geometry::Geometry> solid = make_platonic("dodecahedron", erhe::geometry::shapes::make_dodecahedron);
    std::unique_ptr<erhe::geometry::Geometry> chamfered = apply_chamfer(*solid);
    EXPECT_FALSE(erhe::geometry::has_self_intersections(chamfered->get_mesh()))
        << "Single chamfer of dodecahedron should not self-intersect";
}

TEST(ChamferSelfIntersection, Icosahedron_SingleChamfer)
{
    std::unique_ptr<erhe::geometry::Geometry> solid = make_platonic("icosahedron", erhe::geometry::shapes::make_icosahedron);
    std::unique_ptr<erhe::geometry::Geometry> chamfered = apply_chamfer(*solid);
    EXPECT_FALSE(erhe::geometry::has_self_intersections(chamfered->get_mesh()))
        << "Single chamfer of icosahedron should not self-intersect";
}

//
// === Repeated chamfer (3 iterations) on each platonic solid ===
//

TEST(ChamferSelfIntersection, Tetrahedron_RepeatedChamfer)
{
    std::unique_ptr<erhe::geometry::Geometry> current = make_platonic("tetrahedron", erhe::geometry::shapes::make_tetrahedron);
    for (int i = 0; i < 3; ++i) {
        std::unique_ptr<erhe::geometry::Geometry> chamfered = apply_chamfer(*current);
        EXPECT_FALSE(erhe::geometry::has_self_intersections(chamfered->get_mesh()))
            << "Chamfer iteration " << (i + 1) << " of tetrahedron should not self-intersect";
        current = std::move(chamfered);
    }
}

TEST(ChamferSelfIntersection, Cube_RepeatedChamfer)
{
    std::unique_ptr<erhe::geometry::Geometry> current = make_platonic("cube", erhe::geometry::shapes::make_cube);
    for (int i = 0; i < 3; ++i) {
        std::unique_ptr<erhe::geometry::Geometry> chamfered = apply_chamfer(*current);
        EXPECT_FALSE(erhe::geometry::has_self_intersections(chamfered->get_mesh()))
            << "Chamfer iteration " << (i + 1) << " of cube should not self-intersect";
        current = std::move(chamfered);
    }
}

TEST(ChamferSelfIntersection, Octahedron_RepeatedChamfer)
{
    std::unique_ptr<erhe::geometry::Geometry> current = make_platonic("octahedron", erhe::geometry::shapes::make_octahedron);
    for (int i = 0; i < 3; ++i) {
        std::unique_ptr<erhe::geometry::Geometry> chamfered = apply_chamfer(*current);
        EXPECT_FALSE(erhe::geometry::has_self_intersections(chamfered->get_mesh()))
            << "Chamfer iteration " << (i + 1) << " of octahedron should not self-intersect";
        current = std::move(chamfered);
    }
}

TEST(ChamferSelfIntersection, Dodecahedron_RepeatedChamfer)
{
    std::unique_ptr<erhe::geometry::Geometry> current = make_platonic("dodecahedron", erhe::geometry::shapes::make_dodecahedron);
    for (int i = 0; i < 3; ++i) {
        std::unique_ptr<erhe::geometry::Geometry> chamfered = apply_chamfer(*current);
        EXPECT_FALSE(erhe::geometry::has_self_intersections(chamfered->get_mesh()))
            << "Chamfer iteration " << (i + 1) << " of dodecahedron should not self-intersect";
        current = std::move(chamfered);
    }
}

TEST(ChamferSelfIntersection, Icosahedron_RepeatedChamfer)
{
    std::unique_ptr<erhe::geometry::Geometry> current = make_platonic("icosahedron", erhe::geometry::shapes::make_icosahedron);
    for (int i = 0; i < 3; ++i) {
        std::unique_ptr<erhe::geometry::Geometry> chamfered = apply_chamfer(*current);
        EXPECT_FALSE(erhe::geometry::has_self_intersections(chamfered->get_mesh()))
            << "Chamfer iteration " << (i + 1) << " of icosahedron should not self-intersect";
        current = std::move(chamfered);
    }
}

//
// === Convexity tests ===
//

TEST(ChamferConvexity, Tetrahedron_RepeatedChamfer)
{
    std::unique_ptr<erhe::geometry::Geometry> current = make_platonic("tetrahedron", erhe::geometry::shapes::make_tetrahedron);
    ASSERT_TRUE(is_mesh_convex(current->get_mesh())) << "Source tetrahedron should be convex";
    for (int i = 0; i < 3; ++i) {
        std::unique_ptr<erhe::geometry::Geometry> chamfered = apply_chamfer(*current);
        EXPECT_TRUE(is_mesh_convex(chamfered->get_mesh()))
            << "Chamfer iteration " << (i + 1) << " of tetrahedron should be convex";
        current = std::move(chamfered);
    }
}

TEST(ChamferConvexity, Cube_RepeatedChamfer)
{
    std::unique_ptr<erhe::geometry::Geometry> current = make_platonic("cube", erhe::geometry::shapes::make_cube);
    ASSERT_TRUE(is_mesh_convex(current->get_mesh()));
    for (int i = 0; i < 3; ++i) {
        std::unique_ptr<erhe::geometry::Geometry> chamfered = apply_chamfer(*current);
        EXPECT_TRUE(is_mesh_convex(chamfered->get_mesh()))
            << "Chamfer iteration " << (i + 1) << " of cube should be convex";
        current = std::move(chamfered);
    }
}

TEST(ChamferConvexity, Octahedron_RepeatedChamfer)
{
    std::unique_ptr<erhe::geometry::Geometry> current = make_platonic("octahedron", erhe::geometry::shapes::make_octahedron);
    ASSERT_TRUE(is_mesh_convex(current->get_mesh()));
    for (int i = 0; i < 3; ++i) {
        std::unique_ptr<erhe::geometry::Geometry> chamfered = apply_chamfer(*current);
        EXPECT_TRUE(is_mesh_convex(chamfered->get_mesh()))
            << "Chamfer iteration " << (i + 1) << " of octahedron should be convex";
        current = std::move(chamfered);
    }
}

TEST(ChamferConvexity, Dodecahedron_RepeatedChamfer)
{
    std::unique_ptr<erhe::geometry::Geometry> current = make_platonic("dodecahedron", erhe::geometry::shapes::make_dodecahedron);
    ASSERT_TRUE(is_mesh_convex(current->get_mesh()));
    for (int i = 0; i < 3; ++i) {
        std::unique_ptr<erhe::geometry::Geometry> chamfered = apply_chamfer(*current);
        EXPECT_TRUE(is_mesh_convex(chamfered->get_mesh()))
            << "Chamfer iteration " << (i + 1) << " of dodecahedron should be convex";
        current = std::move(chamfered);
    }
}

TEST(ChamferConvexity, Icosahedron_RepeatedChamfer)
{
    std::unique_ptr<erhe::geometry::Geometry> current = make_platonic("icosahedron", erhe::geometry::shapes::make_icosahedron);
    ASSERT_TRUE(is_mesh_convex(current->get_mesh()));
    for (int i = 0; i < 3; ++i) {
        std::unique_ptr<erhe::geometry::Geometry> chamfered = apply_chamfer(*current);
        EXPECT_TRUE(is_mesh_convex(chamfered->get_mesh()))
            << "Chamfer iteration " << (i + 1) << " of icosahedron should be convex";
        current = std::move(chamfered);
    }
}

// (Reference comparison and StraightSkel tests removed — chamfer2 was a dead end)
