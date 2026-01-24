#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics_buffer_sink/graphics_buffer_sink.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_primitive/build_info.hpp"

namespace erhe::graphics {
    class Buffer;
    class Buffer_transfer_queue;
    class Device;
    class Shader_resource;
    class Vertex_format;
    class Vertex_input_state;
}
namespace erhe::scene_renderer {
    class Program_interface;
}

namespace editor {

class Mesh_memory final
{
public:
    Mesh_memory(erhe::graphics::Device& graphics_device, erhe::dataformat::Vertex_format& vertex_format);
    ~Mesh_memory() noexcept;

    // TODO Correct location for these?
    static constexpr std::size_t s_vertex_binding_position     = 0;
    static constexpr std::size_t s_vertex_binding_non_position = 1;
    static constexpr std::size_t s_vertex_binding_custom       = 2;

    [[nodiscard]] auto get_vertex_buffer(std::size_t stream_index) -> erhe::graphics::Buffer*;

    erhe::graphics::Device&                          graphics_device;
    erhe::graphics::Buffer_transfer_queue            buffer_transfer_queue;
    erhe::dataformat::Vertex_format&                 vertex_format;
    erhe::graphics::Buffer                           vertex_buffer_position;
    erhe::graphics::Buffer                           vertex_buffer_non_position;
    erhe::graphics::Buffer                           vertex_buffer_custom;
    erhe::graphics::Buffer                           index_buffer;
    erhe::graphics_buffer_sink::Graphics_buffer_sink graphics_buffer_sink;
    erhe::primitive::Buffer_info                     buffer_info;
    erhe::graphics::Vertex_input_state               vertex_input;

private:
    [[nodiscard]] auto get_vertex_buffer_size(std::size_t stream) const -> std::size_t;
    [[nodiscard]] auto get_index_buffer_size() const -> std::size_t;
};

}
