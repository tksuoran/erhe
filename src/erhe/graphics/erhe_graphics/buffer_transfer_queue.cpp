// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_profile/profile.hpp"

#include <fmt/format.h>

namespace erhe::graphics {

Buffer_transfer_queue::Buffer_transfer_queue(Device& device)
    : m_device{device}
{
}

Buffer_transfer_queue::~Buffer_transfer_queue() noexcept
{
    // flush(); TODO causes GL errors in shutdown, investigate
}

auto Buffer_transfer_queue::enqueue(const Buffer* buffer, const std::size_t offset, std::vector<uint8_t>&& data) -> Ticket
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    SPDLOG_LOGGER_TRACE(
        log_buffer,
        "queued buffer {} transfer offset = {} size = {}",
        buffer.gl_name(),
        offset,
        data.size()
    );
    const Ticket ticket = m_next_ticket++;
    m_queued.emplace_back(buffer, offset, std::move(data), ticket);
    return ticket;
}

void Buffer_transfer_queue::flush(Command_buffer& command_buffer)
{
    ERHE_PROFILE_FUNCTION();

    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> drain_lock{m_drain_mutex};
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    // Entries a budgeted drain could not stage are the oldest.
    for (const Transfer_entry& entry : m_retry) {
        command_buffer.upload_to_buffer(*entry.target, entry.target_offset, entry.data.data(), entry.data.size());
    }
    m_retry.clear();
    for (std::size_t i = m_drain_position, end = m_queued.size(); i < end; ++i) {
        const Transfer_entry& entry = m_queued[i];
        SPDLOG_LOGGER_TRACE(
            log_buffer,
            "buffer upload {} {} transfer offset = {} size = {}",
            gl::c_str(entry.target.target()),
            entry.target->gl_name(),
            entry.target_offset,
            entry.data.size()
        );
        command_buffer.upload_to_buffer(*entry.target, entry.target_offset, entry.data.data(), entry.data.size());
    }
    m_queued.clear();
    m_drain_position = 0;
    // Everything ever enqueued has now been recorded. Taking the watermark
    // from the ticket counter rather than from the last drained entry is what
    // keeps a full drain of an EMPTY queue advancing the watermark too.
    m_watermark = m_next_ticket - 1;
}

auto Buffer_transfer_queue::flush_budgeted(
    Command_buffer&   command_buffer,
    const std::size_t max_byte_count,
    const std::size_t staging_capacity_byte_count
) -> std::size_t
{
    ERHE_PROFILE_FUNCTION();

    // One drainer at a time: the batch below is selected, recorded and
    // committed to the watermark as one drain.
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> drain_lock{m_drain_mutex};

    // Select the batch, moving each entry out of the queue, then record it
    // with m_mutex released so concurrent enqueues do not wait for the
    // staging copies. m_retry (older entries an earlier drain could not
    // stage) goes first.
    //
    // The fit is checked BEFORE taking an entry, so an entry larger than the
    // whole budget still goes through as the first entry of a drain
    // (otherwise a mesh bigger than the per-frame budget would never upload
    // at all) but never as a later one.
    std::size_t selected_byte_count = 0;
    const auto fits = [&selected_byte_count, max_byte_count](const Transfer_entry& entry) -> bool {
        return (selected_byte_count < max_byte_count) &&
            ((selected_byte_count == 0) || ((selected_byte_count + entry.data.size()) <= max_byte_count));
    };
    std::size_t retry_taken       = 0;
    bool        selected_to_empty = false;
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
        while ((retry_taken < m_retry.size()) && fits(m_retry[retry_taken])) {
            selected_byte_count += m_retry[retry_taken].data.size();
            m_drain_batch.emplace_back(std::move(m_retry[retry_taken]));
            ++retry_taken;
        }
        while ((retry_taken == m_retry.size()) && (m_drain_position < m_queued.size()) && fits(m_queued[m_drain_position])) {
            selected_byte_count += m_queued[m_drain_position].data.size();
            m_drain_batch.emplace_back(std::move(m_queued[m_drain_position]));
            ++m_drain_position;
        }
        if (m_drain_position == m_queued.size()) {
            m_queued.clear();
            m_drain_position  = 0;
            selected_to_empty = true;
        }
    }

    // Record in FIFO order; the first entry the staging cannot take stops
    // the drain, so every recorded ticket is older than every unrecorded one.
    std::size_t recorded_count      = 0;
    std::size_t recorded_byte_count = 0;
    Ticket      recorded_watermark  = 0;
    for (const Transfer_entry& entry : m_drain_batch) {
        const bool recorded = command_buffer.try_upload_to_buffer(
            *entry.target, entry.target_offset, entry.data.data(), entry.data.size(), staging_capacity_byte_count
        );
        if (!recorded) {
            break;
        }
        ++recorded_count;
        recorded_byte_count += entry.data.size();
        recorded_watermark   = entry.ticket;
    }

    // The unrecorded batch entries, then the retry entries not selected
    // this time, form the new retry list (still in ticket order).
    const bool all_recorded = (recorded_count == m_drain_batch.size());
    std::vector<Transfer_entry> retry;
    if (!all_recorded || (retry_taken > 0)) {
        retry.reserve((m_drain_batch.size() - recorded_count) + (m_retry.size() - retry_taken));
        for (std::size_t i = recorded_count, end = m_drain_batch.size(); i < end; ++i) {
            retry.emplace_back(std::move(m_drain_batch[i]));
        }
        for (std::size_t i = retry_taken, end = m_retry.size(); i < end; ++i) {
            retry.emplace_back(std::move(m_retry[i]));
        }
    }

    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
        if (!all_recorded || (retry_taken > 0)) {
            m_retry.swap(retry);
        }
        Ticket drained_watermark = recorded_watermark;
        if (selected_to_empty && all_recorded && m_retry.empty()) {
            // Drained to empty: nothing enqueued before the selection is
            // outstanding, so the watermark may advance to the last ticket
            // issued then, exactly as a full flush would leave it. Entries
            // enqueued while the batch recorded are in m_queued and newer.
            drained_watermark = m_queued.empty() ? (m_next_ticket - 1) : (m_queued.front().ticket - 1);
        }
        if (drained_watermark > m_watermark) {
            m_watermark = drained_watermark;
        }
    }

    // Recorded; release the staging copies outside m_mutex.
    m_drain_batch.clear();
    return recorded_byte_count;
}

auto Buffer_transfer_queue::get_last_ticket() const -> Ticket
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    return m_next_ticket - 1;
}

auto Buffer_transfer_queue::get_watermark() const -> Ticket
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    return m_watermark;
}

auto Buffer_transfer_queue::get_queued_byte_count() const -> std::size_t
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};
    std::size_t byte_count = 0;
    for (std::size_t i = m_drain_position, end = m_queued.size(); i < end; ++i) {
        byte_count += m_queued[i].data.size();
    }
    for (const Transfer_entry& entry : m_retry) {
        byte_count += entry.data.size();
    }
    return byte_count;
}

} // namespace erhe::graphics
