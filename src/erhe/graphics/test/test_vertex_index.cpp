#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/fragment_output.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/texture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

// erhe injects "layout(location = 0) in vec2 a_position;" from the position
// attribute in the vertex_format.
constexpr const char* c_vertex_source = R"glsl(
void main()
{
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)glsl";

constexpr const char* c_fragment_source = R"glsl(
void main()
{
    out_color = vec4(0.0, 1.0, 0.0, 1.0);
}
)glsl";

} // namespace

// Indexed draw with a real vertex buffer + index buffer. A full-viewport quad
// (4 vertices, 6 indices, two triangles) is drawn with draw_indexed_primitives;
// the whole target should be green. Exercises Vertex_format / Vertex_input_state,
// set_vertex_buffer, set_index_buffer, and draw_indexed_primitives.
TEST_F(Gpu_test, indexed_quad_draw)
{
    constexpr int width  = 16;
    constexpr int height = 16;

    // Full-viewport quad in NDC.
    const std::array<float, 8> positions{
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f
    };
    const std::array<uint16_t, 6> indices{ 0, 1, 2, 0, 2, 3 };

    const std::shared_ptr<erhe::graphics::Buffer> vertex_buffer =
        make_host_buffer(sizeof(positions), erhe::graphics::Buffer_usage::vertex, "quad vertices");
    const std::shared_ptr<erhe::graphics::Buffer> index_buffer =
        make_host_buffer(sizeof(indices), erhe::graphics::Buffer_usage::index, "quad indices");
    {
        const std::span<std::byte> mapped = vertex_buffer->map_bytes(0, sizeof(positions));
        std::memcpy(mapped.data(), positions.data(), sizeof(positions));
        vertex_buffer->unmap();
    }
    {
        const std::span<std::byte> mapped = index_buffer->map_bytes(0, sizeof(indices));
        std::memcpy(mapped.data(), indices.data(), sizeof(indices));
        index_buffer->unmap();
    }

    const erhe::dataformat::Vertex_format vertex_format{
        erhe::dataformat::Vertex_stream{
            0,
            { erhe::dataformat::Vertex_attribute{
                  .format      = erhe::dataformat::Format::format_32_vec2_float,
                  .usage_type  = erhe::dataformat::Vertex_attribute_usage::position,
                  .usage_index = 0
              } }
        }
    };
    const erhe::graphics::Vertex_input_state vertex_input{
        device(),
        erhe::graphics::Vertex_input_state_data::make(vertex_format)
    };

    const erhe::graphics::Bind_group_layout empty_layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"indexed empty layout"},
            .uses_texture_heap = false
        }
    };
    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "indexed_quad",
        .fragment_outputs = &fragment_outputs,
        .vertex_format    = &vertex_format,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source}   },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source} }
        },
        .bind_group_layout = &empty_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "indexed-quad shader failed to compile/link";
    erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

    const std::shared_ptr<erhe::graphics::Texture> output = make_color_target(width, height);

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
    descriptor.debug_label = erhe::utility::Debug_label{"indexed quad"};

    erhe::graphics::Render_pipeline_create_info pipeline_create_info;
    pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
    pipeline_create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
    pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
    pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
    pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
    pipeline_create_info.base.bind_group_layout                 = &empty_layout;
    pipeline_create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
    pipeline_create_info.shader_stages                          = &shader_stages;
    pipeline_create_info.vertex_input                           = &vertex_input;
    pipeline_create_info.vertex_format                          = &vertex_format;
    pipeline_create_info.set_format_from_render_pass(descriptor);
    const erhe::graphics::Render_pipeline pipeline{device(), pipeline_create_info};
    ASSERT_TRUE(pipeline.is_valid()) << "indexed-quad pipeline is not valid";

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass            render_pass{device(), descriptor};
            erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, width, height);
            encoder.set_scissor_rect (0, 0, width, height);
            encoder.set_bind_group_layout(&empty_layout);
            encoder.set_render_pipeline(pipeline);
            encoder.set_vertex_buffer(vertex_buffer.get(), 0, 0);
            encoder.set_index_buffer(index_buffer.get());
            encoder.draw_indexed_primitives(
                erhe::graphics::Primitive_type::triangle,
                6,
                erhe::dataformat::Format::format_16_scalar_uint,
                0
            );
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*output);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);

    int green = 0;
    for (std::size_t i = 0; (i + 3) < pixels.size(); i += 4) {
        if ((pixels[i + 1] >= 250) && (pixels[i + 0] <= 5) && (pixels[i + 2] <= 5)) {
            ++green;
        }
    }
    EXPECT_EQ(green, width * height) << "the indexed quad should cover the whole target with green";
}

} // namespace erhe::graphics::test
