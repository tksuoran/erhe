// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/ring_buffer.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_utility/align.hpp"

//// TODO These are same, no need impl?
//#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
//# include "erhe_graphics/gl/gl_ring_buffer.hpp"
//#endif
//#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
//# include "erhe_graphics/vulkan/vulkan_ring_buffer.hpp"
//#endif

namespace erhe::graphics {

[[nodiscard]] auto get_direction(const Ring_buffer_usage ring_buffer_usage) -> Buffer_direction
{
    switch (ring_buffer_usage) {
        default:
        case Ring_buffer_usage::None      : ERHE_FATAL("Device_impl::allocate_ring_buffer_entry() - bad usage"); return Buffer_direction::cpu_to_gpu;
        case Ring_buffer_usage::CPU_write : return Buffer_direction::cpu_to_gpu;
        case Ring_buffer_usage::CPU_read  : return Buffer_direction::gpu_to_cpu;
        case Ring_buffer_usage::GPU_access: return Buffer_direction::gpu_only;
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
                .capacity_byte_count = create_info.size,
                .usage               = create_info.buffer_usage,
                .direction           = get_direction(create_info.ring_buffer_usage),
                .cache_mode          = Buffer_cache_mode::default_,
                .mapping             = (create_info.ring_buffer_usage != Ring_buffer_usage::GPU_access) ? Buffer_mapping::persistent : Buffer_mapping::not_mappable,
                .coherency           = (create_info.ring_buffer_usage != Ring_buffer_usage::GPU_access) ? Buffer_coherency::on       : Buffer_coherency::off,
                .debug_label         = create_info.debug_label
            }
        )
    }
    , m_read_wrap_count{0}
    , m_read_offset    {m_buffer->get_capacity_byte_count()}
{
    // CPU read/write buffers are persistently mapped
    // GPU only buffers are not mappable
}

Ring_buffer::~Ring_buffer()
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();
}

void Ring_buffer::get_size_available_for_write(
    std::size_t  required_alignment,
    std::size_t& out_alignment_byte_count_without_wrap,
    std::size_t& out_available_byte_count_without_wrap,
    std::size_t& out_available_byte_count_with_wrap
) const
{
    // Initial situation:
    //   +--------------------------+
    //   ^                          ^
    //   w1                         r0

    //  CPU progress:
    //   +------+---------------+---+
    //                          ^   ^
    //                          w1  r0

    //  GPU progress:
    //   +------+---------------+---+
    //          ^               ^    
    //          r1              w1   

    //  CPU progress:
    //   +----+---+---+--------------+-+
    //        ^   ^                     
    //        w2  r1                    
    const std::size_t aligned_write_position = erhe::utility::align_offset_power_of_two(m_write_position, required_alignment);
    ERHE_VERIFY(aligned_write_position >= m_write_position);
    out_alignment_byte_count_without_wrap    = aligned_write_position - m_write_position;

    if (m_write_wrap_count == m_read_wrap_count + 1) {
        ERHE_VERIFY(m_read_offset >= m_write_position);
        out_available_byte_count_without_wrap = m_read_offset - m_write_position;
        out_available_byte_count_with_wrap = 0; // cannot wrap because GPU is still using the buffer from this point
        if (out_available_byte_count_without_wrap > out_alignment_byte_count_without_wrap) {
            out_available_byte_count_without_wrap -= out_alignment_byte_count_without_wrap;
        } else {
            out_alignment_byte_count_without_wrap = 0;
            out_available_byte_count_without_wrap = 0;
        }
        return;
    } else if (m_read_wrap_count == m_write_wrap_count) {
        ERHE_VERIFY(m_write_position >= m_read_offset);
        out_available_byte_count_without_wrap = m_buffer->get_capacity_byte_count() - m_write_position;
        out_available_byte_count_with_wrap = m_read_offset;
        if (out_available_byte_count_without_wrap > out_alignment_byte_count_without_wrap) {
            out_available_byte_count_without_wrap -= out_alignment_byte_count_without_wrap;
        } else {
            out_alignment_byte_count_without_wrap = 0;
            out_available_byte_count_without_wrap = 0;
        }
        return;
    } else {
        ERHE_FATAL("buffer issue");
        out_available_byte_count_without_wrap = 0;
        out_available_byte_count_with_wrap = 0;
        return;
    }
}

