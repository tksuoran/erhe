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
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/texture.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <array>
#include <cmath>
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

// -----------------------------------------------------------------------------
// agfx ports: DrawIndexed, DrawIndexedU16.
//
// A quad over image texels [16, 48) x [16, 48) of a 64x64 target, drawn from
// 4 vertices and 6 indices (triangles 0 1 2 and 0 2 3) with
// draw_indexed_primitives, once with a uint32 and once with a uint16 index
// buffer. Vertices are pulled from an SSBO (two vec4s per vertex: NDC position
// with +y up on every backend, and color) by gl_VertexID, which erhe's Vulkan
// preamble maps to gl_VertexIndex; for an indexed draw it is the index value.
// Corner colors: top-left red, top-right green, bottom-right blue, bottom-left
// white. Every texel is checked against the analytic barycentric interpolation
// at its centre; both index formats render the same image and share the golden
// draw_indexed. A uint32 index buffer read as uint16 yields degenerate
// triangles and draws nothing.
// -----------------------------------------------------------------------------

namespace {

constexpr int c_pulled_size       = 64;
constexpr int c_pulled_quad_begin = 16;
constexpr int c_pulled_quad_end   = 48;

constexpr const char* c_pulled_vertex_source = R"glsl(
layout(location = 0) out vec4 v_color;
void main()
{
    gl_Position = Vertices.data[(2 * gl_VertexID) + 0];
    v_color     = Vertices.data[(2 * gl_VertexID) + 1];
}
)glsl";

constexpr const char* c_pulled_fragment_source = R"glsl(
layout(location = 0) in vec4 v_color;
void main()
{
    out_color = v_color;
}
)glsl";

const glm::vec4 c_pulled_red  {1.0f, 0.0f, 0.0f, 1.0f};
const glm::vec4 c_pulled_green{0.0f, 1.0f, 0.0f, 1.0f};
const glm::vec4 c_pulled_blue {0.0f, 0.0f, 1.0f, 1.0f};
const glm::vec4 c_pulled_white{1.0f, 1.0f, 1.0f, 1.0f};

} // namespace

class Pulled_quad_test : public Gpu_test
{
protected:
    // Draw the pulled quad through an index buffer of index_format
    // (format_16_scalar_uint or format_32_scalar_uint), check it and assert
    // the golden.
    void run(const erhe::dataformat::Format index_format)
    {
        erhe::graphics::Device& graphics_device = device();

        const std::array<glm::vec4, 8> vertices{
            glm::vec4{-0.5f,  0.5f, 0.0f, 1.0f}, c_pulled_red,   // 0 top-left
            glm::vec4{ 0.5f,  0.5f, 0.0f, 1.0f}, c_pulled_green, // 1 top-right
            glm::vec4{ 0.5f, -0.5f, 0.0f, 1.0f}, c_pulled_blue,  // 2 bottom-right
            glm::vec4{-0.5f, -0.5f, 0.0f, 1.0f}, c_pulled_white  // 3 bottom-left
        };
        const std::array<uint32_t, 6> indices{ 0, 1, 2, 0, 2, 3 };

        const std::size_t index_size  = erhe::dataformat::get_format_size_bytes(index_format);
        const std::size_t index_bytes = indices.size() * index_size;
        std::vector<std::byte> index_data(index_bytes);
        for (std::size_t i = 0; i < indices.size(); ++i) {
            if (index_size == sizeof(uint16_t)) {
                const uint16_t value = static_cast<uint16_t>(indices[i]);
                std::memcpy(index_data.data() + (i * index_size), &value, sizeof(value));
            } else {
                std::memcpy(index_data.data() + (i * index_size), &indices[i], sizeof(uint32_t));
            }
        }

        const std::shared_ptr<erhe::graphics::Buffer> vertex_buffer =
            make_host_buffer(sizeof(vertices), erhe::graphics::Buffer_usage::storage, "pulled quad vertices");
        const std::shared_ptr<erhe::graphics::Buffer> index_buffer =
            make_host_buffer(index_bytes, erhe::graphics::Buffer_usage::index, "pulled quad indices");
        {
            const std::span<std::byte> mapped = vertex_buffer->map_bytes(0, sizeof(vertices));
            std::memcpy(mapped.data(), vertices.data(), sizeof(vertices));
            vertex_buffer->unmap();
        }
        {
            const std::span<std::byte> mapped = index_buffer->map_bytes(0, index_bytes);
            std::memcpy(mapped.data(), index_data.data(), index_bytes);
            index_buffer->unmap();
        }

        erhe::graphics::Shader_resource vertices_block{
            graphics_device,
            erhe::graphics::Shader_resource::Block_create_info{
                .name          = "Vertices",
                .binding_point = 0,
                .type          = erhe::graphics::Shader_resource::Type::shader_storage_block
            }
        };
        vertices_block.add_vec4("data", erhe::graphics::Shader_resource::unsized_array);
        const erhe::graphics::Bind_group_layout layout{
            graphics_device,
            erhe::graphics::Bind_group_layout_create_info{
                .bindings = {
                    erhe::graphics::Bind_group_layout_binding{
                        .binding_point = 0u,
                        .type          = erhe::graphics::Binding_type::storage_buffer,
                        .stage_flags   = erhe::graphics::Shader_stage_flags::vertex
                    }
                },
                .debug_label       = erhe::utility::Debug_label{"pulled quad layout"},
                .uses_texture_heap = false
            }
        };
        const erhe::graphics::Fragment_outputs fragment_outputs{
            { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
        };
        erhe::graphics::Shader_stages_create_info shader_create_info{
            .name             = "pulled_quad",
            .interface_blocks = { &vertices_block },
            .fragment_outputs = &fragment_outputs,
            .no_vertex_input  = true,
            .shaders = {
                { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_pulled_vertex_source}   },
                { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_pulled_fragment_source} }
            },
            .bind_group_layout = &layout
        };
        erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(graphics_device, shader_create_info);
        ASSERT_TRUE(prototype.is_valid()) << "pulled-quad shader failed to compile/link";
        erhe::graphics::Shader_stages shader_stages{graphics_device, std::move(prototype)};

