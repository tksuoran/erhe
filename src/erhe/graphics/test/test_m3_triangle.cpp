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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

// No #version and no vertex-input declarations: erhe injects #version, and
// with no_vertex_input = true it injects no vertex attributes, so the triangle
// is emitted from gl_VertexIndex. erhe injects the out_color declaration from
// fragment_outputs. The triangle covers the half-plane x + y < 0 in clip space
// (vertices at three corners), i.e. roughly half the target.
constexpr const char* c_vertex_source = R"glsl(
void main()
{
    vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2(-1.0,  1.0)
    );
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
)glsl";

constexpr const char* c_fragment_source = R"glsl(
void main()
{
    out_color = vec4(0.0, 1.0, 0.0, 1.0);
}
)glsl";

} // namespace

// Milestone 3: draw a single solid-green triangle into an offscreen target via
// inline GLSL (no vertex buffer, no descriptors), read it back, and assert
// partial coverage: every texel is either green (triangle) or black (clear),
// with the green fraction near one half.
TEST_F(Gpu_test, triangle_draw_coverage)
{
    constexpr int width  = 32;
    constexpr int height = 32;

    const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(width, height);

    // Empty bind group layout: the shader uses no UBO/SSBO/sampler, so the
    // pipeline statically uses no descriptor set (no VUID-vkCmdDraw-None-08600).
    const erhe::graphics::Bind_group_layout empty_layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"M3 empty layout"},
            .uses_texture_heap = false
        }
    };

    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "m3_triangle",
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source}   },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source} }
        },
        .bind_group_layout = &empty_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "M3 triangle shader failed to compile/link";
    erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

    // Offscreen color target, cleared to black.
    erhe::graphics::Render_pass_descriptor descriptor{};
    descriptor.color_attachments[0].texture       = color_target.get();
    descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
    descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.render_target_width  = width;
    descriptor.render_target_height = height;
    descriptor.debug_label = erhe::utility::Debug_label{"M3 triangle"};

    // Build the pipeline directly (no Render_pipeline_state global registry).
    erhe::graphics::Render_pipeline_create_info pipeline_create_info;
    pipeline_create_info.base.input_assembly             = erhe::graphics::Input_assembly_state::triangle;
    pipeline_create_info.base.rasterization              = erhe::graphics::Rasterization_state::cull_mode_none;
    pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
    pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
    pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
    pipeline_create_info.base.bind_group_layout          = &empty_layout;
    pipeline_create_info.base.color_blend                = &erhe::graphics::Color_blend_state::color_blend_disabled;
    pipeline_create_info.shader_stages                   = &shader_stages;
    pipeline_create_info.vertex_input                    = nullptr;
    pipeline_create_info.set_format_from_render_pass(descriptor);
    const erhe::graphics::Render_pipeline pipeline{device(), pipeline_create_info};
    ASSERT_TRUE(pipeline.is_valid()) << "M3 pipeline is not valid";

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

    const std::vector<uint8_t> pixels = read_texture_rgba8(*color_target);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);

    int green_count = 0;
    int clear_count = 0;
    int other_count = 0;
    for (int i = 0; i < width * height; ++i) {
        const int r = pixels[(static_cast<std::size_t>(i) * 4u) + 0];
        const int g = pixels[(static_cast<std::size_t>(i) * 4u) + 1];
        const int b = pixels[(static_cast<std::size_t>(i) * 4u) + 2];
        if ((g >= 250) && (r <= 5) && (b <= 5)) {
            ++green_count;
        } else if ((r <= 5) && (g <= 5) && (b <= 5)) {
            ++clear_count;
        } else {
            ++other_count;
        }
    }

    const int total = width * height;
    EXPECT_EQ(other_count, 0) << "every texel should be exactly green or black";
    EXPECT_GT(green_count, 0) << "triangle produced no covered texels";
    EXPECT_GT(clear_count, 0) << "triangle covered the whole target";
    // A corner-to-corner triangle covers about half the target.
    EXPECT_GE(green_count, (total * 30) / 100) << "coverage too low";
    EXPECT_LE(green_count, (total * 70) / 100) << "coverage too high";
}

} // namespace erhe::graphics::test
