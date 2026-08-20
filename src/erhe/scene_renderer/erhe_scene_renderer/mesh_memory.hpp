#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_primitive/buffer_info.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene_renderer/buffer_pool.hpp"
#include "erhe_scene_renderer/generated/mesh_memory_config.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
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

// Which transfer queue a build path's vertex / index bytes go through
// (doc/async-asset-loading-plan.md 2.6).
enum class Mesh_memory_queue : unsigned int
{
    // Full drain once per frame, in Mesh_memory::flush. "Enqueued implies
    // uploaded by end of frame" holds, which is what lets callers build a
    // mesh and draw it in the SAME command buffer with no gate at all -
    // rendertarget meshes, brush previews, the scene builder, and the
    // init-command-buffer paths of example / rendering_test all rely on it.
    interactive = 0,

    // Budget-drained, only from Mesh_memory::flush_budgeted. Enqueued does
    // NOT imply uploaded, so the ONLY traffic allowed here is traffic whose
    // publish gates on the watermark:
    //     ticket_snapshot <= get_loader_transfer_queue().get_watermark()
    // Anything that publishes immediately must stay on the interactive
    // queue, or it will draw from buffers that are still trickling in.
    loader = 1
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

    // The queue selector is not a parameter on the queue but a choice of
    // SINK: Buffer_info holds a Vertex_buffer_sink& / Index_buffer_sink&,
    // and Mesh_memory itself is the interactive sink, so the loader queue
    // needs a second sink object (see m_loader_sink).
    [[nodiscard]] auto make_primitive_buffer_info        (Mesh_memory_queue queue = Mesh_memory_queue::interactive) -> erhe::primitive::Buffer_info;
    // Same as make_primitive_buffer_info but uses vertex_format_skinned, so
    // skinned meshes get joint_indices + joint_weights vertex attributes in
    // the GPU vertex buffer. Required for the standard.vert skinning path
    // (Shader_key::derive checks the vertex_format for joint attributes).
    [[nodiscard]] auto make_skinned_primitive_buffer_info(Mesh_memory_queue queue = Mesh_memory_queue::interactive) -> erhe::primitive::Buffer_info;

    // Main thread, once per frame, before the frame's draws are recorded:
    // records the queued vertex / index uploads into command_buffer and
    // collects the pool ranges retired since the last call (Buffer_meshes
    // destroyed on any thread) into one device frame-completion handler,
    // which frees them once the GPU has finished the current frame. Until
    // then the ranges stay allocated, so no new mesh can be written into
    // memory a frame in flight may still read.
    void flush(erhe::graphics::Command_buffer& command_buffer);

    // Drains ONLY the loader queue, up to max_byte_count bytes, and returns
    // the bytes recorded. Called from the asset-load tick slot; never from
    // the per-frame flush above, which must keep its full-drain semantics.
    auto flush_budgeted(erhe::graphics::Command_buffer& command_buffer, std::size_t max_byte_count) -> std::size_t;

    // The loader queue, for tickets and the publish watermark (plan 2.5).
    [[nodiscard]] auto get_loader_transfer_queue() -> erhe::graphics::Buffer_transfer_queue&;

    // Aggregated pool byte accounting for memory reporting
    // (doc/reloadable-asset-loads.md). `capacity` only ever grows - pool
    // blocks are never destroyed - so `used` is the figure that drops when
    // meshes are released. Note the release is frame-deferred: a caller
    // sampling this must advance several frames after a removal.
    class Pool_statistics
    {
    public:
        std::string            label;
        bool                   is_index_pool{false};
        Buffer_pool::Statistics statistics;
    };
    [[nodiscard]] auto get_pool_statistics() const -> std::vector<Pool_statistics>;

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

    // A retired-range batch whose frame has completed but which may still be
    // the target of a queued LOADER write: freeing it would let the range be
    // re-allocated and re-enqueued while the older write is still pending, so
    // the stale write would land last. Held until the loader watermark passes
    // the ticket high-water mark taken when the batch was collected (plan 2.5
    // free gate).
    class Pending_free
    {
    public:
        erhe::graphics::Buffer_transfer_queue::Ticket loader_ticket{0};
        std::vector<Retired_range>                    ranges;
    };

