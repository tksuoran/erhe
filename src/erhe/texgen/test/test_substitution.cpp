#include "erhe_texgen/compose_node.hpp"
#include "erhe_texgen/composer.hpp"
#include "erhe_texgen/node_descriptor.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace erhe::texgen::test {

namespace {

// Descriptor with a single output expression and no inputs/parameters
// unless added by the caller.
[[nodiscard]] auto make_expression_descriptor(const Value_type type, const std::string& expression) -> Node_descriptor
{
    Node_descriptor descriptor{};
    descriptor.name = "test_expression";
    Output_descriptor output{};
    output.type       = type;
    output.expression = expression;
    descriptor.outputs.push_back(output);
    return descriptor;
}

} // anonymous namespace

TEST(Substitution, float_parameter_becomes_uniform_reference)
{
    Node_descriptor descriptor = make_expression_descriptor(Value_type::grayscale, "$value");
    Parameter_descriptor parameter{};
    parameter.name          = "value";
    parameter.kind          = Parameter_kind::float_parameter;
    parameter.default_float = 0.5f;
    descriptor.parameters.push_back(parameter);

    Compose_node node{descriptor, 1};
    node.set_float("value", 0.75f);
    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0);

    EXPECT_EQ(shader_code.get_code(), "float o1_0_0_f = p_o1_value;\n");
    EXPECT_EQ(shader_code.get_output_expression(), "o1_0_0_f");
    EXPECT_EQ(shader_code.get_output_type(), Value_type::grayscale);
    ASSERT_EQ(shader_code.get_uniforms().size(), 1u);
    const Uniform& uniform = shader_code.get_uniforms()[0];
    EXPECT_EQ(uniform.name, "p_o1_value");
    EXPECT_EQ(uniform.kind, Uniform_kind::float_uniform);
    EXPECT_FLOAT_EQ(uniform.value[0], 0.75f);
}

TEST(Substitution, color_parameter_becomes_vec4_uniform_reference)
{
    Node_descriptor descriptor = make_expression_descriptor(Value_type::rgba, "$tint");
    Parameter_descriptor parameter{};
    parameter.name = "tint";
    parameter.kind = Parameter_kind::color_parameter;
    descriptor.parameters.push_back(parameter);

    Compose_node node{descriptor, 3};
    node.set_color("tint", {0.1f, 0.2f, 0.3f, 0.4f});
    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0);

    EXPECT_EQ(shader_code.get_code(), "vec4 o3_0_0_rgba = p_o3_tint;\n");
    ASSERT_EQ(shader_code.get_uniforms().size(), 1u);
    const Uniform& uniform = shader_code.get_uniforms()[0];
    EXPECT_EQ(uniform.name, "p_o3_tint");
    EXPECT_EQ(uniform.kind, Uniform_kind::vec4_uniform);
    EXPECT_FLOAT_EQ(uniform.value[0], 0.1f);
    EXPECT_FLOAT_EQ(uniform.value[1], 0.2f);
    EXPECT_FLOAT_EQ(uniform.value[2], 0.3f);
    EXPECT_FLOAT_EQ(uniform.value[3], 0.4f);
}

TEST(Substitution, bool_parameter_substitutes_literal)
{
    Node_descriptor descriptor = make_expression_descriptor(Value_type::grayscale, "$tiled ? 1.0 : 0.0");
    Parameter_descriptor parameter{};
    parameter.name         = "tiled";
    parameter.kind         = Parameter_kind::bool_parameter;
    parameter.default_bool = false;
    descriptor.parameters.push_back(parameter);

    Compose_node node{descriptor, 1};
    const Composer composer{};
    {
        const Shader_code shader_code = composer.compose(node, 0);
        EXPECT_EQ(shader_code.get_code(), "float o1_0_0_f = false ? 1.0 : 0.0;\n");
    }
    node.set_bool("tiled", true);
    {
        const Shader_code shader_code = composer.compose(node, 0);
        EXPECT_EQ(shader_code.get_code(), "float o1_0_0_f = true ? 1.0 : 0.0;\n");
    }
    // Bool parameters do not create uniforms
    const Shader_code shader_code = composer.compose(node, 0);
    EXPECT_TRUE(shader_code.get_uniforms().empty());
}

