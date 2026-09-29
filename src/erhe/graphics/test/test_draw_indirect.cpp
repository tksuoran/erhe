#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/fragment_output.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

// -----------------------------------------------------------------------------
// agfx ports: DrawIndexedIndirect, DrawIndexedIndirectMulti, DrawIndirect,
// DrawIndirectMulti.
//
// A compute shader writes one or three Draw_indexed_primitives_indirect_command
// records into a buffer with usage indirect | storage; a
// Memory_barrier_mask::command_barrier_bit barrier (Vulkan: draw-indirect stage,
// indirect-command-read access) orders the writes before
// multi_draw_indexed_primitives_indirect consumes them. The draw count is
// CPU-side: erhe has no count buffer.
//
// The target is three 32-pixel columns, 96x48. Draw d places its quad in
// column d and colors it with color d (red, green, blue), both from
// ERHE_DRAW_ID: the erhe GLSL preamble defines it as gl_DrawID where multi-draw
// indirect is native and as a per-draw push constant (Vulkan) or uniform (GL)
// where it is emulated, so one shader and one golden serve every backend. The
// vertex shader forwards it to the fragment shader as a flat varying (the
// Vulkan emulation declares it in the vertex stage only). Quad q of the vertex
// data spans the column's full width and the image rows [16 q, 48), so each
// column shows which vertices its command reached.
//
// Vertices are pulled from an SSBO by gl_VertexID, which includes the draw's
// base vertex on every backend.
//
// Indexed variants: 12 vertices, quad q at vertices 4 q .. 4 q + 3; three
// index copies, copy q = {0, 1, 2, 0, 2, 3} + q at indices 6 q .. 6 q + 5.
// Command q draws index_count 6 from first_index 6 q with base_vertex 3 q, so
// a lost first_index or base_vertex reaches the wrong quad.
//
// Non-indexed variants (DrawIndirect, DrawIndirectMulti): erhe's indirect draw
// is indexed only, so they go through the same path with a trivial identity
// index buffer (0 .. 17) over 18 de-indexed vertices (quad q as two triangles
// at vertices 6 q .. 6 q + 5); command q draws 6 indices from first_index 6 q
// with base_vertex 0, which is the non-indexed Draw_primitives_indirect_command
// {count 6, first 6 q}. Both variants render the same image and share its
// golden (draw_indirect_single, draw_indirect_multi).
// -----------------------------------------------------------------------------

namespace {

constexpr int      c_column_count = 3;
constexpr int      c_column_width = 32;
constexpr int      c_width        = c_column_count * c_column_width;
constexpr int      c_height       = 48;
constexpr int      c_row_step     = c_height / c_column_count; // quad q starts at image row 16 q
constexpr uint32_t c_command_words = sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command) / sizeof(uint32_t);

// Command q: index_count 6, instance_count 1, first_index 6 q,
// base_vertex BASE_VERTEX_STEP q, base_instance 0.
constexpr const char* c_command_compute_source = R"glsl(
layout(local_size_x = 1) in;
void main()
{
    uint q    = gl_GlobalInvocationID.x;
    uint base = q * COMMAND_WORDS;
    Commands.data[base + 0u] = 6u;
    Commands.data[base + 1u] = 1u;
    Commands.data[base + 2u] = q * 6u;
    Commands.data[base + 3u] = q * BASE_VERTEX_STEP;
    Commands.data[base + 4u] = 0u;
}
)glsl";

// Vertex record: x in [0, 1] across the column, y in NDC (+y up on every
// backend).
constexpr const char* c_vertex_source = R"glsl(
layout(location = 0) flat out int v_draw_id;
void main()
{
    vec4  p            = Vertices.data[gl_VertexID];
    float column_width = 2.0 / float(COLUMN_COUNT);
    gl_Position = vec4(-1.0 + ((float(ERHE_DRAW_ID) + p.x) * column_width), p.y, 0.0, 1.0);
    v_draw_id   = ERHE_DRAW_ID;
}
)glsl";

constexpr const char* c_fragment_source = R"glsl(
layout(location = 0) flat in int v_draw_id;
void main()
{
    vec4 colors[3] = vec4[3](vec4(1.0, 0.0, 0.0, 1.0), vec4(0.0, 1.0, 0.0, 1.0), vec4(0.0, 0.0, 1.0, 1.0));
    out_color = colors[v_draw_id];
}
)glsl";

