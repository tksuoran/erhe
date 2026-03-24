#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_utility/debug_label.hpp"

#include <span>
#include <vector>

namespace erhe::graphics {

class Buffer_impl final
{
public:
    explicit Buffer_impl(Device& device);
    ~Buffer_impl        () noexcept;
    Buffer_impl         (Device& device, const Buffer_create_info& create_info) noexcept;
    Buffer_impl         (const Buffer_impl&) = delete;
    void operator=      (const Buffer_impl&) = delete;
    Buffer_impl         (Buffer_impl&& other) noexcept;
    auto operator=      (Buffer_impl&& other) noexcept -> Buffer_impl&;

    [[nodiscard]] auto get_capacity_byte_count () const noexcept -> std::size_t;
    [[nodiscard]] auto get_debug_label         () const noexcept -> erhe::utility::Debug_label;
    [[nodiscard]] auto get_map                 () const -> std::span<std::byte>;
    [[nodiscard]] auto gl_name                 () const noexcept -> unsigned int;
    void unmap                () noexcept;
    void invalidate           (std::size_t byte_offset, std::size_t byte_count) noexcept;
    void flush_bytes          (std::size_t byte_offset, std::size_t byte_count) noexcept;
    void flush_and_unmap_bytes(std::size_t byte_count) noexcept;
    void dump                 () const noexcept;
    auto begin_write          (std::size_t byte_offset, std::size_t byte_count) noexcept -> std::span<std::byte>;
    void end_write            (std::size_t byte_offset, std::size_t byte_count) noexcept;
    auto map_all_bytes        () noexcept -> std::span<std::byte>;
    auto map_bytes            (std::size_t byte_offset, std::size_t byte_count) noexcept -> std::span<std::byte>;

    template <typename T>
    [[nodiscard]]
    auto map_elements(const std::size_t element_offset, const std::size_t element_count) noexcept -> std::span<T>
    {
        const std::size_t byte_offset = element_offset * sizeof(T);
        const std::size_t byte_count  = element_count  * sizeof(T);
        std::span<std::byte> raw_map = map_bytes(byte_offset, byte_count);
        return std::span(
            reinterpret_cast<T*>(raw_map.data()),
            raw_map.size_bytes() / sizeof(T)
        );
    }

    friend class Vertex_input_state;
    friend class Texture;

private:
    Device&                    m_device;
    std::vector<std::byte>     m_storage;
    std::span<std::byte>       m_map;
    std::size_t                m_capacity_byte_count{0};
    Buffer_usage               m_usage              {0};
    erhe::utility::Debug_label m_debug_label        {};
};

class Buffer_impl_hash
{
public:
    [[nodiscard]] auto operator()(const Buffer_impl& buffer) const noexcept -> std::size_t
    {
        static_cast<void>(buffer);
        return 0;
    }
};

[[nodiscard]] auto operator==(const Buffer_impl& lhs, const Buffer_impl& rhs) noexcept -> bool;
[[nodiscard]] auto operator!=(const Buffer_impl& lhs, const Buffer_impl& rhs) noexcept -> bool;

} // namespace erhe::graphics
