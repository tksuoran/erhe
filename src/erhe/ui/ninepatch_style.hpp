#ifndef ninepatch_style_hpp_erhe_ui
#define ninepatch_style_hpp_erhe_ui

#include "erhe/graphics/texture.hpp"
#include <filesystem>
#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace erhe::graphics
{

class Shader_stages;

} // namespace erhe::graphics

namespace erhe::ui
{

class Gui_renderer;

class Ninepatch_style
{
public:
    Ninepatch_style(Gui_renderer&                  gui_renderer,
                    const std::filesystem::path&   texture_path,
                    erhe::graphics::Shader_stages* program,
                    size_t                         texture_unit);

    std::unique_ptr<erhe::graphics::Texture> texture;
    erhe::graphics::Shader_stages*           program      {nullptr};
    size_t                                   texture_unit {0};
    glm::vec2                                border_uv    {0.0f};
    glm::vec2                                border_pixels{0.0f};
};

} // namespace erhe::ui

#endif
