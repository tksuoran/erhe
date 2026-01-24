#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_profile/profile.hpp"

#include "volk.h"
#include "vk_mem_alloc.h"

#include <mutex>

namespace erhe::graphics {

class Buffer_impl final
{
public:
    explicit Buffer_impl(Device& device);
    ~Buffer_impl        () noexcept;
    Buffer_impl         (Device& device, const Buffer_create_info& create_info) noexcept;
    Buffer_impl         (const Buffer_impl&) = delete;
    void operator=      (const Buffer_impl&) = delete;
    Buffer_impl         (Buffer_impl&& old) noexcept;
    auto operator=      (Buffer_impl&& old) noexcept -> Buffer_impl&;

    [[nodiscard]] auto get_capacity_byte_count () const noexcept -> std::size_t;
    [[nodiscard]] auto get_debug_label         () const noexcept -> const std::string&;
    [[nodiscard]] auto get_map                 () const -> std::span<std::byte>;
    void unmap                () noexcept;
    void invalidate           (std::size_t byte_offset, std::size_t byte_count) noexcept;
    void flush_bytes          (std::size_t byte_offset, std::size_t byte_count) noexcept;
    void flush_and_unmap_bytes(std::size_t byte_count) noexcept;
    void dump                 () const noexcept;

    auto begin_write(std::size_t byte_offset, std::size_t byte_count) noexcept -> std::span<std::byte>;
    void end_write  (std::size_t byte_offset, std::size_t byte_count) noexcept;

    template <typename T> [[nodiscard]]
    auto map_elements(const std::size_t element_offset, const std::size_t element_count) noexcept -> std::span<T>
    {
        const std::size_t byte_offset = element_offset * sizeof(T);
        const std::size_t byte_count  = element_count * sizeof(T);
        auto raw_map = map_bytes(byte_offset, byte_count);
        return std::span(
            reinterpret_cast<T*>(raw_map.data()),
            raw_map.size_bytes() / sizeof(T)
        );
    }

    auto map_all_bytes() noexcept -> std::span<std::byte>;

    auto map_bytes(const std::size_t byte_offset, const std::size_t byte_count) noexcept -> std::span<std::byte>;

    [[nodiscard]] auto get_vma_allocation() const -> VmaAllocation;
    [[nodiscard]] auto get_vk_buffer     () const -> VkBuffer;

private:
    friend bool operator==(const Buffer_impl& lhs, const Buffer_impl& rhs) noexcept;
    friend class Buffer_impl_hash;
    friend class Vertex_input_state;
    friend class Texture;

    ERHE_PROFILE_MUTEX(std::mutex, m_allocate_mutex);
    Device&       m_device;
    VmaAllocation m_vma_allocation     {VK_NULL_HANDLE};
    VkBuffer      m_vk_buffer          {VK_NULL_HANDLE};	
    VkMemoryType  m_vk_memory_type     {};
    bool          m_persistently_mapped{false};

    Buffer_usage  m_usage                                 {0};
    uint64_t      m_memory_allocation_create_flag_bit_mask{0};
    uint64_t      m_required_memory_property_bit_mask     {0};
    uint64_t      m_preferred_memory_property_bit_mask    {0};
    std::string   m_debug_label                           {};
    std::size_t   m_capacity_byte_count                   {0};

    std::span<std::byte> m_map_all;
    std::span<std::byte> m_map;
    std::size_t          m_map_byte_offset{0}; // m_map offset in m_map_all
    bool                 m_allocated {false};
};

class Buffer_impl_hash
{
public:
    [[nodiscard]] auto operator()(const Buffer_impl& buffer) const noexcept -> std::size_t
    {
        return
            reinterpret_cast<std::uintptr_t>(buffer.m_vma_allocation) ^
            reinterpret_cast<std::uintptr_t>(buffer.m_vk_buffer);
    }
};

[[nodiscard]] auto operator==(const Buffer_impl& lhs, const Buffer_impl& rhs) noexcept -> bool;
[[nodiscard]] auto operator!=(const Buffer_impl& lhs, const Buffer_impl& rhs) noexcept -> bool;


} // namespace erhe::graphics
