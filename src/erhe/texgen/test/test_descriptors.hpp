#pragma once

// Minimal Node_descriptor tables used by the erhe_texgen unit tests.
// These are test-local stand-ins for real node definitions (constant color,
// uv gradient, blend multiply, fake noise, ...), small enough that the
// composed shader text can be asserted on directly.

#include "erhe_texgen/node_descriptor.hpp"

namespace erhe::texgen::test {

// Generator: constant color - one color parameter, rgba output.
[[nodiscard]] inline auto make_constant_color_descriptor() -> Node_descriptor
{
    Node_descriptor descriptor{};
    descriptor.name  = "constant_color";
    descriptor.label = "Constant Color";
    Parameter_descriptor color{};
    color.name          = "color";
    color.label         = "Color";
    color.kind          = Parameter_kind::color_parameter;
    color.default_color = {1.0f, 1.0f, 1.0f, 1.0f};
    descriptor.parameters.push_back(color);
    Output_descriptor output{};
    output.type       = Value_type::rgba;
    output.expression = "$color";
    descriptor.outputs.push_back(output);
    return descriptor;
}

// Generator: uv gradient - rgb output built from the sampling coordinates.
[[nodiscard]] inline auto make_uv_gradient_descriptor() -> Node_descriptor
{
    Node_descriptor descriptor{};
    descriptor.name  = "uv_gradient";
    descriptor.label = "UV Gradient";
    Output_descriptor output{};
    output.type       = Value_type::rgb;
    output.expression = "vec3(($uv).x, ($uv).y, 0.0)";
    descriptor.outputs.push_back(output);
    return descriptor;
}

// Generator: grayscale value - one float parameter, f output.
[[nodiscard]] inline auto make_grayscale_value_descriptor() -> Node_descriptor
{
    Node_descriptor descriptor{};
    descriptor.name  = "grayscale_value";
    descriptor.label = "Grayscale Value";
    Parameter_descriptor value{};
    value.name          = "value";
    value.label         = "Value";
    value.kind          = Parameter_kind::float_parameter;
    value.default_float = 0.5f;
    descriptor.parameters.push_back(value);
    Output_descriptor output{};
    output.type       = Value_type::grayscale;
    output.expression = "$value";
    descriptor.outputs.push_back(output);
    return descriptor;
}

// Filter: blend multiply - two rgba inputs, one float amount parameter.
[[nodiscard]] inline auto make_blend_multiply_descriptor() -> Node_descriptor
{
    Node_descriptor descriptor{};
    descriptor.name  = "blend_multiply";
    descriptor.label = "Blend Multiply";
    Input_descriptor in1{};
    in1.name               = "in1";
    in1.type               = Value_type::rgba;
    in1.default_expression = "vec4(1.0)";
    descriptor.inputs.push_back(in1);
    Input_descriptor in2{};
    in2.name               = "in2";
    in2.type               = Value_type::rgba;
    in2.default_expression = "vec4(1.0)";
    descriptor.inputs.push_back(in2);
    Parameter_descriptor amount{};
    amount.name          = "amount";
    amount.label         = "Amount";
    amount.kind          = Parameter_kind::float_parameter;
    amount.default_float = 1.0f;
    descriptor.parameters.push_back(amount);
    Output_descriptor output{};
    output.type       = Value_type::rgba;
    output.expression = "mix($in1($uv), $in1($uv) * $in2($uv), $amount)";
    descriptor.outputs.push_back(output);
    return descriptor;
}

// Generator: fake noise - uses the common library (rand) via a global
// helper and the per-node $seed.
[[nodiscard]] inline auto make_fake_noise_descriptor() -> Node_descriptor
{
    Node_descriptor descriptor{};
    descriptor.name   = "fake_noise";
    descriptor.label  = "Fake Noise";
    descriptor.global =
        "float test_noise(vec2 uv, float seed) {\n"
        "    return rand(uv + vec2(seed));\n"
        "}\n";
    Output_descriptor output{};
    output.type       = Value_type::grayscale;
    output.expression = "test_noise($uv, $seed)";
    descriptor.outputs.push_back(output);
    return descriptor;
}

// Filter: fake blur - one grayscale function-form input sampled at two
// coordinates.
[[nodiscard]] inline auto make_fake_blur_descriptor() -> Node_descriptor
{
    Node_descriptor descriptor{};
    descriptor.name  = "fake_blur";
    descriptor.label = "Fake Blur";
    Input_descriptor source{};
    source.name               = "source";
    source.type               = Value_type::grayscale;
    source.default_expression = "0.0";
    source.function           = true;
    descriptor.inputs.push_back(source);
    Output_descriptor output{};
    output.type       = Value_type::grayscale;
    output.expression = "0.5 * ($source($uv) + $source($uv + vec2(0.01, 0.0)))";
    descriptor.outputs.push_back(output);
    return descriptor;
}

// Filter with an inline code stanza using $(name_uv) locals - one grayscale
// input, offset parameter.
[[nodiscard]] inline auto make_code_offset_descriptor() -> Node_descriptor
{
    Node_descriptor descriptor{};
    descriptor.name  = "code_offset";
    descriptor.label = "Code Offset";
    Input_descriptor source{};
    source.name               = "source";
    source.type               = Value_type::grayscale;
    source.default_expression = "0.0";
    descriptor.inputs.push_back(source);
    Parameter_descriptor offset{};
    offset.name          = "offset";
    offset.label         = "Offset";
    offset.kind          = Parameter_kind::float_parameter;
    offset.default_float = 0.25f;
    descriptor.parameters.push_back(offset);
    descriptor.code = "float $(name_uv)_value = $source($uv) + $offset;\n";
    Output_descriptor output{};
    output.type       = Value_type::grayscale;
    output.expression = "$(name_uv)_value";
    descriptor.outputs.push_back(output);
    return descriptor;
}

// Generator with two grayscale outputs computed from the sampling
// coordinates - drives output_index selection and per-output locals.
[[nodiscard]] inline auto make_two_output_descriptor() -> Node_descriptor
{
    Node_descriptor descriptor{};
    descriptor.name = "two_output";
    Output_descriptor output0{};
    output0.type       = Value_type::grayscale;
    output0.expression = "($uv).x";
    descriptor.outputs.push_back(output0);
    Output_descriptor output1{};
    output1.type       = Value_type::grayscale;
    output1.expression = "($uv).y";
    descriptor.outputs.push_back(output1);
    return descriptor;
}

} // namespace erhe::texgen::test
