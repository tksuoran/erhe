// Unit tests for Circular_ring_buffer_algorithm.
//
// These tests exercise the pure bookkeeping logic that was extracted from
// erhe::graphics::Ring_buffer into its own library so it can be reasoned
// about without a graphics device. The invariants being defended are:
//
//   - The write position never exceeds capacity.
//   - Exactly one of:
//       write_wrap_count == read_wrap_count + 1, with read_offset >= write_position
//       write_wrap_count == read_wrap_count    , with write_position >= read_offset
//     holds at all times (initial state matches the first branch).
//   - acquire() returns nullopt iff the request cannot be satisfied in
//     either the current segment or the wrap-around segment.
//   - make_sync_entry() merges by (frame_index, wrap_count) and only grows
//     the recorded range.
//   - frame_completed() advances the read position past every matching
//     entry's range and erases those entries.

#include <gtest/gtest.h>

#include "erhe_circular_ring_buffer/circular_ring_buffer_algorithm.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <iostream>
#include <optional>
#include <random>

// Defined in main.cpp; populated from ERHE_STRESS_* environment variables
// before any tests run. The long-running smoke test below switches its
// random-mix phase into a wall-clock loop when seconds > 0.
extern std::uint64_t g_stress_seconds;
extern std::uint64_t g_stress_seed;
extern bool          g_stress_seed_was_set;

using erhe::circular_ring_buffer::Circular_ring_buffer_algorithm;
using Allocation = Circular_ring_buffer_algorithm::Allocation;

TEST(CircularRingBufferAlgorithm, initial_state)
{
    Circular_ring_buffer_algorithm algorithm{64};
    EXPECT_EQ(algorithm.get_capacity_byte_count(), 64u);
    EXPECT_EQ(algorithm.get_write_position     (),  0u);
    EXPECT_EQ(algorithm.get_write_wrap_count   (),  1u);
    EXPECT_EQ(algorithm.get_read_offset        (), 64u);
    EXPECT_EQ(algorithm.get_read_wrap_count    (),  0u);
    EXPECT_EQ(algorithm.get_sync_entry_count   (),  0u);
    algorithm.assert_invariants();

    std::size_t alignment_padding{0};
    std::size_t available_no_wrap{0};
    std::size_t available_with_wrap{0};
    algorithm.get_size_available_for_write(1, alignment_padding, available_no_wrap, available_with_wrap);
    EXPECT_EQ(alignment_padding,    0u);
    EXPECT_EQ(available_no_wrap,   64u);
    EXPECT_EQ(available_with_wrap,  0u);
}

TEST(CircularRingBufferAlgorithm, acquire_unaligned_advance)
{
    Circular_ring_buffer_algorithm algorithm{64};

    const std::optional<Allocation> opt_allocation = algorithm.acquire(1, 16);
    ASSERT_TRUE(opt_allocation.has_value());
    EXPECT_EQ(opt_allocation->wrap_count,   1u);
    EXPECT_EQ(opt_allocation->byte_offset,  0u);
    EXPECT_EQ(opt_allocation->byte_count,  16u);

    EXPECT_EQ(algorithm.get_write_position  (), 16u);
    EXPECT_EQ(algorithm.get_write_wrap_count(),  1u);
    algorithm.assert_invariants();
}

TEST(CircularRingBufferAlgorithm, acquire_alignment_padding_consumed)
{
    Circular_ring_buffer_algorithm algorithm{64};

    const std::optional<Allocation> first = algorithm.acquire(1, 4);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->byte_offset, 0u);
    EXPECT_EQ(algorithm.get_write_position(), 4u);

    const std::optional<Allocation> aligned = algorithm.acquire(16, 16);
    ASSERT_TRUE(aligned.has_value());
    EXPECT_EQ(aligned->wrap_count,   1u);
    EXPECT_EQ(aligned->byte_offset, 16u); // skipped 12 bytes of padding
    EXPECT_EQ(aligned->byte_count,  16u);
    EXPECT_EQ(algorithm.get_write_position(), 32u);
    algorithm.assert_invariants();
}

TEST(CircularRingBufferAlgorithm, acquire_zero_byte_count_takes_max_segment)
{
    Circular_ring_buffer_algorithm algorithm{64};

    // From the initial state, available_no_wrap = 64, available_with_wrap = 0,
    // so passing byte_count = 0 should claim the entire capacity in one go.
    const std::optional<Allocation> opt_allocation = algorithm.acquire(1, 0);
    ASSERT_TRUE(opt_allocation.has_value());
    EXPECT_EQ(opt_allocation->wrap_count,   1u);
    EXPECT_EQ(opt_allocation->byte_offset,  0u);
    EXPECT_EQ(opt_allocation->byte_count,  64u);
    EXPECT_EQ(algorithm.get_write_position(), 64u);
    algorithm.assert_invariants();
}

