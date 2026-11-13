#include "navigation/navigation_build.hpp"
#include "navigation/navigation_mesh.hpp"
#include "navigation/navigation_mesh_io.hpp"

#include <Recast.h>

#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <taskflow/taskflow.hpp>

#include <bit>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

using editor::Navigation_build_settings;
using editor::Navigation_input;
using editor::Navigation_input_mesh;
using editor::Navigation_mesh;
using editor::Navigation_polygon;
using editor::Navigation_seed_result;

class Test_geometry
{
public:
    std::vector<glm::vec3> positions;
    std::vector<uint32_t>  indices;

    // Corners in an order whose (a, b, c) normal points up for walkable faces.
    void add_quad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d)
    {
        const uint32_t base = static_cast<uint32_t>(positions.size());
        positions.push_back(a);
        positions.push_back(b);
        positions.push_back(c);
        positions.push_back(d);
        indices.insert(indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
    }

    void add_floor(const float x0, const float z0, const float x1, const float z1, const float y)
    {
        add_quad({x0, y, z0}, {x0, y, z1}, {x1, y, z1}, {x1, y, z0});
    }

    void add_box(const glm::vec3& lo, const glm::vec3& hi)
    {
        add_floor(lo.x, lo.z, hi.x, hi.z, hi.y);                                         // top
        add_quad({lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z}); // bottom
        add_quad({lo.x, lo.y, lo.z}, {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, lo.y, lo.z});
        add_quad({lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z});
        add_quad({lo.x, lo.y, lo.z}, {lo.x, lo.y, hi.z}, {lo.x, hi.y, hi.z}, {lo.x, hi.y, lo.z});
        add_quad({hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {hi.x, lo.y, hi.z});
    }

    [[nodiscard]] auto make_input() const -> Navigation_input
    {
        Navigation_input input;
        input.meshes.push_back(Navigation_input_mesh{.positions = positions, .triangle_indices = indices});
        return input;
    }
};

[[nodiscard]] auto build(const Test_geometry& geometry, const Navigation_build_settings& settings = {}) -> std::unique_ptr<Navigation_mesh>
{
    tf::Executor executor{4};
    std::string  error;
    std::size_t  last_done  = 0;
    std::size_t  last_total = 0;
    std::unique_ptr<Navigation_mesh> mesh = editor::build_navigation_mesh(
        geometry.make_input(), settings, executor,
        [&last_done, &last_total](const std::size_t done, const std::size_t total) {
            last_done  = done;
            last_total = total;
        },
        error
    );
    EXPECT_TRUE(mesh) << error;
    EXPECT_EQ(last_done, last_total);
    return mesh;
}

[[nodiscard]] auto point_in_triangle_xz(const glm::vec2& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) -> bool
{
    const auto edge = [&p](const glm::vec3& u, const glm::vec3& v) -> float {
        return ((v.x - u.x) * (p.y - u.z)) - ((v.z - u.z) * (p.x - u.x));
    };
    const float e0 = edge(a, b);
    const float e1 = edge(b, c);
    const float e2 = edge(c, a);
    return ((e0 >= 0.0f) && (e1 >= 0.0f) && (e2 >= 0.0f)) || ((e0 <= 0.0f) && (e1 <= 0.0f) && (e2 <= 0.0f));
}

// Whether a polygon whose detail triangles lie between y_min and y_max covers the xz point.
[[nodiscard]] auto covered(const Navigation_mesh& mesh, const glm::vec2& p, const float y_min, const float y_max) -> bool
{
    const std::span<const glm::vec3> vertices = mesh.get_detail_triangle_vertices();
    for (std::size_t t = 0; (t + 2) < vertices.size(); t += 3) {
        const glm::vec3& a = vertices[t];
        const glm::vec3& b = vertices[t + 1];
        const glm::vec3& c = vertices[t + 2];
        if ((a.y < y_min) || (a.y > y_max)) {
            continue;
        }
        if (point_in_triangle_xz(p, a, b, c)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] auto area_between(const Navigation_mesh& mesh, const float y_min, const float y_max) -> double
{
    double area = 0.0;
    for (uint32_t i = 0, end = static_cast<uint32_t>(mesh.get_polygons().size()); i < end; ++i) {
        const float y = mesh.get_polygon_centroid(i).y;
        if ((y >= y_min) && (y <= y_max)) {
            area += static_cast<double>(mesh.get_polygons()[i].area);
        }
    }
    return area;
}

// Side of the floor left after the erosion band of agent_radius (in whole cells) on both sides.
[[nodiscard]] auto eroded_side(const float side, const Navigation_build_settings& settings) -> double
{
    const rcConfig config = editor::make_recast_config(settings);
    return static_cast<double>(side) - (2.0 * static_cast<double>(config.walkableRadius) * static_cast<double>(settings.cell_size));
}

} // anonymous namespace

TEST(Navigation, recast_config_converts_meters_to_cells)
{
    const Navigation_build_settings settings{};
    const rcConfig config = editor::make_recast_config(settings);
    EXPECT_EQ(config.walkableHeight,  18);  // 1.8 / 0.1, rounded up
    EXPECT_EQ(config.walkableClimb,   4);   // 0.4 / 0.1, rounded down
    EXPECT_EQ(config.walkableRadius,  2);   // 0.3 / 0.15, rounded up
    EXPECT_EQ(config.minRegionArea,   64);  // (1.2 / 0.15)^2
    EXPECT_EQ(config.mergeRegionArea, 400); // (3.0 / 0.15)^2
    EXPECT_EQ(config.tileSize,        128); // 19.2 / 0.15
    EXPECT_EQ(config.maxEdgeLen,      80);  // 12 / 0.15
    EXPECT_EQ(config.borderSize,      5);
}

TEST(Navigation, floor_20m_is_one_region_minus_erosion_band)
{
    Test_geometry geometry;
    geometry.add_floor(-10.0f, -10.0f, 10.0f, 10.0f, 0.0f);
    const Navigation_build_settings settings{};
    std::unique_ptr<Navigation_mesh> mesh = build(geometry, settings);
    ASSERT_TRUE(mesh);

    const std::vector<glm::vec3>        seeds{glm::vec3{0.0f, 0.0f, 0.0f}};
    std::vector<Navigation_seed_result> results;
    ASSERT_TRUE(mesh->mark_reachable(seeds, settings.get_seed_search_half_extents(), results));
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].found);

    const editor::Navigation_totals totals = mesh->get_totals();
    EXPECT_GT(totals.polygon_count, 0u);
    EXPECT_EQ(totals.reachable_polygon_count, totals.polygon_count); // one connected region

    // The erosion band is within 2 cells of agent_radius on every side.
    const double side      = eroded_side(20.0f, settings);
    const double tolerance = 4.0 * static_cast<double>(settings.cell_size);
    EXPECT_GE(totals.reachable_area, (side - tolerance) * (side - tolerance));
    EXPECT_LE(totals.reachable_area, (side + tolerance) * (side + tolerance));
}

