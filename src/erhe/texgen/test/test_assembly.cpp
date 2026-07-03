#include "erhe_texgen/common_library.hpp"
#include "erhe_texgen/compose_node.hpp"
#include "erhe_texgen/composer.hpp"
#include "erhe_texgen/node_descriptor.hpp"

#include "test_descriptors.hpp"

#include <gtest/gtest.h>

#include <fmt/format.h>

#include <cstddef>
#include <string>

namespace erhe::texgen::test {

TEST(Assembly, golden_constant_color_byte_exact)
{
    const Node_descriptor descriptor = make_constant_color_descriptor();
    const Compose_node node{descriptor, 1};
    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0);
    const std::string fragment = composer.assemble_fragment(shader_code);
    EXPECT_EQ(
        fragment,
        "uniform vec4 p_o1_color = vec4(1.000000000, 1.000000000, 1.000000000, 1.000000000);\n"
        "void main() {\n"
        "vec2 uv = v_texcoord;\n"
        "vec4 o1_0_0_rgba = p_o1_color;\n"
        "out_color = o1_0_0_rgba;\n"
        "}\n"
    );
}

TEST(Assembly, golden_gradient_blend_structure)
{
    const Node_descriptor gradient_descriptor  = make_uv_gradient_descriptor();
    const Node_descriptor grayscale_descriptor = make_grayscale_value_descriptor();
    const Node_descriptor blend_descriptor     = make_blend_multiply_descriptor();

    const Compose_node gradient {gradient_descriptor,  1};
    const Compose_node grayscale{grayscale_descriptor, 2};
    Compose_node       blend    {blend_descriptor,     3};
    blend.set_input(blend.get_input_index("in1"), &gradient,  0);
    blend.set_input(blend.get_input_index("in2"), &grayscale, 0);

    const Composer composer{};
    const Shader_code shader_code = composer.compose(blend, 0);
    const std::string fragment = composer.assemble_fragment(shader_code);

    // Uniform declarations for all live parameters
    EXPECT_NE(fragment.find("uniform float p_o3_amount = 1.000000000;"), std::string::npos);
    EXPECT_NE(fragment.find("uniform float p_o2_value = 0.500000000;"),  std::string::npos);
    // Entry point skeleton
    EXPECT_NE(fragment.find("void main() {"),        std::string::npos);
    EXPECT_NE(fragment.find("vec2 uv = v_texcoord;"), std::string::npos);
    // rgb -> rgba conversion inserted at the link
    EXPECT_NE(fragment.find("vec4(o1_0_0_rgb, 1.0)"), std::string::npos);
    // Final assignment (rgba sink, no wrap)
    EXPECT_NE(fragment.find("out_color = o3_0_0_rgba;"), std::string::npos);
    // Upstream stanzas precede the sink stanza
    const std::size_t gradient_position = fragment.find("vec3 o1_0_0_rgb =");
    const std::size_t blend_position    = fragment.find("vec4 o3_0_0_rgba =");
    ASSERT_NE(gradient_position, std::string::npos);
    ASSERT_NE(blend_position,    std::string::npos);
    EXPECT_LT(gradient_position, blend_position);
    // No common library needed
    EXPECT_EQ(fragment.find("float rand(vec2 x)"), std::string::npos);
}

