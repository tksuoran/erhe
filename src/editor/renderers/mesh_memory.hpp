#pragma once

#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/buffer_sink.hpp"

namespace erhe::graphics {
    class Shader_resource;
    class Vertex_format;
    class Vertex_input_state;
}
namespace erhe::scene_renderer {
    class Program_interface;
}

namespace igl {
    class IDdevice;
}

namespace editor
{

class Mesh_memory
{
public:
    Mesh_memory(
        igl::IDevice&                            device,
        erhe::scene_renderer::Program_interface& program_interface
    );

    igl::IDevice&                           device;
    erhe::graphics::Vertex_format           vertex_format;
    std::shared_ptr<igl::IBuffer>           vertex_buffer;
    std::shared_ptr<igl::IBuffer>           index_buffer;
    erhe::primitive::Igl_buffer_sink        buffer_sink;
    erhe::primitive::Buffer_info            buffer_info;
    std::shared_ptr<igl::IVertexInputState> vertex_input;

private:
    [[nodiscard]] auto get_vertex_buffer_size() const -> std::size_t;
    [[nodiscard]] auto get_index_buffer_size() const -> std::size_t;
};

} // namespace editor