        const std::shared_ptr<erhe::graphics::Texture> output = make_color_target(c_pulled_size, c_pulled_size);

        erhe::graphics::Render_pass_descriptor descriptor{};
        descriptor.color_attachments[0].texture       = output.get();
        descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
        descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
        descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
        descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.render_target_width  = c_pulled_size;
        descriptor.render_target_height = c_pulled_size;
        descriptor.debug_label = erhe::utility::Debug_label{"pulled quad"};

        erhe::graphics::Render_pipeline_create_info pipeline_create_info;
        pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
        pipeline_create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
        pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
        pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
        pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
        pipeline_create_info.base.bind_group_layout                 = &layout;
        pipeline_create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
        pipeline_create_info.shader_stages                          = &shader_stages;
        pipeline_create_info.vertex_input                           = nullptr;
        pipeline_create_info.set_format_from_render_pass(descriptor);
        const erhe::graphics::Render_pipeline pipeline{graphics_device, pipeline_create_info};
        ASSERT_TRUE(pipeline.is_valid()) << "pulled-quad pipeline is not valid";

        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                erhe::graphics::Render_pass              render_pass{graphics_device, descriptor};
                erhe::graphics::Render_command_encoder   encoder = graphics_device.make_render_command_encoder(command_buffer);
                const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                encoder.set_viewport_rect(0, 0, c_pulled_size, c_pulled_size);
                encoder.set_scissor_rect (0, 0, c_pulled_size, c_pulled_size);
                encoder.set_bind_group_layout(&layout);
                encoder.set_render_pipeline(pipeline);
                encoder.set_buffer(erhe::graphics::Buffer_target::storage, vertex_buffer.get(), 0, sizeof(vertices), 0);
                encoder.set_index_buffer(index_buffer.get());
                encoder.draw_indexed_primitives(erhe::graphics::Primitive_type::triangle, indices.size(), index_format, 0);
            }
        );

        const std::vector<uint8_t> pixels = read_texture_rgba8(*output);
        ASSERT_EQ(pixels.size(), static_cast<std::size_t>(c_pulled_size) * static_cast<std::size_t>(c_pulled_size) * 4u);

        // Analytic image (image rows): u, v are the texel centre's position
        // across the quad from its top-left corner. Triangle 0 1 2 covers
        // v <= u, triangle 0 2 3 covers v >= u; outside is the clear color.
        std::vector<uint8_t> expected(pixels.size());
        const float quad_extent = static_cast<float>(c_pulled_quad_end - c_pulled_quad_begin);
        for (int y = 0; y < c_pulled_size; ++y) {
            for (int x = 0; x < c_pulled_size; ++x) {
                const bool inside =
                    (x >= c_pulled_quad_begin) && (x < c_pulled_quad_end) &&
                    (y >= c_pulled_quad_begin) && (y < c_pulled_quad_end);
                glm::vec4 color{0.0f, 0.0f, 0.0f, 1.0f};
                if (inside) {
                    const float u = (static_cast<float>(x - c_pulled_quad_begin) + 0.5f) / quad_extent;
                    const float v = (static_cast<float>(y - c_pulled_quad_begin) + 0.5f) / quad_extent;
                    color = (v <= u)
                        ? (((1.0f - u) * c_pulled_red) + ((u - v) * c_pulled_green) + (v * c_pulled_blue))
                        : (((1.0f - v) * c_pulled_red) + (u * c_pulled_blue) + ((v - u) * c_pulled_white));
                }
                const std::size_t base = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(c_pulled_size)) + static_cast<std::size_t>(x)) * 4u;
                for (int c = 0; c < 4; ++c) {
                    expected[base + static_cast<std::size_t>(c)] = static_cast<uint8_t>(std::lround(color[c] * 255.0f));
                }
            }
        }
        const std::vector<uint8_t> image_rows = memory_rows_to_image_rows(pixels, static_cast<std::size_t>(c_pulled_size) * 4u, c_pulled_size);
        expect_rgba8_near(image_rows, expected, c_pulled_size, c_pulled_size, 2, "pulled quad");

        expect_image_matches_golden("draw_indexed", c_pulled_size, c_pulled_size, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{pixels}));
    }
};

// agfx DrawIndexed: uint32 indices.
TEST_F(Pulled_quad_test, draw_indexed)
{
    run(erhe::dataformat::Format::format_32_scalar_uint);
}

// agfx DrawIndexedU16: uint16 indices, same image and golden.
TEST_F(Pulled_quad_test, draw_indexed_u16)
{
    run(erhe::dataformat::Format::format_16_scalar_uint);
}

} // namespace erhe::graphics::test
