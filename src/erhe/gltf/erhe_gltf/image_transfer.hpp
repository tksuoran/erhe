#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_dataformat/dataformat.hpp"

namespace erhe::graphics {
    class Image_info;
    class Texture;
}

namespace erhe::gltf {

class Image_transfer
{
public:
    explicit Image_transfer(erhe::graphics::Device& graphics_device);

    [[nodiscard]] auto acquire_range(std::size_t byte_count) -> erhe::graphics::Buffer_range;

    void upload_to_texture(
        const erhe::graphics::Image_info& image_info,
        erhe::graphics::Buffer_range&     buffer_range,
        erhe::graphics::Texture&          texture,
        bool                              generate_mipmap
    );

private:
    erhe::graphics::Device&                m_graphics_device;
    erhe::graphics::GPU_ring_buffer_client m_texture_upload_buffer;
};

} // namespace erhe::gltf
