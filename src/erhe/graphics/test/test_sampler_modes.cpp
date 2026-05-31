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
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

constexpr const char* c_vertex_source = R"glsl(
void main()
{
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)glsl";

// Sample the bound texture at a fixed UV provided through the SAMPLE_UV define
// (a vec2 literal). The whole output therefore carries one texture() result.
constexpr const char* c_fragment_source = R"glsl(
void main()
{
    out_color = texture(s_texture, SAMPLE_UV);
}
)glsl";

constexpr const char* c_sampler_binding_name = "s_texture";

// Build the combined-image-sampler bind group layout shared by every sampling
// pass in this file.
auto make_sampler_layout(erhe::graphics::Device& device, const char* debug_label) -> erhe::graphics::Bind_group_layout
{
    return erhe::graphics::Bind_group_layout{
        device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                erhe::graphics::Bind_group_layout_binding{
                    .binding_point = 0,
                    .type          = erhe::graphics::Binding_type::combined_image_sampler,
                    .name          = c_sampler_binding_name,
                    .glsl_type     = erhe::graphics::Glsl_type::sampler_2d
                }
            },
            .debug_label       = erhe::utility::Debug_label{debug_label},
            .uses_texture_heap = false
        }
    };
}

// Build a sampling shader whose fragment samples at the given UV literal.
auto make_sampling_shader(
    erhe::graphics::Device&                  device,
    const erhe::graphics::Bind_group_layout& layout,
    const erhe::graphics::Fragment_outputs&  fragment_outputs,
    const char*                              sample_uv_glsl,
    const char*                              name
) -> erhe::graphics::Shader_stages
{
    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = name,
        .defines          = { { "SAMPLE_UV", sample_uv_glsl } },
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

// Sampler linear filter: upload a 2x1 texture with two distinct texel values
// (black texel0, white texel1), sample at u = 0.5 -- exactly halfway between the
// two texel centres (0.25 and 0.75) -- with Filter::linear, and assert the
// result is their interpolated midpoint (~128 gray), within a tolerance.
// Contrasts with texture_sample_nearest, where the nearest filter would snap to
// one texel's value with no interpolation.
TEST_F(Gpu_test, sampler_linear_filter_midpoint)
{
    constexpr int src_width  = 2;
    constexpr int src_height = 1;
    constexpr int out_size   = 1;

    erhe::graphics::Device& graphics_device = device();

    // texel0 = black {0,0,0,255}, texel1 = white {255,255,255,255}.
    const std::array<uint8_t, 8> source_texels{
        0u,   0u,   0u,   255u,
        255u, 255u, 255u, 255u
    };

    // Sampled (heap) + transfer_dst for the upload.
    const std::shared_ptr<erhe::graphics::Texture> source =
        make_color_target(src_width, src_height, erhe::dataformat::Format::format_8_vec4_unorm, /*include_transfer_dst=*/true);
    const std::shared_ptr<erhe::graphics::Texture> output = make_color_target(out_size, out_size);

    // Upload the pattern, then move the source into shader_read_only_optimal.
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            command_buffer.upload_to_texture(
                *source,
                0, 0, 0,
                src_width,
                src_height,
                erhe::dataformat::Format::format_8_vec4_unorm,
                source_texels.data(),
                src_width * 4
            );
            command_buffer.transition_texture_layout(*source, erhe::graphics::Image_layout::shader_read_only_optimal);
        }
    );

    const erhe::graphics::Sampler sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter   = erhe::graphics::Filter::linear,
            .mag_filter   = erhe::graphics::Filter::linear,
            .mipmap_mode  = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .address_mode = {
                erhe::graphics::Sampler_address_mode::clamp_to_edge,
                erhe::graphics::Sampler_address_mode::clamp_to_edge,
                erhe::graphics::Sampler_address_mode::clamp_to_edge
            },
            .debug_label  = erhe::utility::Debug_label{"sampler linear"}
        }
    };

    const erhe::graphics::Bind_group_layout sampler_layout = make_sampler_layout(graphics_device, "sampler linear layout");
    const erhe::graphics::Fragment_outputs  fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    // Sample exactly between the two texel centres.
    erhe::graphics::Shader_stages shader_stages = make_sampling_shader(graphics_device, sampler_layout, fragment_outputs, "vec2(0.5, 0.5)", "sampler_linear");

    erhe::graphics::Render_pass_descriptor descriptor{};
    descriptor.color_attachments[0].texture       = output.get();
    descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
    descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.render_target_width  = out_size;
    descriptor.render_target_height = out_size;
    descriptor.debug_label = erhe::utility::Debug_label{"sampler linear sample"};

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
    ASSERT_TRUE(pipeline.is_valid()) << "sampler linear pipeline is not valid";

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass            render_pass{graphics_device, descriptor};
            erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, out_size, out_size);
            encoder.set_scissor_rect (0, 0, out_size, out_size);
            encoder.set_bind_group_layout(&sampler_layout);
            encoder.set_render_pipeline(pipeline);
            encoder.set_sampled_image(0, *source, sampler);
            encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*output);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(out_size) * static_cast<std::size_t>(out_size) * 4u);

    // Midpoint of black and white is ~128. Allow a small tolerance for rounding /
    // sRGB-vs-unorm interpolation behaviour.
    const int r = pixels[0];
    const int g = pixels[1];
    const int b = pixels[2];
    const int a = pixels[3];
    EXPECT_GE(r, 120); EXPECT_LE(r, 136) << "linear-filtered R is not the black/white midpoint";
    EXPECT_GE(g, 120); EXPECT_LE(g, 136) << "linear-filtered G is not the black/white midpoint";
    EXPECT_GE(b, 120); EXPECT_LE(b, 136) << "linear-filtered B is not the black/white midpoint";
    EXPECT_GE(a, 253) << "alpha (255 in both texels) should stay opaque";
}

