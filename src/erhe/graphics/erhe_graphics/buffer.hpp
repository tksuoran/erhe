#pragma once

#include "erhe_graphics/gl_objects.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_dataformat/dataformat.hpp"

#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics {

class Instance;

class Buffer final
{
public:
    explicit Buffer(Instance& instance);

    ~Buffer() noexcept;

    Buffer(
        Instance&               instance,
        gl::Buffer_target       target,
        std::size_t             capacity_bytes_count,
        gl::Buffer_storage_mask storage_mask
    ) noexcept;

    Buffer(
        Instance&               instance,
        std::size_t             capacity_bytes_count,
        gl::Buffer_storage_mask storage_mask
    ) noexcept;

    Buffer(
        Instance&                  instance,
        std::size_t                capacity_byte_count,
        gl::Buffer_storage_mask    storage_mask,
        gl::Map_buffer_access_mask map_buffer_access_mask
    ) noexcept;

    Buffer(
        Instance&                  instance,
        gl::Buffer_target          target,
        std::size_t                capacity_byte_count,
        gl::Buffer_storage_mask    storage_mask,
        gl::Map_buffer_access_mask map_buffer_access_mask
    ) noexcept;

    Buffer(
        Instance&                  instance,
        gl::Buffer_target          target,
        std::size_t                capacity_byte_count,
        gl::Buffer_storage_mask    storage_mask,
        gl::Map_buffer_access_mask map_buffer_access_mask,
        std::string_view           debug_label
    ) noexcept;

    Buffer        (const Buffer&) = delete;
    void operator=(const Buffer&) = delete;
    Buffer        (Buffer&& other) noexcept;
    auto operator=(Buffer&& other) noexcept -> Buffer&;

    [[nodiscard]] auto map                () const          -> std::span<std::byte>;
    [[nodiscard]] auto debug_label        () const noexcept -> const std::string&;
    [[nodiscard]] auto capacity_byte_count() const noexcept -> std::size_t;
    [[nodiscard]] auto allocate_bytes     (std::size_t byte_count, std::size_t alignment = 64) noexcept -> std::size_t;
    [[nodiscard]] auto free_capacity_bytes() const noexcept -> std::size_t;
    [[nodiscard]] auto target             () const noexcept -> gl::Buffer_target;
    [[nodiscard]] auto gl_name            () const noexcept -> unsigned int;
    void unmap                () noexcept;
    void flush_bytes          (std::size_t byte_offset, std::size_t byte_count) noexcept;
    void flush_and_unmap_bytes(std::size_t byte_count) noexcept;
    void set_debug_label      (const std::string_view label) noexcept;
    void dump                 () const noexcept;

    auto begin_write(std::size_t byte_offset, std::size_t byte_count) noexcept -> std::span<std::byte>;
    void end_write  (std::size_t byte_offset, std::size_t byte_count) noexcept;

    template <typename T>
    [[nodiscard]]
    auto map_elements(
        const std::size_t                element_offset,
        const std::size_t                element_count,
        const gl::Map_buffer_access_mask access_mask
    ) noexcept -> std::span<T>
    {
        const std::size_t byte_offset = element_offset * sizeof(T);
        const std::size_t byte_count  = element_count * sizeof(T);
        auto raw_map = map_bytes(byte_offset, byte_count, access_mask);
        return std::span(
            reinterpret_cast<T*>(raw_map.data()),
            raw_map.size_bytes() / sizeof(T)
        );
    }

    auto map_all_bytes(const gl::Map_buffer_access_mask access_mask) noexcept -> std::span<std::byte>;

    auto map_bytes(
        const std::size_t                byte_offset,
        const std::size_t                byte_count,
        const gl::Map_buffer_access_mask access_mask
    ) noexcept -> std::span<std::byte>;

    friend class Vertex_input_state;
    friend class Texture;

private:
    void allocate_storage();
    void capability_check(gl::Buffer_storage_mask storage_mask);
    void capability_check(gl::Map_buffer_access_mask access_mask);

    Instance&                      m_instance;
    Gl_buffer                      m_handle;
    std::string                    m_debug_label;
    gl::Buffer_target              m_target             {gl::Buffer_target::array_buffer};
    std::size_t                    m_capacity_byte_count{0};
    std::size_t                    m_next_free_byte     {0};
    gl::Buffer_storage_mask        m_storage_mask       {0};
    gl::Map_buffer_access_mask     m_access_mask        {0};
    ERHE_PROFILE_MUTEX(std::mutex, m_allocate_mutex);

    // Last MapBuffer
    std::span<std::byte>       m_map;
    std::size_t                m_map_byte_offset       {0};
    gl::Map_buffer_access_mask m_map_buffer_access_mask{0};
    //std::vector<uint8_t>       m_cpu_copy;
};

class Buffer_hash
{
public:
    [[nodiscard]] auto operator()(const Buffer& buffer) const noexcept -> std::size_t
    {
        return static_cast<std::size_t>(buffer.gl_name());
    }
};

[[nodiscard]] auto operator==(const Buffer& lhs, const Buffer& rhs) noexcept -> bool;
[[nodiscard]] auto operator!=(const Buffer& lhs, const Buffer& rhs) noexcept -> bool;
[[nodiscard]] auto to_gl_index_type(erhe::dataformat::Format format) -> gl::Draw_elements_type;

} // namespace erhe::graphics
