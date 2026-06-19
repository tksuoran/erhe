#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_primitive/buffer_info.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene_renderer/generated/mesh_memory_config.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace erhe::graphics {
    class Buffer;
    class Buffer_transfer_queue;
    class Device;
    class Shader_resource;
    class Vertex_format;
    class Vertex_input_state;
}
namespace erhe::graphics_buffer_sink {
    class Graphics_vertex_buffer_sink;
    class Graphics_index_buffer_sink;
}
namespace erhe::primitive {
    class Buffer_mesh;
    class Buffer_range;
    class Index_buffer_writer;
    class Vertex_buffer_writer;
}
namespace erhe::scene {
    class Mesh;
    class Mesh_primitive;
}

struct Mesh_memory_config;

namespace erhe::scene_renderer {

class Buffer_pool;
class Program_interface;

// Used to identify a specific buffer in specific pool - owned by Mesh_memory.
// User still needs to know whether it's from vertex buffer pool or index buffer pool.
class Pool_buffer_identity
{
public:
    std::size_t pool_id;
    std::size_t buffer_id;

    [[nodiscard]] auto operator==(const Pool_buffer_identity& other) const -> bool
    {
        return (pool_id == other.pool_id) && (buffer_id == other.buffer_id);
    }
    [[nodiscard]] auto operator!=(const Pool_buffer_identity& other) const -> bool
    {
        return !(*this == other);
    }
};

class Vertex_input_entry
{
public:
    size_t                                              key;
    std::unique_ptr<erhe::graphics::Vertex_input_state> vertex_input;
    erhe::dataformat::Vertex_format                     vertex_format;
};

class Mesh_memory final
    : public erhe::primitive::Vertex_buffer_sink
    , public erhe::primitive::Index_buffer_sink
{
public:
    Mesh_memory(
        const Mesh_memory_config& mesh_memory_config,
        erhe::graphics::Device&   graphics_device
    );
    ~Mesh_memory() noexcept;

    auto get_vertex_input_from_vertex_format(const erhe::dataformat::Vertex_format& vertex_format) -> const Vertex_input_entry&;
    auto get_empty_vertex_input() -> const Vertex_input_entry&;

    [[nodiscard]] auto get_vertex_input(size_t vertex_input_key) const -> const Vertex_input_entry&;

    [[nodiscard]] auto make_primitive_buffer_info        () -> erhe::primitive::Buffer_info;
    // Same as make_primitive_buffer_info but uses vertex_format_skinned, so
    // skinned meshes get joint_indices + joint_weights vertex attributes in
    // the GPU vertex buffer. Required for the standard.vert skinning path
    // (Shader_key::derive checks the vertex_format for joint attributes).
    [[nodiscard]] auto make_skinned_primitive_buffer_info() -> erhe::primitive::Buffer_info;
    void flush(erhe::graphics::Command_buffer& command_buffer);

    [[nodiscard]] auto get_vertex_buffer(const erhe::primitive::Buffer_range& buffer_range) -> erhe::graphics::Buffer*;
    [[nodiscard]] auto get_vertex_buffer(const Pool_buffer_identity& buffer_identity) -> erhe::graphics::Buffer*;
    [[nodiscard]] auto get_index_buffer (const erhe::primitive::Buffer_range& buffer_range) -> erhe::graphics::Buffer*;
    [[nodiscard]] auto get_index_buffer (const Pool_buffer_identity& buffer_identity) -> erhe::graphics::Buffer*;

    [[nodiscard]] auto get_vertex_stream(const Pool_buffer_identity& buffer_identity) -> erhe::dataformat::Vertex_stream;
    [[nodiscard]] auto get_index_format (const Pool_buffer_identity& buffer_identity) -> erhe::dataformat::Format;

    // Implements erhe::primitive::Vertex_buffer_sink
    auto allocate_vertex_buffer_range(const erhe::dataformat::Vertex_stream& vertex_stream, std::size_t vertex_count) -> erhe::primitive::Buffer_sink_allocation override;
    void enqueue_vertex_data         (const erhe::primitive::Buffer_range& buffer_range, std::vector<uint8_t>&& data) override;
    void vertex_writer_ready         (erhe::primitive::Vertex_buffer_writer& writer)                                  override;

    // Implements erhe::primitive::Index_buffer_sink
    auto allocate_index_buffer_range(const erhe::dataformat::Format index_format, std::size_t index_count) -> erhe::primitive::Buffer_sink_allocation override;
    void enqueue_index_data         (const erhe::primitive::Buffer_range& buffer_range, std::vector<uint8_t>&& data) override;
    void index_writer_ready         (erhe::primitive::Index_buffer_writer&  writer)                                  override;

    erhe::dataformat::Vertex_format vertex_format_empty;
    erhe::dataformat::Vertex_format vertex_format_skinned;
    erhe::dataformat::Vertex_format vertex_format_not_skinned;

    // Expanded solid-wireframe fill formats: identical streams to
    // vertex_format_not_skinned / vertex_format_skinned, plus a dedicated
    // stream (binding 3) carrying the packed wireframe attribute
    // (custom_attribute_wireframe: corner index + real-edge mask). Used to
    // allocate Buffer_mesh::expanded_vertex_buffer_ranges; the extra stream
    // keeps streams 0..2 byte-identical to the base format so the shared
    // build's per-attribute offsets stay valid for the expanded build.
    erhe::dataformat::Vertex_format vertex_format_not_skinned_wireframe;
    erhe::dataformat::Vertex_format vertex_format_skinned_wireframe;

    // Single-stream vertex format used to allocate
    // Buffer_mesh::edge_line_vertex_buffer_range. The stream layout
    // matches the compute shader's input SSBO struct
    // (Content_wide_line_renderer reads it as
    //   struct edge_line_vertex { vec4 position; vec4 normal; }).
    // Each Buffer_pool keys on Vertex_stream, so this dedicated
    // stream naturally lives in its own pool independent of the
    // main mesh vertex pools.
    erhe::dataformat::Vertex_format vertex_format_edge_line;

    // Parallel single-stream format for the joint side buffer that
    // skinned edge lines need. Layout matches the compute shader's
    // skinned-variant SSBO struct
    //   struct edge_line_joint { uvec4 joint_indices; vec4 joint_weights; }.
    // Allocated only for meshes that carry joint attributes; lives in
    // its own Buffer_pool keyed on this stream.
    erhe::dataformat::Vertex_format vertex_format_edge_line_joints;

private:
    Mesh_memory_config                    m_mesh_memory_config;
    erhe::graphics::Device&               m_graphics_device;
    std::vector<Vertex_input_entry>       m_vertex_input_entries;
    std::vector<Buffer_pool>              m_vertex_pools;
    std::vector<Buffer_pool>              m_index_pools;
    erhe::graphics::Buffer_transfer_queue m_buffer_transfer_queue;
};

class Mesh_primitive_entry
{
public:
    erhe::scene::Mesh* mesh{nullptr};
    const uint16_t     mesh_primitive_index;
};

// Identifies a specific vertex input state, specific index buffer, and specific vertex buffers.
class Buffer_set
{
public:
    size_t                            vertex_input_key;
    Pool_buffer_identity              index_buffer;
    std::vector<Pool_buffer_identity> vertex_buffers;

