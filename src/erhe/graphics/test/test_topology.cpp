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
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

constexpr int c_size = 16;

// White, no vertex input. Both topology tests emit positions from gl_VertexIndex.
constexpr const char* c_fragment_source = R"glsl(
void main()
{
    out_color = vec4(1.0, 1.0, 1.0, 1.0);
}
)glsl";

// Four 1-pixel points placed at exact target pixel centres. gl_PointSize is
// written (via the explicit gl_PerVertex out block) so the Vulkan rasterizer
// produces a defined single-pixel point.
//
// Each point is emitted from a target pixel coordinate (TARGET_X<i>, TARGET_Y<i>)
// in image-memory space (row 0 == first row returned by read_texture_rgba8) via
// the pixel-centre mapping. For a target of size TARGET_SIZE and target pixel
// (px, py) the NDC that lands on that pixel's centre is:
//     ndc_x =  ((px + 0.5) / TARGET_SIZE) * 2 - 1
//     ndc_y = -(((py + 0.5) / TARGET_SIZE) * 2 - 1)
// The Y term is negated to match the render encoder's negative-height viewport
// (VK_KHR_maintenance1 Y-flip), so increasing py moves down in image memory.
// Mapping to pixel centres (offset +0.5) removes the half-pixel rasterization
// ambiguity, so each point lands on exactly one unambiguous texel.
constexpr const char* c_point_vertex_source = R"glsl(
out gl_PerVertex {
    vec4  gl_Position;
    float gl_PointSize;
};
float pixel_center_ndc(float pixel)
{
    return ((pixel + 0.5) / float(TARGET_SIZE)) * 2.0 - 1.0;
}
void main()
{
    vec2 target_pixels[4] = vec2[4](
        vec2(float(TARGET_X0), float(TARGET_Y0)),
        vec2(float(TARGET_X1), float(TARGET_Y1)),
        vec2(float(TARGET_X2), float(TARGET_Y2)),
        vec2(float(TARGET_X3), float(TARGET_Y3))
    );
    vec2 p = target_pixels[gl_VertexIndex];
    gl_Position  = vec4(pixel_center_ndc(p.x), -pixel_center_ndc(p.y), 0.0, 1.0);
    gl_PointSize = 1.0;
}
)glsl";

// A single horizontal segment through y = 0 (NDC), spanning most of the width.
// y = 0 maps to the centre row regardless of Y orientation.
constexpr const char* c_line_vertex_source = R"glsl(
void main()
{
    vec2 positions[2] = vec2[2](
        vec2(-0.9, 0.0),
        vec2( 0.9, 0.0)
    );
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
)glsl";

// Count texels whose red channel is lit (all draws emit white over a black clear).
auto count_lit_texels(const std::vector<uint8_t>& pixels, const int width, const int height) -> int
{
    int count = 0;
    for (int i = 0; i < width * height; ++i) {
        if (pixels[(static_cast<std::size_t>(i) * 4u) + 0u] > 127u) {
            ++count;
        }
    }
    return count;
}

} // namespace

