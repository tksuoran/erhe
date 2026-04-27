#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_dataformat/dataformat.hpp"

namespace erhe::graphics {
    class Command_buffer;
    class Image_info;
    class Ring_buffer_range;
    class Texture;
}

namespace erhe::gltf {

// Records glTF texture uploads into a caller-supplied init command
// buffer. The caller (the glTF parse driver) creates the cb, owns
// its lifetime, and is responsible for ending + submitting it (and
// waiting on the GPU) before the uploaded textures are sampled.
class Image_transfer
{
public:
    Image_transfer(
        erhe::graphics::Device&         graphics_device,
        erhe::graphics::Command_buffer& init_command_buffer
    );

    [[nodiscard]] auto acquire_range(std::size_t byte_count) -> erhe::graphics::Ring_buffer_range;

    void upload_to_texture(
        const erhe::graphics::Image_info&  image_info,
        erhe::graphics::Ring_buffer_range& buffer_range,
        erhe::graphics::Texture&           texture,
        bool                               generate_mipmap
    );

private:
    erhe::graphics::Device&            m_graphics_device;
    erhe::graphics::Ring_buffer_client m_texture_upload_buffer;
    erhe::graphics::Command_buffer&    m_init_command_buffer;
};

} // namespace erhe::gltf