TEST(CircularRingBufferAlgorithm, acquire_triggers_wrap_when_no_room_at_end)
{
    Circular_ring_buffer_algorithm algorithm{64};

    // Fill 60 of the 64 bytes, then let the GPU "catch up" so the read
    // pointer is at the same position as the write pointer. From that
    // state, a 10-byte request cannot fit in the 4 bytes left at the tail
    // and must wrap to offset 0.
    const std::optional<Allocation> first = algorithm.acquire(1, 60);
    ASSERT_TRUE(first.has_value());
    algorithm.make_sync_entry(1u, first->wrap_count, first->byte_offset, first->byte_count);
    algorithm.frame_completed(1u);
    EXPECT_EQ(algorithm.get_read_offset    (), 60u);
    EXPECT_EQ(algorithm.get_read_wrap_count(),  1u);

    const std::optional<Allocation> wrapped = algorithm.acquire(1, 10);
    ASSERT_TRUE(wrapped.has_value());
    EXPECT_EQ(wrapped->wrap_count,   2u);
    EXPECT_EQ(wrapped->byte_offset,  0u);
    EXPECT_EQ(wrapped->byte_count,  10u);
    EXPECT_EQ(algorithm.get_write_position  (), 10u);
    EXPECT_EQ(algorithm.get_write_wrap_count(),  2u);
    algorithm.assert_invariants();
}

TEST(CircularRingBufferAlgorithm, acquire_returns_nullopt_when_full)
{
    Circular_ring_buffer_algorithm algorithm{64};

    // Fill the buffer completely without letting any sync entry complete,
    // so the read pointer cannot advance.
    const std::optional<Allocation> first = algorithm.acquire(1, 64);
    ASSERT_TRUE(first.has_value());
    algorithm.make_sync_entry(1u, first->wrap_count, first->byte_offset, first->byte_count);

    const std::size_t write_position_before    = algorithm.get_write_position();
    const std::size_t write_wrap_count_before  = algorithm.get_write_wrap_count();
    const std::size_t sync_entry_count_before  = algorithm.get_sync_entry_count();

    const std::optional<Allocation> denied = algorithm.acquire(1, 1);
    EXPECT_FALSE(denied.has_value());

    // State must be unchanged after a denied acquire.
    EXPECT_EQ(algorithm.get_write_position  (), write_position_before);
    EXPECT_EQ(algorithm.get_write_wrap_count(), write_wrap_count_before);
    EXPECT_EQ(algorithm.get_sync_entry_count(), sync_entry_count_before);
    algorithm.assert_invariants();
}

TEST(CircularRingBufferAlgorithm, make_sync_entry_dedup_same_frame_same_wrap)
{
    Circular_ring_buffer_algorithm algorithm{64};

    algorithm.make_sync_entry(/*frame*/ 10u, /*wrap*/ 1u, /*offset*/ 0u, /*count*/ 10u);
    EXPECT_EQ(algorithm.get_sync_entry_count(), 1u);

    // Larger range: should grow the existing entry, not create a new one.
    algorithm.make_sync_entry(10u, 1u, 0u, 20u);
    EXPECT_EQ(algorithm.get_sync_entry_count(), 1u);

    // Smaller range: matches the existing entry, doesn't grow, and still
    // doesn't append.
    algorithm.make_sync_entry(10u, 1u, 0u, 5u);
    EXPECT_EQ(algorithm.get_sync_entry_count(), 1u);
}

TEST(CircularRingBufferAlgorithm, make_sync_entry_distinct_frames_appended)
{
    Circular_ring_buffer_algorithm algorithm{64};

    algorithm.make_sync_entry(10u, 1u, 0u, 8u);
    algorithm.make_sync_entry(11u, 1u, 8u, 8u);
    EXPECT_EQ(algorithm.get_sync_entry_count(), 2u);

    // Different wrap_count with the same frame is also distinct.
    algorithm.make_sync_entry(11u, 2u, 0u, 4u);
    EXPECT_EQ(algorithm.get_sync_entry_count(), 3u);
}

TEST(CircularRingBufferAlgorithm, frame_completed_advances_read_pointer)
{
    Circular_ring_buffer_algorithm algorithm{64};

    const std::optional<Allocation> opt_allocation = algorithm.acquire(1, 30);
    ASSERT_TRUE(opt_allocation.has_value());
    algorithm.make_sync_entry(/*frame*/ 5u, opt_allocation->wrap_count, opt_allocation->byte_offset, opt_allocation->byte_count);
    EXPECT_EQ(algorithm.get_sync_entry_count(), 1u);

    algorithm.frame_completed(5u);
    EXPECT_EQ(algorithm.get_read_offset    (), 30u);
    EXPECT_EQ(algorithm.get_read_wrap_count(),  1u);
    EXPECT_EQ(algorithm.get_sync_entry_count(), 0u);
    algorithm.assert_invariants();
}

