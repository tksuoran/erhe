#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/capsule.hpp"

#include <geogram/basic/geometry.h>
#include <geogram/mesh/mesh.h>

#include <gtest/gtest.h>

#include <cmath>

using erhe::geometry::get_pointf;

namespace {

constexpr float pi = 3.14159265358979323846f;

// Capsule_builder creates ring vertices in stack-major order starting at
// index 0: stacks 1 .. stack_count - 1 with slice_count vertices each, then
// the bottom and top pole vertices.
auto ring_vertex(const int slice, const int stack, const int slice_count) -> GEO::index_t
{
    return static_cast<GEO::index_t>(((stack - 1) * slice_count) + slice);
}

class Capsule_fixture
{
public:
    Capsule_fixture(const float bottom_radius, const float top_radius, const float length, const int slice_count, const int hemisphere_stack_count)
        : geometry              {"capsule"}
        , mesh                  {geometry.get_mesh()}
        , bottom_radius         {bottom_radius}
        , top_radius            {top_radius}
        , length                {length}
        , slice_count           {slice_count}
        , hemisphere_stack_count{hemisphere_stack_count}
        , stack_count           {(2 * hemisphere_stack_count) + 1}
        , sin_alpha             {(bottom_radius == top_radius) ? 0.0f : ((bottom_radius - top_radius) / length)}
        , alpha                 {std::asin(sin_alpha)}
        , cos_alpha             {std::cos(alpha)}
    {
        erhe::geometry::shapes::make_capsule(mesh, bottom_radius, top_radius, length, slice_count, hemisphere_stack_count);
    }

    erhe::geometry::Geometry geometry;
    GEO::Mesh&               mesh;
    const float              bottom_radius;
    const float              top_radius;
    const float              length;
    const int                slice_count;
    const int                hemisphere_stack_count;
    const int                stack_count;
    const float              sin_alpha;
    const float              alpha;
    const float              cos_alpha;
};

} // anonymous namespace

// The cap normal at each junction ring must equal the cone body normal:
// latitude angle alpha with sin(alpha) = (bottom_radius - top_radius) / length.
// Because the cone normal is constant along the slant, this is equivalent to
// the bottom and top junction ring normals being identical per slice.
TEST(TaperedCapsule, JunctionNormalContinuity)
{
    Capsule_fixture f{1.0f, 0.5f, 2.0f, 8, 4};
    erhe::geometry::Mesh_attributes attributes{f.mesh};

    const int m = f.hemisphere_stack_count;
    for (int slice = 0; slice < f.slice_count; ++slice) {
        const float phi = 2.0f * pi * static_cast<float>(slice) / static_cast<float>(f.slice_count);
        const GEO::vec3f expected_normal{
            f.cos_alpha * std::cos(phi),
            f.sin_alpha,
            f.cos_alpha * std::sin(phi)
        };
        const GEO::vec3f n_bottom = attributes.vertex_normal.get(ring_vertex(slice, m,     f.slice_count));
        const GEO::vec3f n_top    = attributes.vertex_normal.get(ring_vertex(slice, m + 1, f.slice_count));

        EXPECT_NEAR(n_bottom.x, expected_normal.x, 1e-6f);
        EXPECT_NEAR(n_bottom.y, expected_normal.y, 1e-6f);
        EXPECT_NEAR(n_bottom.z, expected_normal.z, 1e-6f);
        EXPECT_NEAR(n_top.x,    expected_normal.x, 1e-6f);
        EXPECT_NEAR(n_top.y,    expected_normal.y, 1e-6f);
        EXPECT_NEAR(n_top.z,    expected_normal.z, 1e-6f);
    }
}