TEST(Navigation, box_on_floor_leaves_a_hole)
{
    Test_geometry geometry;
    geometry.add_floor(-10.0f, -10.0f, 10.0f, 10.0f, 0.0f);
    // Lower than agent_height: Recast sees only surfaces, so a taller hollow box would have a walkable inside.
    geometry.add_box({-2.0f, 0.0f, -2.0f}, {2.0f, 1.0f, 2.0f});
    const Navigation_build_settings settings{};
    std::unique_ptr<Navigation_mesh> mesh = build(geometry, settings);
    ASSERT_TRUE(mesh);

    EXPECT_FALSE(covered(*mesh, {0.0f, 0.0f}, -0.5f, 0.5f));
    EXPECT_FALSE(covered(*mesh, {1.9f, 0.0f}, -0.5f, 0.5f));
    EXPECT_TRUE (covered(*mesh, {6.0f, 0.0f}, -0.5f, 0.5f));

    const double side       = eroded_side(20.0f, settings);
    const double floor_area = area_between(*mesh, -0.5f, 0.5f);
    EXPECT_LT(floor_area, (side * side) - (4.0 * 4.0));
}

namespace {

[[nodiscard]] auto ramp_reaches_upper_level(const float angle_degrees, std::size_t& out_upper_polygons) -> bool
{
    const float height = 2.0f;
    const float run    = height / std::tan(glm::radians(angle_degrees));
    Test_geometry geometry;
    geometry.add_floor(-10.0f, -3.0f, 0.0f, 3.0f, 0.0f);
    geometry.add_quad({0.0f, 0.0f, -3.0f}, {0.0f, 0.0f, 3.0f}, {run, height, 3.0f}, {run, height, -3.0f});
    geometry.add_floor(run, -3.0f, run + 10.0f, 3.0f, height);
    const Navigation_build_settings settings{};
    std::unique_ptr<Navigation_mesh> mesh = build(geometry, settings);
    if (!mesh) {
        return false;
    }
    const std::vector<glm::vec3>        seeds{glm::vec3{-5.0f, 0.0f, 0.0f}};
    std::vector<Navigation_seed_result> results;
    EXPECT_TRUE(mesh->mark_reachable(seeds, settings.get_seed_search_half_extents(), results));

    out_upper_polygons = 0;
    std::size_t reachable_upper = 0;
    for (uint32_t i = 0, end = static_cast<uint32_t>(mesh->get_polygons().size()); i < end; ++i) {
        const glm::vec3 centroid = mesh->get_polygon_centroid(i);
        if ((std::abs(centroid.y - height) < 0.3f) && (centroid.x > (run + 1.0f))) {
            ++out_upper_polygons;
            if (mesh->get_polygons()[i].reachable) {
                ++reachable_upper;
            }
        }
    }
    return (out_upper_polygons > 0) && (reachable_upper == out_upper_polygons);
}

} // anonymous namespace

