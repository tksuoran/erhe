#include "gpu_test_fixture.hpp"

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/fragment_output.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"

#include "erhe_texgen/compose_node.hpp"
#include "erhe_texgen/composer.hpp"
#include "erhe_texgen/node_descriptor.hpp"
#include "erhe_texgen/shader_code.hpp"
#include "erhe_texgen/value_type.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// -----------------------------------------------------------------------------
// GLSL dialect / fixture-fit decisions (Phase 2 of doc/texture-graph-plan.md)
// -----------------------------------------------------------------------------
// The headless test device is Vulkan (glslang). Vulkan GLSL rejects a plain
// non-opaque global uniform, and it also rejects a default initializer on a
// uniform, so erhe::texgen's DEFAULT Uniform_declaration_mode::plain_uniforms
// ("uniform float x = 1.0;") does NOT compile here. Two consequences drive the
// authoring of the tests below:
//
//  1. The render tests that assert on pixels are authored with NO free
//     float/color parameters -- the numeric constants live directly in the
//     descriptor output/expression templates (inline literals). With zero
//     uniforms, assemble_fragment() emits no "uniform" declarations at all, so
//     plain_uniforms vs none is moot and the source is Vulkan-clean.
//
//  2. The parameter path is proven separately (Texgen_render_ubo_parameter)
//     via a real std140 UBO: compose with Uniform_declaration_mode::none +
//     uniform_accessor_prefix "params.", declare the uniforms as a
//     Shader_resource uniform block named "params" (erhe emits it as
//     "uniform params_block { ... } params;", referenced "params.<field>"),
//     bind it with set_buffer, and re-upload a different value WITHOUT
//     recompiling. This is the recompile-free-parameter contract, on the GPU.
//     A pure-string sibling test (Texgen_recompile_free_source) additionally
//     asserts that two graphs differing only in a parameter value assemble to
//     byte-identical source in none mode.
//
// assemble_fragment() emits a complete "void main()" that (a) reads the
// coordinate from uv_source_expression and (b) writes the vec4 result to
// output_variable_name. The fixture's draw_fullscreen_triangle only injects an
// *expression* into "out_color = FRAG_COLOR;" and exposes no uv varying, so it
// cannot host a full main(). This file therefore uses its own small render
// helper (render_fragment) that mirrors draw_fullscreen_triangle but takes a
// complete fragment body and adds a v_uv varying. Compose_options are matched
// to that helper: function_name "main", output_variable_name "out_color",
// uv_source_expression "v_uv".
//
// UV orientation (determined empirically from Texgen_render_uv_gradient and
// documented here): the vertex shader sets v_uv = position*0.5 + 0.5 over the
// fullscreen triangle. Under Vulkan (framebuffer origin top-left, NDC +y down),
// read_texture_rgba8 row 0 is the top of the image and corresponds to
// v_uv.y ~= 0; the last row is the bottom and v_uv.y ~= 1. v_uv.x ~= 0 at
// column 0 (left) and ~= 1 at the last column (right). So for the gradient
// vec3(uv.x, uv.y, 0): red increases left->right across columns, green
// increases top->bottom across rows.
// -----------------------------------------------------------------------------

