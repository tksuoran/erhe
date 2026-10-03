#include "erhe_graphics/ring_buffer_pool.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_profile/profile.hpp"

#include <algorithm>
#include <array>

namespace erhe::graphics {

Ring_buffer_pool::Ring_buffer_pool(Device& device, const std::size_t min_buffer_size)
    : m_device         {device}
    , m_min_buffer_size{min_buffer_size}
{
}

Ring_buffer_pool::~Ring_buffer_pool() noexcept = default;

auto Ring_buffer_pool::allocate(
    const std::size_t       required_alignment,
    const Ring_buffer_usage usage,
    const std::size_t       byte_count
) -> Ring_buffer_range
{
    ERHE_PROFILE_FUNCTION();

    std::size_t alignment_byte_count_without_wrap{0};
    std::size_t available_byte_count_without_wrap{0};
    std::size_t available_byte_count_with_wrap{0};

    // Pass 1: Do we have buffer that can be used without a wrap?
    for (const std::unique_ptr<Ring_buffer>& ring_buffer : m_ring_buffers) {
        if (!ring_buffer->match(usage)) {
            continue;
        }
        ring_buffer->get_size_available_for_write(
            required_alignment,
            alignment_byte_count_without_wrap,
            available_byte_count_without_wrap,
            available_byte_count_with_wrap
        );
        if (available_byte_count_without_wrap >= byte_count) {
            return ring_buffer->acquire(required_alignment, usage, byte_count);
        }
    }

    // Pass 2: Do we have buffer that can be used with a wrap?
    for (const std::unique_ptr<Ring_buffer>& ring_buffer : m_ring_buffers) {
        if (!ring_buffer->match(usage)) {
            continue;
        }
        ring_buffer->get_size_available_for_write(
            required_alignment,
            alignment_byte_count_without_wrap,
            available_byte_count_without_wrap,
            available_byte_count_with_wrap
        );
        if (available_byte_count_with_wrap >= byte_count) {
            return ring_buffer->acquire(required_alignment, usage, byte_count);
        }
    }

    // No existing usable buffer found, create new buffer. The first buffer
    // of a usage class gets 4x headroom; SPILL buffers (created because
    // every existing buffer was momentarily full of in-flight ranges) are
    // sized to the request only - the 4x multiplier on spills is what
    // turned a load's 16 MiB staging acquisitions into a pile of 64 MiB
    // buffers. Spills are reclaimed once idle (see frame_completed).
    std::size_t existing_count      = 0;
    std::size_t existing_byte_count = 0;
    std::size_t total_byte_count    = 0;
    for (const std::unique_ptr<Ring_buffer>& ring_buffer : m_ring_buffers) {
        total_byte_count += ring_buffer->get_capacity_byte_count();
        if (ring_buffer->match(usage)) {
            ++existing_count;
            existing_byte_count += ring_buffer->get_capacity_byte_count();
        }
    }
    const bool is_spill = (existing_count > 0);
    const Ring_buffer_create_info create_info{
        .size              = std::max(m_min_buffer_size, is_spill ? byte_count : 4 * byte_count),
        .ring_buffer_usage = usage,
        .debug_label       = "Ring_buffer"
    };
    m_ring_buffers.push_back(std::make_unique<Ring_buffer>(m_device, create_info));
    log_buffer->info(
        "Ring buffer pool: created {} byte buffer (usage {}, request {} bytes); now {} buffer(s), {} bytes total",
        create_info.size,
        static_cast<unsigned int>(usage),
        byte_count,
        m_ring_buffers.size(),
        total_byte_count + create_info.size
    );
    if (existing_count >= warn_buffer_count) {
        log_buffer->warn(
            "Ring buffer pool: {} buffers ({} bytes) for one usage class - "
            "acquisitions are outpacing frame completion (missing upload flushes?)",
            existing_count + 1,
            existing_byte_count + create_info.size
        );
    }
    return m_ring_buffers.back()->acquire(required_alignment, usage, byte_count);
}

void Ring_buffer_pool::frame_completed(const uint64_t completed_frame)
{
    for (const std::unique_ptr<Ring_buffer>& ring_buffer : m_ring_buffers) {
        ring_buffer->frame_completed(completed_frame);
    }

    // Reclaim ring buffers that have sat idle (no in-flight GPU work, all
    // space free) for a while, keeping one warm buffer per usage class so
    // steady state does not thrash. Without this the pool only ever grows:
    // a load spike that momentarily filled every buffer leaves its spill
    // buffers alive for the process lifetime. Destruction is deferred-safe
    // (the backend buffer frees its memory through a completion handler).
    std::array<bool, 4> warm_kept{false, false, false, false};
    std::size_t reclaimed_byte_count = 0;
    std::size_t reclaimed_count      = 0;
    const auto keep = [&](const std::unique_ptr<Ring_buffer>& ring_buffer) -> bool {
        if (!ring_buffer->is_idle()) {
            return true;
        }
        const std::size_t usage_index = static_cast<std::size_t>(ring_buffer->get_ring_buffer_usage());
        if ((usage_index < warm_kept.size()) && !warm_kept[usage_index]) {
            warm_kept[usage_index] = true;
            return true;
        }
        if (completed_frame < ring_buffer->get_last_used_frame() + idle_frame_threshold) {
            return true;
        }
        reclaimed_byte_count += ring_buffer->get_capacity_byte_count();
        ++reclaimed_count;
        return false;
    };
    const auto i = std::remove_if(
        m_ring_buffers.begin(),
        m_ring_buffers.end(),
        [&keep](const std::unique_ptr<Ring_buffer>& ring_buffer) { return !keep(ring_buffer); }
    );
    if (i != m_ring_buffers.end()) {
        m_ring_buffers.erase(i, m_ring_buffers.end());
        log_buffer->info(
            "Ring buffer pool: reclaimed {} idle buffer(s), {} bytes; {} buffer(s) remain",
            reclaimed_count, reclaimed_byte_count, m_ring_buffers.size()
        );
    }
}

void Ring_buffer_pool::clear()
{
    m_ring_buffers.clear();
}

auto Ring_buffer_pool::get_buffer_count() const -> std::size_t
{
    return m_ring_buffers.size();
}

auto Ring_buffer_pool::get_total_byte_count() const -> std::size_t
{
    std::size_t total = 0;
    for (const std::unique_ptr<Ring_buffer>& ring_buffer : m_ring_buffers) {
        total += ring_buffer->get_capacity_byte_count();
    }
    return total;
}

} // namespace erhe::graphics
