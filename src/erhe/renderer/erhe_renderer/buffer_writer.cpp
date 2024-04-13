#include "erhe_renderer/buffer_writer.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_renderer/renderer_log.hpp"
#include "erhe_verify/verify.hpp"

#include "igl/Buffer.h"
#include "igl/vulkan/Buffer.h"
#include "igl/vulkan/VulkanBuffer.h"
#include "igl/Device.h"

#include <sstream>

namespace erhe::renderer
{

Buffer_writer::Buffer_writer(igl::IDevice& device)
    : m_device{device}
{
}

auto Buffer_writer::begin(
    const igl::IBuffer* buffer,
    std::size_t         byte_count
) -> std::span<std::byte>
{
    ERHE_VERIFY(m_buffer == nullptr);
    m_buffer = buffer;

    using namespace erhe::bit;
    const igl::BufferDesc::BufferType buffer_type = buffer->getBufferType();
    bool uniform_buffer        = test_all_rhs_bits_set(buffer_type, static_cast<igl::BufferDesc::BufferType>(igl::BufferDesc::BufferTypeBits::Uniform));
    bool shader_storage_buffer = test_all_rhs_bits_set(buffer_type, static_cast<igl::BufferDesc::BufferType>(igl::BufferDesc::BufferTypeBits::Storage));
    std::size_t required_binding_offset_alignment = 1;
    if (uniform_buffer) {
        std::size_t uniform_buffer_aligment = 1;
        m_device.getFeatureLimits(igl::DeviceFeatureLimits::BufferAlignment, uniform_buffer_aligment);
        required_binding_offset_alignment = std::max(required_binding_offset_alignment, uniform_buffer_aligment);
    }
    if (shader_storage_buffer) {
        std::size_t shader_storage_buffer_aligment = 1;
        m_device.getFeatureLimits(igl::DeviceFeatureLimits::ShaderStorageBufferOffsetAlignment, shader_storage_buffer_aligment);
        required_binding_offset_alignment = std::max(required_binding_offset_alignment, shader_storage_buffer_aligment);
    }

    write_offset = erhe::math::align(write_offset, required_binding_offset_alignment);

    if (byte_count == 0) {
        byte_count = buffer->getSizeInBytes() - write_offset;
    } else {
        byte_count = std::min(byte_count, buffer->getSizeInBytes() - write_offset);
    }

    // The whole buffer is always mapped - return subspan for requested range
    auto* vk_buffer = static_cast<const igl::vulkan::Buffer*>(buffer);
    const std::shared_ptr<igl::vulkan::VulkanBuffer>& vulkan_buffer = vk_buffer->currentVulkanBuffer();
    uint8_t* const map_ptr = vulkan_buffer->getMappedPtr();
    assert(vulkan_buffer->isCoherentMemory());

    write_end               = write_offset + byte_count;
    range.first_byte_offset = write_offset;
    write_offset = 0;

    m_range = std::span<std::byte>{reinterpret_cast<std::byte*>(map_ptr + write_offset), byte_count};
    return m_range;
}

auto Buffer_writer::subspan(const std::size_t byte_count) -> std::span<std::byte>
{
    ERHE_VERIFY(m_range.size() >= write_offset + byte_count);
    auto result = m_range.subspan(write_offset, byte_count);
    write_offset += byte_count;
    return result;
}

void Buffer_writer::dump()
{
    if (m_range.empty() || (m_buffer == nullptr))
    {
        return;
    }

    const auto* vk_buffer = static_cast<const igl::vulkan::Buffer*>(m_buffer);
    const std::shared_ptr<igl::vulkan::VulkanBuffer>& vulkan_buffer = vk_buffer->currentVulkanBuffer();
    uint8_t* const map_ptr = vulkan_buffer->getMappedPtr();
    assert(vulkan_buffer->isCoherentMemory());

    uint8_t* data = reinterpret_cast<uint8_t*>(map_ptr);


    const std::size_t byte_count = m_range.size();
    const std::size_t word_count{byte_count / sizeof(uint32_t)};

    std::stringstream ss;
    for (std::size_t i = 0; i < word_count; ++i) {
        if (i % 16u == 0) {
            ss << fmt::format("{:04x}: ", static_cast<unsigned int>(i));
        }

        ss << fmt::format("{:02x} ", data[i]);

        if (i % 16u == 15u) {
            log_buffer_writer->info(ss.str());
            ss = std::stringstream();
        }
    }
    if (!ss.str().empty()) {
        log_buffer_writer->info(ss.str());
    }
}

void Buffer_writer::end()
{
    ERHE_VERIFY(m_buffer != nullptr);

    //if (!m_instance.info.use_persistent_buffers) {
    //    range.byte_count = write_offset;
    //    m_buffer->end_write(map_offset, write_offset);
    //    write_offset += map_offset;
    //    map_offset = 0;
    //    write_end = 0;
    //} else {
    range.byte_count = write_offset;
    write_offset += range.first_byte_offset;
    //}

    m_buffer = nullptr;
    m_range = {};

}

void Buffer_writer::reset()
{
    range.first_byte_offset = 0;
    range.byte_count        = 0;
    map_offset              = 0;
    write_offset            = 0;
    m_range                 = {};
}

} // namespace erhe::renderer
