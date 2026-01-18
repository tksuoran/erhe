#pragma once

#include "erhe_graphics/enums.hpp"
#include "erhe_profile/profile.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>

namespace erhe::graphics {

class Device;

class Buffer_create_info
{
public:
    std::size_t  capacity_byte_count                   {0};
    uint64_t     memory_allocation_create_flag_bit_mask{0};
    Buffer_usage usage                                 {0};
    uint64_t     required_memory_property_bit_mask     {0};
    uint64_t     preferred_memory_property_bit_mask    {0};
    const void*  init_data                             {nullptr};
    std::string  debug_label                           {};
};

class Buffer_impl;

class Buffer final
{
public:
    explicit Buffer(Device& device);
    ~Buffer        () noexcept;
    Buffer         (Device& device, const Buffer_create_info& create_info) noexcept;
    Buffer         (const Buffer&) = delete;
    void operator= (const Buffer&) = delete;
    Buffer         (Buffer&& other) noexcept;
    auto operator= (Buffer&& other) noexcept -> Buffer&;

    [[nodiscard]] auto get_debug_label        () const noexcept -> const std::string&;
    [[nodiscard]] auto get_map                () const -> std::span<std::byte>;
    [[nodiscard]] auto get_capacity_byte_count() const noexcept -> std::size_t;

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

    auto map_bytes(std::size_t byte_offset, std::size_t byte_count) noexcept -> std::span<std::byte>;

    [[nodiscard]] auto get_impl() -> Buffer_impl&;
    [[nodiscard]] auto get_impl() const -> const Buffer_impl&;

    friend class Vertex_input_state;
    friend class Texture;

private:
    std::unique_ptr<Buffer_impl> m_impl;
};

class Buffer_allocator
{
public:
    explicit Buffer_allocator(Buffer* buffer);
    ~Buffer_allocator() noexcept;
    Buffer_allocator(Buffer_allocator&&) noexcept;
    auto operator=(Buffer_allocator&&) noexcept -> Buffer_allocator&;

    void clear() noexcept;
    [[nodiscard]] auto allocate_bytes          (std::size_t byte_count, std::size_t alignment = 64) noexcept -> std::optional<std::size_t>;
    [[nodiscard]] auto get_used_byte_count     () const -> std::size_t;
    [[nodiscard]] auto get_available_byte_count(std::size_t alignment = 64) const noexcept -> std::size_t;
    [[nodiscard]] auto get_buffer              () -> Buffer*;
    [[nodiscard]] auto get_buffer              () const -> const Buffer*;

private:
    ERHE_PROFILE_MUTEX(std::mutex, m_allocate_mutex);
    Buffer*     m_buffer{nullptr};
    std::size_t m_next_free_byte{0};
};

} // namespace erhe::graphics
