#pragma once

#include "erhe_graphics/enums.hpp"

#include <optional>
#include <span>
#include <string>

namespace erhe::graphics {

class Device;

class Buffer_create_info
{
public:
    std::size_t       capacity_byte_count {0};
    Buffer_usage      usage               {0};
    Buffer_direction  direction           {0};
    Buffer_cache_mode cache_mode          {0};
    Buffer_mapping    mapping             {0};
    Buffer_coherency  coherency           {0};
    const void*       init_data           {nullptr};
    std::string       debug_label         {};
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

    [[nodiscard]] auto get_impl() -> Buffer_impl&;
    [[nodiscard]] auto get_impl() const -> const Buffer_impl&;

    friend class Vertex_input_state;
    friend class Texture;

private:
    std::unique_ptr<Buffer_impl> m_impl;
};

} // namespace erhe::graphics
