#include "gpu_test_fixture.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/draw_indirect.hpp"
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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

namespace {

// One instance per column. gl_InstanceID selects the column; the
// triangle-strip quad (4 vertices) is sized to one column wide and full
// height, then translated to column gl_InstanceID. The fragment color
// encodes the instance index in the red channel so the readback can verify
// each column was drawn by its own instance (not just "something filled the
// target"). INSTANCE_COUNT columns tile the whole [-1,1] NDC width.
constexpr const char* c_vertex_source = R"glsl(
layout(location = 0) flat out int v_instance;
void main()
{
    // Unit quad as a triangle strip: (0,0) (1,0) (0,1) (1,1) in [0,1]^2.
    vec2 corners[4] = vec2[4](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0));
    vec2 c = corners[gl_VertexID];

    float column_width = 2.0 / float(INSTANCE_COUNT);
    float x0 = -1.0 + (float(gl_InstanceID) * column_width);
    float x  = x0 + (c.x * column_width);
    float y  = -1.0 + (c.y * 2.0);
    gl_Position = vec4(x, y, 0.0, 1.0);
    v_instance = gl_InstanceID;
}
)glsl";

constexpr const char* c_fragment_source = R"glsl(
layout(location = 0) flat in int v_instance;
void main()
{
    // Red channel = (instance index + 1) * STEP so each column is a distinct,
    // exactly-representable 8-bit value; green channel constant to mark "drawn".
    float r = float((v_instance + 1) * STEP) / 255.0;
    out_color = vec4(r, 1.0, 0.0, 1.0);
}
)glsl";

} // namespace

// Instanced draw with a triangle-strip topology. Draws INSTANCE_COUNT
// instances of a 4-vertex strip quad, each translated to its own column via
// gl_InstanceID, tiling the viewport. Exercises draw_primitives with an
// instance_count > 1, gl_InstanceID, and Input_assembly_state::triangle_strip.
TEST_F(Gpu_test, instanced_triangle_strip_columns)
{
    constexpr int      width          = 16;
    constexpr int      height         = 16;
    constexpr uint32_t instance_count = 4;
    constexpr int      step           = 50; // red value per instance: 50,100,150,200
    constexpr int      column_width   = width / static_cast<int>(instance_count); // 4

    const std::shared_ptr<erhe::graphics::Texture> output = make_color_target(width, height);

    const erhe::graphics::Bind_group_layout empty_layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"instanced empty layout"},
            .uses_texture_heap = false
        }
    };
    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "instanced_strip",
        .defines          = { { "INSTANCE_COUNT", "4" }, { "STEP", "50" } },
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_vertex_source}   },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_fragment_source} }
        },
        .bind_group_layout = &empty_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "instanced-strip shader failed to compile/link";
    erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

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
    descriptor.debug_label = erhe::utility::Debug_label{"instanced"};

    erhe::graphics::Render_pipeline_create_info pipeline_create_info;
    pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle_strip;
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
    ASSERT_TRUE(pipeline.is_valid()) << "instanced pipeline is not valid";

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass            render_pass{device(), descriptor};
            erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, width, height);
            encoder.set_scissor_rect (0, 0, width, height);
            encoder.set_bind_group_layout(&empty_layout);
            encoder.set_render_pipeline(pipeline);
            // 4 strip vertices, instance_count instances.
            encoder.draw_primitives(erhe::graphics::Primitive_type::triangle_strip, 0, 4, instance_count);
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*output);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);

    // Each column band must carry the red value of its own instance and be
    // fully covered (green == 255). Column c spans x in [c*column_width,
    // (c+1)*column_width). Sample the center texel of each column band, every
    // row, to avoid any single-pixel seam at column boundaries.
    int bad = 0;
    for (uint32_t instance = 0; instance < instance_count; ++instance) {
        const int expected_r = static_cast<int>((instance + 1u) * static_cast<uint32_t>(step));
        const int x_center   = (static_cast<int>(instance) * column_width) + (column_width / 2);
        for (int y = 0; y < height; ++y) {
            const std::size_t i =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x_center)) * 4u;
            const int r = pixels[i + 0];
            const int g = pixels[i + 1];
            const int b = pixels[i + 2];
            const bool ok = (std::abs(r - expected_r) <= 2) && (g >= 250) && (b <= 5);
            if (!ok) {
                ++bad;
            }
        }
    }
    EXPECT_EQ(bad, 0)
        << bad << " sampled texels did not match their instance's expected color"
        << " (each of " << instance_count << " instances should fill its own column)";
}

namespace {

// draw_parameters_indirect: one column quad per instance. The per-vertex
// stream carries the quad corners, the per-instance stream (step per
// instance) the column's NDC x offset (a_custom_0) and color (a_color_0).
// Instance attributes are fetched at base_instance + instance on every
// backend, unlike gl_InstanceID (which GL counts from 0 and Vulkan's
// gl_InstanceIndex from base_instance), so the shader never reads the
// instance index and one expectation holds for both.
constexpr const char* c_draw_parameters_vertex_source = R"glsl(
layout(location = 0) flat out vec4 v_color;
void main()
{
    gl_Position = vec4(a_position.x + a_custom_0, a_position.y, 0.0, 1.0);
    v_color     = a_color_0;
}
)glsl";

