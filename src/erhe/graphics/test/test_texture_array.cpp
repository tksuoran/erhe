#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
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

#include <glm/glm.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

constexpr const char* c_array_vertex_source = R"glsl(
layout(location = 0) out vec2 v_uv;
void main()
{
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
    v_uv = (positions[gl_VertexID] * 0.5) + vec2(0.5);
}
)glsl";

// LAYER is injected as a define so each pass samples one fixed array layer.
constexpr const char* c_array_fragment_source = R"glsl(
layout(location = 0) in vec2 v_uv;
void main()
{
    out_color = texture(s_texture, vec3(v_uv, float(LAYER)));
}
)glsl";

} // namespace

// 2D array texture sampling. Create a texture_2d_array with 3 layers, fill each
// layer with a distinct solid color via copy_from_buffer's destination_slice
// (the buffer->image overload's baseArrayLayer). Then, in a separate render pass
// per layer, sample that layer through a sampler2DArray (layer selected by a
// GLSL define) and assert the rendered color matches the color uploaded to that
// layer. set_sampled_image builds a VK_IMAGE_VIEW_TYPE_2D_ARRAY view spanning
// all get_array_layer_count() layers, so the shader's vec3(uv, layer) lookup
// reaches each distinct slice. Mirrors test_texture_sample.cpp for the
// combined_image_sampler / set_sampled_image wiring.
TEST_F(Gpu_test, texture_2d_array_sample_layers)
{
    constexpr int layer_count = 3;
    constexpr int layer_size  = 4;  // each layer is 4x4 texels
    constexpr int out_width   = 16;
    constexpr int out_height  = 16;

    // Distinct solid color per layer (RGBA8).
    const std::array<std::array<uint8_t, 4>, layer_count> layer_colors{{
        {{ 200u,  30u,  40u, 255u }},  // layer 0
        {{  40u, 200u,  60u, 255u }},  // layer 1
        {{  50u,  70u, 210u, 255u }}   // layer 2
    }};

    erhe::graphics::Device& graphics_device = device();

    // Array texture: sampled (combined_image_sampler input) + transfer_dst
    // (copy_from_buffer target) + transfer_src (kept available for symmetry with
    // the readback helpers, though this test verifies via sampling).
    const std::shared_ptr<erhe::graphics::Texture> array_texture = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info{
            .device            = graphics_device,
            .usage_mask        =
                erhe::graphics::Image_usage_flag_bit_mask::sampled      |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_src |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
            .type              = erhe::graphics::Texture_type::texture_2d_array,
            .pixelformat       = erhe::dataformat::Format::format_8_vec4_unorm,
            .width             = layer_size,
            .height            = layer_size,
            .array_layer_count = layer_count,
            .debug_label       = erhe::utility::Debug_label{"array texture"}
        }
    );
    ASSERT_EQ(array_texture->get_array_layer_count(), layer_count);

    // Fill each layer from a host buffer via destination_slice. Each call
    // transitions its own layer subresource UNDEFINED -> TRANSFER_DST ->
    // SHADER_READ_ONLY and tracks the latter, so after all layers are filled the
    // texture is uniformly in shader_read_only_optimal, ready to sample.
    const std::size_t layer_bytes = static_cast<std::size_t>(layer_size) * static_cast<std::size_t>(layer_size) * 4u;
    for (int layer = 0; layer < layer_count; ++layer) {
        std::vector<uint8_t> layer_data(layer_bytes);
        for (std::size_t texel = 0; texel < (layer_bytes / 4u); ++texel) {
            layer_data[(texel * 4u) + 0] = layer_colors[layer][0];
            layer_data[(texel * 4u) + 1] = layer_colors[layer][1];
            layer_data[(texel * 4u) + 2] = layer_colors[layer][2];
            layer_data[(texel * 4u) + 3] = layer_colors[layer][3];
        }

        const std::shared_ptr<erhe::graphics::Buffer> layer_buffer =
            make_host_buffer(layer_bytes, erhe::graphics::Buffer_usage::transfer_src, "array layer source");
        {
            const std::span<std::byte> mapped = layer_buffer->map_bytes(0, layer_bytes);
            std::memcpy(mapped.data(), layer_data.data(), layer_bytes);
            layer_buffer->unmap();
        }

        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Blit_command_encoder blit = graphics_device.make_blit_command_encoder(command_buffer);
                blit.copy_from_buffer(
                    layer_buffer.get(),
                    0,                                                  // source_offset
                    static_cast<std::uintptr_t>(layer_size) * 4u,       // source_bytes_per_row
                    static_cast<std::uintptr_t>(layer_bytes),           // source_bytes_per_image
                    glm::ivec3{layer_size, layer_size, 1},              // source_size
                    array_texture.get(),
                    static_cast<std::uintptr_t>(layer),                 // destination_slice (array layer)
                    0,                                                  // destination_level
                    glm::ivec3{0, 0, 0}                                 // destination_origin
                );
            }
        );
    }

    const erhe::graphics::Sampler sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter  = erhe::graphics::Filter::nearest,
            .mag_filter  = erhe::graphics::Filter::nearest,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .debug_label = erhe::utility::Debug_label{"array sample nearest"}
        }
    };

    const erhe::graphics::Bind_group_layout sampler_layout{
        graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                erhe::graphics::Bind_group_layout_binding{
                    .binding_point = 0,
                    .type          = erhe::graphics::Binding_type::combined_image_sampler,
                    .name          = "s_texture",
                    .glsl_type     = erhe::graphics::Glsl_type::sampler_2d_array,
                    .stage_flags   = erhe::graphics::Shader_stage_flags::fragment
                }
            },
            .debug_label       = erhe::utility::Debug_label{"array sample layout"},
            .uses_texture_heap = false
        }
    };

    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    // One pass per layer: sample that layer, render to a fresh output, read back.
    for (int layer = 0; layer < layer_count; ++layer) {
        const std::shared_ptr<erhe::graphics::Texture> output = make_color_target(out_width, out_height);

        erhe::graphics::Shader_stages_create_info shader_create_info{
            .name             = "texture_array_sample",
            .defines          = { { "LAYER", std::to_string(layer) } },
            .fragment_outputs = &fragment_outputs,
            .no_vertex_input  = true,
            .shaders = {
                { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_array_vertex_source}   },
                { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_array_fragment_source} }
            },
            .bind_group_layout = &sampler_layout
        };
        erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(graphics_device, shader_create_info);
        ASSERT_TRUE(prototype.is_valid()) << "array-sample shader (layer " << layer << ") failed to compile/link";
        erhe::graphics::Shader_stages shader_stages{graphics_device, std::move(prototype)};

        erhe::graphics::Render_pass_descriptor descriptor{};
        descriptor.color_attachments[0].texture       = output.get();
        descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
        descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
        descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
        descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.render_target_width  = out_width;
        descriptor.render_target_height = out_height;
        descriptor.debug_label = erhe::utility::Debug_label{"array sample"};

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
        const erhe::graphics::Render_pipeline pipeline{graphics_device, pipeline_create_info};
        ASSERT_TRUE(pipeline.is_valid()) << "array-sample pipeline (layer " << layer << ") is not valid";

        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Render_pass            render_pass{graphics_device, descriptor};
                erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
                const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                encoder.set_viewport_rect(0, 0, out_width, out_height);
                encoder.set_scissor_rect (0, 0, out_width, out_height);
                encoder.set_bind_group_layout(&sampler_layout);
                encoder.set_render_pipeline(pipeline);
                encoder.set_sampled_image(0, *array_texture, sampler);
                encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
            }
        );

        const std::vector<uint8_t> pixels = read_texture_rgba8(*output);
        ASSERT_EQ(pixels.size(), static_cast<std::size_t>(out_width) * static_cast<std::size_t>(out_height) * 4u);

        int bad = 0;
        for (std::size_t i = 0; (i + 3) < pixels.size(); i += 4) {
            const bool ok =
                (std::abs(static_cast<int>(pixels[i + 0]) - static_cast<int>(layer_colors[layer][0])) <= 2) &&
                (std::abs(static_cast<int>(pixels[i + 1]) - static_cast<int>(layer_colors[layer][1])) <= 2) &&
                (std::abs(static_cast<int>(pixels[i + 2]) - static_cast<int>(layer_colors[layer][2])) <= 2) &&
                (std::abs(static_cast<int>(pixels[i + 3]) - static_cast<int>(layer_colors[layer][3])) <= 2);
            if (!ok) {
                ++bad;
            }
        }
        EXPECT_EQ(bad, 0) << bad << " texels did not match layer " << layer << " color {"
                          << static_cast<int>(layer_colors[layer][0]) << ","
                          << static_cast<int>(layer_colors[layer][1]) << ","
                          << static_cast<int>(layer_colors[layer][2]) << ","
                          << static_cast<int>(layer_colors[layer][3]) << "}";
    }
}

} // namespace erhe::graphics::test