enum class Indexing : unsigned int
{
    indexed,  // shared corners, first_index and base_vertex both non-zero
    identity  // de-indexed triangles through an identity index buffer
};

constexpr std::array<std::array<uint8_t, 4>, c_column_count> c_column_colors{{
    {{ 255,   0,   0, 255 }},
    {{   0, 255,   0, 255 }},
    {{   0,   0, 255, 255 }}
}};

[[nodiscard]] auto quad_top_ndc(const int quad) -> float
{
    return 1.0f - ((2.0f * static_cast<float>(quad * c_row_step)) / static_cast<float>(c_height));
}

} // namespace

class Draw_indirect_test : public Gpu_test
{
protected:
    // Write draw_count commands with a compute pass, draw them with
    // multi_draw_indexed_primitives_indirect and return the read-back target
    // (memory rows) in pixels.
    void draw(const uint32_t draw_count, const Indexing indexing, std::vector<uint8_t>& pixels)
    {
        erhe::graphics::Device& graphics_device = device();

        // Vertex and index data.
        std::vector<glm::vec4> vertices;
        std::vector<uint32_t>  indices;
        for (int quad = 0; quad < c_column_count; ++quad) {
            const float     top = quad_top_ndc(quad);
            const glm::vec4 tl{0.0f, top,   0.0f, 1.0f};
            const glm::vec4 tr{1.0f, top,   0.0f, 1.0f};
            const glm::vec4 br{1.0f, -1.0f, 0.0f, 1.0f};
            const glm::vec4 bl{0.0f, -1.0f, 0.0f, 1.0f};
            if (indexing == Indexing::indexed) {
                vertices.insert(vertices.end(), { tl, tr, br, bl });
                for (const uint32_t corner : { 0u, 1u, 2u, 0u, 2u, 3u }) {
                    indices.push_back(corner + static_cast<uint32_t>(quad));
                }
            } else {
                vertices.insert(vertices.end(), { tl, tr, br, tl, br, bl });
            }
        }
        if (indexing == Indexing::identity) {
            for (uint32_t i = 0; i < static_cast<uint32_t>(vertices.size()); ++i) {
                indices.push_back(i);
            }
        }
        const uint32_t base_vertex_step = (indexing == Indexing::indexed) ? 3u : 0u;

        const std::size_t vertex_bytes  = vertices.size() * sizeof(glm::vec4);
        const std::size_t index_bytes   = indices.size() * sizeof(uint32_t);
        const std::size_t command_bytes = static_cast<std::size_t>(c_column_count) * sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command);

        const std::shared_ptr<erhe::graphics::Buffer> vertex_buffer = make_host_buffer(vertex_bytes, erhe::graphics::Buffer_usage::storage, "draw indirect vertices");
        const std::shared_ptr<erhe::graphics::Buffer> index_buffer  = make_host_buffer(index_bytes,  erhe::graphics::Buffer_usage::index,   "draw indirect indices");
        const std::shared_ptr<erhe::graphics::Buffer> command_buffer_object = make_host_buffer(
            command_bytes,
            erhe::graphics::Buffer_usage::indirect | erhe::graphics::Buffer_usage::storage,
            "draw indirect commands"
        );
        {
            const std::span<std::byte> mapped = vertex_buffer->map_bytes(0, vertex_bytes);
            std::memcpy(mapped.data(), vertices.data(), vertex_bytes);
            vertex_buffer->unmap();
        }
        {
            const std::span<std::byte> mapped = index_buffer->map_bytes(0, index_bytes);
            std::memcpy(mapped.data(), indices.data(), index_bytes);
            index_buffer->unmap();
        }
        {
            // Zero commands draw nothing, so a missing compute write shows.
            const std::span<std::byte> mapped = command_buffer_object->map_bytes(0, command_bytes);
            std::memset(mapped.data(), 0, command_bytes);
            command_buffer_object->unmap();
        }

