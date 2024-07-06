#pragma once

#include "erhe_gl/wrapper_enums.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/enums.hpp"

#include <memory>

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

namespace example {

class Mesh_memory
{
public:
    Mesh_memory(erhe::graphics::Instance& graphics_instance, erhe::scene_renderer::Program_interface& program_interface);

    erhe::graphics::Instance&             graphics_instance;
    erhe::graphics::Vertex_format         vertex_format;
    erhe::graphics::Buffer                gl_vertex_buffer;
    erhe::graphics::Buffer                gl_index_buffer;
    erhe::graphics::Buffer_transfer_queue gl_buffer_transfer_queue;
    erhe::primitive::Gl_buffer_sink       gl_buffer_sink;
    erhe::primitive::Buffer_info          buffer_info;
    erhe::graphics::Vertex_input_state    vertex_input;

private:
    [[nodiscard]] auto get_vertex_buffer_size() const -> std::size_t;
    [[nodiscard]] auto get_index_buffer_size() const -> std::size_t;
};

} // namespace example
