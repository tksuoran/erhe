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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

// A corner-to-corner triangle (covers the x + y < 0 half-plane in clip space).
// Its hypotenuse is a diagonal edge that crosses many texel centers at
// sub-texel boundaries, so a multisampled rasterization produces texels with
// some-but-not-all samples covered along the edge.
constexpr const char* c_vertex_source = R"glsl(
void main()
{
    vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2(-1.0,  1.0)
    );
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)glsl";

// Pure green; the clear is pure red. After a 4x -> 1x average resolve, an
// edge texel whose samples are split between covered (green) and uncovered
// (red) reads back as a blend of the two -- intermediate R and G.
constexpr const char* c_fragment_source = R"glsl(
void main()
{
    out_color = vec4(0.0, 1.0, 0.0, 1.0);
}
)glsl";

constexpr int sample_count = 4;

} // namespace

// MSAA color render + resolve. Render a diagonal-edged triangle into a 4x
// multisampled color attachment, resolve it to a single-sample target, read the
// resolve target back, and assert that some texels along the edge have
// intermediate (averaged) color components -- which can only occur if the
// triangle was genuinely multisampled and the samples were averaged on resolve.
// A 1x render would produce only pure-green and pure-red texels with no
// in-between, so the presence of averaged edge texels is the discriminating
// signal. Guarded on 4x being a supported sample count for the color format.
TEST_F(Gpu_test, msaa_color_resolve)
{
    constexpr int width  = 32;
    constexpr int height = 32;
    constexpr erhe::dataformat::Format color_format = erhe::dataformat::Format::format_8_vec4_unorm;

    erhe::graphics::Device& graphics_device = device();

    // Gate on the device actually supporting a 4x multisampled 2D image of this
    // color format. texture_2d_sample_counts lists the supported counts (filled
    // from vkGetPhysicalDeviceImageFormatProperties2 for a color attachment).
    const erhe::graphics::Format_properties color_props = graphics_device.get_format_properties(color_format);
    const std::vector<int>& counts = color_props.texture_2d_sample_counts;
    if (!color_props.color_renderable ||
        (std::find(counts.begin(), counts.end(), sample_count) == counts.end())) {
        GTEST_SKIP() << "4x MSAA is not supported for format_8_vec4_unorm on this device";
    }

    // Multisampled color attachment (rendered into, then resolved). It is never
    // copied/sampled directly, so color_attachment usage only.
    const std::shared_ptr<erhe::graphics::Texture> msaa_color = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info{
            .device       = graphics_device,
            .usage_mask   = erhe::graphics::Image_usage_flag_bit_mask::color_attachment,
            .type         = erhe::graphics::Texture_type::texture_2d,
            .pixelformat  = color_format,
            .sample_count = sample_count,
            .width        = width,
            .height       = height,
            .debug_label  = erhe::utility::Debug_label{"MSAA color"}
        }
    );
    ASSERT_EQ(msaa_color->get_sample_count(), sample_count) << "device gave fewer samples than requested";

    // Single-sample resolve target: color_attachment (resolve writes it as a
    // color attachment) + transfer_src (read it back). NOT sampled and NOT heap
    // registered, matching the readback-target pattern in the float32 test.
    const std::shared_ptr<erhe::graphics::Texture> resolve_target = std::make_shared<erhe::graphics::Texture>(
        graphics_device,
        erhe::graphics::Texture_create_info{
            .device      = graphics_device,
            .usage_mask  =
                erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_src,
            .type        = erhe::graphics::Texture_type::texture_2d,
            .pixelformat = color_format,
            .width       = width,
            .height      = height,
            .debug_label = erhe::utility::Debug_label{"MSAA resolve target"}
        }
    );

    const erhe::graphics::Bind_group_layout empty_layout{
        graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"MSAA empty layout"},
            .uses_texture_heap = false
        }
    };
    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "msaa_resolve",
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source}   },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source} }
        },
        .bind_group_layout = &empty_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(graphics_device, shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "MSAA resolve shader failed to compile/link";
    erhe::graphics::Shader_stages shader_stages{graphics_device, std::move(prototype)};

    // Color attachment is the multisampled image; the resolve happens into
    // resolve_target via store_action = Multisample_resolve. layout_after on the
    // color attachment controls the RESOLVE target's final layout (the MSAA
    // source is discarded), so set it to transfer_src_optimal -- that leaves the
    // resolve target ready for read_texture_rgba8 and the tracked layout in sync.
    erhe::graphics::Render_pass_descriptor descriptor{};
    descriptor.color_attachments[0].texture        = msaa_color.get();
    descriptor.color_attachments[0].resolve_texture = resolve_target.get();
    descriptor.color_attachments[0].clear_value    = std::array<double, 4>{ 1.0, 0.0, 0.0, 1.0 }; // red
    descriptor.color_attachments[0].load_action    = erhe::graphics::Load_action::Clear;
    descriptor.color_attachments[0].store_action   = erhe::graphics::Store_action::Multisample_resolve;
    descriptor.color_attachments[0].usage_before   = erhe::graphics::Image_usage_flag_bit_mask::color_attachment;
    descriptor.color_attachments[0].layout_before  = erhe::graphics::Image_layout::undefined;
    descriptor.color_attachments[0].usage_after    = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_after   = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.render_target_width  = width;
    descriptor.render_target_height = height;
    descriptor.debug_label = erhe::utility::Debug_label{"MSAA resolve"};

    // Pipeline sample count is derived from the multisampled color attachment by
    // set_format_from_render_pass; it must match the render pass's sample count.
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
    ASSERT_EQ(pipeline_create_info.sample_count, static_cast<unsigned int>(sample_count))
        << "pipeline sample count did not match the multisampled attachment";
    const erhe::graphics::Render_pipeline pipeline{graphics_device, pipeline_create_info};
    ASSERT_TRUE(pipeline.is_valid()) << "MSAA resolve pipeline is not valid";

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass            render_pass{graphics_device, descriptor};
            erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            ASSERT_EQ(render_pass.get_sample_count(), static_cast<unsigned int>(sample_count));
            encoder.set_viewport_rect(0, 0, width, height);
            encoder.set_scissor_rect (0, 0, width, height);
            encoder.set_bind_group_layout(&empty_layout);
            encoder.set_render_pipeline(pipeline);
            encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*resolve_target);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);

    int pure_green   = 0; // fully covered: (~0, ~255, 0)
    int pure_red     = 0; // fully uncovered: (~255, 0, 0)
    int intermediate = 0; // edge: a partial average of green and red
    for (int i = 0; i < width * height; ++i) {
        const int r = pixels[(static_cast<std::size_t>(i) * 4u) + 0];
        const int g = pixels[(static_cast<std::size_t>(i) * 4u) + 1];
        const int b = pixels[(static_cast<std::size_t>(i) * 4u) + 2];
        EXPECT_LE(b, 2) << "texel " << i << " has unexpected blue " << b;
        if ((g >= 250) && (r <= 5)) {
            ++pure_green;
        } else if ((r >= 250) && (g <= 5)) {
            ++pure_red;
        } else if ((r >= 20) && (r <= 235) && (g >= 20) && (g <= 235)) {
            // Both channels in a clearly-intermediate band: only possible if
            // this texel averaged some green and some red samples.
            ++intermediate;
        }
    }

    // A genuine 4x multisample + average resolve yields all three populations.
    // The decisive assertion is the presence of intermediate edge texels.
    EXPECT_GT(pure_green,   0) << "no fully-covered (green) texels -- triangle did not render";
    EXPECT_GT(pure_red,     0) << "no fully-uncovered (red) texels -- triangle covered everything";
    EXPECT_GT(intermediate, 0)
        << "no intermediate edge texels: the resolve produced only pure colors, "
           "which means multisampling/averaging did not occur (a 1x render)";
}

} // namespace erhe::graphics::test
