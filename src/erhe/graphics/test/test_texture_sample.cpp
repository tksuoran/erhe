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
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

constexpr const char* c_vertex_source = R"glsl(
layout(location = 0) out vec2 v_uv;
void main()
{
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    v_uv = (positions[gl_VertexIndex] * 0.5) + vec2(0.5);
}
)glsl";

constexpr const char* c_fragment_source = R"glsl(
layout(location = 0) in vec2 v_uv;
void main()
{
    out_color = texture(s_texture, v_uv);
}
)glsl";

} // namespace

// Texture sampling: fill a small source texture (via a render-pass clear that
// leaves it in shader_read_only_optimal), then sample it in a second pass
// through a dedicated Sampler bound with set_sampled_image, and assert the
// output matches the source color. Exercises Sampler, a combined_image_sampler
// Bind_group_layout binding, set_sampled_image, and GLSL texture().
TEST_F(Gpu_test, texture_sample_nearest)
{
    constexpr int source_size = 4;
    constexpr int width       = 16;
    constexpr int height      = 16;

    // Known source color {64, 128, 192, 255}.
    const std::array<double, 4> source_color{ 64.0 / 255.0, 128.0 / 255.0, 192.0 / 255.0, 1.0 };

    const std::shared_ptr<erhe::graphics::Texture> source = make_color_target(source_size, source_size);
    const std::shared_ptr<erhe::graphics::Texture> output = make_color_target(width, height);

    // Pass 1: clear the source to the known color and leave it ready to sample.
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass_descriptor descriptor{};
            descriptor.color_attachments[0].texture       = source.get();
            descriptor.color_attachments[0].clear_value   = source_color;
            descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
            descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
            descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
            descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
            descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::sampled;
            descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::shader_read_only_optimal;
            descriptor.render_target_width  = source_size;
            descriptor.render_target_height = source_size;
            descriptor.debug_label = erhe::utility::Debug_label{"sample source fill"};
            erhe::graphics::Render_pass            render_pass{device(), descriptor};
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
        }
    );

    const erhe::graphics::Sampler sampler{
        device(),
        erhe::graphics::Sampler_create_info{
            .min_filter  = erhe::graphics::Filter::nearest,
            .mag_filter  = erhe::graphics::Filter::nearest,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .debug_label = erhe::utility::Debug_label{"sample nearest"}
        }
    };

    const erhe::graphics::Bind_group_layout sampler_layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                erhe::graphics::Bind_group_layout_binding{
                    .binding_point = 0,
                    .type          = erhe::graphics::Binding_type::combined_image_sampler,
                    .name          = "s_texture",
                    .glsl_type     = erhe::graphics::Glsl_type::sampler_2d
                }
            },
            .debug_label       = erhe::utility::Debug_label{"sample layout"},
            .uses_texture_heap = false
        }
    };

    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "texture_sample",
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source}   },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source} }
        },
        .bind_group_layout = &sampler_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "texture-sample shader failed to compile/link";
    erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

    erhe::graphics::Render_pass_descriptor descriptor{};
    descriptor.color_attachments[0].texture       = output.get();
    descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
    descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.render_target_width  = width;
    descriptor.render_target_height = height;
    descriptor.debug_label = erhe::utility::Debug_label{"texture sample"};

    erhe::graphics::Render_pipeline_create_info pipeline_create_info;
    pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
    pipeline_create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
    pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
    pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
    pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
    pipeline_create_info.base.bind_group_layout                 = &sampler_layout;
    pipeline_create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
    pipeline_create_info.shader_stages                          = &shader_stages;
    pipeline_create_info.vertex_input                           = nullptr;
    pipeline_create_info.set_format_from_render_pass(descriptor);
    const erhe::graphics::Render_pipeline pipeline{device(), pipeline_create_info};
    ASSERT_TRUE(pipeline.is_valid()) << "texture-sample pipeline is not valid";

    // Pass 2: sample the source into the output.
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass            render_pass{device(), descriptor};
            erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, width, height);
            encoder.set_scissor_rect (0, 0, width, height);
            encoder.set_bind_group_layout(&sampler_layout);
            encoder.set_render_pipeline(pipeline);
            encoder.set_sampled_image(0, *source, sampler);
            encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*output);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);

    int bad = 0;
    for (std::size_t i = 0; (i + 3) < pixels.size(); i += 4) {
        const bool ok =
            (std::abs(static_cast<int>(pixels[i + 0]) -  64) <= 2) &&
            (std::abs(static_cast<int>(pixels[i + 1]) - 128) <= 2) &&
            (std::abs(static_cast<int>(pixels[i + 2]) - 192) <= 2) &&
            (std::abs(static_cast<int>(pixels[i + 3]) - 255) <= 2);
        if (!ok) {
            ++bad;
        }
    }
    EXPECT_EQ(bad, 0) << bad << " texels did not match the sampled source color {64,128,192,255}";
}

} // namespace erhe::graphics::test