TEST(CircularRingBufferAlgorithm, frame_completed_no_match_is_no_op)
{
    Circular_ring_buffer_algorithm algorithm{64};

    const std::optional<Allocation> opt_allocation = algorithm.acquire(1, 30);
    ASSERT_TRUE(opt_allocation.has_value());
    algorithm.make_sync_entry(5u, opt_allocation->wrap_count, opt_allocation->byte_offset, opt_allocation->byte_count);

    const std::size_t read_offset_before     = algorithm.get_read_offset();
    const std::size_t read_wrap_count_before = algorithm.get_read_wrap_count();
    const std::size_t sync_entry_count_before= algorithm.get_sync_entry_count();

    algorithm.frame_completed(99u);
    EXPECT_EQ(algorithm.get_read_offset    (), read_offset_before);
    EXPECT_EQ(algorithm.get_read_wrap_count(), read_wrap_count_before);
    EXPECT_EQ(algorithm.get_sync_entry_count(), sync_entry_count_before);
    algorithm.assert_invariants();
}

TEST(CircularRingBufferAlgorithm, wrap_cycle_invariant_holds)
{
    Circular_ring_buffer_algorithm algorithm{64};
    algorithm.assert_invariants();

    // Cycle 1: write, register, complete.
    const std::optional<Allocation> a1 = algorithm.acquire(1, 60);
    ASSERT_TRUE(a1.has_value());
    algorithm.assert_invariants();
    algorithm.make_sync_entry(1u, a1->wrap_count, a1->byte_offset, a1->byte_count);
    algorithm.assert_invariants();
    algorithm.frame_completed(1u);
    algorithm.assert_invariants();
    EXPECT_EQ(algorithm.get_write_wrap_count(), 1u);
    EXPECT_EQ(algorithm.get_read_wrap_count (), 1u);

    // Cycle 2: forced wrap, register, complete.
    const std::optional<Allocation> a2 = algorithm.acquire(1, 10);
    ASSERT_TRUE(a2.has_value());
    EXPECT_EQ(a2->wrap_count, 2u);
    algorithm.assert_invariants();
    algorithm.make_sync_entry(2u, a2->wrap_count, a2->byte_offset, a2->byte_count);
    algorithm.assert_invariants();
    algorithm.frame_completed(2u);
    algorithm.assert_invariants();
    EXPECT_EQ(algorithm.get_write_wrap_count(), 2u);
    EXPECT_EQ(algorithm.get_read_wrap_count (), 2u);
}

TEST(CircularRingBufferAlgorithm, acquire_after_partial_read_uses_freed_tail_only)
{
    Circular_ring_buffer_algorithm algorithm{64};

    // Partially fill the buffer. Without any frame_completed call, the
    // read pointer stays at capacity, so we are in the
    // write_wrap_count == read_wrap_count + 1 branch.
    const std::optional<Allocation> opt_allocation = algorithm.acquire(1, 40);
    ASSERT_TRUE(opt_allocation.has_value());

    std::size_t alignment_padding{0};
    std::size_t available_no_wrap{0};
    std::size_t available_with_wrap{0};
    algorithm.get_size_available_for_write(1, alignment_padding, available_no_wrap, available_with_wrap);

    EXPECT_EQ(alignment_padding,     0u);
    EXPECT_EQ(available_no_wrap,    24u); // 64 - 40
    EXPECT_EQ(available_with_wrap,   0u); // GPU still owns the head, no wrap allowed
}

TEST(CircularRingBufferAlgorithm, producer_ahead_of_consumer_accumulates_then_blocks)
{
    // Producer outruns the consumer: multiple frames in flight, each with
    // its own sync entry, until the buffer is full and acquire is denied.
    // Then the consumer catches up one frame at a time and the producer
    // is able to wrap and continue.
    Circular_ring_buffer_algorithm algorithm{64};

    const std::optional<Allocation> a1 = algorithm.acquire(1, 20);
    ASSERT_TRUE(a1.has_value());
    algorithm.make_sync_entry(/*frame*/ 1u, a1->wrap_count, a1->byte_offset, a1->byte_count);
    EXPECT_EQ(algorithm.get_sync_entry_count(), 1u);

    const std::optional<Allocation> a2 = algorithm.acquire(1, 20);
    ASSERT_TRUE(a2.has_value());
    algorithm.make_sync_entry(2u, a2->wrap_count, a2->byte_offset, a2->byte_count);
    EXPECT_EQ(algorithm.get_sync_entry_count(), 2u);

    const std::optional<Allocation> a3 = algorithm.acquire(1, 20);
    ASSERT_TRUE(a3.has_value());
    algorithm.make_sync_entry(3u, a3->wrap_count, a3->byte_offset, a3->byte_count);
    EXPECT_EQ(algorithm.get_sync_entry_count(), 3u);

    const std::optional<Allocation> a4 = algorithm.acquire(1, 4);
    ASSERT_TRUE(a4.has_value());
    algorithm.make_sync_entry(4u, a4->wrap_count, a4->byte_offset, a4->byte_count);
    EXPECT_EQ(algorithm.get_write_position  (), 64u);
    EXPECT_EQ(algorithm.get_sync_entry_count(),  4u);

    // Producer wants more but consumer hasn't completed anything yet.
    EXPECT_FALSE(algorithm.acquire(1, 1).has_value());

    // Consumer completes the oldest frame; that reclaims 20 bytes.
    algorithm.frame_completed(1u);
    EXPECT_EQ(algorithm.get_read_offset    (), 20u);
    EXPECT_EQ(algorithm.get_read_wrap_count(),  1u);
    EXPECT_EQ(algorithm.get_sync_entry_count(), 3u);

    // Producer can now wrap to offset 0 and consume the freed prefix.
    const std::optional<Allocation> a5 = algorithm.acquire(1, 20);
    ASSERT_TRUE(a5.has_value());
    EXPECT_EQ(a5->wrap_count,   2u);
    EXPECT_EQ(a5->byte_offset,  0u);
    EXPECT_EQ(a5->byte_count,  20u);
    algorithm.assert_invariants();
}

