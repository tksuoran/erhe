#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics_buffer_sink/graphics_buffer_sink.hpp"
#include "erhe_primitive/build_info.hpp"

namespace erhe::graphics {
    class Device;
}

namespace example {

class Mesh_memory
{
public:
    explicit Mesh_memory(erhe::graphics::Device& graphics_device);

    erhe::graphics::Device&                          graphics_device;
    erhe::dataformat::Vertex_format                  vertex_format;
    erhe::graphics::Buffer                           position_vertex_buffer;
    erhe::graphics::Buffer                           non_position_vertex_buffer;
    erhe::graphics::Buffer                           index_buffer;
    erhe::graphics::Buffer_transfer_queue            gl_buffer_transfer_queue;
    erhe::graphics_buffer_sink::Graphics_buffer_sink graphics_buffer_sink;
    erhe::primitive::Buffer_info                     buffer_info;
    erhe::graphics::Vertex_input_state               vertex_input;

private:
    [[nodiscard]] auto get_vertex_buffer_size(std::size_t stream_index) const -> std::size_t;
    [[nodiscard]] auto get_index_buffer_size() const -> std::size_t;
};

} // namespace example
