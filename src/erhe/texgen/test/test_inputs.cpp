#include "erhe_texgen/compose_node.hpp"
#include "erhe_texgen/composer.hpp"
#include "erhe_texgen/node_descriptor.hpp"

#include "test_descriptors.hpp"

#include <gtest/gtest.h>

#include <string>

namespace erhe::texgen::test {

namespace {

[[nodiscard]] auto count_occurrences(const std::string& text, const std::string& pattern) -> std::size_t
{
    std::size_t count    = 0;
    std::size_t position = 0;
    while (true) {
        position = text.find(pattern, position);
        if (position == std::string::npos) {
            return count;
        }
        ++count;
        position += pattern.length();
    }
}

} // anonymous namespace

TEST(Inputs, unconnected_input_falls_back_to_default_expression)
{
    const Node_descriptor blend_descriptor = make_blend_multiply_descriptor();
    const Compose_node blend{blend_descriptor, 1};
    const Composer composer{};
    const Shader_code shader_code = composer.compose(blend, 0);
    EXPECT_EQ(
        shader_code.get_code(),
        "vec4 o1_0_0_rgba = mix((vec4(1.0)), (vec4(1.0)) * (vec4(1.0)), p_o1_amount);\n"
    );
}

TEST(Inputs, unconnected_default_rebinds_uv_to_sampling_coordinates)
{
    Node_descriptor descriptor{};
    descriptor.name = "offset_sampler";
    Input_descriptor source{};
    source.name               = "source";
    source.type               = Value_type::grayscale;
    source.default_expression = "($uv).x";
    descriptor.inputs.push_back(source);
    Output_descriptor output{};
    output.type       = Value_type::grayscale;
    output.expression = "$source($uv + vec2(0.1, 0.0))";
    descriptor.outputs.push_back(output);

    const Compose_node node{descriptor, 1};
    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0);
    // The default expression is wrapped in parentheses so its precedence
    // matches the connected path's atomic local.
    EXPECT_EQ(shader_code.get_code(), "float o1_0_0_f = (((uv + vec2(0.1, 0.0))).x);\n");
}

TEST(Inputs, inline_input_composes_upstream_and_inserts_conversions)
{
    const Node_descriptor gradient_descriptor  = make_uv_gradient_descriptor();
    const Node_descriptor grayscale_descriptor = make_grayscale_value_descriptor();
    const Node_descriptor blend_descriptor     = make_blend_multiply_descriptor();

    const Compose_node gradient {gradient_descriptor,  1};
    const Compose_node grayscale{grayscale_descriptor, 2};
    Compose_node       blend    {blend_descriptor,     3};
    blend.set_input(blend.get_input_index("in1"), &gradient,  0); // rgb  -> rgba conversion
    blend.set_input(blend.get_input_index("in2"), &grayscale, 0); // f    -> rgba conversion

    const Composer composer{};
    const Shader_code shader_code = composer.compose(blend, 0);
    EXPECT_EQ(
        shader_code.get_code(),
        "float o2_0_0_f = p_o2_value;\n"
        "vec3 o1_0_0_rgb = vec3(((uv)).x, ((uv)).y, 0.0);\n"
        "vec4 o3_0_0_rgba = mix(vec4(o1_0_0_rgb, 1.0), vec4(o1_0_0_rgb, 1.0) * vec4(vec3(o2_0_0_f), 1.0), p_o3_amount);\n"
    );
    EXPECT_EQ(shader_code.get_output_expression(), "o3_0_0_rgba");
    EXPECT_EQ(shader_code.get_output_type(), Value_type::rgba);
}

TEST(Inputs, no_conversion_inserted_on_matching_types)
{
    const Node_descriptor value_descriptor = make_grayscale_value_descriptor();
    const Node_descriptor code_descriptor  = make_code_offset_descriptor();

    const Compose_node value{value_descriptor, 1};
    Compose_node       node {code_descriptor,  2};
    node.set_input(node.get_input_index("source"), &value, 0); // f -> f

    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0);
    EXPECT_NE(shader_code.get_code().find("o1_0_0_f + p_o2_offset"), std::string::npos);
    EXPECT_EQ(shader_code.get_code().find("vec3(o1_0_0_f)"), std::string::npos);
}

