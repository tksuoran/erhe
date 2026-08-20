#pragma once

#include "erhe_buffer/free_list_allocator.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/debug_label.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace erhe::graphics {
    class Buffer;
    class Device;
}

namespace erhe::scene_renderer {

// One range of one pool block whose owning Buffer_mesh has been destroyed
// but which may still be read by frames in flight. Applied to the block's
// free list (Buffer_pool::apply_retired) only after the GPU has completed
// the frame that was current when the range was collected.
class Retired_range
{
public:
    erhe::buffer::Free_list_allocator* allocator  {nullptr};
    std::size_t                        byte_offset{0};
    std::size_t                        byte_count {0};
};

// A GPU buffer plus its sub-allocator. Implements
// erhe::buffer::Buffer_allocation_owner by RETIRING released ranges into a
// pending list instead of freeing them: a Buffer_mesh being destroyed
// (async finalize swapping in the full mesh over the fill-only proxy, mesh
// delete, undo, scene close) must not make its vertex / index ranges
// reusable while a submitted frame may still draw from them - a reuse
// would let another mesh's upload land in memory the GPU is reading.
// Mesh_memory::flush() collects the pending ranges once per frame and
// frees them from a device frame-completion handler.
class Pool_block : public erhe::buffer::Buffer_allocation_owner
{
public:
    Pool_block(
        uint64_t                                  buffer_id,
        std::unique_ptr<erhe::graphics::Buffer>&& buffer,
        erhe::buffer::Free_list_allocator&&       allocator
    );

    // Any thread.
    void release_allocation(std::size_t byte_offset, std::size_t byte_count) noexcept override;

    // Moves the pending retired ranges to out_retired (appends).
    void collect_retired(std::vector<Retired_range>& out_retired);

    [[nodiscard]] auto get_pending_retired_byte_count() const -> std::size_t;

    uint64_t                                buffer_id;
    std::unique_ptr<erhe::graphics::Buffer> buffer;
    erhe::buffer::Free_list_allocator       allocator;

private:
    mutable std::mutex                      m_retired_mutex;
    std::vector<Retired_range>              m_retired;
    std::size_t                             m_retired_byte_count{0};
};

class Buffer_pool_block_create_info
{
public:
    erhe::graphics::Buffer_usage usage                             {0};
    uint64_t                     required_memory_property_bit_mask {0};
    uint64_t                     preferred_memory_property_bit_mask{0};
    std::size_t                  block_size_bytes                  {0};
    std::size_t                  max_blocks                        {0};
    std::string                  debug_label_prefix                {};
};

// Buffer_pool owns one slab of GPU buffer memory (one or more Pool_block
// instances) and hands out byte ranges to mesh builds via allocate().
//
// IMPORTANT -- pool identity is intentionally per Vertex_stream INSTANCE,
// not per Vertex_stream layout. Two Vertex_formats whose stream layouts
// happen to be byte-for-byte identical (e.g. vertex_format_skinned and
// vertex_format_not_skinned both carry the same {normal, tangent,
// texcoord, color} layout on stream 1) still get their own dedicated
// pools. This is required for correctness, not an optimisation.
//
// Why: the forward renderer issues multi-draw indexed indirect, and each
// indirect command carries a single scalar `vertexOffset` (a.k.a.
// base_vertex). The GPU applies that one scalar to every binding:
//
//     byte_read_K = (vertexOffset + N) * stride_K
//
// For all bindings of a mesh to land on its own data, the per-stream
// quantity `byte_offset_K / stride_K` must be IDENTICAL across every
// stream K of that mesh. We call this the lockstep invariant.
//
// Buffer_mesh::base_vertex() (buffer_mesh.cpp:11-14) computes the
// indirect command's vertexOffset from stream 0 only, on the assumption
// that the invariant holds. If two Vertex_formats share a pool for one
// stream but not for another, the shared pool advances for both formats'
// meshes while the non-shared pool only advances for one of them. The
// next mesh of the format whose private pool is "behind" then sees
// different `byte_offset_K / stride_K` values across streams, breaking
// the invariant. At draw time the GPU reads stream 0 from the right
// place but reads stream 1 / stream 2 from some other mesh's data --
// the symptom is correct positions but garbage normals / tangents /
// tex_coords / colors.
//
// Keying pools by Vertex_stream pointer identity eliminates this class
// of bug at the cost of a small amount of buffer duplication: each
// Vertex_format keeps its own copy of each stream pool even when other
// formats share the same byte layout. The waste is bounded by
// (num_formats * stride_K) per stream layout -- a few MB at most.
class Buffer_pool
{
public:
    // The vertex-stream constructor stores the address of the passed
    // Vertex_stream as the pool's identity. The caller must ensure the
    // Vertex_stream object outlives the pool (in practice, callers pass
    // streams that are stable members of long-lived Vertex_format
    // objects owned by Mesh_memory).
    Buffer_pool(
        erhe::graphics::Device&                graphics_device,
        uint64_t                               pool_id,
        const erhe::dataformat::Vertex_stream& vertex_stream,
        Buffer_pool_block_create_info          block_create_info
    );

