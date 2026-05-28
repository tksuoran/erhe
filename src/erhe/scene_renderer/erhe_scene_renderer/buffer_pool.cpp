#include "erhe_scene_renderer/buffer_pool.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"

#include "erhe_buffer/buffer_allocation.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_primitive/buffer_range.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <string>

namespace erhe::scene_renderer {

Buffer_pool::Buffer_pool(
    erhe::graphics::Device&                graphics_device,
    uint64_t                               pool_id,
    const erhe::dataformat::Vertex_stream& vertex_stream,
    Buffer_pool_block_create_info          block_create_info
)
    : m_graphics_device      {graphics_device}
    , m_vertex_stream        {vertex_stream}
    , m_source_vertex_stream {&vertex_stream}
    , m_block_create_info    {std::move(block_create_info)}
    , m_pool_id              {pool_id}
{
    log_mesh_memory->trace(
        "Buffer_pool::Buffer_pool() pool_id = {}, stream = {}",
        pool_id,
        vertex_stream.to_string()
    );
}

Buffer_pool::Buffer_pool(
    erhe::graphics::Device&       graphics_device,
    uint64_t                      pool_id,
    erhe::dataformat::Format      index_format,
    Buffer_pool_block_create_info block_create_info
)
    : m_graphics_device  {graphics_device}
    , m_vertex_stream    {erhe::dataformat::Vertex_stream::binding_unused_dummy}
    , m_index_format     {index_format}
    , m_block_create_info{std::move(block_create_info)}
    , m_pool_id          {pool_id}
{
    log_mesh_memory->trace(
        "Buffer_pool::Buffer_pool() pool_id = {}, index_format = {}",
        pool_id,
        erhe::dataformat::c_str(index_format)
    );
}

Buffer_pool::Buffer_pool(Buffer_pool&&) noexcept = default;
Buffer_pool::~Buffer_pool() = default;

auto Buffer_pool::is_compatible(const erhe::dataformat::Vertex_stream& vertex_stream) const -> bool
{
    ERHE_VERIFY(m_index_format == erhe::dataformat::Format::format_undefined);

    // Pointer-identity compatibility -- two Vertex_streams with the same
    // byte layout but coming from different Vertex_format instances are
    // NOT compatible here, by design. See the class-level comment in
    // buffer_pool.hpp for the lockstep-invariant rationale. The two
    // currently relevant cases this distinguishes are:
    //
    //   vertex_format_skinned    .streams[1]  (normal/tangent/texcoord/color)
    //   vertex_format_not_skinned.streams[1]  (normal/tangent/texcoord/color)
    //
    // -- byte-identical layouts, but they MUST live in different pools
    // because their containing formats use a stream-0 with different
    // strides. A pool shared between them would advance for both
    // formats' meshes and break the per-mesh
    //     byte_offset_K / stride_K = const
    // invariant that the indirect draw command's vertexOffset relies on.
    //
    // If a future change reintroduces layout-equality comparison here,
    // the symptom will be skinned-mesh polygon fill reading
    // normals/tangents/etc. from another mesh's bytes (positions stay
    // correct, every other stream-1+ attribute is garbage).
    return m_source_vertex_stream == &vertex_stream;
}

auto Buffer_pool::is_compatible(const erhe::dataformat::Format index_format) const -> bool
{
    ERHE_VERIFY(index_format != erhe::dataformat::Format::format_undefined);
    ERHE_VERIFY(m_index_format != erhe::dataformat::Format::format_undefined);
    return m_index_format == index_format;
}

auto Buffer_pool::allocate_internal(
    const std::size_t allocation_byte_count,
    const std::size_t allocation_alignment
) -> std::optional<std::pair<Pool_block*, std::size_t>>
{
    for (int i = 0; i < 2; ++i) {
        for (const std::unique_ptr<Pool_block>& block : m_blocks) {
            const std::optional<std::size_t> byte_offset_opt = block->allocator.allocate(allocation_byte_count, allocation_alignment);
            if (byte_offset_opt.has_value()) {
                return std::make_pair(block.get(), byte_offset_opt.value());
            }
        }
        if (!create_new_block(allocation_byte_count)) {
            return {};
        }
    }
    return {};
}

auto Buffer_pool::describe() const -> std::string
{
    if (m_index_format == erhe::dataformat::Format::format_undefined) {
        return fmt::format("pool_id = {}, stream = {}",
            m_pool_id,
            m_vertex_stream.to_string()
        );
    } else {
        return fmt::format("pool_id = {}, index format = {}",
            m_pool_id,
            erhe::dataformat::c_str(m_index_format)
        );
    }
}

auto Buffer_pool::create_new_block(const std::size_t min_capacity_bytes) -> bool
{
    const Buffer_pool_block_create_info& info = m_block_create_info;
    if (m_blocks.size() >= info.max_blocks) {
        ERHE_FATAL("Buffer_pool::create_new_block: block limit reached");
        return false;
    }
    const std::size_t capacity_bytes = std::max(info.block_size_bytes, min_capacity_bytes);
    const std::string label          = info.debug_label_prefix + " block " + std::to_string(m_blocks.size());

    erhe::graphics::Buffer_create_info buffer_ci{
        .capacity_byte_count                    = capacity_bytes,
        .memory_allocation_create_flag_bit_mask = 0,
        .usage                                  = info.usage,
        .required_memory_property_bit_mask      = info.required_memory_property_bit_mask,
        .preferred_memory_property_bit_mask     = info.preferred_memory_property_bit_mask,
        .init_data                              = nullptr,
        .debug_label                            = erhe::utility::Debug_label{label}
    };

    ERHE_VERIFY(m_blocks.size() == m_next_buffer_id);

    log_mesh_memory->trace(
        "Buffer_pool::create_new_block() new buffer_id = {}, {}",
        m_next_buffer_id,
        describe()
    );

    m_blocks.push_back(
        std::make_unique<Pool_block>(
            m_next_buffer_id++,
            std::make_unique<erhe::graphics::Buffer>(m_graphics_device, buffer_ci),
            erhe::buffer::Free_list_allocator{capacity_bytes}
        )
    );
    return true;
}

auto Buffer_pool::allocate(const std::size_t element_count) -> erhe::primitive::Buffer_sink_allocation
{
    log_mesh_memory->trace(
        "Buffer_pool::allocate(element_count = {}) {}",
        element_count,
        describe()
    );

    const std::size_t element_size = m_index_format == erhe::dataformat::Format::format_undefined
        ? m_vertex_stream.stride
        : erhe::dataformat::get_format_size_bytes(m_index_format);

    const std::size_t allocation_byte_count = element_count * element_size;
    const std::size_t allocation_alignment  = element_size;

    std::optional<std::pair<Pool_block*, std::size_t>> hit = allocate_internal(allocation_byte_count, allocation_alignment);
    if (!hit.has_value()) {
        ERHE_FATAL("Buffer_pool::allocate: out of memory");
    }

    Pool_block*       block       = hit.value().first;
    const std::size_t byte_offset = hit.value().second;

    log_mesh_memory->trace(
        "allocation buffer_id = {}, byte_offset = {}, element_count = {}, byte_count = {}",
        block->buffer_id,
        byte_offset,
        element_count,
        allocation_byte_count
    );

    return erhe::primitive::Buffer_sink_allocation{
        .range = erhe::primitive::Buffer_range{
            .count        = element_count,
            .element_size = element_size,
            .byte_offset  = byte_offset,
            .pool_id      = m_pool_id,
            .buffer_id    = block->buffer_id
        },
        .allocation = erhe::buffer::Buffer_allocation{block->allocator, byte_offset, allocation_byte_count}
    };
}

auto Buffer_pool::get_buffer(const uint64_t buffer_id) const -> erhe::graphics::Buffer*
{
    return m_blocks.at(buffer_id)->buffer.get();
}

auto Buffer_pool::get_vertex_stream() const -> const erhe::dataformat::Vertex_stream&
{
    return m_vertex_stream;
}

auto Buffer_pool::get_index_format() const -> erhe::dataformat::Format
{
    return m_index_format;
}

} // namespace erhe::scene_renderer