TEST(Inputs, three_node_inline_chain_orders_stanzas_upstream_first)
{
    const Node_descriptor value_descriptor = make_grayscale_value_descriptor();
    const Node_descriptor code_descriptor  = make_code_offset_descriptor();

    const Compose_node value{value_descriptor, 1};
    Compose_node       mid  {code_descriptor,  2};
    Compose_node       sink {code_descriptor,  3};
    mid.set_input (mid.get_input_index("source"),  &value, 0);
    sink.set_input(sink.get_input_index("source"), &mid,   0);

    const Composer composer{};
    const Shader_code shader_code = composer.compose(sink, 0);
    EXPECT_EQ(
        shader_code.get_code(),
        "float o1_0_0_f = p_o1_value;\n"
        "float o2_0_value = o1_0_0_f + p_o2_offset;\n"
        "float o2_0_1_f = o2_0_value;\n"
        "float o3_0_value = o2_0_1_f + p_o3_offset;\n"
        "float o3_0_1_f = o3_0_value;\n"
    );
    EXPECT_EQ(shader_code.get_output_expression(), "o3_0_1_f");
}

TEST(Inputs, function_form_emits_helper_once_and_substitutes_calls)
{
    const Node_descriptor value_descriptor = make_grayscale_value_descriptor();
    const Node_descriptor blur_descriptor  = make_fake_blur_descriptor();

    const Compose_node value{value_descriptor, 1};
    Compose_node       blur {blur_descriptor,  2};
    blur.set_input(blur.get_input_index("source"), &value, 0);

    const Composer composer{};
    const Shader_code shader_code = composer.compose(blur, 0);

    // Helper declared exactly once in defs, wrapping the upstream subtree
    EXPECT_EQ(count_occurrences(shader_code.get_defs(), "float o2_input_source(vec2 uv) {"), 1u);
    EXPECT_NE(shader_code.get_defs().find("float o1_0_0_f = p_o1_value;"), std::string::npos);
    EXPECT_NE(shader_code.get_defs().find("return o1_0_0_f;"), std::string::npos);

    // Both sampling sites substituted as calls; upstream code stays out of
    // the inline code section
    EXPECT_EQ(
        shader_code.get_code(),
        "float o2_0_0_f = 0.5 * (o2_input_source(uv) + o2_input_source(uv + vec2(0.01, 0.0)));\n"
    );

    // Upstream uniform still registered
    ASSERT_EQ(shader_code.get_uniforms().size(), 1u);
    EXPECT_EQ(shader_code.get_uniforms()[0].name, "p_o1_value");
}

TEST(Inputs, function_form_return_value_is_converted_on_type_mismatch)
{
    const Node_descriptor gradient_descriptor = make_uv_gradient_descriptor();
    const Node_descriptor blur_descriptor     = make_fake_blur_descriptor();

    const Compose_node gradient{gradient_descriptor, 1};
    Compose_node       blur    {blur_descriptor,     2};
    blur.set_input(blur.get_input_index("source"), &gradient, 0); // rgb -> f input

    const Composer composer{};
    const Shader_code shader_code = composer.compose(blur, 0);
    EXPECT_NE(
        shader_code.get_defs().find("return (dot(o1_0_0_rgb, vec3(1.0))/3.0);"),
        std::string::npos
    );
}

TEST(Inputs, unconnected_default_is_parenthesized_for_precedence)
{
    // A default consumed inside a larger expression must not leak precedence:
    // "$in($uv) * 2.0" with default "1.0 - $uv.x" yields "(1.0 - (uv).x) * 2.0".
    Node_descriptor descriptor{};
    descriptor.name = "scaled_default";
    Input_descriptor in{};
    in.name               = "in";
    in.type               = Value_type::grayscale;
    in.default_expression = "1.0 - $uv.x";
    descriptor.inputs.push_back(in);
    Output_descriptor output{};
    output.type       = Value_type::grayscale;
    output.expression = "$in($uv) * 2.0";
    descriptor.outputs.push_back(output);

    const Compose_node node{descriptor, 1};
    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0);
    EXPECT_EQ(shader_code.get_code(), "float o1_0_0_f = (1.0 - (uv).x) * 2.0;\n");
}

TEST(Inputs, connected_and_unconnected_are_structurally_equivalent)
{
    // The connected path yields an atomic local; the unconnected path yields
    // a parenthesized default. Both are single units in the consuming
    // template "$in($uv) + 1.0", so the surrounding structure is identical.
    Node_descriptor descriptor{};
    descriptor.name = "passthrough";
    Input_descriptor in{};
    in.name               = "in";
    in.type               = Value_type::grayscale;
    in.default_expression = "0.25 + 0.5";
    descriptor.inputs.push_back(in);
    Output_descriptor output{};
    output.type       = Value_type::grayscale;
    output.expression = "$in($uv) + 1.0";
    descriptor.outputs.push_back(output);

    const Composer composer{};

    // Unconnected: default wrapped as a single unit.
    const Compose_node unconnected{descriptor, 1};
    const Shader_code unconnected_code = composer.compose(unconnected, 0);
    EXPECT_EQ(unconnected_code.get_code(), "float o1_0_0_f = (0.25 + 0.5) + 1.0;\n");

    // Connected: atomic local as a single unit; the " + 1.0" tail is identical.
    const Node_descriptor value_descriptor = make_grayscale_value_descriptor();
    const Compose_node value{value_descriptor, 2};
    Compose_node       connected{descriptor, 1};
    connected.set_input(connected.get_input_index("in"), &value, 0);
    const Shader_code connected_code = composer.compose(connected, 0);
    EXPECT_NE(connected_code.get_code().find("o1_0_0_f = o2_0_0_f + 1.0;\n"), std::string::npos);
}