namespace erhe::graphics::test {

namespace {

// Fullscreen-triangle vertex shader that also emits a [0,1] uv varying, matching
// texgen's uv_source_expression "v_uv" (same pattern as test_texture_sample).
constexpr const char* c_vertex_source = R"glsl(
layout(location = 0) out vec2 v_uv;
void main()
{
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
    v_uv = (positions[gl_VertexID] * 0.5) + vec2(0.5);
}
)glsl";

// The assembled fragment body reads the "v_uv" varying (uv_source_expression),
// but erhe injects only fragment OUTPUTS, not input varyings, so the input
// declaration must be prepended to the assembled body.
constexpr const char* c_fragment_varying = "layout(location = 0) in vec2 v_uv;\n";

// Compose_options matched to render_fragment's shader contract.
[[nodiscard]] auto make_render_options() -> erhe::texgen::Compose_options
{
    erhe::texgen::Compose_options options{};
    options.function_name        = "main";
    options.output_variable_name = "out_color";
    options.uv_source_expression = "v_uv";
    return options;
}

// ------------------------- test-local node descriptors -----------------------

// Generator: constant color baked as an inline literal (no free uniform).
[[nodiscard]] auto make_inline_constant_descriptor(const char* expression) -> erhe::texgen::Node_descriptor
{
    erhe::texgen::Node_descriptor descriptor{};
    descriptor.name  = "inline_constant";
    descriptor.label = "Inline Constant";
    erhe::texgen::Output_descriptor output{};
    output.type       = erhe::texgen::Value_type::rgba;
    output.expression = expression;
    descriptor.outputs.push_back(output);
    return descriptor;
}

// Generator: grayscale value baked inline (f output, no free uniform).
[[nodiscard]] auto make_inline_grayscale_descriptor(const char* expression) -> erhe::texgen::Node_descriptor
{
    erhe::texgen::Node_descriptor descriptor{};
    descriptor.name  = "inline_grayscale";
    descriptor.label = "Inline Grayscale";
    erhe::texgen::Output_descriptor output{};
    output.type       = erhe::texgen::Value_type::grayscale;
    output.expression = expression;
    descriptor.outputs.push_back(output);
    return descriptor;
}

// Generator: rgb value baked inline (rgb output, no free uniform).
[[nodiscard]] auto make_inline_rgb_descriptor(const char* expression) -> erhe::texgen::Node_descriptor
{
    erhe::texgen::Node_descriptor descriptor{};
    descriptor.name  = "inline_rgb";
    descriptor.label = "Inline Rgb";
    erhe::texgen::Output_descriptor output{};
    output.type       = erhe::texgen::Value_type::rgb;
    output.expression = expression;
    descriptor.outputs.push_back(output);
    return descriptor;
}

// Passthrough: one rgba input, rgba output. Feeding a non-rgba source exercises
// texgen's convert() (f->rgba / rgb->rgba) at the input.
[[nodiscard]] auto make_passthrough_rgba_descriptor() -> erhe::texgen::Node_descriptor
{
    erhe::texgen::Node_descriptor descriptor{};
    descriptor.name  = "passthrough_rgba";
    descriptor.label = "Passthrough Rgba";
    erhe::texgen::Input_descriptor in{};
    in.name               = "in";
    in.type               = erhe::texgen::Value_type::rgba;
    in.default_expression = "vec4(0.0)";
    descriptor.inputs.push_back(in);
    erhe::texgen::Output_descriptor output{};
    output.type       = erhe::texgen::Value_type::rgba;
    output.expression = "$in($uv)";
    descriptor.outputs.push_back(output);
    return descriptor;
}

// UV gradient generator: rgb output built from the sampling coordinates.
[[nodiscard]] auto make_uv_gradient_descriptor() -> erhe::texgen::Node_descriptor
{
    erhe::texgen::Node_descriptor descriptor{};
    descriptor.name  = "uv_gradient";
    descriptor.label = "UV Gradient";
    erhe::texgen::Output_descriptor output{};
    output.type       = erhe::texgen::Value_type::rgb;
    output.expression = "vec3(($uv).x, ($uv).y, 0.0)";
    descriptor.outputs.push_back(output);
    return descriptor;
}

// Blend multiply of two rgba inputs (no amount uniform -> pure component-wise
// product). Exercises multi-input composition.
[[nodiscard]] auto make_blend_multiply_descriptor() -> erhe::texgen::Node_descriptor
{
    erhe::texgen::Node_descriptor descriptor{};
    descriptor.name  = "blend_multiply";
    descriptor.label = "Blend Multiply";
    erhe::texgen::Input_descriptor in1{};
    in1.name               = "in1";
    in1.type               = erhe::texgen::Value_type::rgba;
    in1.default_expression = "vec4(1.0)";
    descriptor.inputs.push_back(in1);
    erhe::texgen::Input_descriptor in2{};
    in2.name               = "in2";
    in2.type               = erhe::texgen::Value_type::rgba;
    in2.default_expression = "vec4(1.0)";
    descriptor.inputs.push_back(in2);
    erhe::texgen::Output_descriptor output{};
    output.type       = erhe::texgen::Value_type::rgba;
    output.expression = "$in1($uv) * $in2($uv)";
    descriptor.outputs.push_back(output);
    return descriptor;
}

// Noise generator using the common library (rand) through a global helper.
// A literal seed (no $seed) keeps it free of uniforms so it renders on Vulkan.
[[nodiscard]] auto make_common_library_noise_descriptor() -> erhe::texgen::Node_descriptor
{
    erhe::texgen::Node_descriptor descriptor{};
    descriptor.name   = "common_library_noise";
    descriptor.label  = "Common Library Noise";
    descriptor.global =
        "float test_noise(vec2 uv) {\n"
        "    return rand(uv * 8.0);\n"
        "}\n";
    erhe::texgen::Output_descriptor output{};
    output.type       = erhe::texgen::Value_type::grayscale;
    output.expression = "test_noise($uv)";
    descriptor.outputs.push_back(output);
    return descriptor;
}

// Constant color driven by a color PARAMETER (a free vec4 uniform). Only used
// by the UBO parameter test (composed in Uniform_declaration_mode::none).
[[nodiscard]] auto make_color_parameter_descriptor() -> erhe::texgen::Node_descriptor
{
    erhe::texgen::Node_descriptor descriptor{};
    descriptor.name  = "constant_color";
    descriptor.label = "Constant Color";
    erhe::texgen::Parameter_descriptor color{};
    color.name          = "color";
    color.label         = "Color";
    color.kind          = erhe::texgen::Parameter_kind::color_parameter;
    color.default_color = {1.0f, 1.0f, 1.0f, 1.0f};
    descriptor.parameters.push_back(color);
    erhe::texgen::Output_descriptor output{};
    output.type       = erhe::texgen::Value_type::rgba;
    output.expression = "$color";
    descriptor.outputs.push_back(output);
    return descriptor;
}

} // anonymous namespace

