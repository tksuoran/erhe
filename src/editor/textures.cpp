#include "textures.hpp"
#include "erhe/graphics_experimental/image_transfer.hpp"
#include "erhe/graphics/png_loader.hpp"

namespace editor {

using erhe::graphics::Texture;
using erhe::graphics::PNG_loader;
using erhe::log::Log;
using std::shared_ptr;

void Textures::connect()
{
    m_image_transfer = require<erhe::graphics::ImageTransfer>();

    Ensures(m_image_transfer);
}

void Textures::initialize_component()
{
    Expects(m_image_transfer);

    background_texture = load(std::filesystem::path("res") / "images" / "background.png");
}

auto Textures::load(const std::filesystem::path& path)
-> shared_ptr<Texture>
{
    if (std::filesystem::exists(path) &&
        !std::filesystem::is_empty(path))
    {
        Texture::Create_info create_info;
        PNG_loader           loader;

        bool ok = loader.open(path, create_info);
        if (!ok)
        {
            return {};
        }

        auto& slot = m_image_transfer->get_slot();
        gsl::span<std::byte> span = slot.span_for(create_info.width,
                                                  create_info.height,
                                                  create_info.internal_format);

        ok = loader.load(span);
        loader.close();
        if (!ok)
        {
            return {};
        }

        auto texture = std::make_shared<Texture>(create_info);
        texture->set_debug_label(path.string());

        gl::flush_mapped_named_buffer_range(slot.gl_name(), 0, span.size_bytes());
        gl::pixel_store_i(gl::Pixel_store_parameter::unpack_alignment, 1);
        gl::bind_buffer(gl::Buffer_target::pixel_unpack_buffer, slot.gl_name());
        texture->upload(create_info.internal_format, create_info.width, create_info.height);
        gl::bind_buffer(gl::Buffer_target::pixel_unpack_buffer, 0);

        return texture;
    }

    return {};
}

}
