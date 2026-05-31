#include "gpu_test_fixture.hpp"

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
#include "erhe_graphics/texture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

// Fullscreen triangle; each pass restricts the actually-written region with a
// scissor rect rather than with geometry, so both draws cover the viewport and
// the scissor decides which half is touched.
constexpr const char* c_vertex_source = R"glsl(
void main()
{
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)glsl";

// Constant color injected via COLOR define (an rgba vec4 literal).
constexpr const char* c_fragment_source = R"glsl(
void main()
{
    out_color = COLOR;
}
)glsl";

auto build_color_shader(
    erhe::graphics::Device&                 device,
    const erhe::graphics::Bind_group_layout& layout,
    const erhe::graphics::Fragment_outputs&  fragment_outputs,
    const char*                              color_define,
    const char*                              name
) -> erhe::graphics::Shader_stages
{
    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = name,
        .defines          = { { "COLOR", color_define } },
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source}   },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source} }
        },
        .bind_group_layout = &layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device, shader_create_info);
    EXPECT_TRUE(prototype.is_valid()) << name << " shader failed to compile/link";
    return erhe::graphics::Shader_stages{device, std::move(prototype)};
}

} // namespace

// Load_action::Load coverage: two render passes recorded into ONE command
// buffer, both targeting the SAME color texture.
//
//   Pass 1: Load_action::Clear (to black) + Store. Scissored to the LEFT half,
//           draws red there. layout_after = color_attachment_optimal.
//   Pass 2: Load_action::Load (PRESERVE pass 1) + Store. layout_before MUST
//           equal pass 1's layout_after (color_attachment_optimal). Scissored to
//           the RIGHT half, draws green there. layout_after = transfer_src so the
//           result can be read back.
//
// The two scissored regions do NOT overlap, so the left half is never touched by
// pass 2. Reading back, the left half must still be red (pass 1's result) and the
// right half must be green. If pass 2 had used Load_action::Clear instead of Load,
// the left half would have been wiped to black -- so an asserted-red left half is
// exactly the proof that Load preserved the prior contents.
TEST_F(Gpu_test, load_action_preserves_prior_pass)
{
    constexpr int width      = 16;
    constexpr int height     = 16;
    constexpr int half_width = width / 2;

    erhe::graphics::Device& graphics_device = device();

    const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(width, height);

    const erhe::graphics::Bind_group_layout empty_layout{
        graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"load_action empty layout"},
            .uses_texture_heap = false
        }
    };
    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    erhe::graphics::Shader_stages red_shader   = build_color_shader(graphics_device, empty_layout, fragment_outputs, "vec4(1.0, 0.0, 0.0, 1.0)", "load_action_red");
    erhe::graphics::Shader_stages green_shader = build_color_shader(graphics_device, empty_layout, fragment_outputs, "vec4(0.0, 1.0, 0.0, 1.0)", "load_action_green");

    // Pass 1: clear to black, draw red into the left half, leave the image in
    // color_attachment_optimal so pass 2 can Load it without a layout transition
    // between the two passes.
    erhe::graphics::Render_pass_descriptor pass1_descriptor{};
    pass1_descriptor.color_attachments[0].texture       = color_target.get();
    pass1_descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 }; // black
    pass1_descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    pass1_descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    pass1_descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    pass1_descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
    pass1_descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::color_attachment;
    pass1_descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::color_attachment_optimal;
    pass1_descriptor.render_target_width  = width;
    pass1_descriptor.render_target_height = height;
    pass1_descriptor.debug_label = erhe::utility::Debug_label{"load_action pass 1 (clear)"};

    // Pass 2: LOAD the prior result (layout_before matches pass 1 layout_after),
    // draw green into the right half, end in transfer_src for readback.
    erhe::graphics::Render_pass_descriptor pass2_descriptor{};
    pass2_descriptor.color_attachments[0].texture       = color_target.get();
    pass2_descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Load;
    pass2_descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    pass2_descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::color_attachment;
    pass2_descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::color_attachment_optimal;
    pass2_descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    pass2_descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
    pass2_descriptor.render_target_width  = width;
    pass2_descriptor.render_target_height = height;
    pass2_descriptor.debug_label = erhe::utility::Debug_label{"load_action pass 2 (load)"};

    erhe::graphics::Render_pipeline_create_info red_pipeline_create_info;
    red_pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
    red_pipeline_create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
    red_pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
    red_pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
    red_pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
    red_pipeline_create_info.base.bind_group_layout                 = &empty_layout;
    red_pipeline_create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
    red_pipeline_create_info.shader_stages                          = &red_shader;
    red_pipeline_create_info.vertex_input                           = nullptr;
    red_pipeline_create_info.set_format_from_render_pass(pass1_descriptor);
    const erhe::graphics::Render_pipeline red_pipeline{graphics_device, red_pipeline_create_info};
    ASSERT_TRUE(red_pipeline.is_valid()) << "load_action red pipeline is not valid";

    erhe::graphics::Render_pipeline_create_info green_pipeline_create_info;
    green_pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
    green_pipeline_create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
    green_pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
    green_pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
    green_pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
    green_pipeline_create_info.base.bind_group_layout                 = &empty_layout;
    green_pipeline_create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
    green_pipeline_create_info.shader_stages                          = &green_shader;
    green_pipeline_create_info.vertex_input                           = nullptr;
    green_pipeline_create_info.set_format_from_render_pass(pass2_descriptor);
    const erhe::graphics::Render_pipeline green_pipeline{graphics_device, green_pipeline_create_info};
    ASSERT_TRUE(green_pipeline.is_valid()) << "load_action green pipeline is not valid";

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            // Pass 1: clear black, red into the left half.
            {
                erhe::graphics::Render_pass            render_pass{graphics_device, pass1_descriptor};
                erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
                const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                encoder.set_viewport_rect(0, 0, width, height);
                encoder.set_scissor_rect (0, 0, half_width, height); // left half only
                encoder.set_bind_group_layout(&empty_layout);
                encoder.set_render_pipeline(red_pipeline);
                encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
            }
            // Pass 2: LOAD prior contents, green into the right half.
            {
                erhe::graphics::Render_pass            render_pass{graphics_device, pass2_descriptor};
                erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
                const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                encoder.set_viewport_rect(0, 0, width, height);
                encoder.set_scissor_rect (half_width, 0, width - half_width, height); // right half only
                encoder.set_bind_group_layout(&empty_layout);
                encoder.set_render_pipeline(green_pipeline);
                encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
            }
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*color_target);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);

    auto texel = [&](const int x, const int y) -> std::array<int, 4> {
        const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
        return std::array<int, 4>{
            static_cast<int>(pixels[index + 0u]),
            static_cast<int>(pixels[index + 1u]),
            static_cast<int>(pixels[index + 2u]),
            static_cast<int>(pixels[index + 3u])
        };
    };

    int left_red    = 0; // left half still carries pass 1's red (proves Load preserved it)
    int left_black  = 0; // left half was wiped to black (would indicate Load behaved like Clear)
    int right_green = 0; // right half carries pass 2's green
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::array<int, 4> c = texel(x, y);
            const bool is_red   = (c[0] >= 253) && (c[1] <= 2)   && (c[2] <= 2)   && (c[3] >= 253);
            const bool is_green = (c[0] <= 2)   && (c[1] >= 253) && (c[2] <= 2)   && (c[3] >= 253);
            const bool is_black = (c[0] <= 2)   && (c[1] <= 2)   && (c[2] <= 2)   && (c[3] >= 253);
            if (x < half_width) {
                if (is_red)   ++left_red;
                if (is_black) ++left_black;
            } else {
                if (is_green) ++right_green;
            }
        }
    }

    const int half_texels = half_width * height;
    // The whole left half must be red and untouched by pass 2 (the crux of Load vs Clear).
    EXPECT_EQ(left_red,   half_texels) << "left half should still be pass 1's red after a Load pass 2";
    EXPECT_EQ(left_black, 0)           << "left half went black -> Load behaved like Clear (regression)";
    // The whole right half must be pass 2's green.
    EXPECT_EQ(right_green, (width - half_width) * height) << "right half should be pass 2's green";
}

} // namespace erhe::graphics::test