TEST(CircularRingBufferAlgorithm, consumer_keeps_up_with_producer_steady_state)
{
    // Producer allocates a small chunk per frame and the consumer
    // immediately completes the same frame, so sync entries never
    // accumulate. Drive enough iterations to wrap several times and
    // confirm bookkeeping stays consistent.
    Circular_ring_buffer_algorithm algorithm{32};

    constexpr std::size_t chunk_size  = 8;
    constexpr std::uint64_t iterations = 10;
    std::size_t expected_write_wrap = 1; // initial value

    for (std::uint64_t frame = 1; frame <= iterations; ++frame) {
        const std::optional<Allocation> opt_allocation = algorithm.acquire(1, chunk_size);
        ASSERT_TRUE(opt_allocation.has_value()) << "frame " << frame;

        // A wrap happens precisely when the previous write_position equalled
        // capacity, i.e. when frame is one past a multiple of capacity/chunk.
        if (frame > 1 && ((frame - 1) % (32 / chunk_size)) == 0) {
            ++expected_write_wrap;
        }
        EXPECT_EQ(algorithm.get_write_wrap_count(), expected_write_wrap) << "frame " << frame;

        algorithm.make_sync_entry(frame, opt_allocation->wrap_count, opt_allocation->byte_offset, opt_allocation->byte_count);
        EXPECT_EQ(algorithm.get_sync_entry_count(), 1u) << "frame " << frame;

        algorithm.frame_completed(frame);
        EXPECT_EQ(algorithm.get_sync_entry_count(), 0u) << "frame " << frame;

        // After completion the consumer has caught up exactly to the producer.
        EXPECT_EQ(algorithm.get_read_offset    (), algorithm.get_write_position  ())
            << "frame " << frame;
        EXPECT_EQ(algorithm.get_read_wrap_count(), algorithm.get_write_wrap_count())
            << "frame " << frame;

        algorithm.assert_invariants();
    }
}

TEST(CircularRingBufferAlgorithm, multiple_allocations_sum_exactly_to_capacity)
{
    // Two equal-sized allocations land on byte 0 and 16, the second
    // ending exactly at capacity. The buffer is then completely full;
    // a 1-byte acquire is denied. After both are reclaimed in one
    // frame, the producer can wrap and refill.
    Circular_ring_buffer_algorithm algorithm{32};

    const std::optional<Allocation> a1 = algorithm.acquire(1, 16);
    ASSERT_TRUE(a1.has_value());
    EXPECT_EQ(a1->byte_offset, 0u);
    EXPECT_EQ(a1->byte_count, 16u);

    const std::optional<Allocation> a2 = algorithm.acquire(1, 16);
    ASSERT_TRUE(a2.has_value());
    EXPECT_EQ(a2->byte_offset, 16u);
    EXPECT_EQ(a2->byte_count, 16u);
    EXPECT_EQ(algorithm.get_write_position(), 32u); // exactly capacity

    algorithm.make_sync_entry(1u, a1->wrap_count, a1->byte_offset, a1->byte_count);
    algorithm.make_sync_entry(1u, a2->wrap_count, a2->byte_offset, a2->byte_count);
    // The two ranges merge into one entry under the same (frame, wrap_count).
    EXPECT_EQ(algorithm.get_sync_entry_count(), 1u);

    EXPECT_FALSE(algorithm.acquire(1, 1).has_value());

    algorithm.frame_completed(1u);
    EXPECT_EQ(algorithm.get_read_offset    (), 32u);
    EXPECT_EQ(algorithm.get_read_wrap_count(),  1u);

    const std::optional<Allocation> a3 = algorithm.acquire(1, 32);
    ASSERT_TRUE(a3.has_value());
    EXPECT_EQ(a3->wrap_count,   2u);
    EXPECT_EQ(a3->byte_offset,  0u);
    EXPECT_EQ(a3->byte_count,  32u);
    algorithm.assert_invariants();
}

