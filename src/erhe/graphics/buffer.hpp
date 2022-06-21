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
    Buffer ();
    ~Buffer() noexcept;

    Buffer(
        const gl::Buffer_target       target,
        const std::size_t             capacity_bytes_count,
        const gl::Buffer_storage_mask storage_mask
    ) noexcept;

    Buffer(
        const gl::Buffer_target          target,
        const std::size_t                capacity_byte_count,
        const gl::Buffer_storage_mask    storage_mask,
        const gl::Map_buffer_access_mask map_buffer_access_mask
    ) noexcept;

    Buffer        (const Buffer&) = delete;
    void operator=(const Buffer&) = delete;
    Buffer        (Buffer&& other) noexcept;
    auto operator=(Buffer&& other) noexcept -> Buffer&;

    [[nodiscard]] auto map                () const          -> gsl::span<std::byte>;
    [[nodiscard]] auto debug_label        () const noexcept -> const std::string&;
    [[nodiscard]] auto capacity_byte_count() const noexcept -> std::size_t;
    [[nodiscard]] auto allocate_bytes     (const std::size_t byte_count, const std::size_t alignment = 64) noexcept -> std::size_t;
    [[nodiscard]] auto free_capacity_bytes() const noexcept -> std::size_t;
    [[nodiscard]] auto target             () const noexcept -> gl::Buffer_target;
    [[nodiscard]] auto gl_name            () const noexcept -> unsigned int;
    void unmap                () noexcept;
    void flush_bytes          (const std::size_t byte_offset, const std::size_t byte_count) noexcept;
    void flush_and_unmap_bytes(const std::size_t byte_count) noexcept;
    void set_debug_label      (const std::string& label) noexcept;
    void dump                 () const noexcept;

    template <typename T>
    [[nodiscard]]
    auto map_elements(
        const std::size_t                element_offset,
        const std::size_t                element_count,
        const gl::Map_buffer_access_mask access_mask
    ) noexcept -> gsl::span<T>
    {
        const std::size_t byte_offset = element_offset * sizeof(T);
        const std::size_t byte_count  = element_count * sizeof(T);
        auto raw_map = map_bytes(byte_offset, byte_count, access_mask);
        return gsl::span(
            reinterpret_cast<T*>(raw_map.data()),
            raw_map.size_bytes() / sizeof(T)
        );
    }

    auto map_all_bytes(
        const gl::Map_buffer_access_mask access_mask
    ) noexcept -> gsl::span<std::byte>;

    auto map_bytes(
        const std::size_t                byte_offset,
        const std::size_t                byte_count,
        const gl::Map_buffer_access_mask access_mask
    ) noexcept -> gsl::span<std::byte>;

    friend class Vertex_input_state;
    friend class Texture;

private:
    Gl_buffer               m_handle;
    std::string             m_debug_label;
    gl::Buffer_target       m_target             {gl::Buffer_target::array_buffer};
    std::size_t             m_capacity_byte_count{0};
    std::size_t             m_next_free_byte     {0};
    gl::Buffer_storage_mask m_storage_mask       {0};
    std::mutex              m_allocate_mutex;

    // Last MapBuffer
    gsl::span<std::byte>        m_map;
    std::size_t                 m_map_byte_offset       {0};
    gl::Map_buffer_access_mask  m_map_buffer_access_mask{0};
};

class Buffer_hash
{
public:
    [[nodiscard]]
    auto operator()(const Buffer& buffer) const noexcept -> std::size_t
    {
        return static_cast<std::size_t>(buffer.gl_name());
    }
};

[[nodiscard]] auto operator==(
    const Buffer& lhs,
    const Buffer& rhs
) noexcept -> bool;

[[nodiscard]] auto operator!=(
    const Buffer& lhs,
    const Buffer& rhs
) noexcept -> bool;

} // namespace erhe::graphics
