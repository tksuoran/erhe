#pragma once

#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/gl/gl_objects.hpp"
#include "erhe_gl/wrapper_enums.hpp"

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
    [[nodiscard]] auto get_debug_label         () const noexcept -> const std::string&;
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
        auto raw_map = map_bytes(byte_offset, byte_count);
        return std::span(
            reinterpret_cast<T*>(raw_map.data()),
            raw_map.size_bytes() / sizeof(T)
        );
    }

    friend class Vertex_input_state;
    friend class Texture;

private:
    void allocate_storage(const void* init_data = nullptr);
    void capability_check(gl::Buffer_storage_mask storage_mask);
    void capability_check(gl::Map_buffer_access_mask access_mask);
    [[nodiscard]] auto get_gl_storage_mask() const -> gl::Buffer_storage_mask;
    [[nodiscard]] auto get_gl_access_mask () const -> gl::Map_buffer_access_mask;

    static constexpr const char* s_pool_name = "glBuffer";

    Device&              m_device;
    Gl_buffer            m_handle;
    std::size_t          m_capacity_byte_count                   {0};
    Buffer_usage         m_usage                                 {0};
    uint64_t             m_memory_allocation_create_flag_bit_mask{0};
    uint64_t             m_required_memory_property_bit_mask     {0};
    uint64_t             m_preferred_memory_property_bit_mask    {0};
    std::string          m_debug_label                           {};

    // Last MapBuffer
    std::span<std::byte> m_map;
    std::size_t          m_map_byte_offset{0};
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