TEST(Navigation, ramp_30_degrees_connects_two_levels)
{
    std::size_t upper_polygons = 0;
    EXPECT_TRUE(ramp_reaches_upper_level(30.0f, upper_polygons));
    EXPECT_GT(upper_polygons, 0u);
}

TEST(Navigation, ramp_60_degrees_does_not_connect)
{
    std::size_t upper_polygons = 0;
    EXPECT_FALSE(ramp_reaches_upper_level(60.0f, upper_polygons));
    EXPECT_GT(upper_polygons, 0u); // the upper level is present, only not reachable
}

TEST(Navigation, platform_1m_above_floor_is_present_and_not_reachable)
{
    Test_geometry geometry;
    geometry.add_floor(-10.0f, -10.0f, 10.0f, 10.0f, 0.0f);
    geometry.add_box({2.0f, 0.9f, 2.0f}, {6.0f, 1.0f, 6.0f});
    const Navigation_build_settings settings{};
    std::unique_ptr<Navigation_mesh> mesh = build(geometry, settings);
    ASSERT_TRUE(mesh);

    const std::vector<glm::vec3>        seeds{glm::vec3{-8.0f, 0.0f, -8.0f}};
    std::vector<Navigation_seed_result> results;
    ASSERT_TRUE(mesh->mark_reachable(seeds, settings.get_seed_search_half_extents(), results));

    std::size_t platform_polygons  = 0;
    std::size_t platform_reachable = 0;
    std::size_t floor_reachable    = 0;
    for (uint32_t i = 0, end = static_cast<uint32_t>(mesh->get_polygons().size()); i < end; ++i) {
        const glm::vec3 centroid  = mesh->get_polygon_centroid(i);
        const bool      reachable = mesh->get_polygons()[i].reachable;
        if ((std::abs(centroid.y - 1.0f) < 0.2f) && (centroid.x > 2.0f) && (centroid.x < 6.0f) && (centroid.z > 2.0f) && (centroid.z < 6.0f)) {
            ++platform_polygons;
            platform_reachable += reachable ? 1 : 0;
        } else if (std::abs(centroid.y) < 0.2f) {
            floor_reachable += reachable ? 1 : 0;
        }
    }
    EXPECT_GT(platform_polygons, 0u);
    EXPECT_EQ(platform_reachable, 0u);
    EXPECT_GT(floor_reachable, 0u);
    EXPECT_FALSE(covered(*mesh, {4.0f, 4.0f}, -0.5f, 0.5f)); // too low a ceiling under the platform
}