TEST(Substitution, size_parameter_substitutes_power_of_two_float_literal)
{
    Node_descriptor descriptor = make_expression_descriptor(Value_type::grayscale, "$resolution");
    Parameter_descriptor parameter{};
    parameter.name                  = "resolution";
    parameter.kind                  = Parameter_kind::size_parameter;
    parameter.default_size_exponent = 4;
    descriptor.parameters.push_back(parameter);

    Compose_node node{descriptor, 1};
    const Composer composer{};
    {
        const Shader_code shader_code = composer.compose(node, 0);
        EXPECT_EQ(shader_code.get_code(), "float o1_0_0_f = 16.0;\n");
    }
    node.set_size_exponent("resolution", 10);
    {
        const Shader_code shader_code = composer.compose(node, 0);
        EXPECT_EQ(shader_code.get_code(), "float o1_0_0_f = 1024.0;\n");
    }
}

TEST(Substitution, enum_parameter_substitutes_code_fragment)
{
    Node_descriptor descriptor = make_expression_descriptor(Value_type::grayscale, "(1.0 $operation 2.0)");
    Parameter_descriptor parameter{};
    parameter.name = "operation";
    parameter.kind = Parameter_kind::enum_parameter;
    parameter.enum_values.push_back(Enum_value{.label = "Add",      .code = "+"});
    parameter.enum_values.push_back(Enum_value{.label = "Multiply", .code = "*"});
    parameter.default_enum_index = 0;
    descriptor.parameters.push_back(parameter);

    Compose_node node{descriptor, 1};
    const Composer composer{};
    {
        const Shader_code shader_code = composer.compose(node, 0);
        EXPECT_EQ(shader_code.get_code(), "float o1_0_0_f = (1.0 + 2.0);\n");
    }
    node.set_enum_index("operation", 1);
    {
        const Shader_code shader_code = composer.compose(node, 0);
        EXPECT_EQ(shader_code.get_code(), "float o1_0_0_f = (1.0 * 2.0);\n");
    }
}

TEST(Substitution, enum_parameter_word_form_glues_as_function_name_prefix)
{
    Node_descriptor descriptor = make_expression_descriptor(Value_type::grayscale, "blend_$mode(1.0, 2.0)");
    Parameter_descriptor parameter{};
    parameter.name = "mode";
    parameter.kind = Parameter_kind::enum_parameter;
    parameter.enum_values.push_back(Enum_value{.label = "Multiply", .code = "multiply"});
    descriptor.parameters.push_back(parameter);

    Compose_node node{descriptor, 1};
    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0);
    EXPECT_EQ(shader_code.get_code(), "float o1_0_0_f = blend_multiply(1.0, 2.0);\n");
}

TEST(Substitution, parenthesized_form_glues_against_adjacent_identifier_text)
{
    Node_descriptor descriptor = make_expression_descriptor(Value_type::grayscale, "(uv).$(axis)");
    Parameter_descriptor parameter{};
    parameter.name = "axis";
    parameter.kind = Parameter_kind::enum_parameter;
    parameter.enum_values.push_back(Enum_value{.label = "X", .code = "x"});
    parameter.enum_values.push_back(Enum_value{.label = "Y", .code = "y"});
    parameter.default_enum_index = 1;
    descriptor.parameters.push_back(parameter);

    Compose_node node{descriptor, 1};
    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0);
    EXPECT_EQ(shader_code.get_code(), "float o1_0_0_f = (uv).y;\n");
}

TEST(Substitution, longest_identifier_match_wins_over_shorter_builtin)
{
    // "$uv_scale" must resolve the parameter "uv_scale", never "$uv"
    // followed by the literal text "_scale".
    Node_descriptor descriptor = make_expression_descriptor(Value_type::grayscale, "($uv).x * $uv_scale");
    Parameter_descriptor parameter{};
    parameter.name          = "uv_scale";
    parameter.kind          = Parameter_kind::float_parameter;
    parameter.default_float = 2.0f;
    descriptor.parameters.push_back(parameter);

    Compose_node node{descriptor, 1};
    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0);
    EXPECT_EQ(shader_code.get_code(), "float o1_0_0_f = (uv).x * p_o1_uv_scale;\n");
}

TEST(Substitution, uv_builtin_resolves_to_current_coordinate_expression)
{
    const Node_descriptor descriptor = make_expression_descriptor(Value_type::grayscale, "($uv).x");
    const Compose_node node{descriptor, 1};
    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0);
    EXPECT_EQ(shader_code.get_code(), "float o1_0_0_f = (uv).x;\n");
}

