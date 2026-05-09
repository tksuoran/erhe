#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics_buffer_sink/buffer_pool.hpp"
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

struct Mesh_memory_config;

namespace erhe::scene_renderer {

class Mesh_memory final
{
public:
    Mesh_memory(
        const Mesh_memory_config&        mesh_memory_config,
        erhe::graphics::Device&          graphics_device,
        erhe::dataformat::Vertex_format& vertex_format
    );
    ~Mesh_memory() noexcept;

    // TODO Correct location for these?
    static constexpr std::size_t s_vertex_binding_position     = 0;
    static constexpr std::size_t s_vertex_binding_non_position = 1;
    static constexpr std::size_t s_vertex_binding_custom       = 2;

    // Returns the first block of the requested vertex stream's pool, or
    // nullptr if the pool has not yet been grown by an allocate(). Provided
    // for callers that need a "default" buffer pointer; new code should bind
    // from Buffer_range::buffer instead.
    [[nodiscard]] auto get_vertex_buffer       (std::size_t stream_index) -> erhe::graphics::Buffer*;
    [[nodiscard]] auto get_default_index_buffer() -> erhe::graphics::Buffer*;

    erhe::graphics::Device&                          graphics_device;
    erhe::graphics::Buffer_transfer_queue            buffer_transfer_queue;
    erhe::dataformat::Vertex_format&                 vertex_format;
    // The edge-line vertex buffer is still owned eagerly by Mesh_memory so that
    // Content_wide_line_renderer's existing Buffer& constructor parameter
    // resolves at editor startup. Step 7 of the mesh_memory plan moves this
    // into edge_line_vertex_pool.
    erhe::graphics::Buffer                           edge_line_vertex_buffer;
    erhe::graphics_buffer_sink::Buffer_pool          vertex_pool_position;
    erhe::graphics_buffer_sink::Buffer_pool          vertex_pool_non_position;
    erhe::graphics_buffer_sink::Buffer_pool          vertex_pool_custom;
    erhe::graphics_buffer_sink::Buffer_pool          index_pool;
    erhe::graphics_buffer_sink::Buffer_pool          edge_line_vertex_pool;
    erhe::graphics_buffer_sink::Graphics_buffer_sink graphics_buffer_sink;
    erhe::primitive::Buffer_info                     buffer_info;
    erhe::graphics::Vertex_input_state               vertex_input;
};

}
