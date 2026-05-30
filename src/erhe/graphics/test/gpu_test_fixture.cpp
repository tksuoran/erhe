#include "gpu_test_fixture.hpp"
#include "gpu_test_environment.hpp"

#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/texture.hpp"

#include <glm/glm.hpp>

#include <cstring>
#include <iostream>
#include <span>
#include <string>
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
    const erhe::dataformat::Format format
) -> std::shared_ptr<erhe::graphics::Texture>
{
    erhe::graphics::Device& graphics_device = device();
    const erhe::graphics::Texture_create_info create_info{
        .device      = graphics_device,
        .usage_mask  =
            erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
            erhe::graphics::Image_usage_flag_bit_mask::sampled          |
            erhe::graphics::Image_usage_flag_bit_mask::transfer_src,
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

auto Gpu_test::make_readback_buffer(const std::size_t byte_count, const char* debug_label)
    -> std::shared_ptr<erhe::graphics::Buffer>
{
    erhe::graphics::Device& graphics_device = device();
    const erhe::graphics::Buffer_create_info create_info{
        .capacity_byte_count                    = byte_count,
        .memory_allocation_create_flag_bit_mask = erhe::graphics::Memory_allocation_create_flag_bit_mask::mapped,
        .usage                                  =
            erhe::graphics::Buffer_usage::transfer_dst |
            erhe::graphics::Buffer_usage::storage,
        .required_memory_property_bit_mask      =
            erhe::graphics::Memory_property_flag_bit_mask::host_read  |
            erhe::graphics::Memory_property_flag_bit_mask::host_write |
            erhe::graphics::Memory_property_flag_bit_mask::host_coherent,
        .debug_label = erhe::utility::Debug_label{debug_label}
    };
    return std::make_shared<erhe::graphics::Buffer>(graphics_device, create_info);
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

} // namespace erhe::graphics::test
