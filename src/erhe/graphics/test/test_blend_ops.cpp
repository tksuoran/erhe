#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/fragment_output.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/color_blend_state.hpp"
#include "erhe_graphics/texture.hpp"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Blend equations, ported from the agfx tests DrawBlendOp<Op>.
//
// The scene is the one of test_blend_factors.cpp: a 64x64
// format_8_vec4_unorm target cleared to opaque black (the background
// destination), three destination columns (left / middle / right thirds)
// over the band y in [-0.5, 0.5] NDC drawn with blending off, each with its
// own color and alpha, then one fullscreen quad of the source color S drawn
// with the equation under test. Both factors are one for rgb and alpha, so
// the result is the equation applied to S and D directly:
//   add              S + D
//   subtract         S - D
//   reverse_subtract D - S
//   min              min(S, D)   (factors do not apply)
//   max              max(S, D)   (factors do not apply)
// per channel, alpha included, clamped to [0, 1] by the unorm target. The
// subtract cases clamp by nature: channels where the subtrahend is larger
// read 0, and the channels where it is smaller carry the difference, so a
// swapped operand order shows in every column.
//
// Expectations are computed on the CPU in unorm (the target is not sRGB),
// with the destination colors quantized to 8 bits as they are stored before
// the source is blended over them; texels must match within +-2/255.
//
// The scene is symmetric about the horizontal centre line, so the region
// checks do not depend on the texture origin.

namespace erhe::graphics::test {

namespace {

constexpr int c_size = 64;

constexpr std::uintptr_t c_columns_first = 0;
constexpr std::uintptr_t c_columns_count = 18;
constexpr std::uintptr_t c_source_first  = 18;
constexpr std::uintptr_t c_source_count  = 6;

// Destination colors (distinct color and alpha per column) and the background
// left by the clear.
const glm::vec4 c_left_color      {0.55f, 0.15f, 0.30f, 1.00f};
const glm::vec4 c_middle_color    {0.15f, 0.55f, 0.30f, 0.50f};
const glm::vec4 c_right_color     {0.30f, 0.15f, 0.55f, 0.25f};
const glm::vec4 c_background_color{0.00f, 0.00f, 0.00f, 1.00f};

// Source color: distinct channels, each between the column values so min /
// max pick from both operands.
const glm::vec4 c_source_color{0.20f, 0.30f, 0.40f, 0.25f};

constexpr int c_tolerance = 2;

constexpr const char* c_vertex_source = R"glsl(
layout(location = 0) flat out vec4 v_color;
void main()
{
    vec2 corners[6] = vec2[6](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    int  quad   = gl_VertexID / 6;
    int  corner = gl_VertexID - (quad * 6);
    vec2 lo;
    vec2 hi;
    vec4 color;
    if (quad <= 2) {
        lo = vec2(-1.0 + ((2.0 * float(quad    )) / 3.0), -0.5);
        hi = vec2(-1.0 + ((2.0 * float(quad + 1)) / 3.0),  0.5);
        if (quad == 0) {
            color = LEFT_COLOR;
        } else if (quad == 1) {
            color = MIDDLE_COLOR;
        } else {
            color = RIGHT_COLOR;
        }
    } else {
        lo    = vec2(-1.0, -1.0);
        hi    = vec2( 1.0,  1.0);
        color = SOURCE_COLOR;
    }
    gl_Position = vec4(mix(lo, hi, corners[corner]), 0.0, 1.0);
    v_color     = color;
}
)glsl";

constexpr const char* c_fragment_source = R"glsl(
layout(location = 0) flat in vec4 v_color;
void main()
{
    out_color = v_color;
}
)glsl";

[[nodiscard]] auto glsl_vec4(const glm::vec4& value) -> std::string
{
    return
        "vec4(" + std::to_string(value.r) + ", " + std::to_string(value.g) + ", " +
        std::to_string(value.b) + ", " + std::to_string(value.a) + ")";
}

// The value an 8-bit unorm attachment stores for a color.
[[nodiscard]] auto quantize_unorm8(const glm::vec4& value) -> glm::vec4
{
    return glm::round(glm::clamp(value, glm::vec4{0.0f}, glm::vec4{1.0f}) * 255.0f) / 255.0f;
}

// The blend result for a source S over a destination D, before clamping.
using Blend_function = std::function<glm::vec4(const glm::vec4& s, const glm::vec4& d)>;

// Every texel of the inclusive rectangle must match expected (rgba, unorm)
// within c_tolerance.
void expect_region(
    const std::vector<uint8_t>& pixels,
    const int                   x0,
    const int                   x1,
    const int                   y0,
    const int                   y1,
    const glm::vec4&            expected,
    const char*                 label
)
{
    const glm::vec4          expected_clamped = glm::clamp(expected, glm::vec4{0.0f}, glm::vec4{1.0f});
    const std::array<int, 4> expected_bytes{
        static_cast<int>(std::lround(expected_clamped.r * 255.0f)),
        static_cast<int>(std::lround(expected_clamped.g * 255.0f)),
        static_cast<int>(std::lround(expected_clamped.b * 255.0f)),
        static_cast<int>(std::lround(expected_clamped.a * 255.0f))
    };
    int                mismatches = 0;
    std::array<int, 4> first_seen{};
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const std::size_t index = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(c_size)) + static_cast<std::size_t>(x)) * 4u;
            bool ok = true;
            for (std::size_t c = 0; c < 4u; ++c) {
                const int value = pixels[index + c];
                if ((value < (expected_bytes[c] - c_tolerance)) || (value > (expected_bytes[c] + c_tolerance))) {
                    ok = false;
                }
            }
            if (!ok) {
                if (mismatches == 0) {
                    first_seen = { pixels[index + 0u], pixels[index + 1u], pixels[index + 2u], pixels[index + 3u] };
                }
                ++mismatches;
            }
        }
    }
    EXPECT_EQ(mismatches, 0)
        << label << ": expected rgba {" << expected_bytes[0] << ", " << expected_bytes[1] << ", "
        << expected_bytes[2] << ", " << expected_bytes[3] << "}, " << mismatches
        << " texels differ (first seen {" << first_seen[0] << ", " << first_seen[1] << ", "
        << first_seen[2] << ", " << first_seen[3] << "})";
}

