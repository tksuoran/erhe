#include "gpu_test_fixture.hpp"
#include "gpu_test_environment.hpp"

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
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
#include "erhe_graphics/texture.hpp"

#include <glm/glm.hpp>

#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics::test {

void Gpu_test::SetUp()
{
    Gpu_test_environment::get().clear_messages();
}

void Gpu_test::TearDown()
{
    erhe::graphics::Device& graphics_device = device();
    graphics_device.wait_idle();
    const std::vector<Gpu_test_environment::Message> messages = Gpu_test_environment::get().take_messages();
    for (const Gpu_test_environment::Message& message : messages) {
        if (message.first) {
            ADD_FAILURE() << "Device validation error during test: " << message.second;
        } else {
            // Best-practices / advisory warnings are surfaced but not fatal,
            // matching the editor's device-message policy (error -> fatal,
            // warning -> log).
            std::cerr << "[ vk-warn  ] " << message.second << "\n";
        }
    }
}

auto Gpu_test::device() -> erhe::graphics::Device&
{
    return Gpu_test_environment::get().device();
}

void Gpu_test::submit_and_wait(const std::function<void(erhe::graphics::Command_buffer&)>& record_fn)
{
    erhe::graphics::Device& graphics_device = device();

    const bool wait_frame_ok = graphics_device.wait_frame();
    ASSERT_TRUE(wait_frame_ok);

    erhe::graphics::Command_buffer& command_buffer = graphics_device.get_command_buffer(0);
    command_buffer.begin();
    record_fn(command_buffer);
    command_buffer.end();

    erhe::graphics::Command_buffer* command_buffers[] = { &command_buffer };
    graphics_device.submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{command_buffers});
    graphics_device.wait_idle();

    const bool end_frame_ok = graphics_device.end_frame();
    ASSERT_TRUE(end_frame_ok);
}

auto Gpu_test::make_color_target(
    const int                      width,
    const int                      height,
    const erhe::dataformat::Format format,
    const bool                     include_transfer_dst
) -> std::shared_ptr<erhe::graphics::Texture>
{
    erhe::graphics::Device& graphics_device = device();
    const uint64_t usage_mask =
        erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
        erhe::graphics::Image_usage_flag_bit_mask::sampled          |
        erhe::graphics::Image_usage_flag_bit_mask::transfer_src      |
        (include_transfer_dst ? erhe::graphics::Image_usage_flag_bit_mask::transfer_dst : uint64_t{0});
    const erhe::graphics::Texture_create_info create_info{
        .device      = graphics_device,
        .usage_mask  = usage_mask,
        .type        = erhe::graphics::Texture_type::texture_2d,
        .pixelformat = format,
        .width       = width,
        .height      = height,
        .debug_label = erhe::utility::Debug_label{"gpu_test color target"}
    };
    std::shared_ptr<erhe::graphics::Texture> texture =
        std::make_shared<erhe::graphics::Texture>(graphics_device, create_info);

    // Move the fresh texture from UNDEFINED into transfer_src_optimal so render
    // passes can uniformly declare usage_before / layout_before = transfer_src /
    // transfer_src_optimal (the Id_renderer readback pattern), and so
    // read_texture_rgba8's blit always finds it in transfer_src_optimal.
    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            command_buffer.transition_texture_layout(*texture, erhe::graphics::Image_layout::transfer_src_optimal);
        }
    );
    return texture;
}

auto Gpu_test::make_host_buffer(
    const std::size_t            byte_count,
    erhe::graphics::Buffer_usage usage,
    const char*                  debug_label
) -> std::shared_ptr<erhe::graphics::Buffer>
{
    erhe::graphics::Device& graphics_device = device();
    const erhe::graphics::Buffer_create_info create_info{
        .capacity_byte_count                    = byte_count,
        .memory_allocation_create_flag_bit_mask = erhe::graphics::Memory_allocation_create_flag_bit_mask::mapped,
        .usage                                  = usage,
        .required_memory_property_bit_mask      =
            erhe::graphics::Memory_property_flag_bit_mask::host_read  |
            erhe::graphics::Memory_property_flag_bit_mask::host_write |
            erhe::graphics::Memory_property_flag_bit_mask::host_coherent,
        .debug_label = erhe::utility::Debug_label{debug_label}
    };
    return std::make_shared<erhe::graphics::Buffer>(graphics_device, create_info);
}

auto Gpu_test::make_readback_buffer(const std::size_t byte_count, const char* debug_label)
    -> std::shared_ptr<erhe::graphics::Buffer>
{
    return make_host_buffer(
        byte_count,
        erhe::graphics::Buffer_usage::transfer_dst | erhe::graphics::Buffer_usage::storage,
        debug_label
    );
}

auto Gpu_test::read_buffer(erhe::graphics::Buffer& buffer, std::size_t byte_count)
    -> std::vector<std::byte>
{
    if (byte_count == 0) {
        byte_count = buffer.get_capacity_byte_count();
    }
    // make_readback_buffer allocates host_coherent memory, so GPU writes are
    // visible to the host once the GPU is idle -- no vkInvalidateMappedMemoryRanges
    // is needed. (Calling invalidate would additionally require the range size to
    // be a multiple of VkPhysicalDeviceLimits::nonCoherentAtomSize, which an
    // arbitrary byte_count is not -- VUID-VkMappedMemoryRange-size-01390.)
    const std::span<std::byte> mapped = buffer.map_bytes(0, byte_count);
    std::vector<std::byte> out(byte_count);
    std::memcpy(out.data(), mapped.data(), byte_count);
    buffer.unmap();
    return out;
}