TEST(Navigation, seed_10m_above_everything_reports_its_reason)
{
    Test_geometry geometry;
    geometry.add_floor(-10.0f, -10.0f, 10.0f, 10.0f, 0.0f);
    const Navigation_build_settings settings{};
    std::unique_ptr<Navigation_mesh> mesh = build(geometry, settings);
    ASSERT_TRUE(mesh);

    {
        const std::vector<glm::vec3>        seeds{glm::vec3{0.0f, 10.0f, 0.0f}};
        std::vector<Navigation_seed_result> results;
        EXPECT_FALSE(mesh->mark_reachable(seeds, settings.get_seed_search_half_extents(), results));
        ASSERT_EQ(results.size(), 1u);
        EXPECT_FALSE(results[0].found);
        EXPECT_EQ(results[0].reason, "no polygon within 1 m horizontally and 3 m vertically");
        EXPECT_EQ(mesh->get_totals().reachable_polygon_count, 0u);
    }
    {
        const std::vector<glm::vec3>        seeds{glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 10.0f, 0.0f}};
        std::vector<Navigation_seed_result> results;
        EXPECT_TRUE(mesh->mark_reachable(seeds, settings.get_seed_search_half_extents(), results));
        ASSERT_EQ(results.size(), 2u);
        EXPECT_TRUE (results[0].found);
        EXPECT_NEAR (results[0].point.y, 0.0f, 0.2f);
        EXPECT_FALSE(results[1].found);
        EXPECT_FALSE(results[1].reason.empty());
    }
}

TEST(Navigation, two_builds_of_one_input_give_equal_bytes)
{
    Test_geometry geometry;
    geometry.add_floor(-10.0f, -10.0f, 30.0f, 10.0f, 0.0f);
    geometry.add_box({-2.0f, 0.0f, -2.0f}, {2.0f, 2.0f, 2.0f});
    geometry.add_box({12.0f, 0.9f, 2.0f}, {16.0f, 1.0f, 6.0f});
    const Navigation_build_settings settings{};
    const std::vector<glm::vec3>    seeds{glm::vec3{-8.0f, 0.0f, -8.0f}};

    std::vector<std::vector<uint8_t>> bytes;
    for (int i = 0; i < 2; ++i) {
        std::unique_ptr<Navigation_mesh> mesh = build(geometry, settings);
        ASSERT_TRUE(mesh);
        EXPECT_GT(mesh->get_tile_count(), 1u);
        std::vector<Navigation_seed_result> results;
        ASSERT_TRUE(mesh->mark_reachable(seeds, settings.get_seed_search_half_extents(), results));
        bytes.push_back(editor::write_navigation_mesh(*mesh, editor::Navigation_build_record{}));
    }
    ASSERT_FALSE(bytes[0].empty());
    EXPECT_EQ(bytes[0], bytes[1]);

    // Reading gives the same mesh back, reachable flags included.
    std::string                     error;
    editor::Navigation_build_record record;
    std::unique_ptr<Navigation_mesh> read_back = editor::read_navigation_mesh(bytes[0], record, error);
    ASSERT_TRUE(read_back) << error;
    EXPECT_EQ(editor::write_navigation_mesh(*read_back, record), bytes[0]);
    EXPECT_GT(read_back->get_totals().reachable_polygon_count, 0u);

    std::vector<uint8_t> truncated = bytes[0];
    truncated.resize(truncated.size() / 2);
    EXPECT_FALSE(editor::read_navigation_mesh(truncated, record, error));
    EXPECT_FALSE(error.empty());
}

