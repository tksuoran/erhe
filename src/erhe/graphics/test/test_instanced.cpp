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

// One instance per column. gl_InstanceIndex selects the column; the
// triangle-strip quad (4 vertices) is sized to one column wide and full
// height, then translated to column gl_InstanceIndex. The fragment color
// encodes the instance index in the red channel so the readback can verify
// each column was drawn by its own instance (not just "something filled the
// target"). INSTANCE_COUNT columns tile the whole [-1,1] NDC width.
constexpr const char* c_vertex_source = R"glsl(
layout(location = 0) flat out int v_instance;
void main()
{
    // Unit quad as a triangle strip: (0,0) (1,0) (0,1) (1,1) in [0,1]^2.
    vec2 corners[4] = vec2[4](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0));
    vec2 c = corners[gl_VertexIndex];

    float column_width = 2.0 / float(INSTANCE_COUNT);
    float x0 = -1.0 + (float(gl_InstanceIndex) * column_width);
    float x  = x0 + (c.x * column_width);
    float y  = -1.0 + (c.y * 2.0);
    gl_Position = vec4(x, y, 0.0, 1.0);
    v_instance = gl_InstanceIndex;
}
)glsl";

constexpr const char* c_fragment_source = R"glsl(
layout(location = 0) flat in int v_instance;
void main()
{
    // Red channel = (instance index + 1) * STEP so each column is a distinct,
    // exactly-representable 8-bit value; green channel constant to mark "drawn".
    float r = float((v_instance + 1) * STEP) / 255.0;
    out_color = vec4(r, 1.0, 0.0, 1.0);
}
)glsl";

} // namespace

// Instanced draw with a triangle-strip topology. Draws INSTANCE_COUNT
// instances of a 4-vertex strip quad, each translated to its own column via
// gl_InstanceIndex, tiling the viewport. Exercises draw_primitives with an
// instance_count > 1, gl_InstanceIndex, and Input_assembly_state::triangle_strip.
TEST_F(Gpu_test, instanced_triangle_strip_columns)
{
    constexpr int      width          = 16;
    constexpr int      height         = 16;
    constexpr uint32_t instance_count = 4;
    constexpr int      step           = 50; // red value per instance: 50,100,150,200
    constexpr int      column_width   = width / static_cast<int>(instance_count); // 4

    const std::shared_ptr<erhe::graphics::Texture> output = make_color_target(width, height);

    const erhe::graphics::Bind_group_layout empty_layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"instanced empty layout"},
            .uses_texture_heap = false
        }
    };
    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "instanced_strip",
        .defines          = { { "INSTANCE_COUNT", "4" }, { "STEP", "50" } },
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source}   },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source} }
        },
        .bind_group_layout = &empty_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "instanced-strip shader failed to compile/link";
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
    descriptor.debug_label = erhe::utility::Debug_label{"instanced"};

    erhe::graphics::Render_pipeline_create_info pipeline_create_info;
    pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle_strip;
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
    ASSERT_TRUE(pipeline.is_valid()) << "instanced pipeline is not valid";

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass            render_pass{device(), descriptor};
            erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, width, height);
            encoder.set_scissor_rect (0, 0, width, height);
            encoder.set_bind_group_layout(&empty_layout);
            encoder.set_render_pipeline(pipeline);
            // 4 strip vertices, instance_count instances.
            encoder.draw_primitives(erhe::graphics::Primitive_type::triangle_strip, 0, 4, instance_count);
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*output);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);

    // Each column band must carry the red value of its own instance and be
    // fully covered (green == 255). Column c spans x in [c*column_width,
    // (c+1)*column_width). Sample the center texel of each column band, every
    // row, to avoid any single-pixel seam at column boundaries.
    int bad = 0;
    for (uint32_t instance = 0; instance < instance_count; ++instance) {
        const int expected_r = static_cast<int>((instance + 1u) * static_cast<uint32_t>(step));
        const int x_center   = (static_cast<int>(instance) * column_width) + (column_width / 2);
        for (int y = 0; y < height; ++y) {
            const std::size_t i =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x_center)) * 4u;
            const int r = pixels[i + 0];
            const int g = pixels[i + 1];
            const int b = pixels[i + 2];
            const bool ok = (std::abs(r - expected_r) <= 2) && (g >= 250) && (b <= 5);
            if (!ok) {
                ++bad;
            }
        }
    }
    EXPECT_EQ(bad, 0)
        << bad << " sampled texels did not match their instance's expected color"
        << " (each of " << instance_count << " instances should fill its own column)";
}

} // namespace erhe::graphics::test