// Sampler address modes: with a 2x1 texture (texel0 = red, texel1 = green) and a
// nearest filter, sample at out-of-[0,1] coordinates with clamp_to_edge, repeat,
// and mirrored_repeat, and assert the fetched texel matches the wrapped
// coordinate for each mode.
//
// Two sample points uniquely identify each mode:
//   u = 1.25                          u = 1.75
//   clamp -> 1.0     -> texel1 (G)    clamp -> 1.0       -> texel1 (G)
//   repeat-> 0.25    -> texel0 (R)    repeat-> 0.75      -> texel1 (G)
//   mirror-> 2-1.25=0.75 -> texel1(G) mirror-> 2-1.75=0.25 -> texel0 (R)
// so the (1.25, 1.75) signatures are clamp=(G,G), repeat=(R,G), mirror=(G,R) --
// all distinct.
TEST_F(Gpu_test, sampler_address_modes)
{
    constexpr int src_width  = 2;
    constexpr int src_height = 1;
    constexpr int out_size   = 1;

    erhe::graphics::Device& graphics_device = device();

    // texel0 = red {255,0,0,255}, texel1 = green {0,255,0,255}.
    const std::array<uint8_t, 8> source_texels{
        255u, 0u,   0u, 255u,
        0u,   255u, 0u, 255u
    };

    const std::shared_ptr<erhe::graphics::Texture> source =
        make_color_target(src_width, src_height, erhe::dataformat::Format::format_8_vec4_unorm, /*include_transfer_dst=*/true);

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            command_buffer.upload_to_texture(
                *source,
                0, 0, 0,
                src_width,
                src_height,
                erhe::dataformat::Format::format_8_vec4_unorm,
                source_texels.data(),
                src_width * 4
            );
            command_buffer.transition_texture_layout(*source, erhe::graphics::Image_layout::shader_read_only_optimal);
        }
    );

    const erhe::graphics::Bind_group_layout sampler_layout = make_sampler_layout(graphics_device, "sampler address layout");
    const erhe::graphics::Fragment_outputs  fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    enum class Texel { red, green };

    // Run one sampling pass at the given out-of-range u with the given address
    // mode, and return whether the single output texel reads red or green.
    auto sample_with = [&](
        const erhe::graphics::Sampler_address_mode address_mode,
        const char*                                sample_uv_glsl,
        const char*                                name
    ) -> Texel {
        const erhe::graphics::Sampler sampler{
            graphics_device,
            erhe::graphics::Sampler_create_info{
                .min_filter   = erhe::graphics::Filter::nearest,
                .mag_filter   = erhe::graphics::Filter::nearest,
                .mipmap_mode  = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
                .address_mode = { address_mode, address_mode, address_mode },
                .debug_label  = erhe::utility::Debug_label{name}
            }
        };

        const std::shared_ptr<erhe::graphics::Texture> output = make_color_target(out_size, out_size);

        erhe::graphics::Shader_stages shader_stages = make_sampling_shader(graphics_device, sampler_layout, fragment_outputs, sample_uv_glsl, name);

        erhe::graphics::Render_pass_descriptor descriptor{};
        descriptor.color_attachments[0].texture       = output.get();
        descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
        descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
        descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
        descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.render_target_width  = out_size;
        descriptor.render_target_height = out_size;
        descriptor.debug_label = erhe::utility::Debug_label{name};

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
        EXPECT_TRUE(pipeline.is_valid()) << name << " pipeline is not valid";

        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Render_pass            render_pass{graphics_device, descriptor};
                erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
                const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                encoder.set_viewport_rect(0, 0, out_size, out_size);
                encoder.set_scissor_rect (0, 0, out_size, out_size);
                encoder.set_bind_group_layout(&sampler_layout);
                encoder.set_render_pipeline(pipeline);
                encoder.set_sampled_image(0, *source, sampler);
                encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
            }
        );

        const std::vector<uint8_t> pixels = read_texture_rgba8(*output);
        EXPECT_EQ(pixels.size(), static_cast<std::size_t>(out_size) * static_cast<std::size_t>(out_size) * 4u);
        const int r = pixels[0];
        const int g = pixels[1];
        // Red texel is {255,0,0}; green texel is {0,255,0}: classify by dominant channel.
        return (r > g) ? Texel::red : Texel::green;
    };

    // clamp_to_edge: both 1.25 and 1.75 clamp to the last texel (green).
    EXPECT_EQ(sample_with(erhe::graphics::Sampler_address_mode::clamp_to_edge,   "vec2(1.25, 0.5)", "clamp_1_25"),  Texel::green) << "clamp(1.25) should read the last texel (green)";
    EXPECT_EQ(sample_with(erhe::graphics::Sampler_address_mode::clamp_to_edge,   "vec2(1.75, 0.5)", "clamp_1_75"),  Texel::green) << "clamp(1.75) should read the last texel (green)";

    // repeat: 1.25 -> 0.25 (red), 1.75 -> 0.75 (green).
    EXPECT_EQ(sample_with(erhe::graphics::Sampler_address_mode::repeat,          "vec2(1.25, 0.5)", "repeat_1_25"), Texel::red)   << "repeat(1.25) should wrap to 0.25 (red)";
    EXPECT_EQ(sample_with(erhe::graphics::Sampler_address_mode::repeat,          "vec2(1.75, 0.5)", "repeat_1_75"), Texel::green) << "repeat(1.75) should wrap to 0.75 (green)";

    // mirrored_repeat: 1.25 -> 0.75 (green), 1.75 -> 0.25 (red).
    EXPECT_EQ(sample_with(erhe::graphics::Sampler_address_mode::mirrored_repeat, "vec2(1.25, 0.5)", "mirror_1_25"), Texel::green) << "mirror(1.25) should reflect to 0.75 (green)";
    EXPECT_EQ(sample_with(erhe::graphics::Sampler_address_mode::mirrored_repeat, "vec2(1.75, 0.5)", "mirror_1_75"), Texel::red)   << "mirror(1.75) should reflect to 0.25 (red)";
}

} // namespace erhe::graphics::test
