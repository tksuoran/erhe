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

// Tests of the recreated basic kit (kit_shapes_basic_v2.cpp).
namespace {

using editor::Kit_parameter_values;
using editor::Kit_shape_definition;
using editor::Kit_stairs_layout;
using namespace editor::kit_test;

// The plain building blocks keep their originals' bounds at every value, so the two tile and stack alike.
TEST(Kit_recreated, basic_shapes_keep_their_originals_bounds)
{
    expect_recreated_keep_original_bounds(get_basic_v2_rows().recreated_originals);
}

// The recreated stairs' side walls are a column per step: no vertex gathers a fan of facets, at any step count.
TEST(Kit_recreated, stairs_sides_have_no_fan)
{
    const Kit_shape_definition& stairs = get_shape("basic.stairs.v2");
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

// The recreated cylinder's mirror images are equal to the bit: about z = 0 always, about x = 0 at an even segment count.
TEST(Kit_recreated, cylinder_is_exactly_symmetric)
{
    const Kit_shape_definition& cylinder = get_shape("basic.cylinder.v2");
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
