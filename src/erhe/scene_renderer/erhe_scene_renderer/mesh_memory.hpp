#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics_buffer_sink/buffer_pool.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_scene_renderer/format_pools.hpp"
#include "erhe_scene_renderer/generated/mesh_memory_config.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

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

    [[nodiscard]] auto get_vertex_buffer       (std::size_t stream_index) -> erhe::graphics::Buffer*;
    [[nodiscard]] auto get_default_index_buffer() -> erhe::graphics::Buffer*;

    // Look up (or create) the Format_pools that owns vertex storage for the
    // given Vertex_format. The lookup key is the uint64_t hash returned by
    // compute_vertex_format_key, which folds stream bindings, strides, and
    // each attribute's (usage, usage_index, format, offset); two formats
    // that share that full layout collapse to the same Format_pools.
    //
    // The default editor format's pools are accessible both via this method
    // and via the legacy direct fields below (buffer_info, vertex_input,
    // vertex_pool_position, etc.). The two views refer to the same Format_pools
    // instance; the legacy fields stay so older call sites compile unchanged.
    [[nodiscard]] auto get_or_create_format_pools(const erhe::dataformat::Vertex_format& format) -> Format_pools&;

    erhe::graphics::Device&                          graphics_device;
    erhe::graphics::Buffer_transfer_queue            buffer_transfer_queue;
    erhe::dataformat::Vertex_format&                 vertex_format;
    erhe::graphics_buffer_sink::Buffer_pool          index_pool;
    erhe::graphics_buffer_sink::Buffer_pool          edge_line_vertex_pool;
    Format_pools                                     default_format_pools;

    // Reference aliases into default_format_pools. Existing call sites that
    // wrote `mesh_memory.buffer_info` or took its address keep compiling.
    erhe::primitive::Buffer_info&                    buffer_info;
    erhe::graphics::Vertex_input_state&              vertex_input;
    erhe::graphics_buffer_sink::Graphics_buffer_sink& graphics_buffer_sink;
    erhe::graphics_buffer_sink::Buffer_pool&         vertex_pool_position;
    erhe::graphics_buffer_sink::Buffer_pool&         vertex_pool_non_position;
    erhe::graphics_buffer_sink::Buffer_pool&         vertex_pool_custom;

private:
    Mesh_memory_config                                          m_config;
    // Guards m_extra_format_pools insertion and lookup. The map is
    // populated lazily by get_or_create_format_pools(); today the
    // editor only constructs Mesh_memory at init and adds extra
    // formats from a single thread, but parallel-init taskflow
    // workers (and any future runtime adder) need the same protection.
    mutable std::mutex                                          m_extra_format_pools_mutex;
    std::unordered_map<uint64_t, std::unique_ptr<Format_pools>> m_extra_format_pools;
    uint64_t                                                    m_default_format_key{0};
};

}