// Renders a complete texgen-assembled fragment body over a fullscreen triangle
// into a fresh RGBA8 target and returns the read-back pixels. Mirrors
// Gpu_test::draw_fullscreen_triangle but takes a full main() (not an expression)
// and supplies the v_uv varying that the assembled body reads.
class Texgen_render_test : public Gpu_test
{
protected:
    [[nodiscard]] auto render_fragment(const std::string& fragment_source, int width, int height)
        -> std::vector<uint8_t>
    {
        const std::string full_fragment_source = std::string{c_fragment_varying} + fragment_source;

        const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(width, height);

        const erhe::graphics::Bind_group_layout empty_layout{
            device(),
            erhe::graphics::Bind_group_layout_create_info{
                .bindings          = {},
                .debug_label       = erhe::utility::Debug_label{"texgen empty layout"},
                .uses_texture_heap = false
            }
        };
        const erhe::graphics::Fragment_outputs fragment_outputs{
            { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
        };

        erhe::graphics::Shader_stages_create_info shader_create_info{
            .name             = "texgen_fragment",
            .fragment_outputs = &fragment_outputs,
            .no_vertex_input  = true,
            .shaders = {
                { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source} },
                { erhe::graphics::Shader_type::fragment_shader, std::string_view{full_fragment_source} }
            },
            .bind_group_layout = &empty_layout
        };
        erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
        if (!prototype.is_valid()) {
            ADD_FAILURE() << "texgen fragment failed to compile/link:\n" << full_fragment_source;
            return {};
        }
        erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

        erhe::graphics::Render_pass_descriptor descriptor{};
        descriptor.color_attachments[0].texture       = color_target.get();
        descriptor.color_attachments[0].clear_value   = std::array<double, 4>{0.0, 0.0, 0.0, 1.0};
        descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
        descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
        descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.render_target_width  = width;
        descriptor.render_target_height = height;
        descriptor.debug_label = erhe::utility::Debug_label{"texgen render"};

        erhe::graphics::Render_pipeline_create_info pipeline_create_info;
        pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
        pipeline_create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
        pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
        pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
        pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
        pipeline_create_info.base.bind_group_layout                 = &empty_layout;
        pipeline_create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
        pipeline_create_info.shader_stages                          = &shader_stages;
        pipeline_create_info.vertex_input                           = nullptr;
        pipeline_create_info.set_format_from_render_pass(descriptor);
        const erhe::graphics::Render_pipeline pipeline{device(), pipeline_create_info};
        if (!pipeline.is_valid()) {
            ADD_FAILURE() << "texgen render pipeline is not valid";
            return {};
        }

        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Render_pass            render_pass{device(), descriptor};
                erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
                const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                encoder.set_viewport_rect(0, 0, width, height);
                encoder.set_scissor_rect (0, 0, width, height);
                encoder.set_bind_group_layout(&empty_layout);
                encoder.set_render_pipeline(pipeline);
                encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
            }
        );

        return read_texture_rgba8(*color_target);
    }

    // Compose the sink node's output 0 with render options and assemble the
    // fragment body.
    [[nodiscard]] auto assemble(const erhe::texgen::Compose_node& sink) -> std::string
    {
        const erhe::texgen::Composer   composer{make_render_options()};
        const erhe::texgen::Shader_code shader_code = composer.compose(sink, 0);
        return composer.assemble_fragment(shader_code);
    }
};

