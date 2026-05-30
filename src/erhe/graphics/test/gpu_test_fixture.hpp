#pragma once

#include "erhe_dataformat/dataformat.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace erhe::graphics {
    class Buffer;
    class Command_buffer;
    class Device;
    class Texture;
}

namespace erhe::graphics::test {

// Per-test fixture over the process-wide headless Vulkan device. Provides the
// minimal frame sequence and offscreen-readback helpers shared by the GPU
// milestones. SetUp clears the runtime validation-message list; TearDown
// waits the device idle and fails the case for any validation message
// emitted during the test.
class Gpu_test : public ::testing::Test
{
protected:
    void SetUp   () override;
    void TearDown() override;

    [[nodiscard]] auto device() -> erhe::graphics::Device&;

    // Drive one offscreen frame: wait_frame -> get_command_buffer(0) ->
    // begin -> record_fn -> end -> submit -> wait_idle -> end_frame. After
    // it returns all GPU work is complete and any mappable readback buffer
    // is safe to read.
    void submit_and_wait(const std::function<void(erhe::graphics::Command_buffer&)>& record_fn);

    // 2D color render target, usage color_attachment | sampled | transfer_src
    // (transfer_src so read_texture_rgba8 can copy it out).
    [[nodiscard]] auto make_color_target(
        int                      width,
        int                      height,
        erhe::dataformat::Format format = erhe::dataformat::Format::format_8_vec4_unorm
    ) -> std::shared_ptr<erhe::graphics::Texture>;

    // Host-visible mappable buffer, usage transfer_dst | storage.
    [[nodiscard]] auto make_readback_buffer(std::size_t byte_count, const char* debug_label)
        -> std::shared_ptr<erhe::graphics::Buffer>;

    // Map a host-visible buffer and copy out the first byte_count bytes (or
    // the whole capacity when byte_count == 0). Assumes the GPU is idle.
    [[nodiscard]] auto read_buffer(erhe::graphics::Buffer& buffer, std::size_t byte_count = 0)
        -> std::vector<std::byte>;

    // Copy an RGBA8 texture's level 0 to a mappable buffer and return the
    // bytes, tightly packed at width*4 bytes per row. Records its own blit
    // frame internally, so call it after the frame that produced the texture
    // contents (and with the texture left in transfer_src_optimal).
    [[nodiscard]] auto read_texture_rgba8(const erhe::graphics::Texture& texture)
        -> std::vector<uint8_t>;
};

} // namespace erhe::graphics::test
