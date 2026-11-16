#include "level/level_group.hpp"
#include "level/kit_shape.hpp"
#include "level/kit_shape_library.hpp"

#include "erhe_geometry/geometry.hpp"

#include <fmt/format.h>
#include <geogram/mesh/mesh.h>
#include <gtest/gtest.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <memory>
#include <vector>

namespace {

using editor::Group_pose;
using editor::Kit_mirror_plane;
using editor::Kit_parameter;
using editor::Kit_parameter_type;
using editor::Kit_parameter_values;
using editor::Kit_shape_definition;
using editor::Kit_shape_library;
using editor::Mirror_image;
using editor::Mirrored_pose;

constexpr float c_tolerance        = 1.0e-4f;
constexpr float c_mesh_tolerance   = 1.0e-4f;
constexpr float c_module           = 1.0f;
constexpr float c_wall_height      = 3.0f;

const glm::vec3 c_x{1.0f, 0.0f, 0.0f};
const glm::vec3 c_y{0.0f, 1.0f, 0.0f};
const glm::vec3 c_z{0.0f, 0.0f, 1.0f};

auto yaw(const float degrees) -> glm::quat
{
    return glm::angleAxis(glm::radians(degrees), c_y);
}

void expect_vec3(const glm::vec3 got, const glm::vec3 expected)
{
    EXPECT_NEAR(got.x, expected.x, c_tolerance);
    EXPECT_NEAR(got.y, expected.y, c_tolerance);
    EXPECT_NEAR(got.z, expected.z, c_tolerance);
}

void expect_rotation(const glm::quat got, const glm::quat expected)
{
    // q and -q are the same rotation.
    EXPECT_NEAR(std::abs(glm::dot(got, expected)), 1.0f, c_tolerance);
}

auto get_library() -> const Kit_shape_library&
{
    static const Kit_shape_library library;
    return library;
}

auto mirror(const Group_pose& pose, const glm::vec3& point, const glm::vec3& normal, const std::vector<Kit_mirror_plane>& planes) -> Mirrored_pose
{
    return editor::mirror_pose(pose, point, normal, planes);
}

TEST(Level_group, position_reflects_across_the_plane)
{
    const Mirrored_pose result = mirror(Group_pose{.position = {3.0f, 1.0f, 2.0f}}, glm::vec3{1.0f, 0.0f, 0.0f}, c_x, {Kit_mirror_plane::x});
    expect_vec3(result.pose.position, {-1.0f, 1.0f, 2.0f});
    const Mirrored_pose across_z = mirror(Group_pose{.position = {3.0f, 1.0f, 2.0f}}, glm::vec3{0.0f, 0.0f, -1.0f}, c_z, {Kit_mirror_plane::x});
    expect_vec3(across_z.pose.position, {3.0f, 1.0f, -4.0f});
}

TEST(Level_group, symmetric_shape_aligned_with_the_plane_keeps_its_rotation)
{
    // A block (planes x and z) mirrored across x: its x plane is the reflection, so it only moves.
    const Mirrored_pose block = mirror(Group_pose{.position = {2.0f, 0.0f, 1.0f}}, glm::vec3{0.0f}, c_x, {Kit_mirror_plane::x, Kit_mirror_plane::z});
    EXPECT_EQ(block.image, Mirror_image::exact);
    EXPECT_EQ(block.plane, Kit_mirror_plane::x);
    expect_vec3(block.pose.position, {-2.0f, 0.0f, 1.0f});
    expect_rotation(block.pose.rotation, glm::quat{1.0f, 0.0f, 0.0f, 0.0f});

    // Yawed 90 degrees its z axis is world x: the z plane is the one aligned with the reflection.
    const Mirrored_pose yawed = mirror(Group_pose{.position = {2.0f, 0.0f, 1.0f}, .rotation = yaw(90.0f)}, glm::vec3{0.0f}, c_x, {Kit_mirror_plane::x, Kit_mirror_plane::z});
    EXPECT_EQ(yawed.plane, Kit_mirror_plane::z);
    expect_rotation(yawed.pose.rotation, yaw(90.0f));
}

TEST(Level_group, symmetric_shape_across_the_other_axis_turns_half_way)
{
    // A wedge (plane x only) rising toward -z mirrored across z rises toward +z: turned 180 degrees about y.
    const Mirrored_pose wedge = mirror(Group_pose{.position = {0.0f, 0.0f, 3.0f}}, glm::vec3{0.0f}, c_z, {Kit_mirror_plane::x});
    EXPECT_EQ(wedge.image, Mirror_image::exact);
    EXPECT_EQ(wedge.plane, Kit_mirror_plane::x);
    expect_vec3(wedge.pose.position, {0.0f, 0.0f, -3.0f});
    expect_rotation(wedge.pose.rotation, yaw(180.0f));
}

TEST(Level_group, shape_without_symmetry_uses_local_x_and_is_not_exact)
{
    const Mirrored_pose rock = mirror(Group_pose{.position = {1.0f, 0.0f, 0.0f}}, glm::vec3{0.0f}, c_z, {});
    EXPECT_EQ(rock.image, Mirror_image::not_exact);
    EXPECT_EQ(rock.plane, Kit_mirror_plane::x);
    expect_rotation(rock.pose.rotation, yaw(180.0f));
}

TEST(Level_group, mirrored_twice_is_the_original)
{
    const std::vector<std::vector<Kit_mirror_plane>> plane_sets{{}, {Kit_mirror_plane::x}, {Kit_mirror_plane::z}, {Kit_mirror_plane::x, Kit_mirror_plane::z}};
    const glm::vec3 point{0.5f, -1.0f, 2.0f};
    const glm::vec3 normal = glm::normalize(glm::vec3{1.0f, 0.2f, -0.5f});
    for (const std::vector<Kit_mirror_plane>& planes : plane_sets) {
        for (const float degrees : {0.0f, 30.0f, 90.0f, 135.0f, 270.0f}) {
            SCOPED_TRACE(fmt::format("{} planes, yaw {}", planes.size(), degrees));
            const Group_pose pose{
                .position = {1.5f, 0.25f, -3.0f},
                .rotation = yaw(degrees) * glm::angleAxis(glm::radians(20.0f), c_x),
                .scale    = {1.0f, 2.0f, 0.5f}
            };
            const Mirrored_pose once  = mirror(pose, point, normal, planes);
            const Mirrored_pose twice = mirror(once.pose, point, normal, planes);
            expect_vec3(twice.pose.position, pose.position);
            expect_rotation(twice.pose.rotation, pose.rotation);
            expect_vec3(twice.pose.scale, pose.scale);
        }
    }
}

TEST(Level_group, group_origin_is_the_base_center)
{
    erhe::math::Aabb bounds{};
    bounds.include(glm::vec3{-1.0f, 0.5f, 2.0f});
    bounds.include(glm::vec3{3.0f, 4.0f, 6.0f});
    expect_vec3(editor::get_group_origin(bounds), {1.0f, 0.5f, 4.0f});
}

auto generate(const Kit_parameter_values& values) -> std::unique_ptr<GEO::Mesh>
{
    std::unique_ptr<GEO::Mesh> mesh = std::make_unique<GEO::Mesh>(3, true);
    values.get_shape()->generator(values, *mesh);
    return mesh;
}

auto facet_points(const GEO::Mesh& mesh, const GEO::index_t facet) -> std::vector<glm::vec3>
{
    std::vector<glm::vec3> points;
    for (GEO::index_t lv = 0, end = mesh.facets.nb_vertices(facet); lv < end; ++lv) {
        const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, mesh.facets.vertex(facet, lv));
        points.push_back(glm::vec3{p.x, p.y, p.z});
    }
    return points;
}

