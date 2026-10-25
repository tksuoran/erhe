#include "erhe_graphics/ring_buffer_pool.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <array>

namespace erhe::graphics {

Ring_buffer_pool::Ring_buffer_pool(Device& device, const std::size_t min_buffer_size)
    : m_device         {device}
    , m_min_buffer_size{min_buffer_size}
{
}

Ring_buffer_pool::~Ring_buffer_pool() noexcept = default;

auto Ring_buffer_pool::get_calling_role_index() -> std::size_t
{
    return erhe::utility::Thread_roles::is_render_role() ? 1 : 0;
}

auto Ring_buffer_pool::allocate(
    const std::size_t       required_alignment,
    const Ring_buffer_usage usage,
    const std::size_t       byte_count
) -> Ring_buffer_range
{
    ERHE_PROFILE_FUNCTION();

    const std::lock_guard<std::mutex> lock{m_mutex};
    std::vector<std::unique_ptr<Ring_buffer>>& ring_buffers = m_ring_buffers[get_calling_role_index()];

    // Pass 1: Do we have buffer that can be used without a wrap?
    for (const std::unique_ptr<Ring_buffer>& ring_buffer : ring_buffers) {
        if (!ring_buffer->match(usage)) {
            continue;
        }
        Ring_buffer_range range = ring_buffer->try_acquire(required_alignment, usage, byte_count, Ring_buffer_wrap::not_allowed);
        if (range.get_buffer() != nullptr) {
            return range;
        }
    }

    // Pass 2: Do we have buffer that can be used with a wrap?
    for (const std::unique_ptr<Ring_buffer>& ring_buffer : ring_buffers) {
        if (!ring_buffer->match(usage)) {
            continue;
        }
        Ring_buffer_range range = ring_buffer->try_acquire(required_alignment, usage, byte_count, Ring_buffer_wrap::allowed);
        if (range.get_buffer() != nullptr) {
            return range;
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
    std::size_t total_buffer_count  = 0;
    for (const std::vector<std::unique_ptr<Ring_buffer>>& role_ring_buffers : m_ring_buffers) {
        total_buffer_count += role_ring_buffers.size();
        for (const std::unique_ptr<Ring_buffer>& ring_buffer : role_ring_buffers) {
            total_byte_count += ring_buffer->get_capacity_byte_count();
        }
    }
    for (const std::unique_ptr<Ring_buffer>& ring_buffer : ring_buffers) {
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
    ring_buffers.push_back(std::make_unique<Ring_buffer>(m_device, create_info));
    log_buffer->info(
        "Ring buffer pool: created {} byte buffer (usage {}, role {}, request {} bytes); now {} buffer(s), {} bytes total",
        create_info.size,
        static_cast<unsigned int>(usage),
        (get_calling_role_index() == 0) ? "main" : "render",
        byte_count,
        total_buffer_count + 1,
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
    return ring_buffers.back()->acquire(required_alignment, usage, byte_count);
}

auto Ring_buffer_pool::get_upload_staging_bytes_in_use_locked() const -> std::size_t
{
    std::size_t bytes = 0;
    if (m_upload_staging) {
        bytes += m_upload_staging->get_bytes_in_use();
    }
    if (m_upload_staging_oversize) {
        bytes += m_upload_staging_oversize->get_bytes_in_use();
    }
    return bytes;
}

auto Ring_buffer_pool::allocate_upload_staging(
    const std::size_t required_alignment,
    const std::size_t byte_count,
    const std::size_t capacity_byte_count
) -> Ring_buffer_range
{
    ERHE_PROFILE_FUNCTION();
    // The staging ring retires its space in acquisition order, which holds
    // only for one role's frames (see the class comment).
    ERHE_VERIFY(!erhe::utility::Thread_roles::is_render_role());
    ERHE_VERIFY(byte_count > 0);
    ERHE_VERIFY(capacity_byte_count > 0);

    const std::lock_guard<std::mutex> lock{m_mutex};

    // Fairness: a request that found no room is remembered by its size, and
    // smaller requests are refused until one at least that large is served
    // (its owner retries it every frame), so a stream of small uploads
    // cannot keep the ring from ever draining for a large one. The mark
    // expires when nothing that large retried for a while (a cancelled
    // load).
    if (
        (m_upload_staging_waiting_byte_count > 0) &&
        (m_last_completed_frame > (m_upload_staging_waiting_frame + upload_staging_wait_expiry_frames))
    ) {
        m_upload_staging_waiting_byte_count = 0;
    }
    const auto refuse = [this, byte_count](const Upload_staging_wait wait) -> Ring_buffer_range {
        ++m_upload_staging_usage.refused_count;
        if ((wait == Upload_staging_wait::remember) && (byte_count >= m_upload_staging_waiting_byte_count)) {
            m_upload_staging_waiting_byte_count = byte_count;
            m_upload_staging_waiting_frame      = m_last_completed_frame;
        }
        return Ring_buffer_range{};
    };
    const auto serve = [this, byte_count](Ring_buffer_range&& range) -> Ring_buffer_range {
        if (byte_count >= m_upload_staging_waiting_byte_count) {
            m_upload_staging_waiting_byte_count = 0;
        }
        m_upload_staging_usage.high_water_bytes = std::max(m_upload_staging_usage.high_water_bytes, get_upload_staging_bytes_in_use_locked());
        return std::move(range);
    };
    if (byte_count < m_upload_staging_waiting_byte_count) {
        return refuse(Upload_staging_wait::forget);
    }

    // A capacity change replaces the ring, but never under in-flight ranges:
    // until the old ring is idle every request waits.
    if (m_upload_staging && (m_upload_staging->get_capacity_byte_count() != capacity_byte_count)) {
        if (!m_upload_staging->is_idle()) {
            return refuse(Upload_staging_wait::forget);
        }
        log_buffer->info(
            "Upload staging: capacity changed from {} to {} bytes",
            m_upload_staging->get_capacity_byte_count(), capacity_byte_count
        );
        m_upload_staging.reset();
    }
    if (!m_upload_staging) {
        m_upload_staging = std::make_unique<Ring_buffer>(
            m_device,
            Ring_buffer_create_info{
                .size              = capacity_byte_count,
                .ring_buffer_usage = Ring_buffer_usage::CPU_write,
                .debug_label       = "Upload staging"
            }
        );
        m_upload_staging_usage.capacity_byte_count = capacity_byte_count;
        log_buffer->info("Upload staging: created {} byte staging ring", capacity_byte_count);
    }
    // An idle ring goes back to offset 0: a caught-up but mid-buffer write
    // position would refuse a contiguous request the empty ring can hold.
    // Idle means no range is open and no sync entry is live, so nothing
    // references the old positions.
    if (m_upload_staging->is_idle()) {
        m_upload_staging->complete_all_syncs();
    }

    // Larger than the whole cap: a one-shot buffer, but only once nothing
    // else is staged, so the cap is exceeded by at most this one request.
    if ((byte_count + required_alignment) > capacity_byte_count) {
        if (!m_upload_staging->is_idle() || m_upload_staging_oversize) {
            return refuse(Upload_staging_wait::remember);
        }
        m_upload_staging_oversize = std::make_unique<Ring_buffer>(
            m_device,
            Ring_buffer_create_info{
                .size              = byte_count + required_alignment,
                .ring_buffer_usage = Ring_buffer_usage::CPU_write,
                .debug_label       = "Upload staging (oversize)"
            }
        );
        ++m_upload_staging_usage.oversize_count;
        log_buffer->info(
            "Upload staging: {} byte request exceeds the {} byte cap; using a one-shot staging buffer",
            byte_count, capacity_byte_count
        );
        return serve(m_upload_staging_oversize->acquire(required_alignment, Ring_buffer_usage::CPU_write, byte_count));
    }

    // While a one-shot buffer is live the ring takes nothing, so the staged
    // bytes stay within max(cap, largest single request).
    if (m_upload_staging_oversize) {
        return refuse(Upload_staging_wait::forget);
    }
    Ring_buffer_range range = m_upload_staging->try_acquire(required_alignment, Ring_buffer_usage::CPU_write, byte_count, Ring_buffer_wrap::not_allowed);
    if (range.get_buffer() == nullptr) {
        range = m_upload_staging->try_acquire(required_alignment, Ring_buffer_usage::CPU_write, byte_count, Ring_buffer_wrap::allowed);
    }
    if (range.get_buffer() == nullptr) {
        return refuse(Upload_staging_wait::remember);
    }
    return serve(std::move(range));
}

auto Ring_buffer_pool::get_upload_staging_usage() const -> Upload_staging_usage
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    Upload_staging_usage usage = m_upload_staging_usage;
    usage.bytes_in_use = get_upload_staging_bytes_in_use_locked();
    return usage;
}

void Ring_buffer_pool::frame_completed(const uint64_t completed_frame)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_last_completed_frame = std::max(m_last_completed_frame, completed_frame);
    for (std::vector<std::unique_ptr<Ring_buffer>>& role_ring_buffers : m_ring_buffers) {
        frame_completed_in_role(role_ring_buffers, completed_frame);
    }
    if (m_upload_staging) {
        m_upload_staging->frame_completed(completed_frame);
    }
    if (m_upload_staging_oversize) {
        m_upload_staging_oversize->frame_completed(completed_frame);
        // One-shot: destroyed as soon as its range retired (deferred-safe,
        // like the idle reclaim below).
        if (m_upload_staging_oversize->is_idle()) {
            m_upload_staging_oversize.reset();
        }
    }
}

void Ring_buffer_pool::frame_completed_in_role(std::vector<std::unique_ptr<Ring_buffer>>& ring_buffers, const uint64_t completed_frame)
{
    for (const std::unique_ptr<Ring_buffer>& ring_buffer : ring_buffers) {
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
        ring_buffers.begin(),
        ring_buffers.end(),
        [&keep](const std::unique_ptr<Ring_buffer>& ring_buffer) { return !keep(ring_buffer); }
    );
    if (i != ring_buffers.end()) {
        ring_buffers.erase(i, ring_buffers.end());
        log_buffer->info(
            "Ring buffer pool: reclaimed {} idle buffer(s), {} bytes; {} buffer(s) remain in that role",
            reclaimed_count, reclaimed_byte_count, ring_buffers.size()
        );
    }
}

void Ring_buffer_pool::clear()
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    for (std::vector<std::unique_ptr<Ring_buffer>>& role_ring_buffers : m_ring_buffers) {
        role_ring_buffers.clear();
    }
    m_upload_staging.reset();
    m_upload_staging_oversize.reset();
}

auto Ring_buffer_pool::get_buffer_count() const -> std::size_t
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    std::size_t count = 0;
    for (const std::vector<std::unique_ptr<Ring_buffer>>& role_ring_buffers : m_ring_buffers) {
        count += role_ring_buffers.size();
    }
    return count;
}

auto Ring_buffer_pool::get_total_byte_count() const -> std::size_t
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    std::size_t total = 0;
    for (const std::vector<std::unique_ptr<Ring_buffer>>& role_ring_buffers : m_ring_buffers) {
        for (const std::unique_ptr<Ring_buffer>& ring_buffer : role_ring_buffers) {
            total += ring_buffer->get_capacity_byte_count();
        }
    }
    return total;
}

auto Ring_buffer_pool::get_usage() const -> Ring_buffer_pool_usage
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    Ring_buffer_pool_usage usage{};
    for (const std::vector<std::unique_ptr<Ring_buffer>>& role_ring_buffers : m_ring_buffers) {
        for (const std::unique_ptr<Ring_buffer>& ring_buffer : role_ring_buffers) {
            ++usage.buffer_count;
            if (!ring_buffer->is_idle()) {
                ++usage.busy_buffer_count;
            }
            usage.bytes_in_use   += ring_buffer->get_bytes_in_use();
            usage.capacity_bytes += ring_buffer->get_capacity_byte_count();
        }
    }
    return usage;
}

} // namespace erhe::graphics
