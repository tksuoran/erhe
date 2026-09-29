#include "gpu_test_fixture.hpp"

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/fragment_output.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"

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

constexpr const char* c_vertex_source = R"glsl(
void main()
{
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)glsl";

// Two fragment outputs -> two color attachments.
constexpr const char* c_fragment_source = R"glsl(
void main()
{
    out_color0 = vec4(1.0, 0.0, 0.0, 1.0); // attachment 0: red
    out_color1 = vec4(0.0, 1.0, 0.0, 1.0); // attachment 1: green
}
)glsl";

// Two ramps, one per attachment, in exact k / 255 steps so each texel stores
// the byte k: attachment 0 = (x, y, 64, 255), attachment 1 = (2 x, 64, 3 y, 255),
// x and y the pixel position in memory rows (gl_FragCoord).
constexpr const char* c_ramp_fragment_source = R"glsl(
void main()
{
    float x = floor(gl_FragCoord.x);
    float y = floor(gl_FragCoord.y);
    out_color0 = vec4(x / 255.0,         y / 255.0,    64.0 / 255.0,      1.0);
    out_color1 = vec4((2.0 * x) / 255.0, 64.0 / 255.0, (3.0 * y) / 255.0, 1.0);
}
)glsl";

// texelFetch both attachments at every texel and store the per-channel sum,
// scaled back to bytes, as four uints per texel (memory row order).
constexpr const char* c_sum_compute_source = R"glsl(
layout(local_size_x = 8, local_size_y = 8) in;
void main()
{
    ivec2 position = ivec2(gl_GlobalInvocationID.xy);
    vec4  sum      = texelFetch(s_attachment0, position, 0) + texelFetch(s_attachment1, position, 0);
    uint  base     = ((uint(position.y) * TARGET_WIDTH) + uint(position.x)) * 4u;
    Output.data[base + 0u] = uint(round(sum.r * 255.0));
    Output.data[base + 1u] = uint(round(sum.g * 255.0));
    Output.data[base + 2u] = uint(round(sum.b * 255.0));
    Output.data[base + 3u] = uint(round(sum.a * 255.0));
}
)glsl";

[[nodiscard]] auto all_match(const std::vector<uint8_t>& pixels, int r, int g, int b, int a) -> int
{
    int bad = 0;
    for (std::size_t i = 0; (i + 3) < pixels.size(); i += 4) {
        const bool ok =
            (std::abs(static_cast<int>(pixels[i + 0]) - r) <= 2) &&
            (std::abs(static_cast<int>(pixels[i + 1]) - g) <= 2) &&
            (std::abs(static_cast<int>(pixels[i + 2]) - b) <= 2) &&
            (std::abs(static_cast<int>(pixels[i + 3]) - a) <= 2);
        if (!ok) {
            ++bad;
        }
    }
    return bad;
}

} // namespace

// Multiple render targets: a fragment shader with two outputs writing to two
// color attachments in one render pass. Reads both back and asserts each got
// its own color.
TEST_F(Gpu_test, multiple_render_targets)
{
    constexpr int width  = 16;
    constexpr int height = 16;

    const std::shared_ptr<erhe::graphics::Texture> target0 = make_color_target(width, height);
    const std::shared_ptr<erhe::graphics::Texture> target1 = make_color_target(width, height);

    const erhe::graphics::Bind_group_layout empty_layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"MRT empty layout"},
            .uses_texture_heap = false
        }
    };

    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color0", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 },
          erhe::graphics::Fragment_output{ .name = "out_color1", .type = erhe::graphics::Glsl_type::float_vec4, .location = 1 } }
    };

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "mrt",
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source}   },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source} }
        },
        .bind_group_layout = &empty_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "MRT shader failed to compile/link";
    erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

    erhe::graphics::Render_pass_descriptor descriptor{};
    for (int index = 0; index < 2; ++index) {
        erhe::graphics::Texture* texture = (index == 0) ? target0.get() : target1.get();
        descriptor.color_attachments[index].texture       = texture;
        descriptor.color_attachments[index].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
        descriptor.color_attachments[index].load_action   = erhe::graphics::Load_action::Clear;
        descriptor.color_attachments[index].store_action  = erhe::graphics::Store_action::Store;
        descriptor.color_attachments[index].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[index].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.color_attachments[index].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[index].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
    }
    descriptor.render_target_width  = width;
    descriptor.render_target_height = height;
    descriptor.debug_label = erhe::utility::Debug_label{"MRT"};

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
    ASSERT_TRUE(pipeline.is_valid()) << "MRT pipeline is not valid";

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

    const std::vector<uint8_t> pixels0 = read_texture_rgba8(*target0);
    const std::vector<uint8_t> pixels1 = read_texture_rgba8(*target1);

    EXPECT_EQ(all_match(pixels0, 255, 0, 0, 255), 0) << "attachment 0 should be red";
    EXPECT_EQ(all_match(pixels1, 0, 255, 0, 255), 0) << "attachment 1 should be green";
}

