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
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

constexpr int c_size = 16;

// Centered half-size quad as a triangle strip (covers the inner [0.25,0.75]
// region of the target in both axes). Used by the stencil-writing draw.
constexpr const char* c_inner_quad_vertex_source = R"glsl(
void main()
{
    vec2 corners[4] = vec2[4](
        vec2(-0.5, -0.5),
        vec2( 0.5, -0.5),
        vec2(-0.5,  0.5),
        vec2( 0.5,  0.5)
    );
    gl_Position = vec4(corners[gl_VertexIndex], 0.0, 1.0);
}
)glsl";

// Fullscreen triangle. Used by the stencil-tested draw that covers the whole
// target but is masked to where stencil == 1.
constexpr const char* c_fullscreen_vertex_source = R"glsl(
void main()
{
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
)glsl";

constexpr const char* c_white_fragment_source = R"glsl(
void main()
{
    out_color = vec4(1.0, 1.0, 1.0, 1.0);
}
)glsl";

auto build_white_shader(erhe::graphics::Device& device, const char* vertex_source, const erhe::graphics::Bind_group_layout& layout, const erhe::graphics::Fragment_outputs& fragment_outputs, const char* name)
    -> erhe::graphics::Shader_stages
{
    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = name,
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{vertex_source}        },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_white_fragment_source} }
        },
        .bind_group_layout = &layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device, shader_create_info);
    EXPECT_TRUE(prototype.is_valid()) << name << " shader failed to compile/link";
    return erhe::graphics::Shader_stages{device, std::move(prototype)};
}

} // namespace

