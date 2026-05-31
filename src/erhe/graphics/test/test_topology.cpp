#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"

#include <glm/glm.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace erhe::graphics::test {
namespace {

constexpr int target_size = 16;

// Count texels whose RGB is not (near) black. The clear colour is opaque black, so
// any lit fragment is detectable by a non-zero red channel (all draws emit white).
auto count_lit_texels(const std::vector<uint8_t>& pixels, int width, int height) -> int
{
    int count = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t index = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
            if (pixels[index + 0] > 127u) {
                ++count;
            }
        }
    }
    return count;
}

auto make_topology_pipeline(
    erhe::graphics::Device&             device,
    const erhe::graphics::Input_assembly_state& input_assembly,
    const char*                         vs_source
) -> std::shared_ptr<erhe::graphics::Render_pipeline_state>
{
    static const char* fs_source =
        "void main() {\n"
        "    out_color = vec4(1.0, 1.0, 1.0, 1.0);\n"
        "}\n";

    erhe::graphics::Shader_stages_create_info shader_stages_create_info{
        .device = device,
        .pipeline_stages = {
            { erhe::graphics::Shader_type::vertex_shader,   vs_source },
            { erhe::graphics::Shader_type::fragment_shader, fs_source }
        },
        .name = "topology test pipeline"
    };
    erhe::graphics::Fragment_outputs fragment_outputs;
    fragment_outputs.add("out_color", erhe::dataformat::Format::format_8_vec4_unorm, 0);
    shader_stages_create_info.fragment_outputs = &fragment_outputs;
    erhe::graphics::Shader_stages shader_stages{device, shader_stages_create_info};

    erhe::graphics::Render_pipeline_state pipeline_state{
        erhe::graphics::Render_pipeline_state::Create_info{
            .name           = "topology test pipeline",
            .shader_stages  = &shader_stages,
            .vertex_input   = nullptr,
            .input_assembly = input_assembly,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
            .depth_stencil  = erhe::graphics::Depth_stencil_state{
                .depth_test_enable   = false,
                .depth_write_enable  = false,
                .depth_compare_op    = erhe::graphics::Compare_operation::always,
                .stencil_test_enable = false
            },
            .color_blend    = erhe::graphics::Color_blend_state::color_blend_disabled
        }
    };
    return std::make_shared<erhe::graphics::Render_pipeline_state>(std::move(pipeline_state));
}

} // namespace

// Render a small set of points and assert that exactly that many texels are lit:
// each 1-pixel point should light exactly one texel. gl_PointSize is written so the
// Vulkan rasterizer produces a defined 1-pixel point.
TEST_F(Gpu_test, topology_point_list)
{
    erhe::graphics::Device& device = device();

    // Four points at distinct positions. The centre point (0,0) is independent of the
    // framebuffer Y orientation, the others are simply spread out so each lands on its
    // own texel.
    static const char* vs_source =
        "void main() {\n"
        "    vec2 positions[4] = vec2[4](\n"
        "        vec2( 0.0,  0.0),\n"
        "        vec2(-0.5, -0.5),\n"
        "        vec2( 0.5, -0.5),\n"
        "        vec2( 0.5,  0.5)\n"
        "    );\n"
        "    gl_Position  = vec4(positions[gl_VertexIndex], 0.0, 1.0);\n"
        "    gl_PointSize = 1.0;\n"
        "}\n";

    std::shared_ptr<erhe::graphics::Texture> color_texture = make_color_target(target_size, target_size);
    std::shared_ptr<erhe::graphics::Render_pass> render_pass = make_single_color_render_pass(color_texture.get(), 0.0, 0.0, 0.0, 1.0);

    std::shared_ptr<erhe::graphics::Render_pipeline_state> pipeline = make_topology_pipeline(device, erhe::graphics::Input_assembly_state::points, vs_source);

    erhe::graphics::Render_command_encoder encoder = device.make_render_command_encoder(*render_pass);
    encoder.set_render_pipeline_state(*pipeline);
    encoder.draw_primitives(erhe::graphics::Primitive_type::points, 0, 4);
    device.flush_render_command_encoder(encoder);
    device.wait_for_idle();

    std::vector<uint8_t> pixels = read_texture_rgba8(color_texture.get());

    // Exactly four 1-pixel points -> exactly four lit texels.
    EXPECT_EQ(count_lit_texels(pixels, target_size, target_size), 4);

    // The centre point is Y-orientation independent; it must be lit.
    const size_t center_index = ((static_cast<size_t>(target_size) / 2u) * static_cast<size_t>(target_size) + (static_cast<size_t>(target_size) / 2u)) * 4u;
    EXPECT_GT(pixels[center_index + 0], 127u);
}

// Render a single horizontal line through the vertical centre of the target and assert
// coarse coverage: many texels along the row are lit, and rows far from the centre stay
// dark.
TEST_F(Gpu_test, topology_line_list)
{
    erhe::graphics::Device& device = device();

    // A horizontal segment at y = 0 (NDC) spanning most of the width. y = 0 maps to the
    // centre row regardless of Y orientation.
    static const char* vs_source =
        "void main() {\n"
        "    vec2 positions[2] = vec2[2](\n"
        "        vec2(-0.9, 0.0),\n"
        "        vec2( 0.9, 0.0)\n"
        "    );\n"
        "    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);\n"
        "}\n";

    std::shared_ptr<erhe::graphics::Texture> color_texture = make_color_target(target_size, target_size);
    std::shared_ptr<erhe::graphics::Render_pass> render_pass = make_single_color_render_pass(color_texture.get(), 0.0, 0.0, 0.0, 1.0);

    std::shared_ptr<erhe::graphics::Render_pipeline_state> pipeline = make_topology_pipeline(device, erhe::graphics::Input_assembly_state::lines, vs_source);

    erhe::graphics::Render_command_encoder encoder = device.make_render_command_encoder(*render_pass);
    encoder.set_render_pipeline_state(*pipeline);
    encoder.draw_primitives(erhe::graphics::Primitive_type::lines, 0, 2);
    device.flush_render_command_encoder(encoder);
    device.wait_for_idle();

    std::vector<uint8_t> pixels = read_texture_rgba8(color_texture.get());

    // The segment spans ~0.9 of the half-width on each side -> ~0.9 * 16 ~= 14 texels.
    // Assert coarse coverage rather than an exact run to stay rasterizer-tolerant.
    const int lit = count_lit_texels(pixels, target_size, target_size);
    EXPECT_GE(lit, 8);
    EXPECT_LE(lit, target_size + 2);

    // All lit texels must lie on (at most) the two centre rows: rows at the very top and
    // bottom must be dark. This confirms the lit run is a thin horizontal line, not a
    // filled region.
    auto row_is_dark = [&](int y) -> bool {
        for (int x = 0; x < target_size; ++x) {
            const size_t index = (static_cast<size_t>(y) * static_cast<size_t>(target_size) + static_cast<size_t>(x)) * 4u;
            if (pixels[index + 0] > 127u) {
                return false;
            }
        }
        return true;
    };
    EXPECT_TRUE(row_is_dark(0));
    EXPECT_TRUE(row_is_dark(target_size - 1));
}

} // namespace erhe::graphics::test
