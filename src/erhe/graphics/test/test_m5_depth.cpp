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
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

// Two fullscreen triangles in one 6-vertex draw. Triangle 0 (gl_VertexIndex
// 0..2) is the near triangle at NEAR_Z; triangle 1 (3..5) is the far triangle
// at FAR_Z. A flat per-triangle index is forwarded so the fragment shader
// colors triangle 0 green and triangle 1 red.
constexpr const char* c_vertex_source = R"glsl(
layout(location = 0) flat out int v_tri;
void main()
{
    vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    int tri = gl_VertexIndex / 3;
    int v   = gl_VertexIndex - (tri * 3);
    float z = (tri == 0) ? NEAR_Z : FAR_Z;
    gl_Position = vec4(positions[v], z, 1.0);
    v_tri = tri;
}
)glsl";

constexpr const char* c_fragment_source = R"glsl(
layout(location = 0) flat in int v_tri;
void main()
{
    out_color = (v_tri == 0) ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);
}
)glsl";

} // namespace

// Milestone 5 (state coverage): depth testing. The near (green) triangle is
// drawn first, then the far (red) triangle over the same full-viewport area.
// With depth testing enabled the far triangle must fail the depth test and be
// rejected, so the whole target is green. If depth testing were broken the
// later (far, red) draw would overwrite. Proves the result is governed by depth
// and not by draw order. Exercises a depth attachment + depth-test state.
TEST_F(Gpu_test, depth_test_nearer_wins)
{
    constexpr int width  = 16;
    constexpr int height = 16;

    const bool   reverse_depth = device().get_reverse_depth();
    const float  near_z        = reverse_depth ? 0.9f : 0.1f;
    const float  far_z         = reverse_depth ? 0.1f : 0.9f;
    const double depth_clear   = reverse_depth ? 0.0  : 1.0; // cleared to "far"

    const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(width, height);

    const std::shared_ptr<erhe::graphics::Texture> depth_target = std::make_shared<erhe::graphics::Texture>(
        device(),
        erhe::graphics::Texture_create_info{
            .device      = device(),
            .usage_mask  = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment,
            .type        = erhe::graphics::Texture_type::texture_2d,
            .pixelformat = erhe::dataformat::Format::format_d32_sfloat,
            .width       = width,
            .height      = height,
            .debug_label = erhe::utility::Debug_label{"M5 depth target"}
        }
    );

    const erhe::graphics::Bind_group_layout empty_layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"M5 depth empty layout"},
            .uses_texture_heap = false
        }
    };

    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "m5_depth",
        .defines          = { { "NEAR_Z", std::to_string(near_z) }, { "FAR_Z", std::to_string(far_z) } },
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source}   },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source} }
        },
        .bind_group_layout = &empty_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "M5 depth shader failed to compile/link";
    erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

    erhe::graphics::Render_pass_descriptor descriptor{};
    descriptor.color_attachments[0].texture       = color_target.get();
    descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
    descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.depth_attachment.texture           = depth_target.get();
    descriptor.depth_attachment.clear_value[0]    = depth_clear;
    descriptor.depth_attachment.load_action       = erhe::graphics::Load_action::Clear;
    descriptor.depth_attachment.store_action      = erhe::graphics::Store_action::Dont_care;
    descriptor.depth_attachment.usage_before      = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
    descriptor.depth_attachment.layout_before     = erhe::graphics::Image_layout::undefined;
    descriptor.depth_attachment.usage_after       = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
    descriptor.depth_attachment.layout_after      = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
    descriptor.render_target_width  = width;
    descriptor.render_target_height = height;
    descriptor.debug_label = erhe::utility::Debug_label{"M5 depth"};

    erhe::graphics::Render_pipeline_create_info pipeline_create_info;
    pipeline_create_info.base.input_assembly    = erhe::graphics::Input_assembly_state::triangle;
    pipeline_create_info.base.rasterization     = erhe::graphics::Rasterization_state::cull_mode_none;
    pipeline_create_info.base.depth_stencil     =
        erhe::graphics::Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth);
    pipeline_create_info.base.bind_group_layout = &empty_layout;
    pipeline_create_info.base.color_blend       = &erhe::graphics::Color_blend_state::color_blend_disabled;
    pipeline_create_info.shader_stages          = &shader_stages;
    pipeline_create_info.vertex_input           = nullptr;
    pipeline_create_info.set_format_from_render_pass(descriptor);
    const erhe::graphics::Render_pipeline pipeline{device(), pipeline_create_info};
    ASSERT_TRUE(pipeline.is_valid()) << "M5 depth pipeline is not valid";

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass            render_pass{device(), descriptor};
            erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, width, height);
            encoder.set_scissor_rect (0, 0, width, height);
            encoder.set_bind_group_layout(&empty_layout);
            encoder.set_render_pipeline(pipeline);
            encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 6); // two triangles
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*color_target);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);

    int green_count = 0;
    int red_count   = 0;
    int other_count = 0;
    for (int i = 0; i < width * height; ++i) {
        const int r = pixels[(static_cast<std::size_t>(i) * 4u) + 0];
        const int g = pixels[(static_cast<std::size_t>(i) * 4u) + 1];
        const int b = pixels[(static_cast<std::size_t>(i) * 4u) + 2];
        if ((g >= 250) && (r <= 5) && (b <= 5)) {
            ++green_count;
        } else if ((r >= 250) && (g <= 5) && (b <= 5)) {
            ++red_count;
        } else {
            ++other_count;
        }
    }

    EXPECT_EQ(other_count, 0) << "every texel should be exactly green or red";
    EXPECT_EQ(red_count, 0)   << "the far (red) triangle should have been rejected by the depth test";
    EXPECT_EQ(green_count, width * height) << "the near (green) triangle should cover the whole target";
}

} // namespace erhe::graphics::test