        // Compute pass writing the commands.
        erhe::graphics::Shader_resource commands_block{
            graphics_device,
            erhe::graphics::Shader_resource::Block_create_info{
                .name          = "Commands",
                .binding_point = 0,
                .type          = erhe::graphics::Shader_resource::Type::shader_storage_block
            }
        };
        commands_block.add_uint("data", erhe::graphics::Shader_resource::unsized_array);
        const erhe::graphics::Bind_group_layout compute_layout{
            graphics_device,
            erhe::graphics::Bind_group_layout_create_info{
                .bindings = {
                    erhe::graphics::Bind_group_layout_binding{
                        .binding_point = 0u,
                        .type          = erhe::graphics::Binding_type::storage_buffer,
                        .stage_flags   = erhe::graphics::Shader_stage_flags::compute
                    }
                },
                .debug_label       = erhe::utility::Debug_label{"draw indirect command layout"},
                .uses_texture_heap = false
            }
        };
        const Compute_program program = make_compute_program(
            "draw_indirect_commands",
            c_command_compute_source,
            {
                { "COMMAND_WORDS",    std::to_string(c_command_words) + "u" },
                { "BASE_VERTEX_STEP", std::to_string(base_vertex_step) + "u" }
            },
            {},
            { &commands_block },
            compute_layout
        );
        ASSERT_TRUE(program.is_valid());

