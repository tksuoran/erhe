#include "mesh_memory.hpp"

#include "generated/rendering_test_mesh_memory.hpp"
#include "generated/rendering_test_mesh_memory_serialization.hpp"
#include "erhe_codegen/config_io.hpp"
#include "erhe_scene_renderer/program_interface.hpp"

namespace rendering_test {

static Rendering_test_mesh_memory& get_mesh_memory_config()
{
    static Rendering_test_mesh_memory config = erhe::codegen::load_config<Rendering_test_mesh_memory>("rendering_test_mesh_memory.json");
    return config;
}

auto Mesh_memory::get_vertex_buffer_size(std::size_t) const -> std::size_t
{
    const Rendering_test_mesh_memory& config = get_mesh_memory_config();
    return static_cast<std::size_t>(config.vertex_buffer_size) * 1024 * 1024;
}

auto Mesh_memory::get_index_buffer_size() const -> std::size_t
{
    const Rendering_test_mesh_memory& config = get_mesh_memory_config();
    return static_cast<std::size_t>(config.index_buffer_size) * 1024 * 1024;
}

Mesh_memory::Mesh_memory(erhe::graphics::Device& graphics_device)
    : graphics_device{graphics_device}
    , vertex_format{
        {
            0,
            {
                { erhe::dataformat::Format::format_32_vec3_float, erhe::dataformat::Vertex_attribute_usage::position}
            }
        },
        {
            1,
            {
                { erhe::dataformat::Format::format_32_vec3_float, erhe::dataformat::Vertex_attribute_usage::normal      },
                { erhe::dataformat::Format::format_32_vec2_float, erhe::dataformat::Vertex_attribute_usage::tex_coord, 0}
            }
        }
    }
    , position_vertex_buffer{
        graphics_device,
        erhe::graphics::Buffer_create_info{
            .capacity_byte_count                    = get_vertex_buffer_size(0),
            .memory_allocation_create_flag_bit_mask = 0,
            .usage                                  = erhe::graphics::Buffer_usage::vertex,
            .required_memory_property_bit_mask      = erhe::graphics::Memory_property_flag_bit_mask::device_local,
            .preferred_memory_property_bit_mask     = erhe::graphics::Memory_property_flag_bit_mask::none,
            .debug_label                            = "Mesh_memory position vertex buffer"
        }
    }
    , non_position_vertex_buffer{
        graphics_device,
        erhe::graphics::Buffer_create_info{
            .capacity_byte_count                    = get_vertex_buffer_size(1),
            .memory_allocation_create_flag_bit_mask = 0,
            .usage                                  = erhe::graphics::Buffer_usage::vertex,
            .required_memory_property_bit_mask      = erhe::graphics::Memory_property_flag_bit_mask::device_local,
            .preferred_memory_property_bit_mask     = erhe::graphics::Memory_property_flag_bit_mask::none,
            .debug_label                            = "Mesh_memory non-position vertex buffer"
        }
    }
    , index_buffer{
        graphics_device,
        erhe::graphics::Buffer_create_info{
            .capacity_byte_count                    = get_index_buffer_size(),
            .memory_allocation_create_flag_bit_mask = 0,
            .usage                                  = erhe::graphics::Buffer_usage::index,
            .required_memory_property_bit_mask      = erhe::graphics::Memory_property_flag_bit_mask::device_local,
            .preferred_memory_property_bit_mask     = erhe::graphics::Memory_property_flag_bit_mask::none,
            .debug_label                            = "Mesh_memory index buffer"
        }
    }
    , edge_line_vertex_buffer{
        graphics_device,
        erhe::graphics::Buffer_create_info{
            .capacity_byte_count                    = get_vertex_buffer_size(0) / 4,
            .memory_allocation_create_flag_bit_mask = 0,
            .usage                                  = erhe::graphics::Buffer_usage::vertex | erhe::graphics::Buffer_usage::storage,
            .required_memory_property_bit_mask      = erhe::graphics::Memory_property_flag_bit_mask::device_local,
            .preferred_memory_property_bit_mask     = erhe::graphics::Memory_property_flag_bit_mask::none,
            .debug_label                            = "Mesh_memory edge line vertex buffer"
        }
    }
    , buffer_transfer_queue{graphics_device}
    , graphics_buffer_sink{
        buffer_transfer_queue,
        { &position_vertex_buffer, &non_position_vertex_buffer },
        &index_buffer,
        &edge_line_vertex_buffer
    }
    , buffer_info{
        .index_type    = erhe::dataformat::Format::format_32_scalar_uint,
        .vertex_format = vertex_format,
        .buffer_sink   = graphics_buffer_sink
    }
    , vertex_input{graphics_device, erhe::graphics::Vertex_input_state_data::make(vertex_format)}
{
}

} // namespace rendering_test
