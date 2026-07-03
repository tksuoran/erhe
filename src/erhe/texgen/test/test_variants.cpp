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

// Filter sampling one grayscale input at two different coordinates (inline
// form, no function flag) - drives the variant context.
[[nodiscard]] auto make_two_coordinate_sampler_descriptor() -> Node_descriptor
{
    Node_descriptor descriptor{};
    descriptor.name = "two_coordinate_sampler";
    Input_descriptor source{};
    source.name               = "source";
    source.type               = Value_type::grayscale;
    source.default_expression = "0.0";
    descriptor.inputs.push_back(source);
    Output_descriptor output{};
    output.type       = Value_type::grayscale;
    output.expression = "$source($uv) + $source($uv + vec2(0.5, 0.5))";
    descriptor.outputs.push_back(output);
    return descriptor;
}

} // anonymous namespace

TEST(Variants, same_source_sampled_at_same_coordinates_emits_one_stanza)
{
    const Node_descriptor offset_descriptor = make_code_offset_descriptor();
    const Node_descriptor blend_descriptor  = make_blend_multiply_descriptor();

    const Compose_node offset{offset_descriptor, 2};
    Compose_node       blend {blend_descriptor,  3};
    blend.set_input(blend.get_input_index("in1"), &offset, 0);
    blend.set_input(blend.get_input_index("in2"), &offset, 0);

    const Composer composer{};
    const Shader_code shader_code = composer.compose(blend, 0);
    const std::string& code = shader_code.get_code();

    // The upstream code stanza and its output local appear exactly once...
    EXPECT_EQ(count_occurrences(code, "float o2_0_value ="), 1u);
    EXPECT_EQ(count_occurrences(code, "float o2_0_1_f ="),   1u);
    // ...and both inputs reference the shared local (3 = blend template
    // samples in1 twice, in2 once)
    EXPECT_EQ(count_occurrences(code, "vec4(vec3(o2_0_1_f), 1.0)"), 3u);
}

TEST(Variants, same_source_sampled_at_different_coordinates_emits_two_variants)
{
    const Node_descriptor offset_descriptor  = make_code_offset_descriptor();
    const Node_descriptor sampler_descriptor = make_two_coordinate_sampler_descriptor();

    const Compose_node offset {offset_descriptor,  2};
    Compose_node       sampler{sampler_descriptor, 3};
    sampler.set_input(sampler.get_input_index("source"), &offset, 0);

    const Composer composer{};
    const Shader_code shader_code = composer.compose(sampler, 0);
    const std::string& code = shader_code.get_code();

    // Two code stanzas with variant-distinct local names ($name_uv)
    EXPECT_EQ(count_occurrences(code, "float o2_0_value ="), 1u);
    EXPECT_EQ(count_occurrences(code, "float o2_2_value ="), 1u);
    // Two output locals, one per sampled coordinate variant
    EXPECT_EQ(count_occurrences(code, "float o2_0_1_f ="), 1u);
    EXPECT_EQ(count_occurrences(code, "float o2_0_3_f ="), 1u);
}

TEST(Variants, uniforms_are_registered_once_for_repeated_samplings)
{
    const Node_descriptor offset_descriptor  = make_code_offset_descriptor();
    const Node_descriptor sampler_descriptor = make_two_coordinate_sampler_descriptor();

    const Compose_node offset {offset_descriptor,  2};
    Compose_node       sampler{sampler_descriptor, 3};
    sampler.set_input(sampler.get_input_index("source"), &offset, 0);

    const Composer composer{};
    const Shader_code shader_code = composer.compose(sampler, 0);
    ASSERT_EQ(shader_code.get_uniforms().size(), 1u);
    EXPECT_EQ(shader_code.get_uniforms()[0].name, "p_o2_offset");
}

TEST(Variants, identical_globals_from_two_nodes_are_emitted_once)
{
    const Node_descriptor noise_descriptor = make_fake_noise_descriptor();
    const Node_descriptor blend_descriptor = make_blend_multiply_descriptor();

    const Compose_node noise_a{noise_descriptor, 1};
    const Compose_node noise_b{noise_descriptor, 2};
    Compose_node       blend  {blend_descriptor, 3};
    blend.set_input(blend.get_input_index("in1"), &noise_a, 0);
    blend.set_input(blend.get_input_index("in2"), &noise_b, 0);

    const Composer composer{};
    const Shader_code shader_code = composer.compose(blend, 0);
    ASSERT_EQ(shader_code.get_globals().size(), 1u);
    EXPECT_NE(shader_code.get_globals()[0].find("float test_noise"), std::string::npos);

    // Both nodes still contribute their own seed uniforms and output locals
    EXPECT_EQ(count_occurrences(shader_code.get_code(), "test_noise((uv), p_o1_seed)"), 1u);
    EXPECT_EQ(count_occurrences(shader_code.get_code(), "test_noise((uv), p_o2_seed)"), 1u);
}

