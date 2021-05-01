#include "erhe/ui/ninepatch_style.hpp"
#include "erhe/graphics/png_loader.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/gl/gl.hpp"
#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/ui/gui_renderer.hpp"
#include "erhe/ui/log.hpp"
#include "erhe/ui/style.hpp"

using erhe::graphics::Texture;
using erhe::graphics::Shader_stages;
using erhe::graphics::PNG_loader;

namespace erhe::ui
{

Ninepatch_style::Ninepatch_style(Gui_renderer&                gui_renderer,
                                 const std::filesystem::path& texture_path,
                                 Shader_stages*               program,
                                 size_t                       texture_unit)
    : program     {program}
    , texture_unit{texture_unit}
    , border_uv   {0.25f, 0.25f}
{
    log_ninepatch_style.trace("Ninepatch_style::Ninepatch_style(path = {})\n",
                              texture_path.string());

    if (std::filesystem::exists(texture_path) &&
        !std::filesystem::is_empty(texture_path))
    {
        Texture::Create_info create_info;
        PNG_loader           loader;

        bool ok = loader.open(texture_path, create_info);
        if (!ok)
        {
            return;
        }

        auto& image_transfer = gui_renderer.image_transfer();
        auto& slot = image_transfer.get_slot();
        auto span = slot.span_for(create_info.width, create_info.height, create_info.internal_format);
        ok = loader.load(span);
        loader.close();
        if (!ok)
        {
            return;
        }

        texture = std::make_unique<Texture>(create_info);
        texture->set_debug_label(texture_path.string());

        gl::bind_buffer(gl::Buffer_target::pixel_unpack_buffer, slot.gl_name());
        texture->upload(create_info.width, create_info.height, create_info.internal_format);
        gl::bind_buffer(gl::Buffer_target::pixel_unpack_buffer, 0);

        border_pixels.x = border_uv.x * static_cast<float>(texture->width());
        border_pixels.y = border_uv.y * static_cast<float>(texture->height());
    }
}

} // namespace erhe::ui
