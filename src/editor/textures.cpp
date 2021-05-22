#include "textures.hpp"
#include "erhe/graphics_experimental/image_transfer.hpp"
#include "erhe/graphics/png_loader.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/toolkit/verify.hpp"

namespace editor {

using erhe::graphics::Texture;
using erhe::graphics::PNG_loader;
using erhe::log::Log;
using std::shared_ptr;

void Textures::connect()
{
    m_image_transfer = require<erhe::graphics::Image_transfer>();

    Ensures(m_image_transfer);
}

void Textures::initialize_component()
{
    Expects(m_image_transfer);

    background_texture = load(std::filesystem::path("res") / "images" / "background.png");
}

gl::Internal_format to_gl(erhe::graphics::Image_format format)
{
    switch (format)
    {
        case erhe::graphics::Image_format::rgb8:  return gl::Internal_format::rgb8;
        case erhe::graphics::Image_format::rgba8: return gl::Internal_format::rgba8;
        default:
            FATAL("Bad image format\n");
    }
    return gl::Internal_format::rgba8;
}

auto Textures::load(const std::filesystem::path& path)
-> shared_ptr<Texture>
{
    if (std::filesystem::exists(path) &&
        !std::filesystem::is_empty(path))
    {
        erhe::graphics::Image_info image_info;
        PNG_loader                 loader;

        bool ok = loader.open(path, image_info);
        if (!ok)
        {
            return {};
        }

        auto& slot = m_image_transfer->get_slot();

        Texture::Create_info texture_create_info;
        texture_create_info.width           = image_info.width;
        texture_create_info.height          = image_info.height;
        texture_create_info.depth           = image_info.depth;
        texture_create_info.level_count     = image_info.level_count;
        texture_create_info.row_stride      = image_info.row_stride;
        texture_create_info.use_mipmaps     = texture_create_info.level_count > 1;
        texture_create_info.internal_format = to_gl(image_info.format);
        gsl::span<std::byte> span = slot.span_for(image_info.width,
                                                  image_info.height,
                                                  texture_create_info.internal_format);

        ok = loader.load(span);
        loader.close();
        if (!ok)
        {
            return {};
        }

        auto texture = std::make_shared<Texture>(texture_create_info);
        texture->set_debug_label(path.string());

        gl::flush_mapped_named_buffer_range(slot.gl_name(), 0, span.size_bytes());
        gl::pixel_store_i(gl::Pixel_store_parameter::unpack_alignment, 1);
        gl::bind_buffer(gl::Buffer_target::pixel_unpack_buffer, slot.gl_name());
        texture->upload(texture_create_info.internal_format, texture_create_info.width, texture_create_info.height);
        gl::bind_buffer(gl::Buffer_target::pixel_unpack_buffer, 0);

        return texture;
    }

    return {};
}

}