TEST(Variants, distinct_nodes_get_distinct_id_based_names)
{
    const Node_descriptor value_descriptor = make_grayscale_value_descriptor();
    const Node_descriptor blend_descriptor = make_blend_multiply_descriptor();

    const Compose_node value_a{value_descriptor, 10};
    const Compose_node value_b{value_descriptor, 11};
    Compose_node       blend  {blend_descriptor, 12};
    blend.set_input(blend.get_input_index("in1"), &value_a, 0);
    blend.set_input(blend.get_input_index("in2"), &value_b, 0);

    const Composer composer{};
    const Shader_code shader_code = composer.compose(blend, 0);
    const std::string& code = shader_code.get_code();
    EXPECT_NE(code.find("float o10_0_0_f = p_o10_value;"), std::string::npos);
    EXPECT_NE(code.find("float o11_0_0_f = p_o11_value;"), std::string::npos);
    ASSERT_EQ(shader_code.get_uniforms().size(), 3u);
}

TEST(Variants, multi_output_source_selects_expression_and_distinct_locals)
{
    const Node_descriptor source_descriptor = make_two_output_descriptor();

    // Consumer summing input a (source output 0) and input b (source output 1),
    // both sampled at the same coordinate.
    Node_descriptor consumer_descriptor{};
    consumer_descriptor.name = "sum_two";
    Input_descriptor a{};
    a.name               = "a";
    a.type               = Value_type::grayscale;
    a.default_expression = "0.0";
    consumer_descriptor.inputs.push_back(a);
    Input_descriptor b{};
    b.name               = "b";
    b.type               = Value_type::grayscale;
    b.default_expression = "0.0";
    consumer_descriptor.inputs.push_back(b);
    Output_descriptor out{};
    out.type       = Value_type::grayscale;
    out.expression = "$a($uv) + $b($uv)";
    consumer_descriptor.outputs.push_back(out);

    const Compose_node source  {source_descriptor,   1};
    Compose_node       consumer{consumer_descriptor, 2};
    consumer.set_input(consumer.get_input_index("a"), &source, 0);
    consumer.set_input(consumer.get_input_index("b"), &source, 1);

    const Composer composer{};
    const Shader_code shader_code = composer.compose(consumer, 0);
    const std::string& code = shader_code.get_code();

    // output_index 0 selects ($uv).x, output_index 1 selects ($uv).y.
    // replace_variables resolves right-to-left, so $b (source output 1) is
    // composed first (variant index 0 -> o1_1_0_f) and $a (source output 0)
    // second (variant index 1 -> o1_0_1_f). The two locals stay distinct and
    // do not collide.
    EXPECT_NE(code.find("float o1_1_0_f = ((uv)).y;"), std::string::npos);
    EXPECT_NE(code.find("float o1_0_1_f = ((uv)).x;"), std::string::npos);
    // Distinct locals, each emitted once, no collision.
    EXPECT_EQ(count_occurrences(code, "float o1_1_0_f ="), 1u);
    EXPECT_EQ(count_occurrences(code, "float o1_0_1_f ="), 1u);
    // Consuming expression references both distinct locals.
    EXPECT_NE(code.find("float o2_0_0_f = o1_0_1_f + o1_1_0_f;"), std::string::npos);
}

TEST(Variants, shared_upstream_across_function_and_inline_inputs_dedups_declarations)
{
    // Node X (noise: has a global + seed uniform) feeds BOTH the function-form
    // input of A (fake_blur) and the inline input of B (code_offset). A and B
    // feed a blend sink. X's global and uniform must appear exactly once, and
    // A's helper function must be defined exactly once.
    const Node_descriptor noise_descriptor  = make_fake_noise_descriptor();  // X
    const Node_descriptor blur_descriptor    = make_fake_blur_descriptor();   // A (function input)
    const Node_descriptor offset_descriptor  = make_code_offset_descriptor(); // B (inline input)
    const Node_descriptor blend_descriptor   = make_blend_multiply_descriptor();

    const Compose_node noise{noise_descriptor,  1};
    Compose_node       a    {blur_descriptor,   2};
    Compose_node       b    {offset_descriptor, 3};
    Compose_node       sink {blend_descriptor,  4};
    a.set_input(a.get_input_index("source"),    &noise, 0);
    b.set_input(b.get_input_index("source"),    &noise, 0);
    sink.set_input(sink.get_input_index("in1"), &a,     0);
    sink.set_input(sink.get_input_index("in2"), &b,     0);

    const Composer composer{};
    const Shader_code shader_code = composer.compose(sink, 0);

    // X's global present exactly once.
    ASSERT_EQ(shader_code.get_globals().size(), 1u);
    EXPECT_NE(shader_code.get_globals()[0].find("float test_noise"), std::string::npos);

    // X's seed uniform registered exactly once.
    std::size_t seed_uniform_count = 0;
    for (const Uniform& uniform : shader_code.get_uniforms()) {
        if (uniform.name == "p_o1_seed") {
            ++seed_uniform_count;
        }
    }
    EXPECT_EQ(seed_uniform_count, 1u);

    // A's input helper function defined exactly once.
    EXPECT_EQ(count_occurrences(shader_code.get_defs(), "float o2_input_source(vec2 uv) {"), 1u);
}

} // namespace erhe::texgen::test
