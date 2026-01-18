#include "renderers/mesh_memory.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_scene_renderer/program_interface.hpp"

namespace editor {

auto Mesh_memory::get_vertex_buffer_size(std::size_t stream_index) const -> std::size_t
{
    int vertex_buffer_size{stream_index == 0 ? 8 : 32}; // in megabytes
    const auto& ini = erhe::configuration::get_ini_file_section(erhe::c_erhe_config_file_path, "mesh_memory");
    ini.get("vertex_buffer_size", vertex_buffer_size);
    std::size_t kilo = 1024;
    std::size_t mega = 1024 * kilo;
    return static_cast<std::size_t>(vertex_buffer_size) * mega;
}

auto Mesh_memory::get_index_buffer_size() const -> std::size_t
{
    int index_buffer_size{4}; // in megabytes
    const auto& ini = erhe::configuration::get_ini_file_section(erhe::c_erhe_config_file_path, "mesh_memory");
    ini.get("index_buffer_size", index_buffer_size);
    std::size_t kilo = 1024;
    std::size_t mega = 1024 * kilo;
    return static_cast<std::size_t>(index_buffer_size) * mega;
}

[[nodiscard]] auto Mesh_memory::get_vertex_buffer(std::size_t stream_index) -> erhe::graphics::Buffer*
{
    switch (stream_index) {
        case s_vertex_binding_position:     return &position_vertex_buffer;
        case s_vertex_binding_non_position: return &non_position_vertex_buffer;
        default: return nullptr;
    }
}

Mesh_memory::Mesh_memory(erhe::graphics::Device& graphics_device, erhe::dataformat::Vertex_format& vertex_format)
    : graphics_device       {graphics_device}
    , buffer_transfer_queue {graphics_device}
    , vertex_format         {vertex_format}
    , position_vertex_buffer{
        graphics_device,
        erhe::graphics::Buffer_create_info{
            .capacity_byte_count                    = get_vertex_buffer_size(s_vertex_binding_position),
            .memory_allocation_create_flag_bit_mask = 0,
            .usage                                  = erhe::graphics::Buffer_usage::vertex,
            .required_memory_property_bit_mask      = erhe::graphics::Memory_property_flag_bit_mask::device_local, // GPU only
            .preferred_memory_property_bit_mask     = erhe::graphics::Memory_property_flag_bit_mask::none, // uploads via staging buffer
            .debug_label                            = "Mesh_memory position vertex buffer"
        }
    }
    , non_position_vertex_buffer{
        graphics_device,
        erhe::graphics::Buffer_create_info{
            .capacity_byte_count                    = get_vertex_buffer_size(s_vertex_binding_non_position),
            .memory_allocation_create_flag_bit_mask = 0,
            .usage                                  = erhe::graphics::Buffer_usage::vertex,
            .required_memory_property_bit_mask      = erhe::graphics::Memory_property_flag_bit_mask::device_local, // GPU only
            .preferred_memory_property_bit_mask     = erhe::graphics::Memory_property_flag_bit_mask::none, // uploads via staging buffer
            .debug_label                            = "Mesh_memory non-position vertex buffer"
        }
    }
    , index_buffer{
        graphics_device,
        erhe::graphics::Buffer_create_info{
            .capacity_byte_count                    = get_index_buffer_size(),
            .memory_allocation_create_flag_bit_mask = 0,
            .usage                                  = erhe::graphics::Buffer_usage::index,
            .required_memory_property_bit_mask      = erhe::graphics::Memory_property_flag_bit_mask::device_local, // GPU only
            .preferred_memory_property_bit_mask     = erhe::graphics::Memory_property_flag_bit_mask::none, // uploads via staging buffer
            .debug_label                            = "Mesh_memory index buffer"
        }
    }
    , graphics_buffer_sink{buffer_transfer_queue, {&position_vertex_buffer, &non_position_vertex_buffer}, &index_buffer}
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
