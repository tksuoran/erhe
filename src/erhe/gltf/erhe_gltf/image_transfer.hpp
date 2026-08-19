#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_dataformat/dataformat.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace erhe::graphics {
    class Command_buffer;
    class Image_info;
    class Texture;
}

namespace erhe::gltf {

// How an Image_transfer gets its staging space and its command buffer
// (doc/async-asset-loading-plan.md 2.6).
enum class Image_transfer_mode : unsigned int
{
    // The original mode: a private fixed-size staging ring and a transfer
    // command buffer of its own, flushed with submit-and-wait. Required by
    // callers that have NO frame loop running, so no frame completion will
    // ever arrive to reclaim staging: src/example (parses in its
    // constructor, before the loop starts) and src/rendering_test. The
    // editor's OpenXR controller visualization keeps it too - a small GLB
    // already in memory, loaded from a live tick.
    blocking_drain = 0,

    // Copies are recorded into the caller's command buffer - the frame's -
    // and staged from the device's shared ring buffer, whose ranges are
    // reclaimed by frame completion. Nothing blocks: an upload that does not
    // fit this frame's byte budget reports budget_exhausted and the caller
    // resumes next frame. Requires a live frame loop.
    frame_recording = 1
};

// Outcome of one frame-recording upload.
enum class Image_upload_result : unsigned int
{
    recorded         = 0, // copies are in the command buffer
    budget_exhausted = 1  // no byte budget left this frame; retry next frame
};

// Stages glTF texture uploads and records the per-level copies to the
// texture. In blocking_drain mode staging is a private fixed-size ring and
// the copies go to a transfer command buffer of its own; when that ring
// fills, flush() submits it, blocks on its fence and reclaims the staging
// space - so a scene load makes forward progress with bounded staging memory
// even when the application renders no frames while loading. The destructor
// performs a final flush; the uploaded textures are safe to sample by
// anything submitted afterwards on the same queue.
//
// In frame_recording mode neither the private ring nor the private command
// buffer exists: see upload_into_frame.
class Image_transfer
{
public:
    explicit Image_transfer(erhe::graphics::Device& graphics_device);
    Image_transfer(erhe::graphics::Device& graphics_device, Image_transfer_mode mode);
    ~Image_transfer();
    Image_transfer(const Image_transfer&)  = delete;
    void operator=(const Image_transfer&)  = delete;
    Image_transfer(Image_transfer&&)       = delete;
    void operator=(Image_transfer&&)       = delete;

    [[nodiscard]] auto get_mode() const -> Image_transfer_mode { return m_mode; }

    // Stage the pixel data (level_count levels contiguous, largest-first,
    // tightly packed) and record the per-level copies to the texture.
    // blocking_drain mode only.
    void upload(
        const erhe::graphics::Image_info&   image_info,
        std::span<const std::uint8_t>       pixels,
        erhe::graphics::Texture&            texture,
        bool                                generate_mipmap
    );

    // Same, but staged from the device ring and recorded into
    // command_buffer. `remaining_budget_bytes` is the caller's per-frame GPU
    // upload allowance: nothing is recorded and budget_exhausted is returned
    // when it is already zero, otherwise it is decremented by the staged byte
    // count. An image larger than the whole remaining budget still goes in
    // one piece and takes the budget to zero - the budget bounds how much a
    // frame STARTS, not how far a single image may overshoot; per-level
    // slicing of one oversize image is a later step.
    //
    // The budget is not merely pacing: Device::allocate_ring_buffer_entry
    // never refuses - it spills a new ring buffer sized to the request - so
    // this budget is the ONLY thing bounding staging memory in this mode.
    // A caller that passes an unbounded budget will grow the device's
    // staging rings by the size of the scene.
    // frame_recording mode only.
    [[nodiscard]] auto upload_into_frame(
        erhe::graphics::Command_buffer&     command_buffer,
        const erhe::graphics::Image_info&   image_info,
        std::span<const std::uint8_t>       pixels,
        erhe::graphics::Texture&            texture,
        bool                                generate_mipmap,
        std::size_t&                        remaining_budget_bytes
    ) -> Image_upload_result;

    // Submit the pending copies, wait for the GPU, reclaim staging space.
    // No-op in frame_recording mode - the frame owns the command buffer.
    void flush();

private:
    void record_copies(
        erhe::graphics::Command_buffer&   command_buffer,
        const erhe::graphics::Image_info& image_info,
        const erhe::graphics::Buffer&     source_buffer,
        std::size_t                       source_offset,
        erhe::graphics::Texture&          texture,
        bool                              generate_mipmap
    );
    [[nodiscard]] auto get_transfer_command_buffer() -> erhe::graphics::Command_buffer&;

    erhe::graphics::Device&                    m_graphics_device;
    Image_transfer_mode                        m_mode{Image_transfer_mode::blocking_drain};
    std::optional<erhe::graphics::Ring_buffer> m_staging; // blocking_drain only
    erhe::graphics::Command_buffer*            m_command_buffer{nullptr};
};

} // namespace erhe::gltf
