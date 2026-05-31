#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
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
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

// Fullscreen (oversized) triangle emitted at a constant NDC depth DRAW_Z. This
// is a depth-only pass (no color attachment), so the fragment shader declares no
// outputs and writes nothing; the point of the draw is to stamp DRAW_Z into the
// depth buffer via depth_write_enable.
constexpr const char* c_vertex_source = R"glsl(
void main()
{
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexID], DRAW_Z, 1.0);
}
)glsl";

constexpr const char* c_fragment_source = R"glsl(
void main()
{
}
)glsl";

} // namespace

// Depth-texture readback. Render a depth-only pass that writes a known NDC depth
// across the whole target, then copy the depth aspect back to the host and assert
// the read-back float depth equals the drawn NDC depth. The render pass leaves the
// depth texture in transfer_src_optimal (usage_after / layout_after = transfer_src)
// so the readback blit is correctly ordered behind the render pass's own barrier.
//
// Proves: depth_write_enable stamps the rasterized NDC depth into the depth image,
// and the depth aspect survives a texture->buffer copy unchanged.
TEST_F(Gpu_test, depth_texture_readback)
{
    constexpr int width  = 16;
    constexpr int height = 16;

    erhe::graphics::Device& graphics_device = device();

    // We want a PURE 32-bit-float depth format (no stencil) so the depth aspect
    // copies out as a tightly-packed 4-byte float per texel.
    // choose_depth_stencil_format(require_depth, ...) is not suitable here: its
    // heuristic happily returns a combined depth+stencil format (e.g.
    // format_d32_sfloat_s8_uint) when one is supported, which has a separate
    // stencil aspect and is not a clean float-per-texel readback. Instead pick
    // the first supported, depth-renderable, 32-bit, stencil-free format from the
    // device's supported list (format_d32_sfloat on this device).
    erhe::dataformat::Format depth_format = erhe::dataformat::Format::format_undefined;
    const std::vector<erhe::dataformat::Format> supported = graphics_device.get_supported_depth_stencil_formats();
    for (const erhe::dataformat::Format candidate : supported) {
        if ((erhe::dataformat::get_depth_size_bits(candidate) == 32) &&
            (erhe::dataformat::get_stencil_size_bits(candidate) == 0) &&
            graphics_device.get_format_properties(candidate).depth_renderable) {
            depth_format = candidate;
            break;
        }
    }
    if (depth_format == erhe::dataformat::Format::format_undefined) {
        GTEST_SKIP() << "no pure 32-bit-float depth-renderable format available on this device";
    }

    const bool   reverse_depth = graphics_device.get_reverse_depth();
    // Drawn NDC depth in the untouched-region-distinct range. With reverse-Z the
    // depth range is still [0,1] in framebuffer space; gl_Position.z / w lands in
    // [0,1] regardless. Pick 0.25 so it differs from both the 0.0 and 1.0 clears.
    const float  draw_z      = 0.25f;
    // Clear depth to the "far" plane: 1.0 normally, 0.0 under reverse-Z. The
    // depth test is "always", so every fragment overwrites the clear with draw_z.
    const double depth_clear = reverse_depth ? 0.0 : 1.0;

    // Depth-only target: depth_stencil_attachment (render to it) + transfer_src
    // (copy the depth aspect out). No sampled usage, no color usage.
    const std::shared_ptr<erhe::graphics::Texture> depth_target = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info{
            .device      = graphics_device,
            .usage_mask  =
                erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_src,
            .type        = erhe::graphics::Texture_type::texture_2d,
            .pixelformat = depth_format,
            .width       = width,
            .height      = height,
            .debug_label = erhe::utility::Debug_label{"depth readback target"}
        }
    );

    const erhe::graphics::Bind_group_layout empty_layout{
        graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"depth readback empty layout"},
            .uses_texture_heap = false
        }
    };

    // Depth-only pass: no color attachment, so no fragment outputs.
    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "depth_readback",
        .defines          = { { "DRAW_Z", std::to_string(draw_z) } },
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source}   },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source} }
        },
        .bind_group_layout = &empty_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(graphics_device, shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "depth readback shader failed to compile/link";
    erhe::graphics::Shader_stages shader_stages{graphics_device, std::move(prototype)};

    // Depth-only render pass: no color attachment. The depth attachment's
    // usage_after / layout_after = transfer_src / transfer_src_optimal makes the
    // render pass emit the barrier that hands the depth image to the readback blit.
    erhe::graphics::Render_pass_descriptor descriptor{};
    descriptor.depth_attachment.texture           = depth_target.get();
    descriptor.depth_attachment.clear_value[0]    = depth_clear;
    descriptor.depth_attachment.load_action       = erhe::graphics::Load_action::Clear;
    descriptor.depth_attachment.store_action      = erhe::graphics::Store_action::Store;
    descriptor.depth_attachment.usage_before      = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
    descriptor.depth_attachment.layout_before     = erhe::graphics::Image_layout::undefined;
    descriptor.depth_attachment.usage_after       = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.depth_attachment.layout_after      = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.render_target_width  = width;
    descriptor.render_target_height = height;
    descriptor.debug_label = erhe::utility::Debug_label{"depth readback"};

    // Depth-test "always" so draw_z always wins over the clear, depth_write on.
    erhe::graphics::Depth_stencil_state depth_stencil{};
    depth_stencil.depth_test_enable   = true;
    depth_stencil.depth_write_enable  = true;
    depth_stencil.depth_compare_op    = erhe::graphics::Compare_operation::always;
    depth_stencil.stencil_test_enable = false;

    erhe::graphics::Render_pipeline_create_info pipeline_create_info;
    pipeline_create_info.base.input_assembly    = erhe::graphics::Input_assembly_state::triangle;
    pipeline_create_info.base.rasterization     = erhe::graphics::Rasterization_state::cull_mode_none;
    pipeline_create_info.base.depth_stencil     = depth_stencil;
    pipeline_create_info.base.bind_group_layout = &empty_layout;
    pipeline_create_info.base.color_blend       = &erhe::graphics::Color_blend_state::color_writes_disabled;
    pipeline_create_info.shader_stages          = &shader_stages;
    pipeline_create_info.vertex_input           = nullptr;
    pipeline_create_info.set_format_from_render_pass(descriptor);
    const erhe::graphics::Render_pipeline pipeline{graphics_device, pipeline_create_info};
    ASSERT_TRUE(pipeline.is_valid()) << "depth readback pipeline is not valid";

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

    const std::vector<float> depths = read_texture_depth32f(*depth_target);
    ASSERT_EQ(depths.size(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

    // Every texel was covered by the fullscreen triangle, so all should read
    // back as draw_z (within float tolerance). A 16-bit-or-coarser depth would
    // be skipped above, so a tight tolerance is appropriate for f32 depth.
    int   bad      = 0;
    float max_diff = 0.0f;
    for (const float d : depths) {
        const float diff = std::fabs(d - draw_z);
        if (diff > max_diff) {
            max_diff = diff;
        }
        if (diff > 1.0e-5f) {
            ++bad;
        }
    }
    EXPECT_EQ(bad, 0) << bad << " of " << depths.size() << " depth texels differed from the drawn NDC depth "
                      << draw_z << " (max diff " << max_diff << ")";
}

} // namespace erhe::graphics::test
