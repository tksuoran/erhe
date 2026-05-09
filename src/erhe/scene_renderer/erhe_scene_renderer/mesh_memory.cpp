#include "erhe_scene_renderer/mesh_memory.hpp"

#include "erhe_scene_renderer/generated/mesh_memory_config.hpp"
#include "erhe_scene_renderer/program_interface.hpp"

namespace erhe::scene_renderer {

namespace {

constexpr std::size_t kilo = 1024;
constexpr std::size_t mega = 1024 * kilo;

[[nodiscard]] auto vertex_block_size_bytes(const Mesh_memory_config& config) -> std::size_t
{
    return static_cast<std::size_t>(config.vertex_pool_block_size_mb) * mega;
}

[[nodiscard]] auto index_block_size_bytes(const Mesh_memory_config& config) -> std::size_t
{
    return static_cast<std::size_t>(config.index_pool_block_size_mb) * mega;
}

[[nodiscard]] auto edge_line_vertex_buffer_size_bytes(const Mesh_memory_config& config) -> std::size_t
{
    return static_cast<std::size_t>(config.edge_line_vertex_pool_block_size_mb) * mega;
}

[[nodiscard]] auto make_vertex_pool_block_info(
    const Mesh_memory_config&  config,
    const std::string&         debug_label_prefix
) -> erhe::graphics_buffer_sink::Buffer_pool_block_create_info
{
    return erhe::graphics_buffer_sink::Buffer_pool_block_create_info{
        .usage                              = erhe::graphics::Buffer_usage::vertex,
        .required_memory_property_bit_mask  = erhe::graphics::Memory_property_flag_bit_mask::device_local, // GPU only
        .preferred_memory_property_bit_mask = erhe::graphics::Memory_property_flag_bit_mask::none,         // uploads via staging
        .block_size_bytes                   = vertex_block_size_bytes(config),
        .max_blocks                         = static_cast<std::size_t>(config.max_buffers_per_pool),
        .debug_label_prefix                 = debug_label_prefix
    };
}

[[nodiscard]] auto make_index_pool_block_info(
    const Mesh_memory_config& config
) -> erhe::graphics_buffer_sink::Buffer_pool_block_create_info
{
    return erhe::graphics_buffer_sink::Buffer_pool_block_create_info{
        .usage                              = erhe::graphics::Buffer_usage::index,
        .required_memory_property_bit_mask  = erhe::graphics::Memory_property_flag_bit_mask::device_local,
        .preferred_memory_property_bit_mask = erhe::graphics::Memory_property_flag_bit_mask::none,
        .block_size_bytes                   = index_block_size_bytes(config),
        .max_blocks                         = static_cast<std::size_t>(config.max_buffers_per_pool),
        .debug_label_prefix                 = "Mesh_memory index pool"
    };
}

} // anonymous namespace

[[nodiscard]] auto Mesh_memory::get_vertex_buffer(const std::size_t stream_index) -> erhe::graphics::Buffer*
{
    switch (stream_index) {
        case s_vertex_binding_position:     return vertex_pool_position    .get_first_buffer();
        case s_vertex_binding_non_position: return vertex_pool_non_position.get_first_buffer();
        case s_vertex_binding_custom:       return vertex_pool_custom      .get_first_buffer();
        default: return nullptr;
    }
}

[[nodiscard]] auto Mesh_memory::get_default_index_buffer() -> erhe::graphics::Buffer*
{
    return index_pool.get_first_buffer();
}

Mesh_memory::Mesh_memory(const Mesh_memory_config& mesh_memory_config, erhe::graphics::Device& graphics_device, erhe::dataformat::Vertex_format& vertex_format)
    : graphics_device       {graphics_device}
    , buffer_transfer_queue {graphics_device}
    , vertex_format         {vertex_format}
    , edge_line_vertex_buffer{
        graphics_device,
        erhe::graphics::Buffer_create_info{
            .capacity_byte_count                    = edge_line_vertex_buffer_size_bytes(mesh_memory_config),
            .memory_allocation_create_flag_bit_mask = 0,
            .usage                                  = erhe::graphics::Buffer_usage::vertex | erhe::graphics::Buffer_usage::storage,
            .required_memory_property_bit_mask      = erhe::graphics::Memory_property_flag_bit_mask::device_local,
            .preferred_memory_property_bit_mask     = erhe::graphics::Memory_property_flag_bit_mask::none,
            .debug_label                            = "Mesh_memory edge line vertex buffer"
        }
    }
    , vertex_pool_position{
        graphics_device,
        buffer_transfer_queue,
        s_vertex_binding_position,
        make_vertex_pool_block_info(mesh_memory_config, "Mesh_memory vertex pool position")
    }
    , vertex_pool_non_position{
        graphics_device,
        buffer_transfer_queue,
        s_vertex_binding_non_position,
        make_vertex_pool_block_info(mesh_memory_config, "Mesh_memory vertex pool non-position")
    }
    , vertex_pool_custom{
        graphics_device,
        buffer_transfer_queue,
        s_vertex_binding_custom,
        make_vertex_pool_block_info(mesh_memory_config, "Mesh_memory vertex pool custom")
    }
    , index_pool{
        graphics_device,
        buffer_transfer_queue,
        0,
        make_index_pool_block_info(mesh_memory_config)
    }
    , edge_line_vertex_pool{
        buffer_transfer_queue,
        0
    }
    , graphics_buffer_sink{
        {
            &vertex_pool_position,
            &vertex_pool_non_position,
            &vertex_pool_custom
        },
        &index_pool,
        &edge_line_vertex_pool
    }
    , buffer_info{
        .index_type    = erhe::dataformat::Format::format_32_scalar_uint,
        .vertex_format = vertex_format,
        .buffer_sink   = graphics_buffer_sink
    }
    , vertex_input{
        graphics_device,
        erhe::graphics::Vertex_input_state_data::make(vertex_format)
    }
{
    // The edge-line buffer is still eagerly created above; register it as the
    // single block of the edge-line pool so allocate() can hand out ranges.
    edge_line_vertex_pool.add_existing_block(&edge_line_vertex_buffer, edge_line_vertex_buffer.get_capacity_byte_count());
}

Mesh_memory::~Mesh_memory() noexcept = default;

}