TEST(CircularRingBufferAlgorithm, alignment_padding_consumes_tail_and_forces_wrap)
{
    // The tail has exactly enough bytes for the alignment pad but none
    // left over. The algorithm reports both alignment_padding and
    // available_without_wrap as 0, forcing the next acquire to wrap
    // even though the request itself is tiny.
    Circular_ring_buffer_algorithm algorithm{64};

    const std::optional<Allocation> a1 = algorithm.acquire(1, 60);
    ASSERT_TRUE(a1.has_value());
    algorithm.make_sync_entry(1u, a1->wrap_count, a1->byte_offset, a1->byte_count);
    algorithm.frame_completed(1u);
    EXPECT_EQ(algorithm.get_write_position(), 60u);
    EXPECT_EQ(algorithm.get_read_offset   (), 60u);

    std::size_t alignment_padding{0};
    std::size_t available_no_wrap{0};
    std::size_t available_with_wrap{0};
    algorithm.get_size_available_for_write(16, alignment_padding, available_no_wrap, available_with_wrap);

    // 4 bytes of tail are exactly consumed by alignment; nothing
    // remains for the payload at the tail, so the reported segment
    // collapses to 0.
    EXPECT_EQ(alignment_padding,    0u);
    EXPECT_EQ(available_no_wrap,    0u);
    EXPECT_EQ(available_with_wrap, 60u);

    const std::optional<Allocation> a2 = algorithm.acquire(16, 1);
    ASSERT_TRUE(a2.has_value());
    EXPECT_EQ(a2->wrap_count,  2u);
    EXPECT_EQ(a2->byte_offset, 0u);
    EXPECT_EQ(a2->byte_count,  1u);
    EXPECT_EQ(algorithm.get_write_position  (), 1u);
    EXPECT_EQ(algorithm.get_write_wrap_count(), 2u);
    algorithm.assert_invariants();
}

