#include "renderers/mesh_memory.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_scene_renderer/program_interface.hpp"

namespace editor {

auto Mesh_memory::get_vertex_buffer_size() const -> std::size_t
{
    int vertex_buffer_size{32}; // in megabytes
    const auto ini = erhe::configuration::get_ini("erhe.ini", "mesh_memory");
    ini->get("vertex_buffer_size", vertex_buffer_size);
    return vertex_buffer_size * 1024 * 1024;
}

auto Mesh_memory::get_index_buffer_size() const -> std::size_t
{
    int index_buffer_size{8}; // in megabytes
    const auto ini = erhe::configuration::get_ini("erhe.ini", "mesh_memory");
    ini->get("index_buffer_size", index_buffer_size);
    return index_buffer_size * 1024 * 1024;
}

Mesh_memory::Mesh_memory(
    igl::IDevice&                            device,
    erhe::scene_renderer::Program_interface& program_interface
)
    : device{device}
    , vertex_format{
        erhe::graphics::Vertex_attribute::position_float3(),
        erhe::graphics::Vertex_attribute::normal0_float3(),
        erhe::graphics::Vertex_attribute::normal1_float3(), // editor wireframe bias requires smooth normal attribute
        erhe::graphics::Vertex_attribute::tangent_float4(),
        erhe::graphics::Vertex_attribute::texcoord0_float2(),
        erhe::graphics::Vertex_attribute::color_ubyte4(),
        erhe::graphics::Vertex_attribute::aniso_control_ubyte2(),
        erhe::graphics::Vertex_attribute::joint_indices0_ubyte4(),
        erhe::graphics::Vertex_attribute::joint_weights0_float4()
    }
    , vertex_buffer{
        device.createBuffer(
            igl::BufferDesc(
                static_cast<igl::BufferDesc::BufferType>(igl::BufferDesc::BufferTypeBits::Vertex),
                nullptr,
                get_vertex_buffer_size(),
                igl::ResourceStorage::Shared,
                0,
                "Mesh memory vertex buffer"
            ),
            nullptr
        )
    }
    , index_buffer{
        device.createBuffer(
            igl::BufferDesc(
                static_cast<igl::BufferDesc::BufferType>(igl::BufferDesc::BufferTypeBits::Index),
                nullptr,
                get_index_buffer_size(),
                igl::ResourceStorage::Shared,
                0,
                "Mesh memory index buffer"
            ),
            nullptr
        )
    }
    , buffer_sink{
        device,
        *vertex_buffer.get(),
        0,
        *index_buffer.get(),
        0
    }
    , buffer_info{
        .usage         = igl::ResourceStorage::Shared, // TODO use transfer 
        .normal_style  = erhe::primitive::Normal_style::corner_normals, // TODO is this needed?
        .index_type    = igl::IndexFormat::UInt32,
        .vertex_format = vertex_format,
        .buffer_sink   = buffer_sink
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
        vertex_format.make_vertex_input_state(
            device,
            program_interface.attribute_mappings,
            vertex_buffer.get()
        )
        //erhe::graphics::Vertex_input_state_data::make(
        //    program_interface.attribute_mappings,
        //    vertex_format,
        //    &gl_vertex_buffer,
        //    &gl_index_buffer
        //)
    }
{
    //// vertex_buffer.set_debug_label("Mesh Memory Vertex");
    //// index_buffer .set_debug_label("Mesh Memory Index");
}

} // namespace editor
