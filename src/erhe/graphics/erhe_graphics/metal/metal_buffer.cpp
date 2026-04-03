#include "erhe_graphics/metal/metal_buffer.hpp"
#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_verify/verify.hpp"

#include <Metal/Metal.hpp>

#include <cstring>

namespace erhe::graphics {

Buffer_impl::Buffer_impl(Device& device)
    : m_device{device}
{
}

Buffer_impl::Buffer_impl(Device& device, const Buffer_create_info& create_info) noexcept
    : m_device             {device}
    , m_capacity_byte_count{create_info.capacity_byte_count}
    , m_usage              {create_info.usage}
    , m_debug_label        {create_info.debug_label}
{
    Device_impl& device_impl = device.get_impl();
    MTL::Device* mtl_device = device_impl.get_mtl_device();
    if ((mtl_device != nullptr) && (m_capacity_byte_count > 0)) {
        m_mtl_buffer = mtl_device->newBuffer(m_capacity_byte_count, MTL::ResourceStorageModeShared);
        if (m_mtl_buffer != nullptr) {
            // StorageModeShared: always CPU-accessible, keep persistently mapped
            m_map = std::span<std::byte>(static_cast<std::byte*>(m_mtl_buffer->contents()), m_capacity_byte_count);
            if (create_info.init_data != nullptr) {
                std::memcpy(m_mtl_buffer->contents(), create_info.init_data, m_capacity_byte_count);
            }
        }
        if ((m_mtl_buffer != nullptr) && !m_debug_label.empty()) {
            NS::String* label = NS::String::alloc()->init(
                m_debug_label.data(),
                NS::UTF8StringEncoding
            );
            m_mtl_buffer->setLabel(label);
            label->release();
        }
    }
}

Buffer_impl::~Buffer_impl() noexcept
{
    if (m_mtl_buffer != nullptr) {
        m_mtl_buffer->release();
        m_mtl_buffer = nullptr;
    }
}

Buffer_impl::Buffer_impl(Buffer_impl&& other) noexcept
    : m_device             {other.m_device}
    , m_mtl_buffer         {other.m_mtl_buffer}
    , m_map                {other.m_map}
    , m_capacity_byte_count{other.m_capacity_byte_count}
    , m_usage              {other.m_usage}
    , m_debug_label        {other.m_debug_label}
{
    other.m_mtl_buffer = nullptr;
    other.m_map = {};
    other.m_capacity_byte_count = 0;
}

auto Buffer_impl::operator=(Buffer_impl&& other) noexcept -> Buffer_impl&
{
    if (this != &other) {
        if (m_mtl_buffer != nullptr) {
            m_mtl_buffer->release();
        }
        m_mtl_buffer          = other.m_mtl_buffer;
        m_map                 = other.m_map;
        m_capacity_byte_count = other.m_capacity_byte_count;
        m_usage               = other.m_usage;
        m_debug_label         = other.m_debug_label;
        other.m_mtl_buffer = nullptr;
        other.m_map = {};
        other.m_capacity_byte_count = 0;
    }
    return *this;
}

auto Buffer_impl::get_capacity_byte_count() const noexcept -> std::size_t { return m_capacity_byte_count; }
auto Buffer_impl::get_debug_label() const noexcept -> erhe::utility::Debug_label { return m_debug_label; }
auto Buffer_impl::get_map() const -> std::span<std::byte> { return m_map; }
auto Buffer_impl::gl_name() const noexcept -> unsigned int { return 0; }
auto Buffer_impl::get_mtl_buffer() const noexcept -> MTL::Buffer* { return m_mtl_buffer; }

void Buffer_impl::unmap() noexcept { m_map = {}; }

void Buffer_impl::invalidate(const std::size_t byte_offset, const std::size_t byte_count) noexcept
{
    static_cast<void>(byte_offset);
    static_cast<void>(byte_count);
}

void Buffer_impl::flush_bytes(const std::size_t byte_offset, const std::size_t byte_count) noexcept
{
    static_cast<void>(byte_offset);
    static_cast<void>(byte_count);
    // StorageModeShared on macOS: no explicit flush needed
}

void Buffer_impl::flush_and_unmap_bytes(const std::size_t byte_count) noexcept
{
    static_cast<void>(byte_count);
    m_map = {};
}

void Buffer_impl::upload_sub_data(const std::size_t byte_offset, const std::size_t byte_count, const void* data) noexcept
{
    ERHE_VERIFY(byte_offset + byte_count <= m_capacity_byte_count);
    if ((data != nullptr) && (m_mtl_buffer != nullptr)) {
        std::memcpy(static_cast<std::byte*>(m_mtl_buffer->contents()) + byte_offset, data, byte_count);
    }
}

void Buffer_impl::dump() const noexcept {}

auto Buffer_impl::begin_write(const std::size_t byte_offset, const std::size_t byte_count) noexcept -> std::span<std::byte>
{
    return map_bytes(byte_offset, byte_count);
}

void Buffer_impl::end_write(const std::size_t byte_offset, const std::size_t byte_count) noexcept
{
    static_cast<void>(byte_offset);
    static_cast<void>(byte_count);
}

auto Buffer_impl::map_all_bytes() noexcept -> std::span<std::byte>
{
    if (m_mtl_buffer != nullptr) {
        m_map = std::span<std::byte>(static_cast<std::byte*>(m_mtl_buffer->contents()), m_capacity_byte_count);
    } else {
        m_map = {};
    }
    return m_map;
}

auto Buffer_impl::map_bytes(const std::size_t byte_offset, const std::size_t byte_count) noexcept -> std::span<std::byte>
{
    ERHE_VERIFY(byte_offset + byte_count <= m_capacity_byte_count);
    if (m_mtl_buffer != nullptr) {
        m_map = std::span<std::byte>(static_cast<std::byte*>(m_mtl_buffer->contents()) + byte_offset, byte_count);
    } else {
        m_map = {};
    }
    return m_map;
}

auto operator==(const Buffer_impl& lhs, const Buffer_impl& rhs) noexcept -> bool { return &lhs == &rhs; }
auto operator!=(const Buffer_impl& lhs, const Buffer_impl& rhs) noexcept -> bool { return !(lhs == rhs); }

} // namespace erhe::graphics
