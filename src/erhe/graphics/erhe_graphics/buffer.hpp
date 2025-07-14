#pragma once

#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/gl_objects.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_dataformat/dataformat.hpp"

#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graphics {

class Device;

enum class Buffer_usage : unsigned int {
    none     = 0x00,
    vertex   = 0x01,
    index    = 0x02,
    uniform  = 0x04,
    storage  = 0x08,
    indirect = 0x10,
    texture  = 0x20,
    pixel    = 0x40, // pixel pack / unpack based on direction
    transfer = 0x80
};

enum class Buffer_direction : unsigned int {
    cpu_to_gpu = 0,
    gpu_only   = 1,
    gpu_to_cpu = 2
};

enum class Buffer_cache_mode : unsigned int {
    write_combined = 1,
    default_       = 2
};

enum class Buffer_mapping : unsigned int {
    not_mappable = 0,
    persistent   = 1,
    transient    = 2
};

enum class Buffer_coherency : unsigned int {
    off = 0,  // Need explicit flushes
    on  = 1
};

enum class Buffer_map_flags : unsigned int {
    none              = 0x00,
    invalidate_range  = 0x01,
    invalidate_buffer = 0x02,
    explicit_flush    = 0x04
};

[[nodiscard]] auto to_string(Buffer_usage usage) -> std::string;
[[nodiscard]] auto c_str(Buffer_direction direction) -> const char*;
[[nodiscard]] auto c_str(Buffer_cache_mode cache_mode) -> const char*;
[[nodiscard]] auto c_str(Buffer_mapping mapping) -> const char*;
[[nodiscard]] auto c_str(Buffer_coherency coherency) -> const char*;
[[nodiscard]] auto to_string(Buffer_map_flags flags) -> std::string;

[[nodiscard]] auto get_buffer_usage(Buffer_target target) -> Buffer_usage;

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
