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
#include "erhe_graphics/indirect_count_draws.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <algorithm>
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
// CPU-side, except in the count variants below.
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
//
// First-instance variant (draw_indexed_indirect_first_instance): command q
// carries first_instance 2 - q, and the color comes from
// ERHE_PRIMITIVE_INDEX instead of ERHE_DRAW_ID, so column q shows color 2 - q
// where the record index travels in the command's first_instance
// (Device_info::record_indexing == Record_indexing::first_instance; skipped
// elsewhere). Its expected image is computed; it has no golden.
//
// Count variants (draw_indexed_indirect_count, draw_indexed_indirect_count_zero,
// draw_indexed_indirect_count_two_jobs, draw_indexed_indirect_count_set_per_pass,
// the last one preparing and drawing each job in its own render pass, see
// its test): the compute pass writes every command
// slot, a second compute encoder prepares the jobs
// (Compute_command_encoder::prepare_indexed_indirect_count_draws) and the
// render pass draws each job with multi_draw_indexed_primitives_indirect_count
// (set, job): max_draw_count 3 per job and the draw count read from the job's
// entry of a count buffer (stride 8, the second word a decoy), so the
// GPU-read count, not the maximum, decides (Device_info::
// use_draw_indirect_count; skipped elsewhere). Prepared draws carry no
// per-draw ERHE_DRAW_ID on Metal, so these variants place and color quad q by
// ERHE_PRIMITIVE_INDEX, the command's first_instance q (Color_source::
// first_instance; needs Record_indexing::first_instance), and do not check
// that ERHE_DRAW_ID counts up within a count draw (no consumer of the count
// draws relies on it). One job of three slots with count 2 draws columns 0
// and 1; with count 0 nothing. Two jobs: job 0 at command 0 (slots 0..2 =
// quads 0, 1, 2) with count 1 from index buffer 0, job 1 at command 4 (slots
// 4..6 = copies 0, 1, 2 of index buffer 1 with base vertex 0; slot 3 unused)
// with count 2. Index buffer 1 is 16-bit and its copy c holds the absolute
// vertices of quad (c + 2) % 3, so job 1 draws quads 2 and 0 in columns 2
// and 0: columns 0 and 2 drawn, column 1 clear. A job range read from the
// wrong offset, the wrong job's count or the wrong index buffer (or index
// type) draws column 1, loses column 2 or draws the wrong quad heights.
// Expected images are computed.
// -----------------------------------------------------------------------------

namespace {

constexpr int      c_column_count = 3;
constexpr int      c_column_width = 32;
constexpr int      c_width        = c_column_count * c_column_width;
constexpr int      c_height       = 48;
constexpr int      c_row_step     = c_height / c_column_count; // quad q starts at image row 16 q
constexpr uint32_t c_command_words = sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command) / sizeof(uint32_t);

