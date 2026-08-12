// Phase 2 tests for erhe::voxel::Grid (doc/openvdb-integration-plan.md):
// SDF primitives, mesh round-trip against erhe::geometry::Geometry,
// CSG identities, offset/smooth behavior.

#include "erhe_voxel/voxel.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/box.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

namespace {

const erhe::voxel::Grid_create_info c_create_info{
    .voxel_size        = 0.1f,
    .narrow_band_width = 3
};

// Signed volume via divergence theorem; positive for outward-facing
// counter-clockwise winding in a right-handed coordinate system.
auto signed_volume(const erhe::geometry::Geometry& geometry) -> float
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    float volume_sum = 0.0f;
    for (GEO::index_t facet : mesh.facets) {
        const GEO::index_t corner_count = mesh.facets.nb_vertices(facet);
        const GEO::vec3f p0 = erhe::geometry::get_pointf(mesh.vertices, mesh.facets.vertex(facet, 0));
        for (GEO::index_t i = 1; i + 1 < corner_count; ++i) {
            const GEO::vec3f p1 = erhe::geometry::get_pointf(mesh.vertices, mesh.facets.vertex(facet, i));
            const GEO::vec3f p2 = erhe::geometry::get_pointf(mesh.vertices, mesh.facets.vertex(facet, i + 1));
            volume_sum += GEO::dot(p0, GEO::cross(p1, p2)) / 6.0f;
        }
    }
    return volume_sum;
}

} // anonymous namespace

TEST(Grid, sphere_sample_and_volume)
{
    const float radius = 1.0f;
    const erhe::voxel::Grid sphere = erhe::voxel::Grid::make_sphere(c_create_info, glm::vec3{0.0f}, radius);

    EXPECT_FALSE(sphere.is_empty());
    EXPECT_FLOAT_EQ(sphere.get_voxel_size(), c_create_info.voxel_size);
    EXPECT_FLOAT_EQ(sphere.get_background(), 0.3f);

    // Signed distances: 0 at the surface, negative inside, positive outside
    EXPECT_NEAR (sphere.sample(glm::vec3{1.0f, 0.0f, 0.0f}),  0.0f, 0.5f * c_create_info.voxel_size);
    EXPECT_NEAR (sphere.sample(glm::vec3{0.9f, 0.0f, 0.0f}), -0.1f, 0.5f * c_create_info.voxel_size);
    EXPECT_NEAR (sphere.sample(glm::vec3{1.1f, 0.0f, 0.0f}),  0.1f, 0.5f * c_create_info.voxel_size);
    EXPECT_FLOAT_EQ(sphere.sample(glm::vec3{0.0f}), -sphere.get_background()); // clamped deep inside

    // Volume ~ 4/3 pi r^3 = 4.18879
    EXPECT_NEAR(sphere.get_volume(), 4.18879f, 0.05f);

    // Aabb ~ [-1, 1]^3 with narrow band margin
    const erhe::math::Aabb aabb = sphere.get_aabb();
    EXPECT_NEAR(aabb.min.x, -1.0f, sphere.get_background() + c_create_info.voxel_size);
    EXPECT_NEAR(aabb.max.x,  1.0f, sphere.get_background() + c_create_info.voxel_size);
}

TEST(Grid, capsule)
{
    const erhe::voxel::Grid capsule = erhe::voxel::Grid::make_capsule(
        c_create_info,
        glm::vec3{-1.0f, 0.0f, 0.0f},
        glm::vec3{ 1.0f, 0.0f, 0.0f},
        0.5f,
        0.25f
    );
    EXPECT_FALSE(capsule.is_empty());
    EXPECT_LT(capsule.sample(glm::vec3{-1.0f, 0.0f, 0.0f}), 0.0f); // inside thick end
    EXPECT_LT(capsule.sample(glm::vec3{ 1.0f, 0.0f, 0.0f}), 0.0f); // inside thin end
    EXPECT_GT(capsule.sample(glm::vec3{ 1.0f, 0.4f, 0.0f}), 0.0f); // outside thin end radius
    EXPECT_LT(capsule.sample(glm::vec3{-1.0f, 0.4f, 0.0f}), 0.0f); // inside thick end radius
}

TEST(Grid, geometry_round_trip)
{
    // Box -> SDF -> mesh: volume and bounds must survive the round trip
    erhe::geometry::Geometry box_geometry{"box"};
    erhe::geometry::shapes::make_box(box_geometry.get_mesh(), 2.0f, 1.0f, 0.5f);

    const erhe::voxel::Grid grid = erhe::voxel::Grid::from_geometry(c_create_info, box_geometry);
    EXPECT_FALSE(grid.is_empty());
    EXPECT_LT(grid.sample(glm::vec3{0.0f}), 0.0f);                 // center is inside
    EXPECT_GT(grid.sample(glm::vec3{1.2f, 0.0f, 0.0f}), 0.0f);     // outside +x face
    EXPECT_NEAR(grid.get_volume(), 2.0f * 1.0f * 0.5f, 0.05f);

    erhe::geometry::Geometry mesh_geometry{"meshed"};
    grid.to_geometry(mesh_geometry);
    const GEO::Mesh& mesh = mesh_geometry.get_mesh();
    EXPECT_GT(mesh.vertices.nb(), GEO::index_t{0});
    EXPECT_GT(mesh.facets.nb(),   GEO::index_t{0});
    EXPECT_TRUE(mesh_geometry.validate().empty()) << mesh_geometry.validate();

    // Outward winding: positive signed volume, close to the box volume
    const float mesh_volume = signed_volume(mesh_geometry);
    EXPECT_NEAR(mesh_volume, 1.0f, 0.1f);

    const erhe::math::Aabb aabb = mesh_geometry.get_aabb();
    EXPECT_NEAR(aabb.min.x, -1.0f,  1.5f * c_create_info.voxel_size);
    EXPECT_NEAR(aabb.max.x,  1.0f,  1.5f * c_create_info.voxel_size);
    EXPECT_NEAR(aabb.min.y, -0.5f,  1.5f * c_create_info.voxel_size);
    EXPECT_NEAR(aabb.max.z,  0.25f, 1.5f * c_create_info.voxel_size);
}