auto same_point_set(const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b) -> bool
{
    if (a.size() != b.size()) {
        return false;
    }
    for (const glm::vec3& p : a) {
        bool found = false;
        for (const glm::vec3& q : b) {
            if (glm::length(p - q) <= c_mesh_tolerance) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

// Every facet reflected across the plane is a facet of the mesh: the mesh is its own mirror image.
auto check_mirror_symmetry(const Kit_parameter_values& values, const Kit_mirror_plane plane) -> std::string
{
    const std::unique_ptr<GEO::Mesh> mesh = generate(values);
    const glm::vec3 flip = (plane == Kit_mirror_plane::x) ? glm::vec3{-1.0f, 1.0f, 1.0f} : glm::vec3{1.0f, 1.0f, -1.0f};
    std::vector<bool> used(mesh->facets.nb(), false);
    for (const GEO::index_t facet : mesh->facets) {
        std::vector<glm::vec3> reflected = facet_points(*mesh, facet);
        for (glm::vec3& p : reflected) {
            p *= flip;
        }
        bool matched = false;
        for (const GEO::index_t other : mesh->facets) {
            if (!used[other] && same_point_set(reflected, facet_points(*mesh, other))) {
                used[other] = true;
                matched     = true;
                break;
            }
        }
        if (!matched) {
            return fmt::format("facet {} has no mirror image across {}", facet, editor::c_str(plane));
        }
    }
    return {};
}

auto second_values(const Kit_shape_definition& shape) -> Kit_parameter_values
{
    // Every derivable parameter derived from a seed other than the default: a second parameter set.
    Kit_parameter_values values = editor::make_default_values(shape, c_module, c_wall_height);
    EXPECT_TRUE(values.set_int("seed", 4711));
    for (std::size_t i = 0, end = shape.parameters.size(); i < end; ++i) {
        if (shape.parameters[i].derive == editor::Kit_derive::derivable) {
            values.set_value_source(i, editor::Kit_value_source::seed);
        }
    }
    values.resolve_derived(c_module, c_wall_height);
    return values;
}

TEST(Kit_shape_library, declared_mirror_planes_hold_for_the_generated_meshes)
{
    std::size_t declaring = 0;
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        if (shape.mirror_planes.empty()) {
            continue;
        }
        ++declaring;
        std::vector<Kit_parameter_values> value_sets{editor::make_default_values(shape, c_module, c_wall_height), second_values(shape)};
        // Every value of an enum parameter, the others at their defaults.
        for (const Kit_parameter& parameter : shape.parameters) {
            if (parameter.type != Kit_parameter_type::enum_value) {
                continue;
            }
            for (const std::string& value_name : parameter.enum_values) {
                Kit_parameter_values values = editor::make_default_values(shape, c_module, c_wall_height);
                ASSERT_TRUE(values.set_enum(parameter.name, value_name));
                value_sets.push_back(values);
            }
        }
        for (const Kit_parameter_values& values : value_sets) {
            ASSERT_EQ(values.validate(), "") << shape.id;
            for (const Kit_mirror_plane plane : shape.mirror_planes) {
                SCOPED_TRACE(fmt::format("{} {} across {}", shape.id, values.to_canonical_text(), editor::c_str(plane)));
                EXPECT_EQ(check_mirror_symmetry(values, plane), "");
            }
        }
    }
    // The basic and architecture shapes but the column and the tower (whose leaves and door bands have no exact mirror
    // triangulation), and the water, declare planes (the roof x alone: a shed rises toward -Z); the seeded nature shapes none.
    EXPECT_EQ(declaring, 14u);
}

TEST(Kit_shape_library, seeded_shapes_declare_no_mirror_plane)
{
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        if (shape.randomness == editor::Kit_randomness::seeded) {
            EXPECT_TRUE(shape.mirror_planes.empty()) << shape.id;
        }
    }
}

} // anonymous namespace