TEST(Navigation, build_record_round_trips_bit_exactly)
{
    Test_geometry geometry;
    geometry.add_floor(-10.0f, -10.0f, 10.0f, 10.0f, 0.0f);
    std::unique_ptr<Navigation_mesh> mesh = build(geometry);
    ASSERT_TRUE(mesh);
    std::vector<Navigation_seed_result> results;
    ASSERT_TRUE(mesh->mark_reachable(std::vector<glm::vec3>{glm::vec3{1.0f, 0.0f, 1.0f}, glm::vec3{0.0f, 50.0f, 0.0f}}, glm::vec3{1.0f, 3.0f, 1.0f}, results));
    ASSERT_EQ(results.size(), 2u);

    editor::Navigation_build_record record;
    record.seed_search_radius = 1.25f;
    record.seed_search_height = 0x1.000002p+1f; // floats compared for equality survive bit for bit
    record.seeds.push_back(editor::Navigation_record_seed{.uid = "seedUid000000001", .name = "Seed", .point = glm::vec3{0x1.fffffep-1f, -0.0f, 1e-30f}, .result = results[0]});
    record.seeds.push_back(editor::Navigation_record_seed{.uid = "seedUid000000002", .name = "High Seed", .point = glm::vec3{0.0f, 50.0f, 0.0f}, .result = results[1]});
    editor::Navigation_record_input input{.uid = "meshUid000000001", .root_from_node = glm::mat4{1.0f}};
    input.root_from_node[3] = glm::vec4{0x1.234568p-3f, -2.5f, 7.0f, 1.0f};
    input.shape_hashes.push_back(editor::Navigation_shape_hash{.known = true, .bytes = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}});
    input.shape_hashes.push_back(editor::Navigation_shape_hash{});
    record.input.push_back(input);
    record.input.push_back(editor::Navigation_record_input{.uid = "meshUid000000002"});

    const std::vector<uint8_t> bytes = editor::write_navigation_mesh(*mesh, record);
    editor::Navigation_build_record read_record;
    std::string                     error;
    std::unique_ptr<Navigation_mesh> read_back = editor::read_navigation_mesh(bytes, read_record, error);
    ASSERT_TRUE(read_back) << error;
    EXPECT_EQ(std::bit_cast<uint32_t>(read_record.seed_search_radius), std::bit_cast<uint32_t>(record.seed_search_radius));
    EXPECT_EQ(std::bit_cast<uint32_t>(read_record.seed_search_height), std::bit_cast<uint32_t>(record.seed_search_height));
    ASSERT_EQ(read_record.seeds.size(), 2u);
    for (std::size_t i = 0; i < 2; ++i) {
        const editor::Navigation_record_seed& expected = record.seeds[i];
        const editor::Navigation_record_seed& actual   = read_record.seeds[i];
        EXPECT_EQ(actual.uid,  expected.uid);
        EXPECT_EQ(actual.name, expected.name);
        for (int c = 0; c < 3; ++c) {
            EXPECT_EQ(std::bit_cast<uint32_t>(actual.point[c]), std::bit_cast<uint32_t>(expected.point[c]));
            EXPECT_EQ(std::bit_cast<uint32_t>(actual.result.point[c]), std::bit_cast<uint32_t>(expected.result.point[c]));
            EXPECT_EQ(std::bit_cast<uint32_t>(actual.result.seed[c]), std::bit_cast<uint32_t>(expected.result.seed[c]));
        }
        EXPECT_EQ(actual.result.found,   expected.result.found);
        EXPECT_EQ(actual.result.polygon, expected.result.polygon);
        EXPECT_EQ(actual.result.reason,  expected.result.reason);
    }
    EXPECT_TRUE(read_record.seeds[0].result.found);
    EXPECT_FALSE(read_record.seeds[1].result.found);
    EXPECT_FALSE(read_record.seeds[1].result.reason.empty());
    ASSERT_EQ(read_record.input.size(), 2u);
    EXPECT_EQ(read_record.input[0].uid, "meshUid000000001");
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_EQ(std::bit_cast<uint32_t>(read_record.input[0].root_from_node[column][row]), std::bit_cast<uint32_t>(input.root_from_node[column][row]));
        }
    }
    EXPECT_EQ(read_record.input[0].shape_hashes, input.shape_hashes);
    EXPECT_TRUE(read_record.input[1].shape_hashes.empty());
    EXPECT_EQ(editor::write_navigation_mesh(*read_back, read_record), bytes);
}