TEST(Grid, csg_identities)
{
    const erhe::voxel::Grid sphere = erhe::voxel::Grid::make_sphere(c_create_info, glm::vec3{0.0f}, 1.0f);
    const float sphere_volume = sphere.get_volume();

    { // A union A == A
        erhe::voxel::Grid grid = sphere;
        grid.union_with(sphere);
        EXPECT_NEAR(grid.get_volume(), sphere_volume, 0.01f);
    }
    { // A intersect A == A
        erhe::voxel::Grid grid = sphere;
        grid.intersect(sphere);
        EXPECT_NEAR(grid.get_volume(), sphere_volume, 0.01f);
    }
    { // A subtract A == empty
        erhe::voxel::Grid grid = sphere;
        grid.subtract(sphere);
        EXPECT_TRUE(grid.is_empty());
    }
    { // Union of disjoint spheres: volumes add
        erhe::voxel::Grid left  = erhe::voxel::Grid::make_sphere(c_create_info, glm::vec3{-2.0f, 0.0f, 0.0f}, 1.0f);
        erhe::voxel::Grid right = erhe::voxel::Grid::make_sphere(c_create_info, glm::vec3{ 2.0f, 0.0f, 0.0f}, 1.0f);
        left.union_with(right);
        EXPECT_NEAR(left.get_volume(), 2.0f * sphere_volume, 0.02f);
        // Operand is unmodified
        EXPECT_NEAR(right.get_volume(), sphere_volume, 0.01f);
    }
    { // Near-half sphere: subtract a big sphere whose surface passes through
      // the origin. The cutting surface is not the x = 0 plane: it bulges by
      // rho^2 / (2 R), leaving integral pi/40 ~ 0.0785 extra volume for R = 10.
        erhe::voxel::Grid grid  = sphere;
        erhe::voxel::Grid other = erhe::voxel::Grid::make_sphere(c_create_info, glm::vec3{10.0f, 0.0f, 0.0f}, 10.0f);
        grid.subtract(other);
        EXPECT_NEAR(grid.get_volume(), 0.5f * sphere_volume + 0.0785f, 0.03f);
    }
}

TEST(Grid, offset_and_smooth)
{
    const float radius = 1.0f;
    { // Positive offset grows: r 1.0 -> 1.1
        erhe::voxel::Grid grid = erhe::voxel::Grid::make_sphere(c_create_info, glm::vec3{0.0f}, radius);
        grid.offset(0.1f);
        EXPECT_NEAR(grid.sample(glm::vec3{1.1f, 0.0f, 0.0f}), 0.0f, 0.5f * c_create_info.voxel_size);
        EXPECT_NEAR(grid.get_volume(), 4.18879f * 1.331f, 0.1f); // (1.1)^3
    }
    { // Negative offset shrinks: r 1.0 -> 0.9
        erhe::voxel::Grid grid = erhe::voxel::Grid::make_sphere(c_create_info, glm::vec3{0.0f}, radius);
        grid.offset(-0.1f);
        EXPECT_NEAR(grid.sample(glm::vec3{0.9f, 0.0f, 0.0f}), 0.0f, 0.5f * c_create_info.voxel_size);
        EXPECT_NEAR(grid.get_volume(), 4.18879f * 0.729f, 0.1f); // (0.9)^3
    }
    { // Smoothing a cube erodes volume toward rounder shape, stays nonempty
        erhe::geometry::Geometry box_geometry{"box"};
        erhe::geometry::shapes::make_box(box_geometry.get_mesh(), 1.0f, 1.0f, 1.0f);
        erhe::voxel::Grid grid = erhe::voxel::Grid::from_geometry(c_create_info, box_geometry);
        const float volume_before = grid.get_volume();
        grid.smooth(2);
        EXPECT_FALSE(grid.is_empty());
        EXPECT_LT(grid.get_volume(), volume_before);
        EXPECT_GT(grid.get_volume(), 0.5f * volume_before);
    }
}

TEST(Grid, copy_and_empty_semantics)
{
    const erhe::voxel::Grid_create_info create_info{};
    erhe::voxel::Grid empty{create_info};
    EXPECT_TRUE(empty.is_empty());
    EXPECT_EQ(empty.get_active_voxel_count(), std::int64_t{0});
    EXPECT_EQ(empty.get_volume(), 0.0f);
    EXPECT_FALSE(empty.get_aabb().is_valid());

    // Deep copy: mutating the copy leaves the source untouched
    erhe::voxel::Grid source = erhe::voxel::Grid::make_sphere(c_create_info, glm::vec3{0.0f}, 1.0f);
    erhe::voxel::Grid copy = source;
    copy.offset(0.5f);
    EXPECT_NEAR(source.sample(glm::vec3{1.0f, 0.0f, 0.0f}), 0.0f, 0.5f * c_create_info.voxel_size);
    EXPECT_LT  (copy.sample(glm::vec3{1.0f, 0.0f, 0.0f}), 0.0f);

    EXPECT_GT(source.get_memory_usage(), std::int64_t{0});
}
