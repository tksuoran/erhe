#include "erhe_scene_renderer/mesh_memory.hpp"

#include "erhe_scene_renderer/generated/mesh_memory_config.hpp"
#include "erhe_scene_renderer/program_interface.hpp"

namespace erhe::scene_renderer {

namespace {

constexpr std::size_t kilo = 1024;
constexpr std::size_t mega = 1024 * kilo;

[[nodiscard]] auto make_index_pool_block_info(
    const Mesh_memory_config& config
) -> erhe::graphics_buffer_sink::Buffer_pool_block_create_info
{
    return erhe::graphics_buffer_sink::Buffer_pool_block_create_info{
        .usage                              = erhe::graphics::Buffer_usage::index,
        .required_memory_property_bit_mask  = erhe::graphics::Memory_property_flag_bit_mask::device_local,
        .preferred_memory_property_bit_mask = erhe::graphics::Memory_property_flag_bit_mask::none,
        .block_size_bytes                   = static_cast<std::size_t>(config.index_pool_block_size_mb) * mega,
        .max_blocks                         = static_cast<std::size_t>(config.max_buffers_per_pool),
        .debug_label_prefix                 = "Mesh_memory index pool"
    };
}

[[nodiscard]] auto make_edge_line_vertex_pool_block_info(
    const Mesh_memory_config& config
) -> erhe::graphics_buffer_sink::Buffer_pool_block_create_info
{
    return erhe::graphics_buffer_sink::Buffer_pool_block_create_info{
        .usage                              = erhe::graphics::Buffer_usage::vertex | erhe::graphics::Buffer_usage::storage,
        .required_memory_property_bit_mask  = erhe::graphics::Memory_property_flag_bit_mask::device_local,
        .preferred_memory_property_bit_mask = erhe::graphics::Memory_property_flag_bit_mask::none,
        .block_size_bytes                   = static_cast<std::size_t>(config.edge_line_vertex_pool_block_size_mb) * mega,
        .max_blocks                         = static_cast<std::size_t>(config.max_buffers_per_pool),
        .debug_label_prefix                 = "Mesh_memory edge line vertex pool"
    };
}

} // anonymous namespace

[[nodiscard]] auto Mesh_memory::get_vertex_buffer(const std::size_t stream_index) -> erhe::graphics::Buffer*
{
    if (stream_index >= default_format_pools.vertex_pools.size()) {
        return nullptr;
    }
    return default_format_pools.vertex_pools[stream_index]->get_first_buffer();
}

[[nodiscard]] auto Mesh_memory::get_default_index_buffer() -> erhe::graphics::Buffer*
{
    return index_pool.get_first_buffer();
}

auto Mesh_memory::get_or_create_format_pools(const erhe::dataformat::Vertex_format& format) -> Format_pools&
{
    const uint32_t key = erhe::dataformat::compute_vertex_format_key(format);
    if (key == m_default_format_key) {
        return default_format_pools;
    }
    const auto it = m_extra_format_pools.find(key);
    if (it != m_extra_format_pools.end()) {
        return *it->second;
    }
    auto pools = std::make_unique<Format_pools>(
        graphics_device,
        buffer_transfer_queue,
        m_config,
        format,
        index_pool,
        edge_line_vertex_pool,
        std::string{"Mesh_memory format pool"}
    );
    Format_pools& ref = *pools;
    m_extra_format_pools.emplace(key, std::move(pools));
    return ref;
}

Mesh_memory::Mesh_memory(
    const Mesh_memory_config&        mesh_memory_config,
    erhe::graphics::Device&          graphics_device,
    erhe::dataformat::Vertex_format& vertex_format
)
    : graphics_device       {graphics_device}
    , buffer_transfer_queue {graphics_device}
    , vertex_format         {vertex_format}
    , index_pool{
        graphics_device,
        buffer_transfer_queue,
        0,
        make_index_pool_block_info(mesh_memory_config)
    }
    , edge_line_vertex_pool{
        graphics_device,
        buffer_transfer_queue,
        0,
        make_edge_line_vertex_pool_block_info(mesh_memory_config)
    }
    , default_format_pools{
        graphics_device,
        buffer_transfer_queue,
        mesh_memory_config,
        vertex_format,
        index_pool,
        edge_line_vertex_pool,
        std::string{"Mesh_memory default"}
    }
    , buffer_info          {default_format_pools.buffer_info}
    , vertex_input         {default_format_pools.vertex_input}
    , graphics_buffer_sink {default_format_pools.graphics_buffer_sink}
    , vertex_pool_position    {*default_format_pools.vertex_pools.at(0)}
    , vertex_pool_non_position{*default_format_pools.vertex_pools.at(1)}
    , vertex_pool_custom      {*default_format_pools.vertex_pools.at(2)}
    , m_config{mesh_memory_config}
{
    m_default_format_key = erhe::dataformat::compute_vertex_format_key(vertex_format);
}

Mesh_memory::~Mesh_memory() noexcept = default;

}