TEST(Navigation, older_byte_form_version_is_refused)
{
    Test_geometry geometry;
    geometry.add_floor(-10.0f, -10.0f, 10.0f, 10.0f, 0.0f);
    std::unique_ptr<Navigation_mesh> mesh = build(geometry);
    ASSERT_TRUE(mesh);
    std::vector<uint8_t> bytes = editor::write_navigation_mesh(*mesh, editor::Navigation_build_record{});
    ASSERT_GE(bytes.size(), 8u);
    const uint32_t older_version = editor::c_navigation_mesh_format_version - 1;
    std::memcpy(bytes.data() + 4, &older_version, sizeof(older_version));
    editor::Navigation_build_record record;
    std::string                     error;
    EXPECT_FALSE(editor::read_navigation_mesh(bytes, record, error));
    EXPECT_NE(error.find("unsupported version 1"), std::string::npos) << error;
}

TEST(Navigation, floor_100m_spans_tiles_and_floods_across_them)
{
    Test_geometry geometry;
    geometry.add_floor(-50.0f, -50.0f, 50.0f, 50.0f, 0.0f);
    const Navigation_build_settings settings{};
    std::unique_ptr<Navigation_mesh> mesh = build(geometry, settings);
    ASSERT_TRUE(mesh);
    EXPECT_EQ(mesh->get_tile_count(), 36u); // ceil(667 cells / 128) = 6 per side

    const std::vector<glm::vec3>        seeds{glm::vec3{-45.0f, 0.0f, -45.0f}};
    std::vector<Navigation_seed_result> results;
    ASSERT_TRUE(mesh->mark_reachable(seeds, settings.get_seed_search_half_extents(), results));
    const editor::Navigation_totals totals = mesh->get_totals();
    EXPECT_EQ(totals.reachable_polygon_count, totals.polygon_count);

    const double side      = eroded_side(100.0f, settings);
    const double tolerance = 4.0 * static_cast<double>(settings.cell_size);
    EXPECT_GE(totals.reachable_area, (side - tolerance) * (side - tolerance));
    EXPECT_LE(totals.reachable_area, (side + tolerance) * (side + tolerance));
}

TEST(Navigation, empty_bounds_return_null_with_reason)
{
    Test_geometry geometry;
    geometry.add_floor(-10.0f, -10.0f, 10.0f, 10.0f, 0.0f);
    Navigation_build_settings settings{};
    settings.region_min = glm::vec3{5.0f, 0.0f, 5.0f};
    settings.region_max = glm::vec3{5.0f, 1.0f, 8.0f};
    tf::Executor executor{2};
    std::string  error;
    std::unique_ptr<Navigation_mesh> mesh = editor::build_navigation_mesh(geometry.make_input(), settings, executor, {}, error);
    EXPECT_FALSE(mesh);
    EXPECT_NE(error.find("empty"), std::string::npos) << error;
}

namespace {

// Floor at y 0 (x -10..0), stairs of 10 steps of 0.2 m (treads 0.3 m) rising along +x, an upper floor at y 2
// (x 3..13), and a separate platform at y 0.9 (x 20..26) that no step reaches.
class Two_floors_and_platform
{
public:
    static constexpr float upper_y    = 2.0f;
    static constexpr float platform_y = 0.9f;