// Command slot s draws index copy q = QUAD_OF_SLOT: index_count 6,
// instance_count 1, first_index 6 q, base_vertex BASE_VERTEX_OF_SLOT,
// base_instance FIRST_INSTANCE_OF_SLOT (expressions in s and q).
constexpr const char* c_command_compute_source = R"glsl(
layout(local_size_x = 1) in;
void main()
{
    uint s    = gl_GlobalInvocationID.x;
    uint q    = QUAD_OF_SLOT;
    uint base = s * COMMAND_WORDS;
    Commands.data[base + 0u] = 6u;
    Commands.data[base + 1u] = 1u;
    Commands.data[base + 2u] = q * 6u;
    Commands.data[base + 3u] = BASE_VERTEX_OF_SLOT;
    Commands.data[base + 4u] = FIRST_INSTANCE_OF_SLOT;
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
#if COLUMN_FROM_PRIMITIVE_INDEX
    float column       = float(ERHE_PRIMITIVE_INDEX);
#else
    float column       = float(ERHE_DRAW_ID);
#endif
    gl_Position = vec4(-1.0 + ((column + p.x) * column_width), p.y, 0.0, 1.0);
#if COLOR_FROM_PRIMITIVE_INDEX
    v_draw_id   = int(ERHE_PRIMITIVE_INDEX);
#else
    v_draw_id   = ERHE_DRAW_ID;
#endif
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

// What colors a column: the draw's position, or the record index its
// command's first_instance names (reversed: command q names record 2 - q;
// first_instance: command for quad q names record q, which also places the
// quad in column q).
enum class Color_source : unsigned int
{
    draw_id,
    first_instance_reversed,
    first_instance
};

// A count variant's job: command slots [command_offset, command_offset + 3)
// drawn with the GPU-read count from index buffer index_buffer (0 or 1).
class Count_job
{
public:
    uint32_t command_offset{0};
    uint32_t count         {0};
    uint32_t index_buffer  {0};
};

constexpr uint32_t c_count_job_slots       = 3;
constexpr uint32_t c_count_words_per_job   = 2; // {count, decoy}
constexpr uint32_t c_count_decoy           = 0xdeadbeefu;

// How the count variants prepare their jobs: all in one set drawn in one
// render pass, or one set per job, each prepared just before its own render
// pass (the second pass loads the first one's image), reusing the
// Indirect_count_draws' state between passes of one frame.
enum class Prepare_mode : unsigned int
{
    one_set,
    set_per_pass
};

// The count variants' command slots and which quad each draws.
class Count_setup
{
public:
    Prepare_mode           prepare_mode{Prepare_mode::one_set};
    uint32_t               command_slots{c_count_job_slots};
    std::string            quad_of_slot {"s"};
    // Empty: the defaults (base vertex 3 q, first instance q).
    std::string            base_vertex_of_slot;
    std::string            first_instance_of_slot;
    std::vector<Count_job> jobs;
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
    // (memory rows) in pixels. With count_setup, write its command slots,
    // prepare its jobs and draw each with the prepared
    // multi_draw_indexed_primitives_indirect_count (draw_count unused).
    void draw(
        const uint32_t            draw_count,
        const Indexing            indexing,
        const Color_source        color_source,
        const Count_setup* const  count_setup,
        std::vector<uint8_t>&     pixels
    )
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
        const bool        reversed               = (color_source == Color_source::first_instance_reversed);
        const bool        by_first_instance      = (color_source == Color_source::first_instance);
        const std::string first_instance_of_quad = reversed ? std::string{"(2u - q)"} : (by_first_instance ? std::string{"q"} : std::string{"0u"});
        const uint32_t    command_write_count    = (count_setup != nullptr) ? count_setup->command_slots : draw_count;

        const std::size_t vertex_bytes  = vertices.size() * sizeof(glm::vec4);
        const std::size_t index_bytes   = indices.size() * sizeof(uint32_t);
        const std::size_t command_bytes = static_cast<std::size_t>(std::max<uint32_t>(command_write_count, 1u)) * sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command);

        const std::shared_ptr<erhe::graphics::Buffer> vertex_buffer = make_host_buffer(vertex_bytes, erhe::graphics::Buffer_usage::storage, "draw indirect vertices");
        const std::shared_ptr<erhe::graphics::Buffer> index_buffer  = make_host_buffer(index_bytes,  erhe::graphics::Buffer_usage::index,   "draw indirect indices");
        // The count variants' second index buffer (same contents).
        // The count variants' second index buffer: 16-bit, copy c holds
        // the absolute vertices of quad (c + 2) % 3 (drawn with base vertex
        // 0), so a job drawn from the wrong index buffer draws the wrong
        // quads.
        std::vector<uint16_t> indices_1;
        if (indexing == Indexing::indexed) {
            for (uint32_t copy = 0; copy < static_cast<uint32_t>(c_column_count); ++copy) {
                const uint32_t quad = (copy + 2u) % static_cast<uint32_t>(c_column_count);
                for (const uint32_t corner : { 0u, 1u, 2u, 0u, 2u, 3u }) {
                    indices_1.push_back(static_cast<uint16_t>((4u * quad) + corner));
                }
            }
        } else {
            indices_1.resize(indices.size(), 0);
        }
        const std::size_t index_bytes_1 = indices_1.size() * sizeof(uint16_t);
        const std::shared_ptr<erhe::graphics::Buffer> index_buffer_1 = make_host_buffer(index_bytes_1, erhe::graphics::Buffer_usage::index, "draw indirect indices 1");
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
            const std::span<std::byte> mapped = index_buffer_1->map_bytes(0, index_bytes_1);
            std::memcpy(mapped.data(), indices_1.data(), index_bytes_1);
            index_buffer_1->unmap();
        }
        {
            // Zero commands draw nothing, so a missing compute write shows.
            const std::span<std::byte> mapped = command_buffer_object->map_bytes(0, command_bytes);
            std::memset(mapped.data(), 0, command_bytes);
            command_buffer_object->unmap();
        }
        // The count variants' draw counts, read by the GPU: per job {count,
        // decoy}.
        std::shared_ptr<erhe::graphics::Buffer> count_buffer_object{};
        if (count_setup != nullptr) {
            std::vector<uint32_t> counts;
            for (const Count_job& job : count_setup->jobs) {
                counts.push_back(job.count);
                counts.push_back(c_count_decoy);
            }
            const std::size_t count_bytes = std::max<std::size_t>(counts.size() * sizeof(uint32_t), sizeof(uint32_t));
            count_buffer_object = make_host_buffer(count_bytes, erhe::graphics::Buffer_usage::indirect | erhe::graphics::Buffer_usage::storage, "draw indirect count");
            const std::span<std::byte> mapped = count_buffer_object->map_bytes(0, count_bytes);
            std::memcpy(mapped.data(), counts.data(), counts.size() * sizeof(uint32_t));
            count_buffer_object->unmap();
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
                { "COMMAND_WORDS",       std::to_string(c_command_words) + "u" },
                { "QUAD_OF_SLOT",           ((count_setup != nullptr) && !count_setup->quad_of_slot.empty())           ? count_setup->quad_of_slot           : std::string{"s"} },
                { "BASE_VERTEX_OF_SLOT",    ((count_setup != nullptr) && !count_setup->base_vertex_of_slot.empty())    ? count_setup->base_vertex_of_slot    : ("(q * " + std::to_string(base_vertex_step) + "u)") },
                { "FIRST_INSTANCE_OF_SLOT", ((count_setup != nullptr) && !count_setup->first_instance_of_slot.empty()) ? count_setup->first_instance_of_slot : first_instance_of_quad }
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
            .defines          = {
                { "COLUMN_COUNT",                std::to_string(c_column_count) },
                { "COLOR_FROM_PRIMITIVE_INDEX",  (reversed || by_first_instance) ? "1" : "0" },
                { "COLUMN_FROM_PRIMITIVE_INDEX", by_first_instance ? "1" : "0" }
            },
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
        pipeline_create_info.base.indirect_count_draws              = (count_setup != nullptr) ? erhe::graphics::Indirect_count_draw_support::enabled : erhe::graphics::Indirect_count_draw_support::disabled;
        pipeline_create_info.shader_stages                          = &shader_stages;
        pipeline_create_info.vertex_input                           = nullptr;
        pipeline_create_info.set_format_from_render_pass(descriptor);
        const erhe::graphics::Render_pipeline pipeline{graphics_device, pipeline_create_info};
        ASSERT_TRUE(pipeline.is_valid()) << "draw indirect pipeline is not valid";

        // The count variants' prepared draws.
        erhe::graphics::Indirect_count_draws              count_draws{graphics_device, erhe::utility::Debug_label{"draw indirect count"}};
        std::vector<erhe::graphics::Indirect_count_draw_job> count_draw_jobs;
        if (count_setup != nullptr) {
            for (const Count_job& job : count_setup->jobs) {
                count_draw_jobs.push_back(
                    erhe::graphics::Indirect_count_draw_job{
                        .command_offset = job.command_offset,
                        .max_draw_count = c_count_job_slots,
                        .index_buffer   = job.index_buffer
                    }
                );
            }
        }
        const std::array<erhe::graphics::Indirect_count_index_buffer, 2> count_index_buffers{{
            { .buffer = index_buffer.get(),   .index_type = erhe::dataformat::Format::format_32_scalar_uint },
            { .buffer = index_buffer_1.get(), .index_type = erhe::dataformat::Format::format_16_scalar_uint }
        }};

        submit_and_wait(
            [&](erhe::graphics::Command_buffer& command_buffer) {
                {
                    erhe::graphics::Compute_command_encoder encoder = graphics_device.make_compute_command_encoder(command_buffer);
                    encoder.set_bind_group_layout(&compute_layout);
                    encoder.set_compute_pipeline(*program.pipeline);
                    encoder.set_buffer(erhe::graphics::Buffer_target::storage, command_buffer_object.get(), 0, command_bytes, 0);
                    encoder.dispatch_compute(command_write_count, 1, 1);
                }
                if ((count_setup != nullptr) && (count_setup->prepare_mode == Prepare_mode::set_per_pass)) {
                    // The command writes -> the prepare steps' reads.
                    command_buffer.memory_barrier(erhe::graphics::Memory_barrier_mask::shader_storage_barrier_bit | erhe::graphics::Memory_barrier_mask::command_barrier_bit);
                    erhe::graphics::Render_pass_descriptor load_descriptor = descriptor;
                    load_descriptor.color_attachments[0].load_action = erhe::graphics::Load_action::Load;
                    for (std::size_t job = 0; job < count_draw_jobs.size(); ++job) {
                        // Rebased to command 0, so every pass encodes into
                        // the same indirect command buffer entries.
                        const erhe::graphics::Indirect_count_draw_job pass_job{
                            .command_offset = 0,
                            .max_draw_count = count_draw_jobs[job].max_draw_count,
                            .index_buffer   = count_draw_jobs[job].index_buffer
                        };
                        erhe::graphics::Indirect_count_draw_set pass_set{};
                        {
                            erhe::graphics::Compute_command_encoder encoder = graphics_device.make_compute_command_encoder(command_buffer);
                            pass_set = encoder.prepare_indexed_indirect_count_draws(
                                count_draws,
                                erhe::graphics::Indirect_count_draws_prepare_info{
                                    .frame_index         = graphics_device.get_frame_index(),
                                    .primitive_type      = erhe::graphics::Primitive_type::triangle,
                                    .index_buffers       = count_index_buffers,
                                    .command_buffer      = command_buffer_object.get(),
                                    .command_byte_offset = static_cast<std::size_t>(count_draw_jobs[job].command_offset) * sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command),
                                    .count_buffer        = count_buffer_object.get(),
                                    .count_byte_offset   = job * c_count_words_per_job * sizeof(uint32_t),
                                    .count_byte_stride   = c_count_words_per_job * sizeof(uint32_t),
                                    .jobs                = std::span<const erhe::graphics::Indirect_count_draw_job>{&pass_job, 1}
                                }
                            );
                        }
                        erhe::graphics::Render_pass              render_pass{graphics_device, (job == 0) ? descriptor : load_descriptor};
                        erhe::graphics::Render_command_encoder   encoder = graphics_device.make_render_command_encoder(command_buffer);
                        const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
                        encoder.set_viewport_rect(0, 0, c_width, c_height);
                        encoder.set_scissor_rect (0, 0, c_width, c_height);
                        encoder.set_bind_group_layout(&draw_layout);
                        encoder.set_render_pipeline(pipeline);
                        encoder.set_buffer(erhe::graphics::Buffer_target::storage, vertex_buffer.get(), 0, vertex_bytes, 0);
                        encoder.multi_draw_indexed_primitives_indirect_count(pass_set, 0);
                    }
                    return;
                }
                erhe::graphics::Indirect_count_draw_set count_draw_set{};
                if (count_setup != nullptr) {
                    // The command writes -> the prepare step's reads.
                    command_buffer.memory_barrier(erhe::graphics::Memory_barrier_mask::shader_storage_barrier_bit);
                    erhe::graphics::Compute_command_encoder encoder = graphics_device.make_compute_command_encoder(command_buffer);
                    count_draw_set = encoder.prepare_indexed_indirect_count_draws(
                        count_draws,
                        erhe::graphics::Indirect_count_draws_prepare_info{
                            .frame_index         = graphics_device.get_frame_index(),
                            .primitive_type      = erhe::graphics::Primitive_type::triangle,
                            .index_buffers       = count_index_buffers,
                            .command_buffer      = command_buffer_object.get(),
                            .command_byte_offset = 0,
                            .count_buffer        = count_buffer_object.get(),
                            .count_byte_offset   = 0,
                            .count_byte_stride   = c_count_words_per_job * sizeof(uint32_t),
                            .jobs                = count_draw_jobs
                        }
                    );
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
                    if (count_setup != nullptr) {
                        for (uint32_t job = 0; job < static_cast<uint32_t>(count_setup->jobs.size()); ++job) {
                            encoder.multi_draw_indexed_primitives_indirect_count(count_draw_set, job);
                        }
                    } else {
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
            }
        );

        pixels = read_texture_rgba8(*output);
        ASSERT_EQ(pixels.size(), static_cast<std::size_t>(c_width) * static_cast<std::size_t>(c_height) * 4u);
    }

    // Column c in drawn_columns (bit c) holds color c (color 2 - c for
    // first_instance_reversed) in image rows [16 c, 48) and the clear color
    // above; the other columns are clear. Exact per texel; an empty
    // golden_name skips the golden comparison.
    void check(const std::vector<uint8_t>& pixels, const uint32_t drawn_columns, const Color_source color_source, std::string_view golden_name)
    {
        std::vector<uint8_t> expected(static_cast<std::size_t>(c_width) * static_cast<std::size_t>(c_height) * 4u);
        for (int y = 0; y < c_height; ++y) {
            for (int x = 0; x < c_width; ++x) {
                const int                    column = x / c_column_width;
                const bool                   drawn  = ((drawn_columns & (1u << static_cast<uint32_t>(column))) != 0) && (y >= (column * c_row_step));
                const int                    record = (color_source == Color_source::first_instance_reversed) ? (c_column_count - 1 - column) : column;
                const std::array<uint8_t, 4> color  = drawn ? c_column_colors[static_cast<std::size_t>(record)] : std::array<uint8_t, 4>{ 0, 0, 0, 255 };
                const std::size_t            base   = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(c_width)) + static_cast<std::size_t>(x)) * 4u;
                std::memcpy(expected.data() + base, color.data(), 4u);
            }
        }
        const std::vector<uint8_t> image_rows = memory_rows_to_image_rows(pixels, static_cast<std::size_t>(c_width) * 4u, c_height);
        const std::string_view     name       =
            !golden_name.empty()                                           ? golden_name :
            (color_source == Color_source::first_instance_reversed)        ? std::string_view{"draw_indirect_first_instance"} :
                                                                             std::string_view{"draw_indirect_count"};
        expect_rgba8_near(image_rows, expected, c_width, c_height, 1, name);

        if (!golden_name.empty()) {
            expect_image_matches_golden(golden_name, c_width, c_height, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{pixels}));
        }
    }

    void run(const uint32_t draw_count, const Indexing indexing, std::string_view golden_name)
    {
        run(draw_count, indexing, Color_source::draw_id, golden_name);
    }

    void run(const uint32_t draw_count, const Indexing indexing, const Color_source color_source, std::string_view golden_name)
    {
        std::vector<uint8_t> pixels;
        draw(draw_count, indexing, color_source, nullptr, pixels);
        if (HasFatalFailure()) {
            return;
        }
        check(pixels, (1u << draw_count) - 1u, color_source, golden_name);
    }

    // The prepared count draws of count_setup; drawn_columns as in check().
    void run_count(const Count_setup& count_setup, const uint32_t drawn_columns)
    {
        if (!device().get_info().use_draw_indirect_count) {
            GTEST_SKIP() << "Device_info::use_draw_indirect_count not available on this device";
        }
        if (device().get_info().record_indexing != erhe::graphics::Record_indexing::first_instance) {
            GTEST_SKIP() << "Record_indexing::first_instance not available on this device";
        }
        std::vector<uint8_t> pixels;
        draw(0, Indexing::indexed, Color_source::first_instance, &count_setup, pixels);
        if (HasFatalFailure()) {
            return;
        }
        check(pixels, drawn_columns, Color_source::first_instance, {});
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

// Three GPU-written indexed commands whose first_instance names the record
// in reverse order: ERHE_PRIMITIVE_INDEX (doc/erhe/graphics.md "Draw
// records") reaches the shader as 2 - q in column q.
TEST_F(Draw_indirect_test, draw_indexed_indirect_first_instance)
{
    if (device().get_info().record_indexing != erhe::graphics::Record_indexing::first_instance) {
        GTEST_SKIP() << "Record_indexing::first_instance not available on this device";
    }
    run(3, Indexing::indexed, Color_source::first_instance_reversed, {});
}

// Three GPU-written indexed commands drawn with a GPU-read count of 2 out of
// max_draw_count 3: columns 0 and 1, column 2 clear.
TEST_F(Draw_indirect_test, draw_indexed_indirect_count)
{
    run_count(Count_setup{.jobs = { Count_job{.command_offset = 0, .count = 2, .index_buffer = 0} }}, 0b011u);
}

// The same with a GPU-read count of 0: nothing drawn.
TEST_F(Draw_indirect_test, draw_indexed_indirect_count_zero)
{
    run_count(Count_setup{.jobs = { Count_job{.command_offset = 0, .count = 0, .index_buffer = 0} }}, 0b000u);
}

// The two jobs above, each prepared just before its own render pass and
// rebased to command 0: the second prepare rewrites the indirect command
// buffer entries and range the first pass drew from. Job 1 draws only
// column 2 here, so a first pass that executed the second prepare's
// commands would lose column 0.
TEST_F(Draw_indirect_test, draw_indexed_indirect_count_set_per_pass)
{
    run_count(
        Count_setup{
            .prepare_mode           = Prepare_mode::set_per_pass,
            .command_slots          = 7,
            .quad_of_slot           = "((s < 4u) ? (s % 3u) : (s - 4u))",
            .base_vertex_of_slot    = "((s < 4u) ? (q * 3u) : 0u)",
            .first_instance_of_slot = "((s < 4u) ? q : ((q + 2u) % 3u))",
            .jobs                   = {
                Count_job{.command_offset = 0, .count = 1, .index_buffer = 0},
                Count_job{.command_offset = 4, .count = 1, .index_buffer = 1}
            }
        },
        0b101u
    );
}

// Two jobs with their own command offset, count and index buffer (16-bit,
// permuted): job 0 draws column 0, job 1 (slots 4.. = quads 2, 0, 1)
// columns 2 and 0.
TEST_F(Draw_indirect_test, draw_indexed_indirect_count_two_jobs)
{
    run_count(
        Count_setup{
            .command_slots          = 7,
            .quad_of_slot           = "((s < 4u) ? (s % 3u) : (s - 4u))",
            .base_vertex_of_slot    = "((s < 4u) ? (q * 3u) : 0u)",
            .first_instance_of_slot = "((s < 4u) ? q : ((q + 2u) % 3u))",
            .jobs                   = {
                Count_job{.command_offset = 0, .count = 1, .index_buffer = 0},
                Count_job{.command_offset = 4, .count = 2, .index_buffer = 1}
            }
        },
        0b101u
    );
}

} // namespace erhe::graphics::test
