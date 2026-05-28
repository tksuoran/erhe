#include "erhe_circular_ring_buffer/circular_ring_buffer_algorithm.hpp"

#include "erhe_utility/align.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace erhe::circular_ring_buffer {

Circular_ring_buffer_algorithm::Circular_ring_buffer_algorithm(std::size_t capacity_byte_count)
    : m_capacity_byte_count{capacity_byte_count}
    , m_write_position     {0}
    , m_write_wrap_count   {1}
    , m_read_wrap_count    {0}
    , m_read_offset        {capacity_byte_count}
{
}

void Circular_ring_buffer_algorithm::get_size_available_for_write(
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
    out_alignment_byte_count_without_wrap = aligned_write_position - m_write_position;

    if (m_write_wrap_count == m_read_wrap_count + 1) {
        ERHE_VERIFY(m_read_offset >= m_write_position);
        out_available_byte_count_without_wrap = m_read_offset - m_write_position;
        out_available_byte_count_with_wrap = 0;
        if (out_available_byte_count_without_wrap > out_alignment_byte_count_without_wrap) {
            out_available_byte_count_without_wrap -= out_alignment_byte_count_without_wrap;
        } else {
            out_alignment_byte_count_without_wrap = 0;
            out_available_byte_count_without_wrap = 0;
        }
        return;
    } else if (m_read_wrap_count == m_write_wrap_count) {
        ERHE_VERIFY(m_write_position >= m_read_offset);
        out_available_byte_count_without_wrap = m_capacity_byte_count - m_write_position;
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

auto Circular_ring_buffer_algorithm::acquire(
    std::size_t required_alignment,
    std::size_t byte_count
) -> std::optional<Allocation>
{
    assert_invariants();

    std::size_t alignment_byte_count_without_wrap{0};
    std::size_t available_byte_count_without_wrap{0};
    std::size_t available_byte_count_with_wrap   {0};

    get_size_available_for_write(
        required_alignment,
        alignment_byte_count_without_wrap,
        available_byte_count_without_wrap,
        available_byte_count_with_wrap
    );

    if (byte_count == 0) {
        byte_count = std::max(available_byte_count_without_wrap, available_byte_count_with_wrap);
    }

    const bool wrap = (byte_count > available_byte_count_without_wrap);
    if (wrap && (byte_count > available_byte_count_with_wrap)) {
        return std::nullopt;
    }

    if (wrap) {
        wrap_write();
    } else {
        m_write_position += alignment_byte_count_without_wrap;
    }

    assert_invariants();

    Allocation allocation{
        .wrap_count  = m_write_wrap_count,
        .byte_offset = m_write_position,
        .byte_count  = byte_count
    };
    m_write_position += byte_count;
    assert_invariants();
    return allocation;
}

void Circular_ring_buffer_algorithm::make_sync_entry(
    std::uint64_t frame_index,
    std::size_t   wrap_count,
    std::size_t   byte_offset,
    std::size_t   byte_count
)
{
    assert_invariants();

    for (Sync_entry& entry : m_sync_entries) {
        if (
            (entry.waiting_for_frame == frame_index) &&
            (entry.wrap_count == wrap_count)
        ) {
            if (byte_offset + byte_count > entry.byte_offset + entry.byte_count) {
                entry.byte_offset = byte_offset;
                entry.byte_count  = byte_count;
            }
            return;
        }
    }

    m_sync_entries.push_back(
        Sync_entry{
            .waiting_for_frame = frame_index,
            .wrap_count        = wrap_count,
            .byte_offset       = byte_offset,
            .byte_count        = byte_count
        }
    );
}

void Circular_ring_buffer_algorithm::frame_completed(std::uint64_t completed_frame)
{
    assert_invariants();

    for (Sync_entry& entry : m_sync_entries) {
        if (entry.waiting_for_frame == completed_frame) {
            if (
                (entry.wrap_count > m_read_wrap_count) ||
                (
                    (entry.wrap_count == m_read_wrap_count) &&
                    (entry.byte_offset + entry.byte_count > m_read_offset)
                )
            ) {
                const std::size_t new_read_wrap_count = entry.wrap_count;
                const std::size_t new_read_offset     = entry.byte_offset + entry.byte_count;
                if (m_write_wrap_count == new_read_wrap_count + 1) {
                    ERHE_VERIFY(new_read_offset >= m_write_position);
                } else if (new_read_wrap_count == m_write_wrap_count) {
                    ERHE_VERIFY(m_write_position >= new_read_offset);
                } else {
                    ERHE_FATAL("buffer issue");
                }

                m_read_wrap_count = new_read_wrap_count;
                m_read_offset     = new_read_offset;
                assert_invariants();
            }
        }
    }

    auto i = std::remove_if(
        m_sync_entries.begin(),
        m_sync_entries.end(),
        [completed_frame](Sync_entry& entry) { return entry.waiting_for_frame == completed_frame; }
    );
    if (i != m_sync_entries.end()) {
        m_sync_entries.erase(i, m_sync_entries.end());
    }
}

auto Circular_ring_buffer_algorithm::get_capacity_byte_count() const -> std::size_t
{
    return m_capacity_byte_count;
}

auto Circular_ring_buffer_algorithm::get_write_position() const -> std::size_t
{
    return m_write_position;
}

auto Circular_ring_buffer_algorithm::get_write_wrap_count() const -> std::size_t
{
    return m_write_wrap_count;
}

auto Circular_ring_buffer_algorithm::get_read_offset() const -> std::size_t
{
    return m_read_offset;
}

auto Circular_ring_buffer_algorithm::get_read_wrap_count() const -> std::size_t
{
    return m_read_wrap_count;
}

auto Circular_ring_buffer_algorithm::get_sync_entry_count() const -> std::size_t
{
    return m_sync_entries.size();
}

void Circular_ring_buffer_algorithm::assert_invariants() const
{
    ERHE_VERIFY(m_write_position <= m_capacity_byte_count);

    if (m_write_wrap_count == m_read_wrap_count + 1) {
        ERHE_VERIFY(m_read_offset >= m_write_position);
    } else if (m_read_wrap_count == m_write_wrap_count) {
        ERHE_VERIFY(m_write_position >= m_read_offset);
    } else {
        ERHE_FATAL("buffer issue");
    }
}

void Circular_ring_buffer_algorithm::wrap_write()
{
    assert_invariants();

    ++m_write_wrap_count;
    m_write_position = 0;

    assert_invariants();
}

} // namespace erhe::circular_ring_buffer