    Buffer_pool(
        erhe::graphics::Device&       graphics_device,
        uint64_t                      pool_id,
        erhe::dataformat::Format      index_format,
        Buffer_pool_block_create_info block_create_info
    );

    Buffer_pool(const Buffer_pool&)            = delete;
    Buffer_pool& operator=(const Buffer_pool&) = delete;
    Buffer_pool(Buffer_pool&&) noexcept;
    Buffer_pool& operator=(Buffer_pool&&)      = delete;

    ~Buffer_pool();

    [[nodiscard]] auto is_compatible    (const erhe::dataformat::Vertex_stream& vertex_stream) const -> bool;
    [[nodiscard]] auto is_compatible    (erhe::dataformat::Format index_format) const -> bool;
    [[nodiscard]] auto allocate         (std::size_t element_count) -> erhe::primitive::Buffer_sink_allocation;
    [[nodiscard]] auto get_buffer       (uint64_t buffer_id) const -> erhe::graphics::Buffer*;
    [[nodiscard]] auto get_vertex_stream() const -> const erhe::dataformat::Vertex_stream&;
    [[nodiscard]] auto get_index_format () const -> erhe::dataformat::Format;

    // Moves every block's pending retired ranges to out_retired (appends).
    // Main thread (Mesh_memory::flush()).
    void collect_retired(std::vector<Retired_range>& out_retired);

    // Frees collected ranges. Caller guarantees the GPU has completed every
    // frame that may read them and holds
    // erhe::primitive::buffer_mesh_allocation_mutex() (lockstep invariant:
    // no allocation transaction may interleave with the frees).
    static void apply_retired(const std::vector<Retired_range>& retired);

    // Byte accounting for memory reporting (doc/reloadable-asset-loads.md).
    // Capacity is what the pool has committed in VkBuffer blocks - it only
    // ever grows, because blocks are never destroyed. Used is what the free
    // list currently hands out, so it is what drops when meshes are released.
    class Statistics
    {
    public:
        std::size_t block_count         {0};
        std::size_t capacity_bytes      {0};
        std::size_t used_bytes          {0};
        std::size_t free_bytes          {0};
        std::size_t allocation_count    {0};
        std::size_t pending_retired_bytes{0}; // released, not yet frame-safe to reuse
    };
    [[nodiscard]] auto get_statistics() const -> Statistics;
    [[nodiscard]] auto get_debug_label() const -> const std::string&;

private:
    [[nodiscard]] auto allocate_internal(std::size_t allocation_byte_count, std::size_t allocation_alignment) -> std::optional<std::pair<Pool_block*, std::size_t>>;
    [[nodiscard]] auto create_new_block (std::size_t min_capacity_bytes) -> bool;
    [[nodiscard]] auto describe() const -> std::string;

    erhe::graphics::Device&                  m_graphics_device;
    // Local copy of the stream's layout (stride / attributes), used for
    // allocate() sizing and debug/logging.
    erhe::dataformat::Vertex_stream          m_vertex_stream;
    // Address of the originating Vertex_stream instance. The pool's
    // is_compatible(stream) check is pointer equality against this
    // member, NOT a layout-equality test. See the class-level comment
    // for the lockstep invariant this enforces.
    const erhe::dataformat::Vertex_stream*   m_source_vertex_stream{nullptr};
    erhe::dataformat::Format                 m_index_format{erhe::dataformat::Format::format_undefined};
    Buffer_pool_block_create_info            m_block_create_info;
    std::vector<std::unique_ptr<Pool_block>> m_blocks;
    uint64_t                                 m_pool_id;
    uint64_t                                 m_next_buffer_id{0};
};

} // namespace erhe::scene_renderer
