#include "renderers/mesh_memory.hpp"

#include "config/generated/mesh_memory_config.hpp"
#include "erhe_scene_renderer/program_interface.hpp"

namespace editor {

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
    , graphics_buffer_sink{
        buffer_transfer_queue,
        {
            &vertex_buffer_position,
            &vertex_buffer_non_position,
            &vertex_buffer_custom
        },
        &index_buffer,
        &edge_line_vertex_buffer
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
}

Mesh_memory::~Mesh_memory() noexcept
{
}

}