auto Ring_buffer::acquire(std::size_t required_alignment, Ring_buffer_usage ring_buffer_usage, std::size_t byte_count) -> Ring_buffer_range
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(ring_buffer_usage == m_ring_buffer_usage); // TODO Cleanup this API

    sanity_check();

    std::size_t alignment_byte_count_without_wrap{0};
    std::size_t available_byte_count_without_wrap{0};
    std::size_t available_byte_count_with_wrap   {0};
    bool wrap{false};

    sanity_check();

    get_size_available_for_write(required_alignment, alignment_byte_count_without_wrap, available_byte_count_without_wrap, available_byte_count_with_wrap);

    if (byte_count == 0) {
        byte_count = std::max(available_byte_count_without_wrap, available_byte_count_with_wrap);
    }

    wrap = (byte_count > available_byte_count_without_wrap);
    if (wrap && (byte_count > available_byte_count_with_wrap)) {
        return {}; // Not enough space currently available
    }

    if (wrap) {
        wrap_write();
    } else {
        m_write_position += alignment_byte_count_without_wrap;
    }

    sanity_check();

    if (!m_device.get_info().use_persistent_buffers) {
        if (ring_buffer_usage == Ring_buffer_usage::CPU_write) {
            m_buffer->begin_write(m_write_position, byte_count); // maps requested range
        }
        Ring_buffer_range result{
            *this,
            ring_buffer_usage,
            (ring_buffer_usage != Ring_buffer_usage::GPU_access)
                ? m_buffer->get_map()
                : std::span<std::byte>{},
            m_write_wrap_count,
            m_write_position
        };
        m_write_position += byte_count;
        sanity_check();
        return result;
    } else {
        //// TODO size_t buffer_mapped_span_byte_count = m_buffer->get_map().size_bytes();
        //// ERHE_VERIFY(m_write_position + byte_count <= buffer_mapped_span_byte_count);
        Ring_buffer_range result{
            *this,
            ring_buffer_usage,
            (ring_buffer_usage != Ring_buffer_usage::GPU_access)
                ? m_buffer->get_map().subspan(m_write_position, byte_count)
                : std::span<std::byte>{},
            m_write_wrap_count,
            m_write_position
        };
        m_write_position += byte_count;
        sanity_check();
        return result;
    }
}

auto Ring_buffer::match(const Ring_buffer_usage ring_buffer_usage) const -> bool
{
    return m_ring_buffer_usage == ring_buffer_usage; // TODO some usages might be compatible with each other?
}

void Ring_buffer::flush(const std::size_t byte_offset, const std::size_t byte_count)
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    ERHE_VERIFY(m_ring_buffer_usage == Ring_buffer_usage::CPU_write);
    if (
        !m_device.get_info().use_persistent_buffers &&
        (m_ring_buffer_usage != Ring_buffer_usage::GPU_access)
    ) {
        m_buffer->flush_bytes(byte_offset, byte_count);
    }
}

void Ring_buffer::close(std::size_t byte_offset, std::size_t byte_write_count)
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    if (m_ring_buffer_usage == Ring_buffer_usage::CPU_write) {
        if (!m_device.get_info().use_persistent_buffers) {
            m_buffer->end_write(byte_offset, byte_write_count); // flush and unmap
        }
    }
    //m_map = {};
}

void Ring_buffer::make_sync_entry(std::size_t wrap_count, std::size_t byte_offset, std::size_t byte_count)
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    // Merge to existing sync
    for (Ring_buffer_sync_entry& entry : m_sync_entries) {
        if (
            (entry.waiting_for_frame == m_device.get_frame_index()) &&
            (entry.wrap_count == wrap_count)
        ) {
            if (byte_offset + byte_count > entry.byte_offset + entry.byte_count) {
                entry.byte_offset = byte_offset;
                entry.byte_count  = byte_count;
            }
            return;
        }
    }

    // Make new sync entry
    m_sync_entries.push_back(
        Ring_buffer_sync_entry{
            .waiting_for_frame = m_device.get_frame_index(),
            .wrap_count        = wrap_count,
            .byte_offset       = byte_offset,
            .byte_count        = byte_count
        }
    );
}

auto Ring_buffer::get_buffer() -> Buffer*
{
    return m_buffer.get();
}

auto Ring_buffer::get_name() const -> const std::string&
{
    return m_name;
}

void Ring_buffer::frame_completed(const uint64_t completed_frame)
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    for (Ring_buffer_sync_entry& entry : m_sync_entries) {
        if (entry.waiting_for_frame == completed_frame) {
            if (
                (entry.wrap_count > m_read_wrap_count) ||
                (
                    (entry.wrap_count == m_read_wrap_count) &&
                    (entry.byte_offset + entry.byte_count > m_read_offset)
                )
            ) {
                size_t new_read_wrap_count = entry.wrap_count;
                size_t new_read_offset     = entry.byte_offset + entry.byte_count;
                if (m_write_wrap_count == new_read_wrap_count + 1) {
                    ERHE_VERIFY(new_read_offset >= m_write_position);
                } else if (new_read_wrap_count == m_write_wrap_count) {
                    ERHE_VERIFY(m_write_position >= new_read_offset);
                } else {
                    ERHE_FATAL("buffer issue");
                }

                m_read_wrap_count = new_read_wrap_count;
                m_read_offset = new_read_offset;
                sanity_check();
            }
        }
    }

    auto i = std::remove_if(
        m_sync_entries.begin(),
        m_sync_entries.end(),
        [completed_frame](Ring_buffer_sync_entry& entry) { return entry.waiting_for_frame == completed_frame; }
    );
    if (i != m_sync_entries.end()) {
        m_sync_entries.erase(i, m_sync_entries.end());
    }
}

void Ring_buffer::sanity_check()
{
    ERHE_VERIFY(m_write_position <= m_buffer->get_capacity_byte_count());

    if (m_write_wrap_count == m_read_wrap_count + 1) {
        ERHE_VERIFY(m_read_offset >= m_write_position);
    } else if (m_read_wrap_count == m_write_wrap_count) {
        ERHE_VERIFY(m_write_position >= m_read_offset);
    } else {
        ERHE_FATAL("buffer issue");
    }
}

void Ring_buffer::wrap_write()
{
    ERHE_PROFILE_FUNCTION();

    sanity_check();

    ++m_write_wrap_count;
    m_write_position = 0;

    sanity_check();
}

} // namespace erhe::graphics