namespace {

// Fetch the pixel at (x, y) as 4 ints from a tightly-packed RGBA8 buffer.
[[nodiscard]] auto pixel_at(const std::vector<uint8_t>& pixels, int width, int x, int y) -> std::array<int, 4>
{
    const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
    return {
        static_cast<int>(pixels[index + 0]),
        static_cast<int>(pixels[index + 1]),
        static_cast<int>(pixels[index + 2]),
        static_cast<int>(pixels[index + 3])
    };
}

// Expect every texel to equal r,g,b,a within tolerance.
void expect_uniform_color(const std::vector<uint8_t>& pixels, int width, int height, int r, int g, int b, int a, int tolerance)
{
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    int bad = 0;
    for (std::size_t i = 0; (i + 3) < pixels.size(); i += 4) {
        const bool ok =
            (std::abs(static_cast<int>(pixels[i + 0]) - r) <= tolerance) &&
            (std::abs(static_cast<int>(pixels[i + 1]) - g) <= tolerance) &&
            (std::abs(static_cast<int>(pixels[i + 2]) - b) <= tolerance) &&
            (std::abs(static_cast<int>(pixels[i + 3]) - a) <= tolerance);
        if (!ok) {
            ++bad;
        }
    }
    EXPECT_EQ(bad, 0) << bad << " texels did not match expected color {"
                      << r << "," << g << "," << b << "," << a << "}";
}

} // anonymous namespace

// Constant color node -> every pixel equals the constant (inline literal, no
// uniforms). 0.25/0.5/0.75 -> 64/128/191.
TEST_F(Texgen_render_test, constant_color)
{
    const erhe::texgen::Node_descriptor descriptor = make_inline_constant_descriptor("vec4(0.25, 0.5, 0.75, 1.0)");
    const erhe::texgen::Compose_node    node{descriptor, 1};
    const std::string fragment = assemble(node);

    const std::vector<uint8_t> pixels = render_fragment(fragment, 8, 8);
    expect_uniform_color(pixels, 8, 8, 64, 128, 191, 255, 2);
}

// Grayscale (f) node with expression "0.5", wrapped by the sink to
// vec4(vec3(0.5), 1.0) -> mid-gray.
TEST_F(Texgen_render_test, grayscale_sink_wrap)
{
    const erhe::texgen::Node_descriptor descriptor = make_inline_grayscale_descriptor("0.5");
    const erhe::texgen::Compose_node    node{descriptor, 1};
    const std::string fragment = assemble(node);

    const std::vector<uint8_t> pixels = render_fragment(fragment, 8, 8);
    expect_uniform_color(pixels, 8, 8, 128, 128, 128, 255, 2);
}

// f -> rgba conversion at a connected input: grayscale 0.5 into a rgba
// passthrough -> vec4(vec3(0.5), 1.0) -> gray. Exercises texgen convert().
TEST_F(Texgen_render_test, convert_f_to_rgba)
{
    const erhe::texgen::Node_descriptor source_descriptor = make_inline_grayscale_descriptor("0.5");
    const erhe::texgen::Node_descriptor sink_descriptor   = make_passthrough_rgba_descriptor();
    erhe::texgen::Compose_node source{source_descriptor, 1};
    erhe::texgen::Compose_node sink  {sink_descriptor,   2};
    sink.set_input(0, &source, 0);
    const std::string fragment = assemble(sink);

    const std::vector<uint8_t> pixels = render_fragment(fragment, 8, 8);
    expect_uniform_color(pixels, 8, 8, 128, 128, 128, 255, 2);
}

