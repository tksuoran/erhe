#pragma once

#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/primitive/build_info.hpp"
#include "erhe/primitive/buffer_sink.hpp"
#include "erhe/primitive/enums.hpp"

#include <memory>

namespace erhe::graphics
{
    class Buffer;
    class Buffer_transfer_queue;
    class Instance;
    class Shader_resource;
    class Vertex_format;
    class Vertex_input_state;
}

namespace erhe::primitive
{
    class Gl_buffer_sink;
}

namespace erhe::scene_renderer
{
    class Program_interface;
}

namespace example
{


class Mesh_memory
{
public:
    Mesh_memory(
        erhe::graphics::Instance&                graphics_instance,
        erhe::scene_renderer::Program_interface& program_interface
    );

    erhe::graphics::Instance&             graphics_instance;
    erhe::graphics::Vertex_format         vertex_format;
    erhe::graphics::Buffer                gl_vertex_buffer;
    erhe::graphics::Buffer                gl_index_buffer;
    erhe::primitive::Buffer_info          buffer_info;
    erhe::graphics::Buffer_transfer_queue gl_buffer_transfer_queue;
    erhe::primitive::Gl_buffer_sink       gl_buffer_sink;
    erhe::graphics::Vertex_input_state    vertex_input;
    //erhe::graphics::Shader_resource       vertex_data_in;   // For SSBO read
    //erhe::graphics::Shader_resource       vertex_data_out;  // For SSBO write

private:
    [[nodiscard]] auto get_vertex_buffer_size() const -> std::size_t;
    [[nodiscard]] auto get_index_buffer_size() const -> std::size_t;
};

} // namespace example