TEST(Substitution, name_builtin_resolves_to_node_id)
{
    const Node_descriptor descriptor = make_expression_descriptor(Value_type::grayscale, "$name");
    const Compose_node node{descriptor, 42};
    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0);
    EXPECT_EQ(shader_code.get_code(), "float o42_0_0_f = o42;\n");
}

TEST(Substitution, time_builtin_resolves_to_elapsed_time)
{
    const Node_descriptor descriptor = make_expression_descriptor(Value_type::grayscale, "$time");
    const Compose_node node{descriptor, 1};
    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0);
    EXPECT_EQ(shader_code.get_code(), "float o1_0_0_f = elapsed_time;\n");
}

TEST(Substitution, seed_builtin_creates_per_node_seed_uniform)
{
    const Node_descriptor descriptor = make_expression_descriptor(Value_type::grayscale, "$seed");
    Compose_node node{descriptor, 7};
    node.set_seed(0.25f);
    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0);
    EXPECT_EQ(shader_code.get_code(), "float o7_0_0_f = p_o7_seed;\n");
    ASSERT_EQ(shader_code.get_uniforms().size(), 1u);
    EXPECT_EQ(shader_code.get_uniforms()[0].name, "p_o7_seed");
    EXPECT_EQ(shader_code.get_uniforms()[0].kind, Uniform_kind::float_uniform);
    EXPECT_FLOAT_EQ(shader_code.get_uniforms()[0].value[0], 0.25f);
}

TEST(Substitution, unknown_variable_substitutes_error_marker)
{
    const Node_descriptor descriptor = make_expression_descriptor(Value_type::grayscale, "$nosuch");
    const Compose_node node{descriptor, 1};
    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0);
    EXPECT_NE(shader_code.get_code().find("(error: nosuch not found)"), std::string::npos);
}

TEST(Substitution, rnd_rewrites_to_param_rnd_with_seed_offset)
{
    const Node_descriptor descriptor = make_expression_descriptor(Value_type::grayscale, "$rnd(0.0, 1.0)");
    const Compose_node node{descriptor, 1};
    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0);
    EXPECT_NE(shader_code.get_code().find("param_rnd(0.0, 1.0, p_o1_seed + "), std::string::npos);
    // $rnd usage implies seed usage -> seed uniform registered
    ASSERT_EQ(shader_code.get_uniforms().size(), 1u);
    EXPECT_EQ(shader_code.get_uniforms()[0].name, "p_o1_seed");
}

TEST(Substitution, rnd_positional_offsets_differ_per_occurrence)
{
    const Node_descriptor descriptor = make_expression_descriptor(
        Value_type::grayscale,
        "$rnd(0.0, 1.0) + $rnd(0.0, 1.0)"
    );
    const Compose_node node{descriptor, 1};
    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0);
    const std::string& code = shader_code.get_code();

    const std::string pattern{"param_rnd(0.0, 1.0, p_o1_seed + "};
    const std::size_t first = code.find(pattern);
    ASSERT_NE(first, std::string::npos);
    const std::size_t second = code.find(pattern, first + pattern.length());
    ASSERT_NE(second, std::string::npos);

    const std::size_t first_offset_begin  = first + pattern.length();
    const std::size_t second_offset_begin = second + pattern.length();
    const std::string first_offset  = code.substr(first_offset_begin,  code.find(')', first_offset_begin)  - first_offset_begin);
    const std::string second_offset = code.substr(second_offset_begin, code.find(')', second_offset_begin) - second_offset_begin);
    EXPECT_NE(first_offset, second_offset);
}

TEST(Substitution, rndi_rewrites_to_param_rndi_with_seed_offset)
{
    const Node_descriptor descriptor = make_expression_descriptor(Value_type::grayscale, "$rndi(0.0, 4.0)");
    const Compose_node node{descriptor, 1};
    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0);
    EXPECT_NE(shader_code.get_code().find("param_rndi(0.0, 4.0, p_o1_seed + "), std::string::npos);
    // $rndi usage implies seed usage -> seed uniform registered
    ASSERT_EQ(shader_code.get_uniforms().size(), 1u);
    EXPECT_EQ(shader_code.get_uniforms()[0].name, "p_o1_seed");
}