        // Draw pass pulling vertices from an SSBO.
        erhe::graphics::Shader_resource vertices_block{
            graphics_device,
            erhe::graphics::Shader_resource::Block_create_info{
                .name          = "Vertices",
                .binding_point = 0,
                .type          = erhe::graphics::Shader_resource::Type::shader_storage_block
            }
        };
        vertices_block.add_vec4("data", erhe::graphics::Shader_resource::unsized_array);
        const erhe::graphics::Bind_group_layout draw_layout{
            graphics_device,
            erhe::graphics::Bind_group_layout_create_info{
                .bindings = {
                    erhe::graphics::Bind_group_layout_binding{
                        .binding_point = 0u,
                        .type          = erhe::graphics::Binding_type::storage_buffer,
                        .stage_flags   = erhe::graphics::Shader_stage_flags::vertex
                    }
                },
                .debug_label       = erhe::utility::Debug_label{"draw indirect draw layout"},
                .uses_texture_heap = false
            }
        };
        const erhe::graphics::Fragment_outputs fragment_outputs{
            { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
        };
        erhe::graphics::Shader_stages_create_info shader_create_info{
            .name             = "draw_indirect",
            .defines          = { { "COLUMN_COUNT", std::to_string(c_column_count) } },
            .interface_blocks = { &vertices_block },
            .fragment_outputs = &fragment_outputs,
            .no_vertex_input  = true,
            .shaders = {
                { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source}   },
                { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source} }
            },
            .bind_group_layout = &draw_layout
        };
        erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(graphics_device, shader_create_info);
        ASSERT_TRUE(prototype.is_valid()) << "draw indirect shader failed to compile/link";
        erhe::graphics::Shader_stages shader_stages{graphics_device, std::move(prototype)};

        const std::shared_ptr<erhe::graphics::Texture> output = make_color_target(c_width, c_height);

        erhe::graphics::Render_pass_descriptor descriptor{};
        descriptor.color_attachments[0].texture       = output.get();
        descriptor.color_attachments[0].clear_value   = std::array<double, 4>{ 0.0, 0.0, 0.0, 1.0 };
        descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
        descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
        descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
        descriptor.render_target_width  = c_width;
        descriptor.render_target_height = c_height;
        descriptor.debug_label = erhe::utility::Debug_label{"draw indirect"};

        erhe::graphics::Render_pipeline_create_info pipeline_create_info;
        pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
        pipeline_create_info.base.rasterization                     = erhe::graphics::Rasterization_state::cull_mode_none;
        pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
        pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
        pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
        pipeline_create_info.base.bind_group_layout                 = &draw_layout;
        pipeline_create_info.base.color_blend                       = &erhe::graphics::Color_blend_state::color_blend_disabled;
        pipeline_create_info.shader_stages                          = &shader_stages;
        pipeline_create_info.vertex_input                           = nullptr;
        pipeline_create_info.set_format_from_render_pass(descriptor);
        const erhe::graphics::Render_pipeline pipeline{graphics_device, pipeline_create_info};
        ASSERT_TRUE(pipeline.is_valid()) << "draw indirect pipeline is not valid";

        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                {
                    erhe::graphics::Compute_command_encoder encoder = graphics_device.make_compute_command_encoder(command_buffer);
                    encoder.set_bind_group_layout(&compute_layout);
                    encoder.set_compute_pipeline(*program.pipeline);
                    encoder.set_buffer(erhe::graphics::Buffer_target::storage, command_buffer_object.get(), 0, command_bytes, 0);
                    encoder.dispatch_compute(draw_count, 1, 1);
                }
                command_buffer.memory_barrier(erhe::graphics::Memory_barrier_mask::command_barrier_bit);
                {
                    erhe::graphics::Render_pass              render_pass{graphics_device, descriptor};
                    erhe::graphics::Render_command_encoder   encoder = graphics_device.make_render_command_encoder(command_buffer);
                    const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                    encoder.set_viewport_rect(0, 0, c_width, c_height);
                    encoder.set_scissor_rect (0, 0, c_width, c_height);
                    encoder.set_bind_group_layout(&draw_layout);
                    encoder.set_render_pipeline(pipeline);
                    encoder.set_buffer(erhe::graphics::Buffer_target::storage, vertex_buffer.get(), 0, vertex_bytes, 0);
                    encoder.set_index_buffer(index_buffer.get());
                    encoder.set_buffer(erhe::graphics::Buffer_target::draw_indirect, command_buffer_object.get());
                    encoder.multi_draw_indexed_primitives_indirect(
                        erhe::graphics::Primitive_type::triangle,
                        erhe::dataformat::Format::format_32_scalar_uint,
                        0,
                        draw_count,
                        sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command)
                    );
                }
            }
        );

        pixels = read_texture_rgba8(*output);
        ASSERT_EQ(pixels.size(), static_cast<std::size_t>(c_width) * static_cast<std::size_t>(c_height) * 4u);
    }

    // Column c < draw_count holds color c in image rows [16 c, 48) and the
    // clear color above; the other columns are clear. Exact per texel.
    void check(const std::vector<uint8_t>& pixels, const uint32_t draw_count, std::string_view golden_name)
    {
        std::vector<uint8_t> expected(static_cast<std::size_t>(c_width) * static_cast<std::size_t>(c_height) * 4u);
        for (int y = 0; y < c_height; ++y) {
            for (int x = 0; x < c_width; ++x) {
                const int                    column = x / c_column_width;
                const bool                   drawn  = (column < static_cast<int>(draw_count)) && (y >= (column * c_row_step));
                const std::array<uint8_t, 4> color  = drawn ? c_column_colors[static_cast<std::size_t>(column)] : std::array<uint8_t, 4>{ 0, 0, 0, 255 };
                const std::size_t            base   = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(c_width)) + static_cast<std::size_t>(x)) * 4u;
                std::memcpy(expected.data() + base, color.data(), 4u);
            }
        }
        const std::vector<uint8_t> image_rows = memory_rows_to_image_rows(pixels, static_cast<std::size_t>(c_width) * 4u, c_height);
        expect_rgba8_near(image_rows, expected, c_width, c_height, 1, golden_name);

        expect_image_matches_golden(golden_name, c_width, c_height, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{pixels}));
    }

    void run(const uint32_t draw_count, const Indexing indexing, std::string_view golden_name)
    {
        std::vector<uint8_t> pixels;
        draw(draw_count, indexing, pixels);
        if (HasFatalFailure()) {
            return;
        }
        check(pixels, draw_count, golden_name);
    }
};

// agfx DrawIndexedIndirect: one GPU-written indexed command, column 0.
TEST_F(Draw_indirect_test, draw_indexed_indirect)
{
    run(1, Indexing::indexed, "draw_indirect_single");
}

// agfx DrawIndexedIndirectMulti: three GPU-written indexed commands in one
// multi-draw, one column each by draw index.
TEST_F(Draw_indirect_test, draw_indexed_indirect_multi)
{
    run(3, Indexing::indexed, "draw_indirect_multi");
}

// agfx DrawIndirect: one GPU-written command through the identity index
// buffer; same image as draw_indexed_indirect.
TEST_F(Draw_indirect_test, draw_indirect)
{
    run(1, Indexing::identity, "draw_indirect_single");
}

// agfx DrawIndirectMulti: three GPU-written commands through the identity
// index buffer; same image as draw_indexed_indirect_multi.
TEST_F(Draw_indirect_test, draw_indirect_multi)
{
    run(3, Indexing::identity, "draw_indirect_multi");
}

} // namespace erhe::graphics::test
