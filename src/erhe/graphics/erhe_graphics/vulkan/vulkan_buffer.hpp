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
    [[nodiscard]] auto allocate_bytes          (std::size_t byte_count, std::size_t alignment = 64) noexcept -> std::optional<std::size_t>;
    [[nodiscard]] auto get_debug_label         () const noexcept -> const std::string&;
    [[nodiscard]] auto get_used_byte_count     () const -> std::size_t;
    [[nodiscard]] auto get_available_byte_count(std::size_t alignment = 64) const noexcept -> std::size_t;

    [[nodiscard]] auto get_map                 () const -> std::span<std::byte>;
    void clear                () noexcept;
    void unmap                () noexcept;
    void flush_bytes          (std::size_t byte_offset, std::size_t byte_count) noexcept;
    void flush_and_unmap_bytes(std::size_t byte_count) noexcept;
    void dump                 () const noexcept;

    auto begin_write(std::size_t byte_offset, std::size_t byte_count) noexcept -> std::span<std::byte>;
    void end_write  (std::size_t byte_offset, std::size_t byte_count) noexcept;

    template <typename T>
    [[nodiscard]]
    auto map_elements(const std::size_t element_offset, const std::size_t element_count, Buffer_map_flags flags = Buffer_map_flags::none) noexcept -> std::span<T>
    {
        const std::size_t byte_offset = element_offset * sizeof(T);
        const std::size_t byte_count  = element_count * sizeof(T);
        auto raw_map = map_bytes(byte_offset, byte_count, flags);
        return std::span(
            reinterpret_cast<T*>(raw_map.data()),
            raw_map.size_bytes() / sizeof(T)
        );
    }

    auto map_all_bytes(Buffer_map_flags flags) noexcept -> std::span<std::byte>;

    auto map_bytes(const std::size_t byte_offset, const std::size_t byte_count, Buffer_map_flags flags) noexcept -> std::span<std::byte>;

private:
    friend bool operator==(const Buffer_impl& lhs, const Buffer_impl& rhs) noexcept;
    friend class Buffer_impl_hash;
    friend class Vertex_input_state;
    friend class Texture;

    ERHE_PROFILE_MUTEX(std::mutex, m_allocate_mutex);
    Device&       m_device;
    VmaAllocation m_vma_allocation     {VK_NULL_HANDLE};
    VkBuffer      m_vk_buffer          {VK_NULL_HANDLE};	
    std::string   m_debug_label        {};
    std::size_t   m_capacity_byte_count{0};
    std::size_t   m_next_free_byte     {0};
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