TEST(CircularRingBufferAlgorithm, stress_test_long_running_mixed_workload)
{
    // Long-running smoke test. Each "session" allocates a freshly-sized,
    // non-power-of-two ring buffer, then runs a sequence of distinct
    // workloads on it (steady-state, producer-ahead, burst-drain,
    // alignment churn, tiny / huge / out-of-order / random-mix) for a
    // randomized number of steps, drains it, and a new session begins
    // with a new capacity. The session loop continues until
    // ERHE_STRESS_SECONDS elapses (or a small fixed budget in quick mode
    // for CTest).
    //
    // Beyond the bookkeeping invariants checked after every op, each
    // acquire writes a unique per-byte pattern into a CPU shadow buffer
    // (the "data" vector); the matching frame_completed reads those
    // bytes back and verifies they were not overwritten while the frame
    // was still in flight. An overlap check at acquire time also catches
    // algorithm bugs that would let two intact in-flight ranges share
    // bytes, before any pattern is touched.

    const bool          stress_mode = (g_stress_seconds > 0);
    const std::uint32_t seed        = static_cast<std::uint32_t>(
        g_stress_seed_was_set ? g_stress_seed : 0xC0FFEEu
    );

    std::cout
        << "[circular_ring_buffer smoke] seed=" << seed
        << " duration=" << g_stress_seconds << "s"
        << " mode=" << (stress_mode ? "stress" : "quick")
        << std::endl;

    std::mt19937 rng{seed};

    class In_flight_range
    {
    public:
        std::uint64_t frame_index  {0};
        std::size_t   wrap_count   {0};
        std::size_t   byte_offset  {0};
        std::size_t   byte_count   {0};
        std::uint32_t pattern_base {0};
        bool          expect_intact{true};
    };

    auto byte_at = [](std::uint32_t base, std::size_t i) -> std::uint8_t {
        return static_cast<std::uint8_t>((static_cast<std::size_t>(base) + i) & 0xFFu);
    };

    constexpr std::array<std::size_t, 7> alignments{1u, 2u, 4u, 8u, 16u, 32u, 64u};

    // Global stats accumulated across all sessions.
    std::size_t   total_acquires    = 0;
    std::size_t   total_denied      = 0;
    std::size_t   total_completions = 0;
    std::size_t   total_sessions    = 0;
    std::size_t   max_sync_entries  = 0;
    std::size_t   max_wrap_count    = 0;
    std::size_t   min_capacity_seen = std::numeric_limits<std::size_t>::max();
    std::size_t   max_capacity_seen = 0;
    std::uint64_t total_iterations  = 0;
    std::uint64_t global_frame      = 1;

    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::seconds(g_stress_seconds);
    constexpr std::uint64_t quick_iteration_budget = 8000u;
    constexpr std::uint64_t deadline_check_mask    = 1023u; // check every 1024 ops

    bool budget_exhausted = false;
    auto check_budget = [&]() -> bool {
        if (budget_exhausted) {
            return true;
        }
        if ((total_iterations & deadline_check_mask) != 0u) {
            return false;
        }
        if (stress_mode) {
            if (std::chrono::steady_clock::now() >= deadline) {
                budget_exhausted = true;
            }
        } else {
            if (total_iterations >= quick_iteration_budget) {
                budget_exhausted = true;
            }
        }
        return budget_exhausted;
    };

    std::uniform_int_distribution<std::size_t> capacity_dist     {33u, 1023u};
    std::uniform_int_distribution<std::size_t> session_steps_dist{150u, 1200u};
    std::uniform_int_distribution<std::size_t> workload_steps_dist{20u, 200u};
    std::uniform_int_distribution<int>         workload_dist     {0, 7};

    while (!budget_exhausted) {
        // New session: pick a non-power-of-two capacity.
        std::size_t capacity = capacity_dist(rng);
        if ((capacity & (capacity - 1u)) == 0u) {
            ++capacity;
        }
        const std::size_t session_step_budget = session_steps_dist(rng);

        max_capacity_seen = std::max(max_capacity_seen, capacity);
        min_capacity_seen = std::min(min_capacity_seen, capacity);
        ++total_sessions;

        Circular_ring_buffer_algorithm algorithm{capacity};
        std::vector<std::uint8_t>      data(capacity, 0xCCu);
        std::vector<In_flight_range>   in_flight;
        std::deque<std::uint64_t>      pending_frames;
        std::uint32_t                  pattern_counter = 1;

        // Default allocation sizes scale with the chosen capacity. The
        // huge_allocations workload picks its own larger range.
        const std::size_t default_max_size =
            std::max<std::size_t>(2u, (capacity * 2u) / 3u);
        std::uniform_int_distribution<std::size_t> size_dist{1u, default_max_size};

        auto fill_pattern = [&](std::uint32_t base, std::size_t offset, std::size_t count) {
            for (std::size_t i = 0; i < count; ++i) {
                data[offset + i] = byte_at(base, i);
            }
        };
        auto verify_pattern = [&](std::uint32_t base, std::size_t offset, std::size_t count) -> bool {
            for (std::size_t i = 0; i < count; ++i) {
                if (data[offset + i] != byte_at(base, i)) {
                    return false;
                }
            }
            return true;
        };
        auto record_stats = [&] {
            max_sync_entries = std::max(max_sync_entries, algorithm.get_sync_entry_count());
            max_wrap_count   = std::max(max_wrap_count,   algorithm.get_write_wrap_count());
        };
        // After any read-pointer advance, in-flight ranges that lie
        // entirely behind the new read pointer have been implicitly
        // released (typically by out-of-order frame_completed); the
        // producer is now free to overwrite them, so we must stop
        // expecting their patterns to survive until their own
        // frame_completed call.
        auto update_intact_after_release = [&]() {
            const std::size_t cur_read_offset = algorithm.get_read_offset();
            const std::size_t cur_read_wrap   = algorithm.get_read_wrap_count();
            for (In_flight_range& r : in_flight) {
                if (!r.expect_intact) {
                    continue;
                }
                if (r.wrap_count < cur_read_wrap) {
                    r.expect_intact = false;
                } else if (r.wrap_count == cur_read_wrap &&
                           r.byte_offset + r.byte_count <= cur_read_offset)
                {
                    r.expect_intact = false;
                }
            }
        };

        auto try_acquire_and_sync = [&](std::size_t alignment, std::size_t size, std::uint64_t frame) -> bool {
            ++total_iterations;
            const std::optional<Allocation> opt = algorithm.acquire(alignment, size);
            algorithm.assert_invariants();
            if (!opt.has_value()) {
                ++total_denied;
                record_stats();
                return false;
            }

            const std::size_t new_end = opt->byte_offset + opt->byte_count;
            for (const In_flight_range& r : in_flight) {
                if (!r.expect_intact) {
                    continue;
                }
                const std::size_t r_end = r.byte_offset + r.byte_count;
                const bool overlaps = (opt->byte_offset < r_end) && (r.byte_offset < new_end);
                if (overlaps) {
                    ADD_FAILURE()
                        << "algorithm returned overlapping allocation: capacity="
                        << capacity << " new [" << opt->byte_offset << "," << new_end
                        << ") wrap=" << opt->wrap_count
                        << " overlaps intact in-flight range frame=" << r.frame_index
                        << " [" << r.byte_offset << "," << r_end
                        << ") wrap=" << r.wrap_count;
                    return false;
                }
            }

            const std::uint32_t base = pattern_counter++;
            fill_pattern(base, opt->byte_offset, opt->byte_count);

            algorithm.make_sync_entry(frame, opt->wrap_count, opt->byte_offset, opt->byte_count);
            algorithm.assert_invariants();
            ++total_acquires;

            in_flight.push_back(In_flight_range{
                .frame_index   = frame,
                .wrap_count    = opt->wrap_count,
                .byte_offset   = opt->byte_offset,
                .byte_count    = opt->byte_count,
                .pattern_base  = base,
                .expect_intact = true
            });

            if (pending_frames.empty() || pending_frames.back() != frame) {
                pending_frames.push_back(frame);
            }
            record_stats();
            return true;
        };

        auto complete_frame = [&](std::uint64_t frame) {
            ++total_iterations;
            // The data the producer wrote for this frame must still be
            // present and unaltered. If it isn't, the algorithm let
            // someone else overwrite bytes that were still in flight.
            for (const In_flight_range& r : in_flight) {
                if (r.frame_index != frame) {
                    continue;
                }
                if (!r.expect_intact) {
                    continue;
                }
                if (!verify_pattern(r.pattern_base, r.byte_offset, r.byte_count)) {
                    ADD_FAILURE()
                        << "producer pattern overwritten while frame=" << frame
                        << " still in flight: capacity=" << capacity
                        << " range [" << r.byte_offset << "," << r.byte_offset + r.byte_count
                        << ") wrap=" << r.wrap_count
                        << " base=" << r.pattern_base;
                }
            }
            algorithm.frame_completed(frame);
            algorithm.assert_invariants();
            ++total_completions;
            in_flight.erase(
                std::remove_if(
                    in_flight.begin(),
                    in_flight.end(),
                    [frame](const In_flight_range& r) { return r.frame_index == frame; }
                ),
                in_flight.end()
            );
            update_intact_after_release();
            record_stats();
        };

        // ---- Workloads ----
        // Each workload runs `sub_steps` outer iterations; each outer
        // iteration calls try_acquire_and_sync / complete_frame zero,
        // one, or two times. Workloads share the session's algorithm,
        // in_flight, and pending_frames state, so workload order matters:
        // producer_ahead leaves entries that burst_drain reclaims, etc.

        auto wl_steady_state = [&](std::size_t sub_steps) {
            for (std::size_t s = 0; s < sub_steps; ++s) {
                const std::size_t alignment = alignments[rng() % alignments.size()];
                const std::size_t size      = size_dist(rng);
                if (try_acquire_and_sync(alignment, size, global_frame)) {
                    complete_frame(global_frame);
                }
                ++global_frame;
                if (check_budget()) { return; }
            }
        };

        auto wl_producer_ahead = [&](std::size_t sub_steps) {
            for (std::size_t s = 0; s < sub_steps; ++s) {
                const std::size_t alignment = alignments[rng() % alignments.size()];
                const std::size_t size      = size_dist(rng);
                try_acquire_and_sync(alignment, size, global_frame);
                ++global_frame;
                if (check_budget()) { return; }
            }
        };

        auto wl_burst_drain = [&](std::size_t sub_steps) {
            for (std::size_t s = 0; s < sub_steps; ++s) {
                if (pending_frames.empty()) {
                    return;
                }
                complete_frame(pending_frames.front());
                pending_frames.pop_front();
                if (check_budget()) { return; }
            }
        };

        auto wl_alignment_churn = [&](std::size_t sub_steps) {
            constexpr std::array<std::size_t, 4> heavy_aligns{8u, 16u, 32u, 64u};
            for (std::size_t s = 0; s < sub_steps; ++s) {
                const std::size_t alignment = heavy_aligns[rng() % heavy_aligns.size()];
                const std::size_t size      = size_dist(rng);
                if (try_acquire_and_sync(alignment, size, global_frame)) {
                    if ((rng() & 1u) == 0u) {
                        complete_frame(global_frame);
                    }
                }
                ++global_frame;
                if (check_budget()) { return; }
            }
        };

        auto wl_tiny_allocations = [&](std::size_t sub_steps) {
            std::uniform_int_distribution<std::size_t> tiny_dist{1u, 8u};
            for (std::size_t s = 0; s < sub_steps; ++s) {
                const std::size_t alignment = alignments[rng() % alignments.size()];
                const std::size_t size      = tiny_dist(rng);
                if (try_acquire_and_sync(alignment, size, global_frame)) {
                    if (((rng() & 7u) != 0u) && !pending_frames.empty()) {
                        complete_frame(pending_frames.front());
                        pending_frames.pop_front();
                    }
                }
                ++global_frame;
                if (check_budget()) { return; }
            }
        };

        auto wl_huge_allocations = [&](std::size_t sub_steps) {
            // Bias toward allocations near capacity. Many will be denied;
            // a few succeed when the buffer is fresh or freshly drained.
            std::uniform_int_distribution<std::size_t> huge_dist{
                std::max<std::size_t>(1u, capacity / 2u),
                capacity
            };
            for (std::size_t s = 0; s < sub_steps; ++s) {
                const std::size_t alignment = alignments[rng() % alignments.size()];
                const std::size_t size      = huge_dist(rng);
                try_acquire_and_sync(alignment, size, global_frame);
                if (((rng() & 1u) == 0u) && !pending_frames.empty()) {
                    complete_frame(pending_frames.front());
                    pending_frames.pop_front();
                }
                ++global_frame;
                if (check_budget()) { return; }
            }
        };

        auto wl_out_of_order = [&](std::size_t sub_steps) {
            for (std::size_t s = 0; s < sub_steps; ++s) {
                if (pending_frames.size() >= 3u && ((rng() & 3u) == 0u)) {
                    std::uniform_int_distribution<std::size_t> idx{0u, pending_frames.size() - 1u};
                    const std::size_t i = idx(rng);
                    complete_frame(pending_frames[i]);
                    pending_frames.erase(pending_frames.begin() + static_cast<std::ptrdiff_t>(i));
                } else {
                    const std::size_t alignment = alignments[rng() % alignments.size()];
                    const std::size_t size      = size_dist(rng);
                    try_acquire_and_sync(alignment, size, global_frame);
                    ++global_frame;
                }
                if (check_budget()) { return; }
            }
        };

        auto wl_random_mix = [&](std::size_t sub_steps) {
            std::uniform_int_distribution<int> mode_dist{0, 99};
            for (std::size_t s = 0; s < sub_steps; ++s) {
                const int mode = mode_dist(rng);
                if (mode < 55) {
                    const std::size_t alignment = alignments[rng() % alignments.size()];
                    const std::size_t size      = size_dist(rng);
                    try_acquire_and_sync(alignment, size, global_frame);
                } else if (mode < 70) {
                    ++global_frame;
                } else if (mode < 92) {
                    if (!pending_frames.empty()) {
                        complete_frame(pending_frames.front());
                        pending_frames.pop_front();
                    }
                } else {
                    if (pending_frames.size() > 1) {
                        std::uniform_int_distribution<std::size_t> idx{0u, pending_frames.size() - 1u};
                        const std::size_t i = idx(rng);
                        complete_frame(pending_frames[i]);
                        pending_frames.erase(pending_frames.begin() + static_cast<std::ptrdiff_t>(i));
                    }
                }
                if (check_budget()) { return; }
            }
        };

        // Run a sequence of workloads, each for a randomized sub-step
        // count, until the session's step budget is consumed.
        std::size_t session_steps_done = 0;
        while (session_steps_done < session_step_budget && !budget_exhausted) {
            const std::size_t sub_steps = std::min(
                workload_steps_dist(rng),
                session_step_budget - session_steps_done
            );
            const int workload_id = workload_dist(rng);
            switch (workload_id) {
                case 0: wl_steady_state    (sub_steps); break;
                case 1: wl_producer_ahead  (sub_steps); break;
                case 2: wl_burst_drain     (sub_steps); break;
                case 3: wl_alignment_churn (sub_steps); break;
                case 4: wl_tiny_allocations(sub_steps); break;
                case 5: wl_huge_allocations(sub_steps); break;
                case 6: wl_out_of_order    (sub_steps); break;
                case 7: wl_random_mix      (sub_steps); break;
                default: break;
            }
            session_steps_done += sub_steps;
        }

        // Drain whatever remains so the session ends in a clean state.
        // This verifies every still-intact in-flight range one last time
        // before the buffer is discarded.
        while (!pending_frames.empty()) {
            complete_frame(pending_frames.front());
            pending_frames.pop_front();
        }
        EXPECT_EQ(algorithm.get_sync_entry_count(), 0u);
        EXPECT_TRUE(in_flight.empty()) << "in_flight not drained at session end";
    }

    std::cout
        << "[circular_ring_buffer smoke]"
        << " sessions="    << total_sessions
        << " iterations="  << total_iterations
        << " acquires="    << total_acquires
        << " denied="      << total_denied
        << " completions=" << total_completions
        << " wraps="       << max_wrap_count
        << " peak_sync="   << max_sync_entries
        << " cap_range=["  << min_capacity_seen << "," << max_capacity_seen << "]"
        << std::endl;

    // Sanity: the test should genuinely exercise the buffer even in
    // quick mode. These minimums are well below typical observed counts.
    EXPECT_GT(total_acquires,    100u);
    EXPECT_GT(total_denied,        0u);
    EXPECT_GT(total_completions,  50u);
    EXPECT_GT(total_sessions,      1u);
    EXPECT_GT(max_wrap_count,      0u);
}
