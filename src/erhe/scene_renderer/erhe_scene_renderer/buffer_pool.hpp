#pragma once

#include "erhe_buffer/free_list_allocator.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/debug_label.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace erhe::graphics {
    class Buffer;
    class Device;
}

namespace erhe::scene_renderer {

class Pool_block
{
public:
    uint64_t                                buffer_id;
    std::unique_ptr<erhe::graphics::Buffer> buffer;
    erhe::buffer::Free_list_allocator       allocator;
};

class Buffer_pool_block_create_info
{
public:
    erhe::graphics::Buffer_usage usage                             {0};
    uint64_t                     required_memory_property_bit_mask {0};
    uint64_t                     preferred_memory_property_bit_mask{0};
    std::size_t                  block_size_bytes                  {0};
    std::size_t                  max_blocks                        {0};
    std::string                  debug_label_prefix                {};
};

class Buffer_pool
{
public:
    Buffer_pool(
        erhe::graphics::Device&         graphics_device,
        uint64_t                        pool_id,
        erhe::dataformat::Vertex_stream vertex_stream,
        Buffer_pool_block_create_info   block_create_info
    );

    Buffer_pool(
        erhe::graphics::Device&       graphics_device,
        uint64_t                      pool_id,
        erhe::dataformat::Format      index_format,
        Buffer_pool_block_create_info block_create_info
    );

    Buffer_pool(const Buffer_pool&)            = delete;
    Buffer_pool& operator=(const Buffer_pool&) = delete;
    Buffer_pool(Buffer_pool&&) noexcept;
    Buffer_pool& operator=(Buffer_pool&&)      = delete;

    ~Buffer_pool();

    [[nodiscard]] auto is_compatible    (const erhe::dataformat::Vertex_stream& vertex_stream) const -> bool;
    [[nodiscard]] auto is_compatible    (erhe::dataformat::Format index_format) const -> bool;
    [[nodiscard]] auto allocate         (std::size_t element_count) -> erhe::primitive::Buffer_sink_allocation;
    [[nodiscard]] auto get_buffer       (uint64_t buffer_id) const -> erhe::graphics::Buffer*;
    [[nodiscard]] auto get_vertex_stream() const -> const erhe::dataformat::Vertex_stream&;
    [[nodiscard]] auto get_index_format () const -> erhe::dataformat::Format;

private:
    [[nodiscard]] auto allocate_internal(std::size_t allocation_byte_count, std::size_t allocation_alignment) -> std::optional<std::pair<Pool_block*, std::size_t>>;
    [[nodiscard]] auto create_new_block (std::size_t min_capacity_bytes) -> bool;
    [[nodiscard]] auto describe() const -> std::string;

    erhe::graphics::Device&                  m_graphics_device;
    erhe::dataformat::Vertex_stream          m_vertex_stream;
    erhe::dataformat::Format                 m_index_format{erhe::dataformat::Format::format_undefined};
    Buffer_pool_block_create_info            m_block_create_info;
    std::vector<std::unique_ptr<Pool_block>> m_blocks;
    uint64_t                                 m_pool_id;
    uint64_t                                 m_next_buffer_id{0};
};

} // namespace erhe::scene_renderer
