#include "renderers/mesh_memory.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_scene_renderer/program_interface.hpp"

namespace editor {

static constexpr gl::Buffer_storage_mask storage_mask{gl::Buffer_storage_mask::map_write_bit};

auto Mesh_memory::get_vertex_buffer_size(std::size_t stream_index) const -> std::size_t
{
    int vertex_buffer_size{stream_index == 0 ? 8 : 32}; // in megabytes
    const auto& ini = erhe::configuration::get_ini_file_section("erhe.ini", "mesh_memory");
    ini.get("vertex_buffer_size", vertex_buffer_size);
    std::size_t kilo = 1024;
    std::size_t mega = 1024 * kilo;
    return static_cast<std::size_t>(vertex_buffer_size) * mega;
}

auto Mesh_memory::get_index_buffer_size() const -> std::size_t
{
    int index_buffer_size{4}; // in megabytes
    const auto& ini = erhe::configuration::get_ini_file_section("erhe.ini", "mesh_memory");
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

Mesh_memory::Mesh_memory(erhe::graphics::Instance& graphics_instance, erhe::dataformat::Vertex_format& vertex_format)
    : graphics_instance         {graphics_instance}
    , vertex_format             {vertex_format}
    , position_vertex_buffer{
        graphics_instance,
        erhe::graphics::Buffer_create_info{
            .target              = gl::Buffer_target::array_buffer,
            .capacity_byte_count = get_vertex_buffer_size(s_vertex_binding_position),
            .storage_mask        = storage_mask,
            .debug_label         = "Mesh_memory position vertex buffer"
        }
    }
    , non_position_vertex_buffer{
        graphics_instance,
        erhe::graphics::Buffer_create_info{
            .target              = gl::Buffer_target::array_buffer,
            .capacity_byte_count = get_vertex_buffer_size(s_vertex_binding_non_position),
            .storage_mask        = storage_mask,
            .debug_label         = "Mesh_memory non-position vertex buffer"
        }
    }
    , index_buffer{
        graphics_instance,
        erhe::graphics::Buffer_create_info{
            .target              = gl::Buffer_target::element_array_buffer,
            .capacity_byte_count = get_index_buffer_size(),
            .storage_mask        = storage_mask,
            .debug_label         = "Mesh_memory index buffer"
        }
    }
    , graphics_buffer_sink{gl_buffer_transfer_queue, {&position_vertex_buffer, &non_position_vertex_buffer}, index_buffer}
    , buffer_info{
        .index_type    = erhe::dataformat::Format::format_32_scalar_uint,
        .vertex_format = vertex_format,
        .buffer_sink   = graphics_buffer_sink
    }
    //, build_info{
    //    .primitive_types{
    //        .fill_triangles  = true,
    //        .edge_lines      = true,
    //        .corner_points   = true,
    //        .centroid_points = true
    //    },
    //    .buffer_info{
    //        .index_type    = gl::Draw_elements_type::unsigned_int,
    //        .vertex_format = vertex_format,
    //        .buffer_sink   = gl_buffer_sink
    //    },
    //    .constant_color{0.5f, 0.5f, 0.5f, 1.0f},
    //    .
    //}
    , vertex_input{
        erhe::graphics::Vertex_input_state_data::make(vertex_format)
    }
{
}

} // namespace editor