constexpr const char* c_draw_parameters_fragment_source = R"glsl(
layout(location = 0) flat in vec4 v_color;
void main()
{
    out_color = v_color;
}
)glsl";

class Instance_record
{
public:
    float                x_offset;
    std::array<float, 4> color;
};

} // namespace

// agfx DrawParameters: first_index, base_vertex and base_instance, all
// non-zero, through one Draw_indexed_primitives_indirect_command issued with
// multi_draw_indexed_primitives_indirect (draw count 1; the direct indexed
// draw call has no base vertex / base instance parameters).
//
// Buffers (96x32 target, six 16-pixel columns):
//   vertices  0..3   full-target quad  (reached only if base_vertex is lost)
//   vertices  4..7   column-0 quad, NDC x in [-1, -2/3]
//   vertices  8..11  full-target quad  (reached only if first_index is lost)
//   indices   0..5   0 1 2 0 2 3 + 4   -> vertices 8..11 (skipped by first_index)
//   indices   6..11  0 1 2 0 2 3       -> vertices 4..7 with base_vertex 4
//   instances 0..5   column i offset, color i
// The command draws index_count 6 from first_index 6 with base_vertex 4,
// instance_count 3 from base_instance 2: columns 2, 3 and 4 in instance colors
// 2, 3 and 4, and columns 0, 1 and 5 stay at the clear color. Every column is
// asserted analytically; a lost first_index or base_vertex fills the whole
// target, a lost base_instance moves the colors to columns 0..2.
//
// A non-zero base_instance in an indirect command needs
// Device_info::use_base_instance (Vulkan drawIndirectFirstInstance, GL 4.2 /
// ARB_base_instance); the test skips without it.
TEST_F(Gpu_test, draw_parameters_indirect)
{
    constexpr int      column_count   = 6;
    constexpr int      column_width   = 16;
    constexpr int      width          = column_count * column_width;
    constexpr int      height         = 32;
    constexpr uint32_t first_index    = 6;
    constexpr uint32_t base_vertex    = 4;
    constexpr uint32_t base_instance  = 2;
    constexpr uint32_t instance_count = 3;

    erhe::graphics::Device& graphics_device = device();
    if (!graphics_device.get_info().use_base_instance) {
        GTEST_SKIP() << "device does not support a non-zero base instance in indirect draws (Device_info::use_base_instance)";
    }

    constexpr float column_ndc = 2.0f / static_cast<float>(column_count);
    const std::array<float, 24> positions{
        -1.0f, -1.0f,   1.0f, -1.0f,   1.0f,  1.0f,  -1.0f,  1.0f,                             // 0..3  full target
        -1.0f, -1.0f,  -1.0f + column_ndc, -1.0f,  -1.0f + column_ndc, 1.0f,  -1.0f, 1.0f,     // 4..7  column 0
        -1.0f, -1.0f,   1.0f, -1.0f,   1.0f,  1.0f,  -1.0f,  1.0f                              // 8..11 full target
    };
    const std::array<uint16_t, 12> indices{ 4, 5, 6, 4, 6, 7,   0, 1, 2, 0, 2, 3 };

    const std::array<std::array<float, 4>, column_count> colors{{
        {{ 1.0f,  0.0f,  0.0f,  1.0f }},
        {{ 1.0f,  1.0f,  0.0f,  1.0f }},
        {{ 0.0f,  1.0f,  0.0f,  1.0f }},
        {{ 0.0f,  1.0f,  1.0f,  1.0f }},
        {{ 0.0f,  0.0f,  1.0f,  1.0f }},
        {{ 1.0f,  0.0f,  1.0f,  1.0f }}
    }};
    std::array<Instance_record, column_count> instances{};
    for (int i = 0; i < column_count; ++i) {
        instances[static_cast<std::size_t>(i)] = Instance_record{
            .x_offset = static_cast<float>(i) * column_ndc,
            .color    = colors[static_cast<std::size_t>(i)]
        };
    }
    const erhe::graphics::Draw_indexed_primitives_indirect_command command{
        .index_count    = 6,
        .instance_count = instance_count,
        .first_index    = first_index,
        .base_vertex    = base_vertex,
        .base_instance  = base_instance
    };

    const std::shared_ptr<erhe::graphics::Buffer> vertex_buffer   = make_host_buffer(sizeof(positions), erhe::graphics::Buffer_usage::vertex,   "draw parameters vertices");
    const std::shared_ptr<erhe::graphics::Buffer> instance_buffer = make_host_buffer(sizeof(instances), erhe::graphics::Buffer_usage::vertex,   "draw parameters instances");
    const std::shared_ptr<erhe::graphics::Buffer> index_buffer    = make_host_buffer(sizeof(indices),   erhe::graphics::Buffer_usage::index,    "draw parameters indices");
    const std::shared_ptr<erhe::graphics::Buffer> indirect_buffer = make_host_buffer(sizeof(command),   erhe::graphics::Buffer_usage::indirect, "draw parameters command");
    const auto fill = [](erhe::graphics::Buffer& buffer, const void* data, const std::size_t byte_count) {
        const std::span<std::byte> mapped = buffer.map_bytes(0, byte_count);
        std::memcpy(mapped.data(), data, byte_count);
        buffer.unmap();
    };
    fill(*vertex_buffer,   positions.data(), sizeof(positions));
    fill(*instance_buffer, instances.data(), sizeof(instances));
    fill(*index_buffer,    indices.data(),   sizeof(indices));
    fill(*indirect_buffer, &command,         sizeof(command));

    erhe::dataformat::Vertex_stream vertex_stream{
        0,
        {
            erhe::dataformat::Vertex_attribute{
                .format      = erhe::dataformat::Format::format_32_vec2_float,
                .usage_type  = erhe::dataformat::Vertex_attribute_usage::position,
                .usage_index = 0
            }
        }
    };
    erhe::dataformat::Vertex_stream instance_stream{
        1,
        {
            erhe::dataformat::Vertex_attribute{
                .format      = erhe::dataformat::Format::format_32_scalar_float,
                .usage_type  = erhe::dataformat::Vertex_attribute_usage::custom,
                .usage_index = 0
            },
            erhe::dataformat::Vertex_attribute{
                .format      = erhe::dataformat::Format::format_32_vec4_float,
                .usage_type  = erhe::dataformat::Vertex_attribute_usage::color,
                .usage_index = 0
            }
        }
    };
    instance_stream.step = erhe::dataformat::Vertex_step::Step_per_instance;
    ASSERT_EQ(vertex_stream.stride,   2u * sizeof(float));
    ASSERT_EQ(instance_stream.stride, sizeof(Instance_record));
    const erhe::dataformat::Vertex_format    vertex_format{ vertex_stream, instance_stream };
    const erhe::graphics::Vertex_input_state vertex_input{
        graphics_device,
        erhe::graphics::Vertex_input_state_data::make(vertex_format)
    };

    const erhe::graphics::Bind_group_layout empty_layout{
        graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"draw parameters empty layout"},
            .uses_texture_heap = false
        }
    };
    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };
    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "draw_parameters",
        .fragment_outputs = &fragment_outputs,
        .vertex_format    = &vertex_format,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{c_draw_parameters_vertex_source}   },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{c_draw_parameters_fragment_source} }
        },
        .bind_group_layout = &empty_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(graphics_device, shader_create_info);
    ASSERT_TRUE(prototype.is_valid()) << "draw parameters shader failed to compile/link";
    erhe::graphics::Shader_stages shader_stages{graphics_device, std::move(prototype)};

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
    descriptor.debug_label = erhe::utility::Debug_label{"draw parameters"};

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
    const erhe::graphics::Render_pipeline pipeline{graphics_device, pipeline_create_info};
    ASSERT_TRUE(pipeline.is_valid()) << "draw parameters pipeline is not valid";

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass            render_pass{graphics_device, descriptor};
            erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, width, height);
            encoder.set_scissor_rect (0, 0, width, height);
            encoder.set_bind_group_layout(&empty_layout);
            encoder.set_render_pipeline(pipeline);
            encoder.set_vertex_buffer(vertex_buffer.get(),   0, 0);
            encoder.set_vertex_buffer(instance_buffer.get(), 0, 1);
            encoder.set_index_buffer(index_buffer.get());
            encoder.set_buffer(erhe::graphics::Buffer_target::draw_indirect, indirect_buffer.get());
            encoder.multi_draw_indexed_primitives_indirect(
                erhe::graphics::Primitive_type::triangle,
                erhe::dataformat::Format::format_16_scalar_uint,
                0,
                1,
                sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command)
            );
        }
    );

    const std::vector<uint8_t> pixels = read_texture_rgba8(*output);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);

    // Every texel of each column (the columns span whole rows, so the texture
    // origin does not matter) must carry its expected color.
    for (int column = 0; column < column_count; ++column) {
        const bool drawn = (column >= static_cast<int>(base_instance)) && (column < static_cast<int>(base_instance + instance_count));
        const std::array<float, 4> expected_color = drawn ? colors[static_cast<std::size_t>(column)] : std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f };
        int bad = 0;
        for (int y = 0; y < height; ++y) {
            for (int x = column * column_width; x < ((column + 1) * column_width); ++x) {
                const std::size_t i = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x)) * 4u;
                for (std::size_t c = 0; c < 4u; ++c) {
                    const int expected = static_cast<int>(std::lround(expected_color[c] * 255.0f));
                    if (std::abs(static_cast<int>(pixels[i + c]) - expected) > 1) {
                        ++bad;
                        break;
                    }
                }
            }
        }
        EXPECT_EQ(bad, 0) << "column " << column << (drawn ? " (drawn by instance record " : " (clear, record ") << column << "): " << bad << " texels differ";
    }
    expect_image_matches_golden("draw_parameters_indirect", width, height, erhe::dataformat::Format::format_8_vec4_unorm, std::as_bytes(std::span<const uint8_t>{pixels}));
}

} // namespace erhe::graphics::test
