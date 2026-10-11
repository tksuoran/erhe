#include "gpu_test_fixture.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>

namespace erhe::graphics::test {

// Two device frames open at once (doc/erhe/graphics.md "Frame lifecycle"):
// frame B is started and its slot acquired while frame A is still open, the
// way a pipelining application prepares frame N+1 on one thread while
// another records frame N. On one thread (no Thread_roles set) the frame
// index reports the newest acquired frame, so the command buffer obtained
// after B's acquire belongs to B and comes from the other frame-in-flight
// slot. A ends first, then B, strictly in order.
TEST_F(Gpu_test, two_frames_open)
{
    erhe::graphics::Device& graphics_device = device();
    const uint64_t frames_in_flight = static_cast<uint64_t>(graphics_device.get_number_of_frames_in_flight());
    ASSERT_GE(frames_in_flight, 2u);

    // Frame A
    const uint64_t frame_a = graphics_device.wait_for_frame_start();
    ASSERT_TRUE(graphics_device.acquire_frame_slot(frame_a));
    EXPECT_EQ(graphics_device.get_frame_index(), frame_a);
    erhe::graphics::Command_buffer& command_buffer_a = graphics_device.get_command_buffer(0);
    EXPECT_EQ(command_buffer_a.get_frame_id(), frame_a);
    command_buffer_a.begin();
    command_buffer_a.end();

    // Frame B, while A is still open
    const uint64_t frame_b = graphics_device.wait_for_frame_start();
    EXPECT_EQ(frame_b, frame_a + 1);
    ASSERT_TRUE(graphics_device.acquire_frame_slot(frame_b));
    EXPECT_EQ(graphics_device.get_frame_index(), frame_b);
    erhe::graphics::Command_buffer& command_buffer_b = graphics_device.get_command_buffer(0);
    EXPECT_EQ(command_buffer_b.get_frame_id(), frame_b);
    EXPECT_NE(command_buffer_a.get_frame_id() % frames_in_flight, command_buffer_b.get_frame_id() % frames_in_flight);
    command_buffer_b.begin();
    command_buffer_b.end();

    // A's command buffer is still valid: acquiring B reset only B's slot.
    erhe::graphics::Command_buffer* command_buffers_a[] = { &command_buffer_a };
    graphics_device.submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{command_buffers_a});
    ASSERT_TRUE(graphics_device.end_frame(frame_a));

    erhe::graphics::Command_buffer* command_buffers_b[] = { &command_buffer_b };
    graphics_device.submit_command_buffers(std::span<erhe::graphics::Command_buffer* const>{command_buffers_b});
    ASSERT_TRUE(graphics_device.end_frame(frame_b));

    graphics_device.wait_idle();

    // Both frames ended: the next frame to start follows B.
    EXPECT_EQ(graphics_device.get_frame_index(), frame_b + 1);
}

} // namespace erhe::graphics::test
