#include "kit_shape_test_support.hpp"

#include "level/kit_shape.hpp"
#include "level/kit_shape_checks.hpp"
#include "level/kit_shapes.hpp"

#include "erhe_geometry/geometry.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <geogram/mesh/mesh.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Tests of the basic kit (kit_shapes_basic.cpp).
namespace {

using editor::Kit_parameter_values;
using editor::Kit_shape_definition;
using editor::Kit_stairs_layout;
using namespace editor::kit_test;

// The plain building blocks' bounds are the boxes their values state, at the defaults, both range corners and derived
// seeds, so they tile and stack on the module grid.
TEST(Kit_basic_shapes, bounds_follow_the_values)
{
    for (const Kit_parameter_values& v : bounds_value_sets(get_shape("basic.block"))) {
        const float w = v.get_float("width");
        const float d = v.get_float("depth");
        expect_bounds(v, glm::vec3{-0.5f * w, 0.0f, -0.5f * d}, glm::vec3{0.5f * w, v.get_float("height"), 0.5f * d});
    }
    for (const Kit_parameter_values& v : bounds_value_sets(get_shape("basic.wedge"))) {
        const float w = v.get_float("width");
        const float r = v.get_float("run");
        expect_bounds(v, glm::vec3{-0.5f * w, 0.0f, -0.5f * r}, glm::vec3{0.5f * w, v.get_float("rise"), 0.5f * r});
    }
    for (const Kit_parameter_values& v : bounds_value_sets(get_shape("basic.cylinder"))) {
        const std::array<float, 4> e = circle_extent(v.get_int("segments"), v.get_float("radius"));
        expect_bounds(v, glm::vec3{e[0], 0.0f, e[2]}, glm::vec3{e[1], v.get_float("height"), e[3]});
    }
    for (const Kit_parameter_values& v : bounds_value_sets(get_shape("basic.stairs"))) {
        const float w   = v.get_float("width");
        const float run = editor::get_stairs_layout(v).run;
        expect_bounds(v, glm::vec3{-0.5f * w, 0.0f, -0.5f * run}, glm::vec3{0.5f * w, v.get_float("rise"), 0.5f * run});
    }
    for (const Kit_parameter_values& v : bounds_value_sets(get_shape("basic.floor_slab"))) {
        const float w = v.get_float("width");
        const float d = v.get_float("depth");
        expect_bounds(v, glm::vec3{-0.5f * w, 0.0f, -0.5f * d}, glm::vec3{0.5f * w, v.get_float("thickness"), 0.5f * d});
    }
}

// The stairs' side walls are a column per step: no vertex gathers a fan of facets, at any step count.
TEST(Kit_basic_shapes, stairs_sides_have_no_fan)
{
    const Kit_shape_definition& stairs = get_shape("basic.stairs");
    for (const float rise : {0.2f, 1.0f, 3.0f, 16.0f}) {
        Kit_parameter_values values = defaults(stairs);
        ASSERT_TRUE(values.set_float("rise", rise));
        ASSERT_TRUE(values.set_float("step_rise_limit", (rise > 8.0f) ? 0.125f : 0.2f));
        ASSERT_EQ(values.validate(), "");
        const Kit_stairs_layout          layout = editor::get_stairs_layout(values);
        const std::unique_ptr<GEO::Mesh> mesh   = generate(values);
        SCOPED_TRACE(fmt::format("rise {}: {} steps", rise, layout.step_count));
        EXPECT_EQ(editor::count_kit_triangles(*mesh), static_cast<unsigned int>(12 * layout.step_count));
        std::vector<int> facets_at(mesh->vertices.nb(), 0);
        for (GEO::index_t f = 0; f < mesh->facets.nb(); ++f) {
            for (GEO::index_t c = 0; c < mesh->facets.nb_vertices(f); ++c) {
                ++facets_at[mesh->facets.vertex(f, c)];
            }
        }
        // A riser foot: its riser, the tread before it, two columns and two step triangles.
        EXPECT_LE(*std::max_element(facets_at.begin(), facets_at.end()), 6);
    }
}

// The cylinder's mirror images are equal to the bit: about z = 0 always, about x = 0 at an even segment count.
TEST(Kit_basic_shapes, cylinder_is_exactly_symmetric)
{
    const Kit_shape_definition& cylinder = get_shape("basic.cylinder");
    for (const int segments : {3, 4, 5, 6, 7, 12, 16, 33, 64}) {
        Kit_parameter_values values = defaults(cylinder);
        ASSERT_TRUE(values.set_int("segments", segments));
        ASSERT_TRUE(values.set_float("radius", 0.7f));
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        std::set<std::array<float, 3>> points;
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const GEO::vec3f p = erhe::geometry::get_pointf(mesh->vertices, v);
            points.insert({p.x, p.y, p.z});
        }
        SCOPED_TRACE(fmt::format("{} segments", segments));
        EXPECT_EQ(points.size(), static_cast<std::size_t>(2 * segments));
        for (const std::array<float, 3>& p : points) {
            EXPECT_TRUE(points.contains({p[0], p[1], -p[2]}));
            if ((segments % 2) == 0) {
                EXPECT_TRUE(points.contains({-p[0], p[1], p[2]}));
            }
        }
    }
}

} // anonymous namespace
