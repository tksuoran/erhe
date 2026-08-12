// Phase 1 smoke test for the OpenVDB dependency (doc/openvdb-integration-plan.md):
// proves that the exact OpenVDB core tools the SDF geometry-node work needs
// (level set construction, CSG, volumeToMesh) compile, link and run in the
// erhe build. The erhe_voxel wrapper library arrives in Phase 2.

#include <gtest/gtest.h>

#include <openvdb/openvdb.h>
#include <openvdb/tools/Composite.h>
#include <openvdb/tools/LevelSetSphere.h>
#include <openvdb/tools/VolumeToMesh.h>

#include <cstdint>
#include <vector>

namespace {

constexpr float c_voxel_size        = 0.5f;
constexpr float c_half_width_voxels = 3.0f; // openvdb::LEVEL_SET_HALF_WIDTH default

} // anonymous namespace

TEST(Openvdb_smoke, level_set_sphere_signed_distances)
{
    openvdb::initialize();

    const float radius = 10.0f;
    openvdb::FloatGrid::Ptr grid = openvdb::tools::createLevelSetSphere<openvdb::FloatGrid>(
        radius,
        openvdb::Vec3f{0.0f, 0.0f, 0.0f},
        c_voxel_size
    );
    ASSERT_TRUE(grid);
    EXPECT_EQ(grid->getGridClass(), openvdb::GRID_LEVEL_SET);

    const float background = c_half_width_voxels * c_voxel_size;
    EXPECT_FLOAT_EQ(grid->background(), background);

    openvdb::FloatGrid::ConstAccessor accessor = grid->getConstAccessor();

    // Voxel exactly on the surface: signed distance ~ 0
    const float surface_value = accessor.getValue(openvdb::Coord{20, 0, 0}); // (10, 0, 0) in world units
    EXPECT_NEAR(surface_value, 0.0f, c_voxel_size);

    // Center is far inside the narrow band: clamped to -background
    const float center_value = accessor.getValue(openvdb::Coord{0, 0, 0});
    EXPECT_FLOAT_EQ(center_value, -background);

    // Far outside the narrow band: +background
    const float outside_value = accessor.getValue(openvdb::Coord{100, 100, 100});
    EXPECT_FLOAT_EQ(outside_value, background);
}

TEST(Openvdb_smoke, csg_union_and_volume_to_mesh)
{
    openvdb::initialize();

    const float radius = 4.0f;
    openvdb::FloatGrid::Ptr left_sphere = openvdb::tools::createLevelSetSphere<openvdb::FloatGrid>(
        radius,
        openvdb::Vec3f{-3.0f, 0.0f, 0.0f},
        c_voxel_size
    );
    openvdb::FloatGrid::Ptr right_sphere = openvdb::tools::createLevelSetSphere<openvdb::FloatGrid>(
        radius,
        openvdb::Vec3f{3.0f, 0.0f, 0.0f},
        c_voxel_size
    );
    ASSERT_TRUE(left_sphere);
    ASSERT_TRUE(right_sphere);

    // csgUnion() consumes the second grid (exercises the TBB thread pool)
    openvdb::tools::csgUnion(*left_sphere, *right_sphere);

    std::vector<openvdb::Vec3s> points{};
    std::vector<openvdb::Vec3I> triangles{};
    std::vector<openvdb::Vec4I> quads{};
    openvdb::tools::volumeToMesh(*left_sphere, points, triangles, quads, 0.0, 0.0);

    EXPECT_GT(points.size(), std::size_t{0});
    EXPECT_GT(triangles.size() + quads.size(), std::size_t{0});

    // The union of spheres at x = -3 and x = +3 with radius 4 spans
    // approximately [-7, 7] x [-4, 4] x [-4, 4] in world units.
    openvdb::Vec3s minimum = points.front();
    openvdb::Vec3s maximum = points.front();
    for (const openvdb::Vec3s& point : points) {
        minimum = openvdb::math::minComponent(minimum, point);
        maximum = openvdb::math::maxComponent(maximum, point);
    }
    EXPECT_NEAR(minimum.x(), -7.0f, 2.0f * c_voxel_size);
    EXPECT_NEAR(maximum.x(),  7.0f, 2.0f * c_voxel_size);
    EXPECT_NEAR(minimum.y(), -4.0f, 2.0f * c_voxel_size);
    EXPECT_NEAR(maximum.y(),  4.0f, 2.0f * c_voxel_size);
}
