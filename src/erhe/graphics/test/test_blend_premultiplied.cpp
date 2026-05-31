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

constexpr const char* c_vertex_source = R"glsl(
void main()
{
    vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
)glsl";

// PREMULTIPLIED source: RGB is straight red (1,0,0) already scaled by alpha 0.5,
// i.e. (0.5, 0, 0), emitted alongside that same alpha. This is the contract for
// premultiplied "over" blending (src_factor = one): the shader must hand the
// blender colors that are already multiplied through by their own alpha.
constexpr const char* c_fragment_source = R"glsl(
void main()
{
    out_color = vec4(0.5, 0.0, 0.0, 0.5);
}
)glsl";

} // namespace

// Color blend (premultiplied "over"): composite a PREMULTIPLIED-alpha source over
// a known background using premultiplied Over factors
//   src_factor = one, dst_factor = one_minus_src_alpha   (for both rgb and alpha).
// The blend reduces to:
//   result.rgb = src.rgb + (1 - src.a) * dst.rgb
//   result.a   = src.a   + (1 - src.a) * dst.a
// Contrast with the straight-alpha blend_alpha_over test, which uses
// src_factor = src_alpha on a NON-premultiplied source. Here the source already
// carries premultiplied_rgb = (0.5,0,0) at alpha 0.5 over a blue (0,0,1,1)
// background, so the composite is
//   rgb = (0.5,0,0) + 0.5*(0,0,1) = (0.5,0,0.5) -> {128,0,128}
//   a   = 0.5 + 0.5*1.0           = 1.0          -> 255
// (the same final pixel as the straight path produces from a straight source,
//  reached through the premultiplied factors).
TEST_F(Gpu_test, blend_premultiplied_over)
{
    constexpr int width  = 16;
    constexpr int height = 16;

    erhe::graphics::Device& graphics_device = device();

    const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(width, height);

    const erhe::graphics::Bind_group_layout empty_layout{
        graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"premultiplied blend empty layout"},
            .uses_texture_heap = false
        }
    };

    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "blend_premultiplied",
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source}   },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source} }
        },
        .bind_group_layout = &empty_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(graphics_device, shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "premultiplied blend shader failed to compile/link";
    erhe::graphics::Shader_stages shader_stages{graphics_device, std::move(prototype)};

    // Premultiplied "over": src_factor = one, dst_factor = one_minus_src_alpha
    // (same for the alpha channel).
    erhe::graphics::Color_blend_state blend_state;
    blend_state.enabled                  = true;
    blend_state.rgb.equation_mode        = erhe::graphics::Blend_equation_mode::func_add;
    blend_state.rgb.source_factor        = erhe::graphics::Blending_factor::one;
    blend_state.rgb.destination_factor   = erhe::graphics::Blending_factor::one_minus_src_alpha;
    blend_state.alpha.equation_mode      = erhe::graphics::Blend_equation_mode::func_add;
    blend_state.alpha.source_factor      = erhe::graphics::Blending_factor::one;
    blend_state.alpha.destination_factor = erhe::graphics::Blending_factor::one_minus_src_alpha;

    erhe::graphics::Render_pass_descriptor descriptor{};
    descriptor.color_attachments[0].texture       = color_target.get();
    descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 1.0, 1.0 }; // blue background
    descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.render_target_width  = width;
    descriptor.render_target_height = height;
    descriptor.debug_label = erhe::utility::Debug_label{"premultiplied blend"};

    erhe::graphics::Render_pipeline_create_info pipeline_create_info;
    pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
    pipeline_create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
    pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
    pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
    pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
    pipeline_create_info.base.bind_group_layout                 = &empty_layout;
    pipeline_create_info.base.color_blend                       = &blend_state;
    pipeline_create_info.shader_stages                          = &shader_stages;
    pipeline_create_info.vertex_input                           = nullptr;
    pipeline_create_info.set_format_from_render_pass(descriptor);
    const erhe::graphics::Render_pipeline pipeline{graphics_device, pipeline_create_info};
    ASSERT_TRUE(pipeline.is_valid()) << "premultiplied blend pipeline is not valid";

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass            render_pass{graphics_device, descriptor};
            erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, width, height);
            encoder.set_scissor_rect (0, 0, width, height);
            encoder.set_bind_group_layout(&empty_layout);
            encoder.set_render_pipeline(pipeline);
            encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*color_target);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);

    // premultiplied (0.5,0,0,0.5) over blue: { 0.5, 0.0, 0.5, 1.0 } -> {128,0,128,255}.
    int bad = 0;
    for (int i = 0; i < width * height; ++i) {
        const int r = pixels[(static_cast<std::size_t>(i) * 4u) + 0];
        const int g = pixels[(static_cast<std::size_t>(i) * 4u) + 1];
        const int b = pixels[(static_cast<std::size_t>(i) * 4u) + 2];
        const int a = pixels[(static_cast<std::size_t>(i) * 4u) + 3];
        const bool ok =
            (r >= 126) && (r <= 130) &&
            (g <= 2) &&
            (b >= 126) && (b <= 130) &&
            (a >= 253);
        if (!ok) {
            ++bad;
        }
    }
    EXPECT_EQ(bad, 0) << bad << " of " << (width * height) << " texels were not the expected premultiplied composite {128,0,128,255}";
}

} // namespace erhe::graphics::test