    // Second sink, so that Buffer_info can select the loader queue. Every
    // allocation delegates to Mesh_memory (the pools are SHARED between the
    // two queues - which is exactly why the free gate above is evaluated
    // against the loader watermark); only the enqueue target differs.
    class Loader_buffer_sink final
        : public erhe::primitive::Vertex_buffer_sink
        , public erhe::primitive::Index_buffer_sink
    {
    public:
        explicit Loader_buffer_sink(Mesh_memory& mesh_memory);

        auto allocate_vertex_buffer_range(const erhe::dataformat::Vertex_stream& vertex_stream, std::size_t vertex_count) -> erhe::primitive::Buffer_sink_allocation override;
        void enqueue_vertex_data         (const erhe::primitive::Buffer_range& buffer_range, std::vector<uint8_t>&& data) override;
        void vertex_writer_ready         (erhe::primitive::Vertex_buffer_writer& writer)                                  override;

        auto allocate_index_buffer_range(erhe::dataformat::Format index_format, std::size_t index_count) -> erhe::primitive::Buffer_sink_allocation override;
        void enqueue_index_data         (const erhe::primitive::Buffer_range& buffer_range, std::vector<uint8_t>&& data) override;
        void index_writer_ready         (erhe::primitive::Index_buffer_writer&  writer)                                  override;

    private:
        Mesh_memory& m_mesh_memory;
    };

private:
    friend class Loader_buffer_sink;

    void enqueue_vertex_data_to(erhe::graphics::Buffer_transfer_queue& queue, const erhe::primitive::Buffer_range& buffer_range, std::vector<uint8_t>&& data);
    void enqueue_index_data_to (erhe::graphics::Buffer_transfer_queue& queue, const erhe::primitive::Buffer_range& buffer_range, std::vector<uint8_t>&& data);
    void vertex_writer_ready_to(erhe::graphics::Buffer_transfer_queue& queue, erhe::primitive::Vertex_buffer_writer& writer);
    void index_writer_ready_to (erhe::graphics::Buffer_transfer_queue& queue, erhe::primitive::Index_buffer_writer&  writer);

    // Applies every pending free whose loader ticket the watermark has now
    // passed. Called from flush().
    void apply_ready_pending_frees();

    Mesh_memory_config                    m_mesh_memory_config;
    erhe::graphics::Device&               m_graphics_device;
    std::vector<Vertex_input_entry>       m_vertex_input_entries;
    std::vector<Buffer_pool>              m_vertex_pools;
    std::vector<Buffer_pool>              m_index_pools;
    erhe::graphics::Buffer_transfer_queue m_buffer_transfer_queue;
    erhe::graphics::Buffer_transfer_queue m_loader_transfer_queue;
    Loader_buffer_sink                    m_loader_sink;
    // Frame-completion handlers registered by flush() capture a weak_ptr to
    // this token; a handler that outlives the Mesh_memory (pools already
    // destroyed) then does nothing.
    std::shared_ptr<int>                  m_alive_token;
    ERHE_PROFILE_MUTEX(std::mutex, m_pending_free_mutex);
    std::vector<Pending_free>             m_pending_frees;
};

// Vertex input key / vertex buffer ranges of a Buffer_mesh for a primitive
// mode: solid_wireframe draws from the expanded vertex stream(s), everything
// else from the normal stream(s). Shared by Render_bucket and Draw_list_scene.
[[nodiscard]] auto bucket_vertex_input_key(const erhe::primitive::Buffer_mesh& buffer_mesh, erhe::primitive::Primitive_mode primitive_mode) -> std::size_t;
[[nodiscard]] auto bucket_vertex_ranges   (const erhe::primitive::Buffer_mesh& buffer_mesh, erhe::primitive::Primitive_mode primitive_mode) -> const std::vector<erhe::primitive::Buffer_range>&;

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
// Base_render_pipeline::get_pipeline_for front_face_flip), and by the
// material's double_sided flag (glTF material.doubleSided), which selects a
// face-culling-disabled pipeline variant.
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
        const bool                          double_sided,
        const erhe::primitive::Primitive_mode primitive_mode
    );

    [[nodiscard]] auto accept(
        erhe::scene::Mesh&                  mesh,
        const std::size_t                   mesh_primitive_index,
        const erhe::primitive::Buffer_mesh& buffer_mesh,
        const uint64_t                      primitive_shader_key_hash,
        const bool                          primitive_negative_determinant,
        const bool                          primitive_double_sided
    ) -> bool;

    Buffer_set                        buffer_set;
    std::vector<Mesh_primitive_entry> entries;
    Shader_key                        shader_key{};
    uint64_t                          shader_key_hash;
    bool                              negative_determinant{false};
    bool                              double_sided{false};
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
    const erhe::Item_filter&                                   shader_debug_filter = {},
    // Skip primitives whose material is unlit (KHR_materials_unlit). Used by
    // the shadow pass: unlit geometry (sky domes, backdrops, emissive decals)
    // is not part of the lit scene and should not occlude it.
    bool                                                       exclude_unlit_primitives = false
);

}