// Primitive topology: point_list. Draws four 1-pixel points and asserts that
// exactly four texels are lit (one per point), including the Y-orientation
// independent centre texel. Exercises Input_assembly_state::point +
// Primitive_type::point.
TEST_F(Gpu_test, topology_point_list)
{
    const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(c_size, c_size);

    // Target pixel centres (image-memory coordinates: row 0 == first row returned
    // by read_texture_rgba8) for the four points. Chosen interior pixels so the
    // 1-pixel point cannot be clipped and the lit texels are unambiguous.
    struct Target_pixel { int x; int y; };
    constexpr std::array<Target_pixel, 4> target_pixels{
        Target_pixel{ 8,  8},
        Target_pixel{ 4,  4},
        Target_pixel{12,  4},
        Target_pixel{12, 12}
    };

    const erhe::graphics::Bind_group_layout empty_layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"topology point layout"},
            .uses_texture_heap = false
        }
    };
    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "topology_point",
        .defines          = {
            { "TARGET_SIZE", std::to_string(c_size)                },
            { "TARGET_X0",   std::to_string(target_pixels[0].x)    },
            { "TARGET_Y0",   std::to_string(target_pixels[0].y)    },
            { "TARGET_X1",   std::to_string(target_pixels[1].x)    },
            { "TARGET_Y1",   std::to_string(target_pixels[1].y)    },
            { "TARGET_X2",   std::to_string(target_pixels[2].x)    },
            { "TARGET_Y2",   std::to_string(target_pixels[2].y)    },
            { "TARGET_X3",   std::to_string(target_pixels[3].x)    },
            { "TARGET_Y3",   std::to_string(target_pixels[3].y)    }
        },
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_point_vertex_source} },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source}     }
        },
        .bind_group_layout = &empty_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "topology point shader failed to compile/link";
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
    descriptor.render_target_width  = c_size;
    descriptor.render_target_height = c_size;
    descriptor.debug_label = erhe::utility::Debug_label{"topology point"};

    erhe::graphics::Render_pipeline_create_info pipeline_create_info;
    pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::point;
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
    ASSERT_TRUE(pipeline.is_valid()) << "topology point pipeline is not valid";

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass            render_pass{device(), descriptor};
            erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, c_size, c_size);
            encoder.set_scissor_rect (0, 0, c_size, c_size);
            encoder.set_bind_group_layout(&empty_layout);
            encoder.set_render_pipeline(pipeline);
            encoder.draw_primitives(erhe::graphics::Primitive_type::point, 0, 4);
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*color_target);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(c_size) * static_cast<std::size_t>(c_size) * 4u);

    // Four 1-pixel points -> exactly four lit texels.
    EXPECT_EQ(count_lit_texels(pixels, c_size, c_size), 4);

    // Each point was placed at the centre of its chosen target pixel, so exactly
    // those four texels (and no others, per the count above) must be lit. This is
    // unambiguous: the pixel-centre mapping removes the half-pixel boundary case.
    auto is_lit = [&](const int x, const int y) -> bool {
        const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(c_size) + static_cast<std::size_t>(x)) * 4u;
        return pixels[index + 0u] > 127u;
    };
    for (const Target_pixel& target : target_pixels) {
        EXPECT_TRUE(is_lit(target.x, target.y))
            << "point at target pixel (" << target.x << ", " << target.y << ") was not lit";
    }
}

// Primitive topology: line_list. Draws one horizontal segment through the centre
// row and asserts coarse coverage along the run while the top and bottom rows stay
// dark (confirming a thin line, not a filled region). Exercises
// Input_assembly_state::line + Primitive_type::line.
TEST_F(Gpu_test, topology_line_list)
{
    const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(c_size, c_size);

    const erhe::graphics::Bind_group_layout empty_layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"topology line layout"},
            .uses_texture_heap = false
        }
    };
    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "topology_line",
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_line_vertex_source} },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source}    }
        },
        .bind_group_layout = &empty_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "topology line shader failed to compile/link";
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
    descriptor.render_target_width  = c_size;
    descriptor.render_target_height = c_size;
    descriptor.debug_label = erhe::utility::Debug_label{"topology line"};

    erhe::graphics::Render_pipeline_create_info pipeline_create_info;
    pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::line;
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
    ASSERT_TRUE(pipeline.is_valid()) << "topology line pipeline is not valid";

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass            render_pass{device(), descriptor};
            erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, c_size, c_size);
            encoder.set_scissor_rect (0, 0, c_size, c_size);
            encoder.set_bind_group_layout(&empty_layout);
            encoder.set_render_pipeline(pipeline);
            encoder.draw_primitives(erhe::graphics::Primitive_type::line, 0, 2);
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*color_target);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(c_size) * static_cast<std::size_t>(c_size) * 4u);

    // The segment spans ~0.9 of the half-width on each side -> ~0.9 * 16 ~= 14 texels.
    // Assert coarse coverage rather than an exact run to stay rasterizer-tolerant.
    const int lit = count_lit_texels(pixels, c_size, c_size);
    EXPECT_GE(lit, 8);
    EXPECT_LE(lit, c_size + 2);

    // All lit texels must lie on (at most) the centre rows: the very top and bottom
    // rows must be dark. This confirms the lit run is a thin horizontal line.
    auto row_is_dark = [&](const int y) -> bool {
        for (int x = 0; x < c_size; ++x) {
            const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(c_size) + static_cast<std::size_t>(x)) * 4u;
            if (pixels[index + 0u] > 127u) {
                return false;
            }
        }
        return true;
    };
    EXPECT_TRUE(row_is_dark(0));
    EXPECT_TRUE(row_is_dark(c_size - 1));
}

} // namespace erhe::graphics::test