TEST(Inputs, unconnected_function_input_falls_back_to_inline_default)
{
    const Node_descriptor blur_descriptor = make_fake_blur_descriptor();
    const Compose_node blur{blur_descriptor, 2};
    const Composer composer{};
    const Shader_code shader_code = composer.compose(blur, 0);
    EXPECT_TRUE(shader_code.get_defs().empty());
    EXPECT_EQ(shader_code.get_code(), "float o2_0_0_f = 0.5 * ((0.0) + (0.0));\n");
}

TEST(Cycles, self_binding_inline_input_emits_error_and_terminates)
{
    Node_descriptor descriptor{};
    descriptor.name = "self_inline";
    Input_descriptor in{};
    in.name               = "in";
    in.type               = Value_type::grayscale;
    in.default_expression = "0.0";
    descriptor.inputs.push_back(in);
    Output_descriptor output{};
    output.type       = Value_type::grayscale;
    output.expression = "$in($uv)";
    descriptor.outputs.push_back(output);

    Compose_node node{descriptor, 1};
    node.set_input(node.get_input_index("in"), &node, 0); // self reference

    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0); // must not stack overflow
    EXPECT_NE(shader_code.get_code().find("(error:"), std::string::npos);
}

TEST(Cycles, two_node_inline_cycle_with_coordinate_transform_terminates)
{
    // A -> B -> A, each transforming the coordinate so no uv-variant ever
    // repeats: only the active-path cycle guard can stop this.
    Node_descriptor descriptor{};
    descriptor.name = "cyclic";
    Input_descriptor in{};
    in.name               = "in";
    in.type               = Value_type::grayscale;
    in.default_expression = "0.0";
    descriptor.inputs.push_back(in);
    Output_descriptor output{};
    output.type       = Value_type::grayscale;
    output.expression = "$in(($uv) * 2.0)";
    descriptor.outputs.push_back(output);

    Compose_node a{descriptor, 1};
    Compose_node b{descriptor, 2};
    a.set_input(a.get_input_index("in"), &b, 0);
    b.set_input(b.get_input_index("in"), &a, 0);

    const Composer composer{};
    const Shader_code shader_code = composer.compose(a, 0); // must terminate
    EXPECT_NE(shader_code.get_code().find("(error:"), std::string::npos);
}

TEST(Cycles, function_form_self_cycle_emits_error_and_terminates)
{
    Node_descriptor descriptor{};
    descriptor.name = "self_function";
    Input_descriptor in{};
    in.name               = "in";
    in.type               = Value_type::grayscale;
    in.default_expression = "0.0";
    in.function           = true;
    descriptor.inputs.push_back(in);
    Output_descriptor output{};
    output.type       = Value_type::grayscale;
    output.expression = "$in($uv)";
    descriptor.outputs.push_back(output);

    Compose_node node{descriptor, 1};
    node.set_input(node.get_input_index("in"), &node, 0); // self reference

    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0); // must terminate
    // The cycle surfaces inside the emitted helper function body.
    EXPECT_NE(shader_code.get_defs().find("(error:"), std::string::npos);
}

TEST(Cycles, identity_uv_inline_cycle_does_not_emit_self_referential_glsl)
{
    // Identity coordinate: memoization would otherwise terminate but emit
    // "oX = f(oX)". The cycle guard must break it with an error marker
    // instead, and no output local may reference itself.
    Node_descriptor descriptor{};
    descriptor.name = "identity_cycle";
    Input_descriptor in{};
    in.name               = "in";
    in.type               = Value_type::grayscale;
    in.default_expression = "0.0";
    descriptor.inputs.push_back(in);
    Output_descriptor output{};
    output.type       = Value_type::grayscale;
    output.expression = "$in($uv)";
    descriptor.outputs.push_back(output);

    Compose_node node{descriptor, 1};
    node.set_input(node.get_input_index("in"), &node, 0);

    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0);
    EXPECT_NE(shader_code.get_code().find("(error:"), std::string::npos);
    // No self-referential assignment "oX = ... oX ...".
    EXPECT_EQ(shader_code.get_code(), "float o1_0_0_f = /* (error: cycle) */ 0.0;\n");
}

} // namespace erhe::texgen::test
