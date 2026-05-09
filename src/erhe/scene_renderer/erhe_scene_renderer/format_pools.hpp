#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics_buffer_sink/buffer_pool.hpp"
#include "erhe_graphics_buffer_sink/graphics_buffer_sink.hpp"
#include "erhe_primitive/build_info.hpp"

#include <memory>
#include <string>
#include <vector>

namespace erhe::graphics {
    class Buffer_transfer_queue;
    class Device;
}

struct Mesh_memory_config;

namespace erhe::scene_renderer {

// One Vertex_format's worth of GPU buffer storage. Owns a Buffer_pool per
// stream of the format plus the Graphics_buffer_sink, Buffer_info, and
// Vertex_input_state needed to build and bind primitives in this format.
//
// Index and edge-line storage is shared across all formats, so Format_pools
// holds references (not ownership) to those external pools.
//
// Used by Mesh_memory's format-pool registry: get_or_create_format_pools(format)
// returns a Format_pools& keyed by Vertex_format_key (a uint32_t bit-mask of
// which attribute usages the format declares).
class Format_pools
{
public:
    Format_pools(
        erhe::graphics::Device&                  graphics_device,
        erhe::graphics::Buffer_transfer_queue&   transfer_queue,
        const Mesh_memory_config&                config,
        erhe::dataformat::Vertex_format          vertex_format,
        erhe::graphics_buffer_sink::Buffer_pool& index_pool,
        erhe::graphics_buffer_sink::Buffer_pool& edge_line_vertex_pool,
        std::string                              debug_label_prefix
    );
    ~Format_pools();

    Format_pools(const Format_pools&)            = delete;
    Format_pools(Format_pools&&)                 = delete;
    Format_pools& operator=(const Format_pools&) = delete;
    Format_pools& operator=(Format_pools&&)      = delete;

    erhe::dataformat::Vertex_format                                       vertex_format;
    std::vector<std::unique_ptr<erhe::graphics_buffer_sink::Buffer_pool>> vertex_pools;
    erhe::graphics_buffer_sink::Graphics_buffer_sink                      graphics_buffer_sink;
    erhe::primitive::Buffer_info                                          buffer_info;
    erhe::graphics::Vertex_input_state                                    vertex_input;
};

} // namespace erhe::scene_renderer
