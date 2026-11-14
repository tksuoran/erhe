#include "level/level_region.hpp"

#include <gtest/gtest.h>

#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using editor::Height_map_cell;
using editor::Height_map_grid;
using editor::Height_range;
using editor::Level_region;
using editor::Level_region_shape;

auto make_box(const glm::vec3 min, const glm::vec3 max) -> erhe::math::Aabb
{
    erhe::math::Aabb aabb{};
    aabb.include(min);
    aabb.include(max);
    return aabb;
}

TEST(Level_region, box_intersection)
{
    const Level_region region{.shape = Level_region_shape::box, .min = glm::vec3{0.0f}, .max = glm::vec3{2.0f}};
    EXPECT_TRUE (editor::region_intersects(region, make_box(glm::vec3{1.0f}, glm::vec3{3.0f})));
    EXPECT_TRUE (editor::region_intersects(region, make_box(glm::vec3{2.0f, 0.0f, 0.0f}, glm::vec3{3.0f, 1.0f, 1.0f}))); // touching
    EXPECT_TRUE (editor::region_intersects(region, make_box(glm::vec3{-5.0f}, glm::vec3{5.0f})));                       // containing
    EXPECT_FALSE(editor::region_intersects(region, make_box(glm::vec3{2.01f, 0.0f, 0.0f}, glm::vec3{3.0f, 1.0f, 1.0f})));
    EXPECT_FALSE(editor::region_intersects(region, make_box(glm::vec3{0.0f, -2.0f, 0.0f}, glm::vec3{1.0f, -0.5f, 1.0f})));
    EXPECT_FALSE(editor::region_intersects(region, erhe::math::Aabb{}));
}

TEST(Level_region, sphere_intersection)
{
    const Level_region region{.shape = Level_region_shape::sphere, .center = glm::vec3{0.0f}, .radius = 1.0f};
    EXPECT_TRUE (editor::region_intersects(region, make_box(glm::vec3{0.5f}, glm::vec3{2.0f})));
    EXPECT_TRUE (editor::region_intersects(region, make_box(glm::vec3{-0.1f}, glm::vec3{0.1f})));                       // inside
    EXPECT_TRUE (editor::region_intersects(region, make_box(glm::vec3{1.0f, -1.0f, -1.0f}, glm::vec3{2.0f, 1.0f, 1.0f}))); // touching a face
    // The box corner nearest the center is sqrt(3) * 0.7 = 1.21 away.
    EXPECT_FALSE(editor::region_intersects(region, make_box(glm::vec3{0.7f}, glm::vec3{2.0f})));
    EXPECT_FALSE(editor::region_intersects(region, erhe::math::Aabb{}));
}

TEST(Level_region, height_map_grid)
{
    const Height_map_grid grid = editor::make_height_map_grid(glm::vec3{-1.0f, 0.0f, -2.0f}, glm::vec3{1.0f, 4.0f, 1.0f}, 0.5f);
    EXPECT_EQ(grid.columns, 4u);
    EXPECT_EQ(grid.rows,    6u);
    const glm::vec2 first = editor::get_cell_center(grid, 0, 0);
    const glm::vec2 last  = editor::get_cell_center(grid, 3, 5);
    EXPECT_FLOAT_EQ(first.x, -0.75f);
    EXPECT_FLOAT_EQ(first.y, -1.75f);
    EXPECT_FLOAT_EQ(last.x,   0.75f);
    EXPECT_FLOAT_EQ(last.y,   0.75f);

    const Height_map_grid partial = editor::make_height_map_grid(glm::vec3{0.0f}, glm::vec3{1.2f, 1.0f, 1.0f}, 0.5f);
    EXPECT_EQ(partial.columns, 3u);
    EXPECT_EQ(partial.rows,    2u);

    EXPECT_EQ(editor::make_height_map_grid(glm::vec3{0.0f}, glm::vec3{1.0f}, 0.0f).columns, 0u);
    EXPECT_EQ(editor::make_height_map_grid(glm::vec3{0.0f}, glm::vec3{0.0f, 1.0f, 1.0f}, 0.5f).columns, 0u);
}

TEST(Level_region, height_digits)
{
    const Height_range range{.min = 1.0f, .max = 10.0f, .valid = true};
    EXPECT_EQ(editor::height_digit( 1.0f, range), '0');
    EXPECT_EQ(editor::height_digit(10.0f, range), '9');
    EXPECT_EQ(editor::height_digit( 5.0f, range), '4');
    EXPECT_EQ(editor::height_digit( 5.6f, range), '5');
    EXPECT_EQ(editor::height_digit(20.0f, range), '9');
    EXPECT_EQ(editor::height_digit( 3.0f, Height_range{.min = 3.0f, .max = 3.0f, .valid = true}), '0');
}