// True tangency, checked from positions alone: along one slice the cone slant
// edge (top junction vertex - bottom junction vertex) must be perpendicular
// to the shared junction normal.
TEST(TaperedCapsule, BodyIsTangentToBothCaps)
{
    Capsule_fixture f{1.0f, 0.5f, 2.0f, 8, 4};
    erhe::geometry::Mesh_attributes attributes{f.mesh};

    const int m = f.hemisphere_stack_count;
    for (int slice = 0; slice < f.slice_count; ++slice) {
        const GEO::vec3f p_bottom = get_pointf(f.mesh.vertices, ring_vertex(slice, m,     f.slice_count));
        const GEO::vec3f p_top    = get_pointf(f.mesh.vertices, ring_vertex(slice, m + 1, f.slice_count));
        const GEO::vec3f normal   = attributes.vertex_normal.get(ring_vertex(slice, m, f.slice_count));
        const GEO::vec3f slant    = p_top - p_bottom;
        EXPECT_GT(GEO::length(slant), 0.0f);
        EXPECT_NEAR(GEO::dot(slant, normal), 0.0f, 1e-5f);
    }
}

// Every cap vertex must lie exactly on its cap sphere; with different radii
// the caps are spherical caps cut at latitude alpha, not hemispheres.
TEST(TaperedCapsule, CapVerticesLieOnCapSpheres)
{
    Capsule_fixture f{1.0f, 0.25f, 2.0f, 8, 4};

    const float      half_length   = 0.5f * f.length;
    const GEO::vec3f bottom_center {0.0f, -half_length, 0.0f};
    const GEO::vec3f top_center    {0.0f,  half_length, 0.0f};
    const int        m             = f.hemisphere_stack_count;

    for (int stack = 1; stack <= m; ++stack) {
        for (int slice = 0; slice < f.slice_count; ++slice) {
            const GEO::vec3f position = get_pointf(f.mesh.vertices, ring_vertex(slice, stack, f.slice_count));
            EXPECT_NEAR(GEO::length(position - bottom_center), f.bottom_radius, 1e-5f);
        }
    }
    for (int stack = m + 1; stack <= 2 * m; ++stack) {
        for (int slice = 0; slice < f.slice_count; ++slice) {
            const GEO::vec3f position = get_pointf(f.mesh.vertices, ring_vertex(slice, stack, f.slice_count));
            EXPECT_NEAR(GEO::length(position - top_center), f.top_radius, 1e-5f);
        }
    }

    // Pole vertices follow the ring vertices in creation order
    const GEO::index_t bottom_pole = static_cast<GEO::index_t>((f.stack_count - 1) * f.slice_count);
    const GEO::index_t top_pole    = bottom_pole + 1;
    const GEO::vec3f   p_bottom    = get_pointf(f.mesh.vertices, bottom_pole);
    const GEO::vec3f   p_top       = get_pointf(f.mesh.vertices, top_pole);
    EXPECT_NEAR(p_bottom.x, 0.0f, 1e-6f);
    EXPECT_NEAR(p_bottom.y, -half_length - f.bottom_radius, 1e-6f);
    EXPECT_NEAR(p_bottom.z, 0.0f, 1e-6f);
    EXPECT_NEAR(p_top.x, 0.0f, 1e-6f);
    EXPECT_NEAR(p_top.y, half_length + f.top_radius, 1e-6f);
    EXPECT_NEAR(p_top.z, 0.0f, 1e-6f);
}

// Junction rings sit at the analytic tangency circles of the cone:
// ring radius r * cos(alpha) at height center_y + r * sin(alpha).
TEST(TaperedCapsule, JunctionRingsMatchAnalyticTangencyCircles)
{
    Capsule_fixture f{1.0f, 0.5f, 2.0f, 8, 4};

    const float half_length = 0.5f * f.length;
    const int   m           = f.hemisphere_stack_count;
    for (int slice = 0; slice < f.slice_count; ++slice) {
        const GEO::vec3f p_bottom = get_pointf(f.mesh.vertices, ring_vertex(slice, m,     f.slice_count));
        const GEO::vec3f p_top    = get_pointf(f.mesh.vertices, ring_vertex(slice, m + 1, f.slice_count));

        const float bottom_ring_radius = std::sqrt((p_bottom.x * p_bottom.x) + (p_bottom.z * p_bottom.z));
        const float top_ring_radius    = std::sqrt((p_top.x * p_top.x) + (p_top.z * p_top.z));

        EXPECT_NEAR(bottom_ring_radius, f.bottom_radius * f.cos_alpha, 1e-5f);
        EXPECT_NEAR(p_bottom.y, -half_length + (f.bottom_radius * f.sin_alpha), 1e-5f);
        EXPECT_NEAR(top_ring_radius, f.top_radius * f.cos_alpha, 1e-5f);
        EXPECT_NEAR(p_top.y, half_length + (f.top_radius * f.sin_alpha), 1e-5f);
    }
}