    [[nodiscard]] auto valid() const -> bool
    {
        return !vertex_buffers.empty();
    }

    [[nodiscard]] auto operator==(const Buffer_set& other) const -> bool
    {
        return
            (vertex_input_key == other.vertex_input_key) &&
            (index_buffer     == other.index_buffer) &&
            (vertex_buffers   == other.vertex_buffers);
    }

    [[nodiscard]] auto operator!=(const Buffer_set& other) const -> bool
    {
        return !(*this == other);
    }
};

// Each bucket is a group of Mesh_primitives that can be rendered using same render pipeline
// state - they share the same vertex input state, index buffer, and vertex buffers.
// Buckets are also partitioned by the negative-determinant flag of the mesh world
// transform: mirrored geometry has reversed apparent triangle winding and must be
// drawn with a front-face-flipped pipeline variant (see
// Base_render_pipeline::get_pipeline_for front_face_flip).
class Render_bucket
{
public:
    Render_bucket();
    ~Render_bucket() noexcept;

    Render_bucket(
        erhe::scene::Mesh&                  mesh,
        const std::size_t                   mesh_primitive_index,
        const erhe::primitive::Buffer_mesh& buffer_mesh,
        const Shader_key&                   shader_key,
        const uint64_t                      shader_key_hash,
        const bool                          negative_determinant,
        const erhe::primitive::Primitive_mode primitive_mode
    );

    [[nodiscard]] auto accept(
        erhe::scene::Mesh&                  mesh,
        const std::size_t                   mesh_primitive_index,
        const erhe::primitive::Buffer_mesh& buffer_mesh,
        const uint64_t                      primitive_shader_key_hash,
        const bool                          primitive_negative_determinant
    ) -> bool;

    Buffer_set                        buffer_set;
    std::vector<Mesh_primitive_entry> entries;
    Shader_key                        shader_key{};
    uint64_t                          shader_key_hash;
    bool                              negative_determinant{false};
    // The primitive mode this bucket draws. solid_wireframe selects the
    // expanded vertex input key + expanded vertex buffer ranges of each
    // Buffer_mesh (the index buffer is shared with the normal ranges).
    erhe::primitive::Primitive_mode   primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};
};

enum class Blending_mode_policy : uint32_t
{
    not_set                            = 0, // error
    opaque_primitives_only             = 1, // Keep opaque primitives only
    translucent_primitives_only        = 2, // Keep translucent primitives only
    allow_all                          = 3, // unique buckets for each blending mode
    override_with_base_render_pipeline = 4  // override primitive blending mode from base render pipeline
};

void bucket_primitives(
    std::vector<Render_bucket>&                                buckets,
    uint32_t                                                   boolean_mask_force_enable,
    uint32_t                                                   boolean_mask_force_disable,
    const Mesh_memory&                                         mesh_memory,
    const Shader_key&                                          environment_shader_key,
    const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes,
    const erhe::Item_filter&                                   filter,
    erhe::primitive::Primitive_mode                            primitive_mode,
    Blending_mode_policy                                       blending_mode_policy,
    const erhe::Item_filter&                                   shader_debug_filter = {}
);

}