    Two_floors_and_platform()
    {
        Test_geometry geometry;
        geometry.add_floor(-10.0f, -3.0f, 0.0f, 3.0f, 0.0f);
        for (int i = 0; i < 10; ++i) {
            const float x0 = 0.3f * static_cast<float>(i);
            geometry.add_box({x0, 0.0f, -3.0f}, {x0 + 0.3f, 0.2f * static_cast<float>(i + 1), 3.0f});
        }
        geometry.add_box({3.0f, 0.0f, -3.0f}, {13.0f, upper_y, 3.0f});
        geometry.add_box({20.0f, 0.0f, -3.0f}, {26.0f, platform_y, 3.0f});
        mesh = build(geometry, settings);
        if (mesh) {
            const std::vector<glm::vec3>        seeds{glm::vec3{-5.0f, 0.0f, 0.0f}};
            std::vector<Navigation_seed_result> results;
            EXPECT_TRUE(mesh->mark_reachable(seeds, settings.get_seed_search_half_extents(), results));
        }
    }

    Navigation_build_settings        settings{};
    std::unique_ptr<Navigation_mesh> mesh;
};

} // anonymous namespace

TEST(Navigation, islands_of_two_floors_joined_by_stairs_and_a_separate_platform)
{
    Two_floors_and_platform level;
    ASSERT_TRUE(level.mesh);
    const std::span<const editor::Navigation_island> islands = level.mesh->get_islands();
    ASSERT_EQ(islands.size(), 2u);
    EXPECT_EQ(level.mesh->get_reachable_island_count(), 1u);
    const editor::Navigation_totals totals = level.mesh->get_totals();
    EXPECT_EQ(totals.island_count, 2u);
    EXPECT_EQ(totals.reachable_island_count, 1u);

    // Every polygon's island id matches its island's reachable flag, and the islands' counts and areas add up.
    std::size_t polygon_sum = 0;
    double      area_sum    = 0.0;
    for (const editor::Navigation_island& island : islands) {
        polygon_sum += island.polygon_count;
        area_sum    += island.area;
    }
    EXPECT_EQ(polygon_sum, totals.polygon_count);
    EXPECT_NEAR(area_sum, totals.total_area, 1.0e-3);
    for (const Navigation_polygon& polygon : level.mesh->get_polygons()) {
        ASSERT_LT(polygon.island, islands.size());
        EXPECT_EQ(polygon.reachable, islands[polygon.island].reachable);
    }

    // Recast lifts the surface by up to two cell heights plus the detail error.
    const float height_tolerance = (2.0f * level.settings.cell_height) + level.settings.detail_sample_max_error;
    const editor::Navigation_island& walkable = islands[0].reachable ? islands[0] : islands[1];
    const editor::Navigation_island& platform = islands[0].reachable ? islands[1] : islands[0];
    EXPECT_NEAR(walkable.bounds_min.y, 0.0f, height_tolerance);
    EXPECT_NEAR(walkable.bounds_max.y, Two_floors_and_platform::upper_y, height_tolerance);
    EXPECT_GT(walkable.bounds_max.x, 12.0f);
    EXPECT_NEAR(platform.bounds_min.y, Two_floors_and_platform::platform_y, height_tolerance);
    EXPECT_NEAR(platform.bounds_max.y, Two_floors_and_platform::platform_y, height_tolerance);
    EXPECT_GT(platform.point.x, 20.0f);
    EXPECT_LT(platform.point.x, 26.0f);
    EXPECT_GT(walkable.area, platform.area);

    // The flags follow clear_reachable() and set_reachable().
    level.mesh->clear_reachable();
    EXPECT_EQ(level.mesh->get_reachable_island_count(), 0u);
    std::vector<uint8_t> all(level.mesh->get_polygons().size(), uint8_t{1});
    level.mesh->set_reachable(all);
    EXPECT_EQ(level.mesh->get_reachable_island_count(), 2u);
}

