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

    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

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

auto Buffer_transfer_queue::flush_budgeted(Command_buffer& command_buffer, const std::size_t max_byte_count) -> std::size_t
{
    ERHE_PROFILE_FUNCTION();

    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    std::size_t recorded_byte_count = 0;
    while (m_drain_position < m_queued.size()) {
        Transfer_entry& entry = m_queued[m_drain_position];
        // Checked BEFORE recording, so an entry larger than the whole budget
        // still goes through as the first entry of a drain (otherwise a mesh
        // bigger than the per-frame budget would never upload at all) but
        // never as a later one.
        if ((recorded_byte_count > 0) && ((recorded_byte_count + entry.data.size()) > max_byte_count)) {
            break;
        }
        command_buffer.upload_to_buffer(*entry.target, entry.target_offset, entry.data.data(), entry.data.size());
        recorded_byte_count += entry.data.size();
        m_watermark = entry.ticket; // FIFO: every ticket at or below this is recorded
        entry.data  = std::vector<uint8_t>{}; // recorded; release the staging copy now
        ++m_drain_position;
        if (recorded_byte_count >= max_byte_count) {
            break;
        }
    }
    if (m_drain_position == m_queued.size()) {
        // Drained to empty: nothing enqueued is outstanding, so the watermark
        // may advance all the way, exactly as a full flush would leave it.
        m_queued.clear();
        m_drain_position = 0;
        m_watermark      = m_next_ticket - 1;
    }
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
    return byte_count;
}

} // namespace erhe::graphics
