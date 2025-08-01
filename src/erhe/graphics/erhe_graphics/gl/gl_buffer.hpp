#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/gl/gl_objects.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_gl/wrapper_enums.hpp"

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
    Buffer_impl         (Buffer_impl&& other) noexcept;
    auto operator=      (Buffer_impl&& other) noexcept -> Buffer_impl&;

    [[nodiscard]] auto get_capacity_byte_count () const noexcept -> std::size_t;
    [[nodiscard]] auto allocate_bytes          (std::size_t byte_count, std::size_t alignment = 64) noexcept -> std::optional<std::size_t>;
    [[nodiscard]] auto get_debug_label         () const noexcept -> const std::string&;
    [[nodiscard]] auto get_used_byte_count     () const -> std::size_t;
    [[nodiscard]] auto get_available_byte_count(std::size_t alignment = 64) const noexcept -> std::size_t;

    [[nodiscard]] auto get_map                 () const -> std::span<std::byte>;
    [[nodiscard]] auto gl_name                 () const noexcept -> unsigned int;
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

    friend class Vertex_input_state;
    friend class Texture;

private:
    void allocate_storage(const void* init_data = nullptr);
    void capability_check(gl::Buffer_storage_mask storage_mask);
    void capability_check(gl::Map_buffer_access_mask access_mask);
    [[nodiscard]] auto get_gl_storage_mask() -> gl::Buffer_storage_mask;
    [[nodiscard]] auto get_gl_access_mask (Buffer_map_flags flags) const -> gl::Map_buffer_access_mask;

    ERHE_PROFILE_MUTEX(std::mutex, m_allocate_mutex);
    Device&                        m_device;
    Gl_buffer                      m_handle;
    std::size_t                    m_capacity_byte_count{0};
    std::size_t                    m_next_free_byte     {0};
    Buffer_usage                   m_usage              {0};
    Buffer_direction               m_direction          {0};
    Buffer_cache_mode              m_cache_mode         {0};
    Buffer_mapping                 m_mapping            {0};
    Buffer_coherency               m_coherency          {0};
    std::string                    m_debug_label        {};

    static constexpr const char* s_pool_name = "glBuffer";

    // Last MapBuffer
    std::span<std::byte> m_map;
    std::size_t          m_map_byte_offset{0};
    Buffer_map_flags     m_map_flags      {0};
    bool                 m_allocated      {false};
};

class Buffer_impl_hash
{
public:
    [[nodiscard]] auto operator()(const Buffer_impl& buffer) const noexcept -> std::size_t
    {
        return static_cast<std::size_t>(buffer.gl_name());
    }
};

[[nodiscard]] auto operator==(const Buffer_impl& lhs, const Buffer_impl& rhs) noexcept -> bool;
[[nodiscard]] auto operator!=(const Buffer_impl& lhs, const Buffer_impl& rhs) noexcept -> bool;


} // namespace erhe::graphics