TEST(Level_region, text_pictures)
{
    const Height_map_grid grid = editor::make_height_map_grid(glm::vec3{0.0f}, glm::vec3{3.0f, 1.0f, 2.0f}, 1.0f);
    ASSERT_EQ(grid.columns, 3u);
    ASSERT_EQ(grid.rows,    2u);
    std::vector<Height_map_cell> cells(6);
    cells[0] = Height_map_cell{.height = 0.2f, .hit = true, .walkable = true,  .island = 0};
    cells[1] = Height_map_cell{.height = 1.2f, .hit = true, .walkable = false, .island = editor::c_no_island};
    cells[2] = Height_map_cell{};
    cells[3] = Height_map_cell{.height = 0.2f, .hit = true, .walkable = true,  .island = 11};
    cells[4] = Height_map_cell{.height = 0.75f, .hit = true, .walkable = true,  .island = 37};
    cells[5] = Height_map_cell{.height = 0.2f, .hit = true, .walkable = false, .island = editor::c_no_island};

    const Height_range range = editor::get_height_range(cells);
    EXPECT_TRUE(range.valid);
    EXPECT_FLOAT_EQ(range.min, 0.2f);
    EXPECT_FLOAT_EQ(range.max, 1.2f);

    std::string text;
    editor::write_height_text(grid, cells, text);
    EXPECT_EQ(text, "09.\n050\n");
    editor::write_island_text(grid, cells, text);
    EXPECT_EQ(text, "0#.\nb1#\n");

    // A cell count that does not match the grid writes nothing.
    editor::write_height_text(grid, std::span<const Height_map_cell>{cells.data(), 5}, text);
    EXPECT_TRUE(text.empty());
    EXPECT_FALSE(editor::get_height_range(std::span<const Height_map_cell>{cells.data() + 2, 1}).valid);
}

} // anonymous namespace

namespace {

auto box_at(const glm::vec3 center, const float half) -> erhe::math::Aabb
{
    return make_box(center - glm::vec3{half}, center + glm::vec3{half});
}

} // anonymous namespace

TEST(Level_region, bounds_overlap_is_the_smallest_axis_overlap)
{
    const erhe::math::Aabb unit = box_at(glm::vec3{0.0f}, 0.5f);
    EXPECT_NEAR(editor::get_bounds_overlap(unit, box_at(glm::vec3{0.75f, 0.0f, 0.0f}, 0.5f)), 0.25f, 1.0e-6f);
    EXPECT_NEAR(editor::get_bounds_overlap(unit, box_at(glm::vec3{0.75f, 0.9f, 0.0f}, 0.5f)), 0.1f, 1.0e-6f);
    EXPECT_NEAR(editor::get_bounds_overlap(unit, box_at(glm::vec3{1.0f, 0.0f, 0.0f}, 0.5f)), 0.0f, 1.0e-6f); // touching
    EXPECT_LT  (editor::get_bounds_overlap(unit, box_at(glm::vec3{3.0f, 0.0f, 0.0f}, 0.5f)), 0.0f);
    EXPECT_LT  (editor::get_bounds_overlap(unit, erhe::math::Aabb{}), 0.0f);
    const glm::vec3 center = editor::get_bounds_overlap_center(unit, box_at(glm::vec3{0.75f, 0.0f, 0.0f}, 0.5f));
    EXPECT_NEAR(center.x, 0.375f, 1.0e-6f);
    EXPECT_NEAR(center.y, 0.0f, 1.0e-6f);
}

TEST(Level_region, bounds_pairs_overlap_by_more_than_the_tolerance)
{
    // 0 and 1 overlap by 0.25, 1 and 2 touch, 3 lies apart, 4 holds 0 and 1, 5 is invalid.
    const std::vector<erhe::math::Aabb> bounds{
        box_at(glm::vec3{0.0f}, 0.5f),
        box_at(glm::vec3{0.75f, 0.0f, 0.0f}, 0.5f),
        box_at(glm::vec3{1.75f, 0.0f, 0.0f}, 0.5f),
        box_at(glm::vec3{10.0f, 0.0f, 0.0f}, 0.5f),
        box_at(glm::vec3{0.5f, 0.0f, 0.0f}, 2.0f),
        erhe::math::Aabb{}
    };
    std::vector<std::size_t>         order;
    std::vector<editor::Bounds_pair> pairs;
    editor::find_bounds_pairs(bounds, 0.01f, order, pairs);
    std::vector<std::pair<std::size_t, std::size_t>> found;
    for (const editor::Bounds_pair& pair : pairs) {
        found.emplace_back(pair.a, pair.b);
    }
    const std::vector<std::pair<std::size_t, std::size_t>> expected{{0, 1}, {0, 4}, {1, 4}, {2, 4}};
    EXPECT_EQ(found, expected);

    // A tolerance above the 0.25 overlap leaves only the pairs with the large box.
    editor::find_bounds_pairs(bounds, 0.3f, order, pairs);
    found.clear();
    for (const editor::Bounds_pair& pair : pairs) {
        found.emplace_back(pair.a, pair.b);
    }
    const std::vector<std::pair<std::size_t, std::size_t>> expected_wide{{0, 4}, {1, 4}, {2, 4}};
    EXPECT_EQ(found, expected_wide);
}

TEST(Level_region, tags_and_finding_kinds)
{
    const std::set<std::string>    tags{"rock", "ground"};
    const std::vector<std::string> listed{"tree", "ground"};
    const std::vector<std::string> other{"tree"};
    EXPECT_TRUE (editor::has_any_tag(tags, listed));
    EXPECT_FALSE(editor::has_any_tag(tags, other));
    EXPECT_FALSE(editor::has_any_tag(tags, std::span<const std::string>{}));
    EXPECT_STREQ(editor::c_str(editor::Level_finding_kind::unreachable_island), "unreachable_island");
    EXPECT_STREQ(editor::c_str(editor::Level_finding_kind::unwalkable_parameters), "unwalkable_parameters");
}