// rgb -> rgba conversion at a connected input: rgb(0.25,0.5,0.75) into a rgba
// passthrough -> vec4(rgb, 1.0). Exercises texgen convert().
TEST_F(Texgen_render_test, convert_rgb_to_rgba)
{
    const erhe::texgen::Node_descriptor source_descriptor = make_inline_rgb_descriptor("vec3(0.25, 0.5, 0.75)");
    const erhe::texgen::Node_descriptor sink_descriptor   = make_passthrough_rgba_descriptor();
    erhe::texgen::Compose_node source{source_descriptor, 1};
    erhe::texgen::Compose_node sink  {sink_descriptor,   2};
    sink.set_input(0, &source, 0);
    const std::string fragment = assemble(sink);

    const std::vector<uint8_t> pixels = render_fragment(fragment, 8, 8);
    expect_uniform_color(pixels, 8, 8, 64, 128, 191, 255, 2);
}

// UV gradient -> corner pixels ordered per the documented Vulkan orientation:
// red increases left->right (columns), green increases top->bottom (rows).
TEST_F(Texgen_render_test, uv_gradient)
{
    const erhe::texgen::Node_descriptor descriptor = make_uv_gradient_descriptor();
    const erhe::texgen::Compose_node    node{descriptor, 1};
    const std::string fragment = assemble(node);

    constexpr int size = 8;
    const std::vector<uint8_t> pixels = render_fragment(fragment, size, size);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4u);

    const std::array<int, 4> top_left     = pixel_at(pixels, size, 0,        0);
    const std::array<int, 4> top_right    = pixel_at(pixels, size, size - 1, 0);
    const std::array<int, 4> bottom_left  = pixel_at(pixels, size, 0,        size - 1);
    const std::array<int, 4> bottom_right = pixel_at(pixels, size, size - 1, size - 1);

    // Red = uv.x: low on the left, high on the right, ~constant down a column.
    EXPECT_LT(top_left[0],     40);
    EXPECT_GT(top_right[0],   200);
    EXPECT_LT(bottom_left[0],  40);
    EXPECT_GT(bottom_right[0], 200);

    // Green = uv.y: low at the top, high at the bottom, ~constant across a row.
    EXPECT_LT(top_left[1],     40);
    EXPECT_LT(top_right[1],    40);
    EXPECT_GT(bottom_left[1], 200);
    EXPECT_GT(bottom_right[1],200);

    // Blue is always 0.
    EXPECT_LT(top_left[2],  4);
    EXPECT_LT(bottom_right[2], 4);
}

// blend(multiply) of two inline constants -> component-wise product.
// in1 = (0.5,0.5,0.5,1), in2 = (0.5,1.0,0.25,1) -> (0.25,0.5,0.125,1).
TEST_F(Texgen_render_test, blend_multiply)
{
    const erhe::texgen::Node_descriptor in1_descriptor   = make_inline_constant_descriptor("vec4(0.5, 0.5, 0.5, 1.0)");
    const erhe::texgen::Node_descriptor in2_descriptor   = make_inline_constant_descriptor("vec4(0.5, 1.0, 0.25, 1.0)");
    const erhe::texgen::Node_descriptor blend_descriptor = make_blend_multiply_descriptor();
    erhe::texgen::Compose_node in1  {in1_descriptor,   1};
    erhe::texgen::Compose_node in2  {in2_descriptor,   2};
    erhe::texgen::Compose_node blend{blend_descriptor, 3};
    blend.set_input(0, &in1, 0);
    blend.set_input(1, &in2, 0);
    const std::string fragment = assemble(blend);

    const std::vector<uint8_t> pixels = render_fragment(fragment, 8, 8);
    // 0.25 -> 64, 0.5 -> 128, 0.125 -> 32, 1.0 -> 255.
    expect_uniform_color(pixels, 8, 8, 64, 128, 32, 255, 2);
}

