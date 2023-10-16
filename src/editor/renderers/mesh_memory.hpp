#pragma once

#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/buffer_sink.hpp"

namespace erhe::graphics {
    class Buffer;
    class Buffer_transfer_queue;
    class Instance;
    class Shader_resource;
    class Vertex_format;
    class Vertex_input_state;
}
namespace erhe::primitive {
    class Gl_buffer_sink;
}
namespace erhe::scene_renderer {
    class Program_interface;
}

namespace editor
{

class Mesh_memory
{
public:
    Mesh_memory(
        erhe::graphics::Instance&                graphics_instance,
        erhe::scene_renderer::Program_interface& program_interface
    );

    erhe::graphics::Instance&             graphics_instance;
    erhe::graphics::Buffer_transfer_queue gl_buffer_transfer_queue;
    erhe::graphics::Vertex_format         vertex_format;
    erhe::graphics::Buffer                gl_vertex_buffer;
    erhe::graphics::Buffer                gl_index_buffer;
    erhe::primitive::Gl_buffer_sink       gl_buffer_sink;
    erhe::primitive::Buffer_info          buffer_info;
    //erhe::primitive::Build_info           build_info;
    erhe::graphics::Vertex_input_state    vertex_input;
    //erhe::graphics::Shader_resource       vertex_data_in;   // For SSBO read
    //erhe::graphics::Shader_resource       vertex_data_out;  // For SSBO write

private:
    [[nodiscard]] auto get_vertex_buffer_size() const -> std::size_t;
    [[nodiscard]] auto get_index_buffer_size() const -> std::size_t;
};

} // namespace editor