// Equation under test, factors one / one, for rgb and alpha.
[[nodiscard]] auto equation_state(const erhe::graphics::Blend_equation_mode equation) -> erhe::graphics::Color_blend_state
{
    erhe::graphics::Color_blend_state state;
    state.enabled                  = true;
    state.rgb.equation_mode        = equation;
    state.rgb.source_factor        = erhe::graphics::Blending_factor::one;
    state.rgb.destination_factor   = erhe::graphics::Blending_factor::one;
    state.alpha.equation_mode      = equation;
    state.alpha.source_factor      = erhe::graphics::Blending_factor::one;
    state.alpha.destination_factor = erhe::graphics::Blending_factor::one;
    return state;
}

} // namespace

class Blend_op_test : public Gpu_test
{
protected:
    // Clear to opaque black, draw the destination columns with blending off,
    // then the fullscreen source with blend_state, all in one render pass.
    [[nodiscard]] auto render_blend_scene(const erhe::graphics::Color_blend_state& blend_state) -> std::vector<uint8_t>
    {
        const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(c_size, c_size);

        const erhe::graphics::Bind_group_layout empty_layout{
            device(),
            erhe::graphics::Bind_group_layout_create_info{
                .bindings          = {},
                .debug_label       = erhe::utility::Debug_label{"blend op empty layout"},
                .uses_texture_heap = false
            }
        };
        const erhe::graphics::Fragment_outputs fragment_outputs{
            { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
        };

        erhe::graphics::Shader_stages_create_info shader_create_info{
            .name             = "blend_ops",
            .defines          = {
                { "LEFT_COLOR",   glsl_vec4(c_left_color)   },
                { "MIDDLE_COLOR", glsl_vec4(c_middle_color) },
                { "RIGHT_COLOR",  glsl_vec4(c_right_color)  },
                { "SOURCE_COLOR", glsl_vec4(c_source_color) }
            },
            .fragment_outputs = &fragment_outputs,
            .no_vertex_input  = true,
            .shaders = {
                { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source}   },
                { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source} }
            },
            .bind_group_layout = &empty_layout
        };
        erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
        if (!prototype.is_valid()) {
            ADD_FAILURE() << "blend op shader failed to compile/link";
            return {};
        }
        erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

        erhe::graphics::Render_pass_descriptor descriptor{};
        descriptor.color_attachments[0].texture       = color_target.get();
        descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
        descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
        descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
        descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.render_target_width  = c_size;
        descriptor.render_target_height = c_size;
        descriptor.debug_label = erhe::utility::Debug_label{"blend ops"};