// agfx MRT: two color attachments written with different ramps in one pass;
// a compute pass then samples both (combined image samplers in the compute
// layout, texelFetch, attachments transitioned to shader_read_only_optimal)
// and writes the per-texel sum into an SSBO, compared against the analytic sum
// of the two ramps and byte-exact against the buffer golden mrt_compute_sum.bin.
TEST_F(Gpu_test, multiple_render_targets_compute_sum)
{
    constexpr int width  = 16;
    constexpr int height = 16;

    const std::shared_ptr<erhe::graphics::Texture> target0 = make_color_target(width, height);
    const std::shared_ptr<erhe::graphics::Texture> target1 = make_color_target(width, height);

    const erhe::graphics::Bind_group_layout empty_layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"MRT ramps empty layout"},
            .uses_texture_heap = false
        }
    };

    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color0", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 },
          erhe::graphics::Fragment_output{ .name = "out_color1", .type = erhe::graphics::Glsl_type::float_vec4, .location = 1 } }
    };
    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "mrt_ramps",
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source}        },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_ramp_fragment_source} }
        },
        .bind_group_layout = &empty_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "MRT ramp shader failed to compile/link";
    erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

    erhe::graphics::Render_pass_descriptor descriptor{};
    for (int index = 0; index < 2; ++index) {
        erhe::graphics::Texture* texture = (index == 0) ? target0.get() : target1.get();
        descriptor.color_attachments[index].texture       = texture;
        descriptor.color_attachments[index].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
        descriptor.color_attachments[index].load_action   = erhe::graphics::Load_action::Clear;
        descriptor.color_attachments[index].store_action  = erhe::graphics::Store_action::Store;
        descriptor.color_attachments[index].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[index].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.color_attachments[index].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[index].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
    }
    descriptor.render_target_width  = width;
    descriptor.render_target_height = height;
    descriptor.debug_label = erhe::utility::Debug_label{"MRT ramps"};

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
    ASSERT_TRUE(pipeline.is_valid()) << "MRT ramp pipeline is not valid";

    // Compute pass: output SSBO at binding 0, the attachments at 1 and 2.
    erhe::graphics::Shader_resource output_block{
        device(),
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "Output",
            .binding_point = 0,
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .writeonly     = true
        }
    };
    output_block.add_uint("data", erhe::graphics::Shader_resource::unsized_array);
    const erhe::graphics::Sampler sampler{
        device(),
        erhe::graphics::Sampler_create_info{
            .min_filter   = erhe::graphics::Filter::nearest,
            .mag_filter   = erhe::graphics::Filter::nearest,
            .mipmap_mode  = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .address_mode = {
                erhe::graphics::Sampler_address_mode::clamp_to_edge,
                erhe::graphics::Sampler_address_mode::clamp_to_edge,
                erhe::graphics::Sampler_address_mode::clamp_to_edge
            },
            .debug_label  = erhe::utility::Debug_label{"MRT compute sampler"}
        }
    };
    const erhe::graphics::Bind_group_layout compute_layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                erhe::graphics::Bind_group_layout_binding{
                    .binding_point = 0u,
                    .type          = erhe::graphics::Binding_type::storage_buffer,
                    .stage_flags   = erhe::graphics::Shader_stage_flags::compute
                },
                erhe::graphics::Bind_group_layout_binding{
                    .binding_point = 1u,
                    .type          = erhe::graphics::Binding_type::combined_image_sampler,
                    .name          = "s_attachment0",
                    .glsl_type     = erhe::graphics::Glsl_type::sampler_2d,
                    .stage_flags   = erhe::graphics::Shader_stage_flags::compute
                },
                erhe::graphics::Bind_group_layout_binding{
                    .binding_point = 2u,
                    .type          = erhe::graphics::Binding_type::combined_image_sampler,
                    .name          = "s_attachment1",
                    .glsl_type     = erhe::graphics::Glsl_type::sampler_2d,
                    .stage_flags   = erhe::graphics::Shader_stage_flags::compute
                }
            },
            .debug_label       = erhe::utility::Debug_label{"MRT compute sum layout"},
            .uses_texture_heap = false
        }
    };
    const Compute_program sum_program = make_compute_program(
        "mrt_compute_sum",
        c_sum_compute_source,
        { { "TARGET_WIDTH", std::to_string(width) + "u" } },
        {},
        { &output_block },
        compute_layout
    );
    ASSERT_TRUE(sum_program.is_valid());

    const std::size_t word_count   = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    const std::size_t output_bytes = word_count * sizeof(uint32_t);
    const std::shared_ptr<erhe::graphics::Buffer> output = make_readback_buffer(output_bytes, "MRT compute sum");

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            {
                erhe::graphics::Render_pass            render_pass{device(), descriptor};
                erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
                const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                encoder.set_viewport_rect(0, 0, width, height);
                encoder.set_scissor_rect (0, 0, width, height);
                encoder.set_bind_group_layout(&empty_layout);
                encoder.set_render_pipeline(pipeline);
                encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
            }
            command_buffer.transition_texture_layout(*target0, erhe::graphics::Image_layout::shader_read_only_optimal);
            command_buffer.transition_texture_layout(*target1, erhe::graphics::Image_layout::shader_read_only_optimal);
            {
                erhe::graphics::Compute_command_encoder encoder = device().make_compute_command_encoder(command_buffer);
                encoder.set_bind_group_layout(&compute_layout);
                encoder.set_compute_pipeline(*sum_program.pipeline);
                encoder.set_buffer(erhe::graphics::Buffer_target::storage, output.get(), 0, output_bytes, 0);
                encoder.set_sampled_image(1, *target0, sampler);
                encoder.set_sampled_image(2, *target1, sampler);
                encoder.dispatch_compute(width / 8, height / 8, 1);
            }
            command_buffer.transition_texture_layout(*target0, erhe::graphics::Image_layout::transfer_src_optimal);
            command_buffer.transition_texture_layout(*target1, erhe::graphics::Image_layout::transfer_src_optimal);
        }
    );

    std::vector<uint32_t> expected(word_count);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t base = ((static_cast<std::size_t>(y) * width) + static_cast<std::size_t>(x)) * 4u;
            expected[base + 0] = static_cast<uint32_t>(x + (2 * x));
            expected[base + 1] = static_cast<uint32_t>(y + 64);
            expected[base + 2] = static_cast<uint32_t>(64 + (3 * y));
            expected[base + 3] = 510u;
        }
    }

    const std::vector<std::byte> raw = read_buffer(*output, output_bytes);
    ASSERT_EQ(raw.size(), output_bytes);
    std::vector<uint32_t> got(word_count);
    std::memcpy(got.data(), raw.data(), output_bytes);
    int         mismatches = 0;
    std::size_t first_bad  = 0;
    for (std::size_t i = 0; i < word_count; ++i) {
        if (got[i] != expected[i]) {
            if (mismatches == 0) {
                first_bad = i;
            }
            ++mismatches;
        }
    }
    EXPECT_EQ(mismatches, 0)
        << mismatches << " of " << word_count << " channel sums differ; first at texel " << (first_bad / 4u)
        << " channel " << (first_bad % 4u) << " (got " << got[first_bad] << ", expected " << expected[first_bad] << ")";

    expect_buffer_matches_golden("mrt_compute_sum", std::span<const std::byte>{raw});
}

} // namespace erhe::graphics::test
