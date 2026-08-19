#pragma once

#include "erhe_profile/profile.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace erhe::graphics {

class Buffer;
class Command_buffer;
class Device;

class Buffer_transfer_queue final
{
public:
    explicit Buffer_transfer_queue(Device& device);
    ~Buffer_transfer_queue() noexcept;
    Buffer_transfer_queue(Buffer_transfer_queue&) = delete;
    auto operator=(Buffer_transfer_queue&) -> Buffer_transfer_queue& = delete;

    class Transfer_entry
    {
    public:
        Transfer_entry(const Buffer* target, const std::size_t target_offset, std::vector<uint8_t>&& data, const std::uint64_t ticket)
            : target       {target}
            , target_offset{target_offset}
            , data         {data}
            , ticket       {ticket}
        {
        }

        Transfer_entry(Transfer_entry&) = delete;
        void operator=(Transfer_entry&) = delete;

        Transfer_entry(Transfer_entry&& other) noexcept
            : target       {other.target}
            , target_offset{other.target_offset}
            , data         {std::move(other.data)}
            , ticket       {other.ticket}
        {
        }

        auto operator=(Transfer_entry&& other) = delete;

        const Buffer*        target       {nullptr};
        std::size_t          target_offset{0};
        std::vector<uint8_t> data;
        std::uint64_t        ticket       {0};
    };

    // Monotonically increasing per-enqueue id. 0 is "no transfer": a
    // watermark of 0 means nothing has been drained, and a dependency of 0
    // means nothing is depended on, so `dependency <= watermark` holds
    // trivially. See get_watermark().
    using Ticket = std::uint64_t;

    // Drain the queue by recording every pending upload into the given
    // command buffer. The cb must be in recording state; the caller is
    // responsible for submitting it. On return the watermark is the highest
    // ticket ever enqueued, so "enqueued implies recorded this flush" holds
    // - the guarantee every full-drain caller relies on.
    void flush(Command_buffer& command_buffer);

    // Partial drain: record pending uploads in FIFO order until
    // max_byte_count bytes have been recorded, then stop. Returns the bytes
    // actually recorded. FIFO order is what makes the watermark meaningful -
    // every ticket at or below the watermark has been recorded.
    //
    // This breaks the "enqueued implies uploaded by end of frame" invariant
    // by design, which is why it exists on its own queue: a consumer of a
    // budget-drained queue MUST gate on get_watermark() before drawing from
    // the bytes it enqueued (doc/async-asset-loading-plan.md 2.5).
    auto flush_budgeted(Command_buffer& command_buffer, std::size_t max_byte_count) -> std::size_t;

    auto enqueue(const Buffer* buffer, std::size_t offset, std::vector<uint8_t>&& data) -> Ticket;

    // Highest ticket enqueued so far. A builder snapshots this after
    // enqueuing everything one mesh needs; the mesh may be drawn once
    // get_watermark() has reached that value.
    [[nodiscard]] auto get_last_ticket() const -> Ticket;

    // Highest ticket whose upload has been recorded into a command buffer.
    [[nodiscard]] auto get_watermark() const -> Ticket;

    [[nodiscard]] auto get_queued_byte_count() const -> std::size_t;

private:
    mutable ERHE_PROFILE_MUTEX(std::mutex, m_mutex);
    std::vector<Transfer_entry>    m_queued;
    // Entries before this index have been recorded and their data released.
    // A partial drain advances it instead of erasing from the front, so the
    // Transfer_entry stays move-construct-only (no move ASSIGNMENT, which
    // erase-shifting would need) and a big queue is not shifted per drain.
    std::size_t                    m_drain_position{0};
    Device&                        m_device;
    Ticket                         m_next_ticket{1};
    Ticket                         m_watermark  {0};
};


} // namespace erhe::graphics
