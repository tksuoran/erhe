#include "graphics/textures.hpp"
#include "graphics/gl_context_provider.hpp"
#include "graphics/image_transfer.hpp"

#include "erhe/graphics/png_loader.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/toolkit/verify.hpp"

namespace editor {

using erhe::graphics::Texture;
using erhe::graphics::PNG_loader;
using erhe::log::Log;
using std::shared_ptr;

Textures::Textures()
    : erhe::components::Component{c_name}
{
}

Textures::~Textures() = default;

void Textures::connect()
{
    m_image_transfer = require<Image_transfer>();
    require<Gl_context_provider>();

    Ensures(m_image_transfer);
}

void Textures::initialize_component()
{
    Expects(m_image_transfer);

    Scoped_gl_context gl_context{Component::get<Gl_context_provider>().get()};

    background  = load(std::filesystem::path("res") / "images" / "background.png");
    camera_icon = load(std::filesystem::path("res") / "icons" / "camera16.png");
    mesh_icon   = load(std::filesystem::path("res") / "icons" / "mesh16.png");
    light_icon  = load(std::filesystem::path("res") / "icons" / "light16.png");
}

gl::Internal_format to_gl(erhe::graphics::Image_format format)
{
    switch (format)
    {
        case erhe::graphics::Image_format::rgb8:  return gl::Internal_format::rgb8;
        case erhe::graphics::Image_format::rgba8: return gl::Internal_format::rgba8;
        default:
        {
            FATAL("Bad image format\n");
        }
    }
    // unreachable return gl::Internal_format::rgba8;
}

auto Textures::load(const std::filesystem::path& path)
-> shared_ptr<Texture>
{
    if (!std::filesystem::exists(path) ||
        std::filesystem::is_empty(path))
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

    Texture::Create_info texture_create_info;
    texture_create_info.width           = image_info.width;
    texture_create_info.height          = image_info.height;
    texture_create_info.depth           = image_info.depth;
    texture_create_info.level_count     = image_info.level_count;
    texture_create_info.row_stride      = image_info.row_stride;
    texture_create_info.use_mipmaps     = texture_create_info.level_count > 1;
    texture_create_info.internal_format = to_gl(image_info.format);
    gsl::span<std::byte> span = slot.span_for(
        image_info.width,
        image_info.height,
        texture_create_info.internal_format
    );

    bool ok = loader.load(span);
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

}