auto Gpu_test::read_texture_rgba8(const erhe::graphics::Texture& texture)
    -> std::vector<uint8_t>
{
    const int         width         = texture.get_width();
    const int         height        = texture.get_height();
    const std::size_t bytes_per_row = static_cast<std::size_t>(width) * 4u;
    const std::size_t byte_count    = bytes_per_row * static_cast<std::size_t>(height);

    std::shared_ptr<erhe::graphics::Buffer> readback = make_readback_buffer(byte_count, "read_texture_rgba8");

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Blit_command_encoder blit = device().make_blit_command_encoder(command_buffer);
            blit.copy_from_texture(
                &texture,
                0,                              // source_slice
                0,                              // source_level
                glm::ivec3{0, 0, 0},            // source_origin
                glm::ivec3{width, height, 1},   // source_size
                readback.get(),                 // destination_buffer
                0,                              // destination_offset
                static_cast<std::uintptr_t>(bytes_per_row),
                static_cast<std::uintptr_t>(byte_count)
            );
        }
    );

    const std::vector<std::byte> raw = read_buffer(*readback, byte_count);
    std::vector<uint8_t> out(byte_count);
    std::memcpy(out.data(), raw.data(), byte_count);
    return out;
}

auto Gpu_test::draw_fullscreen_triangle(
    const char*                                fragment_color_glsl,
    const erhe::graphics::Rasterization_state& rasterization,
    const erhe::graphics::Color_blend_state&   color_blend,
    std::array<double, 4>                      clear_value,
    const int                                  width,
    const int                                  height
) -> std::vector<uint8_t>
{
    const std::shared_ptr<erhe::graphics::Texture> color_target = make_color_target(width, height);

    const erhe::graphics::Bind_group_layout empty_layout{
        device(),
        erhe::graphics::Bind_group_layout_create_info{
            .bindings          = {},
            .debug_label       = erhe::utility::Debug_label{"fullscreen empty layout"},
            .uses_texture_heap = false
        }
    };
    const erhe::graphics::Fragment_outputs fragment_outputs{
        { erhe::graphics::Fragment_output{ .name = "out_color", .type = erhe::graphics::Glsl_type::float_vec4, .location = 0 } }
    };

    static constexpr const char* vertex_source = R"glsl(
void main()
{
    vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
)glsl";
    static constexpr const char* fragment_source = R"glsl(
void main()
{
    out_color = FRAG_COLOR;
}
)glsl";

    erhe::graphics::Shader_stages_create_info shader_create_info{
        .name             = "fullscreen_triangle",
        .defines          = { { "FRAG_COLOR", fragment_color_glsl } },
        .fragment_outputs = &fragment_outputs,
        .no_vertex_input  = true,
        .shaders = {
            { erhe::graphics::Shader_type::vertex_shader,   std::string_view{vertex_source}   },
            { erhe::graphics::Shader_type::fragment_shader, std::string_view{fragment_source} }
        },
        .bind_group_layout = &empty_layout
    };
    erhe::graphics::Shader_stages_prototype prototype = erhe::graphics::build_shader_stages(device(), shader_create_info);
    if (!prototype.is_valid()) {
        ADD_FAILURE() << "draw_fullscreen_triangle: shader failed to compile/link";
        return {};
    }
    erhe::graphics::Shader_stages shader_stages{device(), std::move(prototype)};

    erhe::graphics::Render_pass_descriptor descriptor{};
    descriptor.color_attachments[0].texture       = color_target.get();
    descriptor.color_attachments[0].clear_value   = clear_value;
    descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
    descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
    descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
    descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::transfer_src_optimal;
    descriptor.render_target_width  = width;
    descriptor.render_target_height = height;
    descriptor.debug_label = erhe::utility::Debug_label{"fullscreen triangle"};

    erhe::graphics::Render_pipeline_create_info pipeline_create_info;
    pipeline_create_info.base.input_assembly                    = erhe::graphics::Input_assembly_state::triangle;
    pipeline_create_info.base.rasterization                     = rasterization;
    pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
    pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
    pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
    pipeline_create_info.base.bind_group_layout                 = &empty_layout;
    pipeline_create_info.base.color_blend                       = &color_blend;
    pipeline_create_info.shader_stages                          = &shader_stages;
    pipeline_create_info.vertex_input                           = nullptr;
    pipeline_create_info.set_format_from_render_pass(descriptor);
    const erhe::graphics::Render_pipeline pipeline{device(), pipeline_create_info};
    if (!pipeline.is_valid()) {
        ADD_FAILURE() << "draw_fullscreen_triangle: pipeline is not valid";
        return {};
    }

    submit_and_wait(
        [&](erhe::graphics::Command_buffer& command_buffer) {
            erhe::graphics::Render_pass            render_pass{device(), descriptor};
            erhe::graphics::Render_command_encoder encoder = device().make_render_command_encoder(command_buffer);
            const erhe::graphics::Scoped_render_pass scoped{render_pass, command_buffer};
            encoder.set_viewport_rect(0, 0, width, height);
            encoder.set_scissor_rect (0, 0, width, height);
            encoder.set_bind_group_layout(&empty_layout);
            encoder.set_render_pipeline(pipeline);
            encoder.draw_primitives(erhe::graphics::Primitive_type::triangle, 0, 3);
        }
    );

    return read_texture_rgba8(*color_target);
}

} // namespace erhe::graphics::test
