#include "graphics/textures.hpp"
#include "erhe_graphics/gl_context_provider.hpp"
#include "graphics/image_transfer.hpp"

#include "erhe_graphics/png_loader.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

using erhe::graphics::Texture;
using erhe::graphics::PNG_loader;
using erhe::log::Log;
using std::shared_ptr;

Textures::Textures()
{
}

Textures::~Textures() = default;

void Textures::connect()
{
    m_image_transfer = require<Image_transfer>();
    require<erhe::graphics::Gl_context_provider>();

    ERHE_VERIFY(m_image_transfer);
}

void Textures::initialize_component()
{
    ERHE_VERIFY(m_image_transfer);

    const Scoped_gl_context gl_context{Component::get<erhe::graphics::Gl_context_provider>()};

    background = load(std::filesystem::path("res") / "images" / "background.png");
}

gl::Internal_format to_gl(erhe::graphics::Image_format format)
{
    switch (format)
    {
        //using enum erhe::graphics::Image_format;
        case erhe::graphics::Image_format::rgb8:  return gl::Internal_format::rgb8;
        case erhe::graphics::Image_format::rgba8: return gl::Internal_format::rgba8;
        default:
        {
            ERHE_FATAL("Bad image format %04x\n", static_cast<unsigned int>(format));
        }
    }
    // std::unreachable() return gl::Internal_format::rgba8;
}

auto Textures::load(
    const std::filesystem::path& path
) -> shared_ptr<Texture>
{
    if (
        !std::filesystem::exists(path) ||
        std::filesystem::is_empty(path)
    )
    {
        return {};
    }

    erhe::graphics::Image_info image_info;
    PNG_loader                 loader;

    if (!loader.open(path, image_info))
    {
        return {};
    }

    auto& slot = m_image_transfer->get_slot();

    erhe::graphics::Texture_create_info texture_create_info{
        .internal_format = to_gl(image_info.format),
        .use_mipmaps     = (image_info.level_count > 1),
        .width           = image_info.width,
        .height          = image_info.height,
        .depth           = image_info.depth,
        .level_count     = image_info.level_count,
        .row_stride      = image_info.row_stride,
    };
    std::span<std::byte> span = slot.begin_span_for(
        image_info.width,
        image_info.height,
        texture_create_info.internal_format
    );

    bool ok = loader.load(span);
    loader.close();
    if (!ok)
    {
        slot.end();
        return {};
    }

    auto texture = std::make_shared<Texture>(texture_create_info);
    texture->set_debug_label(path.string());

    gl::flush_mapped_named_buffer_range(slot.gl_name(), 0, span.size_bytes());
    gl::pixel_store_i(gl::Pixel_store_parameter::unpack_alignment, 1);
    gl::bind_buffer(gl::Buffer_target::pixel_unpack_buffer, slot.gl_name());
    texture->upload(texture_create_info.internal_format, texture_create_info.width, texture_create_info.height);
    gl::bind_buffer(gl::Buffer_target::pixel_unpack_buffer, 0);
    slot.end();

    return texture;
}

}