// A graph using the common library (rand) compiles under Vulkan/glslang and
// renders finite, deterministic values in [0,1] -- proving common_library.glsl
// is valid GLSL under the Vulkan path (a compile failure or a validation
// message would fail the case).
TEST_F(Texgen_render_test, common_library_compiles_and_is_deterministic)
{
    const erhe::texgen::Node_descriptor descriptor = make_common_library_noise_descriptor();
    const erhe::texgen::Compose_node    node{descriptor, 1};
    const std::string fragment = assemble(node);

    // The composed source must actually pull in the common library.
    EXPECT_NE(fragment.find("float rand(vec2 x)"), std::string::npos)
        << "common library was not injected into the composed source";

    constexpr int size = 16;
    const std::vector<uint8_t> pixels_a = render_fragment(fragment, size, size);
    const std::vector<uint8_t> pixels_b = render_fragment(fragment, size, size);
    ASSERT_EQ(pixels_a.size(), static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4u);

    // Deterministic: identical source + identical inputs -> identical output.
    EXPECT_EQ(pixels_a, pixels_b) << "common-library render was not deterministic across two identical renders";

    // Grayscale noise: r==g==b, alpha 255, values are finite bytes in [0,255]
    // (rand returns fract(...) in [0,1)). Also require some spatial variation
    // so we know rand actually ran rather than returning a constant.
    int min_v = 255;
    int max_v = 0;
    for (std::size_t i = 0; (i + 3) < pixels_a.size(); i += 4) {
        const int r = static_cast<int>(pixels_a[i + 0]);
        const int g = static_cast<int>(pixels_a[i + 1]);
        const int b = static_cast<int>(pixels_a[i + 2]);
        const int a = static_cast<int>(pixels_a[i + 3]);
        EXPECT_EQ(g, r);
        EXPECT_EQ(b, r);
        EXPECT_EQ(a, 255);
        min_v = std::min(min_v, r);
        max_v = std::max(max_v, r);
    }
    EXPECT_GT(max_v, min_v) << "expected spatial variation from rand(), got a constant";
}