// Inverted taper (top wider than bottom) tilts the junction latitude the
// other way: the shared junction normal points downward.
TEST(TaperedCapsule, InvertedTaperJunctionNormalPointsDown)
{
    Capsule_fixture f{0.5f, 1.0f, 2.0f, 8, 4};
    erhe::geometry::Mesh_attributes attributes{f.mesh};

    EXPECT_LT(f.sin_alpha, 0.0f);
    const int m = f.hemisphere_stack_count;
    for (int slice = 0; slice < f.slice_count; ++slice) {
        const GEO::vec3f n_bottom = attributes.vertex_normal.get(ring_vertex(slice, m,     f.slice_count));
        const GEO::vec3f n_top    = attributes.vertex_normal.get(ring_vertex(slice, m + 1, f.slice_count));
        EXPECT_NEAR(n_bottom.y, f.sin_alpha, 1e-6f);
        EXPECT_NEAR(n_top.y,    f.sin_alpha, 1e-6f);
    }
}

// The equal-radius overload and the two-radius overload with equal radii must
// produce the same classic capsule: horizontal junction normals, junction
// rings at full radius and y = -/+ length / 2.
TEST(TaperedCapsule, EqualRadiiReducesToClassicCapsule)
{
    erhe::geometry::Geometry classic_geometry{"classic"};
    GEO::Mesh& classic_mesh = classic_geometry.get_mesh();
    erhe::geometry::shapes::make_capsule(classic_mesh, 1.0f, 2.0f, 8, 4);

    Capsule_fixture f{1.0f, 1.0f, 2.0f, 8, 4};

    ASSERT_EQ(classic_mesh.vertices.nb(), f.mesh.vertices.nb());
    ASSERT_EQ(classic_mesh.facets.nb(),   f.mesh.facets.nb());

    erhe::geometry::Mesh_attributes classic_attributes{classic_mesh};
    erhe::geometry::Mesh_attributes tapered_attributes{f.mesh};

    for (GEO::index_t vertex = 0; vertex < f.mesh.vertices.nb(); ++vertex) {
        const GEO::vec3f p_classic = get_pointf(classic_mesh.vertices, vertex);
        const GEO::vec3f p_tapered = get_pointf(f.mesh.vertices, vertex);
        EXPECT_FLOAT_EQ(p_classic.x, p_tapered.x);
        EXPECT_FLOAT_EQ(p_classic.y, p_tapered.y);
        EXPECT_FLOAT_EQ(p_classic.z, p_tapered.z);

        const GEO::vec3f n_classic = classic_attributes.vertex_normal.get(vertex);
        const GEO::vec3f n_tapered = tapered_attributes.vertex_normal.get(vertex);
        EXPECT_FLOAT_EQ(n_classic.x, n_tapered.x);
        EXPECT_FLOAT_EQ(n_classic.y, n_tapered.y);
        EXPECT_FLOAT_EQ(n_classic.z, n_tapered.z);
    }
}

// Texture coordinate v must increase strictly with stack so the parametri-
// zation stays monotonic from pole to pole even with strong taper.
TEST(TaperedCapsule, TexcoordVIsStrictlyMonotonic)
{
    Capsule_fixture f{1.0f, 0.25f, 2.0f, 8, 4};
    erhe::geometry::Mesh_attributes attributes{f.mesh};

    float previous_v = 0.0f; // v of the bottom pole
    for (int stack = 1; stack < f.stack_count; ++stack) {
        const GEO::vec2f texcoord = attributes.vertex_texcoord_0.get(ring_vertex(0, stack, f.slice_count));
        EXPECT_GT(texcoord.y, previous_v);
        previous_v = texcoord.y;
    }
    EXPECT_LT(previous_v, 1.0f); // top pole closes the range at v = 1
}