TEST(Navigation, nearest_point_snaps_onto_the_floor_and_names_its_island)
{
    Two_floors_and_platform level;
    ASSERT_TRUE(level.mesh);
    const glm::vec3 half_extents = level.settings.get_seed_search_half_extents();

    const editor::Navigation_point_result floor = level.mesh->find_nearest_point({-5.0f, 0.7f, 1.0f}, half_extents);
    ASSERT_TRUE(floor.found);
    EXPECT_NEAR(floor.point.x, -5.0f, 1.0e-3f);
    EXPECT_NEAR(floor.point.z, 1.0f, 1.0e-3f);
    EXPECT_NEAR(floor.point.y, 0.0f, 0.2f);
    EXPECT_TRUE(floor.reachable);
    EXPECT_EQ(floor.island, level.mesh->get_polygons()[floor.polygon].island);

    const editor::Navigation_point_result platform = level.mesh->find_nearest_point({23.0f, 1.5f, 0.0f}, half_extents);
    ASSERT_TRUE(platform.found);
    EXPECT_NEAR(platform.point.y, Two_floors_and_platform::platform_y, 0.2f);
    EXPECT_FALSE(platform.reachable);
    EXPECT_NE(platform.island, floor.island);

    // The polygon reference maps back to the polygon index.
    uint32_t index = 0;
    ASSERT_TRUE(level.mesh->find_polygon_index(level.mesh->get_polygon_ref(platform.polygon), index));
    EXPECT_EQ(index, platform.polygon);

    EXPECT_FALSE(level.mesh->find_nearest_point({-5.0f, 20.0f, 0.0f}, half_extents).found);
}

TEST(Navigation, path_climbs_the_stairs_and_none_reaches_the_platform)
{
    Two_floors_and_platform level;
    ASSERT_TRUE(level.mesh);
    const glm::vec3 half_extents = level.settings.get_seed_search_half_extents();

    editor::Navigation_path_result path;
    level.mesh->find_path({-5.0f, 0.0f, 0.0f}, {8.0f, Two_floors_and_platform::upper_y, 0.0f}, half_extents, path);
    EXPECT_TRUE(path.found);
    EXPECT_TRUE(path.complete);
    EXPECT_EQ(path.failure, editor::Navigation_path_failure::none);
    ASSERT_GE(path.corners.size(), 2u);
    EXPECT_NEAR(path.corners.front().x, -5.0f, 1.0e-3f);
    EXPECT_NEAR(path.corners.back().x, 8.0f, 1.0e-3f);
    EXPECT_NEAR(path.corners.back().y, Two_floors_and_platform::upper_y, 0.2f);
    // At least the horizontal 13 m, at most a few meters more for the climb.
    EXPECT_GE(path.length, 13.0f);
    EXPECT_LE(path.length, 16.0f);

    level.mesh->find_path({-5.0f, 0.0f, 0.0f}, {23.0f, Two_floors_and_platform::platform_y, 0.0f}, half_extents, path);
    EXPECT_FALSE(path.found);
    EXPECT_FALSE(path.complete);
    EXPECT_EQ(path.failure, editor::Navigation_path_failure::different_islands);
    EXPECT_TRUE(path.start.found);
    EXPECT_TRUE(path.end.found);
    EXPECT_TRUE(path.corners.empty());

    level.mesh->find_path({-5.0f, 0.0f, 0.0f}, {50.0f, 0.0f, 0.0f}, half_extents, path);
    EXPECT_FALSE(path.found);
    EXPECT_EQ(path.failure, editor::Navigation_path_failure::no_end_polygon);
}

TEST(Navigation, read_back_mesh_keeps_islands_and_answers_queries)
{
    Two_floors_and_platform level;
    ASSERT_TRUE(level.mesh);
    const std::vector<uint8_t> bytes = editor::write_navigation_mesh(*level.mesh, editor::Navigation_build_record{});
    editor::Navigation_build_record record;
    std::string                     error;
    std::unique_ptr<Navigation_mesh> read_back = editor::read_navigation_mesh(bytes, record, error);
    ASSERT_TRUE(read_back) << error;
    EXPECT_EQ(read_back->get_islands().size(), 2u);
    EXPECT_EQ(read_back->get_reachable_island_count(), 1u);
    editor::Navigation_path_result path;
    read_back->find_path({-5.0f, 0.0f, 0.0f}, {8.0f, Two_floors_and_platform::upper_y, 0.0f}, level.settings.get_seed_search_half_extents(), path);
    EXPECT_TRUE(path.found);
    EXPECT_TRUE(path.complete);
}
