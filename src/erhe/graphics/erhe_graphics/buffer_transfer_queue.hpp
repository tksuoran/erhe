#pragma once

#include "erhe_profile/profile.hpp"
#include "erhe_utility/memory_budget.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>
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
            , data         {std::move(data)}
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
    // the bytes it enqueued (doc/editor/async_asset_loading_design.md 2.5).
    //
    // Host-visible staging comes from Device::allocate_upload_staging with a
    // cap of staging_capacity_byte_count (Command_buffer::try_upload_to_buffer):
    // when the staging has no room the drain stops early, the unrecorded
    // entries stay queued in order, and they go first next drain.
    auto flush_budgeted(Command_buffer& command_buffer, std::size_t max_byte_count, std::size_t staging_capacity_byte_count) -> std::size_t;

    auto enqueue(const Buffer* buffer, std::size_t offset, std::vector<uint8_t>&& data) -> Ticket;

    // Highest ticket enqueued so far. A builder snapshots this after
    // enqueuing everything one mesh needs; the mesh may be drawn once
    // get_watermark() has reached that value.
    [[nodiscard]] auto get_last_ticket() const -> Ticket;

    // Highest ticket whose upload has been recorded into a command buffer.
    [[nodiscard]] auto get_watermark() const -> Ticket;

    [[nodiscard]] auto get_queued_byte_count() const -> std::size_t;

    // Keeps `lease` until every upload enqueued so far has been recorded
    // (the watermark reaches the last ticket issued at this call), then
    // drops it. A producer that enqueued uploads paid by a load memory
    // budget hands its lease in after its last enqueue, so the budget counts
    // the bytes while they wait in this queue (doc/editor/async_asset_loading.md
    // "Load memory budget"). Dropped at once when nothing is outstanding.
    void hold_until_drained(erhe::utility::Memory_budget_lease&& lease);

private:
    // m_drain_mutex serializes the drainers (flush, flush_budgeted) against
    // each other; m_mutex guards the queue state and is what enqueue takes.
    // flush_budgeted records its batch with only m_drain_mutex held, so
    // workers enqueuing concurrently (the parallel import build) never wait
    // for the upload recording. Lock order: m_drain_mutex, then m_mutex.
    ERHE_PROFILE_MUTEX(std::mutex, m_drain_mutex);
    mutable ERHE_PROFILE_MUTEX(std::mutex, m_mutex);
    std::vector<Transfer_entry>    m_queued;
    // flush_budgeted's batch, moved out of m_queued under m_mutex and
    // recorded outside it. Cleared after each drain, capacity kept.
    std::vector<Transfer_entry>    m_drain_batch;
    // Entries a budgeted drain selected but could not record because the
    // upload staging was full. They are older than everything in m_queued
    // and are drained first, in order. Guarded by m_mutex, like m_queued.
    std::vector<Transfer_entry>    m_retry;
    // Entries before this index have been recorded and their data released.
    // A partial drain advances it instead of erasing from the front, so the
    // Transfer_entry stays move-construct-only (no move ASSIGNMENT, which
    // erase-shifting would need) and a big queue is not shifted per drain.
    std::size_t                    m_drain_position{0};
    class Held_lease
    {
    public:
        Ticket                             ticket{0};
        erhe::utility::Memory_budget_lease lease;
    };
    // Leases waiting for the watermark, in ticket order. Guarded by m_mutex.
    std::deque<Held_lease>         m_held_leases;
    // Moves the held leases the watermark has passed into `out`; m_mutex held.
    void take_drained_leases_locked(std::vector<erhe::utility::Memory_budget_lease>& out);
    Device&                        m_device;
    Ticket                         m_next_ticket{1};
    Ticket                         m_watermark  {0};
};


} // namespace erhe::graphics
