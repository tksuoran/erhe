#pragma once

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/enums.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace erhe::graphics {
    class Buffer;
    class Color_blend_state;
    class Command_buffer;
    class Device;
    class Rasterization_state;
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
    // (transfer_src so read_texture_rgba8 can copy it out). When
    // include_transfer_dst is true the texture additionally gets transfer_dst
    // usage, required for upload_to_texture / clear_texture targets (without it
    // the image lacks VK_IMAGE_USAGE_TRANSFER_DST_BIT and validation rejects the
    // transfer). The fresh texture is pre-transitioned to transfer_src_optimal
    // either way.
    [[nodiscard]] auto make_color_target(
        int                      width,
        int                      height,
        erhe::dataformat::Format format               = erhe::dataformat::Format::format_8_vec4_unorm,
        bool                     include_transfer_dst = false
    ) -> std::shared_ptr<erhe::graphics::Texture>;

    // Host-visible mappable buffer with the requested usage.
    [[nodiscard]] auto make_host_buffer(std::size_t byte_count, erhe::graphics::Buffer_usage usage, const char* debug_label)
        -> std::shared_ptr<erhe::graphics::Buffer>;

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

    // Copy a texture's level 0 (color aspect) to a mappable buffer and return
    // the raw bytes, tightly packed at width * bytes_per_texel bytes per row.
    // bytes_per_texel must match the texture's pixel format size. Same framing
    // contract as read_texture_rgba8 (texture must be in transfer_src_optimal).
    // The byte-level workhorse the typed color readbacks build on; also used
    // directly when the caller wants to reinterpret texels as signed bytes
    // (snorm8) etc.
    [[nodiscard]] auto read_texture_color_bytes(const erhe::graphics::Texture& texture, std::size_t bytes_per_texel)
        -> std::vector<std::byte>;

    // Copy a format_32_vec4_float color texture's level 0 to a mappable buffer
    // and return the texels as floats (4 floats per texel, tightly packed).
    // Same framing contract as read_texture_rgba8.
    [[nodiscard]] auto read_texture_rgba32f(const erhe::graphics::Texture& texture)
        -> std::vector<float>;

    // Copy a depth texture's level 0 (depth aspect) to a mappable buffer and
    // return one 32-bit float depth value per texel, tightly packed at width
    // floats per row. Requires a format_d32_sfloat texture left in
    // transfer_src_optimal (the render pass must set usage_after / layout_after
    // = transfer_src / transfer_src_optimal so the depth aspect is readable).
    // The Blit_command_encoder texture->buffer overload selects the depth aspect
    // for depth formats.
    [[nodiscard]] auto read_texture_depth32f(const erhe::graphics::Texture& texture)
        -> std::vector<float>;

    // Render a fullscreen (oversized) triangle of a constant color into a fresh
    // RGBA8 color target with the given rasterization + blend state, then return
    // the read-back pixels. The color is GLSL injected as the fragment output,
    // e.g. "vec4(0.0, 1.0, 0.0, 1.0)". Shared by the pipeline-state coverage
    // tests (cull, color write mask, blend variants, ...).
    [[nodiscard]] auto draw_fullscreen_triangle(
        const char*                                fragment_color_glsl,
        const erhe::graphics::Rasterization_state& rasterization,
        const erhe::graphics::Color_blend_state&   color_blend,
        std::array<double, 4>                      clear_value,
        int                                        width  = 16,
        int                                        height = 16
    ) -> std::vector<uint8_t>;
};

} // namespace erhe::graphics::test
