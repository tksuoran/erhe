#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_dataformat/dataformat.hpp"

#include <cstdint>
#include <span>

namespace erhe::graphics {
    class Command_buffer;
    class Image_info;
    class Texture;
}

namespace erhe::gltf {

// Stages glTF texture uploads through a private fixed-size ring buffer and
// records the copies into its own transfer command buffer. When the staging
// ring fills, flush() submits the transfer command buffer, blocks on its
// fence and reclaims the staging space - so a scene load makes forward
// progress with bounded staging memory even when the application renders no
// frames while loading (no frame completions ever arrive in that case).
// The destructor performs a final flush; the uploaded textures are safe to
// sample by anything submitted afterwards on the same queue.
class Image_transfer
{
public:
    explicit Image_transfer(erhe::graphics::Device& graphics_device);
    ~Image_transfer();
    Image_transfer(const Image_transfer&)  = delete;
    void operator=(const Image_transfer&)  = delete;
    Image_transfer(Image_transfer&&)       = delete;
    void operator=(Image_transfer&&)       = delete;

    // Stage the pixel data (level_count levels contiguous, largest-first,
    // tightly packed) and record the per-level copies to the texture.
    void upload(
        const erhe::graphics::Image_info&   image_info,
        std::span<const std::uint8_t>       pixels,
        erhe::graphics::Texture&            texture,
        bool                                generate_mipmap
    );

    // Submit the pending copies, wait for the GPU, reclaim staging space.
    void flush();

private:
    void record_copies(
        const erhe::graphics::Image_info& image_info,
        const erhe::graphics::Buffer&     source_buffer,
        std::size_t                       source_offset,
        erhe::graphics::Texture&          texture,
        bool                              generate_mipmap
    );
    [[nodiscard]] auto get_transfer_command_buffer() -> erhe::graphics::Command_buffer&;

    erhe::graphics::Device&         m_graphics_device;
    erhe::graphics::Ring_buffer     m_staging;
    erhe::graphics::Command_buffer* m_command_buffer{nullptr};
};

} // namespace erhe::gltf