TEST(Assembly, golden_noise_includes_common_library_and_global)
{
    const Node_descriptor descriptor = make_fake_noise_descriptor();
    Compose_node node{descriptor, 1};
    node.set_seed(0.5f);
    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0);
    const std::string fragment = composer.assemble_fragment(shader_code);

    // Common library first (referenced through the node's global helper)
    EXPECT_EQ(fragment.rfind("float dot2(vec2 x) {", 0), 0u);
    EXPECT_NE(fragment.find("float rand(vec2 x) {"),      std::string::npos);
    EXPECT_NE(fragment.find("float param_rnd(float minimum, float maximum, float seed) {"), std::string::npos);
    // Node global, seed uniform, composed expression
    EXPECT_NE(fragment.find("float test_noise(vec2 uv, float seed) {"), std::string::npos);
    EXPECT_NE(fragment.find("uniform float p_o1_seed = 0.500000000;"),  std::string::npos);
    EXPECT_NE(fragment.find("float o1_0_0_f = test_noise(uv, p_o1_seed);"), std::string::npos);
    // Grayscale sink is wrapped to vec4 for the output assignment
    EXPECT_NE(fragment.find("out_color = vec4(vec3(o1_0_0_f), 1.0);"), std::string::npos);
    // Library precedes the node global which precedes main
    const std::size_t library_position = fragment.find("float rand(vec2 x) {");
    const std::size_t global_position  = fragment.find("float test_noise(vec2 uv, float seed) {");
    const std::size_t main_position    = fragment.find("void main() {");
    EXPECT_LT(library_position, global_position);
    EXPECT_LT(global_position,  main_position);
}

TEST(Assembly, common_library_modes)
{
    const Node_descriptor descriptor = make_constant_color_descriptor();
    const Compose_node node{descriptor, 1};
    {
        Compose_options options{};
        options.common_library_mode = Common_library_mode::always;
        const Composer composer{options};
        const std::string fragment = composer.assemble_fragment(composer.compose(node, 0));
        EXPECT_NE(fragment.find("float rand(vec2 x) {"), std::string::npos);
    }
    {
        Compose_options options{};
        options.common_library_mode = Common_library_mode::never;
        const Composer composer{options};
        const std::string fragment = composer.assemble_fragment(composer.compose(node, 0));
        EXPECT_EQ(fragment.find("float rand(vec2 x) {"), std::string::npos);
    }
}

TEST(Assembly, uniform_list_is_ordered_sink_first_then_upstream)
{
    const Node_descriptor gradient_descriptor  = make_uv_gradient_descriptor();
    const Node_descriptor grayscale_descriptor = make_grayscale_value_descriptor();
    const Node_descriptor blend_descriptor     = make_blend_multiply_descriptor();

    const Compose_node gradient {gradient_descriptor,  1};
    const Compose_node grayscale{grayscale_descriptor, 2};
    Compose_node       blend    {blend_descriptor,     3};
    blend.set_input(blend.get_input_index("in1"), &gradient,  0);
    blend.set_input(blend.get_input_index("in2"), &grayscale, 0);

    const Composer composer{};
    const Shader_code shader_code = composer.compose(blend, 0);
    ASSERT_EQ(shader_code.get_uniforms().size(), 2u);
    EXPECT_EQ(shader_code.get_uniforms()[0].name, "p_o3_amount");
    EXPECT_EQ(shader_code.get_uniforms()[1].name, "p_o2_value");
}

TEST(Assembly, uniform_accessor_prefix_rewrites_references_not_names)
{
    const Node_descriptor descriptor = make_grayscale_value_descriptor();
    const Compose_node node{descriptor, 1};

    Compose_options options{};
    options.uniform_accessor_prefix   = "params.";
    options.uniform_declaration_mode  = Uniform_declaration_mode::none;
    const Composer composer{options};
    const Shader_code shader_code = composer.compose(node, 0);

    // Generated code references the uniform through the accessor prefix
    EXPECT_EQ(shader_code.get_code(), "float o1_0_0_f = params.p_o1_value;\n");
    // The registered uniform name stays unprefixed (it is the UBO member name)
    ASSERT_EQ(shader_code.get_uniforms().size(), 1u);
    EXPECT_EQ(shader_code.get_uniforms()[0].name, "p_o1_value");
    // Declaration mode none: no plain uniform declarations in the fragment
    const std::string fragment = composer.assemble_fragment(shader_code);
    EXPECT_EQ(fragment.find("uniform "), std::string::npos);
}

