#include "erhe_frame_pacing/frame_time_recorder.hpp"

#include <gtest/gtest.h>

namespace {

using erhe::frame_pacing::Frame_time_record;
using erhe::frame_pacing::Frame_time_recorder;

// The editor opens a frame's CPU slot at the start of M1, so the main
// thread's wait for the previous frame's render phase lies inside the slot.
// That wait is involuntary and must not count as CPU service time
// (doc/frame_pacing/inputs.md section 3.2).
TEST(Frame_time_recorder, cpu_service_time_excludes_join_wait)
{
    Frame_time_recorder recorder{8};
    recorder.begin_frame(5, 10.0);
    recorder.set(5, &Frame_time_record::join_wait_begin, 10.002, &Frame_time_record::join_wait_end, 10.007);
    recorder.set(5, &Frame_time_record::cpu_slot_end, 10.010);

    Frame_time_record record{};
    ASSERT_TRUE(recorder.get(5, record));
    EXPECT_NEAR(record.cpu_slot_span(),    0.010, 1e-9);
    EXPECT_NEAR(record.cpu_service_time(), 0.005, 1e-9);
}

// Waits are subtracted only where they overlap the slot; the join wait
// sums with the other involuntary waits.
TEST(Frame_time_recorder, cpu_service_time_subtracts_all_waits)
{
    Frame_time_recorder recorder{8};
    recorder.begin_frame(1, 1.0);
    recorder.set(1, &Frame_time_record::pacer_wait_begin,   0.999, &Frame_time_record::pacer_wait_end,   1.001); // 1 ms inside the slot
    recorder.set(1, &Frame_time_record::join_wait_begin,    1.002, &Frame_time_record::join_wait_end,    1.004);
    recorder.set(1, &Frame_time_record::present_wait_begin, 1.004, &Frame_time_record::present_wait_end, 1.005);
    recorder.add(1, &Frame_time_record::fence_wait_duration, 0.001);
    recorder.set(1, &Frame_time_record::cpu_slot_end, 1.010);

    Frame_time_record record{};
    ASSERT_TRUE(recorder.get(1, record));
    EXPECT_NEAR(record.cpu_service_time(), 0.010 - 0.001 - 0.002 - 0.001 - 0.001, 1e-9);
}

// The chain latency excludes the involuntary waits between the release and
// the submit (claim C28): the FR5 clamp, the join, the acquire and the fence
// waits, each only after the release; the holdback after the submit is not
// one of them.
TEST(Frame_time_recorder, release_wait_time_sums_waits_after_release)
{
    Frame_time_recorder recorder{8};
    recorder.begin_frame(2, 2.0);
    const double release = 2.001;
    recorder.set(2, &Frame_time_record::pacer_wait_begin,   2.000, &Frame_time_record::pacer_wait_end,   release);
    recorder.set(2, &Frame_time_record::present_wait_begin, 2.0015, &Frame_time_record::present_wait_end, 2.0075); // 6 ms
    recorder.set(2, &Frame_time_record::join_wait_begin,    2.0005, &Frame_time_record::join_wait_end,    2.0015); // 0.5 ms after the release
    recorder.set(2, &Frame_time_record::acquire_begin,      2.008, &Frame_time_record::acquire_end,      2.009);   // 1 ms
    recorder.add(2, &Frame_time_record::fence_wait_duration, 0.0002);
    recorder.set(2, &Frame_time_record::present_holdback_begin, 2.010, &Frame_time_record::present_holdback_end, 2.012);

    Frame_time_record record{};
    ASSERT_TRUE(recorder.get(2, record));
    EXPECT_NEAR(record.release_wait_time(release), 0.006 + 0.0005 + 0.001 + 0.0002, 1e-9);
}

TEST(Frame_time_recorder, writes_to_unopened_or_evicted_records_are_ignored)
{
    Frame_time_recorder recorder{4};
    recorder.begin_frame(2, 1.0);
    recorder.begin_frame(6, 2.0); // same ring slot as 2: evicts it
    recorder.set(2, &Frame_time_record::cpu_slot_end, 3.0);

    Frame_time_record record{};
    EXPECT_FALSE(recorder.get(2, record));
    EXPECT_FALSE(recorder.contains(2));
    ASSERT_TRUE(recorder.get(6, record));
    EXPECT_EQ(record.cpu_slot_end, 0.0);
    EXPECT_EQ(recorder.get_latest_frame_id(), 6);
}

} // namespace
