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
#include "erhe_graphics/texture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
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
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
)glsl";

// Signed-normalized values spanning the snorm range: a negative full-scale, a
// negative mid, zero, and a positive full-scale. Stored into an snorm8 target
// these become signed bytes (~ -127, ~ -64, 0, 127).
constexpr const char* c_fragment_source = R"glsl(
void main()
{
    out_color = vec4(-1.0, -0.5, 0.0, 1.0);
}
)glsl";

} // namespace

// snorm color render + readback. Render known signed-normalized values into a
// format_8_vec4_snorm color target, read the raw bytes back, reinterpret them as
// signed int8, and assert they decode (value = max(c/127, -1)) to the written
// signed values. Proves the engine renders to a signed-normalized attachment and
// that the stored signed bytes round-trip through a texture->buffer copy.
TEST_F(Gpu_test, snorm_color_render_readback)
{
    constexpr int width  = 8;
    constexpr int height = 8;
    constexpr erhe::dataformat::Format color_format = erhe::dataformat::Format::format_8_vec4_snorm;

    erhe::graphics::Device& graphics_device = device();

    const uint64_t usage_mask =
        erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
        erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    if (!graphics_device.get_format_properties(color_format).color_renderable ||
        !graphics_device.probe_image_format_support(color_format, usage_mask)) {
        GTEST_SKIP() << "format_8_vec4_snorm is not usable as a color_attachment | transfer_src image on this device";
    }

    // Direct create_info target: color_attachment | transfer_src, NO sampled and
    // NO heap registration (avoids the transfer_src readback layout-tracking
    // conflict, VUID-vkCmdDraw-None-09600).
    const std::shared_ptr<erhe::graphics::Texture> color_target = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info{
            .device      = graphics_device,
            .usage_mask  = usage_mask,
            .type        = erhe::graphics::Texture_type::texture_2d,
            .pixelformat = color_format,
            .width       = width,
            .height      = height,
            .debug_label = erhe::utility::Debug_label{"snorm color target"}
        }
    );

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            command_buffer.transition_texture_layout(*color_target, erhe::graphics::Image_layout::transfer_src_optimal);
        }
    );

    const erhe::graphics::Bind_group_layout empty_layout{
        graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"snorm empty layout"},
            .uses_texture_heap = false
        }
    };
    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "snorm_render",
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source}   },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source} }
        },
        .bind_group_layout = &empty_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(graphics_device, shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "snorm render shader failed to compile/link";
    erhe::graphics::Shader_stages shader_stages{graphics_device, std::move(prototype)};

    erhe::graphics::Render_pass_descriptor descriptor{};
    descriptor.color_attachments[0].texture       = color_target.get();
    descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 0.0 };
    descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.render_target_width  = width;
    descriptor.render_target_height = height;
    descriptor.debug_label = erhe::utility::Debug_label{"snorm render"};

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
    const erhe::graphics::Render_pipeline pipeline{graphics_device, pipeline_create_info};
    ASSERT_TRUE(pipeline.is_valid()) << "snorm render pipeline is not valid";

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

    // Read the raw bytes (4 bytes/texel) and reinterpret as signed int8.
    const std::vector<std::byte> raw = read_texture_color_bytes(*color_target, 4u);
    ASSERT_EQ(raw.size(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);

    const std::array<float, 4> expected{ -1.0f, -0.5f, 0.0f, 1.0f };
    int   bad      = 0;
    float max_diff = 0.0f;
    for (std::size_t t = 0; (t + 3u) < raw.size(); t += 4u) {
        for (std::size_t c = 0; c < 4u; ++c) {
            const int8_t stored  = static_cast<int8_t>(raw[t + c]);
            const float  decoded = erhe::dataformat::snorm8_to_float(stored);
            const float  diff    = std::fabs(decoded - expected[c]);
            if (diff > max_diff) {
                max_diff = diff;
            }
            // snorm8 quantizes to steps of 1/127 (~0.0079); -0.5 lands on -63 or
            // -64 depending on the rounding mode, both within one step of -0.5.
            // A tolerance of 0.01 (> 1/127) covers either rounding without
            // admitting a wrong sign or a clamped result.
            if (diff > 0.01f) {
                ++bad;
            }
        }
    }
    EXPECT_EQ(bad, 0) << bad << " snorm components decoded outside tolerance of {-1.0, -0.5, 0.0, 1.0} (max diff "
                      << max_diff << ")";

    // Spot-check the first texel's stored signed bytes for a readable message.
    ASSERT_GE(raw.size(), 4u);
    const int8_t s0 = static_cast<int8_t>(raw[0]);
    const int8_t s1 = static_cast<int8_t>(raw[1]);
    const int8_t s2 = static_cast<int8_t>(raw[2]);
    const int8_t s3 = static_cast<int8_t>(raw[3]);
    EXPECT_EQ(static_cast<int>(s0), -127) << "-1.0 should encode to snorm8 -127";
    EXPECT_NEAR(static_cast<int>(s1), -64, 1) << "-0.5 should encode to snorm8 ~ -64 (or -63)";
    EXPECT_EQ(static_cast<int>(s2),    0) << "0.0 should encode to snorm8 0";
    EXPECT_EQ(static_cast<int>(s3),  127) << "1.0 should encode to snorm8 127";
}

} // namespace erhe::graphics::test
