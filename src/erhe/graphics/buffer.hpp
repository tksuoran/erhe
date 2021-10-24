#pragma once

#include "erhe/graphics/gl_objects.hpp"
#include "erhe/graphics/span.hpp"

#include <gsl/span>

#include <string_view>
#include <mutex>

namespace erhe::graphics
{

class Buffer final
{
public:
    Buffer () = default;
    ~Buffer();

    Buffer(
        const gl::Buffer_target       target,
        const size_t                  capacity_bytes_count,
        const gl::Buffer_storage_mask storage_mask
    ) noexcept;

    Buffer(
        const gl::Buffer_target          target,
        const size_t                     capacity_byte_count,
        const gl::Buffer_storage_mask    storage_mask,
        const gl::Map_buffer_access_mask map_buffer_access_mask
    ) noexcept;

    Buffer        (const Buffer&) = delete;
    void operator=(const Buffer&) = delete;
    Buffer        (Buffer&& other) noexcept;
    auto operator=(Buffer&& other) noexcept -> Buffer&;

    auto map                  () const          -> gsl::span<std::byte>;
    auto debug_label          () const noexcept -> const std::string&;
    auto capacity_byte_count  () const noexcept -> size_t;
    auto allocate_bytes       (const size_t byte_count, const size_t alignment = 64) noexcept -> size_t;
    void unmap                () noexcept;
    void flush_bytes          (const size_t byte_offset, const size_t byte_count) noexcept;
    void flush_and_unmap_bytes(const size_t byte_count) noexcept;
    auto free_capacity_bytes  () const noexcept -> size_t;
    auto target               () const noexcept -> gl::Buffer_target;
    void set_debug_label      (std::string_view label) noexcept;
    void dump                 () const noexcept;
    auto gl_name              () const noexcept -> unsigned int;

    template <typename T>
    auto map_elements(
        const size_t                     element_offset,
        const size_t                     element_count,
        const gl::Map_buffer_access_mask access_mask
    ) noexcept -> gsl::span<T>
    {
        const size_t byte_offset = element_offset * sizeof(T);
        const size_t byte_count  = element_count * sizeof(T);
        auto raw_map = map_bytes(byte_offset, byte_count, access_mask);
        return gsl::span(
            reinterpret_cast<T*>(raw_map.data()),
            raw_map.size_bytes() / sizeof(T)
        );
    }

    auto map_all_bytes(const gl::Map_buffer_access_mask access_mask) noexcept
        -> gsl::span<std::byte>;

    auto map_bytes(
        const size_t                     byte_offset,
        const size_t                     byte_count,
        const gl::Map_buffer_access_mask access_mask
    ) noexcept -> gsl::span<std::byte>;

    friend class Vertex_input_state;
    friend class Texture;

private:
    Gl_buffer               m_handle;
    std::string             m_debug_label;
    gl::Buffer_target       m_target             {gl::Buffer_target::array_buffer};
    size_t                  m_capacity_byte_count{0};
    size_t                  m_next_free_byte     {0};
    gl::Buffer_storage_mask m_storage_mask       {0};
    std::mutex              m_allocate_mutex;

    // Last MapBuffer
    gsl::span<std::byte>        m_map;
    size_t                      m_map_byte_offset       {0};
    gl::Map_buffer_access_mask  m_map_buffer_access_mask{0};
};

class Buffer_hash
{
public:
    auto operator()(const Buffer& buffer) const noexcept
    -> size_t
    {
        return static_cast<size_t>(buffer.gl_name());
    }
};

auto operator==(const Buffer& lhs, const Buffer& rhs) noexcept -> bool;
auto operator!=(const Buffer& lhs, const Buffer& rhs) noexcept -> bool;

} // namespace erhe::graphics