// Real-GPU recompile-free parameter proof: compose a color-parameter node in
// Uniform_declaration_mode::none, declare the uniforms as a std140 UBO, and
// render twice with two different uploaded color values using the SAME compiled
// shader/pipeline.
TEST_F(Texgen_render_test, ubo_parameter)
{
    const erhe::texgen::Node_descriptor node_descriptor = make_color_parameter_descriptor();
    erhe::texgen::Compose_node node{node_descriptor, 1};

    erhe::texgen::Compose_options options = make_render_options();
    options.uniform_declaration_mode = erhe::texgen::Uniform_declaration_mode::none;
    options.uniform_accessor_prefix  = "params.";
    const erhe::texgen::Composer    composer{options};
    const erhe::texgen::Shader_code shader_code = composer.compose(node, 0);
    const std::string               fragment    = std::string{c_fragment_varying} + composer.assemble_fragment(shader_code);

    const std::vector<erhe::texgen::Uniform>& uniforms = shader_code.get_uniforms();
    ASSERT_EQ(uniforms.size(), 1u) << "expected exactly one (color) uniform";
    ASSERT_EQ(uniforms[0].kind, erhe::texgen::Uniform_kind::vec4_uniform);

    // In none mode the value must NOT appear in the source (that is the whole
    // point: value edits do not touch the shader text).
    EXPECT_EQ(fragment.find("uniform float"), std::string::npos);
    EXPECT_EQ(fragment.find("uniform vec4"),  std::string::npos);
    EXPECT_NE(fragment.find("params."),       std::string::npos)
        << "expected the UBO accessor prefix in the generated code";

    // Build the UBO block "params" from the composed uniform list.
    erhe::graphics::Shader_resource ubo_block{
        device(),
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "params",
            .binding_point = 0,
            .type          = erhe::graphics::Shader_resource::Type::uniform_block
        }
    };
    std::vector<std::size_t> member_offsets;
    for (const erhe::texgen::Uniform& uniform : uniforms) {
        erhe::graphics::Shader_resource* member =
            (uniform.kind == erhe::texgen::Uniform_kind::vec4_uniform)
                ? ubo_block.add_vec4 (uniform.name)
                : ubo_block.add_float(uniform.name);
        member_offsets.push_back(member->get_offset_in_parent());
    }
    const std::size_t ubo_bytes = ubo_block.get_size_bytes(erhe::graphics::Shader_resource::Layout::std140);
    ASSERT_GE(ubo_bytes, 16u);

    const erhe::graphics::Bind_group_layout layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = { { 0u, erhe::graphics::Binding_type::uniform_buffer } },
            .debug_label       = erhe::utility::Debug_label{"texgen ubo layout"},
            .uses_texture_heap = false
        }
    };
    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name              = "texgen_ubo",
        .interface_blocks  = { &ubo_block },
        .fragment_outputs  = &fragment_outputs,
        .no_vertex_input   = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source} },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{fragment} }
        },
        .bind_group_layout = &layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "texgen UBO shader failed to compile/link:\n" << fragment;
    erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

    constexpr int width  = 8;
    constexpr int height = 8;
    const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(width, height);

    erhe::graphics::Render_pass_descriptor descriptor{};
    descriptor.color_attachments[0].texture       = color_target.get();
    descriptor.color_attachments[0].clear_value   = std::array<double, 4>{0.0, 0.0, 0.0, 1.0};
    descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.render_target_width  = width;
    descriptor.render_target_height = height;
    descriptor.debug_label = erhe::utility::Debug_label{"texgen ubo render"};

    erhe::graphics::Render_pipeline_create_info pipeline_create_info;
    pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
    pipeline_create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
    pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
    pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
    pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
    pipeline_create_info.base.bind_group_layout                 = &layout;
    pipeline_create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
    pipeline_create_info.shader_stages                          = &shader_stages;
    pipeline_create_info.vertex_input                           = nullptr;
    pipeline_create_info.set_format_from_render_pass(descriptor);
    const erhe::graphics::Render_pipeline pipeline{device(), pipeline_create_info};
    ASSERT_TRUE(pipeline.is_valid()) << "texgen UBO pipeline is not valid";

    const std::shared_ptr<erhe::graphics::Buffer> ubo =
        make_host_buffer(ubo_bytes, erhe::graphics::Buffer_usage::uniform, "texgen ubo");

    // Render once with the given color, WITHOUT recompiling, and read back.
    auto render_with_color = [&](const std::array<float, 4>& color) -> std::vector<uint8_t> {
        {
            const std::span<std::byte> mapped = ubo->map_bytes(0, ubo_bytes);
            std::memset(mapped.data(), 0, ubo_bytes);
            std::memcpy(mapped.data() + member_offsets[0], color.data(), sizeof(color));
            ubo->unmap();
        }
        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Render_pass            render_pass{device(), descriptor};
                erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
                const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                encoder.set_viewport_rect(0, 0, width, height);
                encoder.set_scissor_rect (0, 0, width, height);
                encoder.set_bind_group_layout(&layout);
                encoder.set_render_pipeline(pipeline);
                encoder.set_buffer(erhe::graphics::Buffer_target::uniform, ubo.get(), 0, ubo_bytes, 0);
                encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
            }
        );
        return read_texture_rgba8(*color_target);
    };

    const std::vector<uint8_t> pixels_a = render_with_color({0.25f, 0.5f, 0.75f, 1.0f});
    expect_uniform_color(pixels_a, width, height, 64, 128, 191, 255, 2);

    const std::vector<uint8_t> pixels_b = render_with_color({0.9f, 0.1f, 0.4f, 1.0f});
    // 0.9 -> 229, 0.1 -> 26, 0.4 -> 102.
    expect_uniform_color(pixels_b, width, height, 229, 26, 102, 255, 2);
}

// Pure-string sibling of the UBO proof: in none mode, two graphs differing only
// in a parameter VALUE assemble to byte-identical source (recompile-free
// contract, provable without a GPU).
TEST_F(Texgen_render_test, recompile_free_source_identity)
{
    const erhe::texgen::Node_descriptor descriptor = make_color_parameter_descriptor();

    erhe::texgen::Compose_options options = make_render_options();
    options.uniform_declaration_mode = erhe::texgen::Uniform_declaration_mode::none;
    options.uniform_accessor_prefix  = "params.";
    const erhe::texgen::Composer composer{options};

    erhe::texgen::Compose_node node_a{descriptor, 1};
    node_a.set_color("color", {0.1f, 0.2f, 0.3f, 1.0f});
    const std::string source_a = composer.assemble_fragment(composer.compose(node_a, 0));

    erhe::texgen::Compose_node node_b{descriptor, 1};
    node_b.set_color("color", {0.9f, 0.8f, 0.7f, 0.5f});
    const std::string source_b = composer.assemble_fragment(composer.compose(node_b, 0));

    EXPECT_EQ(source_a, source_b)
        << "parameter value must not leak into the shader source in none mode";
}

} // namespace erhe::graphics::test
