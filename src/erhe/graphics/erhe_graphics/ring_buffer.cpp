// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/ring_buffer.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <cstring>

namespace erhe::graphics {

// https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html#usage_patterns_advanced_data_uploading

[[nodiscard]] auto get_memory_usage(const Ring_buffer_usage ring_buffer_usage) -> Memory_usage
{
    switch (ring_buffer_usage) {
        default:
        case Ring_buffer_usage::None      : ERHE_FATAL("Device_impl::allocate_ring_buffer_entry() - bad usage"); return Memory_usage::cpu_to_gpu;
        case Ring_buffer_usage::CPU_write : return Memory_usage::cpu_to_gpu;
        case Ring_buffer_usage::CPU_read  : return Memory_usage::gpu_to_cpu;
        case Ring_buffer_usage::GPU_access: return Memory_usage::gpu_only;
    }
}

[[nodiscard]] auto get_required_memory_property_bit_mask(const Ring_buffer_usage ring_buffer_usage) -> uint64_t
{
    switch (ring_buffer_usage) {
        default:
        case Ring_buffer_usage::None      : ERHE_FATAL("Device_impl::allocate_ring_buffer_entry() - bad usage"); return Memory_property_flag_bit_mask::none;
        case Ring_buffer_usage::CPU_write : return Memory_property_flag_bit_mask::host_write;
        case Ring_buffer_usage::CPU_read  : return Memory_property_flag_bit_mask::host_read;
        case Ring_buffer_usage::GPU_access: return Memory_property_flag_bit_mask::device_local;
    }
}

[[nodiscard]] auto get_preferred_memory_property_bit_mask(const Ring_buffer_usage ring_buffer_usage) -> uint64_t
{
    switch (ring_buffer_usage) {
        default:
        case Ring_buffer_usage::None      : ERHE_FATAL("Device_impl::allocate_ring_buffer_entry() - bad usage"); return Memory_property_flag_bit_mask::none;
        case Ring_buffer_usage::CPU_write : return Memory_property_flag_bit_mask::device_local | Memory_property_flag_bit_mask::host_persistent;
        case Ring_buffer_usage::CPU_read  : return Memory_property_flag_bit_mask::host_cached  | Memory_property_flag_bit_mask::host_persistent;
        case Ring_buffer_usage::GPU_access: return Memory_property_flag_bit_mask::none;
    }
}

Ring_buffer::Ring_buffer(
    Device&                        device,
    const Ring_buffer_create_info& create_info
)
    : m_device           {device}
    , m_ring_buffer_usage{create_info.ring_buffer_usage}
    , m_buffer{
        std::make_unique<Buffer>(
            m_device,
            Buffer_create_info{
                // NOTE: The ring buffer should be persistently mapped for CPU access
                .capacity_byte_count                    = create_info.size,
                .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::none,
                .usage                                  = create_info.buffer_usage,
                .required_memory_property_bit_mask      = get_required_memory_property_bit_mask(create_info.ring_buffer_usage),
                .preferred_memory_property_bit_mask     = get_preferred_memory_property_bit_mask(create_info.ring_buffer_usage),
                .debug_label                            = create_info.debug_label
            }
        )
    }
    , m_algorithm{create_info.size}
{
    // CPU read/write buffers are persistently mapped when persistent buffers are available.
    // Otherwise, use a CPU shadow buffer: CPU_write uploads from it via glBufferSubData,
    // CPU_read downloads into it (via map/read) once the producing frame completes. The
    // acquired span points into this shadow, so it must be allocated up front and never
    // reallocated (the pointer must stay stable for the range's lifetime).
    if (
        !m_device.get_info().use_persistent_buffers &&
        (
            (create_info.ring_buffer_usage == Ring_buffer_usage::CPU_write) ||
            (create_info.ring_buffer_usage == Ring_buffer_usage::CPU_read)
        )
    ) {
        m_cpu_buffer.resize(create_info.size);
    }
}

Ring_buffer::~Ring_buffer() noexcept
{
    ERHE_PROFILE_FUNCTION();

    m_algorithm.assert_invariants();
}

void Ring_buffer::get_size_available_for_write(
    std::size_t  required_alignment,
    std::size_t& out_alignment_byte_count_without_wrap,
    std::size_t& out_available_byte_count_without_wrap,
    std::size_t& out_available_byte_count_with_wrap
) const
{
    m_algorithm.get_size_available_for_write(
        required_alignment,
        out_alignment_byte_count_without_wrap,
        out_available_byte_count_without_wrap,
        out_available_byte_count_with_wrap
    );
}

auto Ring_buffer::acquire(std::size_t required_alignment, Ring_buffer_usage ring_buffer_usage, std::size_t byte_count) -> Ring_buffer_range
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(ring_buffer_usage == m_ring_buffer_usage); // TODO Cleanup this API

