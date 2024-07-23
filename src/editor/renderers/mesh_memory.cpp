#include "renderers/mesh_memory.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_scene_renderer/program_interface.hpp"

namespace editor {

static constexpr gl::Buffer_storage_mask storage_mask{gl::Buffer_storage_mask::map_write_bit};

auto Mesh_memory::get_vertex_buffer_size() const -> std::size_t
{
    int vertex_buffer_size{32}; // in megabytes
    const auto ini = erhe::configuration::get_ini("erhe.ini", "mesh_memory");
    ini->get("vertex_buffer_size", vertex_buffer_size);
    std::size_t kilo = 1024;
    std::size_t mega = 1024 * kilo;
    return static_cast<std::size_t>(vertex_buffer_size) * mega;
}

auto Mesh_memory::get_index_buffer_size() const -> std::size_t
{
    int index_buffer_size{8}; // in megabytes
    const auto ini = erhe::configuration::get_ini("erhe.ini", "mesh_memory");
    ini->get("index_buffer_size", index_buffer_size);
    std::size_t kilo = 1024;
    std::size_t mega = 1024 * kilo;
    return static_cast<std::size_t>(index_buffer_size) * mega;
}

Mesh_memory::Mesh_memory(erhe::graphics::Instance& graphics_instance, erhe::scene_renderer::Program_interface& program_interface)
    : graphics_instance{graphics_instance}
    , vertex_format{
        erhe::graphics::Vertex_attribute::position_float3(),
        erhe::graphics::Vertex_attribute::normal0_float3(),
        erhe::graphics::Vertex_attribute::normal1_float3(), // editor wireframe bias requires smooth normal attribute
        erhe::graphics::Vertex_attribute::tangent_float4(),
        erhe::graphics::Vertex_attribute::texcoord0_float2(),
        erhe::graphics::Vertex_attribute::color_ubyte4(),
        erhe::graphics::Vertex_attribute::aniso_control_ubyte2(),
        erhe::graphics::Vertex_attribute::joint_indices0_ubyte4(),
        erhe::graphics::Vertex_attribute::joint_weights0_float4(),
        erhe::graphics::Vertex_attribute::vertex_valency()
    }
    , gl_vertex_buffer{graphics_instance, gl::Buffer_target::array_buffer, get_vertex_buffer_size(), storage_mask}
    , gl_index_buffer{graphics_instance, gl::Buffer_target::element_array_buffer, get_index_buffer_size(), storage_mask}
    , gl_buffer_sink{gl_buffer_transfer_queue, gl_vertex_buffer, gl_index_buffer}
    , buffer_info{
        .index_type    = erhe::dataformat::Format::format_32_scalar_uint,
        .vertex_format = vertex_format,
        .buffer_sink   = gl_buffer_sink
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
        erhe::graphics::Vertex_input_state_data::make(program_interface.attribute_mappings, vertex_format, &gl_vertex_buffer, &gl_index_buffer)
    }
{
    gl_vertex_buffer.set_debug_label("Mesh Memory Vertex");
    gl_index_buffer .set_debug_label("Mesh Memory Index");
}

} // namespace editor
