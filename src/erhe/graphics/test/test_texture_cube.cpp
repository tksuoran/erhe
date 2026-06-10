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

constexpr const char* c_cube_vertex_source = R"glsl(
void main()
{
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)glsl";

// DIR is injected as a define so each pass samples one fixed cube face by its
// center direction vector (nearest filtering -> exact value).
constexpr const char* c_cube_fragment_source = R"glsl(
void main()
{
    out_color = texture(s_texture, vec3(DIR));
}
)glsl";

// Center direction vector for each cube face, in Vulkan cube-face order:
// 0:+X, 1:-X, 2:+Y, 3:-Y, 4:+Z, 5:-Z. A direction that points at the center of
// a face resolves to that face regardless of the face's internal orientation,
// so a 1x1 face (one texel) makes the sampled color exact and seam-free.
auto face_direction(const int face) -> const char*
{
    switch (face) {
        case 0:  return "1.0, 0.0, 0.0";
        case 1:  return "-1.0, 0.0, 0.0";
        case 2:  return "0.0, 1.0, 0.0";
        case 3:  return "0.0, -1.0, 0.0";
        case 4:  return "0.0, 0.0, 1.0";
        default: return "0.0, 0.0, -1.0";
    }
}

} // namespace

// Cube map sampling. Create a texture_cube_map (6 array layers, 1x1 per face),
// fill each face with a distinct solid color via copy_from_buffer's
// destination_slice (the buffer->image overload's baseArrayLayer = cube face,
// Vulkan order +X,-X,+Y,-Y,+Z,-Z). Then, in a render pass per face, sample that
// face through a samplerCube using the face's center direction vector (baked as
// a GLSL define) and assert the rendered color matches the color uploaded to
// that face. The Vulkan backend sets VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT for
// texture_cube_map and set_sampled_image builds a VK_IMAGE_VIEW_TYPE_CUBE view
// spanning all 6 layers, so the shader's vec3(dir) lookup reaches each distinct
// face. With a 1x1 face the lookup is orientation-free: any direction resolving
// to a face returns that face's single texel. Mirrors test_texture_array.cpp
// for the combined_image_sampler / set_sampled_image / per-slice-fill wiring.
TEST_F(Gpu_test, texture_cube_sample_faces)
{
    constexpr int face_count = 6;
    constexpr int out_width  = 16;
    constexpr int out_height = 16;

    // Distinct solid color per cube face (RGBA8), one per +X,-X,+Y,-Y,+Z,-Z.
    const std::array<std::array<uint8_t, 4>, face_count> face_colors{{
        {{ 210u,  20u,  30u, 255u }},  // +X
        {{  20u, 210u,  30u, 255u }},  // -X
        {{  30u,  40u, 220u, 255u }},  // +Y
        {{ 220u, 210u,  30u, 255u }},  // -Y
        {{  30u, 210u, 210u, 255u }},  // +Z
        {{ 210u,  30u, 210u, 255u }}   // -Z
    }};

    erhe::graphics::Device& graphics_device = device();

    // Cube texture: sampled (combined_image_sampler input) + transfer_dst
    // (copy_from_buffer target) + transfer_src (symmetry with the readback
    // helpers; verified here via sampling). texture_cube_map implies the
    // CUBE-compatible image flag; the 6 faces are array layers.
    const std::shared_ptr<erhe::graphics::Texture> cube_texture = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info{
            .device            = graphics_device,
            .usage_mask        =
                erhe::graphics::Image_usage_flag_bit_mask::sampled      |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_src |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
            .type              = erhe::graphics::Texture_type::texture_cube_map,
            .pixelformat       = erhe::dataformat::Format::format_8_vec4_unorm,
            .width             = 1,
            .height            = 1,
            .array_layer_count = face_count,
            .debug_label       = erhe::utility::Debug_label{"cube texture"}
        }
    );
    ASSERT_EQ(cube_texture->get_array_layer_count(), face_count);

    // Fill each face (1x1 texel) from a host buffer via destination_slice. Each
    // call transitions its own face subresource UNDEFINED -> TRANSFER_DST ->
    // SHADER_READ_ONLY and tracks the latter, so after all faces are filled the
    // cube is uniformly in shader_read_only_optimal, ready to sample.
    constexpr std::size_t face_bytes = 4u; // 1 texel * RGBA8
    for (int face = 0; face < face_count; ++face) {
        const std::array<uint8_t, 4> color = face_colors[face];

        const std::shared_ptr<erhe::graphics::Buffer> face_buffer =
            make_host_buffer(face_bytes, erhe::graphics::Buffer_usage::transfer_src, "cube face source");
        {
            const std::span<std::byte> mapped = face_buffer->map_bytes(0, face_bytes);
            std::memcpy(mapped.data(), color.data(), face_bytes);
            face_buffer->unmap();
        }

        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Blit_command_encoder blit = graphics_device.make_blit_command_encoder(command_buffer);
                blit.copy_from_buffer(
                    face_buffer.get(),
                    0,                                          // source_offset
                    static_cast<std::uintptr_t>(face_bytes),    // source_bytes_per_row (1 texel row)
                    static_cast<std::uintptr_t>(face_bytes),    // source_bytes_per_image (1 row)
                    glm::ivec3{1, 1, 1},                        // source_size (1x1 face)
                    cube_texture.get(),
                    static_cast<std::uintptr_t>(face),          // destination_slice (cube face / array layer)
                    0,                                          // destination_level
                    glm::ivec3{0, 0, 0}                         // destination_origin
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
            .debug_label = erhe::utility::Debug_label{"cube sample nearest"}
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
                    .glsl_type     = erhe::graphics::Glsl_type::sampler_cube,
                    .stage_flags   = erhe::graphics::Shader_stage_flags::fragment
                }
            },
            .debug_label       = erhe::utility::Debug_label{"cube sample layout"},
            .uses_texture_heap = false
        }
    };

    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    // One pass per face: sample that face by its center direction, render to a
    // fresh output, read back, and assert every texel equals the face's color.
    for (int face = 0; face < face_count; ++face) {
        const std::array<uint8_t, 4>                   expected = face_colors[face];
        const std::shared_ptr<erhe::graphics::Texture> output   = make_color_target(out_width, out_height);

        erhe::graphics::Shader_stages_create_info shader_create_info{
            .name             = "texture_cube_sample",
            .defines          = { { "DIR", std::string{face_direction(face)} } },
            .fragment_outputs = &fragment_outputs,
            .no_vertex_input  = true,
            .shaders = {
                { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_cube_vertex_source}   },
                { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_cube_fragment_source} }
            },
            .bind_group_layout = &sampler_layout
        };
        erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(graphics_device, shader_create_info);
        ASSERT_TRUE(prototype.is_valid()) << "cube-sample shader (face " << face << ") failed to compile/link";
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
        descriptor.debug_label = erhe::utility::Debug_label{"cube sample"};

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
        ASSERT_TRUE(pipeline.is_valid()) << "cube-sample pipeline (face " << face << ") is not valid";

        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Render_pass            render_pass{graphics_device, descriptor};
                erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
                const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                encoder.set_viewport_rect(0, 0, out_width, out_height);
                encoder.set_scissor_rect (0, 0, out_width, out_height);
                encoder.set_bind_group_layout(&sampler_layout);
                encoder.set_render_pipeline(pipeline);
                encoder.set_sampled_image(0, *cube_texture, sampler);
                encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
            }
        );

        const std::vector<uint8_t> pixels = read_texture_rgba8(*output);
        ASSERT_EQ(pixels.size(), static_cast<std::size_t>(out_width) * static_cast<std::size_t>(out_height) * 4u);

        int bad = 0;
        for (std::size_t i = 0; (i + 3) < pixels.size(); i += 4) {
            const bool ok =
                (std::abs(static_cast<int>(pixels[i + 0]) - static_cast<int>(expected[0])) <= 2) &&
                (std::abs(static_cast<int>(pixels[i + 1]) - static_cast<int>(expected[1])) <= 2) &&
                (std::abs(static_cast<int>(pixels[i + 2]) - static_cast<int>(expected[2])) <= 2) &&
                (std::abs(static_cast<int>(pixels[i + 3]) - static_cast<int>(expected[3])) <= 2);
            if (!ok) {
                ++bad;
            }
        }
        EXPECT_EQ(bad, 0) << bad << " texels did not match cube face " << face << " color {"
                          << static_cast<int>(expected[0]) << ","
                          << static_cast<int>(expected[1]) << ","
                          << static_cast<int>(expected[2]) << ","
                          << static_cast<int>(expected[3]) << "}";
    }
}

} // namespace erhe::graphics::test