    const std::optional<erhe::circular_ring_buffer::Circular_ring_buffer_algorithm::Allocation> opt_allocation =
        m_algorithm.acquire(required_alignment, byte_count);
    if (!opt_allocation.has_value()) {
        return {};
    }
    const erhe::circular_ring_buffer::Circular_ring_buffer_algorithm::Allocation& allocation = opt_allocation.value();

    const bool make_span = (ring_buffer_usage != Ring_buffer_usage::GPU_access);
    std::span<std::byte> span{};
    if (make_span) {
        if (m_device.get_info().use_persistent_buffers) {
            //// TODO size_t buffer_mapped_span_byte_count = m_buffer->get_map().size_bytes();
            //// ERHE_VERIFY(allocation.byte_offset + allocation.byte_count <= buffer_mapped_span_byte_count);
            span = m_buffer->get_map().subspan(allocation.byte_offset, allocation.byte_count);
        } else {
            span = std::span<std::byte>{m_cpu_buffer.data() + allocation.byte_offset, allocation.byte_count};

            // Non-persistent CPU_read: the GPU writes this region during the
            // current frame; schedule a download into the shadow once that
            // frame's fence signals (see frame_completed()). The consumer's
            // completion handler -- which runs after frame_completed for the
            // same frame -- then reads the populated shadow via this span.
            if (ring_buffer_usage == Ring_buffer_usage::CPU_read) {
                m_pending_reads.push_back(
                    Pending_read{
                        .frame       = m_device.get_frame_index(),
                        .byte_offset = allocation.byte_offset,
                        .byte_count  = allocation.byte_count
                    }
                );
            }
        }
    }

    return Ring_buffer_range{
        *this,
        ring_buffer_usage,
        span,
        allocation.wrap_count,
        allocation.byte_offset
    };
}

auto Ring_buffer::match(const Ring_buffer_usage ring_buffer_usage) const -> bool
{
    return m_ring_buffer_usage == ring_buffer_usage; // TODO some usages might be compatible with each other?
}

void Ring_buffer::flush(const std::size_t byte_offset, const std::size_t byte_count)
{
    ERHE_PROFILE_FUNCTION();

    m_algorithm.assert_invariants();

    ERHE_VERIFY(m_ring_buffer_usage == Ring_buffer_usage::CPU_write);
    if (!m_device.get_info().use_persistent_buffers) {
        // Non-persistent: upload from CPU shadow buffer to GPU
        if (byte_count > 0) {
            m_buffer->upload_sub_data(byte_offset, byte_count, m_cpu_buffer.data() + byte_offset);
        }
    } else {
        // Persistent: flush the mapped range
        m_buffer->flush_bytes(byte_offset, byte_count);
    }
}

void Ring_buffer::close(std::size_t byte_offset, std::size_t byte_write_count)
{
    ERHE_PROFILE_FUNCTION();

    m_algorithm.assert_invariants();

    if (m_ring_buffer_usage == Ring_buffer_usage::CPU_write) {
        if (!m_device.get_info().use_persistent_buffers) {
            // Non-persistent: upload from CPU shadow buffer to GPU
            if (byte_write_count > 0) {
                m_buffer->upload_sub_data(byte_offset, byte_write_count, m_cpu_buffer.data() + byte_offset);
            }
        } else {
            m_buffer->end_write(byte_offset, byte_write_count); // flush and unmap
        }
    }
}

void Ring_buffer::make_sync_entry(std::size_t wrap_count, std::size_t byte_offset, std::size_t byte_count)
{
    ERHE_PROFILE_FUNCTION();

    m_algorithm.make_sync_entry(m_device.get_frame_index(), wrap_count, byte_offset, byte_count);
}

auto Ring_buffer::get_buffer() -> Buffer*
{
    return m_buffer.get();
}

void Ring_buffer::frame_completed(const uint64_t completed_frame)
{
    ERHE_PROFILE_FUNCTION();

    // Non-persistent CPU_read: now that this frame's GPU work (including the
    // texture/buffer copies that filled the read buffer) has completed, map
    // the written regions and copy them into the shadow the acquired spans
    // point into. This runs before the device's per-frame completion handlers
    // (see Device_impl::frame_completed), so the consumer reads valid data.
    if (!m_pending_reads.empty()) {
        for (const Pending_read& pending_read : m_pending_reads) {
            if (pending_read.frame != completed_frame) {
                continue;
            }
            const std::span<std::byte> mapped = m_buffer->map_bytes(pending_read.byte_offset, pending_read.byte_count);
            std::memcpy(m_cpu_buffer.data() + pending_read.byte_offset, mapped.data(), pending_read.byte_count);
            m_buffer->unmap();
        }
        std::erase_if(
            m_pending_reads,
            [completed_frame](const Pending_read& pending_read) {
                return pending_read.frame == completed_frame;
            }
        );
    }

    m_algorithm.frame_completed(completed_frame);
}

} // namespace erhe::graphics
