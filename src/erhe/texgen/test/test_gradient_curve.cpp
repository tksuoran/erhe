#include "erhe_texgen/compose_node.hpp"
#include "erhe_texgen/composer.hpp"
#include "erhe_texgen/node_descriptor.hpp"

#include <gtest/gtest.h>

#include <string>

namespace erhe::texgen::test {

namespace {

// Counts non-overlapping occurrences of needle in haystack.
[[nodiscard]] auto count_occurrences(const std::string& haystack, const std::string& needle) -> std::size_t
{
    if (needle.empty()) {
        return 0;
    }
    std::size_t count    = 0;
    std::size_t position = 0;
    while ((position = haystack.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.length();
    }
    return count;
}

// Returns the single global whose text contains marker (assumed unique), or "".
[[nodiscard]] auto find_global(const Shader_code& shader_code, const std::string& marker) -> std::string
{
    for (const std::string& global : shader_code.get_globals()) {
        if (global.find(marker) != std::string::npos) {
            return global;
        }
    }
    return {};
}

// A descriptor whose single rgba output applies a gradient parameter to $uv.x
// (mirrors Material Maker's colorize: "$gradient($input($uv))").
[[nodiscard]] auto make_gradient_descriptor(
    const std::vector<Gradient_stop>& stops,
    const Gradient_interpolation      interpolation
) -> Node_descriptor
{
    Node_descriptor descriptor{};
    descriptor.name = "gradient_test";
    Parameter_descriptor parameter{};
    parameter.name                           = "gradient";
    parameter.kind                           = Parameter_kind::gradient_parameter;
    parameter.default_gradient_stops         = stops;
    parameter.default_gradient_interpolation = interpolation;
    descriptor.parameters.push_back(parameter);
    Output_descriptor output{};
    output.type       = Value_type::rgba;
    output.expression = "$gradient($uv.x)";
    descriptor.outputs.push_back(output);
    return descriptor;
}

// A descriptor whose single grayscale output applies a curve parameter to $uv.x
// (a tone remap).
[[nodiscard]] auto make_curve_descriptor(const std::vector<Curve_point>& points) -> Node_descriptor
{
    Node_descriptor descriptor{};
    descriptor.name = "curve_test";
    Parameter_descriptor parameter{};
    parameter.name                 = "curve";
    parameter.kind                 = Parameter_kind::curve_parameter;
    parameter.default_curve_points = points;
    descriptor.parameters.push_back(parameter);
    Output_descriptor output{};
    output.type       = Value_type::grayscale;
    output.expression = "$curve($uv.x)";
    descriptor.outputs.push_back(output);
    return descriptor;
}

const std::vector<Gradient_stop> black_to_white_linear{
    Gradient_stop{.position = 0.0f, .color = {0.0f, 0.0f, 0.0f, 1.0f}},
    Gradient_stop{.position = 1.0f, .color = {1.0f, 1.0f, 1.0f, 1.0f}}
};

} // anonymous namespace

TEST(Gradient, function_emitted_and_referenced_at_call_site)
{
    const Node_descriptor descriptor = make_gradient_descriptor(black_to_white_linear, Gradient_interpolation::linear);
    const Compose_node    node{descriptor, 5};
    const Composer        composer{};
    const Shader_code     shader_code = composer.compose(node, 0);

    // A gradient parameter emits no live uniform (constant-baked codegen).
    EXPECT_TRUE(shader_code.get_uniforms().empty());

    const std::string function = find_global(shader_code, "o5_gradient_gradient");
    ASSERT_FALSE(function.empty()) << "gradient function not emitted into globals";
    EXPECT_NE(function.find("vec4 o5_gradient_gradient(float x)"), std::string::npos);

    // The output expression $gradient($uv.x) resolves to a call to that function.
    EXPECT_NE(shader_code.get_code().find("o5_gradient_gradient("), std::string::npos);
}

TEST(Gradient, ladder_branch_count_matches_stops)
{
    const std::vector<Gradient_stop> three_stops{
        Gradient_stop{.position = 0.0f, .color = {0.0f, 0.0f, 0.0f, 1.0f}},
        Gradient_stop{.position = 0.5f, .color = {1.0f, 0.0f, 0.0f, 1.0f}},
        Gradient_stop{.position = 1.0f, .color = {1.0f, 1.0f, 1.0f, 1.0f}}
    };
    const Node_descriptor descriptor = make_gradient_descriptor(three_stops, Gradient_interpolation::linear);
    const Compose_node    node{descriptor, 1};
    const Composer        composer{};
    const Shader_code     shader_code = composer.compose(node, 0);

    const std::string function = find_global(shader_code, "o1_gradient_gradient");
    ASSERT_FALSE(function.empty());
    // Linear: one mix() branch per adjacent pair (stops - 1).
    EXPECT_EQ(count_occurrences(function, "return mix("), three_stops.size() - 1);
}

TEST(Gradient, linear_uses_mix_without_cosine)
{
    const Node_descriptor descriptor = make_gradient_descriptor(black_to_white_linear, Gradient_interpolation::linear);
    const Compose_node    node{descriptor, 2};
    const Composer        composer{};
    const Shader_code     shader_code = composer.compose(node, 0);
    const std::string     function = find_global(shader_code, "o2_gradient_gradient");
    ASSERT_FALSE(function.empty());
    EXPECT_NE(function.find("return mix("), std::string::npos);
    EXPECT_EQ(function.find("cos("), std::string::npos);
}

TEST(Gradient, smoothstep_uses_cosine)
{
    const Node_descriptor descriptor = make_gradient_descriptor(black_to_white_linear, Gradient_interpolation::smoothstep);
    const Compose_node    node{descriptor, 3};
    const Composer        composer{};
    const Shader_code     shader_code = composer.compose(node, 0);
    const std::string     function = find_global(shader_code, "o3_gradient_gradient");
    ASSERT_FALSE(function.empty());
    EXPECT_NE(function.find("0.5-0.5*cos(3.14159265359*"), std::string::npos);
}

TEST(Gradient, constant_uses_no_mix)
{
    const Node_descriptor descriptor = make_gradient_descriptor(black_to_white_linear, Gradient_interpolation::constant);
    const Compose_node    node{descriptor, 4};
    const Composer        composer{};
    const Shader_code     shader_code = composer.compose(node, 0);
    const std::string     function = find_global(shader_code, "o4_gradient_gradient");
    ASSERT_FALSE(function.empty());
    EXPECT_EQ(function.find("mix("), std::string::npos);
    // Two stops -> one if branch then a trailing return.
    EXPECT_NE(function.find("if (x <"), std::string::npos);
}

TEST(Gradient, unsorted_stops_are_sorted_before_emit)
{
    const std::vector<Gradient_stop> unsorted{
        Gradient_stop{.position = 1.0f, .color = {1.0f, 0.0f, 0.0f, 1.0f}},
        Gradient_stop{.position = 0.0f, .color = {0.0f, 0.0f, 1.0f, 1.0f}}
    };
    Node_descriptor descriptor = make_gradient_descriptor(unsorted, Gradient_interpolation::linear);
    Compose_node    node{descriptor, 1};
    // Also drive the runtime setter with an unsorted list.
    node.set_gradient("gradient", unsorted, Gradient_interpolation::linear);
    const Composer    composer{};
    const Shader_code shader_code = composer.compose(node, 0);
    const std::string function = find_global(shader_code, "o1_gradient_gradient");
    ASSERT_FALSE(function.empty());
    // The first guard compares against the lowest position (0.0), and the blue
    // stop color (the position-0 stop) is returned first.
    const std::size_t first_guard  = function.find("if (x < 0.000000000)");
    const std::size_t blue_return  = function.find("vec4(0.000000000, 0.000000000, 1.000000000, 1.000000000)");
    const std::size_t red_return   = function.find("vec4(1.000000000, 0.000000000, 0.000000000, 1.000000000)");
    EXPECT_NE(first_guard, std::string::npos);
    ASSERT_NE(blue_return, std::string::npos);
    ASSERT_NE(red_return, std::string::npos);
    EXPECT_LT(blue_return, red_return) << "blue (position 0) must be emitted before red (position 1)";
}

TEST(Gradient, single_stop_is_constant)
{
    const std::vector<Gradient_stop> one_stop{
        Gradient_stop{.position = 0.25f, .color = {0.2f, 0.4f, 0.6f, 1.0f}}
    };
    const Node_descriptor descriptor = make_gradient_descriptor(one_stop, Gradient_interpolation::linear);
    const Compose_node    node{descriptor, 1};
    const Composer        composer{};
    const Shader_code     shader_code = composer.compose(node, 0);
    const std::string     function = find_global(shader_code, "o1_gradient_gradient");
    ASSERT_FALSE(function.empty());
    // One stop, linear: "if (x < pos) return c;" then "return c;" - no mix.
    EXPECT_EQ(function.find("mix("), std::string::npos);
    EXPECT_NE(function.find("vec4(0.200000003"), std::string::npos);
}

TEST(Gradient, empty_stops_degrade_to_single_black_via_setter)
{
    const Node_descriptor descriptor = make_gradient_descriptor(black_to_white_linear, Gradient_interpolation::linear);
    Compose_node          node{descriptor, 1};
    node.set_gradient("gradient", {}, Gradient_interpolation::linear); // empty -> one black stop
    const Composer    composer{};
    const Shader_code shader_code = composer.compose(node, 0);
    const std::string function = find_global(shader_code, "o1_gradient_gradient");
    ASSERT_FALSE(function.empty());
    EXPECT_EQ(shader_code.get_output_expression().empty(), false);
    // Fragment still assembles textually (no error marker).
    EXPECT_EQ(composer.assemble_fragment(shader_code).find("(error:"), std::string::npos);
}

TEST(Gradient, fragment_assembles_without_error_marker)
{
    const Node_descriptor descriptor = make_gradient_descriptor(black_to_white_linear, Gradient_interpolation::cubic);
    const Compose_node    node{descriptor, 7};
    const Composer        composer{};
    const Shader_code     shader_code = composer.compose(node, 0);
    const std::string     fragment    = composer.assemble_fragment(shader_code);
    EXPECT_EQ(fragment.find("(error:"), std::string::npos);
    EXPECT_NE(fragment.find("vec4 o7_gradient_gradient(float x)"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Curve
// ---------------------------------------------------------------------------

const std::vector<Curve_point> identity_curve{
    Curve_point{.x = 0.0f, .y = 0.0f, .left_slope = 0.0f, .right_slope = 1.0f},
    Curve_point{.x = 1.0f, .y = 1.0f, .left_slope = 1.0f, .right_slope = 0.0f}
};

TEST(Curve, function_emitted_and_referenced_at_call_site)
{
    const Node_descriptor descriptor = make_curve_descriptor(identity_curve);
    const Compose_node    node{descriptor, 6};
    const Composer        composer{};
    const Shader_code     shader_code = composer.compose(node, 0);

    EXPECT_TRUE(shader_code.get_uniforms().empty());
    const std::string function = find_global(shader_code, "o6_curve_curve");
    ASSERT_FALSE(function.empty()) << "curve function not emitted into globals";
    EXPECT_NE(function.find("float o6_curve_curve(float x)"), std::string::npos);
    EXPECT_NE(function.find("return y1*omt3 + yac*omt2*t*3.0 + ybc*omt*t2*3.0 + y2*t3;"), std::string::npos);
    EXPECT_NE(shader_code.get_code().find("o6_curve_curve("), std::string::npos);
}

TEST(Curve, segment_count_matches_points)
{
    const std::vector<Curve_point> three_points{
        Curve_point{.x = 0.0f, .y = 0.0f, .left_slope = 0.0f, .right_slope = 0.0f},
        Curve_point{.x = 0.5f, .y = 0.8f, .left_slope = 0.0f, .right_slope = 0.0f},
        Curve_point{.x = 1.0f, .y = 1.0f, .left_slope = 0.0f, .right_slope = 0.0f}
    };
    const Node_descriptor descriptor = make_curve_descriptor(three_points);
    const Compose_node    node{descriptor, 1};
    const Composer        composer{};
    const Shader_code     shader_code = composer.compose(node, 0);
    const std::string     function = find_global(shader_code, "o1_curve_curve");
    ASSERT_FALSE(function.empty());
    // One Hermite segment per adjacent pair (points - 1).
    EXPECT_EQ(count_occurrences(function, "return y1*omt3"), three_points.size() - 1);
    // Interior segment boundaries are guarded; the final one is unguarded.
    EXPECT_EQ(count_occurrences(function, "if (x <= "), three_points.size() - 2);
}

TEST(Curve, unsorted_points_sorted_by_setter)
{
    const std::vector<Curve_point> unsorted{
        Curve_point{.x = 1.0f, .y = 1.0f, .left_slope = 0.0f, .right_slope = 0.0f},
        Curve_point{.x = 0.0f, .y = 0.0f, .left_slope = 0.0f, .right_slope = 0.0f}
    };
    const Node_descriptor descriptor = make_curve_descriptor(identity_curve);
    Compose_node          node{descriptor, 1};
    node.set_curve("curve", unsorted);
    const Composer    composer{};
    const Shader_code shader_code = composer.compose(node, 0);
    const std::string function = find_global(shader_code, "o1_curve_curve");
    ASSERT_FALSE(function.empty());
    // The first segment's dx uses the lowest x (0.0).
    EXPECT_NE(function.find("float dx = x - 0.000000000;"), std::string::npos);
    EXPECT_EQ(composer.assemble_fragment(shader_code).find("(error:"), std::string::npos);
}

TEST(Curve, single_point_is_constant)
{
    const std::vector<Curve_point> one_point{
        Curve_point{.x = 0.0f, .y = 0.7f, .left_slope = 0.0f, .right_slope = 0.0f}
    };
    const Node_descriptor descriptor = make_curve_descriptor(one_point);
    const Compose_node    node{descriptor, 1};
    const Composer        composer{};
    const Shader_code     shader_code = composer.compose(node, 0);
    const std::string     function = find_global(shader_code, "o1_curve_curve");
    ASSERT_FALSE(function.empty());
    EXPECT_NE(function.find("return 0.699999988;"), std::string::npos);
    EXPECT_EQ(function.find("omt3"), std::string::npos);
}

TEST(Curve, fragment_assembles_without_error_marker)
{
    const Node_descriptor descriptor = make_curve_descriptor(identity_curve);
    const Compose_node    node{descriptor, 1};
    const Composer        composer{};
    const Shader_code     shader_code = composer.compose(node, 0);
    EXPECT_EQ(composer.assemble_fragment(shader_code).find("(error:"), std::string::npos);
}

} // namespace erhe::texgen::test
