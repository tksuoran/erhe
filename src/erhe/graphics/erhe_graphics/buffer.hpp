#pragma once

#include "erhe_graphics/enums.hpp"
#include "erhe_utility/debug_label.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>

namespace erhe::graphics {

class Device;

class Buffer_create_info
{
public:
    std::size_t                capacity_byte_count                   {0};
    uint64_t                   memory_allocation_create_flag_bit_mask{0};
    Buffer_usage               usage                                 {0};
    uint64_t                   required_memory_property_bit_mask     {0};
    uint64_t                   preferred_memory_property_bit_mask    {0};
    const void*                init_data                             {nullptr};
    erhe::utility::Debug_label debug_label                           {};
};

class Buffer_impl;

// Process-wide buffer memory accounting (Buffer::get_memory_statistics()):
// the capacity of every live Buffer, split by the usage it was created for.
// A buffer counts into the first matching category in declaration order.
class Buffer_memory_statistics
{
public:
    std::size_t buffer_count                           {0};
    std::size_t byte_count                             {0};
    std::size_t acceleration_structure_bytes           {0}; // acceleration_structure_storage
    std::size_t acceleration_structure_build_input_bytes{0}; // acceleration_structure_build_input
    std::size_t vertex_index_bytes                     {0}; // vertex or index
    std::size_t other_bytes                            {0};
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

    [[nodiscard]] auto get_debug_label        () const noexcept -> erhe::utility::Debug_label;
    [[nodiscard]] auto get_map                () const -> std::span<std::byte>;
    [[nodiscard]] auto get_capacity_byte_count() const noexcept -> std::size_t;

    // GPU device address for GL_EXT_buffer_reference access from shaders.
    // Non-zero only on the Vulkan backend for buffers created with
    // Buffer_usage::shader_device_address; other backends return 0.
    [[nodiscard]] auto get_device_address     () const noexcept -> uint64_t;

    void unmap                () noexcept;
    void invalidate           (std::size_t byte_offset, std::size_t byte_count) noexcept;
    void flush_bytes          (std::size_t byte_offset, std::size_t byte_count) noexcept;
    void flush_and_unmap_bytes(std::size_t byte_count) noexcept;
    void upload_sub_data      (std::size_t byte_offset, std::size_t byte_count, const void* data) noexcept;
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

    [[nodiscard]] static auto get_memory_statistics() -> Buffer_memory_statistics;

    friend class Vertex_input_state;
    friend class Texture;

private:
    std::unique_ptr<Buffer_impl> m_impl;
    // What this buffer counts into get_memory_statistics(); moves with
    // m_impl.
    std::size_t                  m_counted_byte_count{0};
    Buffer_usage                 m_counted_usage     {Buffer_usage::none};
};

} // namespace erhe::graphics
