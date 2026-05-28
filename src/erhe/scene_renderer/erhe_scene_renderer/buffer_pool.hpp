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

class Pool_block
{
public:
    uint64_t                                buffer_id;
    std::unique_ptr<erhe::graphics::Buffer> buffer;
    erhe::buffer::Free_list_allocator       allocator;
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
