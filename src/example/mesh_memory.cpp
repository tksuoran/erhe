#include "mesh_memory.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_scene_renderer/program_interface.hpp"

namespace example {

static constexpr gl::Buffer_storage_mask storage_mask{gl::Buffer_storage_mask::map_write_bit};

auto Mesh_memory::get_vertex_buffer_size(std::size_t) const -> std::size_t
{
    int vertex_buffer_size{32}; // in megabytes
    const auto& ini = erhe::configuration::get_ini_file_section("erhe.ini", "mesh_memory");
    ini.get("vertex_buffer_size", vertex_buffer_size);
    return vertex_buffer_size * 1024 * 1024;
}

auto Mesh_memory::get_index_buffer_size() const -> std::size_t
{
    int index_buffer_size{8}; // in megabytes
    const auto& ini = erhe::configuration::get_ini_file_section("erhe.ini", "mesh_memory");
    ini.get("index_buffer_size",  index_buffer_size);
    return index_buffer_size * 1024 * 1024;
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
            .target              = gl::Buffer_target::array_buffer,
            .capacity_byte_count = get_vertex_buffer_size(0),
            .storage_mask        = storage_mask,
            .debug_label         = "Mesh_memory position vertex buffer"
        }
    }
    , non_position_vertex_buffer{
        graphics_device,
        erhe::graphics::Buffer_create_info{
            .target              = gl::Buffer_target::array_buffer,
            .capacity_byte_count = get_vertex_buffer_size(1),
            .storage_mask        = storage_mask,
            .debug_label         = "Mesh_memory non-position vertex buffer"
        }
    }
    , index_buffer{
        graphics_device,
        erhe::graphics::Buffer_create_info{
            .target              = gl::Buffer_target::element_array_buffer,
            .capacity_byte_count = get_index_buffer_size(),
            .storage_mask        = storage_mask,
            .debug_label         = "Mesh_memory index buffer"
        }
    }
    , graphics_buffer_sink{
        gl_buffer_transfer_queue,
        {&position_vertex_buffer,&non_position_vertex_buffer},
        index_buffer
    }
    , buffer_info{
        .index_type    = erhe::dataformat::Format::format_32_scalar_uint,
        .vertex_format = vertex_format,
        .buffer_sink   = graphics_buffer_sink
    }
    , vertex_input{graphics_device, erhe::graphics::Vertex_input_state_data::make(vertex_format)}
{
}

} // namespace example