TEST(Assembly, entry_point_and_output_variable_options)
{
    const Node_descriptor descriptor = make_uv_gradient_descriptor();
    const Compose_node node{descriptor, 1};

    Compose_options options{};
    options.function_name        = "texgen_main";
    options.output_variable_name = "frag_color";
    options.uv_source_expression = "gl_FragCoord.xy / resolution";
    const Composer composer{options};
    const std::string fragment = composer.assemble_fragment(composer.compose(node, 0));

    EXPECT_NE(fragment.find("void texgen_main() {"),                    std::string::npos);
    EXPECT_NE(fragment.find("vec2 uv = gl_FragCoord.xy / resolution;"), std::string::npos);
    // rgb sink wrapped to vec4 and assigned to the configured variable
    EXPECT_NE(fragment.find("frag_color = vec4(o1_0_0_rgb, 1.0);"),     std::string::npos);
    EXPECT_EQ(fragment.find("void main() {"), std::string::npos);
}

TEST(Assembly, grayscale_sink_is_wrapped_to_vec4)
{
    const Node_descriptor descriptor = make_grayscale_value_descriptor();
    const Compose_node node{descriptor, 1};
    const Composer composer{};
    const std::string fragment = composer.assemble_fragment(composer.compose(node, 0));
    EXPECT_NE(fragment.find("out_color = vec4(vec3(o1_0_0_f), 1.0);"), std::string::npos);
}

TEST(Assembly, changing_parameter_value_changes_uniform_value_not_source)
{
    // The live-edit contract: a float/color parameter edit changes only the
    // uniform's registered value; the assembled source with declarations
    // disabled is byte-identical, so no recompile is needed.
    const Node_descriptor descriptor = make_grayscale_value_descriptor();
    Compose_node node{descriptor, 1};

    Compose_options options{};
    options.uniform_declaration_mode = Uniform_declaration_mode::none;
    const Composer composer{options};

    const Shader_code before_code = composer.compose(node, 0);
    const std::string before = composer.assemble_fragment(before_code);
    node.set_float("value", 0.9f);
    const Shader_code after_code = composer.compose(node, 0);
    const std::string after = composer.assemble_fragment(after_code);

    EXPECT_EQ(before, after);
    EXPECT_FLOAT_EQ(before_code.get_uniforms()[0].value[0], 0.5f);
    EXPECT_FLOAT_EQ(after_code.get_uniforms()[0].value[0],  0.9f);
}

TEST(Assembly, decompose_style_multi_output_selects_channel_per_output_index)
{
    // A single rgba input decomposed into four grayscale outputs (like the
    // editor's Decompose node): composing at output index i must select the
    // matching channel expression. Exercises Composer's output_index selection
    // for a >2-output descriptor.
    Node_descriptor descriptor{};
    descriptor.name = "decompose_like";
    Input_descriptor input{};
    input.name               = "i";
    input.type               = Value_type::rgba;
    input.default_expression = "vec4(0.1, 0.2, 0.3, 0.4)";
    descriptor.inputs.push_back(input);
    const char* const channels[4] = {".r", ".g", ".b", ".a"};
    for (const char* channel : channels) {
        Output_descriptor output{};
        output.type       = Value_type::grayscale;
        output.expression = std::string{"$i($uv)"} + channel;
        descriptor.outputs.push_back(output);
    }

    const Composer composer{};
    for (std::size_t output_index = 0; output_index < 4; ++output_index) {
        const Compose_node node{descriptor, 1};
        const std::string  fragment = composer.assemble_fragment(composer.compose(node, output_index));
        const std::string  expected = fmt::format(
            "float o1_{}_0_f = (vec4(0.1, 0.2, 0.3, 0.4)){};", output_index, channels[output_index]
        );
        EXPECT_NE(fragment.find(expected), std::string::npos) << "output_index " << output_index << "\n" << fragment;
    }
}

} // namespace erhe::texgen::test