        const std::array<const erhe::graphics::Color_blend_state*, 2> blend_states{
            &erhe::graphics::Color_blend_state::color_blend_disabled,
            &blend_state
        };
        std::vector<std::unique_ptr<erhe::graphics::Render_pipeline>> pipelines;
        for (const erhe::graphics::Color_blend_state* state : blend_states) {
            erhe::graphics::Render_pipeline_create_info pipeline_create_info;
            pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
            pipeline_create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
            pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
            pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
            pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
            pipeline_create_info.base.bind_group_layout                 = &empty_layout;
            pipeline_create_info.base.color_blend                       = state;
            pipeline_create_info.shader_stages                          = &shader_stages;
            pipeline_create_info.vertex_input                           = nullptr;
            pipeline_create_info.set_format_from_render_pass(descriptor);
            pipelines.push_back(std::make_unique<erhe::graphics::Render_pipeline>(device(), pipeline_create_info));
            if (!pipelines.back()->is_valid()) {
                ADD_FAILURE() << "blend op pipeline is not valid";
                return {};
            }
        }

        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Render_pass            render_pass{device(), descriptor};
                erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
                const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                encoder.set_viewport_rect(0, 0, c_size, c_size);
                encoder.set_scissor_rect (0, 0, c_size, c_size);
                encoder.set_bind_group_layout(&empty_layout);
                encoder.set_render_pipeline(*pipelines[0]);
                encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, c_columns_first, c_columns_count);
                encoder.set_render_pipeline(*pipelines[1]);
                encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, c_source_first, c_source_count);
            }
        );

        return read_texture_rgba8(*color_target);
    }

    // Render with blend_state and compare each destination region against
    // blend(S, D) for that region's D, then against the golden.
    void check_blend(
        const erhe::graphics::Color_blend_state& blend_state,
        const Blend_function&                    blend,
        const char*                              golden_name
    )
    {
        const std::vector<uint8_t> pixels = render_blend_scene(blend_state);
        ASSERT_EQ(pixels.size(), static_cast<std::size_t>(c_size) * static_cast<std::size_t>(c_size) * 4u);

        const glm::vec4 s = c_source_color;
        // Columns span x in [0, 21.33), [21.33, 42.67), [42.67, 64); the band
        // spans rows [16, 48). The boundary columns 21 and 42 are left out.
        expect_region(pixels,  0, 20, 16, 47, blend(s, quantize_unorm8(c_left_color)),   "left column");
        expect_region(pixels, 22, 41, 16, 47, blend(s, quantize_unorm8(c_middle_color)), "middle column");
        expect_region(pixels, 43, 63, 16, 47, blend(s, quantize_unorm8(c_right_color)),  "right column");
        expect_region(pixels,  0, c_size - 1,  0, 15,         blend(s, c_background_color), "background above the band");
        expect_region(pixels,  0, c_size - 1, 48, c_size - 1, blend(s, c_background_color), "background below the band");

        expect_image_matches_golden(golden_name, c_size, c_size, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{pixels}));
    }

    void check_equation(
        const erhe::graphics::Blend_equation_mode equation,
        const Blend_function&                     blend,
        const char*                               golden_name
    )
    {
        check_blend(equation_state(equation), blend, golden_name);
    }
};

// agfx DrawBlendOpAdd: S + D.
TEST_F(Blend_op_test, op_add)
{
    check_equation(
        erhe::graphics::Blend_equation_mode::func_add,
        [](const glm::vec4& s, const glm::vec4& d) { return s + d; },
        "blend_op_add"
    );
}

// agfx DrawBlendOpSubtract: S - D.
TEST_F(Blend_op_test, op_subtract)
{
    check_equation(
        erhe::graphics::Blend_equation_mode::func_subtract,
        [](const glm::vec4& s, const glm::vec4& d) { return s - d; },
        "blend_op_subtract"
    );
}

// agfx DrawBlendOpReverseSubtract: D - S.
TEST_F(Blend_op_test, op_reverse_subtract)
{
    check_equation(
        erhe::graphics::Blend_equation_mode::func_reverse_subtract,
        [](const glm::vec4& s, const glm::vec4& d) { return d - s; },
        "blend_op_reverse_subtract"
    );
}

// agfx DrawBlendOpMin: min(S, D).
TEST_F(Blend_op_test, op_min)
{
    check_equation(
        erhe::graphics::Blend_equation_mode::min_,
        [](const glm::vec4& s, const glm::vec4& d) { return glm::min(s, d); },
        "blend_op_min"
    );
}

// agfx DrawBlendOpMax: max(S, D).
TEST_F(Blend_op_test, op_max)
{
    check_equation(
        erhe::graphics::Blend_equation_mode::max_,
        [](const glm::vec4& s, const glm::vec4& d) { return glm::max(s, d); },
        "blend_op_max"
    );
}

} // namespace erhe::graphics::test
