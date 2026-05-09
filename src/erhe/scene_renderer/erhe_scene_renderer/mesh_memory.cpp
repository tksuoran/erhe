#include "erhe_scene_renderer/mesh_memory.hpp"

#include "erhe_scene_renderer/generated/mesh_memory_config.hpp"
#include "erhe_scene_renderer/program_interface.hpp"

namespace erhe::scene_renderer {

auto Mesh_memory::get_vertex_buffer_size(const Mesh_memory_config& mesh_memory_config, const std::size_t stream_index) const -> std::size_t
{
    static_cast<void>(stream_index);
    const int vertex_buffer_size = mesh_memory_config.vertex_buffer_size;
    std::size_t kilo = 1024;
    std::size_t mega = 1024 * kilo;
    return static_cast<std::size_t>(vertex_buffer_size) * mega;
}

auto Mesh_memory::get_index_buffer_size(const Mesh_memory_config& mesh_memory_config) const -> std::size_t
{
    int index_buffer_size = mesh_memory_config.index_buffer_size;
    std::size_t kilo = 1024;
    std::size_t mega = 1024 * kilo;
    return static_cast<std::size_t>(index_buffer_size) * mega;
}

auto Mesh_memory::get_edge_line_vertex_buffer_size(const Mesh_memory_config& mesh_memory_config) const -> std::size_t
{
    // Use a fraction of the vertex buffer size - edge line data is much smaller
    // Each edge needs 2 vec4 vertices = 32 bytes
    int vertex_buffer_size = mesh_memory_config.vertex_buffer_size;
    std::size_t kilo = 1024;
    std::size_t mega = 1024 * kilo;
    return static_cast<std::size_t>(vertex_buffer_size) * mega / 4;
}

[[nodiscard]] auto Mesh_memory::get_vertex_buffer(const std::size_t stream_index) -> erhe::graphics::Buffer*
{
    switch (stream_index) {
        case s_vertex_binding_position:     return &vertex_buffer_position;
        case s_vertex_binding_non_position: return &vertex_buffer_non_position;
        case s_vertex_binding_custom:       return &vertex_buffer_custom;
        default: return nullptr;
    }
}

Mesh_memory::Mesh_memory(const Mesh_memory_config& mesh_memory_config, erhe::graphics::Device& graphics_device, erhe::dataformat::Vertex_format& vertex_format)
    : graphics_device       {graphics_device}
    , buffer_transfer_queue {graphics_device}
    , vertex_format         {vertex_format}
    , vertex_buffer_position{
        graphics_device,
        erhe::graphics::Buffer_create_info{
            .capacity_byte_count                    = get_vertex_buffer_size(mesh_memory_config, s_vertex_binding_position),
            .memory_allocation_create_flag_bit_mask = 0,
            .usage                                  = erhe::graphics::Buffer_usage::vertex,
            .required_memory_property_bit_mask      = erhe::graphics::Memory_property_flag_bit_mask::device_local, // GPU only
            .preferred_memory_property_bit_mask     = erhe::graphics::Memory_property_flag_bit_mask::none, // uploads via staging buffer
            .debug_label                            = "Mesh_memory position vertex buffer"
        }
    }
    , vertex_buffer_non_position{
        graphics_device,
        erhe::graphics::Buffer_create_info{
            .capacity_byte_count                    = get_vertex_buffer_size(mesh_memory_config, s_vertex_binding_non_position),
            .memory_allocation_create_flag_bit_mask = 0,
            .usage                                  = erhe::graphics::Buffer_usage::vertex,
            .required_memory_property_bit_mask      = erhe::graphics::Memory_property_flag_bit_mask::device_local, // GPU only
            .preferred_memory_property_bit_mask     = erhe::graphics::Memory_property_flag_bit_mask::none, // uploads via staging buffer
            .debug_label                            = "Mesh_memory non-position vertex buffer"
        }
    }
    , vertex_buffer_custom{
        graphics_device,
        erhe::graphics::Buffer_create_info{
            .capacity_byte_count                    = get_vertex_buffer_size(mesh_memory_config, s_vertex_binding_non_position),
            .memory_allocation_create_flag_bit_mask = 0,
            .usage                                  = erhe::graphics::Buffer_usage::vertex,
            .required_memory_property_bit_mask      = erhe::graphics::Memory_property_flag_bit_mask::device_local, // GPU only
            .preferred_memory_property_bit_mask     = erhe::graphics::Memory_property_flag_bit_mask::none, // uploads via staging buffer
            .debug_label                            = "Mesh_memory custom vertex buffer"
        }
    }
    , index_buffer{
        graphics_device,
        erhe::graphics::Buffer_create_info{
            .capacity_byte_count                    = get_index_buffer_size(mesh_memory_config),
            .memory_allocation_create_flag_bit_mask = 0,
            .usage                                  = erhe::graphics::Buffer_usage::index,
            .required_memory_property_bit_mask      = erhe::graphics::Memory_property_flag_bit_mask::device_local, // GPU only
            .preferred_memory_property_bit_mask     = erhe::graphics::Memory_property_flag_bit_mask::none, // uploads via staging buffer
            .debug_label                            = "Mesh_memory index buffer"
        }
    }
    , edge_line_vertex_buffer{
        graphics_device,
        erhe::graphics::Buffer_create_info{
            .capacity_byte_count                    = get_edge_line_vertex_buffer_size(mesh_memory_config),
            .memory_allocation_create_flag_bit_mask = 0,
            .usage                                  = erhe::graphics::Buffer_usage::vertex | erhe::graphics::Buffer_usage::storage,
            .required_memory_property_bit_mask      = erhe::graphics::Memory_property_flag_bit_mask::device_local,
            .preferred_memory_property_bit_mask     = erhe::graphics::Memory_property_flag_bit_mask::none,
            .debug_label                            = "Mesh_memory edge line vertex buffer"
        }
    }
    , vertex_pool_position    {buffer_transfer_queue, s_vertex_binding_position}
    , vertex_pool_non_position{buffer_transfer_queue, s_vertex_binding_non_position}
    , vertex_pool_custom      {buffer_transfer_queue, s_vertex_binding_custom}
    , index_pool              {buffer_transfer_queue, 0}
    , edge_line_vertex_pool   {buffer_transfer_queue, 0}
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
    // Register the eagerly-created buffers as the first (only) block of each
    // pool. Step 3 will defer block creation to first allocate().
    vertex_pool_position    .add_existing_block(&vertex_buffer_position,     vertex_buffer_position    .get_capacity_byte_count());
    vertex_pool_non_position.add_existing_block(&vertex_buffer_non_position, vertex_buffer_non_position.get_capacity_byte_count());
    vertex_pool_custom      .add_existing_block(&vertex_buffer_custom,       vertex_buffer_custom      .get_capacity_byte_count());
    index_pool              .add_existing_block(&index_buffer,               index_buffer              .get_capacity_byte_count());
    edge_line_vertex_pool   .add_existing_block(&edge_line_vertex_buffer,    edge_line_vertex_buffer   .get_capacity_byte_count());
}

Mesh_memory::~Mesh_memory() noexcept
{
}

}