// Stencil coverage: a single render pass with two draws sharing one combined
// depth+stencil image. Draw 1 covers an inner sub-region and writes stencil = 1
// (stencil op replace, ref 1) with color writes disabled. Draw 2 covers the whole
// target but is stencil-tested equal ref 1, so it only writes white where draw 1
// stamped the stencil. Asserts the inner region is lit and the corners are dark,
// proving the result is governed by the stencil buffer and not by draw coverage.
TEST_F(Gpu_test, stencil_two_draw_mask)
{
    erhe::graphics::Device& graphics_device = device();

    // lavapipe does not support d24_unorm_s8; require a depth+stencil format and
    // skip if the device exposes none.
    const erhe::dataformat::Format ds_format = graphics_device.choose_depth_stencil_format(
        erhe::graphics::format_flag_require_depth | erhe::graphics::format_flag_require_stencil,
        1
    );
    if ((ds_format == erhe::dataformat::Format::format_undefined) ||
        (erhe::dataformat::get_stencil_size_bits(ds_format) == 0)) {
        GTEST_SKIP() << "no depth+stencil attachment format available on this device";
    }

    const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(c_size, c_size);

    const std::shared_ptr<erhe::graphics::Texture> depth_stencil_target = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info{
            .device      = graphics_device,
            .usage_mask  = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment,
            .type        = erhe::graphics::Texture_type::texture_2d,
            .pixelformat = ds_format,
            .width       = c_size,
            .height      = c_size,
            .debug_label = erhe::utility::Debug_label{"stencil depth+stencil target"}
        }
    );

    const erhe::graphics::Bind_group_layout empty_layout{
        graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"stencil empty layout"},
            .uses_texture_heap = false
        }
    };
    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    erhe::graphics::Shader_stages write_shader = build_white_shader(graphics_device, c_inner_quad_vertex_source, empty_layout, fragment_outputs, "stencil_write");
    erhe::graphics::Shader_stages test_shader  = build_white_shader(graphics_device, c_fullscreen_vertex_source, empty_layout, fragment_outputs, "stencil_test");

    erhe::graphics::Render_pass_descriptor descriptor{};
    descriptor.color_attachments[0].texture       = color_target.get();
    descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
    descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
    // Combined depth+stencil image: both the depth and the stencil aspect are the
    // same texture. The depth aspect is cleared (clear_value[0]) and the stencil
    // aspect is cleared to 0 (clear_value[1]).
    descriptor.depth_attachment.texture           = depth_stencil_target.get();
    descriptor.depth_attachment.clear_value[0]    = graphics_device.get_reverse_depth() ? 0.0 : 1.0;
    descriptor.depth_attachment.load_action       = erhe::graphics::Load_action::Clear;
    descriptor.depth_attachment.store_action      = erhe::graphics::Store_action::Dont_care;
    descriptor.depth_attachment.usage_before      = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
    descriptor.depth_attachment.layout_before     = erhe::graphics::Image_layout::undefined;
    descriptor.depth_attachment.usage_after       = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
    descriptor.depth_attachment.layout_after      = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
    descriptor.stencil_attachment.texture         = depth_stencil_target.get();
    descriptor.stencil_attachment.clear_value[0]  = 0.0; // stencil clear value
    descriptor.stencil_attachment.load_action     = erhe::graphics::Load_action::Clear;
    descriptor.stencil_attachment.store_action    = erhe::graphics::Store_action::Dont_care;
    descriptor.stencil_attachment.usage_before    = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
    descriptor.stencil_attachment.layout_before   = erhe::graphics::Image_layout::undefined;
    descriptor.stencil_attachment.usage_after     = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
    descriptor.stencil_attachment.layout_after    = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
    descriptor.render_target_width  = c_size;
    descriptor.render_target_height = c_size;
    descriptor.debug_label = erhe::utility::Debug_label{"stencil"};

    // Draw 1 pipeline: stencil always-pass, replace with ref 1, depth test off,
    // color writes disabled (only the stencil is written by this draw).
    erhe::graphics::Render_pipeline_create_info write_pipeline_create_info;
    write_pipeline_create_info.base.input_assembly                     = erhe::graphics::Input_assembly_state::triangle_strip;
    write_pipeline_create_info.base.rasterization                      = erhe::graphics::Rasterization_state::cull_mode_none;
    write_pipeline_create_info.base.depth_stencil.depth_test_enable    = false;
    write_pipeline_create_info.base.depth_stencil.depth_write_enable   = false;
    write_pipeline_create_info.base.depth_stencil.stencil_test_enable  = true;
    write_pipeline_create_info.base.depth_stencil.stencil_front.function   = erhe::graphics::Compare_operation::always;
    write_pipeline_create_info.base.depth_stencil.stencil_front.z_pass_op  = erhe::graphics::Stencil_op::replace;
    write_pipeline_create_info.base.depth_stencil.stencil_front.reference  = 1u;
    write_pipeline_create_info.base.depth_stencil.stencil_front.write_mask = 0xffu;
    write_pipeline_create_info.base.depth_stencil.stencil_back            = write_pipeline_create_info.base.depth_stencil.stencil_front;
    write_pipeline_create_info.base.bind_group_layout                 = &empty_layout;
    write_pipeline_create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_writes_disabled;
    write_pipeline_create_info.shader_stages                          = &write_shader;
    write_pipeline_create_info.vertex_input                           = nullptr;
    write_pipeline_create_info.set_format_from_render_pass(descriptor);
    const erhe::graphics::Render_pipeline write_pipeline{graphics_device, write_pipeline_create_info};
    ASSERT_TRUE(write_pipeline.is_valid()) << "stencil write pipeline is not valid";

    // Draw 2 pipeline: stencil test equal ref 1 (keep), depth test off, color
    // writes enabled. Only fragments where stencil == 1 survive.
    erhe::graphics::Render_pipeline_create_info test_pipeline_create_info;
    test_pipeline_create_info.base.input_assembly                     = erhe::graphics::Input_assembly_state::triangle;
    test_pipeline_create_info.base.rasterization                      = erhe::graphics::Rasterization_state::cull_mode_none;
    test_pipeline_create_info.base.depth_stencil.depth_test_enable    = false;
    test_pipeline_create_info.base.depth_stencil.depth_write_enable   = false;
    test_pipeline_create_info.base.depth_stencil.stencil_test_enable  = true;
    test_pipeline_create_info.base.depth_stencil.stencil_front.function   = erhe::graphics::Compare_operation::equal;
    test_pipeline_create_info.base.depth_stencil.stencil_front.z_pass_op  = erhe::graphics::Stencil_op::keep;
    test_pipeline_create_info.base.depth_stencil.stencil_front.reference  = 1u;
    test_pipeline_create_info.base.depth_stencil.stencil_front.test_mask  = 0xffu;
    test_pipeline_create_info.base.depth_stencil.stencil_back            = test_pipeline_create_info.base.depth_stencil.stencil_front;
    test_pipeline_create_info.base.bind_group_layout                 = &empty_layout;
    test_pipeline_create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
    test_pipeline_create_info.shader_stages                          = &test_shader;
    test_pipeline_create_info.vertex_input                           = nullptr;
    test_pipeline_create_info.set_format_from_render_pass(descriptor);
    const erhe::graphics::Render_pipeline test_pipeline{graphics_device, test_pipeline_create_info};
    ASSERT_TRUE(test_pipeline.is_valid()) << "stencil test pipeline is not valid";

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass            render_pass{graphics_device, descriptor};
            erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, c_size, c_size);
            encoder.set_scissor_rect (0, 0, c_size, c_size);
            encoder.set_bind_group_layout(&empty_layout);

            // Draw 1: stamp stencil = 1 in the inner region (color writes off).
            encoder.set_render_pipeline(write_pipeline);
            encoder.draw_primitives(erhe::graphics::Primitive_type::triangle_strip, 0, 4);

            // Draw 2: fill where stencil == 1 with white.
            encoder.set_render_pipeline(test_pipeline);
            encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*color_target);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(c_size) * static_cast<std::size_t>(c_size) * 4u);

    auto is_lit = [&](const int x, const int y) -> bool {
        const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(c_size) + static_cast<std::size_t>(x)) * 4u;
        return pixels[index + 0u] > 127u;
    };

    // The inner [0.25,0.75] NDC quad maps to roughly texels [4,12) in both axes.
    // The centre must be lit (stencil == 1 there) and the four corners must be dark
    // (stencil == 0 there, so draw 2 was rejected even though it covered them).
    EXPECT_TRUE(is_lit(c_size / 2, c_size / 2)) << "centre should pass the stencil test";
    EXPECT_FALSE(is_lit(0, 0))                  << "top-left corner should be masked out";
    EXPECT_FALSE(is_lit(c_size - 1, 0))         << "top-right corner should be masked out";
    EXPECT_FALSE(is_lit(0, c_size - 1))         << "bottom-left corner should be masked out";
    EXPECT_FALSE(is_lit(c_size - 1, c_size - 1))<< "bottom-right corner should be masked out";

    // Count lit texels: should be a contiguous inner block, well under the full
    // target and comfortably above zero. The inner quad is ~8x8 = ~64 texels.
    int lit = 0;
    for (int y = 0; y < c_size; ++y) {
        for (int x = 0; x < c_size; ++x) {
            if (is_lit(x, y)) {
                ++lit;
            }
        }
    }
    EXPECT_GE(lit, 30);
    EXPECT_LE(lit, 100);
}

} // namespace erhe::graphics::test
