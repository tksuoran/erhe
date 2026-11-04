// Fragment invocation capture (doc/plans/shader_cost.md A1): one frame binds
// two programs in three ranges with known coverage; the capture returns one
// range per bind with the program's name, defines and invocations.

#include "gpu_test_fixture.hpp"

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/fragment_invocation_capture.hpp"
#include "erhe_graphics/fragment_output.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/color_blend_state.hpp"
#include "erhe_graphics/texture.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

constexpr const char* c_vertex_source = R"glsl(
void main()
{
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)glsl";

constexpr const char* c_fragment_source = R"glsl(
void main()
{
    out_color = FRAG_COLOR;
}
)glsl";

class Fragment_invocation_capture_test : public erhe::graphics::test::Gpu_test
{
};

TEST_F(Fragment_invocation_capture_test, counts_per_pipeline_bind_range)
{
    erhe::graphics::Device& graphics_device = device();
    if (!graphics_device.get_info().pipeline_statistics_query) {
        EXPECT_FALSE(graphics_device.request_fragment_invocation_capture());
        GTEST_SKIP() << "the device has no pipeline statistics queries";
    }

    constexpr int width  = 16;
    constexpr int height = 16;
    const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(width, height);

    const erhe::graphics::Bind_group_layout empty_layout{
        graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"fragment capture empty layout"},
            .uses_texture_heap = false
        }
    };
    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };
    const auto make_stages = [&](const char* name, const char* color) -> std::unique_ptr<erhe::graphics::Shader_stages> {
        erhe::graphics::Shader_stages_create_info create_info{
            .name             = name,
            .defines          = { { "FRAG_COLOR", color } },
            .fragment_outputs = &fragment_outputs,
            .no_vertex_input  = true,
            .shaders = {
                { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source}   },
                { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source} }
            },
            .bind_group_layout = &empty_layout
        };
        erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(graphics_device, create_info);
        if (!prototype.is_valid()) {
            return {};
        }
        return std::make_unique<erhe::graphics::Shader_stages>(graphics_device, std::move(prototype));
    };
    const std::unique_ptr<erhe::graphics::Shader_stages> stages_a = make_stages("capture_a", "vec4(1.0, 0.0, 0.0, 1.0)");
    const std::unique_ptr<erhe::graphics::Shader_stages> stages_b = make_stages("capture_b", "vec4(0.0, 1.0, 0.0, 1.0)");
    ASSERT_TRUE(stages_a && stages_b);
    EXPECT_EQ(stages_a->get_variant_defines().size(), 1u);

    erhe::graphics::Render_pass_descriptor descriptor{};
    descriptor.color_attachments[0].texture       = color_target.get();
    descriptor.color_attachments[0].clear_value   = {0.0, 0.0, 0.0, 1.0};
    descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.render_target_width  = width;
    descriptor.render_target_height = height;
    descriptor.debug_label = erhe::utility::Debug_label{"fragment capture pass"};

    const erhe::graphics::Color_blend_state color_blend = erhe::graphics::Color_blend_state::color_blend_disabled;
    const auto make_pipeline = [&](const erhe::graphics::Shader_stages& stages, const char* label) {
        erhe::graphics::Render_pipeline_create_info create_info;
        create_info.base.debug_label                       = erhe::utility::Debug_label{label};
        create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
        create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
        create_info.base.depth_stencil.depth_test_enable   = false;
        create_info.base.depth_stencil.depth_write_enable  = false;
        create_info.base.depth_stencil.stencil_test_enable = false;
        create_info.base.bind_group_layout                 = &empty_layout;
        create_info.base.color_blend                       = &color_blend;
        create_info.shader_stages                          = &stages;
        create_info.vertex_input                           = nullptr;
        create_info.set_format_from_render_pass(descriptor);
        return std::make_unique<erhe::graphics::Render_pipeline>(graphics_device, create_info);
    };
    const std::unique_ptr<erhe::graphics::Render_pipeline> pipeline_a = make_pipeline(*stages_a, "capture pipeline a");
    const std::unique_ptr<erhe::graphics::Render_pipeline> pipeline_b = make_pipeline(*stages_b, "capture pipeline b");
    ASSERT_TRUE(pipeline_a->is_valid() && pipeline_b->is_valid());

    ASSERT_TRUE(graphics_device.request_fragment_invocation_capture());
    EXPECT_FALSE(graphics_device.request_fragment_invocation_capture()); // one capture in flight
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass            render_pass{graphics_device, descriptor};
            erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, width, height);
            encoder.set_bind_group_layout(&empty_layout);
            encoder.set_scissor_rect(0, 0, width, height);
            encoder.set_render_pipeline(*pipeline_a);
            encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3); // 256 pixels
            encoder.set_scissor_rect(0, 0, 4, 4);
            encoder.set_render_pipeline(*pipeline_b);
            encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3); // 16 pixels
            encoder.set_scissor_rect(4, 4, 2, 2);
            encoder.set_render_pipeline(*pipeline_a);
            encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3); // 4 pixels
        }
    );
    EXPECT_FALSE(graphics_device.take_fragment_invocation_capture().has_value()); // the frame has not retired yet
    submit_and_wait([](erhe::graphics::Command_buffer&) {}); // retires the captured frame

    const std::optional<erhe::graphics::Fragment_invocation_capture> capture = graphics_device.take_fragment_invocation_capture();
    ASSERT_TRUE(capture.has_value());
    EXPECT_EQ(capture->dropped_ranges, 0u);
    ASSERT_EQ(capture->ranges.size(), 3u);
    const uint64_t expected[3] = {256u, 16u, 4u};
    const char*    programs[3] = {"capture_a", "capture_b", "capture_a"};
    const char*    labels  [3] = {"capture pipeline a", "capture pipeline b", "capture pipeline a"};
    for (std::size_t i = 0; i < 3; ++i) {
        const erhe::graphics::Fragment_invocation_range& range = capture->ranges[i];
        EXPECT_EQ(range.program, programs[i]) << "range " << i;
        EXPECT_EQ(range.pipeline, labels[i]) << "range " << i;
        EXPECT_EQ(range.view_count, 1u);
        ASSERT_EQ(range.defines.size(), 1u);
        EXPECT_EQ(range.defines[0].first, "FRAG_COLOR");
        EXPECT_EQ(range.invocations, expected[i]) << "range " << i << " (" << range.program << ")";
    }
    EXPECT_FALSE(graphics_device.take_fragment_invocation_capture().has_value()); // taken once
}

} // namespace