TEST(Substitution, nested_rnd_inside_argument_is_also_rewritten)
{
    const Node_descriptor descriptor = make_expression_descriptor(
        Value_type::grayscale,
        "$rnd($rnd(0.0, 1.0), 2.0)"
    );
    const Compose_node node{descriptor, 1};
    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0);
    const std::string& code = shader_code.get_code();

    // Both the outer and the nested $rnd are rewritten; none remain.
    std::size_t param_rnd_count = 0;
    std::size_t position        = 0;
    while (true) {
        position = code.find("param_rnd(", position);
        if (position == std::string::npos) {
            break;
        }
        ++param_rnd_count;
        position += 1;
    }
    EXPECT_EQ(param_rnd_count, 2u);
    EXPECT_EQ(code.find("$rnd("), std::string::npos);
}

TEST(Substitution, code_and_two_outputs_get_three_distinct_rnd_offsets)
{
    // A source with an inline code stanza and two outputs, each starting with
    // $rnd at the same character position. The per-template offset base (17
    // apart, like Material Maker) must decorrelate all three.
    Node_descriptor source_descriptor{};
    source_descriptor.name = "rnd_triple";
    source_descriptor.code = "float $(name_uv)_k = $rnd(0.0, 1.0);\n";
    Output_descriptor o0{};
    o0.type       = Value_type::grayscale;
    o0.expression = "$rnd(0.0, 1.0)";
    source_descriptor.outputs.push_back(o0);
    Output_descriptor o1{};
    o1.type       = Value_type::grayscale;
    o1.expression = "$rnd(0.0, 1.0)";
    source_descriptor.outputs.push_back(o1);

    // Consumer summing both source outputs to force all three stanzas.
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

    // Collect all three "$seed + <offset>" values.
    const std::string pattern{"param_rnd(0.0, 1.0, p_o1_seed + "};
    std::vector<std::string> offsets;
    std::size_t position = 0;
    while (true) {
        position = code.find(pattern, position);
        if (position == std::string::npos) {
            break;
        }
        const std::size_t begin = position + pattern.length();
        const std::size_t end   = code.find(')', begin);
        offsets.push_back(code.substr(begin, end - begin));
        position = begin;
    }
    ASSERT_EQ(offsets.size(), 3u);
    EXPECT_NE(offsets[0], offsets[1]);
    EXPECT_NE(offsets[0], offsets[2]);
    EXPECT_NE(offsets[1], offsets[2]);
}

TEST(Substitution, trailing_glue_parenthesized_form_for_user_parameter)
{
    // "$(color)suffix" must glue the parameter substitution against the
    // trailing identifier text - the parenthesized form's parsing must not
    // consume "suffix".
    Node_descriptor descriptor = make_expression_descriptor(Value_type::grayscale, "$(prefix)_tail");
    Parameter_descriptor parameter{};
    parameter.name = "prefix";
    parameter.kind = Parameter_kind::enum_parameter;
    parameter.enum_values.push_back(Enum_value{.label = "Foo", .code = "foo"});
    descriptor.parameters.push_back(parameter);

    Compose_node node{descriptor, 1};
    const Composer composer{};
    const Shader_code shader_code = composer.compose(node, 0);
    EXPECT_EQ(shader_code.get_code(), "float o1_0_0_f = foo_tail;\n");
}

TEST(Substitution, uniform_accessor_prefix_applies_to_seed_and_rnd_expansion)
{
    // In UBO mode with an accessor prefix, both the plain $seed reference and
    // the $seed injected by $rnd expansion must be prefixed.
    const Node_descriptor descriptor = make_expression_descriptor(
        Value_type::grayscale,
        "$seed + $rnd(0.0, 1.0)"
    );
    Compose_node node{descriptor, 7};
    node.set_seed(0.25f);

    Compose_options options{};
    options.uniform_accessor_prefix  = "params.";
    options.uniform_declaration_mode = Uniform_declaration_mode::none;
    const Composer composer{options};
    const Shader_code shader_code = composer.compose(node, 0);
    const std::string& code = shader_code.get_code();

    EXPECT_NE(code.find("params.p_o7_seed + param_rnd(0.0, 1.0, params.p_o7_seed + "), std::string::npos);
    // The registered uniform name stays unprefixed (it is the UBO member name).
    ASSERT_EQ(shader_code.get_uniforms().size(), 1u);
    EXPECT_EQ(shader_code.get_uniforms()[0].name, "p_o7_seed");
}

} // namespace erhe::texgen::test
