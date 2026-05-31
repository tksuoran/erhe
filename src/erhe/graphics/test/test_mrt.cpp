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
#include <cstdlib>
#include <memory>
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

} // namespace erhe::graphics::test
